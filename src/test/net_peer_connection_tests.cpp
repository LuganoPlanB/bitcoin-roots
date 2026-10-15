// Copyright (c) 2023-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <compat/compat.h>
#include <net.h>
#include <net_processing.h>
#include <netaddress.h>
#include <netbase.h>
#include <netgroup.h>
#include <node/connection_types.h>
#include <node/protocol_version.h>
#include <protocol.h>
#include <random.h>
#include <test/util/logging.h>
#include <test/util/net.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <tinyformat.h>
#include <util/chaintype.h>
#include <util/translation.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>

struct LogIPsTestingSetup : public TestingSetup {
    LogIPsTestingSetup()
        : TestingSetup{ChainType::MAIN, {.extra_args = {"-logips"}}} {}
};

struct MockSocketTestingSetup : LogIPsTestingSetup {
    const decltype(CreateSock) m_original_create_sock{CreateSock};
    const bool m_socks5_interrupted{g_socks5_interrupt.interrupted()};
    MockSocketTestingSetup() { g_socks5_interrupt.reset(); }
    ~MockSocketTestingSetup()
    {
        CreateSock = m_original_create_sock;
        if (m_socks5_interrupted) g_socks5_interrupt();
        else g_socks5_interrupt.reset();
    }
};

BOOST_FIXTURE_TEST_SUITE(net_peer_connection_tests, LogIPsTestingSetup)

static CService ip(uint32_t i)
{
    struct in_addr s;
    s.s_addr = i;
    return CService{CNetAddr{s}, Params().GetDefaultPort()};
}

struct PeerTest : LogIPsTestingSetup {
/** Create a peer and connect to it. If the optional `address` (IP/CJDNS only) isn't passed, a random address is created. */
void AddPeer(NodeId& id, std::vector<CNode*>& nodes, PeerManager& peerman, ConnmanTestMsg& connman, ConnectionType conn_type, bool onion_peer = false, std::optional<std::string> address = std::nullopt)
{
    CAddress addr{};

    if (address.has_value()) {
        addr = CAddress{MaybeFlipIPv6toCJDNS(LookupNumeric(address.value(), Params().GetDefaultPort())), NODE_NONE};
    } else if (onion_peer) {
        auto tor_addr{m_rng.randbytes(ADDR_TORV3_SIZE)};
        BOOST_REQUIRE(addr.SetSpecial(OnionToString(tor_addr)));
    }

    while (!addr.IsLocal() && !addr.IsRoutable()) {
        addr = CAddress{ip(m_rng.randbits(32)), NODE_NONE};
    }

    BOOST_REQUIRE(addr.IsValid());

    const bool inbound_onion{onion_peer && conn_type == ConnectionType::INBOUND};

    nodes.emplace_back(new CNode{++id,
                                 /*sock=*/nullptr,
                                 addr,
                                 /*nKeyedNetGroupIn=*/0,
                                 /*nLocalHostNonceIn=*/0,
                                 CAddress{},
                                 /*addrNameIn=*/"",
                                 conn_type,
                                 /*inbound_onion=*/inbound_onion,
                                 /*network_key=*/0});
    CNode& node = *nodes.back();
    node.SetCommonVersion(PROTOCOL_VERSION);

    peerman.InitializeNode(node, ServiceFlags(NODE_NETWORK | NODE_WITNESS));
    node.fSuccessfullyConnected = true;

    connman.AddTestNode(node);
}
}; // struct PeerTest

