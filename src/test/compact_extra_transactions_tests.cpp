// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <blockencodings.h>
#include <consensus/merkle.h>
#include <core_memusage.h>
#include <net_processing.h>
#include <node/context.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(compact_extra_transactions_tests, TestingSetup)

static CTransactionRef Transaction(size_t script_bytes, uint32_t nonce = 0)
{
    CMutableTransaction tx;
    tx.nLockTime = nonce;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vout[0].nValue = 1;
    tx.vout[0].scriptPubKey.resize(script_bytes);
    return MakeTransactionRef(std::move(tx));
}

// Assert the exact native charge, occupied references, and reconstruction result.
// These transactions are structural fixtures, not consensus-valid block claims.
static void Check(PeerManager& manager, CTxMemPool& pool,
                  const std::vector<CTransactionRef>& offered,
                  const std::vector<bool>& expected, size_t slot_count,
                  size_t byte_limit = DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN_SIZE)
{
    const auto snapshot = manager.UnitTestGetCompactExtraTransactions();
    BOOST_CHECK_EQUAL(snapshot.slots.size(), slot_count);
    size_t charged{0};
    for (const auto& [hash, tx] : snapshot.slots) {
        if (tx) {
            BOOST_CHECK(hash == tx->GetWitnessHash());
            charged += RecursiveDynamicUsage(*tx);
        }
    }
    BOOST_CHECK_EQUAL(snapshot.charged_bytes, charged);
    BOOST_CHECK_LE(snapshot.charged_bytes, byte_limit);
    CBlock block;
    block.nVersion = 1;
    block.nBits = 0x207fffff;
    block.vtx.push_back(Transaction(0, 999));
    block.vtx.insert(block.vtx.end(), offered.begin(), offered.end());
    block.hashMerkleRoot = BlockMerkleRoot(block);
    PartiallyDownloadedBlock partial{&pool};
    BOOST_REQUIRE(partial.InitData(CBlockHeaderAndShortTxIDs{block, 42}, snapshot.slots) == READ_STATUS_OK);
    BOOST_REQUIRE_EQUAL(offered.size(), expected.size());
    for (size_t i{0}; i < expected.size(); ++i) {
        BOOST_CHECK_EQUAL(partial.IsTxAvailable(i + 1), expected[i]);
    }
}

BOOST_AUTO_TEST_CASE(native_limits)
{
    const auto tx = Transaction(200);
    const size_t usage = RecursiveDynamicUsage(*tx);
    BOOST_REQUIRE(usage > sizeof(std::pair<Wtxid, CTransactionRef>));
    const auto make = [&](uint32_t count, size_t budget) {
        PeerManager::Options opts;
        opts.max_extra_txs = count;
        opts.max_extra_txs_size = budget;
        return PeerManager::make(*m_node.connman, *m_node.addrman, nullptr, *m_node.chainman,
                                 *m_node.mempool, *m_node.warnings, opts);
    };
    for (const auto& [count, budget] : std::vector<std::pair<uint32_t, size_t>>{
             {0, usage}, {1, 0}, {1, usage - 1}, {1, usage},
             {std::numeric_limits<uint32_t>::max(), sizeof(std::pair<Wtxid, CTransactionRef>) - 1}}) {
        auto manager = make(count, budget);
        manager->UnitTestAddToCompactExtraTransactions(tx);
        const bool fits = count > 0 && budget >= usage;
        Check(*manager, *m_node.mempool, {tx}, {fits}, fits ? 1 : 0, budget);
    }
    auto one = make(1, usage * 10);
    const auto second = Transaction(200, 1);
    one->UnitTestAddToCompactExtraTransactions(tx);
    one->UnitTestAddToCompactExtraTransactions(second);
    Check(*one, *m_node.mempool, {tx, second}, {false, true}, 1, usage * 10);
    auto bounded = make(std::numeric_limits<uint32_t>::max(), usage);
    bounded->UnitTestAddToCompactExtraTransactions(tx);
    Check(*bounded, *m_node.mempool, {tx}, {true}, usage / sizeof(std::pair<Wtxid, CTransactionRef>), usage);
    auto no_slots = make(1, sizeof(std::pair<Wtxid, CTransactionRef>) - 1);
    no_slots->UnitTestAddToCompactExtraTransactions(MakeTransactionRef(CMutableTransaction{}));
    BOOST_CHECK(no_slots->UnitTestGetCompactExtraTransactions().slots.empty());
}

