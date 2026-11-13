// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/data/base58_encode_decode.json.h>

#include <base58.h>
#include <test/util/json.h>
#include <test/util/random.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <util/vector.h>

#include <univalue.h>

#include <boost/test/unit_test.hpp>
#include <string>

using namespace std::literals;
using namespace util::hex_literals;

BOOST_FIXTURE_TEST_SUITE(base58_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(secure_checksum_encoding_parity)
{
    for (size_t size : {0U, 1U, 4U, 21U, 33U, 34U, 78U, 256U}) {
        std::vector<unsigned char> input(size);
        for (size_t i{0}; i < size; ++i) input[i] = static_cast<unsigned char>(i);
        const auto secure{EncodeBase58CheckSecure(input)};
        BOOST_CHECK((std::string_view{secure.data(), secure.size()} == EncodeBase58Check(input)));
        std::vector<unsigned char, secure_allocator<unsigned char>> decoded;
        BOOST_REQUIRE(DecodeBase58Check(secure, decoded, 256));
        BOOST_CHECK(std::ranges::equal(input, decoded));
        // Small encodings must also use the cleansing allocator, not SSO.
        BOOST_CHECK_GT(secure.capacity(), SecureString{}.capacity());
    }
}

// Goal: test low-level base58 encoding functionality
BOOST_AUTO_TEST_CASE(base58_EncodeBase58)
{
    UniValue tests = read_json(json_tests::base58_encode_decode);
    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 2) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::vector<unsigned char> sourcedata = ParseHex(test[0].get_str());
        std::string base58string = test[1].get_str();
        BOOST_CHECK_MESSAGE(
                    EncodeBase58(sourcedata) == base58string,
                    strTest);
    }
}

// Goal: test low-level base58 decoding functionality
BOOST_AUTO_TEST_CASE(base58_DecodeBase58)
{
    UniValue tests = read_json(json_tests::base58_encode_decode);
    std::vector<unsigned char> result;

    for (unsigned int idx = 0; idx < tests.size(); idx++) {
        const UniValue& test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 2) // Allow for extra stuff (useful for comments)
        {
            BOOST_ERROR("Bad test: " << strTest);
            continue;
        }
        std::vector<unsigned char> expected = ParseHex(test[0].get_str());
        std::string base58string = test[1].get_str();
        BOOST_CHECK_MESSAGE(DecodeBase58(base58string, result, 256), strTest);
        BOOST_CHECK_MESSAGE(std::ranges::equal(result ,expected), strTest);
    }

    BOOST_CHECK(!DecodeBase58("invalid"s, result, 100));
    BOOST_CHECK(!DecodeBase58("invalid\0"s, result, 100));
    BOOST_CHECK(!DecodeBase58("\0invalid"s, result, 100));

    BOOST_CHECK( DecodeBase58("good"s, result, 100));
    BOOST_CHECK(!DecodeBase58("bad0IOl"s, result, 100));
    BOOST_CHECK(!DecodeBase58("goodbad0IOl"s, result, 100));
    BOOST_CHECK(!DecodeBase58("good\0bad0IOl"s, result, 100));

    // check that DecodeBase58 skips whitespace, but still fails with unexpected non-whitespace at the end.
    BOOST_CHECK(!DecodeBase58(" \t\n\v\f\r skip \r\f\v\n\t a", result, 3));
    BOOST_CHECK( DecodeBase58(" \t\n\v\f\r skip \r\f\v\n\t ", result, 3));
    constexpr auto expected{"971a55"_hex_u8};
    BOOST_CHECK_EQUAL_COLLECTIONS(result.begin(), result.end(), expected.begin(), expected.end());

    BOOST_CHECK( DecodeBase58Check("3vQB7B6MrGQZaxCuFg4oh"s, result, 100));
    BOOST_CHECK(!DecodeBase58Check("3vQB7B6MrGQZaxCuFg4oi"s, result, 100));
    BOOST_CHECK(!DecodeBase58Check("3vQB7B6MrGQZaxCuFg4oh0IOl"s, result, 100));
    BOOST_CHECK(!DecodeBase58Check("3vQB7B6MrGQZaxCuFg4oh\0" "0IOl"s, result, 100));
}

BOOST_AUTO_TEST_SUITE_END()
