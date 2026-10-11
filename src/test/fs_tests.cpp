// Copyright (c) 2011-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
#include <test/util/setup_common.h>
#include <util/fs.h>
#include <util/fs_helpers.h>

#include <boost/test/unit_test.hpp>

#include <cstdio>
#include <fstream>
#include <ios>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#ifndef WIN32
#include <sys/resource.h>
#include <unistd.h>
#endif

BOOST_FIXTURE_TEST_SUITE(fs_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(directory_write_probe)
{
    const fs::path dir{m_args.GetDataDirBase() / "write_probe"};
    fs::create_directory(dir);
    BOOST_CHECK(IsDirWritable(dir));
    BOOST_CHECK(fs::is_empty(dir));
    const fs::path file{dir / "file"};
    { std::ofstream stream{file.std_path()}; }
    BOOST_CHECK_THROW(IsDirWritable(file), std::runtime_error);
#ifndef WIN32
    fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec);
    if (geteuid() != 0) BOOST_CHECK(!IsDirWritable(dir));
    fs::permissions(dir, fs::perms::owner_all);
#endif
    fs::remove_all(dir);
}

BOOST_AUTO_TEST_CASE(fsbridge_pathtostring)
{
    std::string u8_str = "fs_tests_₿_🏃";
    std::u8string str8{u8"fs_tests_₿_🏃"};
    BOOST_CHECK_EQUAL(fs::PathToString(fs::PathFromString(u8_str)), u8_str);
    BOOST_CHECK_EQUAL(fs::u8path(u8_str).utf8string(), u8_str);
    BOOST_CHECK_EQUAL(fs::path(str8).utf8string(), u8_str);
    BOOST_CHECK(fs::path(str8).u8string() == str8);
    BOOST_CHECK_EQUAL(fs::PathFromString(u8_str).utf8string(), u8_str);
    BOOST_CHECK_EQUAL(fs::PathToString(fs::u8path(u8_str)), u8_str);
#ifndef WIN32
    // On non-windows systems, verify that arbitrary byte strings containing
    // invalid UTF-8 can be round tripped successfully with PathToString and
    // PathFromString. On non-windows systems, paths are just byte strings so
    // these functions do not do any encoding. On windows, paths are Unicode,
    // and these functions do encoding and decoding, so the behavior of this
    // test would be undefined.
    std::string invalid_u8_str = "\xf0";
    BOOST_CHECK_EQUAL(invalid_u8_str.size(), 1);
    BOOST_CHECK_EQUAL(fs::PathToString(fs::PathFromString(invalid_u8_str)), invalid_u8_str);
#endif
}

BOOST_AUTO_TEST_CASE(fsbridge_stem)
{
    std::string test_filename = "fs_tests_₿_🏃.dat";
    std::string expected_stem = "fs_tests_₿_🏃";
    BOOST_CHECK_EQUAL(fs::PathToString(fs::PathFromString(test_filename).stem()), expected_stem);
}

BOOST_AUTO_TEST_CASE(fsbridge_fstream)
{
    fs::path tmpfolder = m_args.GetDataDirBase();
    // tmpfile1 should be the same as tmpfile2
    fs::path tmpfile1 = tmpfolder / fs::u8path("fs_tests_₿_🏃");
    fs::path tmpfile2 = tmpfolder / fs::path(u8"fs_tests_₿_🏃");
    {
        std::ofstream file{tmpfile1.std_path()};
        file << "bitcoin";
    }
    {
        std::ifstream file{tmpfile2.std_path()};
        std::string input_buffer;
        file >> input_buffer;
        BOOST_CHECK_EQUAL(input_buffer, "bitcoin");
    }
    {
        std::ifstream file{tmpfile1.std_path(), std::ios_base::in | std::ios_base::ate};
        std::string input_buffer;
        file >> input_buffer;
        BOOST_CHECK_EQUAL(input_buffer, "");
    }
    {
        std::ofstream file{tmpfile2.std_path(), std::ios_base::out | std::ios_base::app};
        file << "tests";
    }
    {
        std::ifstream file{tmpfile1.std_path()};
        std::string input_buffer;
        file >> input_buffer;
        BOOST_CHECK_EQUAL(input_buffer, "bitcointests");
    }
    {
        std::ofstream file{tmpfile2.std_path(), std::ios_base::out | std::ios_base::trunc};
        file << "bitcoin";
    }
    {
        std::ifstream file{tmpfile1.std_path()};
        std::string input_buffer;
        file >> input_buffer;
        BOOST_CHECK_EQUAL(input_buffer, "bitcoin");
    }
    {
        // Join an absolute path and a relative path.
        fs::path p = fsbridge::AbsPathJoin(tmpfolder, fs::u8path("fs_tests_₿_🏃"));
        BOOST_CHECK(p.is_absolute());
        BOOST_CHECK_EQUAL(tmpfile1, p);
    }
    {
        // Join two absolute paths.
        fs::path p = fsbridge::AbsPathJoin(tmpfile1, tmpfile2);
        BOOST_CHECK(p.is_absolute());
        BOOST_CHECK_EQUAL(tmpfile2, p);
    }
    {
        // Ensure joining with empty paths does not add trailing path components.
        BOOST_CHECK_EQUAL(tmpfile1, fsbridge::AbsPathJoin(tmpfile1, ""));
        BOOST_CHECK_EQUAL(tmpfile1, fsbridge::AbsPathJoin(tmpfile1, {}));
    }
}

