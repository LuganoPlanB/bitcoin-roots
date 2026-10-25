// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

// TEMPORARY Linux benchmark instrumentation. Copied only into exported sources.
#ifndef BITCOIN_ISSUE56_BENCH_H
#define BITCOIN_ISSUE56_BENCH_H

#include <core_memusage.h>
#include <logging.h>
#include <primitives/transaction.h>

#include <chrono>
#include <cstdint>
#include <ctime>
#include <map>
#include <utility>
#include <vector>

namespace issue56 {
inline uint64_t CpuNs()
{
    timespec value{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) std::terminate();
    return uint64_t(value.tv_sec) * 1000000000 + value.tv_nsec;
}
inline uint64_t WallNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
// The cache and compact reconstruction paths run on the message-processing
// thread. Thread-local totals avoid synchronizing unrelated worker threads.
inline thread_local uint64_t insert_calls{0}, insert_cpu_ns{0}, insert_wall_ns{0};
inline thread_local uint64_t stored_calls{0}, stored_cpu_ns{0}, stored_wall_ns{0};
struct Insertion {
    uint64_t cpu{CpuNs()}, wall{WallNs()};
    bool stored{false};
    ~Insertion()
    {
        const auto elapsed_cpu{CpuNs() - cpu}, elapsed_wall{WallNs() - wall};
        insert_cpu_ns += elapsed_cpu;
        insert_wall_ns += elapsed_wall;
        ++insert_calls;
        if (stored) {
            ++stored_calls;
            stored_cpu_ns += elapsed_cpu;
            stored_wall_ns += elapsed_wall;
        }
    }
};
struct Reconstruction {
    uint64_t slots, slot_bytes, occupied{0}, charged{0}, unique_dynamic{0};
    uint64_t distinct{0}, external_refs{0}, shared_objects{0};
    uint64_t extra_hits{0}, available{0};
    uint64_t cpu, wall;
    const std::string hash;
    Reconstruction(const std::vector<std::pair<Wtxid, CTransactionRef>>& extra,
                   std::string block_hash)
        : slots(extra.size()), slot_bytes(extra.capacity() * sizeof(extra[0])), hash(std::move(block_hash))
    {
        // Snapshot before starting the InitData timer. This traversal itself
        // changes cache warmth; both variants receive exactly this treatment.
        std::map<const CTransaction*, std::pair<uint64_t, uint64_t>> owners;
        for (const auto& [id, tx] : extra) {
            if (!tx) continue;
            ++occupied;
            charged += RecursiveDynamicUsage(*tx);
            auto& [refs, total] = owners[tx.get()];
            ++refs;
            total = tx.use_count();
        }
        distinct = owners.size();
        for (const auto& [tx, counts] : owners) {
            unique_dynamic += RecursiveDynamicUsage(*tx);
            if (counts.second > counts.first) {
                external_refs += counts.second - counts.first;
                ++shared_objects;
            }
        }
        cpu = CpuNs();
        wall = WallNs();
    }
    ~Reconstruction()
    {
        const auto elapsed_cpu{CpuNs() - cpu}, elapsed_wall{WallNs() - wall};
        LogInfo("ISSUE56 block=%s insert_calls=%u insert_cpu_ns=%u insert_wall_ns=%u init_cpu_ns=%u init_wall_ns=%u slots=%u slot_bytes=%u occupied=%u charged_dynamic_bytes=%u unique_dynamic_bytes=%u distinct=%u external_refs=%u shared_objects=%u extra_hits=%u available=%u stored_calls=%u stored_cpu_ns=%u stored_wall_ns=%u\n",
                  hash, insert_calls, insert_cpu_ns, insert_wall_ns, elapsed_cpu, elapsed_wall,
                  slots, slot_bytes, occupied, charged, unique_dynamic, distinct, external_refs, shared_objects, extra_hits, available,
                  stored_calls, stored_cpu_ns, stored_wall_ns);
        insert_calls = insert_cpu_ns = insert_wall_ns = 0;
        stored_calls = stored_cpu_ns = stored_wall_ns = 0;
    }
};
} // namespace issue56
#endif // BITCOIN_ISSUE56_BENCH_H