BOOST_FIXTURE_TEST_CASE(test_addnode_getaddednodeinfo_and_connection_detection, PeerTest)
{
    auto connman = std::make_unique<ConnmanTestMsg>(0x1337, 0x1337, *m_node.addrman, *m_node.netgroupman, Params());
    auto peerman = PeerManager::make(*connman, *m_node.addrman, nullptr, *m_node.chainman, *m_node.mempool, *m_node.warnings, {});
    NodeId id{0};
    std::vector<CNode*> nodes;

    // Connect a localhost peer.
    {
        ASSERT_DEBUG_LOG("Added connection to 127.0.0.1:8333 peer=1");
        AddPeer(id, nodes, *peerman, *connman, ConnectionType::MANUAL, /*onion_peer=*/false, /*address=*/"127.0.0.1");
        BOOST_REQUIRE(nodes.back() != nullptr);
    }

    // Call ConnectNode(), which is also called by RPC addnode onetry, for a localhost
    // address that resolves to multiple IPs, including that of the connected peer.
    // The connection attempt should consistently fail due to the check in ConnectNode().
    for (int i = 0; i < 10; ++i) {
        ASSERT_DEBUG_LOG("Not opening a connection to localhost, already connected to 127.0.0.1:8333");
        BOOST_CHECK(!connman->ConnectNodePublic(*peerman, "localhost", ConnectionType::MANUAL));
    }

    // Add 3 more peer connections.
    AddPeer(id, nodes, *peerman, *connman, ConnectionType::OUTBOUND_FULL_RELAY);
    AddPeer(id, nodes, *peerman, *connman, ConnectionType::BLOCK_RELAY, /*onion_peer=*/true);
    AddPeer(id, nodes, *peerman, *connman, ConnectionType::INBOUND);

    // Add a CJDNS peer connection.
    AddPeer(id, nodes, *peerman, *connman, ConnectionType::INBOUND, /*onion_peer=*/false,
            /*address=*/"[fc00:3344:5566:7788:9900:aabb:ccdd:eeff]:1234");
    BOOST_CHECK(nodes.back()->IsInboundConn());
    BOOST_CHECK_EQUAL(nodes.back()->ConnectedThroughNetwork(), Network::NET_CJDNS);

    BOOST_TEST_MESSAGE("Call AddNode() for all the peers");
    for (auto node : connman->TestNodes()) {
        BOOST_CHECK(connman->AddNode({/*m_added_node=*/node->addr.ToStringAddrPort(), /*m_use_v2transport=*/true}));
        BOOST_TEST_MESSAGE(strprintf("peer id=%s addr=%s", node->GetId(), node->addr.ToStringAddrPort()));
    }

    BOOST_TEST_MESSAGE("\nCall AddNode() with 2 addrs resolving to existing localhost addnode entry; neither should be added");
    BOOST_CHECK(!connman->AddNode({/*m_added_node=*/"127.0.0.1", /*m_use_v2transport=*/true}));
    // OpenBSD doesn't support the IPv4 shorthand notation with omitted zero-bytes.
#if !defined(__OpenBSD__)
    BOOST_CHECK(!connman->AddNode({/*m_added_node=*/"127.1", /*m_use_v2transport=*/true}));
#endif

    BOOST_TEST_MESSAGE("\nExpect GetAddedNodeInfo to return expected number of peers with `include_connected` true/false");
    BOOST_CHECK_EQUAL(connman->GetAddedNodeInfo(/*include_connected=*/true).size(), nodes.size());
    BOOST_CHECK(connman->GetAddedNodeInfo(/*include_connected=*/false).empty());

    // Test AddedNodesContain()
    for (auto node : connman->TestNodes()) {
        BOOST_CHECK(connman->AddedNodesContain(node->addr));
    }
    AddPeer(id, nodes, *peerman, *connman, ConnectionType::OUTBOUND_FULL_RELAY);
    BOOST_CHECK(!connman->AddedNodesContain(nodes.back()->addr));

    BOOST_TEST_MESSAGE("\nPrint GetAddedNodeInfo contents:");
    for (const auto& info : connman->GetAddedNodeInfo(/*include_connected=*/true)) {
        BOOST_TEST_MESSAGE(strprintf("\nadded node: %s", info.m_params.m_added_node));
        BOOST_TEST_MESSAGE(strprintf("connected: %s", info.fConnected));
        if (info.fConnected) {
            BOOST_TEST_MESSAGE(strprintf("IP address: %s", info.resolvedAddress.ToStringAddrPort()));
            BOOST_TEST_MESSAGE(strprintf("direction: %s", info.fInbound ? "inbound" : "outbound"));
        }
    }

    BOOST_TEST_MESSAGE("\nCheck that all connected peers are correctly detected as connected");
    for (const auto& node : connman->TestNodes()) {
        BOOST_CHECK(connman->AlreadyConnectedToAddressPublic(node->addr));
    }

    // Clean up
    for (auto node : connman->TestNodes()) {
        peerman->FinalizeNode(*node);
    }
    connman->ClearTestNodes();
}

