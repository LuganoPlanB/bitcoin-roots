// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/roots_policy_oracles.h>
#include <node/mempool_args.h>
#include <coins.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <util/string.h>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

BOOST_AUTO_TEST_SUITE(roots_policy_oracle_tests)

BOOST_AUTO_TEST_CASE(signed_reference_boundaries)
{
    using Wide = boost::multiprecision::int128_t;
    constexpr auto minimum{std::numeric_limits<int64_t>::min()};
    constexpr auto maximum{std::numeric_limits<int64_t>::max()};
    for (const int64_t expected : {minimum, minimum + 1, int64_t{-1}, int64_t{0}, int64_t{1}, maximum - 1, maximum}) {
        BOOST_CHECK_EQUAL(roots_test::BoundedSignedReference(Wide{expected}), expected);
    }
    BOOST_CHECK_EQUAL(roots_test::BoundedSignedReference(Wide{minimum} - 1), minimum);
    BOOST_CHECK_EQUAL(roots_test::BoundedSignedReference(Wide{maximum} + 1), maximum);
    BOOST_CHECK_EQUAL(roots_test::BoundedSignedReference(Wide{minimum} * 1000), minimum);
    BOOST_CHECK_EQUAL(roots_test::BoundedSignedReference(Wide{maximum} * 1000), maximum);
    // Negative division rounds down in the independent fee-rate oracle.
    const Wide product{-1000};
    Wide quotient{product / 3};
    if (product < 0 && product % 3 != 0) --quotient;
    BOOST_CHECK_EQUAL(roots_test::BoundedSignedReference(quotient), -334);
}

BOOST_AUTO_TEST_CASE(decimal_reference_grammar_and_bytes)
{
    constexpr auto maximum{std::numeric_limits<uint64_t>::max()};
    for (const std::string value : {"0", "1", "+1", "01", "-0", "-000", "18446744073709551615"}) {
        BOOST_CHECK(roots_test::ValidInteger(value, maximum));
    }
    for (const std::string value : {"", "+", "-", "-1", "+-0", " 1", "1 ", "1.0", "18446744073709551616"}) {
        BOOST_CHECK(!roots_test::ValidInteger(value, maximum));
    }
    BOOST_CHECK(roots_test::ValidInteger("0", 0));
    BOOST_CHECK(!roots_test::ValidInteger("1", 0));
    BOOST_CHECK(roots_test::ValidInteger("10", 10));
    BOOST_CHECK(!roots_test::ValidInteger("11", 10));
    // Exercise every byte, including embedded NULs and high-bit values, using
    // the same char representation that std::string_view exposes to the oracle.
    for (unsigned int byte{0}; byte <= 255; ++byte) {
        const char character{static_cast<char>(static_cast<unsigned char>(byte))};
        const std::string value(1, character);
        const bool digit{byte >= '0' && byte <= '9'};
        BOOST_CHECK_EQUAL(roots_test::ValidInteger(value, maximum), digit);
        BOOST_CHECK_EQUAL(roots_test::ValidInteger("1" + value, maximum), digit);
    }
}

BOOST_AUTO_TEST_CASE(carrier_decimal_quarter_oracle)
{
    // Decimal hundredths are constructed independently of ParseFixedPoint.
    // Find the least integral weight whose rational cost covers the request.
    using Wide = boost::multiprecision::cpp_int;
    for (uint64_t cents{0}; cents <= 10000; ++cents) {
        const std::string value{util::ToString(cents / 100) + "." + util::ToString(100 + cents % 100).substr(1)};
        const auto parsed{ParseDataCarrierCost(value)};
        BOOST_REQUIRE(parsed);
        const Wide covered{Wide{*parsed} * 25};
        BOOST_CHECK(covered >= cents);
        if (*parsed > 0) BOOST_CHECK(covered - 25 < cents);
    }
    for (const uint64_t cents : {uint64_t{107374182374}, uint64_t{107374182375}, uint64_t{107374182376}}) {
        const auto value{util::ToString(cents / 100) + "." + util::ToString(100 + cents % 100).substr(1)};
        const auto parsed{ParseDataCarrierCost(value)};
        const bool representable{Wide{cents} <= Wide{UINT32_MAX} * 25};
        BOOST_REQUIRE_EQUAL(bool(parsed), representable);
        if (parsed) BOOST_CHECK(Wide{*parsed} * 25 >= cents && Wide{*parsed - 1} * 25 < cents);
    }
}

BOOST_AUTO_TEST_CASE(output_carrier_weight_oracle)
{
    using Wide = boost::multiprecision::cpp_int;
    CCoinsViewCache view{&CoinsViewEmpty::Get()};
    for (const unsigned int bytes : {0U, 1U, 75U, 76U, 255U, 520U, 1650U}) {
        CMutableTransaction tx;
        tx.vout.emplace_back(0, CScript() << OP_RETURN << std::vector<unsigned char>(bytes, 0x42));
        const CTransaction transaction{tx};
        for (const unsigned int rate : {0U, 1U, 4U, 5U, 100U, UINT32_MAX}) {
            const Wide charged{rate > 4 ? Wide{rate - 4} * tx.vout[0].scriptPubKey.size() : Wide{0}};
            const Wide maximum{INT32_MAX};
            const int32_t expected{static_cast<int32_t>(charged > maximum ? maximum : charged)};
            BOOST_CHECK_EQUAL(CalculateExtraTxWeight(transaction, view, rate), expected);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
