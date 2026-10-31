// Copyright (c) 2023-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <interfaces/init.h>
#include <ipc/capnp/mining.capnp.h>
#include <ipc/capnp/protocol.h>
#include <ipc/process.h>
#include <ipc/protocol.h>
#include <ipc/test/ipc_test.capnp.h>
#include <ipc/test/ipc_test.capnp.proxy.h>
#include <ipc/test/ipc_test.h>
#include <mp/proxy-types.h>
#include <node/miner.h>
#include <pow.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <test/util/txmempool.h>
#include <tinyformat.h>
#include <util/log.h>
#include <validation.h>

#include <future>
#include <thread>
#include <kj/common.h>
#include <kj/memory.h>
#include <kj/test.h>
#include <stdexcept>

#include <boost/test/unit_test.hpp>

static_assert(ipc::capnp::messages::MAX_MONEY == MAX_MONEY);
static_assert(ipc::capnp::messages::MAX_DOUBLE == std::numeric_limits<double>::max());
static_assert(ipc::capnp::messages::DEFAULT_BLOCK_RESERVED_WEIGHT == DEFAULT_BLOCK_RESERVED_WEIGHT);
static_assert(ipc::capnp::messages::DEFAULT_COINBASE_OUTPUT_MAX_ADDITIONAL_SIGOPS == DEFAULT_COINBASE_OUTPUT_MAX_ADDITIONAL_SIGOPS);

//! Remote init class.
class TestInit : public interfaces::Init
{
    node::NodeContext* m_node;
public:
    explicit TestInit(node::NodeContext* node = nullptr) : m_node{node} {}
    std::unique_ptr<interfaces::Echo> makeEcho() override { return interfaces::MakeEcho(); }
    std::unique_ptr<interfaces::Mining> makeMining() override { return m_node ? interfaces::MakeMining(*m_node, /*wait_loaded=*/false) : nullptr; }
};

//! Generate a temporary path with temp_directory_path and mkstemp
static std::string TempPath(std::string_view pattern)
{
    std::string temp{fs::PathToString(fs::path{fs::temp_directory_path()} / fs::PathFromString(std::string{pattern}))};
    temp.push_back('\0');
    int fd{mkstemp(temp.data())};
    BOOST_CHECK_GE(fd, 0);
    BOOST_CHECK_EQUAL(close(fd), 0);
    temp.resize(temp.size() - 1);
    fs::remove(fs::PathFromString(temp));
    return temp;
}

//! Unit test that tests execution of IPC calls without actually creating a
//! separate process. This test is primarily intended to verify behavior of type
//! conversion code that converts C++ objects to Cap'n Proto messages and vice
//! versa.
//!
//! The test creates a thread which creates a FooImplementation object (defined
//! in ipc_test.h) and a two-way pipe accepting IPC requests which call methods
//! on the object through FooInterface (defined in ipc_test.capnp).
void IpcPipeTest()
{
    // Setup: create FooImplementation object and listen for FooInterface requests
    std::promise<std::unique_ptr<mp::ProxyClient<gen::FooInterface>>> foo_promise;
    std::thread thread([&]() {
        mp::EventLoop loop("IpcPipeTest", [](bool raise, const std::string& log) { LogInfo("LOG%i: %s", raise, log); });
        auto pipe = loop.m_io_context.provider->newTwoWayPipe();

        auto connection_client = std::make_unique<mp::Connection>(loop, kj::mv(pipe.ends[0]));
        auto foo_client = std::make_unique<mp::ProxyClient<gen::FooInterface>>(
            connection_client->m_rpc_system->bootstrap(mp::ServerVatId().vat_id).castAs<gen::FooInterface>(),
            connection_client.get(), /* destroy_connection= */ true);
        (void)connection_client.release();
        foo_promise.set_value(std::move(foo_client));

        auto connection_server = std::make_unique<mp::Connection>(loop, kj::mv(pipe.ends[1]), [&](mp::Connection& connection) {
            auto foo_server = kj::heap<mp::ProxyServer<gen::FooInterface>>(std::make_shared<FooImplementation>(), connection);
            return capnp::Capability::Client(kj::mv(foo_server));
        });
        connection_server->onDisconnect([&] { connection_server.reset(); });
        loop.loop();
    });
    std::unique_ptr<mp::ProxyClient<gen::FooInterface>> foo{foo_promise.get_future().get()};

    // Test: make sure arguments were sent and return value is received
    BOOST_CHECK_EQUAL(foo->add(1, 2), 3);

    COutPoint txout1{Txid::FromUint256(uint256{100}), 200};
    COutPoint txout2{foo->passOutPoint(txout1)};
    BOOST_CHECK(txout1 == txout2);

    UniValue uni1{UniValue::VOBJ};
    uni1.pushKV("i", 1);
    uni1.pushKV("s", "two");
    UniValue uni2{foo->passUniValue(uni1)};
    BOOST_CHECK_EQUAL(uni1.write(), uni2.write());

    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.nLockTime = 3;
    mtx.vin.emplace_back(txout1);
    mtx.vout.emplace_back(COIN, CScript());
    CTransactionRef tx1{MakeTransactionRef(mtx)};
    CTransactionRef tx2{foo->passTransaction(tx1)};
    BOOST_CHECK(Assert(tx1)->Equals(*Assert(tx2)));

    std::vector<CTransactionRef> txs1;
    txs1.push_back(tx1);
    txs1.push_back(nullptr);
    std::vector<CTransactionRef> txs2(foo->passTransactions(txs1));
    BOOST_CHECK_EQUAL(txs2.size(), 2);
    BOOST_CHECK(Assert(txs1[0])->Equals(*Assert(txs2[0])));
    BOOST_CHECK(!txs2[1]);

    std::vector<char> vec1{'H', 'e', 'l', 'l', 'o'};
    std::vector<char> vec2{foo->passVectorChar(vec1)};
    BOOST_CHECK_EQUAL(std::string_view(vec1.begin(), vec1.end()), std::string_view(vec2.begin(), vec2.end()));

    auto script1{CScript() << OP_11};
    auto script2{foo->passScript(script1)};
    BOOST_CHECK_EQUAL(HexStr(script1), HexStr(script2));

    // Test cleanup: disconnect and join thread
    foo.reset();
    thread.join();
}

