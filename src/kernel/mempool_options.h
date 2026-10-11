// Copyright (c) 2022-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_KERNEL_MEMPOOL_OPTIONS_H
#define BITCOIN_KERNEL_MEMPOOL_OPTIONS_H

#include <kernel/mempool_limits.h>

#include <policy/feerate.h>
#include <policy/policy.h>

#include <chrono>
#include <cstdint>
#include <optional>

class ValidationSignals;

/** Default for -maxmempool, maximum megabytes of mempool memory usage */
static constexpr unsigned int DEFAULT_MAX_MEMPOOL_SIZE_MB{300};
/** Default for -maxmempool when blocksonly is set */
static constexpr unsigned int DEFAULT_BLOCKSONLY_MAX_MEMPOOL_SIZE_MB{5};
/** Default for -mempoolexpiry, expiration time for mempool transactions in hours */
static constexpr unsigned int DEFAULT_MEMPOOL_EXPIRY_HOURS{336};
/** Whether to fall back to legacy V1 serialization when writing mempool.dat */
static constexpr bool DEFAULT_PERSIST_V1_DAT{false};
/** Default for -acceptnonstdtxn */
static constexpr bool DEFAULT_ACCEPT_NON_STD_TXN{false};
/** Default for -acceptnonstddatacarrier */
static constexpr bool DEFAULT_ACCEPT_NON_STD_DATACARRIER{false};

enum class RBFPolicy { Never, OptIn, Always };
enum class TRUCPolicy { Reject, Accept, Enforce };

namespace kernel {
/**
 * Options struct containing options for constructing a CTxMemPool. Default
 * constructor populates the struct with sane default values which can be
 * modified.
 *
 * Most of the time, this struct should be referenced as CTxMemPool::Options.
 */
struct MemPoolOptions : StandardnessOptions {
    /* The ratio used to determine how often sanity checks will run.  */
    int check_ratio{0};
    int64_t max_size_bytes{DEFAULT_MAX_MEMPOOL_SIZE_MB * 1'000'000};
    std::chrono::seconds expiry{std::chrono::hours{DEFAULT_MEMPOOL_EXPIRY_HOURS}};
    CFeeRate incremental_relay_feerate{DEFAULT_INCREMENTAL_RELAY_FEE};
    /** A fee rate smaller than this is considered zero fee (for relaying, mining and transaction creation) */
    CFeeRate min_relay_feerate{DEFAULT_MIN_RELAY_TX_FEE};
    // Local policy controls. Dynamic dust publication is node-owned.
    unsigned int weight_per_data_byte{WITNESS_SCALE_FACTOR};
    bool subdustfeepenalty{DEFAULT_SUBDUSTFEEPENALTY};
    RBFPolicy rbf_policy{RBFPolicy::Always};
    TRUCPolicy truc_policy{TRUCPolicy::Enforce};
    bool reject_spk_reuse{false};
    /** Negative: estimator target blocks; positive: native mempool position in kvB; zero: off. */
    int32_t dust_relay_target{0};
    int dust_relay_multiplier{3'000};
    uint64_t minrelaycoinblocks{0};
    int minrelaymaturity{0};
    bool datacarrier_fullcount{DEFAULT_DATACARRIER_FULLCOUNT};
    bool accept_non_std_datacarrier{false};
    bool require_standard{true};
    bool persist_v1_dat{DEFAULT_PERSIST_V1_DAT};
    MemPoolLimits limits{};

    ValidationSignals* signals{nullptr};
};
} // namespace kernel

#endif // BITCOIN_KERNEL_MEMPOOL_OPTIONS_H
