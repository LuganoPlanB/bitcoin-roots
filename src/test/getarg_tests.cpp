// Copyright (c) 2012-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/args.h>
#include <common/settings.h>
#include <logging.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/strencodings.h>

#include <fstream>
#include <limits>
#include <sstream>
#include <streambuf>
#include <string>
#include <utility>
#include <vector>

#include <boost/test/unit_test.hpp>

using util::SplitString;

BOOST_FIXTURE_TEST_SUITE(getarg_tests, BasicTestingSetup)

void ResetArgs(ArgsManager& local_args, const std::string& strArg)
{
    std::vector<std::string> vecArg;
    if (strArg.size()) {
        vecArg = SplitString(strArg, ' ');
    }

    // Insert dummy executable name:
    vecArg.insert(vecArg.begin(), "testbitcoin");

    // Convert to char*:
    std::vector<const char*> vecChar;
    vecChar.reserve(vecArg.size());
    for (const std::string& s : vecArg)
        vecChar.push_back(s.c_str());

    std::string error;
    BOOST_CHECK(local_args.ParseParameters(vecChar.size(), vecChar.data(), error));
}

void SetupArgs(ArgsManager& local_args, const std::vector<std::pair<std::string, unsigned int>>& args)
{
    for (const auto& arg : args) {
        local_args.AddArg(arg.first, "", arg.second, OptionsCategory::OPTIONS);
    }
}

namespace {
// Report a real I/O failure after a chosen number of bytes, optionally only
// after the formatter rewinds to copy an invalid file into comments.
class FailingConfigBuffer : public std::streambuf {
    std::string m_text;
    size_t m_position{0};
    size_t m_fail_at;
    bool m_fail_after_rewind;
    bool m_rewound{false};

    int_type underflow() override
    {
        if (m_position == m_fail_at && (!m_fail_after_rewind || m_rewound)) {
            throw std::ios_base::failure("Injected config read failure");
        }
        if (m_position == m_text.size()) return traits_type::eof();
        return traits_type::to_int_type(m_text[m_position]);
    }
    int_type uflow() override
    {
        const auto value{underflow()};
        if (!traits_type::eq_int_type(value, traits_type::eof())) ++m_position;
        return value;
    }
    pos_type seekoff(off_type offset, std::ios_base::seekdir direction, std::ios_base::openmode) override
    {
        if (direction != std::ios_base::beg || offset != 0) return pos_type{off_type{-1}};
        m_position = 0;
        m_rewound = true;
        return pos_type{0};
    }
public:
    FailingConfigBuffer(std::string text, size_t fail_at, bool fail_after_rewind = false)
        : m_text(std::move(text)), m_fail_at(fail_at), m_fail_after_rewind(fail_after_rewind) {}
};
} // namespace