//! Test ipc::Protocol connect() and serve() methods connecting over a socketpair.
void IpcSocketPairTest()
{
    std::unique_ptr<interfaces::Init> init{std::make_unique<TestInit>()};
    std::unique_ptr<ipc::Protocol> protocol{ipc::capnp::MakeCapnpProtocol("IpcSocketPairTest")};
    mp::Stream client_stream;
    std::promise<void> promise;
    std::thread thread([&]() {
        protocol->serve(*init, [&] {
            auto pair{mp::SocketPair()};
            client_stream = protocol->makeStream(pair[0]);
            promise.set_value();
            return protocol->makeStream(pair[1]);
        });
    });
    promise.get_future().wait();
    std::unique_ptr<interfaces::Init> remote_init{protocol->connect(std::move(client_stream))};
    std::unique_ptr<interfaces::Echo> remote_echo{remote_init->makeEcho()};
    BOOST_CHECK_EQUAL(remote_echo->echo("echo test"), "echo test");
    remote_echo.reset();
    remote_init.reset();
    thread.join();
}

//! Test ipc::Process bind() and connect() methods connecting over a unix socket.
void IpcSocketTest(const fs::path& datadir)
{
    std::unique_ptr<interfaces::Init> init{std::make_unique<TestInit>()};
    std::unique_ptr<ipc::Protocol> protocol{ipc::capnp::MakeCapnpProtocol("IpcSocketTest")};
    std::unique_ptr<ipc::Process> process{ipc::MakeProcess()};

    std::string invalid_bind{"invalid:"};
    BOOST_CHECK_THROW(process->bind(datadir, "test_bitcoin", invalid_bind), std::invalid_argument);
    BOOST_CHECK_THROW(process->connect(datadir, "test_bitcoin", invalid_bind), std::invalid_argument);

    auto bind_and_listen{[&](const std::string& bind_address) {
        std::string address{bind_address};
        mp::SocketId serve_fd = process->bind(datadir, "test_bitcoin", address);
        BOOST_CHECK_NE(serve_fd, mp::SocketError);
        BOOST_CHECK_EQUAL(address, bind_address);
        protocol->listen(serve_fd, *init);
    }};

    auto connect_and_test{[&](const std::string& connect_address) {
        std::string address{connect_address};
        mp::SocketId connect_fd{process->connect(datadir, "test_bitcoin", address)};
        BOOST_CHECK_EQUAL(address, connect_address);
        std::unique_ptr<interfaces::Init> remote_init{protocol->connect(protocol->makeStream(connect_fd))};
        std::unique_ptr<interfaces::Echo> remote_echo{remote_init->makeEcho()};
        BOOST_CHECK_EQUAL(remote_echo->echo("echo test"), "echo test");
    }};

    // Need to specify explicit socket addresses outside the data directory, because the data
    // directory path is so long that the default socket address and any other
    // addresses in the data directory would fail with errors like:
    //   Address 'unix' path '"/tmp/test_common_Bitcoin Core/ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff/test_bitcoin.sock"' exceeded maximum socket path length
    std::vector<std::string> addresses{
        strprintf("unix:%s", TempPath("bitcoin_sock0_XXXXXX")),
        strprintf("unix:%s", TempPath("bitcoin_sock1_XXXXXX")),
    };

    // Bind and listen on multiple addresses
    for (const auto& address : addresses) {
        bind_and_listen(address);
    }

    // Connect and test each address multiple times.
    for (int i : {0, 1, 0, 0, 1}) {
        connect_and_test(addresses[i]);
    }
}

