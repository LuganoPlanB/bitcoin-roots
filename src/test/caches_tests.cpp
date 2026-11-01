// Copyright (c) The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit.

#include <common/args.h>
#include <kernel/caches.h>
#include <node/caches.h>
#include <util/byte_units.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

using namespace node;

namespace {
void CheckDbCacheWarnThreshold(uint64_t threshold, uint64_t total_ram)
{
    BOOST_CHECK(!ShouldWarnOversizedDbCache(threshold, total_ram));
    BOOST_CHECK( ShouldWarnOversizedDbCache(threshold + 1, total_ram));
}
} // namespace

BOOST_AUTO_TEST_SUITE(caches_tests)

BOOST_AUTO_TEST_CASE(oversized_dbcache_warning)
{
    BOOST_CHECK(!ShouldWarnOversizedDbCache(MIN_DBCACHE_BYTES, 1_GiB));

    // Below DBCACHE_WARNING_RESERVED_RAM the existing fixed default dominates.
    CheckDbCacheWarnThreshold(DEFAULT_KERNEL_CACHE, 1_GiB);
    CheckDbCacheWarnThreshold(DEFAULT_KERNEL_CACHE, DBCACHE_WARNING_RESERVED_RAM);

    // Above DBCACHE_WARNING_RESERVED_RAM the warning fires at 75% of the headroom.
    CheckDbCacheWarnThreshold(((3_GiB - DBCACHE_WARNING_RESERVED_RAM) / 4) * 3, 3_GiB);

    for (const auto total_ram : {8_GiB, 16_GiB, 32_GiB}) {
        CheckDbCacheWarnThreshold(((total_ram - DBCACHE_WARNING_RESERVED_RAM) / 4) * 3, total_ram);
    }
}

BOOST_AUTO_TEST_CASE(dbcache_argument_boundaries)
{
    ArgsManager args;
    args.ForceSetArg("-txindex", "0");
    args.ForceSetArg("-txospenderindex", "0");
    const uint64_t maximum{sizeof(void*) == 4 ? 1_GiB : std::numeric_limits<uint64_t>::max()};
    for (const auto& [argument, expected] : {
            std::pair<std::string, uint64_t>{"-9223372036854775808", 4_MiB},
            {"-1", 4_MiB}, {"0", 4_MiB}, {"1", 4_MiB}, {"4", 4_MiB},
            {"450", 450_MiB}, {"1024", 1_GiB}, {"9223372036854775807", maximum}}) {
        args.ForceSetArg("-dbcache", argument);
        const auto cache{CalculateCacheSizes(args)};
        BOOST_CHECK_EQUAL(cache.index.tx_index, 0U);
        BOOST_CHECK_EQUAL(cache.index.txospender_index, 0U);
        BOOST_CHECK_EQUAL(cache.index.filter_index, 0U);
        BOOST_CHECK_EQUAL(cache.kernel.block_tree_db + cache.kernel.coins_db + cache.kernel.coins, expected);
    }
}

BOOST_AUTO_TEST_SUITE_END()