// Test behavior of GetArg functions when string, integer, and boolean types
// are specified in the settings.json file. GetArg functions are convenience
// functions. The GetSetting method can always be used instead of GetArg
// methods to retrieve original values, and there's not always an objective
// answer to what GetArg behavior is best in every case. This test makes sure
// there's test coverage for whatever the current behavior is, so it's not
// broken or changed unintentionally.
BOOST_AUTO_TEST_CASE(setting_args)
{
    ArgsManager args;
    SetupArgs(args, {{"-foo", ArgsManager::ALLOW_ANY}});

    auto set_foo = [&](const common::SettingsValue& value) {
      args.LockSettings([&](common::Settings& settings) {
        settings.rw_settings["foo"] = value;
      });
    };

    set_foo("str");
    BOOST_CHECK_EQUAL(args.GetSetting("foo").write(), "\"str\"");
    BOOST_CHECK_EQUAL(args.GetArg("foo", "default"), "str");
    BOOST_CHECK_EQUAL(args.GetIntArg("foo", 100), 0);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", true), false);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", false), false);

    set_foo("99");
    BOOST_CHECK_EQUAL(args.GetSetting("foo").write(), "\"99\"");
    BOOST_CHECK_EQUAL(args.GetArg("foo", "default"), "99");
    BOOST_CHECK_EQUAL(args.GetIntArg("foo", 100), 99);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", true), true);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", false), true);

    set_foo("3.25");
    BOOST_CHECK_EQUAL(args.GetSetting("foo").write(), "\"3.25\"");
    BOOST_CHECK_EQUAL(args.GetArg("foo", "default"), "3.25");
    BOOST_CHECK_EQUAL(args.GetIntArg("foo", 100), 3);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", true), true);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", false), true);

    set_foo("0");
    BOOST_CHECK_EQUAL(args.GetSetting("foo").write(), "\"0\"");
    BOOST_CHECK_EQUAL(args.GetArg("foo", "default"), "0");
    BOOST_CHECK_EQUAL(args.GetIntArg("foo", 100), 0);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", true), false);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", false), false);

    set_foo("");
    BOOST_CHECK_EQUAL(args.GetSetting("foo").write(), "\"\"");
    BOOST_CHECK_EQUAL(args.GetArg("foo", "default"), "");
    BOOST_CHECK_EQUAL(args.GetIntArg("foo", 100), 0);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", true), true);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", false), true);

    set_foo(99);
    BOOST_CHECK_EQUAL(args.GetSetting("foo").write(), "99");
    BOOST_CHECK_EQUAL(args.GetArg("foo", "default"), "99");
    BOOST_CHECK_EQUAL(args.GetIntArg("foo", 100), 99);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", true), true);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", false), true);

    set_foo(3.25);
    BOOST_CHECK_EQUAL(args.GetSetting("foo").write(), "3.25");
    BOOST_CHECK_EQUAL(args.GetArg("foo", "default"), "3.25");
    BOOST_CHECK_THROW(args.GetIntArg("foo", 100), std::runtime_error);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", true), true);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", false), true);

    set_foo(0);
    BOOST_CHECK_EQUAL(args.GetSetting("foo").write(), "0");
    BOOST_CHECK_EQUAL(args.GetArg("foo", "default"), "0");
    BOOST_CHECK_EQUAL(args.GetIntArg("foo", 100), 0);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", true), false);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", false), false);

    set_foo(true);
    BOOST_CHECK_EQUAL(args.GetSetting("foo").write(), "true");
    BOOST_CHECK_EQUAL(args.GetArg("foo", "default"), "1");
    BOOST_CHECK_EQUAL(args.GetIntArg("foo", 100), 1);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", true), true);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", false), true);

    set_foo(false);
    BOOST_CHECK_EQUAL(args.GetSetting("foo").write(), "false");
    BOOST_CHECK_EQUAL(args.GetArg("foo", "default"), "0");
    BOOST_CHECK_EQUAL(args.GetIntArg("foo", 100), 0);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", true), false);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", false), false);

    set_foo(UniValue::VOBJ);
    BOOST_CHECK_EQUAL(args.GetSetting("foo").write(), "{}");
    BOOST_CHECK_THROW(args.GetArg("foo", "default"), std::runtime_error);
    BOOST_CHECK_THROW(args.GetIntArg("foo", 100), std::runtime_error);
    BOOST_CHECK_THROW(args.GetBoolArg("foo", true), std::runtime_error);
    BOOST_CHECK_THROW(args.GetBoolArg("foo", false), std::runtime_error);

    set_foo(UniValue::VARR);
    BOOST_CHECK_EQUAL(args.GetSetting("foo").write(), "[]");
    BOOST_CHECK_THROW(args.GetArg("foo", "default"), std::runtime_error);
    BOOST_CHECK_THROW(args.GetIntArg("foo", 100), std::runtime_error);
    BOOST_CHECK_THROW(args.GetBoolArg("foo", true), std::runtime_error);
    BOOST_CHECK_THROW(args.GetBoolArg("foo", false), std::runtime_error);

    set_foo(UniValue::VNULL);
    BOOST_CHECK_EQUAL(args.GetSetting("foo").write(), "null");
    BOOST_CHECK_EQUAL(args.GetArg("foo", "default"), "default");
    BOOST_CHECK_EQUAL(args.GetIntArg("foo", 100), 100);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", true), true);
    BOOST_CHECK_EQUAL(args.GetBoolArg("foo", false), false);
}

