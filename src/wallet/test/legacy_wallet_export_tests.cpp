// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/legacy_export.h>
#include <wallet/migrate.h>

#include <crypto/common.h>
#include <hash.h>
#include <test/util/setup_common.h>
#include <util/readwritefile.h>
#include <util/strencodings.h>
#include <test/wallet/data/legacy_export.json.h>

#include <univalue.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace wallet {
namespace {
// Same synthetic BDB envelope as test_framework/legacy_export.py::bdb_bytes.
// This builds only a four-page SUBDB/main tree, without a BDB library/engine.
std::string LegacyFile(bool empty = false)
{
    constexpr uint32_t PAGE_SIZE{4096};
    std::string bytes(4 * PAGE_SIZE, '\0');
    auto put32 = [&](size_t offset, uint32_t value) {
        WriteLE32(reinterpret_cast<unsigned char*>(bytes.data() + offset), value);
    };
    auto put16 = [&](size_t offset, uint16_t value) {
        WriteLE16(reinterpret_cast<unsigned char*>(bytes.data() + offset), value);
    };
    for (uint32_t page{0}; page < 4; ++page) {
        put32(page * PAGE_SIZE + 4, 1); // clean LSN
        put32(page * PAGE_SIZE + 8, page);
    }
    for (uint32_t page : {0U, 2U}) {
        const size_t start{page * PAGE_SIZE};
        put32(start + 12, 0x053162);
        put32(start + 16, 9);
        put32(start + 20, PAGE_SIZE);
        bytes[start + 25] = 9;
        put32(start + 32, 3);
        put32(start + 48, 0x20);
        put32(start + 88, page == 0 ? 1 : 3);
    }
    auto leaf = [&](uint32_t page, std::string_view key, std::string_view value) {
        const size_t start{page * PAGE_SIZE};
        bytes[start + 24] = 1;
        bytes[start + 25] = 5;
        if (key.empty()) return;
        put16(start + 20, 2);
        const size_t key_offset{PAGE_SIZE - 3 - key.size()};
        const size_t value_offset{key_offset - 3 - value.size()};
        put16(start + 26, key_offset);
        put16(start + 28, value_offset);
        put16(start + 22, value_offset);
        put16(start + key_offset, key.size());
        bytes[start + key_offset + 2] = 1;
        bytes.replace(start + key_offset + 3, key.size(), key);
        put16(start + value_offset, value.size());
        bytes[start + value_offset + 2] = 1;
        bytes.replace(start + value_offset + 3, value.size(), value);
    };
    leaf(1, "main", std::string_view{"\0\0\0\2", 4});
    leaf(3, empty ? "" : "name", "example");
    return bytes;
}

/** Test-only envelope builder, following the independent Python bdb_bytes
 * fixture layout. No BDB library, database engine or production writer.
 */
struct StorageFixture {
    uint32_t page_size;
    bool big;
    std::string bytes;

    explicit StorageFixture(uint32_t size = 4096, bool big_endian = false)
        : page_size(size), big(big_endian), bytes(std::max<uint32_t>(4 * size, 4096), '\0')
    {
        for (uint32_t page{0}; page < bytes.size() / page_size; ++page) Header(page, 0, 0, 0, 0);
        for (const uint32_t page : {0U, 2U}) {
            const size_t start{page * page_size};
            Put32(start + 12, 0x053162);
            Put32(start + 16, 9);
            Put32(start + 20, page_size);
            bytes[start + 25] = 9;
            Put32(start + 32, bytes.size() / page_size - 1);
            Put32(start + 48, 0x20);
            Put32(start + 88, page == 0 ? 1 : 3);
        }
        Items(1, {Inline("main"), Inline(std::string_view{"\0\0\0\2", 4})});
        Items(3, {Inline("name"), Inline("example")});
    }

