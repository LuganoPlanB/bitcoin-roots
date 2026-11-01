// Copyright (c) 2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stats/stats.h>

#include <test/util/setup_common.h>
#include <util/time.h>

#include <boost/test/unit_test.hpp>
#include <chrono>

BOOST_FIXTURE_TEST_SUITE(stats_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(stats)
{
    BOOST_CHECK(!CStats::m_stats_enabled);
    CStats::DefaultStats()->setMaxMemoryUsageTarget(CStats::DEFAULT_MAX_STATS_MEMORY);
    struct DisableOnExit {
        ~DisableOnExit() { CStats::DefaultStats()->setMaxMemoryUsageTarget(0); }
    } disable_on_exit;
    unsigned notifications{0};
    btcsignals::scoped_connection connection{CStats::DefaultStats()->MempoolStatsDidChange.connect([&] { ++notifications; })};

    const auto start{GetTime()};
    SetMockTime(std::chrono::seconds{start});

    CStats::DefaultStats()->addMempoolSample(0, 1, 1);
    SetMockTime(std::chrono::seconds{start + 1});
    CStats::DefaultStats()->addMempoolSample(0, 2, 2); //1second should be to short
    SetMockTime(std::chrono::seconds{start + 2});
    CStats::DefaultStats()->addMempoolSample(0, 2, 2); //the exact spacing boundary is also skipped
    SetMockTime(std::chrono::seconds{start + 5});
    CStats::DefaultStats()->addMempoolSample(3, 4, 3);

    uint64_t queryFromTime = start;
    uint64_t queryToTime = start + 3600;
    mempoolSamples_t samples = CStats::DefaultStats()->mempoolGetValuesInRange(queryFromTime, queryToTime);
    BOOST_REQUIRE_EQUAL(samples.size(), 2U);
    BOOST_CHECK_EQUAL(notifications, 2U);

    BOOST_CHECK_EQUAL(samples[0].m_time_delta, 0U);
    BOOST_CHECK_EQUAL(samples[1].m_time_delta, 5U);
    BOOST_CHECK_EQUAL(samples[1].m_tx_count, 3);
    BOOST_CHECK_EQUAL(samples[1].m_dyn_mem_usage, 4);
    BOOST_CHECK_EQUAL(samples[1].m_min_fee_per_k, 3);
    BOOST_CHECK_EQUAL(queryFromTime, start);
    BOOST_CHECK_EQUAL(queryToTime, start + 5);

    // check retrieving a subset of the available samples
    queryFromTime = start;
    queryToTime = start;
    samples = CStats::DefaultStats()->mempoolGetValuesInRange(queryFromTime, queryToTime);
    BOOST_CHECK_EQUAL(samples.size(), 1U);

    // add some samples
    for (int i = 0; i < 10000; i++) {
        SetMockTime(std::chrono::seconds{start + 10 + i * 5});
        CStats::DefaultStats()->addMempoolSample(i, i + 1, i + 2);
    }

    queryFromTime = start + 3600;
    queryToTime = start + 3600;
    samples = CStats::DefaultStats()->mempoolGetValuesInRange(queryFromTime, queryToTime);
    BOOST_CHECK_EQUAL(samples.size(), 1U); //get a single sample

    queryFromTime = start;
    queryToTime = start + 3600;
    samples = CStats::DefaultStats()->mempoolGetValuesInRange(queryFromTime, queryToTime);
    BOOST_CHECK(samples.size() >= 3600 / 5);

    // reduce max memory and add 100 samples to ensure it triggers the cleanup
    CStats::DefaultStats()->setMaxMemoryUsageTarget(10 * 1024);
    for (int i = 10000; i < 10100; i++) {
        SetMockTime(std::chrono::seconds{start + 10 + i * 5});
        CStats::DefaultStats()->addMempoolSample(i, i + 1, i + 2);
    }

    queryFromTime = start;
    queryToTime = start + 100;
    samples = CStats::DefaultStats()->mempoolGetValuesInRange(queryFromTime, queryToTime);
    BOOST_CHECK_EQUAL(samples.size(), 1U);

    queryFromTime = 0; // no range limits
    queryToTime = 0;   // no range  limits
    samples = CStats::DefaultStats()->mempoolGetValuesInRange(queryFromTime, queryToTime);
    BOOST_CHECK_EQUAL(samples.size() < 1000U, true);
    CStats::DefaultStats()->setMaxMemoryUsageTarget(0);
    BOOST_CHECK(!CStats::m_stats_enabled);
    BOOST_CHECK(CStats::DefaultStats()->mempoolGetValuesInRange(queryFromTime, queryToTime).empty());
}

BOOST_AUTO_TEST_SUITE_END()