BOOST_AUTO_TEST_CASE(boolarg)
{
    ArgsManager local_args;

    const auto foo = std::make_pair("-foo", ArgsManager::ALLOW_ANY);
    SetupArgs(local_args, {foo});
    ResetArgs(local_args, "-foo");
    BOOST_CHECK(local_args.GetBoolArg("-foo", false));
    BOOST_CHECK(local_args.GetBoolArg("-foo", true));

    BOOST_CHECK(!local_args.GetBoolArg("-fo", false));
    BOOST_CHECK(local_args.GetBoolArg("-fo", true));

    BOOST_CHECK(!local_args.GetBoolArg("-fooo", false));
    BOOST_CHECK(local_args.GetBoolArg("-fooo", true));

    ResetArgs(local_args, "-foo=0");
    BOOST_CHECK(!local_args.GetBoolArg("-foo", false));
    BOOST_CHECK(!local_args.GetBoolArg("-foo", true));

    ResetArgs(local_args, "-foo=1");
    BOOST_CHECK(local_args.GetBoolArg("-foo", false));
    BOOST_CHECK(local_args.GetBoolArg("-foo", true));

    // New 0.6 feature: auto-map -nosomething to !-something:
    ResetArgs(local_args, "-nofoo");
    BOOST_CHECK(!local_args.GetBoolArg("-foo", false));
    BOOST_CHECK(!local_args.GetBoolArg("-foo", true));

    ResetArgs(local_args, "-nofoo=1");
    BOOST_CHECK(!local_args.GetBoolArg("-foo", false));
    BOOST_CHECK(!local_args.GetBoolArg("-foo", true));

    ResetArgs(local_args, "-foo -nofoo"); // -nofoo should win
    BOOST_CHECK(!local_args.GetBoolArg("-foo", false));
    BOOST_CHECK(!local_args.GetBoolArg("-foo", true));

    ResetArgs(local_args, "-foo=1 -nofoo=1"); // -nofoo should win
    BOOST_CHECK(!local_args.GetBoolArg("-foo", false));
    BOOST_CHECK(!local_args.GetBoolArg("-foo", true));

    ResetArgs(local_args, "-foo=0 -nofoo=0"); // -nofoo=0 should win
    BOOST_CHECK(local_args.GetBoolArg("-foo", false));
    BOOST_CHECK(local_args.GetBoolArg("-foo", true));

    // New 0.6 feature: treat -- same as -:
    ResetArgs(local_args, "--foo=1");
    BOOST_CHECK(local_args.GetBoolArg("-foo", false));
    BOOST_CHECK(local_args.GetBoolArg("-foo", true));

    ResetArgs(local_args, "--nofoo=1");
    BOOST_CHECK(!local_args.GetBoolArg("-foo", false));
    BOOST_CHECK(!local_args.GetBoolArg("-foo", true));
}

