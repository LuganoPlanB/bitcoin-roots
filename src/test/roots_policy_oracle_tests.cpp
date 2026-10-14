// Copyright (c) 2026 The Bitcoin Roots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/util/roots_policy_oracles.h>

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <limits>
#include <string>

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

BOOST_AUTO_TEST_SUITE_END()