BOOST_AUTO_TEST_CASE(rename)
{
    const fs::path tmpfolder{m_args.GetDataDirBase()};

    const fs::path path1{tmpfolder / "a"};
    const fs::path path2{tmpfolder / "b"};

    const std::string path1_contents{"1111"};
    const std::string path2_contents{"2222"};

    {
        std::ofstream file{path1.std_path()};
        file << path1_contents;
    }

    {
        std::ofstream file{path2.std_path()};
        file << path2_contents;
    }

    // Rename path1 -> path2.
    BOOST_CHECK(RenameOver(path1, path2));

    BOOST_CHECK(!fs::exists(path1));

    {
        std::ifstream file{path2.std_path()};
        std::string contents;
        file >> contents;
        BOOST_CHECK_EQUAL(contents, path1_contents);
    }
    fs::remove(path2);
}

#ifndef WIN32
BOOST_AUTO_TEST_CASE(file_descriptor_limit_bounds)
{
    struct rlimit limit{};
    BOOST_REQUIRE_EQUAL(getrlimit(RLIMIT_NOFILE, &limit), 0);
    const int expected{limit.rlim_cur == RLIM_INFINITY || std::cmp_greater_equal(limit.rlim_cur, std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max() : static_cast<int>(limit.rlim_cur)};
    // A zero request must retain the current soft limit, saturating the int result.
    BOOST_CHECK_EQUAL(RaiseFileDescriptorLimit(0), expected);
    struct rlimit after{};
    BOOST_REQUIRE_EQUAL(getrlimit(RLIMIT_NOFILE, &after), 0);
    BOOST_CHECK_EQUAL(after.rlim_cur, limit.rlim_cur);
    BOOST_CHECK_EQUAL(after.rlim_max, limit.rlim_max);
}
#endif

BOOST_AUTO_TEST_CASE(allocate_file_range_preserves_data)
{
    const auto path{m_args.GetDataDirBase() / "allocate-range.dat"};
    const auto close_file{[](FILE* file) { std::fclose(file); }};
    std::unique_ptr<FILE, decltype(close_file)> file{fsbridge::fopen(path, "w+b"), close_file};
    BOOST_REQUIRE(file);
    std::vector<unsigned char> contents(3 * 65536);
    for (size_t i{0}; i < contents.size(); ++i) contents[i] = static_cast<unsigned char>(i / 65536 + 1);
    BOOST_REQUIRE_EQUAL(std::fwrite(contents.data(), 1, contents.size(), file.get()), contents.size());
    BOOST_REQUIRE_EQUAL(std::fflush(file.get()), 0);

    AllocateFileRange(file.get(), 65536, static_cast<unsigned int>(contents.size() - 65536 + 256));
    BOOST_REQUIRE_EQUAL(std::fseek(file.get(), 0, SEEK_SET), 0);
    std::vector<unsigned char> actual(contents.size());
    BOOST_REQUIRE_EQUAL(std::fread(actual.data(), 1, actual.size(), file.get()), actual.size());
    BOOST_CHECK(actual == contents);

    // Allocation entirely inside a live file must never shorten it.
    AllocateFileRange(file.get(), 65536, 128);
    BOOST_REQUIRE_EQUAL(std::fseek(file.get(), 0, SEEK_END), 0);
    BOOST_CHECK_EQUAL(std::ftell(file.get()), contents.size() + 256);
}

BOOST_AUTO_TEST_SUITE_END()
