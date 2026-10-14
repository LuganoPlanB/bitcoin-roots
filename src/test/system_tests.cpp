// Copyright (c) 2019-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <common/run_command.h>
#include <test/util/common.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/string.h>

#ifdef ENABLE_EXTERNAL_SIGNER
#include <util/subprocess.h>
#endif // ENABLE_EXTERNAL_SIGNER

#include <boost/cstdlib.hpp>
#include <boost/test/unit_test.hpp>

#include <string>

#ifndef WIN32
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

BOOST_FIXTURE_TEST_SUITE(system_tests, BasicTestingSetup)

#ifdef ENABLE_EXTERNAL_SIGNER

static std::vector<std::string> mock_executable(std::string name)
{
    return {boost::unit_test::framework::master_test_suite().argv[0], "--log_level=nothing", "--report_level=no", "--run_test=mock_process/" + name};
}

BOOST_AUTO_TEST_CASE(run_command)
{
    {
        const UniValue result = RunCommandParseJSON({});
        BOOST_CHECK(result.isNull());
    }
    {
        const UniValue result = RunCommandParseJSON(mock_executable("valid_json"));
        BOOST_CHECK(result.isObject());
        const UniValue& success = result.find_value("success");
        BOOST_CHECK(!success.isNull());
        BOOST_CHECK_EQUAL(success.get_bool(), true);
    }
    {
        // An invalid command is handled by cpp-subprocess
#ifdef WIN32
        const std::string expected{"CreateProcess failed: "};
#else
        const std::string expected{"execve failed: "};
#endif
        BOOST_CHECK_EXCEPTION(RunCommandParseJSON({"invalid_command"}), subprocess::CalledProcessError, HasReason(expected));
    }
    {
        // Return non-zero exit code, no output to stderr
        const std::vector<std::string> command = mock_executable("nonzeroexit_nooutput");
        BOOST_CHECK_EXCEPTION(RunCommandParseJSON(command), std::runtime_error, [&](const std::runtime_error& e) {
            const std::string what{e.what()};
            BOOST_CHECK(what.find(strprintf("RunCommandParseJSON error: process(%s) returned %d: \n", util::Join(command, " "), boost::exit_test_failure)) != std::string::npos);
            return true;
        });
    }
    {
        // Return non-zero exit code, with error message for stderr
        const std::vector<std::string> command = mock_executable("nonzeroexit_stderroutput");
        const std::string expected{"err"};
        BOOST_CHECK_EXCEPTION(RunCommandParseJSON(command), std::runtime_error, [&](const std::runtime_error& e) {
            const std::string what(e.what());
            BOOST_CHECK(what.find(strprintf("RunCommandParseJSON error: process(%s) returned %s: %s", util::Join(command, " "), boost::exit_test_failure, "err")) != std::string::npos);
            BOOST_CHECK(what.find(expected) != std::string::npos);
            return true;
        });
    }
    {
        // Unable to parse JSON
        BOOST_CHECK_EXCEPTION(RunCommandParseJSON(mock_executable("invalid_json")), std::runtime_error, HasReason("Unable to parse JSON: {"));
    }
    {
        // Test stdin
        const UniValue result = RunCommandParseJSON(mock_executable("pass_stdin_to_stdout"), "{\"success\": true}");
        BOOST_CHECK(result.isObject());
        const UniValue& success = result.find_value("success");
        BOOST_CHECK(!success.isNull());
        BOOST_CHECK_EQUAL(success.get_bool(), true);
    }
}
#ifndef WIN32
BOOST_AUTO_TEST_CASE(signer_fallback_closes_descriptors_above_lowered_limit)
{
    const int fd = open("/dev/null", O_RDONLY);
    BOOST_REQUIRE(fd >= 0);
    const int inherited = fcntl(fd, F_DUPFD, 123);
    close(fd);
    BOOST_REQUIRE(inherited >= 123);
    int pipe_fds[2];
    BOOST_REQUIRE_EQUAL(pipe(pipe_fds), 0);
    const pid_t child = fork();
    BOOST_REQUIRE(child >= 0);
    if (child == 0) {
        close(pipe_fds[0]);
        struct rlimit limit;
        if (getrlimit(RLIMIT_NOFILE, &limit) != 0) _exit(1);
        limit.rlim_cur = 32;
        if (setrlimit(RLIMIT_NOFILE, &limit) != 0) _exit(2);
        int error_fd{pipe_fds[1]};
        const bool closed = subprocess::detail::subprocess_close_all_fds(error_fd, /*force_fallback=*/true);
        const unsigned char facts = (closed ? 1 : 0) |
            (fcntl(inherited, F_GETFD) == -1 && errno == EBADF ? 2 : 0) |
            (error_fd == 3 && fcntl(error_fd, F_GETFD) >= 0 ? 4 : 0) |
            (fcntl(0, F_GETFD) >= 0 && fcntl(1, F_GETFD) >= 0 && fcntl(2, F_GETFD) >= 0 ? 8 : 0) |
            ((fcntl(error_fd, F_GETFD) & FD_CLOEXEC) != 0 ? 16 : 0);
        const bool sent = write(error_fd, &facts, sizeof(facts)) == sizeof(facts);
        _exit(sent ? 0 : 3);
    }
    close(inherited);
    close(pipe_fds[1]);
    unsigned char facts{0};
    BOOST_CHECK_EQUAL(read(pipe_fds[0], &facts, sizeof(facts)), sizeof(facts));
    close(pipe_fds[0]);
    int status{0};
    BOOST_REQUIRE_EQUAL(waitpid(child, &status, 0), child);
    BOOST_CHECK(WIFEXITED(status));
    BOOST_CHECK_EQUAL(WEXITSTATUS(status), 0);
#if defined(__linux__) || HAVE_CLOSEFROM
    BOOST_CHECK_EQUAL(facts, 31);
#else
    BOOST_CHECK_EQUAL(facts & 1, 0); // Unsupported platforms fail closed.
#endif
}

BOOST_AUTO_TEST_CASE(signer_closes_inherited_descriptors)
{
    const int fd = open("/dev/null", O_RDONLY);
    BOOST_REQUIRE(fd >= 0);
    // A high descriptor avoids collision with the child's startup files.
    const int inherited = fcntl(fd, F_DUPFD, 123);
    close(fd);
    BOOST_REQUIRE(inherited >= 123);
    const auto result = RunCommandParseJSON(mock_executable("check_inherited_fd"), std::to_string(inherited));
    close(inherited);
    BOOST_CHECK(!result.find_value("inherited").get_bool());
}
#endif
#endif // ENABLE_EXTERNAL_SIGNER

BOOST_AUTO_TEST_SUITE_END()