BOOST_FIXTURE_TEST_CASE(outgoing_whitelist_permissions, MockSocketTestingSetup)
{
    ConnmanTestMsg connman{0x1337, 0x1337, *m_node.addrman, *m_node.netgroupman, Params()};
    auto peerman = PeerManager::make(connman, *m_node.addrman, nullptr, *m_node.chainman, *m_node.mempool, *m_node.warnings, {});
    const Proxy proxy{LookupNumeric("127.0.0.1:9050"), /*tor_stream_isolation=*/false};
    for (const auto subnet : {"all,out@127.0.0.1", "all,out@192.0.2.0/24"}) {
        CConnman::Options options{};
        options.m_msgproc = peerman.get();
        NetWhitelistPermissions permissions;
        ConnectionDirection direction;
        bilingual_str error;
        BOOST_REQUIRE(NetWhitelistPermissions::TryParse(subnet, permissions, direction, error));
        options.vWhitelistedRangeOutgoing.push_back(permissions);
        connman.Init(options);
        for (const auto type : ALL_CONNECTION_TYPES) {
            if (type == ConnectionType::INBOUND) continue;
            for (const bool v2 : {false, true}) {
                // All sockets are mocked. Private broadcast retains its proxy
                // requirement, with a successful SOCKS5 handshake fixture.
                const bool private_broadcast{type == ConnectionType::PRIVATE_BROADCAST};
                CreateSock = [private_broadcast](int, int, int) -> std::unique_ptr<Sock> {
                    if (private_broadcast) {
                        return std::make_unique<StaticContentsSock>(std::string{"\x05\x00\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00", 12});
                    }
                    return std::make_unique<ZeroSock>();
                };
                std::unique_ptr<CNode> node{connman.ConnectNodePublic("127.0.0.1:18444", type, v2,
                    private_broadcast ? std::optional<Proxy>{proxy} : std::nullopt)};
                BOOST_REQUIRE(node);
                BOOST_CHECK(node->m_conn_type == type);
                const auto expected{type == ConnectionType::MANUAL && permissions.m_subnet.Match(node->addr) ? NetPermissionFlags::All : NetPermissionFlags::None};
                BOOST_CHECK(node->m_permission_flags == expected);

                // Private broadcast must keep ignoring PING rather than
                // emitting PONG. A full-relay positive control proves the
                // message fixture is valid. Use v1 to avoid a mock v2 handshake.
                if (!v2 && (private_broadcast || type == ConnectionType::OUTBOUND_FULL_RELAY)) {
                    node->SetCommonVersion(PROTOCOL_VERSION);
                    node->nVersion = PROTOCOL_VERSION;
                    peerman->InitializeNode(*node, ServiceFlags(NODE_NETWORK | NODE_WITNESS));
                    {
                        LOCK(NetEventsInterface::g_msgproc_mutex);
                        peerman->SendMessages(*node);
                    }
                    node->fSuccessfullyConnected = true;
                    connman.FlushSendBuffer(*node);
                    node->fPauseSend = false;
                    BOOST_REQUIRE(connman.ReceiveMsgFrom(*node, NetMsg::Make(NetMsgType::PING, uint64_t{42})));
                    {
                        LOCK(NetEventsInterface::g_msgproc_mutex);
                        connman.ProcessMessagesOnce(*node);
                    }
                    CNodeStats stats;
                    node->CopyStats(stats);
                    BOOST_CHECK_EQUAL(stats.mapSendBytesPerMsgType[NetMsgType::PONG] == 0, private_broadcast);
                    peerman->FinalizeNode(*node);
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(onion_socket_permissions)
{
    ConnmanTestMsg connman{0x1337, 0x1337, *m_node.addrman, *m_node.netgroupman, Params()};
    auto peerman = PeerManager::make(connman, *m_node.addrman, nullptr, *m_node.chainman, *m_node.mempool, *m_node.warnings, {});
    const CAddress bind{LookupNumeric("127.0.0.1:18444"), NODE_NONE};
    const CAddress peer{LookupNumeric("127.0.0.1:12345"), NODE_NETWORK};
    CConnman::Options options{};
    options.m_msgproc = peerman.get();
    options.m_max_automatic_connections = 125;
    options.onion_binds.push_back(bind);
    NetWhitelistPermissions permissions;
    ConnectionDirection direction;
    bilingual_str error;
    BOOST_REQUIRE(NetWhitelistPermissions::TryParse("all@127.0.0.1", permissions, direction, error));
    options.vWhitelistedRangeIncoming.push_back(permissions);
    connman.Init(options);
    const auto socket_flags{NetPermissionFlags::Addr | NetPermissionFlags::BloomFilter};
    connman.CreateNodeFromAcceptedSocketPublic(std::make_unique<ZeroSock>(), socket_flags, bind, peer);
    const auto nodes{connman.TestNodes()};
    BOOST_REQUIRE_EQUAL(nodes.size(), 1U);
    BOOST_CHECK(nodes[0]->m_inbound_onion);
    BOOST_CHECK(nodes[0]->m_conn_type == ConnectionType::INBOUND);
    BOOST_CHECK(nodes[0]->m_permission_flags == socket_flags);
    peerman->FinalizeNode(*nodes[0]);
    connman.ClearTestNodes();
}

BOOST_AUTO_TEST_SUITE_END()