BOOST_AUTO_TEST_CASE(stringarg)
{
    ArgsManager local_args;

    const auto foo = std::make_pair("-foo", ArgsManager::ALLOW_ANY);
    const auto bar = std::make_pair("-bar", ArgsManager::ALLOW_ANY);
    SetupArgs(local_args, {foo, bar});
    ResetArgs(local_args, "");
    BOOST_CHECK_EQUAL(local_args.GetArg("-foo", ""), "");
    BOOST_CHECK_EQUAL(local_args.GetArg("-foo", "eleven"), "eleven");

    ResetArgs(local_args, "-foo -bar");
    BOOST_CHECK_EQUAL(local_args.GetArg("-foo", ""), "");
    BOOST_CHECK_EQUAL(local_args.GetArg("-foo", "eleven"), "");

    ResetArgs(local_args, "-foo=");
    BOOST_CHECK_EQUAL(local_args.GetArg("-foo", ""), "");
    BOOST_CHECK_EQUAL(local_args.GetArg("-foo", "eleven"), "");

    ResetArgs(local_args, "-foo=11");
    BOOST_CHECK_EQUAL(local_args.GetArg("-foo", ""), "11");
    BOOST_CHECK_EQUAL(local_args.GetArg("-foo", "eleven"), "11");

    ResetArgs(local_args, "-foo=eleven");
    BOOST_CHECK_EQUAL(local_args.GetArg("-foo", ""), "eleven");
    BOOST_CHECK_EQUAL(local_args.GetArg("-foo", "eleven"), "eleven");
}

BOOST_AUTO_TEST_CASE(intarg)
{
    ArgsManager local_args;

    const auto foo = std::make_pair("-foo", ArgsManager::ALLOW_ANY);
    const auto bar = std::make_pair("-bar", ArgsManager::ALLOW_ANY);
    SetupArgs(local_args, {foo, bar});

    ResetArgs(local_args, "");
    BOOST_CHECK(!local_args.GetArg<int64_t>("-foo").has_value());
    BOOST_CHECK(!local_args.GetArg<uint8_t>("-bar").has_value());
    BOOST_CHECK_EQUAL(local_args.GetIntArg("-foo", 11), 11);
    BOOST_CHECK_EQUAL(local_args.GetIntArg("-foo", 0), 0);
    BOOST_CHECK_EQUAL(local_args.GetArg("-bar", uint8_t{222}), 222);
    BOOST_CHECK_EQUAL(local_args.GetArg("-bar", uint8_t{0}), 0);

    ResetArgs(local_args, "-foo -bar");
    BOOST_CHECK_EQUAL(local_args.GetArg<int64_t>("-foo"), 0);
    BOOST_CHECK_EQUAL(local_args.GetArg<uint8_t>("-bar"), 0);
    BOOST_CHECK_EQUAL(local_args.GetIntArg("-foo", 11), 0);
    BOOST_CHECK_EQUAL(local_args.GetArg("-bar", uint8_t{222}), 0);

    // Check under-/overflow behavior.
    ResetArgs(local_args, "-foo=-9223372036854775809 -bar=9223372036854775808");
    BOOST_CHECK_EQUAL(local_args.GetArg<int64_t>("-foo"), std::numeric_limits<int64_t>::min());
    BOOST_CHECK_EQUAL(local_args.GetArg<uint8_t>("-bar"), std::numeric_limits<uint8_t>::max());
    BOOST_CHECK_EQUAL(local_args.GetIntArg("-foo", 0), std::numeric_limits<int64_t>::min());
    BOOST_CHECK_EQUAL(local_args.GetIntArg("-bar", 0), std::numeric_limits<int64_t>::max());
    BOOST_CHECK_EQUAL(local_args.GetArg("-foo", uint8_t{0}), std::numeric_limits<uint8_t>::min());
    BOOST_CHECK_EQUAL(local_args.GetArg("-bar", uint8_t{0}), std::numeric_limits<uint8_t>::max());

    ResetArgs(local_args, "-foo=11 -bar=12");
    BOOST_CHECK_EQUAL(local_args.GetArg<int64_t>("-foo"), 11);
    BOOST_CHECK_EQUAL(local_args.GetArg<uint8_t>("-bar"), 12);
    BOOST_CHECK_EQUAL(local_args.GetIntArg("-foo", 0), 11);
    BOOST_CHECK_EQUAL(local_args.GetArg("-bar", uint8_t{11}), 12);

    ResetArgs(local_args, "-foo=NaN -bar=NotANumber");
    BOOST_CHECK_EQUAL(local_args.GetArg<int64_t>("-foo"), 0);
    BOOST_CHECK_EQUAL(local_args.GetArg<uint8_t>("-bar"), 0);
    BOOST_CHECK_EQUAL(local_args.GetIntArg("-foo", 1), 0);
    BOOST_CHECK_EQUAL(local_args.GetArg("-bar", uint8_t{11}), 0);
}