    void Put16(size_t at, uint16_t value)
    {
        auto* output{reinterpret_cast<unsigned char*>(bytes.data() + at)};
        if (big) WriteBE16(output, value); else WriteLE16(output, value);
    }
    void Put32(size_t at, uint32_t value)
    {
        auto* output{reinterpret_cast<unsigned char*>(bytes.data() + at)};
        if (big) WriteBE32(output, value); else WriteLE32(output, value);
    }
    void Header(uint32_t page, uint8_t type, uint16_t entries, uint16_t free, uint8_t level = 1)
    {
        const size_t start{page * page_size};
        Put32(start, 0);
        Put32(start + 4, 1);
        Put32(start + 8, page);
        Put32(start + 12, 0);
        Put32(start + 16, 0);
        Put16(start + 20, entries);
        Put16(start + 22, free);
        bytes[start + 24] = level;
        bytes[start + 25] = type;
    }
    std::string Inline(std::string_view value, bool deleted = false) const
    {
        std::string item(3, '\0');
        auto* output{reinterpret_cast<unsigned char*>(item.data())};
        if (big) WriteBE16(output, value.size()); else WriteLE16(output, value.size());
        item[2] = deleted ? '\x81' : '\x01';
        item.append(value);
        return item;
    }
    std::string Overflow(uint32_t page, uint32_t length, bool deleted = false) const
    {
        std::string item(12, '\0');
        item[2] = deleted ? '\x83' : '\x03';
        auto* output{reinterpret_cast<unsigned char*>(item.data())};
        if (big) { WriteBE32(output + 4, page); WriteBE32(output + 8, length); }
        else { WriteLE32(output + 4, page); WriteLE32(output + 8, length); }
        return item;
    }
    std::string Internal(uint32_t page) const
    {
        auto item{Overflow(page, 0)};
        item[2] = 1;
        return item;
    }
    void Items(uint32_t page, const std::vector<std::string>& items, uint8_t type = 5, uint8_t level = 1, size_t gap = 0)
    {
        const size_t start{page * page_size};
        bytes.replace(start, page_size, page_size, '\0');
        Header(page, type, items.size(), 0, level);
        size_t offset{page_size};
        for (size_t i{0}; i < items.size(); ++i) {
            BOOST_REQUIRE_LE(items[i].size() + gap, offset);
            offset -= items[i].size() + gap;
            BOOST_REQUIRE_GE(offset, 26 + 2 * items.size());
            Put16(start + 26 + 2 * i, offset);
            bytes.replace(start + offset, items[i].size(), items[i]);
        }
        Put16(start + 22, offset);
    }
    void EnsurePages(uint32_t count)
    {
        const uint32_t old{static_cast<uint32_t>(bytes.size() / page_size)};
        if (count <= old) return;
        bytes.resize(count * page_size, '\0');
        for (uint32_t page{old}; page < count; ++page) Header(page, 0, 0, 0, 0);
        Put32(32, count - 1);
        Put32(2 * page_size + 32, count - 1);
    }
    void OverflowPage(uint32_t page, std::string_view value, uint32_t next = 0)
    {
        EnsurePages(page + 1);
        BOOST_REQUIRE_LE(value.size(), page_size - 26);
        Header(page, 7, 1, value.size(), 0);
        Put32(page * page_size + 16, next);
        bytes.replace(page * page_size + 26, value.size(), value);
    }
};

std::set<fs::path> Entries(const fs::path& path)
{
    std::set<fs::path> result;
    for (const auto& entry : fs::directory_iterator(path)) result.insert(entry.path());
    return result;
}

class RestorePermissions
{
    fs::path m_path;
    fs::perms m_permissions;

public:
    explicit RestorePermissions(const fs::path& path) : m_path(path), m_permissions(fs::status(path).permissions()) {}
    ~RestorePermissions() { fs::permissions(m_path, m_permissions); }
};

template <typename... T>
SerializeData Encode(const T&... values)
{
    DataStream stream;
    SerializeMany(stream, values...);
    return {stream.begin(), stream.end()};
}

SerializeData HexData(const std::string& hex)
{
    const auto bytes{ParseHex(hex)};
    const auto span{std::as_bytes(std::span{bytes})};
    return {span.begin(), span.end()};
}

CPubKey PublicKey(const std::string& hex)
{
    return CPubKey{ParseHex(hex)};
}

std::vector<LegacyWalletRecord> FixtureRecords(const UniValue& fixture)
{
    std::vector<LegacyWalletRecord> records;
    for (const auto& record : fixture["records"].getValues()) {
        records.push_back({HexData(record[0].get_str()), HexData(record[1].get_str())});
    }
    return records;
}

UniValue Fixtures()
{
    UniValue fixtures;
    BOOST_REQUIRE(fixtures.read(json_tests::legacy_export));
    return fixtures;
}

void RejectRecords(std::vector<LegacyWalletRecord> records)
{
    BOOST_CHECK_THROW(DecodeLegacyWalletRecords(records), std::runtime_error);
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(legacy_wallet_export_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(source_paths_and_preservation)
{
    const fs::path directory{m_path_root / "offline"};
    fs::create_directory(directory);
    const fs::path path{directory / "wallet.dat"};
    const auto original{LegacyFile()};
    BOOST_REQUIRE(WriteBinaryFile(path, original));
    const auto entries{Entries(directory)};
    const auto mtime{fs::last_write_time(path)};
    for (const auto& source : {path, directory}) {
        LegacyWalletSource snapshot(source);
        BOOST_REQUIRE_EQUAL(snapshot.Records().size(), 1U);
        const auto& record{snapshot.Records().front()};
        BOOST_CHECK_EQUAL((std::string_view{reinterpret_cast<const char*>(record.key.data()), record.key.size()}), "name");
        BOOST_CHECK_EQUAL((std::string_view{reinterpret_cast<const char*>(record.value.data()), record.value.size()}), "example");
        snapshot.VerifyUnchanged();
    }
    BOOST_CHECK(ReadBinaryFile(path).second == original);
    BOOST_CHECK(fs::last_write_time(path) == mtime);
    BOOST_CHECK(Entries(directory) == entries);

    const fs::path explicit_path{directory / "backup.dat"};
    fs::rename(path, explicit_path);
    BOOST_CHECK_NO_THROW(LegacyWalletSource{explicit_path});
}

BOOST_AUTO_TEST_CASE(source_without_write_permissions)
{
    const fs::path directory{m_path_root / "read-only"};
    fs::create_directory(directory);
    const fs::path path{directory / "wallet.dat"};
    const auto original{LegacyFile()};
    BOOST_REQUIRE(WriteBinaryFile(path, original));
    const fs::path dirty{directory / "dirty.dat"};
    auto dirty_bytes{original};
    dirty_bytes[0] = 1;
    BOOST_REQUIRE(WriteBinaryFile(dirty, dirty_bytes));
    const auto entries{Entries(directory)};
    RestorePermissions restore_file(path), restore_dirty(dirty), restore_directory(directory);
    fs::permissions(path, fs::perms::owner_read);
    fs::permissions(dirty, fs::perms::owner_read);
    fs::permissions(directory, fs::perms::owner_read | fs::perms::owner_exec);
#ifndef WIN32
    BOOST_REQUIRE_NE(access(path.c_str(), W_OK), 0);
    BOOST_REQUIRE_NE(access(directory.c_str(), W_OK), 0);
#endif
    BOOST_CHECK_NO_THROW(LegacyWalletSource{directory});
    BOOST_CHECK_THROW(LegacyWalletSource{dirty}, std::runtime_error);
    BOOST_CHECK(ReadBinaryFile(path).second == original);
    BOOST_CHECK(ReadBinaryFile(dirty).second == dirty_bytes);
    BOOST_CHECK(Entries(directory) == entries);
}

BOOST_AUTO_TEST_CASE(source_rejects_formats_and_dirty_logs)
{
    const fs::path path{m_path_root / "wallet.dat"};
    auto reject = [&](const std::string& bytes) {
        BOOST_REQUIRE(WriteBinaryFile(path, bytes));
        const auto entries{Entries(m_path_root)};
        const auto mtime{fs::last_write_time(path)};
        BOOST_CHECK_THROW(LegacyWalletSource{path}, std::runtime_error);
        BOOST_CHECK(ReadBinaryFile(path).second == bytes);
        BOOST_CHECK(fs::last_write_time(path) == mtime);
        BOOST_CHECK(Entries(m_path_root) == entries);
    };
    std::string sqlite(4096, '\0');
    sqlite.replace(0, 16, "SQLite format 3\0", 16);
    reject(sqlite);
    reject(std::string(4096, '\0'));
    reject("short");
    for (const size_t offset : {size_t{16}, size_t{4096 + 4092}, size_t{24}, size_t{8192 + 24}}) {
        auto bytes{LegacyFile()};
        bytes[offset] ^= 1; // version, subdatabase name, built-in encryption
        reject(bytes);
    }
    for (size_t page{0}; page < 4; ++page) {
        auto bytes{LegacyFile()};
        bytes[page * 4096] = 1;
        reject(bytes);
    }
    BOOST_REQUIRE(WriteBinaryFile(path, LegacyFile(true)));
    LegacyWalletSource empty(path);
    BOOST_CHECK(empty.Records().empty());
}

BOOST_AUTO_TEST_CASE(source_rejects_missing_and_special_files)
{
    BOOST_CHECK_THROW(LegacyWalletSource{m_path_root / "missing"}, std::runtime_error);
    BOOST_CHECK_THROW(LegacyWalletSource{m_path_root}, std::runtime_error);
#ifndef WIN32
    const fs::path fifo{m_path_root / "pipe"};
    BOOST_REQUIRE_EQUAL(mkfifo(fifo.c_str(), 0600), 0);
    BOOST_CHECK_THROW(LegacyWalletSource{fifo}, std::runtime_error);
#endif
}

BOOST_AUTO_TEST_CASE(source_detects_changes_before_publication)
{
    const fs::path path{m_path_root / "wallet.dat"};
    const auto original{LegacyFile()};
    for (const auto mutation : {0, 1, 2, 3, 4}) {
        BOOST_REQUIRE(WriteBinaryFile(path, original));
        LegacyWalletSource source(path);
        const auto mtime{fs::last_write_time(path)};
        switch (mutation) {
        case 0: // Different contents, same size and restored mtime.
        {
            auto changed{original};
            changed.back() ^= 1;
            BOOST_REQUIRE(WriteBinaryFile(path, changed));
            fs::last_write_time(path, mtime);
            break;
        }
        case 1: fs::resize_file(path, original.size() + 1); break;
        case 2: fs::last_write_time(path, mtime + std::chrono::seconds{1}); break;
        case 3: // Identical bytes and mtime, distinct source identity.
            fs::remove(path);
            BOOST_REQUIRE(WriteBinaryFile(path, original));
            fs::last_write_time(path, mtime);
            break;
        case 4: fs::remove(path); break;
        }
        BOOST_CHECK_THROW(source.VerifyUnchanged(), std::runtime_error);
    }
}

BOOST_AUTO_TEST_CASE(source_rejects_final_symlinks)
{
    const fs::path directory{m_path_root / "source"};
    fs::create_directory(directory);
    const fs::path path{directory / "wallet.dat"};
    const fs::path original{directory / "original.dat"};
    const fs::path directory_link{m_path_root / "source-link"};
    BOOST_REQUIRE(WriteBinaryFile(path, LegacyFile()));
    BOOST_CHECK_NO_THROW(LegacyWalletSource{path});
    BOOST_CHECK_NO_THROW(LegacyWalletSource{directory});

    // Parent/directory symlinks remain allowed: only the final file component
    // must be a regular non-symlink. Preserve the ordinary path conventions.
    fs::create_directory_symlink(directory, directory_link);
    BOOST_CHECK_NO_THROW(LegacyWalletSource{directory_link});
    BOOST_CHECK_NO_THROW(LegacyWalletSource{directory_link / "wallet.dat"});

    LegacyWalletSource source(path);
    fs::rename(path, original);
    fs::create_symlink(original, path);
    const auto entries{Entries(directory)};
    const auto bytes{ReadBinaryFile(original).second};
    const auto mtime{fs::last_write_time(original)};
    // Same inode, bytes, size and mtime: the final-component type changed.
    BOOST_CHECK_THROW(source.VerifyUnchanged(), std::runtime_error);
    BOOST_CHECK_THROW(LegacyWalletSource{path}, std::runtime_error);
    BOOST_CHECK_THROW(LegacyWalletSource{directory}, std::runtime_error);
    BOOST_CHECK_THROW(LegacyWalletSource{directory_link}, std::runtime_error);
    BOOST_CHECK(Entries(directory) == entries);
    BOOST_CHECK(ReadBinaryFile(original).second == bytes);
    BOOST_CHECK(fs::last_write_time(original) == mtime);
    BOOST_CHECK_NO_THROW(LegacyWalletSource{original});
}

BOOST_AUTO_TEST_CASE(record_fixture_inventories)
{
    // The fixed corpus comes from the independent Python fixture generator;
    // its source hash and donor schema provenance are retained in the JSON.
    const auto fixtures{Fixtures()};
    BOOST_REQUIRE_EQUAL(fixtures["fixtures"].size(), 10U);
    size_t total_records{0};
    for (const auto& fixture : fixtures["fixtures"].getValues()) {
        BOOST_TEST_CONTEXT(fixture["name"].get_str()) {
            const auto records{FixtureRecords(fixture)};
            const auto inventory{DecodeLegacyWalletRecords(records)};
            total_records += records.size();
            BOOST_CHECK_EQUAL(inventory.records.size(), records.size());
            BOOST_CHECK_EQUAL(inventory.keys.size(), fixture["keys"].size());
            BOOST_CHECK_EQUAL(inventory.scripts.size(), fixture["scripts"].size());
            BOOST_CHECK(inventory.omitted.empty());
            const bool encrypted{fixture["encrypted"].get_bool()};
            for (const auto& expected : fixture["keys"].getValues()) {
                const auto pubkey{PublicKey(expected["pubkey"].get_str())};
                const auto entry{inventory.keys.find(pubkey)};
                BOOST_REQUIRE(entry != inventory.keys.end());
                BOOST_CHECK_EQUAL(entry->second.private_key.has_value(), !encrypted);
                BOOST_CHECK_EQUAL(entry->second.ciphertext.has_value(), encrypted);
                if (!encrypted) {
                    BOOST_CHECK_EQUAL(HexStr(*entry->second.private_key), expected["scalar"].get_str());
                    BOOST_CHECK(entry->second.private_key->GetPubKey() == pubkey);
                    BOOST_CHECK_EQUAL(entry->second.private_key->IsCompressed(), pubkey.IsCompressed());
                }
            }
            for (const auto& expected : fixture["scripts"].getValues()) {
                const auto bytes{ParseHex(expected.get_str())};
                const CScript script{bytes.begin(), bytes.end()};
                BOOST_CHECK(inventory.scripts.at(CScriptID(script)).script == script);
            }
            for (size_t index{0}; index < records.size(); ++index) {
                const auto& record{records[index]};
                SpanReader key{record.key};
                std::string family;
                key >> family;
                BOOST_CHECK_EQUAL(inventory.records[index].family, family);
                BOOST_CHECK_EQUAL(inventory.records[index].key_bytes, record.key.size());
                BOOST_CHECK_EQUAL(inventory.records[index].value_bytes, record.value.size());
                BOOST_CHECK(!inventory.records[index].omitted);
                if (family == "keymeta") {
                    CPubKey pubkey;
                    key >> pubkey;
                    BOOST_CHECK(Encode(*inventory.keys.at(pubkey).metadata) == record.value);
                    BOOST_CHECK(inventory.keys.at(pubkey).source_families.contains(family));
                } else if (family == "key" || family == "ckey") {
                    CPubKey pubkey;
                    key >> pubkey;
                    const auto& entry{inventory.keys.at(pubkey)};
                    BOOST_CHECK(entry.source_families.contains(family));
                    if (family == "ckey") {
                        SpanReader value{record.value};
                        std::vector<unsigned char> ciphertext;
                        value >> ciphertext;
                        BOOST_CHECK(entry.ciphertext == ciphertext);
                    }
                } else if (family == "hdchain") {
                    BOOST_CHECK(Encode(*inventory.hdchain) == record.value);
                } else if (family == "mkey") {
                    uint32_t id;
                    key >> id;
                    BOOST_CHECK(Encode(inventory.master_keys.at(id)) == record.value);
                } else if (family == "pool") {
                    int64_t index;
                    key >> index;
                    const auto& pool{inventory.pool.at(index)};
                    DataStream encoded;
                    encoded << pool.stream_version << pool.time << pool.pubkey;
                    if (pool.internal_present) encoded << pool.internal;
                    if (pool.pre_split_present) encoded << pool.pre_split;
                    BOOST_CHECK((SerializeData{encoded.begin(), encoded.end()}) == record.value);
                    BOOST_CHECK(inventory.keys.at(pool.pubkey).source_families.contains("pool"));
                } else if (family == "wkey") {
                    CPubKey pubkey;
                    key >> pubkey;
                    const auto& entry{inventory.keys.at(pubkey)};
                    const auto& old{*entry.wkey};
                    BOOST_CHECK(Encode(old.stream_version, entry.private_key->GetPrivKey(), old.created, old.expires, old.comment) == record.value);
                    BOOST_CHECK(entry.source_families.contains(family));
                } else if (family == "cscript") {
                    uint160 id;
                    key >> id;
                    BOOST_CHECK(inventory.scripts.at(CScriptID(id)).redeem);
                } else if (family == "watchmeta") {
                    CScript script;
                    key >> script;
                    BOOST_CHECK(Encode(*inventory.scripts.at(CScriptID(script)).metadata) == record.value);
                } else if (family == "watchs") {
                    CScript script;
                    key >> script;
                    BOOST_CHECK(inventory.scripts.at(CScriptID(script)).watch_enabled == true);
                } else if (family == "name" || family == "purpose") {
                    std::string address, text;
                    key >> address;
                    SpanReader value{record.value};
                    value >> text;
                    const auto& entry{inventory.addresses.at(address)};
                    BOOST_CHECK((family == "name" ? entry.label : entry.purpose) == text);
                }
            }
        }
    }
    BOOST_CHECK_EQUAL(total_records, 51U);
}

BOOST_AUTO_TEST_CASE(record_truncation_and_trailing_bytes)
{
    const auto fixtures{Fixtures()};
    size_t checked{0};
    for (const auto& fixture : fixtures["fixtures"].getValues()) {
        for (const auto& record : FixtureRecords(fixture)) {
            SpanReader key{record.key};
            std::string family;
            key >> family;
            auto extra_key{record};
            extra_key.key.push_back(std::byte{0});
            RejectRecords({extra_key});
            auto extra_value{record};
            extra_value.value.insert(extra_value.value.end(), 3, std::byte{0});
            RejectRecords({extra_value});
            for (size_t length{0}; length < record.value.size(); ++length) {
                auto shortened{record};
                shortened.value.resize(length);
                // A complete absent optional checksum/pool suffix is supported.
                if ((family == "key" || family == "ckey" || family == "wkey") &&
                    length + 32 == record.value.size()) {
                    SpanReader check{shortened.value};
                    if (family != "wkey") {
                        std::vector<unsigned char> material;
                        try { check >> material; } catch (const std::ios_base::failure&) {}
                        if (check.empty() && !material.empty()) continue;
                    }
                }
                if (family == "pool" && length + 2 >= record.value.size()) {
                    SpanReader check{shortened.value};
                    int32_t version;
                    int64_t time;
                    CPubKey pubkey;
                    try {
                        check >> version >> time >> pubkey;
                        if (pubkey.IsFullyValid()) continue;
                    } catch (const std::ios_base::failure&) {}
                }
                RejectRecords({shortened});
                ++checked;
            }
        }
    }
    BOOST_CHECK_EQUAL(checked, 4836U);
}

BOOST_AUTO_TEST_CASE(record_checksums_and_semantic_conflicts)
{
    const auto fixtures{Fixtures()};
    const auto plain{FixtureRecords(fixtures["fixtures"][1])};
    const auto pubkey{PublicKey(fixtures["fixtures"][1]["keys"][0]["pubkey"].get_str())};
    const auto other{PublicKey(fixtures["fixtures"][1]["keys"][1]["pubkey"].get_str())};
    auto key{plain[0]};
    auto wrong_hash{key};
    wrong_hash.value.back() ^= std::byte{1};
    RejectRecords({wrong_hash});
    SpanReader value{key.value};
    CPrivKey priv;
    value >> priv;
    // A valid checksum must never suppress private/public correspondence.
    RejectRecords({{Encode(std::string{"key"}, other), Encode(priv, Hash(other, priv))}});
    auto no_checksum{key};
    no_checksum.value.resize(no_checksum.value.size() - 32);
    BOOST_CHECK_NO_THROW(DecodeLegacyWalletRecords(std::vector{no_checksum}));
    const auto old{FixtureRecords(fixtures["fixtures"][0])[1]};
    auto checksummed_old{old};
    const auto old_checksum{Encode(Hash(pubkey, priv))};
    checksummed_old.value.insert(checksummed_old.value.end(), old_checksum.begin(), old_checksum.end());
    BOOST_CHECK_NO_THROW(DecodeLegacyWalletRecords(std::vector{checksummed_old}));
    const auto merged{DecodeLegacyWalletRecords(std::vector{key, old})};
    BOOST_CHECK((merged.keys.at(pubkey).source_families == std::set<std::string>{"key", "wkey"}));
    const std::string comment{"expires\0\xff", 9};
    const auto timed{DecodeLegacyWalletRecords(std::vector<LegacyWalletRecord>{
        {Encode(std::string{"wkey"}, pubkey), Encode(int32_t{180100}, priv, int64_t{123}, int64_t{-456}, comment)}})};
    const auto& old_fields{*timed.keys.at(pubkey).wkey};
    BOOST_CHECK_EQUAL(old_fields.stream_version, 180100);
    BOOST_CHECK_EQUAL(old_fields.created, 123);
    BOOST_CHECK_EQUAL(old_fields.expires, -456);
    BOOST_CHECK_EQUAL(old_fields.comment, comment);
    RejectRecords({{Encode(std::string{"wkey"}, pubkey), Encode(int32_t{180100}, priv, int64_t{0}, int64_t{0}, std::string(65537, 'x'))}});

    CKeyMetadata metadata;
    metadata.nVersion = 1;
    auto first{LegacyWalletRecord{Encode(std::string{"keymeta"}, pubkey), Encode(metadata)}};
    metadata.nCreateTime = 1;
    RejectRecords({first, {first.key, Encode(metadata)}});
    const auto address{Encode(std::string{"name"}, std::string{"address"})};
    RejectRecords({{address, Encode(std::string{"one"})}, {address, Encode(std::string{"two"})}});
    CHDChain chain;
    chain.nVersion = 1;
    const auto chain_key{Encode(std::string{"hdchain"})};
    const auto initial_chain{Encode(chain)};
    ++chain.nExternalChainCounter;
    RejectRecords({{chain_key, initial_chain}, {chain_key, Encode(chain)}});
}

BOOST_AUTO_TEST_CASE(record_flags_versions_and_omission_accounting)
{
    const std::string families[]{"tx", "wtxvariant", "acentry", "acc", "bestblock", "bestblock_nomerkle", "orderposnext", "destdata", "lockedutxo", "setting", "settings"};
    std::vector<LegacyWalletRecord> records;
    for (const auto& family : families) records.push_back({Encode(family, uint32_t{42}), HexData("ff0001")});
    const auto omitted{DecodeLegacyWalletRecords(records)};
    BOOST_CHECK_EQUAL(omitted.omitted.size(), 11U);
    BOOST_CHECK_EQUAL(omitted.records.size(), 11U);
    for (const auto& record : omitted.records) {
        BOOST_CHECK(record.omitted);
        BOOST_CHECK_EQUAL(omitted.omitted.at(record.family), 1U);
    }
    const auto flags_key{Encode(std::string{"flags"})};
    for (uint64_t flags : {uint64_t{1} << 34, uint64_t{1} << 35, uint64_t{1} << 63}) {
        RejectRecords({{flags_key, Encode(flags)}});
    }
    const uint64_t flags{0xffffffffULL | WALLET_FLAG_DISABLE_PRIVATE_KEYS | WALLET_FLAG_BLANK_WALLET};
    const auto valid{DecodeLegacyWalletRecords(std::vector<LegacyWalletRecord>{{flags_key, Encode(flags)}})};
    BOOST_CHECK(valid.flags == flags);
    auto private_records{FixtureRecords(Fixtures()["fixtures"][0])};
    private_records.push_back({flags_key, Encode(flags)});
    RejectRecords(private_records);
    for (const std::string name : {"version", "minversion"}) {
        RejectRecords({{Encode(name), Encode(int32_t{-1})}});
    }
    RejectRecords({{Encode(std::string{"minversion"}), Encode(int32_t{169901})}});
    const auto versions{DecodeLegacyWalletRecords(std::vector<LegacyWalletRecord>{
        {Encode(std::string{"version"}), Encode(int32_t{10300})},
        {Encode(std::string{"minversion"}), Encode(int32_t{169900})}})};
    BOOST_CHECK(versions.file_version == 10300);
    BOOST_CHECK(versions.min_version == 169900);
    for (const std::string name : {"unknown", "walletdescriptor", "walletdescriptorcache", "walletdescriptorlhcache", "walletdescriptorkey", "walletdescriptorckey", "activeexternalspk", "activeinternalspk", "addr", "blockindex", "owner"}) {
        RejectRecords({{Encode(name), {}}});
    }
}

BOOST_AUTO_TEST_CASE(record_public_metadata_and_pool_versions)
{
    const auto fixtures{Fixtures()};
    const auto pubkey{PublicKey(fixtures["fixtures"][0]["keys"][0]["pubkey"].get_str())};
    const auto default_key{LegacyWalletRecord{Encode(std::string{"defaultkey"}), Encode(pubkey)}};
    const std::string address{"\0\xff", 2}, label{"\xff\0\r\n", 4};
    const auto inventory{DecodeLegacyWalletRecords(std::vector<LegacyWalletRecord>{
        default_key, {Encode(std::string{"name"}, address), Encode(label)},
        {Encode(std::string{"purpose"}, address), Encode(std::string{})}})};
    BOOST_CHECK(inventory.default_pubkey == pubkey);
    BOOST_CHECK(!inventory.keys.at(pubkey).private_key);
    BOOST_CHECK(inventory.keys.at(pubkey).source_families == std::set<std::string>{"defaultkey"});
    BOOST_CHECK(inventory.addresses.at(address).label == label);
    BOOST_CHECK(inventory.addresses.at(address).purpose == "");

    // Exact disk field orders from pinned v0.13/v0.15/v0.18/v29.5 CKeyPool.
    for (const int version : {130000, 150000, 180100, 259900}) {
        DataStream value;
        value << int32_t{version} << std::numeric_limits<int64_t>::min() << pubkey;
        if (version >= 150000) value << true;
        if (version >= 180100) value << false;
        const auto records{std::vector<LegacyWalletRecord>{{Encode(std::string{"pool"}, int64_t{-7}), {value.begin(), value.end()}}}};
        const auto decoded{DecodeLegacyWalletRecords(records)};
        const auto& pool{decoded.pool.at(-7)};
        BOOST_CHECK_EQUAL(pool.stream_version, version);
        BOOST_CHECK_EQUAL(pool.time, std::numeric_limits<int64_t>::min());
        BOOST_CHECK(pool.pubkey == pubkey);
        BOOST_CHECK_EQUAL(pool.internal_present, version >= 150000);
        BOOST_CHECK_EQUAL(pool.internal, version >= 150000);
        BOOST_CHECK_EQUAL(pool.pre_split_present, version >= 180100);
        BOOST_CHECK(!pool.pre_split);
        BOOST_CHECK_NO_THROW(DecodeLegacyWalletRecords(std::vector{records[0], records[0]}));
        auto conflict{records[0]};
        conflict.value[4] ^= std::byte{1};
        RejectRecords({records[0], conflict});
    }
    CKeyMetadata metadata;
    metadata.nVersion = 12;
    metadata.nCreateTime = std::numeric_limits<int64_t>::min();
    metadata.hdKeypath = std::string{"\xff\0", 2};
    metadata.has_key_origin = false;
    metadata.key_origin.fingerprint[0] = 42;
    metadata.key_origin.path = {0x80000000U, 17};
    const auto meta{DecodeLegacyWalletRecords(std::vector<LegacyWalletRecord>{
        {Encode(std::string{"keymeta"}, pubkey), Encode(metadata)}})};
    BOOST_CHECK(Encode(*meta.keys.at(pubkey).metadata) == Encode(metadata));
    BOOST_CHECK(meta.keys.at(pubkey).source_families == std::set<std::string>{"keymeta"});
    BOOST_CHECK(!meta.keys.at(pubkey).private_key);
}

BOOST_AUTO_TEST_CASE(record_rejects_required_corruption)
{
    const auto fixtures{Fixtures()};
    const auto pubkey{PublicKey(fixtures["fixtures"][0]["keys"][0]["pubkey"].get_str())};
    const auto encrypted{FixtureRecords(fixtures["fixtures"][6])};
    auto cipher{encrypted[3]};
    BOOST_REQUIRE(DecodeLegacyWalletRecords(std::vector{cipher}).keys.begin()->second.ciphertext.has_value());
    cipher.value.back() ^= std::byte{1};
    RejectRecords({cipher});
    auto master{CMasterKey{}};
    master.vchCryptedKey.resize(48);
    master.vchSalt.resize(8);
    const auto master_key{Encode(std::string{"mkey"}, uint32_t{1})};
    for (const auto mutation : {0, 1, 2, 3, 4, 5}) {
        auto bad{master};
        switch (mutation) {
        case 0: bad.nDeriveIterations = 0; break;
        case 1: bad.nDeriveIterations = 10'000'001; break;
        case 2: bad.nDerivationMethod = 1; break;
        case 3: bad.vchSalt.resize(7); break;
        case 4: bad.vchCryptedKey.resize(32); break;
        case 5: bad.vchOtherDerivationParameters.push_back(0); break;
        }
        RejectRecords({{master_key, Encode(bad)}});
    }
    RejectRecords({{master_key, Encode(master)}, {master_key, Encode(master)}});
    CKeyMetadata metadata;
    for (const auto version : {0, 2, 9, 11, 13}) {
        metadata.nVersion = version;
        RejectRecords({{Encode(std::string{"keymeta"}, pubkey), Encode(metadata)}});
    }
    metadata.nVersion = 12;
    auto bad_bool{Encode(metadata)};
    bad_bool.back() = std::byte{2};
    RejectRecords({{Encode(std::string{"keymeta"}, pubkey), bad_bool}});
    auto invalid_pub{ParseHex("02ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff")};
    RejectRecords({{Encode(std::string{"defaultkey"}), Encode(invalid_pub)}});
    RejectRecords({{Encode(std::string{"defaultkey"}), Encode(std::vector<unsigned char>(34))}});
    for (const int version : {0, 3}) {
        RejectRecords({{Encode(std::string{"hdchain"}), Encode(int32_t{version}, uint32_t{0}, uint160{})}});
    }
    for (const auto& script_bytes : {std::vector<unsigned char>{0x4c}, {0x4d, 0x01}, {0x4e, 0xff, 0xff, 0xff, 0x7f}, {0x02, 0x01}}) {
        const CScript script{script_bytes.begin(), script_bytes.end()};
        RejectRecords({{Encode(std::string{"cscript"}, Hash160(script)), Encode(script)}});
        RejectRecords({{Encode(std::string{"watchmeta"}, script), Encode(CKeyMetadata{})}});
    }
    const CScript script{OP_1};
    RejectRecords({{Encode(std::string{"cscript"}, uint160{}), Encode(script)}});
    RejectRecords({{Encode(std::string{"watchs"}, script), Encode(uint8_t{1})}});
    const auto disabled{DecodeLegacyWalletRecords(std::vector<LegacyWalletRecord>{
        {Encode(std::string{"watchs"}, script), Encode(uint8_t{'0'})}})};
    BOOST_CHECK(disabled.scripts.at(CScriptID(script)).watch_enabled == false);
    RejectRecords({{Encode(std::string{"watchs"}, script), Encode(uint8_t{'0'})},
                   {Encode(std::string{"watchs"}, script), Encode(uint8_t{'1'})}});
    RejectRecords({{Encode(std::string{"pool"}, int64_t{0}), Encode(int32_t{150000}, int64_t{0}, pubkey, uint8_t{2})}});
}

BOOST_AUTO_TEST_CASE(record_bounds_before_deserialization)
{
    const auto pubkey{PublicKey(Fixtures()["fixtures"][0]["keys"][0]["pubkey"].get_str())};
    // Oversized declarations with no payload fail before vector/string growth.
    for (const std::string family : {"key", "wkey", "ckey", "mkey"}) {
        DataStream value;
        if (family == "wkey") value << int32_t{180100};
        WriteCompactSize(value, 0xffffffffULL);
        RejectRecords({{family == "mkey" ? Encode(family, uint32_t{1}) : Encode(family, pubkey), {value.begin(), value.end()}}});
    }
    CKeyMetadata metadata;
    metadata.nVersion = 12;
    metadata.key_origin.path.resize(256);
    RejectRecords({{Encode(std::string{"keymeta"}, pubkey), Encode(metadata)}});
    metadata.key_origin.path.resize(255);
    BOOST_CHECK_NO_THROW(DecodeLegacyWalletRecords(std::vector<LegacyWalletRecord>{
        {Encode(std::string{"keymeta"}, pubkey), Encode(metadata)}}));
    metadata.hdKeypath.resize(65537, 'x');
    RejectRecords({{Encode(std::string{"keymeta"}, pubkey), Encode(metadata)}});
    metadata.hdKeypath.resize(65536);
    BOOST_CHECK_NO_THROW(DecodeLegacyWalletRecords(std::vector<LegacyWalletRecord>{
        {Encode(std::string{"keymeta"}, pubkey), Encode(metadata)}}));
    RejectRecords({{Encode(std::string{"name"}, std::string(65537, 'a')), Encode(std::string{})}});
    RejectRecords({{Encode(std::string{"name"}, std::string{}), Encode(std::string(65537, 'a'))}});
    const auto exact{DecodeLegacyWalletRecords(std::vector<LegacyWalletRecord>{
        {Encode(std::string{"name"}, std::string(65536, 'a')), Encode(std::string(65536, 'b'))}})};
    BOOST_CHECK_EQUAL(exact.addresses.begin()->first.size(), 65536U);
    std::vector<LegacyWalletRecord> slots;
    CMasterKey master;
    master.vchCryptedKey.resize(48);
    master.vchSalt.resize(8);
    for (uint32_t id{0}; id < 32; ++id) slots.push_back({Encode(std::string{"mkey"}, id), Encode(master)});
    BOOST_CHECK_NO_THROW(DecodeLegacyWalletRecords(slots));
    slots.push_back({Encode(std::string{"mkey"}, uint32_t{32}), Encode(master)});
    RejectRecords(slots);
}

BOOST_AUTO_TEST_CASE(storage_formats_and_compatibility)
{
    const fs::path path{m_path_root / "wallet.dat"};
    size_t combinations{0};
    for (const uint32_t page_size : {512U, 1024U, 2048U, 4096U, 8192U, 16384U, 32768U, 65536U}) {
        for (const bool big : {false, true}) {
            for (const bool unusual : {false, true}) {
                StorageFixture fixture(page_size, big);
                if (unusual) {
                    fixture.Items(3, {fixture.Inline("name"), fixture.Inline("example")}, 5, 1, 3);
                    // Ignore stale inner last_page and trailing partial slack,
                    // exactly as the migration parser's compatibility comments.
                    fixture.Put32(2 * page_size + 32, 1);
                    fixture.bytes.append(17, '\x7f');
                }
                BOOST_REQUIRE(WriteBinaryFile(path, fixture.bytes));
                const auto time{fs::last_write_time(path)};
                const auto entries{Entries(m_path_root)};
                BerkeleyRODatabase migration(path);
                LegacyWalletSource source(path);
                BOOST_REQUIRE_EQUAL(source.Records().size(), 1U);
                BOOST_REQUIRE_EQUAL(migration.m_records.size(), 1U);
                BOOST_CHECK(source.Records()[0].key == migration.m_records.begin()->first);
                BOOST_CHECK(source.Records()[0].value == migration.m_records.begin()->second);
                BOOST_CHECK(ReadBinaryFile(path).second == fixture.bytes);
                BOOST_CHECK(fs::last_write_time(path) == time);
                BOOST_CHECK(Entries(m_path_root) == entries);
                ++combinations;
            }
        }
    }
    BOOST_CHECK_EQUAL(combinations, 32U);
}

BOOST_AUTO_TEST_CASE(storage_duplicates_and_deletion)
{
    const fs::path path{m_path_root / "wallet.dat"};
    for (const std::string value : {"one", "two"}) {
        StorageFixture fixture;
        fixture.Items(3, {fixture.Inline("same"), fixture.Inline("one"), fixture.Inline("same"), fixture.Inline(value)});
        BOOST_REQUIRE(WriteBinaryFile(path, fixture.bytes));
        BOOST_CHECK_THROW(LegacyWalletSource{path}, std::runtime_error);
        BOOST_CHECK(ReadBinaryFile(path).second == fixture.bytes);
    }
    StorageFixture deleted;
    deleted.Items(3, {deleted.Inline("gone", true), deleted.Overflow(0xffffffff, 0xffffffff, true)});
    BOOST_REQUIRE(WriteBinaryFile(path, deleted.bytes));
    BOOST_CHECK(LegacyWalletSource{path}.Records().empty());
    for (const bool key_deleted : {false, true}) {
        StorageFixture mismatch;
        mismatch.Items(3, {mismatch.Inline("name", key_deleted), mismatch.Inline("example", !key_deleted)});
        BOOST_REQUIRE(WriteBinaryFile(path, mismatch.bytes));
        BOOST_CHECK_THROW(LegacyWalletSource{path}, std::runtime_error);
    }
    // Identical inline storage may legitimately be referenced by distinct
    // values. It must not become a blanket repeated-offset rejection.
    StorageFixture aliases;
    aliases.Items(3, {aliases.Inline("a"), aliases.Inline("v"), aliases.Inline("b"), aliases.Inline("v")});
    aliases.Put16(3 * 4096 + 32, ReadLE16(reinterpret_cast<const unsigned char*>(aliases.bytes.data() + 3 * 4096 + 28)));
    BOOST_REQUIRE(WriteBinaryFile(path, aliases.bytes));
    BOOST_CHECK_EQUAL(LegacyWalletSource{path}.Records().size(), 2U);
}

BOOST_AUTO_TEST_CASE(storage_overflow_sharing_and_cycles, *boost::unit_test::timeout(10))
{
    const fs::path path{m_path_root / "wallet.dat"};
    for (const uint32_t page_size : {512U, 1024U, 2048U, 4096U, 8192U, 16384U, 32768U, 65536U}) {
        for (const bool big : {false, true}) {
            StorageFixture shared(page_size, big);
            shared.OverflowPage(4, "abc", 5);
            shared.OverflowPage(5, "def");
            shared.Items(3, {shared.Inline("a"), shared.Overflow(4, 6), shared.Inline("b"), shared.Overflow(5, 3), shared.Inline("c"), shared.Overflow(4, 6)});
            BOOST_REQUIRE(WriteBinaryFile(path, shared.bytes));
            LegacyWalletSource source(path);
            BOOST_REQUIRE_EQUAL(source.Records().size(), 3U);
            BOOST_CHECK(source.Records()[0].value == HexData("616263646566"));
            BOOST_CHECK(source.Records()[1].value == HexData("646566"));
            BOOST_CHECK(source.Records()[2].value == source.Records()[0].value);
            BOOST_CHECK_EQUAL(BerkeleyRODatabase{path}.m_records.size(), 3U);
            if (page_size != 512) continue;
            for (const uint32_t length : {5U, 7U, 0xffffffffU}) {
                auto inconsistent{shared};
                inconsistent.Items(3, {inconsistent.Inline("a"), inconsistent.Overflow(4, length)});
                BOOST_REQUIRE(WriteBinaryFile(path, inconsistent.bytes));
                BOOST_CHECK_THROW(LegacyWalletSource{path}, std::runtime_error);
            }
            for (const auto pattern : {0, 1, 2}) {
                StorageFixture cycle(512, big);
                cycle.OverflowPage(4, "", pattern == 0 ? 4 : 5);
                cycle.OverflowPage(5, "", pattern == 1 ? 4 : 5);
                cycle.Items(3, {cycle.Inline("a"), cycle.Overflow(4, 0)});
                BOOST_REQUIRE(WriteBinaryFile(path, cycle.bytes));
                BOOST_CHECK_THROW(LegacyWalletSource{path}, std::runtime_error);
            }
            auto huge_key{shared};
            huge_key.Items(3, {huge_key.Overflow(4, 1024 * 1024 + 1), huge_key.Inline("v")});
            BOOST_REQUIRE(WriteBinaryFile(path, huge_key.bytes));
            BOOST_CHECK_THROW(LegacyWalletSource{path}, std::runtime_error);
        }
    }
}

BOOST_AUTO_TEST_CASE(storage_rejects_deleted_subdatabase_entry, *boost::unit_test::timeout(10))
{
    const fs::path path{m_path_root / "wallet.dat"};
    StorageFixture deleted;
    deleted.Items(1, {deleted.Inline("main"), deleted.Inline(std::string_view{"\0\0\0\2", 4}, true)});
    BOOST_REQUIRE(WriteBinaryFile(path, deleted.bytes));
    BOOST_CHECK_THROW(LegacyWalletSource{path}, std::runtime_error);
}

BOOST_AUTO_TEST_CASE(storage_page_bounds_and_tree_traversal, *boost::unit_test::timeout(10))
{
    const fs::path path{m_path_root / "wallet.dat"};
    for (const auto mutation : {0, 1, 2, 3, 4, 5, 6, 7}) {
        StorageFixture fixture;
        switch (mutation) {
        case 0: fixture.Put16(3 * 4096 + 26, 26); break; // index table overlap
        case 1: fixture.Put16(3 * 4096 + 26, 4095); break;
        case 2: fixture.Put16(3 * 4096 + 20, 65535); break;
        case 3: fixture.Put16(3 * 4096 + 4089, 65535); break; // inline length
        case 4: fixture.Put32(88, 0xffffffff); break;
        case 5: fixture.Put32(2 * 4096 + 88, 4); fixture.bytes.append(100, '\0'); break;
        case 6: fixture.bytes.resize(fixture.bytes.size() - 1); break;
        case 7:
            fixture.Put16(3 * 4096 + 26, 4090);
            fixture.bytes[3 * 4096 + 4092] = 3; // pointer lacks its fixed fields
            break;
        }
        BOOST_REQUIRE(WriteBinaryFile(path, fixture.bytes));
        BOOST_CHECK_THROW(LegacyWalletSource{path}, std::runtime_error);
    }
    StorageFixture tree;
    tree.EnsurePages(5);
    tree.Items(4, {tree.Inline("a"), tree.Inline("value")});
    tree.Items(3, {tree.Internal(4)}, 3, 2);
    BOOST_REQUIRE(WriteBinaryFile(path, tree.bytes));
    BOOST_CHECK_EQUAL(LegacyWalletSource{path}.Records().size(), 1U);
    BOOST_CHECK_EQUAL(BerkeleyRODatabase{path}.m_records.size(), 1U);
    for (const bool cycle : {false, true}) {
        auto repeated{tree};
        repeated.Items(3, cycle ? std::vector{tree.Internal(3)} : std::vector{tree.Internal(4), tree.Internal(4)}, 3, 2);
        BOOST_REQUIRE(WriteBinaryFile(path, repeated.bytes));
        BOOST_CHECK_THROW(LegacyWalletSource{path}, std::runtime_error);
    }
    auto internal_length{tree};
    internal_length.Put16(3 * 4096 + 4084, 65535);
    BOOST_REQUIRE(WriteBinaryFile(path, internal_length.bytes));
    BOOST_CHECK_THROW(LegacyWalletSource{path}, std::runtime_error);
    // Physical duplicates on different reachable leaves must also fail.
    auto cross_leaf{tree};
    cross_leaf.EnsurePages(6);
    cross_leaf.Items(5, {tree.Inline("a"), tree.Inline("value")});
    cross_leaf.Items(3, {tree.Internal(4), tree.Internal(5)}, 3, 2);
    BOOST_REQUIRE(WriteBinaryFile(path, cross_leaf.bytes));
    BOOST_CHECK_THROW(LegacyWalletSource{path}, std::runtime_error);
}

BOOST_AUTO_TEST_CASE(storage_resource_boundaries)
{
    const fs::path path{m_path_root / "wallet.dat"};
    const StorageFixture fixture;
    BOOST_REQUIRE(WriteBinaryFile(path, fixture.bytes));
    BerkeleyROLimits exact;
    exact.file_bytes = fixture.bytes.size();
    exact.records = 1;
    exact.key_bytes = 4;
    exact.pair_bytes = 11;
    exact.total_bytes = 11;
    exact.page_visits = 15; // Metadata/LSNs, per-item header probes and lazy payload reads.
    BOOST_CHECK_NO_THROW((BerkeleyRODatabase{path, true, exact}));
    for (const auto field : {&BerkeleyROLimits::file_bytes, &BerkeleyROLimits::records,
                            &BerkeleyROLimits::key_bytes, &BerkeleyROLimits::pair_bytes,
                            &BerkeleyROLimits::total_bytes, &BerkeleyROLimits::page_visits}) {
        auto reduced{exact};
        --(reduced.*field);
        BOOST_CHECK_THROW((BerkeleyRODatabase{path, true, reduced}), std::runtime_error);
    }
    // Both pair and aggregate declarations are rejected before overflow fetch;
    // no giant file or payload is allocated to exercise the production budgets.
    StorageFixture declared;
    declared.Items(3, {declared.Inline("a"), declared.Overflow(0xffffffff, 16 * 1024 * 1024)});
    BOOST_REQUIRE(WriteBinaryFile(path, declared.bytes));
    BOOST_CHECK_EXCEPTION(LegacyWalletSource{path}, std::runtime_error, [](const auto& error) {
        return std::string_view{error.what()} == "BDB records exceed extraction limits";
    });
    StorageFixture aggregate;
    aggregate.Items(3, {aggregate.Inline("a"), aggregate.Inline("x"), aggregate.Inline("b"), aggregate.Overflow(0xffffffff, 1)});
    BerkeleyROLimits aggregate_limit;
    aggregate_limit.total_bytes = 3;
    BOOST_REQUIRE(WriteBinaryFile(path, aggregate.bytes));
    BOOST_CHECK_EXCEPTION((BerkeleyRODatabase{path, true, aggregate_limit}), std::runtime_error, [](const auto& error) {
        return std::string_view{error.what()} == "BDB records exceed extraction limits";
    });
    StorageFixture deleted_aliases(512);
    deleted_aliases.Header(3, 5, 100, 509);
    for (size_t index{0}; index < 100; ++index) deleted_aliases.Put16(3 * 512 + 26 + 2 * index, 509);
    deleted_aliases.Put16(3 * 512 + 509, 0);
    deleted_aliases.bytes[3 * 512 + 511] = '\x81';
    BOOST_REQUIRE(WriteBinaryFile(path, deleted_aliases.bytes));
    BOOST_CHECK(LegacyWalletSource{path}.Records().empty());
    BerkeleyROLimits probe_limit;
    probe_limit.page_visits = 16;
    BOOST_CHECK_EXCEPTION((BerkeleyRODatabase{path, true, probe_limit}), std::runtime_error, [](const auto& error) {
        return std::string_view{error.what()} == "BDB page visits exceed extraction limit";
    });
    StorageFixture overflow;
    overflow.OverflowPage(4, "x");
    overflow.Items(3, {overflow.Inline("a"), overflow.Overflow(4, 5000)});
    overflow.Put16(4 * 4096 + 22, 4095);
    BOOST_REQUIRE(WriteBinaryFile(path, overflow.bytes));
    BOOST_CHECK_THROW(LegacyWalletSource{path}, std::runtime_error);
    BOOST_CHECK(ReadBinaryFile(path).second == overflow.bytes);
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
