// Copyright (c) 2026 The Bitcoin Roots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/args.h>
#include <common/settings.h>
#include <init.h>
#include <node/mempool_args.h>
#include <test/fuzz/FuzzedDataProvider.h>
#include <test/fuzz/fuzz.h>
#include <test/util/roots_policy_oracles.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/check.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace {
void InitializePolicyArgs()
{
    static const auto setup = MakeNoLogFileContext<const BasicTestingSetup>();
}

} // namespace

FUZZ_TARGET(policy_args, .init = InitializePolicyArgs)
{
    FuzzedDataProvider provider{buffer.data(), buffer.size()};
    // The complete remaining input is the value: embedded NULs are not discarded.
    const auto value = provider.ConsumeRemainingBytesAsString();
    for (const char* option : {"-subdustfeepenalty", "-rejectparasites", "-acceptunknownwitness",
             "-datacarrierfullcount", "-permitbareanchor", "-rejecttokens", "-permitbarepubkey",
             "-permitbaredatacarrier", "-acceptnonstddatacarrier"}) {
        ArgsManager args;
        args.ForceSetArg(option, value);
        Assert(bool(ValidateRootsPolicyOptions(args)) == (value.empty() || value == "0" || value == "1"));
    }
    for (const auto& [option, maximum] : {
             std::pair<const char*, uint64_t>{"-maxscriptsize", UINT32_MAX},
             {"-bytespersigopstrict", UINT32_MAX}, {"-maxtxlegacysigops", UINT32_MAX},
             {"-minrelaycoinblocks", INT64_MAX}, {"-minrelaymaturity", INT32_MAX},
             {"-blockmaxsize", 4'000'000}, {"-blockprioritysize", INT64_MAX}}) {
        ArgsManager args;
        args.ForceSetArg(option, value);
        Assert(bool(ValidateRootsPolicyOptions(args)) == roots_test::ValidInteger(value, maximum));
    }
    for (const char* option : {"-datacarriercost", "-mempoolreplacement", "-mempooltruc", "-permitephemeral", "-spkreuse"}) {
        ArgsManager args;
        args.ForceSetArg(option, value);
        const bool valid{bool(ValidateRootsPolicyOptions(args))};
        if (valid) {
            // Neither a valid prefix nor a recognized list token permits trailing junk.
            args.ForceSetArg(option, value + '\xff');
            Assert(!ValidateRootsPolicyOptions(args));
        }
        if (std::string_view{option} == "-datacarriercost") {
            const auto parsed{ParseDataCarrierCost(value)};
            Assert(bool(parsed) == valid);
            if (parsed) {
                // The rounded weight has an exact, canonical quarter-vbyte spelling.
                const auto weight{*parsed};
                const auto canonical{std::to_string(weight / 4) + "." + std::to_string(100 + 25 * (weight % 4)).substr(1)};
                const auto roundtrip{ParseDataCarrierCost(canonical)};
                Assert(roundtrip && *roundtrip == weight);
            }
        }
    }
    const auto dynamic{ParseDustDynamicOpt(value, /*max_fee_estimate_blocks=*/1008)};
    if (dynamic) {
        const auto [target, multiplier]{*dynamic};
        Assert(multiplier > 0);
        Assert(target >= 1 || (target <= -2 && target >= -1008) || target == 0);
        const auto mode{target == 0 ? "off" : std::to_string(multiplier / 1000) + "." +
            std::to_string(1000 + multiplier % 1000).substr(1) + "*" +
            (target < 0 ? "target:" : "mempool:") + std::to_string(target < 0 ? -target : target)};
        const auto roundtrip{ParseDustDynamicOpt(mode, 1008)};
        Assert(roundtrip && *roundtrip == *dynamic);
    }
    UniValue setting;
    if (setting.read(value)) {
        ArgsManager args;
        args.LockSettings([&](common::Settings& settings) { settings.rw_settings["subdustfeepenalty"] = setting; });
        const auto result{ValidateRootsPolicyOptions(args)};
        if (setting.isArray() || setting.isObject()) Assert(!result);
        if (setting.isNull() || setting.isBool()) Assert(result);
    }
    // Check raw negation before ArgsManager's Boolean coercion can hide junk.
    // argv is NUL-terminated; embedded NULs above remain covered through ForceSetArg.
    if (value.find('\0') == value.npos) {
        ArgsManager args;
        SetupServerArgs(args);
        const std::string argument{"-nosubdustfeepenalty=" + value};
        const std::array<const char*, 2> argv{"fuzz", argument.c_str()};
        std::string error;
        Assert(args.ParseParameters(argv.size(), argv.data(), error) == (value.empty() || value == "0" || value == "1"));
    }
}