BOOST_AUTO_TEST_CASE(patharg)
{
    ArgsManager local_args;

    const auto dir = std::make_pair("-dir", ArgsManager::ALLOW_ANY);
    SetupArgs(local_args, {dir});
    ResetArgs(local_args, "");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), fs::path{});

    const fs::path root_path{"/"};
    ResetArgs(local_args, "-dir=/");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), root_path);

    ResetArgs(local_args, "-dir=/.");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), root_path);

    ResetArgs(local_args, "-dir=/./");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), root_path);

    ResetArgs(local_args, "-dir=/.//");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), root_path);

#ifdef WIN32
    const fs::path win_root_path{"C:\\"};
    ResetArgs(local_args, "-dir=C:\\");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), win_root_path);

    ResetArgs(local_args, "-dir=C:/");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), win_root_path);

    ResetArgs(local_args, "-dir=C:\\\\");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), win_root_path);

    ResetArgs(local_args, "-dir=C:\\.");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), win_root_path);

    ResetArgs(local_args, "-dir=C:\\.\\");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), win_root_path);

    ResetArgs(local_args, "-dir=C:\\.\\\\");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), win_root_path);
#endif

    const fs::path absolute_path{"/home/user/.bitcoin"};
    ResetArgs(local_args, "-dir=/home/user/.bitcoin");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), absolute_path);

    ResetArgs(local_args, "-dir=/root/../home/user/.bitcoin");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), absolute_path);

    ResetArgs(local_args, "-dir=/home/./user/.bitcoin");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), absolute_path);

    ResetArgs(local_args, "-dir=/home/user/.bitcoin/");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), absolute_path);

    ResetArgs(local_args, "-dir=/home/user/.bitcoin//");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), absolute_path);

    ResetArgs(local_args, "-dir=/home/user/.bitcoin/.");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), absolute_path);

    ResetArgs(local_args, "-dir=/home/user/.bitcoin/./");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), absolute_path);

    ResetArgs(local_args, "-dir=/home/user/.bitcoin/.//");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), absolute_path);

    const fs::path relative_path{"user/.bitcoin"};
    ResetArgs(local_args, "-dir=user/.bitcoin");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), relative_path);

    ResetArgs(local_args, "-dir=somewhere/../user/.bitcoin");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), relative_path);

    ResetArgs(local_args, "-dir=user/./.bitcoin");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), relative_path);

    ResetArgs(local_args, "-dir=user/.bitcoin/");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), relative_path);

    ResetArgs(local_args, "-dir=user/.bitcoin//");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), relative_path);

    ResetArgs(local_args, "-dir=user/.bitcoin/.");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), relative_path);

    ResetArgs(local_args, "-dir=user/.bitcoin/./");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), relative_path);

    ResetArgs(local_args, "-dir=user/.bitcoin/.//");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir"), relative_path);

    // Check negated and default argument handling. Specifying an empty argument
    // is the same as not specifying the argument. This is convenient for
    // scripting so later command line arguments can override earlier command
    // line arguments or bitcoin.conf values. Currently the -dir= case cannot be
    // distinguished from -dir case with no assignment, but #16545 would add the
    // ability to distinguish these in the future (and treat the no-assign case
    // like an imperative command or an error).
    ResetArgs(local_args, "");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir", "default"), fs::path{"default"});
    ResetArgs(local_args, "-dir=override");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir", "default"), fs::path{"override"});
    ResetArgs(local_args, "-dir=");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir", "default"), fs::path{"default"});
    ResetArgs(local_args, "-dir");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir", "default"), fs::path{"default"});
    ResetArgs(local_args, "-nodir");
    BOOST_CHECK_EQUAL(local_args.GetPathArg("-dir", "default"), fs::path{""});
}