BOOST_AUTO_TEST_CASE(mixed_wraparound_and_sparse_slots)
{
    const auto a = Transaction(200, 1), b = Transaction(200, 2);
    const auto c = Transaction(200, 3), d = Transaction(200, 4);
    const size_t small = RecursiveDynamicUsage(*a);
    // Find an actual native charge which requires three old references to go.
    CTransactionRef large;
    for (size_t bytes{201}; bytes < 10000; ++bytes) {
        auto candidate = Transaction(bytes, 5);
        const size_t usage = RecursiveDynamicUsage(*candidate);
        if (usage > 2 * small && usage <= 3 * small) {
            large = std::move(candidate);
            break;
        }
    }
    BOOST_REQUIRE(large);
    PeerManager::Options opts;
    opts.max_extra_txs = 4;
    opts.max_extra_txs_size = 4 * small;
    auto manager = PeerManager::make(*m_node.connman, *m_node.addrman, nullptr, *m_node.chainman,
                                     *m_node.mempool, *m_node.warnings, opts);
    const std::vector<CTransactionRef> offered{a, b, c, d, large};
    for (const auto& tx : {a, b, c, d}) manager->UnitTestAddToCompactExtraTransactions(tx);
    Check(*manager, *m_node.mempool, offered, {true, true, true, true, false}, 4, opts.max_extra_txs_size);
    manager->UnitTestAddToCompactExtraTransactions(large);
    Check(*manager, *m_node.mempool, offered, {false, false, false, true, true}, 4, opts.max_extra_txs_size);
    // The cursor has crossed wraparound and two slots are empty. D is next oldest.
    manager->UnitTestAddToCompactExtraTransactions(a);
    Check(*manager, *m_node.mempool, offered, {true, false, false, false, true}, 4, opts.max_extra_txs_size);
    manager->UnitTestAddToCompactExtraTransactions(b);
    Check(*manager, *m_node.mempool, offered, {true, true, false, false, false}, 4, opts.max_extra_txs_size);
}

BOOST_AUTO_TEST_CASE(entry_threshold_and_shared_ownership)
{
    CTransactionRef below, above;
    for (size_t bytes{99000}; bytes < 100100; ++bytes) {
        auto tx = Transaction(bytes);
        if (RecursiveDynamicUsage(*tx) < BLOCK_RECONSTRUCTION_EXTRA_TXN_PER_TXN_SIZE_LIMIT) {
            below = std::move(tx);
        } else {
            above = std::move(tx);
            break;
        }
    }
    BOOST_REQUIRE(below && above);
    BOOST_TEST_MESSAGE("Nearest native threshold charges: " << RecursiveDynamicUsage(*below)
                       << " / " << RecursiveDynamicUsage(*above));
    PeerManager::Options opts;
    opts.max_extra_txs = 2;
    auto manager = PeerManager::make(*m_node.connman, *m_node.addrman, nullptr, *m_node.chainman,
                                     *m_node.mempool, *m_node.warnings, opts);
    manager->UnitTestAddToCompactExtraTransactions(below);
    manager->UnitTestAddToCompactExtraTransactions(above);
    Check(*manager, *m_node.mempool, {below, above}, {true, false}, 2);
    // Exercise the actual replaced-transaction insertion loop as well as the
    // common gate. Real mempool replacement is covered by the P2P fixture.
    auto replaced_manager = PeerManager::make(*m_node.connman, *m_node.addrman, nullptr, *m_node.chainman,
                                              *m_node.mempool, *m_node.warnings, opts);
    replaced_manager->UnitTestProcessValidTx(Transaction(0, 10), {below, above});
    Check(*replaced_manager, *m_node.mempool, {below, above}, {true, false}, 2);
    replaced_manager.reset();
    // The raw insertion routine counts repeated references, not unique allocation.
    manager->UnitTestAddToCompactExtraTransactions(below);
    BOOST_CHECK_EQUAL(manager->UnitTestGetCompactExtraTransactions().charged_bytes,
                      2 * RecursiveDynamicUsage(*below));
    Check(*manager, *m_node.mempool, {below}, {true}, 2);
    BOOST_CHECK(RecursiveDynamicUsage(below) > RecursiveDynamicUsage(*below));
    std::weak_ptr<const CTransaction> lifetime = below;
    below.reset();
    BOOST_CHECK(!lifetime.expired());
    auto other_owner = lifetime.lock();
    manager.reset();
    BOOST_CHECK(!lifetime.expired());
    other_owner.reset();
    BOOST_CHECK(lifetime.expired());
}

BOOST_AUTO_TEST_SUITE_END()
