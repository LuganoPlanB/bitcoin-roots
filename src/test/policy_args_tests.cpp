// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <common/args.h>
#include <common/settings.h>
#include <consensus/consensus.h>
#include <init.h>
#include <kernel/mempool_options.h>
#include <node/mempool_args.h>
#include <policy/settings.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/result.h>
#include <util/translation.h>

#include <cstdint>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

namespace {
const std::vector<std::string> BOOL_OPTIONS{
    "subdustfeepenalty", "rejectparasites", "acceptunknownwitness", "datacarrierfullcount",
    "permitbareanchor", "rejecttokens", "permitbarepubkey", "permitbaredatacarrier", "acceptnonstddatacarrier"};

bool ParseOptions(ArgsManager& args, const std::vector<std::string>& options, std::string& error)
{
    std::vector<const char*> argv{"test_bitcoin"};
    for (const auto& option : options) argv.push_back(option.c_str());
    return args.ParseParameters(argv.size(), argv.data(), error);
}
}

BOOST_FIXTURE_TEST_SUITE(policy_args_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(boolean_values)
{
    for (const auto& option : BOOL_OPTIONS) {
        for (const std::string value : {"", "0", "1", "2", "-1", "+1", "01", " 1", "1 ", "1junk", "junk", "false", "0.5", "1.0", "2147483648"}) {
            BOOST_TEST_CONTEXT(option << "=" << value) {
                ArgsManager args;
                args.ForceSetArg("-" + option, value);
                const auto result = ValidateRootsPolicyOptions(args);
                BOOST_CHECK_EQUAL(bool(result), value.empty() || value == "0" || value == "1");
                if (!result) BOOST_CHECK(util::ErrorString(result).original.find("-" + option) != std::string::npos);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(integer_boundaries)
{
    for (const auto& [option, maximum] : std::vector<std::pair<std::string, std::string>>{
             {"maxscriptsize", "4294967295"}, {"bytespersigopstrict", "4294967295"},
             {"maxtxlegacysigops", "4294967295"}, {"minrelaycoinblocks", "9223372036854775807"},
             {"minrelaymaturity", "2147483647"}, {"blockmaxsize", "4000000"},
             {"blockprioritysize", "9223372036854775807"}}) {
        for (const auto& value : std::vector<std::string>{"0", "1", "+1", "01", "-0", maximum}) {
            BOOST_TEST_CONTEXT(option << "=" << value) {
                ArgsManager args;
                args.ForceSetArg("-" + option, value);
                BOOST_CHECK(ValidateRootsPolicyOptions(args));
            }
        }
        for (const auto& value : std::vector<std::string>{"", "-1", "+-0", "++1", " 1", "1 ", "1junk", "1.5", "1e0", "junk", "9223372036854775808", std::to_string(std::stoull(maximum) + 1)}) {
            BOOST_TEST_CONTEXT(option << "=" << value) {
                ArgsManager args;
                args.ForceSetArg("-" + option, value);
                BOOST_CHECK(!ValidateRootsPolicyOptions(args));
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(carrier_cost_compatibility)
{
    for (const auto& [value, weight] : std::vector<std::pair<std::string, uint32_t>>{
             {"0", 0}, {"-0", 0}, {"-0.00", 0}, {"0.01", 1}, {"0.25", 1}, {"0.26", 2},
             {"1", 4}, {"1.2300", 5}, {"1e0", 4}, {"1e-2", 1}, {"1073741823.75", 4294967295U}}) {
        ArgsManager args;
        args.ForceSetArg("-datacarriercost", value);
        BOOST_TEST_CONTEXT(value) {
            BOOST_CHECK(ValidateRootsPolicyOptions(args));
            const auto parsed = ParseDataCarrierCost(value);
            BOOST_REQUIRE(parsed);
            BOOST_CHECK_EQUAL(*parsed, weight);
        }
    }
    for (const std::string value : {"", "-1", "+1", "01", ".5", "1.", "0.001", " 1", "1 ", "1junk", "1073741823.76", "1e100", "9223372036854775808"}) {
        ArgsManager args;
        args.ForceSetArg("-datacarriercost", value);
        BOOST_TEST_CONTEXT(value) { BOOST_CHECK(!ValidateRootsPolicyOptions(args)); }
    }
}

BOOST_AUTO_TEST_CASE(mode_tokens)
{
    const std::vector<std::pair<std::string, std::vector<std::string>>> modes{
        {"mempoolreplacement", {"fee", "optin", "-optin", "fee,optin", "fee+-optin", "optin,-optin,fee"}},
        {"mempooltruc", {"reject", "accept", "enforce", "optin", "-enforce", "-optin", "accept+enforce", "reject,accept", "0,enforce"}},
        {"permitephemeral", {"anchor", "dust", "send", "reject", "-anchor", "-dust", "-send", "-reject", "-0", "anchor+dust,-send", "0,anchor"}},
        {"spkreuse", {"allow", "conflict"}}};
    for (const auto& [option, tokens] : modes) {
        auto accepted{tokens};
        accepted.insert(accepted.end(), {"", "0", "1"});
        for (const auto& value : accepted) {
            ArgsManager args;
            args.ForceSetArg("-" + option, value);
            BOOST_TEST_CONTEXT(option << "=" << value) { BOOST_CHECK(ValidateRootsPolicyOptions(args)); }
        }
        for (const auto& value : std::vector<std::string>{"junk", "2", "01", "1junk", " 1", "1 ", tokens[0] + ",junk", "," + tokens[0], tokens[0] + ",", tokens[0] + ",," + tokens[0]}) {
            ArgsManager args;
            args.ForceSetArg("-" + option, value);
            BOOST_TEST_CONTEXT(option << "=" << value) { BOOST_CHECK(!ValidateRootsPolicyOptions(args)); }
        }
    }
}

BOOST_AUTO_TEST_CASE(raw_negation_is_scoped)
{
    auto options{BOOL_OPTIONS};
    options.insert(options.end(), {"maxscriptsize", "bytespersigopstrict", "maxtxlegacysigops", "minrelaycoinblocks", "minrelaymaturity", "blockmaxsize", "blockprioritysize", "datacarriercost", "dustdynamic", "mempoolreplacement", "mempooltruc", "permitephemeral", "spkreuse"});
    for (const auto& option : options) {
        ArgsManager args;
        SetupServerArgs(args);
        std::string error;
        for (const std::string value : {"junk", "2", "01", "+1", "1junk", "1.0", " 1"}) {
            BOOST_TEST_CONTEXT(option << " negated=" << value) {
                BOOST_CHECK(!ParseOptions(args, {"-no" + option + "=" + value, "-" + option + "=1"}, error));
                BOOST_CHECK(error.find("-no" + option) != std::string::npos);
            }
        }
        for (const std::string value : {"", "0", "1"}) {
            BOOST_REQUIRE(ParseOptions(args, {"-no" + option + "=" + value}, error));
            BOOST_CHECK_EQUAL(args.GetArg("-" + option, "missing"), value == "0" ? "1" : "0");
        }
        BOOST_REQUIRE(ParseOptions(args, {"-no" + option}, error));
        BOOST_CHECK_EQUAL(args.GetArg("-" + option, "missing"), "0");
    }
    ArgsManager args;
    SetupServerArgs(args);
    std::string error;
    BOOST_REQUIRE(ParseOptions(args, {"-nodatacarrier=junk", "-bytespersigop=12junk", "-datacarriersize=83junk"}, error));
    BOOST_CHECK(args.GetBoolArg("-datacarrier", false));
    BOOST_CHECK_EQUAL(args.GetIntArg("-bytespersigop", 0), 12);
    BOOST_CHECK_EQUAL(args.GetIntArg("-datacarriersize", 0), 83);
    BOOST_CHECK(ValidateRootsPolicyOptions(args));
}

BOOST_AUTO_TEST_CASE(local_defaults_and_ephemeral_semantics)
{
    ArgsManager args;
    kernel::MemPoolOptions options;
    BOOST_REQUIRE(ApplyArgsManOptions(args, Params(), options));
    BOOST_CHECK_EQUAL(g_script_size_policy_limit, 1650);
    BOOST_CHECK_EQUAL(nBytesPerSigOpStrict, 20);
    BOOST_CHECK(options.reject_parasites && options.acceptunknownwitness);
    BOOST_CHECK(options.datacarrier_fullcount && options.permitbareanchor);
    BOOST_CHECK(!options.reject_tokens && !options.permit_bare_pubkey && !options.permit_bare_multisig);
    BOOST_CHECK(!options.permitbaredatacarrier && !options.accept_non_std_datacarrier);
    BOOST_CHECK_EQUAL(options.maxtxlegacysigops, 2500);
    BOOST_CHECK_EQUAL(options.minrelaycoinblocks, 0);
    BOOST_CHECK_EQUAL(options.minrelaymaturity, 0);
    BOOST_CHECK_EQUAL(options.max_datacarrier_bytes.value(), 83);
    BOOST_CHECK_EQUAL(options.min_relay_feerate.GetFeePerK(), 1000);
    BOOST_CHECK_EQUAL(options.incremental_relay_feerate.GetFeePerK(), 100);
    BOOST_CHECK(options.permitephemeral_anchor && !options.permitephemeral_dust && !options.permitephemeral_send);
    args.ForceSetArg("-permitephemeral", "0,anchor,dust,-send");
    BOOST_REQUIRE(ApplyArgsManOptions(args, Params(), options));
    BOOST_CHECK(options.permitephemeral_anchor && options.permitephemeral_dust && !options.permitephemeral_send);
    args.ForceSetArg("-permitephemeral", "1");
    BOOST_REQUIRE(ApplyArgsManOptions(args, Params(), options));
    BOOST_CHECK(options.permitephemeral_anchor && options.permitephemeral_dust && options.permitephemeral_send);
    args.ForceSetArg("-permitephemeral", "reject");
    BOOST_REQUIRE(ApplyArgsManOptions(args, Params(), options));
    BOOST_CHECK(!options.permitephemeral_anchor && !options.permitephemeral_dust && !options.permitephemeral_send);
}

BOOST_AUTO_TEST_CASE(settings_types_and_precedence)
{
    ArgsManager args;
    SetupServerArgs(args);
    for (const auto& value : std::vector<UniValue>{UniValue{true}, UniValue{false}, UniValue{1}, UniValue{"0"}}) {
        args.LockSettings([&](common::Settings& settings) { settings.rw_settings["subdustfeepenalty"] = value; });
        BOOST_CHECK(ValidateRootsPolicyOptions(args));
    }
    for (const auto& value : std::vector<UniValue>{UniValue{2}, UniValue{UniValue::VARR}, UniValue{UniValue::VOBJ}}) {
        args.LockSettings([&](common::Settings& settings) { settings.rw_settings["subdustfeepenalty"] = value; });
        BOOST_CHECK(!ValidateRootsPolicyOptions(args));
    }
    std::string error;
    BOOST_REQUIRE(ParseOptions(args, {"-subdustfeepenalty=junk", "-subdustfeepenalty=1"}, error));
    BOOST_CHECK(ValidateRootsPolicyOptions(args));
    BOOST_REQUIRE(ParseOptions(args, {"-subdustfeepenalty=1", "-subdustfeepenalty=junk"}, error));
    BOOST_CHECK(!ValidateRootsPolicyOptions(args));
}

BOOST_AUTO_TEST_CASE(dynamic_dust_grammar_without_runtime_claim)
{
    const auto target = ParseDustDynamicOpt("1e0*target:2", 1008);
    BOOST_REQUIRE(target);
    BOOST_CHECK_EQUAL(target->first, -2);
    BOOST_CHECK_EQUAL(target->second, 1000);
    const auto position = ParseDustDynamicOpt("1.0000*mempool:01", 1008);
    BOOST_REQUIRE(position);
    BOOST_CHECK_EQUAL(position->first, 1);
    for (const auto& [value, expected_target, expected_multiplier] : std::vector<std::tuple<std::string, int32_t, int>>{
             {"off", 0, 3000}, {"0", 0, 3000}, {"1e0*target:2", -2, 1000}, {"2147483.647*mempool:2147483647", INT32_MAX, INT32_MAX}}) {
        ArgsManager args;
        args.ForceSetArg("-dustdynamic", value);
        kernel::MemPoolOptions options;
        BOOST_REQUIRE(ApplyArgsManOptions(args, Params(), options));
        BOOST_CHECK_EQUAL(options.dust_relay_target, expected_target);
        BOOST_CHECK_EQUAL(options.dust_relay_multiplier, expected_multiplier);
    }
    for (const std::string value : {"", "target:1", "target:2junk", "mempool:+1", "mempool:0", "mempool:2147483648", "0*mempool:1", "1.0001*mempool:1", "junk"}) {
        ArgsManager args;
        args.ForceSetArg("-dustdynamic", value);
        kernel::MemPoolOptions options;
        BOOST_TEST_CONTEXT(value) { BOOST_CHECK(!ApplyArgsManOptions(args, Params(), options)); }
    }
}

BOOST_AUTO_TEST_SUITE_END()