BOOST_AUTO_TEST_CASE(doubledash)
{
    ArgsManager local_args;

    const auto foo = std::make_pair("-foo", ArgsManager::ALLOW_ANY);
    const auto bar = std::make_pair("-bar", ArgsManager::ALLOW_ANY);
    SetupArgs(local_args, {foo, bar});
    ResetArgs(local_args, "--foo");
    BOOST_CHECK_EQUAL(local_args.GetBoolArg("-foo", false), true);

    ResetArgs(local_args, "--foo=verbose --bar=1");
    BOOST_CHECK_EQUAL(local_args.GetArg("-foo", ""), "verbose");
    BOOST_CHECK_EQUAL(local_args.GetIntArg("-bar", 0), 1);
}

BOOST_AUTO_TEST_CASE(boolargno)
{
    ArgsManager local_args;

    const auto foo = std::make_pair("-foo", ArgsManager::ALLOW_ANY);
    const auto bar = std::make_pair("-bar", ArgsManager::ALLOW_ANY);
    SetupArgs(local_args, {foo, bar});
    ResetArgs(local_args, "-nofoo");
    BOOST_CHECK(!local_args.GetBoolArg("-foo", true));
    BOOST_CHECK(!local_args.GetBoolArg("-foo", false));

    ResetArgs(local_args, "-nofoo=1");
    BOOST_CHECK(!local_args.GetBoolArg("-foo", true));
    BOOST_CHECK(!local_args.GetBoolArg("-foo", false));

    ResetArgs(local_args, "-nofoo=0");
    BOOST_CHECK(local_args.GetBoolArg("-foo", true));
    BOOST_CHECK(local_args.GetBoolArg("-foo", false));

    ResetArgs(local_args, "-foo --nofoo"); // --nofoo should win
    BOOST_CHECK(!local_args.GetBoolArg("-foo", true));
    BOOST_CHECK(!local_args.GetBoolArg("-foo", false));

    ResetArgs(local_args, "-nofoo -foo"); // foo always wins:
    BOOST_CHECK(local_args.GetBoolArg("-foo", true));
    BOOST_CHECK(local_args.GetBoolArg("-foo", false));
}

BOOST_AUTO_TEST_CASE(logargs)
{
    ArgsManager local_args;

    const auto okaylog_bool = std::make_pair("-okaylog-bool", ArgsManager::ALLOW_ANY);
    const auto okaylog_negbool = std::make_pair("-okaylog-negbool", ArgsManager::ALLOW_ANY);
    const auto okaylog = std::make_pair("-okaylog", ArgsManager::ALLOW_ANY);
    const auto dontlog = std::make_pair("-dontlog", ArgsManager::ALLOW_ANY | ArgsManager::SENSITIVE);
    SetupArgs(local_args, {okaylog_bool, okaylog_negbool, okaylog, dontlog});
    ResetArgs(local_args, "-okaylog-bool -nookaylog-negbool -okaylog=public -dontlog=private42");

    // Everything logged to debug.log will also append to str
    std::string str;
    auto print_connection = LogInstance().PushBackCallback(
        [&str](const std::string& s) {
            str += s;
        });

    local_args.LockSettings([](common::Settings& settings) {
        settings.rw_config["dontlog"] = {"private-rw42"};
    });
    // Log the arguments
    local_args.LogArgs();

    LogInstance().DeleteCallback(print_connection);
    // Check that what should appear does, and what shouldn't doesn't.
    BOOST_CHECK(str.find("Command-line arg: okaylog-bool=\"\"") != std::string::npos);
    BOOST_CHECK(str.find("Command-line arg: okaylog-negbool=false") != std::string::npos);
    BOOST_CHECK(str.find("Command-line arg: okaylog=\"public\"") != std::string::npos);
    BOOST_CHECK(str.find("dontlog=****") != std::string::npos);
    BOOST_CHECK(str.find("private42") == std::string::npos);
    BOOST_CHECK(str.find("private-rw42") == std::string::npos);
}