BOOST_FIXTURE_TEST_SUITE(ipc_tests, BasicTestingSetup)
// Exercise generated mining proxies over an actual socket pair without Python
// bindings. This is a C++ client profile; Python lifecycle/concurrency coverage
// remains separately qualified by interface_ipc_mining.py.
BOOST_FIXTURE_TEST_CASE(mining_socketpair, TestChain100Setup)
{
    const auto tx{MakeTransactionRef(CreateValidMempoolTransaction(
        m_coinbase_txns[0], 0, 1, coinbaseKey, CScript() << OP_TRUE,
        m_coinbase_txns[0]->vout[0].nValue - 1000, /*submit=*/false))};
    {
        LOCK2(cs_main, m_node.mempool->cs);
        TryAddToMempool(*m_node.mempool, TestMemPoolEntryHelper{}.Fee(1000).SigOpsCost(0).SpendsCoinbase(true).FromTx(tx));
    }
    TestInit init{&m_node};
    auto protocol{ipc::capnp::MakeCapnpProtocol("MiningSocketPairTest")};
    mp::Stream client_stream;
    std::promise<void> ready;
    struct JoinedThread {
        std::thread thread;
        ~JoinedThread() { thread.join(); }
    };
    // Declare the thread before remote objects, so exception unwinding closes
    // all client capabilities before joining the server.
    JoinedThread server{std::thread{[&] {
        protocol->serve(init, [&] {
            auto pair{mp::SocketPair()};
            client_stream = protocol->makeStream(pair[0]);
            ready.set_value();
            return protocol->makeStream(pair[1]);
        });
    }}};
    ready.get_future().wait();
    auto remote_init{protocol->connect(std::move(client_stream))};
    auto mining{remote_init->makeMining()};
    BOOST_REQUIRE(mining);
    auto fails_with = [](const std::string& expected) {
        return [expected](const std::exception& error) { return std::string{error.what()}.find(expected) != std::string::npos; };
    };
    BOOST_CHECK_EXCEPTION(remote_init->makeMiningOld2(), std::exception,
                          fails_with("Old mining interface (@2) not supported. Please update your client!"));

    node::BlockCreateOptions request;
    // Native wire fields always supply reserved weight, even when a C++ client
    // leaves its optional field unset. Server-only policy options stay unset.
    m_node.mining_args.block_reserved_weight = MAX_BLOCK_WEIGHT;
    m_node.mining_args.block_max_weight = MAX_BLOCK_WEIGHT;
    m_node.mining_args.block_min_fee_rate = CFeeRate{20'000};
    m_node.mining_args.block_priority_size = 0;
    auto fee_only{mining->createNewBlock(request, /*cooldown=*/false)};
    BOOST_REQUIRE(fee_only);
    BOOST_CHECK_EQUAL(fee_only->getBlock().vtx.size(), 1U);
    m_node.mining_args.block_priority_size = DEFAULT_BLOCK_PRIORITY_SIZE;
    auto priority{mining->createNewBlock(request, /*cooldown=*/false)};
    BOOST_REQUIRE(priority);
    BOOST_CHECK_EQUAL(priority->getBlock().vtx.size(), 2U);
    BOOST_CHECK(priority->getTxFees() == std::vector<CAmount>{1000});
    BOOST_CHECK(priority->getTxSigops() == std::vector<int64_t>{0});
    BOOST_CHECK_EQUAL(priority->getCoinbaseTx().block_reward_remaining, 50 * COIN + 1000);
    // Existing templates retain their resolved policy across waitNext(), even
    // if a later request uses different server defaults.
    auto still_fee_only{fee_only->waitNext({.timeout = MillisecondsDouble{0}, .fee_threshold = 0})};
    BOOST_REQUIRE(still_fee_only);
    BOOST_CHECK_EQUAL(still_fee_only->getBlock().vtx.size(), 1U);
    m_node.mining_args.block_priority_size = 0;
    auto still_priority{priority->waitNext({.timeout = MillisecondsDouble{0}, .fee_threshold = 0})};
    BOOST_REQUIRE(still_priority);
    BOOST_CHECK_EQUAL(still_priority->getBlock().vtx.size(), 2U);

    m_node.mining_args.block_min_fee_rate = CFeeRate{0};
    m_node.mining_args.block_max_size = DEFAULT_BLOCK_RESERVED_SIZE + GetSerializeSize(TX_WITH_WITNESS(*tx));
    BOOST_CHECK_EQUAL(mining->createNewBlock(request, false)->getBlock().vtx.size(), 1U);
    ++*m_node.mining_args.block_max_size;
    BOOST_CHECK_EQUAL(mining->createNewBlock(request, false)->getBlock().vtx.size(), 2U);
    m_node.mining_args.block_max_size.reset();
    m_node.mining_args.block_max_weight = DEFAULT_BLOCK_RESERVED_WEIGHT + GetTransactionWeight(*tx);
    BOOST_CHECK_EQUAL(mining->createNewBlock(request, false)->getBlock().vtx.size(), 1U);
    ++*m_node.mining_args.block_max_weight;
    BOOST_CHECK_EQUAL(mining->createNewBlock(request, false)->getBlock().vtx.size(), 2U);
    m_node.mining_args.block_max_weight = MAX_BLOCK_WEIGHT;
    request.use_mempool = false;
    BOOST_CHECK_EQUAL(mining->createNewBlock(request, false)->getBlock().vtx.size(), 1U);
    request.use_mempool = true;
    request.block_reserved_weight = MAX_BLOCK_WEIGHT;
    BOOST_CHECK_EQUAL(mining->createNewBlock(request, false)->getBlock().vtx.size(), 1U);
    request.block_reserved_weight = 0;
    BOOST_CHECK_EXCEPTION(mining->createNewBlock(request, false), std::exception,
                          fails_with("block_reserved_weight (0) is lower than minimum safety value of (2000)"));
    request.block_reserved_weight = MAX_BLOCK_WEIGHT + 1;
    BOOST_CHECK_EXCEPTION(mining->createNewBlock(request, false), std::exception,
                          fails_with("exceeds consensus maximum block weight (4000000)"));
    request.block_reserved_weight = DEFAULT_BLOCK_RESERVED_WEIGHT;
    request.coinbase_output_max_additional_sigops = MAX_BLOCK_SIGOPS_COST + 1;
    BOOST_CHECK_EXCEPTION(mining->createNewBlock(request, false), std::exception,
                          fails_with("coinbase_output_max_additional_sigops (80001) exceeds consensus maximum block sigops cost (80000)"));
    request.coinbase_output_max_additional_sigops = MAX_BLOCK_SIGOPS_COST;
    BOOST_CHECK_EQUAL(mining->createNewBlock(request, false)->getBlock().vtx.size(), 1U);
    request.coinbase_output_max_additional_sigops = DEFAULT_COINBASE_OUTPUT_MAX_ADDITIONAL_SIGOPS;

    // Construct the client coinbase from the current v32 schema, including its
    // mandatory witness/output/locktime fields, rather than using the dummy.
    auto make_coinbase = [](const node::CoinbaseTx& fields) {
        CMutableTransaction coinbase;
        coinbase.version = fields.version;
        coinbase.vin.resize(1);
        coinbase.vin[0].prevout.SetNull();
        coinbase.vin[0].nSequence = fields.sequence;
        coinbase.vin[0].scriptSig = fields.script_sig_prefix;
        coinbase.vin[0].scriptSig << OP_0;
        if (fields.witness) coinbase.vin[0].scriptWitness.stack.emplace_back(fields.witness->begin(), fields.witness->end());
        coinbase.vout.emplace_back(fields.block_reward_remaining, CScript() << OP_TRUE);
        coinbase.vout.insert(coinbase.vout.end(), fields.required_outputs.begin(), fields.required_outputs.end());
        coinbase.nLockTime = fields.lock_time;
        return MakeTransactionRef(coinbase);
    };
    const auto coinbase{make_coinbase(priority->getCoinbaseTx())};
    CBlock block{priority->getBlock()};
    node::AddMerkleRootAndCoinbase(block, coinbase, block.nVersion, block.nTime, block.nNonce);
    auto solve = [&](CBlock& candidate) {
        while (!CheckProofOfWork(candidate.GetHash(), candidate.nBits, m_node.chainman->GetConsensus())) ++candidate.nNonce;
    };
    CBlock invalid{block};
    invalid.nVersion = 0;
    solve(invalid);
    std::string reason, debug;
    BOOST_CHECK(!mining->checkBlock(invalid, {}, reason, debug));
    BOOST_CHECK_EQUAL(reason, "bad-version(0x00000000)");
    BOOST_CHECK_EQUAL(debug, "rejected nVersion=0x00000000 block");
    BOOST_CHECK(!mining->submitBlock(invalid, reason, debug));
    BOOST_CHECK_EQUAL(reason, "bad-version(0x00000000)");
    BOOST_CHECK_EQUAL(debug, "rejected nVersion=0x00000000 block");
    invalid = block;
    CMutableTransaction nonfinal{*tx};
    nonfinal.nLockTime = 1000;
    nonfinal.vin[0].nSequence = 0;
    invalid.vtx[1] = MakeTransactionRef(nonfinal);
    node::RegenerateCommitments(invalid, *m_node.chainman);
    solve(invalid);
    BOOST_CHECK(!mining->submitBlock(invalid, reason, debug));
    BOOST_CHECK_EQUAL(reason, "bad-txns-nonfinal");
    BOOST_CHECK_EQUAL(debug, "non-final transaction");
    BOOST_CHECK(!mining->submitBlock(invalid, reason, debug));
    BOOST_CHECK_EQUAL(reason, "duplicate-invalid");
    BOOST_CHECK_EQUAL(debug, strprintf("block %s was previously marked invalid", invalid.GetHash().ToString()));
    solve(block);
    BOOST_REQUIRE(mining->checkBlock(block, {}, reason, debug));
    BOOST_CHECK(reason.empty() && debug.empty());
    BOOST_CHECK_EXCEPTION(priority->submitSolutionOld7(0, 0, 0, {}), std::exception,
                          fails_with("Old submitSolution (@7) not supported. Please update your client!"));
    BOOST_REQUIRE(priority->submitSolution(block.nVersion, block.nTime, block.nNonce, coinbase, reason, debug));
    BOOST_CHECK(reason.empty() && debug.empty());
    BOOST_CHECK(!mining->submitBlock(block, reason, debug));
    BOOST_CHECK_EQUAL(reason, "duplicate");
    BOOST_CHECK(debug.empty());
    BOOST_CHECK(!priority->submitSolution(block.nVersion, block.nTime, block.nNonce, coinbase, reason, debug));
    BOOST_CHECK_EQUAL(reason, "duplicate");
    BOOST_CHECK(debug.empty());
    auto next{mining->createNewBlock(request, false)};
    BOOST_REQUIRE(next);
    const auto next_coinbase{make_coinbase(next->getCoinbaseTx())};
    block = next->getBlock();
    node::AddMerkleRootAndCoinbase(block, next_coinbase, block.nVersion, block.nTime, block.nNonce);
    solve(block);
    BOOST_REQUIRE(mining->submitBlock(block, reason, debug));
    BOOST_CHECK(reason.empty() && debug.empty());
    BOOST_CHECK(!next->submitSolution(block.nVersion, block.nTime, block.nNonce, next_coinbase, reason, debug));
    BOOST_CHECK_EQUAL(reason, "duplicate");
    BOOST_CHECK(debug.empty());
}

BOOST_AUTO_TEST_CASE(ipc_tests)
{
    IpcPipeTest();
    IpcSocketPairTest();
    IpcSocketTest(m_args.GetDataDirNet());
}

// Test address parsing.
BOOST_AUTO_TEST_CASE(parse_address_test)
{
    std::unique_ptr<ipc::Process> process{ipc::MakeProcess()};
    fs::path datadir{"/var/empty/notexist"};
    auto check_notexist{[](const std::system_error& e) { return e.code() == std::errc::no_such_file_or_directory; }};
    auto check_address{[&](std::string address, std::string expect_address, std::string expect_error) {
        if (expect_error.empty()) {
            BOOST_CHECK_EXCEPTION(process->connect(datadir, "test_bitcoin", address), std::system_error, check_notexist);
        } else {
            BOOST_CHECK_EXCEPTION(process->connect(datadir, "test_bitcoin", address), std::invalid_argument, HasReason(expect_error));
        }
        BOOST_CHECK_EQUAL(address, expect_address);
    }};
    std::string prefix{fs::PathToString(datadir / "")};
    check_address("unix", "unix:" + prefix + "test_bitcoin.sock", "");
    check_address("unix:", "unix:" + prefix + "test_bitcoin.sock", "");
    check_address("unix:path.sock", "unix:" + prefix + "path.sock", "");
    check_address("unix:0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000.sock",
                  "unix:" + prefix + "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000.sock",
                  "Unix address path \"" + prefix + "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000.sock\" exceeded maximum socket path length");
    check_address("invalid", "invalid", "Unrecognized address 'invalid'");
}

BOOST_AUTO_TEST_SUITE_END()
