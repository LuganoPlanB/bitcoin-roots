// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <bench/bench.h>
#include <core_memusage.h>
#include <net_processing.h>
#include <node/context.h>
#include <primitives/transaction.h>
#include <test/util/setup_common.h>
#include <util/check.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <list>
#include <memory>
#include <utility>
#include <vector>

namespace {
// Structural fixtures: these do not claim admission or consensus validity.
CTransactionRef Transaction(size_t script_bytes, uint32_t nonce)
{
    CMutableTransaction tx;
    tx.nLockTime = nonce;
    tx.vin.resize(1);
    tx.vout.resize(1);
    tx.vout[0].nValue = 1;
    tx.vout[0].scriptPubKey.resize(script_bytes);
    return MakeTransactionRef(std::move(tx));
}

void CheckSnapshot(PeerManager& manager, size_t byte_limit)
{
    const auto snapshot{manager.UnitTestGetCompactExtraTransactions()};
    size_t charge{0};
    for (const auto& [hash, tx] : snapshot.slots) {
        if (tx) {
            Assert(hash == tx->GetWitnessHash());
            charge += RecursiveDynamicUsage(*tx);
        }
    }
    Assert(charge == snapshot.charged_bytes);
    Assert(charge <= byte_limit);
}

void ReportResources(const char* name, const PeerManager::CompactExtraTransactionsSnapshot& snapshot,
                     uint32_t count, size_t byte_limit)
{
    std::cout << "cache_resources name=" << name
              << " count_limit=" << count << " charged_byte_limit=" << byte_limit
              << " slots=" << snapshot.slots.size()
              << " occupied=" << std::count_if(snapshot.slots.begin(), snapshot.slots.end(), [](const auto& slot) { return bool{slot.second}; })
              << " charged_dynamic_bytes=" << snapshot.charged_bytes
              // A count-times-size model, not allocator usage or vector capacity.
              << " modeled_slot_payload_bytes=" << snapshot.slots.size() * sizeof(snapshot.slots[0]) << '\n';
}

void InsertionBench(benchmark::Bench& bench, const char* name, uint32_t count, bool byte_pressure)
{
    const auto setup{MakeNoLogFileContext<const TestingSetup>()};
    const size_t small_charge{RecursiveDynamicUsage(*Transaction(200, 0))};
    size_t large_script{201};
    if (byte_pressure) {
        while (large_script < 10000 && RecursiveDynamicUsage(*Transaction(large_script, 0)) <= 2 * small_charge) ++large_script;
        Assert(RecursiveDynamicUsage(*Transaction(large_script, 0)) <= 3 * small_charge);
    }
    std::vector<CTransactionRef> offers;
    for (uint32_t i{0}; i < 100; ++i) offers.push_back(Transaction(byte_pressure && i % 2 ? large_script : 200, i));
    PeerManager::Options opts;
    opts.max_extra_txs = count;
    opts.max_extra_txs_size = byte_pressure ? 3 * RecursiveDynamicUsage(*offers.front()) : DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN_SIZE;
    auto manager{PeerManager::make(*setup->m_node.connman, *setup->m_node.addrman, nullptr,
                                  *setup->m_node.chainman, *setup->m_node.mempool, *setup->m_node.warnings, opts)};
    // Pre-fill so allocation/initialization is outside these steady insertion timings.
    for (const auto& tx : offers) manager->UnitTestAddToCompactExtraTransactions(tx);
    bench.batch(offers.size()).unit("offered tx").run([&] {
        for (const auto& tx : offers) manager->UnitTestAddToCompactExtraTransactions(tx);
    });
    CheckSnapshot(*manager, opts.max_extra_txs_size);
    const auto snapshot{manager->UnitTestGetCompactExtraTransactions()};
    ReportResources(name, snapshot, count, opts.max_extra_txs_size);
    if (count == 0) {
        Assert(snapshot.slots.empty());
    } else {
        Assert(snapshot.slots.size() == std::min<size_t>(count, opts.max_extra_txs_size / sizeof(snapshot.slots[0])));
    }
}
} // namespace

static void CompactExtraInsertCount(benchmark::Bench& bench)
{
    InsertionBench(bench, "CompactExtraInsertCount", DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN, false);
}

static void CompactExtraInsertBytes(benchmark::Bench& bench)
{
    InsertionBench(bench, "CompactExtraInsertBytes", DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN, true);
}

static void CompactExtraInsertDisabled(benchmark::Bench& bench)
{
    InsertionBench(bench, "CompactExtraInsertDisabled", 0, false);
}

static void CompactExtraColdReplacementProducer(benchmark::Bench& bench)
{
    const auto setup{MakeNoLogFileContext<const TestingSetup>()};
    const auto accepted{Transaction(0, 42)};
    const std::list<CTransactionRef> replaced{Transaction(200, 1), Transaction(1000, 2), Transaction(100000, 3)};
    Assert(RecursiveDynamicUsage(*replaced.back()) >= BLOCK_RECONSTRUCTION_EXTRA_TXN_PER_TXN_SIZE_LIMIT);
    const auto make_manager = [&] {
        return PeerManager::make(*setup->m_node.connman, *setup->m_node.addrman, nullptr,
                                 *setup->m_node.chainman, *setup->m_node.mempool, *setup->m_node.warnings, PeerManager::Options{});
    };
    {
        auto manager{make_manager()};
        manager->UnitTestProcessValidTx(accepted, replaced);
        const auto snapshot{manager->UnitTestGetCompactExtraTransactions()};
        Assert(std::count_if(snapshot.slots.begin(), snapshot.slots.end(), [](const auto& slot) { return bool{slot.second}; }) == 2);
        ReportResources("CompactExtraColdReplacementProducer_preflight", snapshot,
                        DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN, DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN_SIZE);
        CheckSnapshot(*manager, DEFAULT_BLOCK_RECONSTRUCTION_EXTRA_TXN_SIZE);
    }
    // Each iteration includes manager construction, locks, accepted-tx download/
    // broadcast bookkeeping, real replacement insertion and destruction. A fresh
    // manager bounds broadcast backlogs and keeps every iteration's input state equal.
    // Three offered replacements are the denominator; two are cache-eligible.
    bench.batch(replaced.size()).unit("offered replacement").run([&] {
        auto manager{make_manager()};
        manager->UnitTestProcessValidTx(accepted, replaced);
    });
}

BENCHMARK(CompactExtraInsertCount);
BENCHMARK(CompactExtraInsertBytes);
BENCHMARK(CompactExtraInsertDisabled);
BENCHMARK(CompactExtraColdReplacementProducer);