BOOST_AUTO_TEST_CASE(rw_config_stream)
{
    std::istringstream input{"# operator settings\nprune=5\n[regtest]\nprune=1\n"};
    std::ostringstream output;
    ModifyRWConfigStream(input, output, {{"listen", "0"}, {"prune", "550"}});
    BOOST_CHECK_EQUAL(output.str(), "# operator settings\nprune=550\nlisten=0\n[regtest]\nprune=1\n");

    std::istringstream empty;
    std::ostringstream rejected;
    BOOST_CHECK_THROW(ModifyRWConfigStream(empty, rejected, {{"listen", "0\nprune=0"}}), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(config_read_failures)
{
    class ReadArgs : public ArgsManager {
    public:
        using ArgsManager::ReadConfigStream;
    };
    const std::string text{"foo=original\nbar=tail"};
    for (const size_t fail_at : {size_t{0}, size_t{4}, text.size() - 1}) {
        FailingConfigBuffer buffer{text, fail_at};
        std::istream input{&buffer};
        std::ostringstream output;
        BOOST_CHECK_THROW(ModifyRWConfigStream(input, output, {{"foo", "new"}}), std::ios_base::failure);
        BOOST_CHECK(input.bad());

        FailingConfigBuffer parser_buffer{text, fail_at};
        std::istream parser_input{&parser_buffer};
        ReadArgs args;
        SetupArgs(args, {{"-foo", ArgsManager::ALLOW_ANY}, {"-bar", ArgsManager::ALLOW_ANY}});
        std::map<std::string, std::vector<common::SettingsValue>> target{{"foo", {"kept"}}};
        std::string error;
        BOOST_CHECK(!args.ReadConfigStream(parser_input, "fault.conf", error, false, &target));
        BOOST_CHECK_EQUAL(error, "Failed to read config file \"fault.conf\".");
        BOOST_CHECK_EQUAL(target.size(), 1U);
        BOOST_CHECK_EQUAL(target.at("foo").front().get_str(), "kept");
        BOOST_CHECK(!args.IsArgSet("-foo"));
    }
    FailingConfigBuffer copy_buffer{"invalid\nfoo=old", 12, /*fail_after_rewind=*/true};
    std::istream copy_input{&copy_buffer};
    std::ostringstream copy_output;
    BOOST_CHECK_THROW(ModifyRWConfigStream(copy_input, copy_output, {{"foo", "new"}}), std::ios_base::failure);
    BOOST_CHECK(copy_input.bad());

    std::istringstream failed_input{"foo=old"};
    failed_input.setstate(std::ios::failbit);
    std::ostringstream failed_output;
    BOOST_CHECK_THROW(ModifyRWConfigStream(failed_input, failed_output, {{"foo", "new"}}), std::ios_base::failure);

    for (const std::string original : {"", "foo=old", "foo=old\n"}) {
        std::istringstream input{original};
        std::ostringstream output;
        ModifyRWConfigStream(input, output, {{"foo", "new"}});
        BOOST_CHECK_EQUAL(output.str(), original.empty() || original.ends_with('\n') ? "foo=new\n" : "foo=new");
    }
    std::istringstream invalid_final{"invalid"};
    std::ostringstream repaired;
    ModifyRWConfigStream(invalid_final, repaired, {{"foo", "new"}});
    BOOST_CHECK_EQUAL(repaired.str(), "foo=new\n[INVALID]\n# Error parsing line 1: invalid\n#invalid");
}

BOOST_AUTO_TEST_CASE(rw_config_precedence)
{
    ArgsManager args;
    SetupArgs(args, {{"-foo", ArgsManager::ALLOW_ANY}});
    args.SelectConfigNetwork("regtest");
    args.LockSettings([](common::Settings& settings) {
        settings.ro_config[""]["foo"] = {"default"};
        settings.ro_config["regtest"]["foo"] = {"network"};
        settings.rw_settings["foo"] = "json";
        settings.rw_config["foo"] = {"rw-first", "rw-second"};
    });
    BOOST_CHECK_EQUAL(args.GetArg("-foo", "unset"), "rw-first");
    ResetArgs(args, "-foo=cli-first -foo=cli-last");
    BOOST_CHECK_EQUAL(args.GetArg("-foo", "unset"), "cli-last");
    args.ForceSetArg("-foo", "forced");
    BOOST_CHECK_EQUAL(args.GetArg("-foo", "unset"), "forced");
    args.LockSettings([](common::Settings& settings) {
        settings.forced_settings.clear();
        settings.command_line_options.clear();
        settings.rw_config["foo"] = {false, "after-negation"};
    });
    BOOST_CHECK_EQUAL(args.GetArg("-foo", "unset"), "after-negation");
    args.LockSettings([](common::Settings& settings) { settings.rw_config.clear(); });
    BOOST_CHECK_EQUAL(args.GetArg("-foo", "unset"), "json");
    args.LockSettings([](common::Settings& settings) { settings.rw_settings.clear(); });
    BOOST_CHECK_EQUAL(args.GetArg("-foo", "unset"), "network");
}

BOOST_AUTO_TEST_CASE(rw_config_file_failures)
{
    class RWArgs : public ArgsManager {
    public:
        explicit RWArgs(const fs::path& path) { LOCK(cs_args); m_rwconf_path = path; }
    };
    const auto path{m_args.GetDataDirBase() / "rw_config_test.conf"};
    RWArgs args{path};
    SetupArgs(args, {{"-foo", ArgsManager::ALLOW_ANY}});
    args.ModifyRWConfigFile("foo", "original", /*also_settings_json=*/false);
    BOOST_CHECK_EQUAL(args.GetArg("-foo", "unset"), "original");
    BOOST_CHECK_THROW(args.ModifyRWConfigFile("foo", "bad\nvalue", false), std::invalid_argument);
    BOOST_CHECK_EQUAL(args.GetArg("-foo", "unset"), "original");
    std::ifstream input{path.std_path()};
    std::string line;
    std::getline(input, line);
    BOOST_CHECK_EQUAL(line, "foo=original");
    input.close();
    args.EraseRWConfigFile();
    BOOST_CHECK(!fs::exists(path));
    auto reset_path{path};
    reset_path += ".reset";
    BOOST_CHECK(fs::exists(reset_path));

    const auto directory{m_args.GetDataDirBase() / "rw_config_unreadable"};
    fs::create_directory(directory);
    RWArgs unreadable{directory};
    SetupArgs(unreadable, {{"-foo", ArgsManager::ALLOW_ANY}});
    unreadable.LockSettings([](common::Settings& settings) { settings.rw_config["foo"] = {"kept"}; });
    BOOST_CHECK_THROW(unreadable.ModifyRWConfigFile("foo", "new", false), std::ios_base::failure);
    BOOST_CHECK(fs::is_directory(directory));
    BOOST_CHECK_EQUAL(unreadable.GetArg("-foo", "unset"), "kept");
    auto temporary_path{directory};
    temporary_path += ".new";
    BOOST_CHECK(!fs::exists(temporary_path));

    RWArgs missing{path / "missing" / "config"};
    BOOST_CHECK_THROW(missing.ModifyRWConfigFile("foo", "new", false), std::ios_base::failure);
    BOOST_CHECK(!fs::exists(missing.GetRWConfigFilePath()));
}

BOOST_AUTO_TEST_SUITE_END()
