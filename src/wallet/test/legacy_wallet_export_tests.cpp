// Copyright (c) 2026 The Bitcoin Core developers
// Copyright (C) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/legacy_export.h>
#include <wallet/migrate.h>
#include <wallet/wallettool_input.h>

#include <crypto/common.h>
#include <hash.h>
#include <key_io.h>
#include <logging.h>
#include <test/util/setup_common.h>
#include <util/bip32.h>
#include <util/readwritefile.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <test/wallet/data/legacy_export.json.h>

#include <univalue.h>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <list>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <addresstype.h>

#include <fcntl.h>

#ifndef WIN32
#include <csignal>
#include <poll.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/stat.h>
#include <sys/syscall.h>
#endif
#else
#include <windows.h>
#include <aclapi.h>
#include <io.h>
#endif

namespace wallet {
#if defined(WIN32) || defined(__linux__)
struct LegacyExportOutputTestAccess {
#ifdef WIN32
    static bool IsPhysicalVolumeRoot(HANDLE directory)
    {
        return LegacyExportOutput::IsPhysicalWindowsVolumeRoot(directory);
    }
#endif
#ifdef __linux__
    using MountFacts = LegacyExportOutput::LinuxMountFacts;
    static MountFacts Inspect(int directory) { return LegacyExportOutput::InspectLinuxMount(directory); }
    static bool SameMount(const MountFacts& source, const MountFacts& output) { return LegacyExportOutput::SameLinuxMount(source, output); }
#endif
};
#endif
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

// Deterministic disposable test material. Frozen independent OpenSSL vectors
// in legacy_export.json separately check compatibility without this encryptor.
CKey RecoveryTestKey(unsigned char value, bool compressed = true)
{
    CKeyingMaterial scalar(32, 0);
    scalar.back() = value;
    CKey key;
    key.Set(scalar.begin(), scalar.end(), compressed);
    return key;
}

void AddRecoverySlot(LegacyWalletInventory& inventory, uint32_t id, const SecureString& passphrase,
                     unsigned char master_byte, const std::vector<CKey>& keys)
{
    CKeyingMaterial symmetric_key(32, master_byte);
    CMasterKey master;
    master.vchSalt.assign(8, master_byte);
    master.nDeriveIterations = 1;
    CCrypter crypter;
    BOOST_REQUIRE(crypter.SetKeyFromPassphrase(passphrase, master.vchSalt, master.nDeriveIterations, 0));
    BOOST_REQUIRE(crypter.Encrypt(symmetric_key, master.vchCryptedKey));
    inventory.master_keys.emplace(id, master);
    for (const auto& key : keys) {
        const auto pubkey{key.GetPubKey()};
        auto& entry{inventory.keys[pubkey]};
        entry.ciphertext.emplace();
        const CKeyingMaterial scalar{UCharCast(key.begin()), UCharCast(key.end())};
        BOOST_REQUIRE(EncryptSecret(symmetric_key, scalar, pubkey.GetHash(), *entry.ciphertext));
        entry.source_families.insert("ckey");
    }
}

void RejectRecovery(const LegacyWalletInventory& inventory, const SecureString& passphrase, std::string_view message)
{
    try {
        const auto recovered{RecoverLegacyWalletSecrets(inventory, passphrase)};
        BOOST_FAIL("Recovery unexpectedly succeeded");
    } catch (const std::runtime_error& error) {
        // Exact fixed diagnostics prevent secret values being interpolated.
        BOOST_CHECK(std::string_view{error.what()} == message);
    }
}

void MarkRecoverySeed(LegacyWalletInventory& inventory, const CKey& seed, const std::string& marker = "s")
{
    auto& entry{inventory.keys[seed.GetPubKey()]};
    entry.private_key = seed;
    entry.metadata.emplace();
    entry.metadata->hdKeypath = marker;
    entry.metadata->hd_seed_id = seed.GetPubKey().GetID();
}

void RejectRoots(const LegacyWalletInventory& inventory, const LegacyRecoveredSecrets& secrets, std::string_view message)
{
    try {
        const auto roots{DeriveLegacyWalletRoots(inventory, secrets)};
        BOOST_FAIL("Root recovery unexpectedly succeeded");
    } catch (const std::runtime_error& error) {
        BOOST_CHECK(std::string_view{error.what()} == message);
    }
}

void CheckRootOracle(const LegacyExportSeed& seed, const UniValue& expected, bool mainnet)
{
    BOOST_REQUIRE(seed.pubkey);
    BOOST_REQUIRE(seed.root);
    BOOST_REQUIRE(seed.fingerprint);
    BOOST_CHECK(*seed.pubkey == PublicKey(expected["pubkey"].get_str()));
    BOOST_CHECK_EQUAL(seed.active, expected["active"].get_bool());
    BOOST_CHECK((std::string_view{seed.extended_private_key.data(), seed.extended_private_key.size()} ==
                 expected[mainnet ? "root_main" : "root_test"].get_str()));
    BOOST_CHECK((std::string_view{seed.wif.data(), seed.wif.size()} == expected[mainnet ? "wif_main" : "wif_test"].get_str()));
    BOOST_CHECK(std::ranges::equal(*seed.fingerprint, ParseHex(expected["fingerprint"].get_str())));
    BOOST_CHECK_EQUAL(seed.root->nDepth, expected["depth"].getInt<int>());
    BOOST_CHECK_EQUAL(seed.root->nChild, expected["child"].getInt<int>());
    BOOST_CHECK(std::ranges::equal(seed.root->fingerprint, ParseHex(expected["parent_fingerprint"].get_str())));
    BOOST_CHECK(std::ranges::equal(seed.root->chaincode, ParseHex(expected["chaincode"].get_str())));
    BOOST_CHECK(std::ranges::equal(seed.root->key, ParseHex<std::byte>(expected["root_scalar"].get_str())));
}

CKey AddCoverageChild(LegacyWalletInventory& inventory, const CKey& seed, uint32_t branch, uint32_t index)
{
    CExtKey root;
    root.SetSeed(seed);
    CExtKey child{root};
    const std::vector<uint32_t> path{BIP32_HARDENED_FLAG, BIP32_HARDENED_FLAG | branch, BIP32_HARDENED_FLAG | index};
    for (uint32_t step : path) {
        CExtKey next;
        BOOST_REQUIRE(child.Derive(next, step));
        child = std::move(next);
    }
    auto& entry{inventory.keys[child.key.GetPubKey()]};
    entry.private_key = child.key;
    auto& metadata{entry.metadata.emplace()};
    metadata.hdKeypath = WriteHDKeypath(path, true);
    metadata.hd_seed_id = seed.GetPubKey().GetID();
    metadata.key_origin.path = path;
    metadata.key_origin.fingerprint = root.id_key_fingerprint();
    metadata.has_key_origin = true;
    return child.key;
}

void RejectCoverage(const LegacyWalletInventory& inventory, const LegacyRecoveredSecrets& secrets, std::string_view message)
{
    try {
        const auto coverage{ValidateLegacyWalletCoverage(inventory, secrets)};
        BOOST_FAIL("Coverage unexpectedly succeeded");
    } catch (const std::runtime_error& error) {
        BOOST_CHECK(std::string_view{error.what()} == message);
    }
}
#ifdef WIN32
class OutputTestSecurity
{
    HANDLE m_token{nullptr};
    std::vector<DWORD> m_storage;
    std::vector<DWORD> m_acl;
    SECURITY_DESCRIPTOR m_descriptor{};

public:
    OutputTestSecurity()
    {
        BOOST_REQUIRE(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &m_token));
        DWORD size{0};
        GetTokenInformation(m_token, TokenUser, nullptr, 0, &size);
        BOOST_REQUIRE(size >= sizeof(TOKEN_USER));
        m_storage.resize((size + sizeof(DWORD) - 1) / sizeof(DWORD));
        BOOST_REQUIRE(GetTokenInformation(m_token, TokenUser, m_storage.data(), size, &size));
        const auto owner{Owner()};
        m_acl.resize((sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(owner) + sizeof(DWORD) - 1) / sizeof(DWORD));
        auto* acl{reinterpret_cast<PACL>(m_acl.data())};
        BOOST_REQUIRE(InitializeAcl(acl, static_cast<DWORD>(m_acl.size() * sizeof(DWORD)), ACL_REVISION));
        BOOST_REQUIRE(AddAccessAllowedAce(acl, ACL_REVISION, FILE_ALL_ACCESS, owner));
        BOOST_REQUIRE(InitializeSecurityDescriptor(&m_descriptor, SECURITY_DESCRIPTOR_REVISION));
        BOOST_REQUIRE(SetSecurityDescriptorOwner(&m_descriptor, owner, FALSE));
        BOOST_REQUIRE(SetSecurityDescriptorDacl(&m_descriptor, TRUE, acl, FALSE));
        BOOST_REQUIRE(SetSecurityDescriptorControl(&m_descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED));
    }
    ~OutputTestSecurity() { if (m_token) CloseHandle(m_token); }
    PSID Owner() const { return reinterpret_cast<const TOKEN_USER*>(m_storage.data())->User.Sid; }
    SECURITY_DESCRIPTOR* Descriptor() { return &m_descriptor; }
};

void CheckPrivateOutputTestAcl(const fs::path& path)
{
    OutputTestSecurity expected;
    const HANDLE handle{CreateFileW(path.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    BOOST_REQUIRE(handle != INVALID_HANDLE_VALUE);
    PSID owner{nullptr};
    PACL acl{nullptr};
    PSECURITY_DESCRIPTOR descriptor{nullptr};
    const DWORD result{GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                       &owner, nullptr, &acl, nullptr, &descriptor)};
    const BOOL closed{CloseHandle(handle)};
    BOOST_REQUIRE_EQUAL(result, ERROR_SUCCESS);
    // Own the returned descriptor even if an assertion aborts the test.
    const std::unique_ptr<void, decltype(&LocalFree)> security{descriptor, LocalFree};
    BOOST_REQUIRE(closed);
    BOOST_REQUIRE(owner && EqualSid(owner, expected.Owner()));
    BOOST_REQUIRE(acl != nullptr);
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision{0};
    BOOST_REQUIRE(GetSecurityDescriptorControl(descriptor, &control, &revision));
    BOOST_CHECK(control & SE_DACL_PROTECTED);
    ACL_SIZE_INFORMATION info{};
    BOOST_REQUIRE(GetAclInformation(acl, &info, sizeof(info), AclSizeInformation));
    BOOST_REQUIRE_EQUAL(info.AceCount, 1U);
    void* raw{nullptr};
    BOOST_REQUIRE(GetAce(acl, 0, &raw));
    auto* ace{static_cast<ACCESS_ALLOWED_ACE*>(raw)};
    BOOST_CHECK_EQUAL(ace->Header.AceType, ACCESS_ALLOWED_ACE_TYPE);
    BOOST_CHECK_EQUAL(ace->Header.AceFlags, 0);
    BOOST_CHECK_EQUAL(ace->Mask, FILE_ALL_ACCESS);
    BOOST_CHECK(EqualSid(&ace->SidStart, expected.Owner()));
}
#endif

struct OutputFixture {
    const fs::path offline;
    const fs::path directory;
    const fs::path destination;
    std::unique_ptr<LegacyWalletSource> source;

    explicit OutputFixture(const fs::path& root)
        : offline(root / "offline"), directory(root / "exports"), destination(directory / "wallet.keys")
    {
        fs::create_directories(offline);
#ifndef WIN32
        fs::create_directories(directory);
        fs::permissions(directory, fs::perms::owner_all);
#else
        OutputTestSecurity private_directory;
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), private_directory.Descriptor(), FALSE};
        BOOST_REQUIRE(CreateDirectoryW(directory.c_str(), &attributes));
#endif
        if (!WriteBinaryFile(offline / "wallet.dat", LegacyFile(true))) throw std::runtime_error("Cannot create test fixture");
        source = std::make_unique<LegacyWalletSource>(offline);
    }
};

#ifndef WIN32
class OutputUmask
{
    const mode_t m_previous;
public:
    explicit OutputUmask(mode_t mask) : m_previous(umask(mask)) {}
    ~OutputUmask() { umask(m_previous); }
};
#endif

std::string RecordFile(std::span<const LegacyWalletRecord> records, uint32_t page_size = 4096, bool big = false)
{
    StorageFixture storage{page_size, big};
    uint32_t page{static_cast<uint32_t>(storage.bytes.size() / page_size)};
    const auto pack = [&](const SerializeData& data) {
        const std::string_view view{reinterpret_cast<const char*>(data.data()), data.size()};
        if (data.size() <= page_size / 4) return storage.Inline(view);
        const uint32_t first{page};
        for (size_t offset{0}; offset < data.size();) {
            const auto count{std::min<size_t>(page_size - 26, data.size() - offset)};
            storage.OverflowPage(page, view.substr(offset, count), offset + count == data.size() ? 0 : page + 1);
            ++page;
            offset += count;
        }
        return storage.Overflow(first, data.size());
    };
    std::vector<std::string> items;
    for (const auto& record : records) {
        items.push_back(pack(record.key));
        items.push_back(pack(record.value));
    }
    storage.Items(3, items);
    return storage.bytes;
}

struct ReadableExport {
    std::map<std::string, std::vector<UniValue>> metadata;
    std::vector<std::string> payloads;
    std::string active_root;
};

ReadableExport ReadExport(const std::string& bytes, const std::string& network)
{
    BOOST_REQUIRE(bytes.ends_with("# End of dump\n# roots-export-complete: 1\n"));
    BOOST_REQUIRE(bytes.starts_with("# Bitcoin Roots offline legacy key export\n# roots-dumpwallet-version: 1\n# roots-network: " + network + "\n"));
    const std::map<std::string, std::vector<std::string>> fields{
        {"wallet", {"type","network","format","page_size","byte_order","source_bytes","file_version","min_version","flags","unknown_tolerable_flags","default_pubkey","state","counts"}},
        {"hdchain", {"type","version","seed_id","external_counter","internal_counter"}},
        {"key", {"type","pubkey","key_id","has_private_key","source_families","encrypted_in_source","classification","reserve","created_at","metadata","wkey","derivation"}},
        {"seed", {"type","seed_id","pubkey","active","has_secret","xprv","evidence"}},
        {"pool", {"type","index","stream_version","pubkey","time","internal","internal_present","pre_split","pre_split_present"}},
        {"script", {"type","script_id","script_hex","kinds","watch_enabled","created_at","metadata"}},
        {"address", {"type","address","label","purpose","pubkeys"}},
        {"encryption", {"type","id","method","iterations","salt","verified_with_private_key","used_for_pubkeys"}},
        {"omitted", {"type","family","records"}},
    };
    const std::vector<std::string> groups{"wallet","hdchain","key","seed","pool","script","address","encryption","omitted"};
    ReadableExport result;
    std::istringstream input{bytes};
    std::string line;
    int group{0};
    size_t line_number{0};
    while (std::getline(input, line)) {
        ++line_number;
        if (line_number <= 3) continue;
        if (line.starts_with("# roots-meta-v1 ")) {
            UniValue object;
            BOOST_REQUIRE(object.read(std::string_view{line}.substr(16)));
            const auto type{object["type"].get_str()};
            BOOST_REQUIRE(fields.contains(type));
            BOOST_CHECK(object.getKeys() == fields.at(type));
            const int next{static_cast<int>(std::find(groups.begin(), groups.end(), type) - groups.begin())};
            BOOST_REQUIRE_GE(next, group);
            if (line_number == 4) BOOST_REQUIRE_EQUAL(type, "wallet");
            group = next;
            result.metadata[type].push_back(std::move(object));
        } else if (line.starts_with("# extended private masterkey: ")) {
            BOOST_REQUIRE_EQUAL(line_number, 5U);
            result.active_root = line.substr(30);
        } else if (line == "# End of dump" || line == "# roots-export-complete: 1") {
        } else {
            BOOST_REQUIRE(!line.empty() && line[0] != '#');
            BOOST_REQUIRE_EQUAL(group, 0);
            result.payloads.push_back(line);
        }
    }
    BOOST_REQUIRE_EQUAL(result.metadata.at("wallet").size(), 1U);
    return result;
}

std::pair<LegacyExportResult, std::string> ExportRecords(OutputFixture& fixture, std::span<const LegacyWalletRecord> records,
                                                        const std::string& name = "wallet.keys", uint32_t page_size = 4096, bool big = false)
{
    fixture.source.reset();
    BOOST_REQUIRE(WriteBinaryFile(fixture.offline / "wallet.dat", RecordFile(records, page_size, big)));
    fixture.source = std::make_unique<LegacyWalletSource>(fixture.offline);
    const auto destination{fixture.directory / fs::PathFromString(name)};
    auto result{WriteLegacyWalletExport(*fixture.source, SecureString{"test"}, destination)};
    const auto bytes{ReadBinaryFile(destination)};
    BOOST_REQUIRE(bytes.first);
    fixture.source->VerifyUnchanged();
    return {std::move(result), bytes.second};
}

struct FailureMaterial {
    SecureString passphrase{"offline-cancellation-passphrase-7d42"};
    std::string label{"private-label-cb93%\n# roots-export-complete: 1"};
    std::vector<LegacyWalletRecord> records;
    std::vector<std::string> forbidden;

    FailureMaterial()
    {
        const auto fixtures{Fixtures()};
        for (const auto& fixture : fixtures["fixtures"].getValues()) {
            if (fixture["name"].get_str() == "encrypted_hd") records = FixtureRecords(fixture);
        }
        BOOST_REQUIRE(!records.empty());
        const auto inventory{DecodeLegacyWalletRecords(records)};
        const auto secrets{RecoverLegacyWalletSecrets(inventory, SecureString{"test"})};
        LegacyWalletInventory encrypted;
        std::vector<CKey> keys;
        for (const auto& [pubkey, key] : secrets.keys) keys.push_back(key);
        AddRecoverySlot(encrypted, 37, passphrase, 0x79, keys);
        std::erase_if(records, [](const auto& record) {
            SpanReader reader{record.key};
            std::string family;
            reader >> family;
            return family == "mkey" || family == "ckey";
        });
        records.push_back({Encode(std::string{"mkey"}, uint32_t{37}), Encode(encrypted.master_keys.at(37))});
        for (const auto& [pubkey, key] : encrypted.keys) {
            records.push_back({Encode(std::string{"ckey"}, pubkey), Encode(*key.ciphertext)});
        }
        const auto extra{RecoveryTestKey(91)};
        const auto pubkey{extra.GetPubKey()};
        records.push_back({Encode(std::string{"wkey"}, pubkey), Encode(int32_t{10300}, extra.GetPrivKey(), int64_t{4}, int64_t{8}, label)});
        records.push_back({Encode(std::string{"name"}, EncodeDestination(PKHash(pubkey))), Encode(label)});
        records.push_back({Encode(std::string{"pool"}, int64_t{777}), Encode(int32_t{169900}, int64_t{9}, pubkey, false, false)});
        const CScript script{CScript{} << OP_TRUE};
        records.push_back({Encode(std::string{"cscript"}, Hash160(script)), Encode(script)});
        records.push_back({Encode(std::string{"tx"}), Encode(label)});

        const auto complete{DecodeLegacyWalletRecords(records)};
        const auto recovered{RecoverLegacyWalletSecrets(complete, passphrase)};
        const auto coverage{ValidateLegacyWalletCoverage(complete, recovered)};
        forbidden.emplace_back(passphrase.data(), passphrase.size());
        forbidden.push_back(label);
        forbidden.push_back("private-label-cb93");
        // Include the known synthetic plaintext mkey as well as its stored
        // ciphertext below. Never print either needle into test diagnostics.
        forbidden.emplace_back(32, static_cast<char>(0x79));
        forbidden.push_back(HexStr(std::vector<unsigned char>(32, 0x79)));
        for (const auto& [pubkey, key] : recovered.keys) {
            forbidden.emplace_back(reinterpret_cast<const char*>(key.begin()), key.size());
            forbidden.push_back(HexStr(std::span{key.begin(), key.size()}));
        }
        for (const auto& [pubkey, key] : coverage.keys) forbidden.emplace_back(key.wif.data(), key.wif.size());
        for (const auto& [id, seed] : coverage.roots.seeds) {
            if (!seed.extended_private_key.empty()) forbidden.emplace_back(seed.extended_private_key.data(), seed.extended_private_key.size());
        }
        for (const auto& [id, master] : complete.master_keys) {
            forbidden.emplace_back(reinterpret_cast<const char*>(master.vchCryptedKey.data()), master.vchCryptedKey.size());
            forbidden.push_back(HexStr(master.vchCryptedKey));
        }
        for (const auto& record : records) {
            for (const auto& bytes : {record.key, record.value}) {
                if (bytes.size() < 8) continue;
                forbidden.emplace_back(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                forbidden.push_back(HexStr(bytes));
            }
        }
    }

    void Prepare(OutputFixture& fixture) const
    {
        fixture.source.reset();
        BOOST_REQUIRE(WriteBinaryFile(fixture.offline / "wallet.dat", RecordFile(records)));
        fixture.source = std::make_unique<LegacyWalletSource>(fixture.offline);
    }
};

// Real descriptor redirection, rather than replacing a product logging call.
// No assertions print captured bytes or forbidden values, even on a failure.
class ExportDiagnosticCapture
{
    fs::path m_root;
    std::string m_name;
    FILE* m_out{nullptr};
    FILE* m_err{nullptr};
    int m_stdout{-1};
    int m_stderr{-1};
    std::string m_debug;
    std::optional<std::list<std::function<void(const std::string&)>>::iterator> m_connection;
#ifdef WIN32
    static int Descriptor(FILE* file) { return _fileno(file); }
    static int Duplicate(int descriptor) { return _dup(descriptor); }
    static int Redirect(int from, int to) { return _dup2(from, to); }
    static void Close(int descriptor) { _close(descriptor); }
#else
    static int Descriptor(FILE* file) { return fileno(file); }
    static int Duplicate(int descriptor) { return dup(descriptor); }
    static int Redirect(int from, int to) { return dup2(from, to); }
    static void Close(int descriptor) { close(descriptor); }
#endif
    void Restore() noexcept
    {
        std::fflush(stdout);
        std::fflush(stderr);
        if (m_connection) { LogInstance().DeleteCallback(*m_connection); m_connection.reset(); }
        if (m_stdout != -1) { Redirect(m_stdout, Descriptor(stdout)); Close(m_stdout); m_stdout = -1; }
        if (m_stderr != -1) { Redirect(m_stderr, Descriptor(stderr)); Close(m_stderr); m_stderr = -1; }
        if (m_out) { std::fclose(m_out); m_out = nullptr; }
        if (m_err) { std::fclose(m_err); m_err = nullptr; }
    }

public:
    ExportDiagnosticCapture(const fs::path& root, std::string name) : m_root(root), m_name(std::move(name)) {}
    ~ExportDiagnosticCapture() { Restore(); }

    void Start()
    {
        fs::create_directories(m_root);
        m_out = fsbridge::fopen(m_root / "stdout", "w+b");
        m_err = fsbridge::fopen(m_root / "stderr", "w+b");
        if (!m_out || !m_err) throw std::runtime_error("Cannot open diagnostic capture");
        std::cout.flush();
        std::cerr.flush();
        m_stdout = Duplicate(Descriptor(stdout));
        m_stderr = Duplicate(Descriptor(stderr));
        if (m_stdout == -1 || m_stderr == -1 || Redirect(Descriptor(m_out), Descriptor(stdout)) == -1 ||
            Redirect(Descriptor(m_err), Descriptor(stderr)) == -1) throw std::runtime_error("Cannot redirect diagnostic capture");
        m_connection = LogInstance().PushBackCallback([this](const std::string& text) { m_debug += text; });
        std::fputs("capture-ready\n", stdout);
        std::fputs("capture-ready\n", stderr);
        LogInfo("capture-ready");
    }

    void Check(const FailureMaterial& material)
    {
        Restore();
        BOOST_REQUIRE(WriteBinaryFile(m_root / "debug", m_debug));
        for (const auto* channel : {"stdout", "stderr", "debug"}) {
            const auto bytes{ReadBinaryFile(m_root / channel)};
            BOOST_REQUIRE(bytes.first);
            BOOST_CHECK(bytes.second.find("capture-ready") != std::string::npos);
            for (const auto& secret : material.forbidden) {
                BOOST_CHECK(!secret.empty() && bytes.second.find(secret) == std::string::npos);
            }
            if (const auto* archive{std::getenv("LEGACY_EXPORT_DIAGNOSTIC_DIR")}) {
                BOOST_REQUIRE(fs::copy_file(m_root / channel, fs::PathFromString(archive) / fs::PathFromString(m_name + "-" + channel), fs::copy_options::none));
            }
        }
    }
};

struct ExportCancelled : std::runtime_error {
    ExportCancelled() : std::runtime_error("Legacy export cancelled") {}
};

void CaptureExportError(const std::exception& error)
{
    std::fputs(error.what(), stderr);
    std::fputc('\n', stderr);
}

void InjectExportFailure(unsigned kind)
{
    if (kind == 0) throw std::runtime_error("Injected export failure");
    if (kind == 1) throw ExportCancelled{};
    throw std::bad_alloc{};
}
} // namespace

BOOST_FIXTURE_TEST_SUITE(legacy_wallet_export_tests, BasicTestingSetup)

#ifdef __linux__
BOOST_AUTO_TEST_CASE(output_linux_mount_proof_fails_closed)
{
    using Access = LegacyExportOutputTestAccess;
    Access::MountFacts source;
    source.status = 0;
    source.mask = std::numeric_limits<uint32_t>::max();
    source.mode = S_IFDIR | 0700;
    source.device_major = source.expected_major = 8;
    source.device_minor = source.expected_minor = 1;
    source.inode = source.expected_inode = 42;
    source.mount_id = 100;
#if defined(SYS_statx) && defined(STATX_MNT_ID)
    source.mask = STATX_TYPE | STATX_INO | STATX_MNT_ID;
    auto output{source};
    output.inode = output.expected_inode = 43;
    BOOST_CHECK(Access::SameMount(source, output));
    // Subtree and whole-filesystem bind aliases can share st_dev/root facts.
    // Both have a distinct mounted view; no table-root or device-only shortcut.
    for (const uint64_t alias_id : {101ULL, 102ULL, 100ULL + (1ULL << 32)}) {
        auto alias{output};
        alias.mount_id = alias_id;
        BOOST_CHECK(!Access::SameMount(source, alias));
        BOOST_CHECK(!Access::SameMount(alias, source));
    }
    for (const uint32_t missing : {STATX_TYPE, STATX_INO, STATX_MNT_ID}) {
        auto absent{output};
        absent.mask &= ~missing;
        BOOST_CHECK(!Access::SameMount(source, absent));
        BOOST_CHECK(!Access::SameMount(absent, output));
    }
    for (int error : {-1, EINVAL, ENOSYS, EPERM}) {
        auto failed{output};
        failed.status = error;
        BOOST_CHECK(!Access::SameMount(source, failed));
    }
    for (unsigned defect{0}; defect < 5; ++defect) {
        auto malformed{output};
        if (defect == 0) malformed.mount_id = 0;
        if (defect == 1) malformed.inode ^= 1;
        if (defect == 2) malformed.device_major ^= 1;
        if (defect == 3) malformed.device_minor ^= 1;
        if (defect == 4) malformed.mode = S_IFREG | 0600;
        BOOST_CHECK(!Access::SameMount(source, malformed));
    }
    // Fixed-width facts have no parser or narrowed mount ID conversion.
    source.mount_id = output.mount_id = std::numeric_limits<uint64_t>::max();
    BOOST_CHECK(Access::SameMount(source, output));
    --output.mount_id;
    BOOST_CHECK(!Access::SameMount(source, output));
#else
    BOOST_CHECK(!Access::SameMount(source, source));
    BOOST_TEST_MESSAGE("Linux statx mount capability unavailable at compilation; proof rejects even populated facts");
#endif
}

BOOST_AUTO_TEST_CASE(output_linux_native_mount_capability)
{
    OutputFixture fixture{m_path_root};
    struct Directory {
        int descriptor;
        explicit Directory(const fs::path& path) : descriptor(open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)) {}
        ~Directory() { if (descriptor != -1) close(descriptor); }
    } source{fixture.offline}, output{fixture.directory};
    BOOST_REQUIRE_NE(source.descriptor, -1);
    BOOST_REQUIRE_NE(output.descriptor, -1);
    using Access = LegacyExportOutputTestAccess;
    const auto source_facts{Access::Inspect(source.descriptor)};
    const auto output_facts{Access::Inspect(output.descriptor)};
#if defined(SYS_statx) && defined(STATX_MNT_ID)
    BOOST_REQUIRE(Access::SameMount(source_facts, output_facts));
    BOOST_CHECK(!Access::SameMount(Access::Inspect(-1), output_facts));
    WriteLegacyWalletExport(*fixture.source, SecureString{}, fixture.destination);
    BOOST_CHECK(fs::exists(fixture.destination));
    BOOST_TEST_MESSAGE("Linux retained handles: statx identity+mask+same-mounted-view proof succeeded; IDs are invocation-local");
#else
    BOOST_CHECK(!Access::SameMount(source_facts, output_facts));
    BOOST_CHECK_THROW(WriteLegacyWalletExport(*fixture.source, SecureString{}, fixture.destination), std::runtime_error);
    BOOST_CHECK(Entries(fixture.directory).empty());
    BOOST_TEST_MESSAGE("Linux statx mount capability unavailable at compilation; export fails before temporary creation");
#endif
}
#endif

BOOST_AUTO_TEST_CASE(failure_processing_and_output_checkpoints)
{
    SelectParams(ChainType::MAIN);
    const FailureMaterial material;
    OutputFixture fixture{m_path_root};
    material.Prepare(fixture);
    std::set<LegacyExportStage> stages;
    WriteLegacyWalletExport(*fixture.source, material.passphrase, fixture.destination, {}, [&](auto stage) { stages.insert(stage); });
    BOOST_REQUIRE(fs::remove(fixture.destination));
    BOOST_REQUIRE_EQUAL(stages.size(), 17U);
    ExportDiagnosticCapture capture{m_path_root / "capture", "checkpoints"};
    capture.Start();
    size_t processing_failures{0}, output_failures{0};
    for (unsigned kind{0}; kind < 3; ++kind) {
        for (const auto target : stages) {
            bool injected{false};
            try {
                WriteLegacyWalletExport(*fixture.source, material.passphrase, fixture.destination, {}, [&](auto stage) {
                    if (stage == target) { injected = true; InjectExportFailure(kind); }
                });
                BOOST_FAIL("Processing failure did not abort");
            } catch (const std::exception& error) {
                CaptureExportError(error);
                ++processing_failures;
            }
            BOOST_CHECK(injected);
            BOOST_CHECK(Entries(fixture.directory).empty());
        }
        for (const auto target : {LegacyOutputStage::CREATED, LegacyOutputStage::WRITE, LegacyOutputStage::FLUSH,
                                  LegacyOutputStage::SYNC, LegacyOutputStage::CLOSE, LegacyOutputStage::PUBLISH}) {
            bool injected{false};
            try {
                WriteLegacyWalletExport(*fixture.source, material.passphrase, fixture.destination, [&](auto stage) {
                    if (stage == target) { injected = true; InjectExportFailure(kind); }
                });
                BOOST_FAIL("Output failure did not abort");
            } catch (const std::exception& error) {
                CaptureExportError(error);
                ++output_failures;
            }
            BOOST_CHECK(injected);
            BOOST_CHECK(Entries(fixture.directory).empty());
        }
    }
    // The success path must also keep all diagnostics non-secret.
    const auto result{WriteLegacyWalletExport(*fixture.source, material.passphrase, fixture.destination)};
    BOOST_CHECK_EQUAL(result.state, "private");
    BOOST_CHECK_EQUAL(processing_failures, 51U);
    BOOST_CHECK_EQUAL(output_failures, 18U);
    capture.Check(material);
    fixture.source->VerifyUnchanged();
    BOOST_TEST_MESSAGE("failure-matrix processing=51 output=18; all handled failures removed owned output");
}

BOOST_AUTO_TEST_CASE(failure_each_stream_write_cleans_output)
{
    SelectParams(ChainType::MAIN);
    const FailureMaterial material;
    OutputFixture fixture{m_path_root};
    material.Prepare(fixture);
    size_t writes{0};
    WriteLegacyWalletExport(*fixture.source, material.passphrase, fixture.destination, [&](auto stage) { writes += stage == LegacyOutputStage::WRITE; });
    BOOST_REQUIRE(fs::remove(fixture.destination));
    BOOST_REQUIRE(writes > 100);
    ExportDiagnosticCapture capture{m_path_root / "capture", "stream-writes"};
    capture.Start();
    for (size_t target{1}; target <= writes; ++target) {
        size_t observed{0};
        bool injected{false};
        try {
            WriteLegacyWalletExport(*fixture.source, material.passphrase, fixture.destination, [&](auto stage) {
                if (stage == LegacyOutputStage::WRITE && ++observed == target) { injected = true; InjectExportFailure(target % 3); }
            });
            BOOST_FAIL("Stream write failure did not abort");
        } catch (const std::exception& error) { CaptureExportError(error); }
        BOOST_CHECK(injected);
        BOOST_CHECK_EQUAL(observed, target);
        BOOST_CHECK(Entries(fixture.directory).empty());
    }
    capture.Check(material);
    fixture.source->VerifyUnchanged();
    BOOST_TEST_MESSAGE("failure-stream-writes=" << writes << "; all positions including live xprv serialization unwound");
}

BOOST_AUTO_TEST_CASE(failure_real_processing_errors_and_diagnostics)
{
    SelectParams(ChainType::MAIN);
    const FailureMaterial material;
    OutputFixture fixture{m_path_root};
    material.Prepare(fixture);
    ExportDiagnosticCapture capture{m_path_root / "capture", "real-errors"};
    capture.Start();
    const auto reject = [&](const auto& action) {
        bool rejected{false};
        try { action(); } catch (const std::exception& error) { rejected = true; CaptureExportError(error); }
        BOOST_CHECK(rejected);
        BOOST_CHECK(Entries(fixture.directory).empty());
    };
    reject([&] { LegacyWalletSource invalid{m_path_root / fs::PathFromString(material.label + std::string(300, 'x'))}; });
    reject([&] { WriteLegacyWalletExport(*fixture.source, SecureString{"incorrect-passphrase"}, fixture.destination); });
    auto records{material.records};
    records.push_back({Encode(material.label), Encode(material.label)}); // Unknown family must not echo its name.
    fixture.source.reset();
    BOOST_REQUIRE(WriteBinaryFile(fixture.offline / "wallet.dat", RecordFile(records)));
    fixture.source = std::make_unique<LegacyWalletSource>(fixture.offline);
    reject([&] { WriteLegacyWalletExport(*fixture.source, material.passphrase, fixture.destination); });
    material.Prepare(fixture);
    reject([&] {
        WriteLegacyWalletExport(*fixture.source, material.passphrase, fixture.destination, {}, [&](auto stage) {
            if (stage == LegacyExportStage::RENDERED) {
                // In-place mutation: same opened object, content check must fail.
                auto bytes{ReadBinaryFile(fixture.offline / "wallet.dat").second};
                bytes.back() ^= 1;
                if (!WriteBinaryFile(fixture.offline / "wallet.dat", bytes)) throw std::runtime_error("Cannot mutate synthetic source");
            }
        });
    });
    fixture.source.reset();
    BOOST_REQUIRE(WriteBinaryFile(fixture.offline / "wallet.dat", std::string(4096, 'x')));
    reject([&] { LegacyWalletSource invalid{fixture.offline}; });
    capture.Check(material);
}

#ifndef WIN32
BOOST_AUTO_TEST_CASE(failure_abrupt_termination_keeps_private_temporary)
{
    SelectParams(ChainType::MAIN);
    const FailureMaterial material;
    OutputFixture fixture{m_path_root};
    material.Prepare(fixture);
    int ready[2];
    BOOST_REQUIRE_EQUAL(pipe(ready), 0);
    const pid_t child{fork()};
    BOOST_REQUIRE_NE(child, -1);
    if (child == 0) {
        close(ready[0]);
        try {
            WriteLegacyWalletExport(*fixture.source, material.passphrase, fixture.destination, {}, [&](auto stage) {
                if (stage != LegacyExportStage::RENDERED) return;
                const char signal{'r'};
                if (write(ready[1], &signal, 1) != 1) _exit(2);
                for (;;) pause(); // Parent kills only after this exact pre-commit checkpoint.
            });
        } catch (...) { _exit(3); }
        _exit(4);
    }
    close(ready[1]);
    struct ChildOwner {
        pid_t pid;
        ~ChildOwner() { if (pid > 0) { kill(pid, SIGKILL); while (waitpid(pid, nullptr, 0) == -1 && errno == EINTR) {} } }
    } owner{child};
    pollfd notification{ready[0], POLLIN, 0};
    const auto notified{poll(&notification, 1, 5000)};
    char signal{0};
    const auto received{notified == 1 ? read(ready[0], &signal, 1) : -1};
    close(ready[0]);
    BOOST_REQUIRE_EQUAL(notified, 1);
    BOOST_REQUIRE_EQUAL(received, 1);
    BOOST_REQUIRE_EQUAL(signal, 'r');
    BOOST_REQUIRE_EQUAL(kill(child, SIGKILL), 0);
    int status{0};
    BOOST_REQUIRE_EQUAL(waitpid(child, &status, 0), child);
    owner.pid = -1;
    BOOST_REQUIRE(WIFSIGNALED(status));
    BOOST_CHECK_EQUAL(WTERMSIG(status), SIGKILL);
    BOOST_REQUIRE(!fs::exists(fixture.destination));
    const auto entries{Entries(fixture.directory)};
    BOOST_REQUIRE_EQUAL(entries.size(), 1U);
    const auto temporary{*entries.begin()};
    struct stat st{};
    BOOST_REQUIRE_EQUAL(lstat(temporary.c_str(), &st), 0);
    BOOST_CHECK(S_ISREG(st.st_mode));
    BOOST_CHECK_EQUAL(st.st_mode & 0777, 0600);
    BOOST_CHECK_EQUAL(st.st_uid, geteuid());
    BOOST_CHECK_EQUAL(st.st_nlink, 1);
    const auto residue{ReadBinaryFile(temporary)};
    BOOST_REQUIRE(residue.first);
    BOOST_CHECK(residue.second.ends_with("# roots-export-complete: 1\n"));
    // A new invocation reads only its source. It neither consumes nor removes
    // the private residual, even when that residual has a complete envelope.
    WriteLegacyWalletExport(*fixture.source, material.passphrase, fixture.destination);
    BOOST_CHECK(ReadBinaryFile(temporary).second == residue.second);
    BOOST_CHECK_EQUAL(Entries(fixture.directory).size(), 2U);
    BOOST_REQUIRE(fs::remove(temporary)); // Explicit disposal of our synthetic residual only.
    fixture.source->VerifyUnchanged();
    BOOST_TEST_MESSAGE("failure-crash SIGKILL after rendered-before-publish; private0600 residual retained, never consumed by next export");
}
#endif

BOOST_AUTO_TEST_CASE(format_frozen_corpus_all_networks)
{
    const auto fixtures{Fixtures()};
    size_t exported{0};
    for (const auto chain : {ChainType::MAIN, ChainType::TESTNET, ChainType::TESTNET4, ChainType::SIGNET, ChainType::REGTEST}) {
        SelectParams(chain);
        const auto network{ChainTypeToString(chain)};
        for (const auto& fact : fixtures["fixtures"].getValues()) {
            const auto name{fact["name"].get_str()};
            OutputFixture fixture{m_path_root / fs::PathFromString(network + "-" + name)};
            const auto records{FixtureRecords(fact)};
            const auto [result, bytes]{ExportRecords(fixture, records)};
            const auto read{ReadExport(bytes, network)};
            const auto& wallet{read.metadata.at("wallet").front()};
            BOOST_CHECK_EQUAL(result.counts.records, records.size());
            BOOST_CHECK_EQUAL(result.counts.private_keys, fact["wifs"].size());
            BOOST_CHECK_EQUAL(result.counts.identified_seeds, fact["seeds"].size());
            BOOST_CHECK_EQUAL(wallet["counts"]["private_keys"].getInt<uint64_t>(), result.counts.private_keys);
            BOOST_CHECK_EQUAL(wallet["source_bytes"].getInt<uint64_t>(), fs::file_size(fixture.offline / "wallet.dat"));
            BOOST_CHECK_EQUAL(wallet["page_size"].getInt<int>(), 4096);
            BOOST_CHECK_EQUAL(wallet["byte_order"].get_str(), "little");
            std::set<CPubKey> payload_keys;
            std::set<std::string> payload_scripts;
            for (const auto& line : read.payloads) {
                const auto token{line.substr(0, line.find(' '))};
                const auto key{DecodeSecret(token)};
                if (key.IsValid()) payload_keys.insert(key.GetPubKey());
                else { BOOST_REQUIRE(IsHex(token)); payload_scripts.insert(token); }
            }
            BOOST_CHECK_EQUAL(payload_keys.size(), fact["wifs"].size());
            for (const auto& expected : fact["wifs"].getValues()) {
                BOOST_CHECK(payload_keys.contains(PublicKey(expected["pubkey"].get_str())));
                BOOST_CHECK(bytes.find(expected[chain == ChainType::MAIN ? "main" : "test"].get_str()) != std::string::npos);
            }
            size_t roots{0};
            if (read.metadata.contains("seed")) {
                for (const auto& seed : read.metadata.at("seed")) {
                    if (seed["xprv"].isNull()) continue;
                    const auto root{DecodeExtKey(seed["xprv"].get_str())};
                    BOOST_REQUIRE(root.key.IsValid());
                    ++roots;
                    if (seed["active"].get_bool()) BOOST_CHECK_EQUAL(read.active_root, seed["xprv"].get_str());
                }
            }
            BOOST_CHECK_EQUAL(roots, result.counts.roots);
            BOOST_CHECK_EQUAL(result.state, wallet["state"].get_str());
            const auto repeat{WriteLegacyWalletExport(*fixture.source, SecureString{"test"}, fixture.directory / "repeat.keys")};
            BOOST_CHECK_EQUAL(ReadBinaryFile(repeat.destination).second, bytes);
            if (const auto* archive{std::getenv("LEGACY_EXPORT_ORACLE_DIR")}) {
                const auto destination{fs::PathFromString(archive) / fs::PathFromString(network + "-" + name + ".keys")};
                BOOST_REQUIRE(fs::copy_file(result.destination, destination, fs::copy_options::none));
            }
            ++exported;
        }
    }
    BOOST_CHECK_EQUAL(exported, 50U);
    SelectParams(ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(format_actual_source_geometry)
{
    SelectParams(ChainType::MAIN);
    for (const uint32_t size : {512U,1024U,2048U,4096U,8192U,16384U,32768U,65536U}) {
        for (const bool big : {false, true}) {
            OutputFixture fixture{m_path_root / fs::PathFromString(util::ToString(size) + (big ? "big" : "little"))};
            const auto [result, bytes]{ExportRecords(fixture, {}, "wallet.keys", size, big)};
            const auto read{ReadExport(bytes, "main")};
            const auto& wallet{read.metadata.at("wallet").front()};
            BOOST_CHECK_EQUAL(wallet["page_size"].getInt<uint32_t>(), size);
            BOOST_CHECK_EQUAL(wallet["byte_order"].get_str(), big ? "big" : "little");
            BOOST_CHECK_EQUAL(wallet["source_bytes"].getInt<uint64_t>(), fs::file_size(fixture.offline / "wallet.dat"));
            BOOST_CHECK_EQUAL(result.state, "empty");
        }
    }
}

BOOST_AUTO_TEST_CASE(format_labels_associations_and_injection)
{
    SelectParams(ChainType::MAIN);
    const auto private_key{RecoveryTestKey(1)};
    const auto pubkey{private_key.GetPubKey()};
    const auto p2pkh{EncodeDestination(PKHash(pubkey))};
    const auto witness{WitnessV0KeyHash(pubkey)};
    auto bech32{EncodeDestination(witness)};
    std::transform(bech32.begin(), bech32.end(), bech32.begin(), [](unsigned char ch) { return ch >= 'a' && ch <= 'z' ? ch - 32 : ch; });
    const auto wrapped{EncodeDestination(ScriptHash(GetScriptForDestination(witness)))};
    std::string label;
    for (unsigned value{0}; value < 256; ++value) label.push_back(static_cast<char>(value));
    std::vector<LegacyWalletRecord> records{
        {Encode(std::string{"key"}, pubkey), Encode(private_key.GetPrivKey())},
        {Encode(std::string{"name"}, p2pkh), Encode(label)},
        {Encode(std::string{"name"}, wrapped), Encode(std::string{"second\n# roots-export-complete: 1"})},
        {Encode(std::string{"purpose"}, bech32), Encode(std::string{"receive"})},
        {Encode(std::string{"name"}, std::string{"bad\0\xff",5}), Encode(std::string{"raw\nlabel"})},
    };
    OutputFixture fixture{m_path_root};
    const auto [result, bytes]{ExportRecords(fixture, records)};
    const auto read{ReadExport(bytes, "main")};
    BOOST_REQUIRE_EQUAL(read.payloads.size(), 1U);
    const auto& payload{read.payloads.front()};
    BOOST_CHECK(payload.find("1970-01-01T00:00:01Z label=%00%01") != std::string::npos);
    BOOST_CHECK(payload.find("%25") != std::string::npos);
    BOOST_CHECK(payload.find("%ff") != std::string::npos);
    BOOST_CHECK_EQUAL(read.metadata.at("address").size(), 4U);
    size_t matched{0};
    for (const auto& address : read.metadata.at("address")) {
        matched += !address["pubkeys"].empty();
        if (address["label"].isNull()) BOOST_CHECK_EQUAL(address["purpose"]["value"].get_str(), "receive");
        if (address["label"].isObject() && address["label"]["encoding"].get_str() == "hex") {
            BOOST_CHECK_EQUAL(address["label"]["value"].get_str(), HexStr(MakeUCharSpan(label)));
        }
    }
    BOOST_CHECK_EQUAL(matched, 3U);
    BOOST_CHECK_EQUAL(result.counts.addresses, 4U);
    BOOST_CHECK(read.metadata.at("key").front()["created_at"].isNull());
    BOOST_CHECK_EQUAL(read.metadata.at("key").front()["classification"].get_str(), "unclassified");
    if (const auto* archive{std::getenv("LEGACY_EXPORT_ORACLE_DIR")}) {
        BOOST_REQUIRE(fs::copy_file(result.destination, fs::PathFromString(archive) / "main-all-label-bytes.keys", fs::copy_options::none));
    }
}

BOOST_AUTO_TEST_CASE(format_utf8_and_timestamp_provenance)
{
    SelectParams(ChainType::MAIN);
    const auto key{RecoveryTestKey(7)};
    const auto pubkey{key.GetPubKey()};
    CKeyMetadata metadata;
    metadata.nVersion = 12;
    metadata.nCreateTime = -9;
    metadata.hdKeypath = std::string{"opaque\xff\n",8};
    metadata.key_origin.fingerprint = {1,2,3,4};
    metadata.key_origin.path = {0xffffffffU, 0};
    metadata.has_key_origin = false;
    std::vector<LegacyWalletRecord> records{
        {Encode(std::string{"wkey"}, pubkey), Encode(int32_t{180100}, key.GetPrivKey(), int64_t{253402300799}, std::numeric_limits<int64_t>::min(), std::string{"\0\xff",2})},
        {Encode(std::string{"keymeta"}, pubkey), Encode(metadata)},
    };
    const std::vector<std::string> valid{"", std::string{"\0\n\r\t\"",5}, "\xc2\xa0", "\xf4\x8f\xbf\xbf"};
    const std::vector<std::string> invalid{"\x80", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82", "\xff"};
    for (size_t index{0}; index < valid.size() + invalid.size(); ++index) {
        const auto& value{index < valid.size() ? valid[index] : invalid[index - valid.size()]};
        records.push_back({Encode(std::string{"purpose"}, util::ToString(index)), Encode(value)});
    }
    OutputFixture fixture{m_path_root};
    const auto [result, bytes]{ExportRecords(fixture, records)};
    const auto read{ReadExport(bytes, "main")};
    const auto& exported{read.metadata.at("key").front()};
    BOOST_CHECK_EQUAL(exported["created_at"].get_str(), "253402300799");
    BOOST_CHECK_EQUAL(exported["metadata"]["created"].get_str(), "-9");
    BOOST_CHECK_EQUAL(exported["metadata"]["hd_path"]["encoding"].get_str(), "hex");
    BOOST_CHECK_EQUAL(exported["metadata"]["origin"]["fingerprint"].get_str(), "01020304");
    BOOST_CHECK_EQUAL(exported["metadata"]["origin"]["path"][0].getInt<uint32_t>(), 0xffffffffU);
    BOOST_CHECK(!exported["metadata"]["origin"]["has_origin"].get_bool());
    BOOST_CHECK_EQUAL(exported["wkey"]["expires"].get_str(), "-9223372036854775808");
    BOOST_CHECK(read.payloads.front().find("9999-12-31T23:59:59Z unclassified=1") != std::string::npos);
    for (const auto& address : read.metadata.at("address")) {
        const auto parsed{ToIntegral<size_t>(address["address"]["value"].get_str())};
        BOOST_REQUIRE(parsed);
        const auto index{*parsed};
        const auto& purpose{address["purpose"]};
        BOOST_CHECK_EQUAL(purpose["encoding"].get_str(), index < valid.size() ? "utf8" : "hex");
        BOOST_CHECK_EQUAL(purpose["value"].get_str(), index < valid.size() ? valid[index] : HexStr(MakeUCharSpan(invalid[index - valid.size()])));
    }
}

BOOST_AUTO_TEST_CASE(format_empty_redeem_watch_and_omitted_facts)
{
    SelectParams(ChainType::MAIN);
    const CScript empty;
    const CScript redeem{CScript{} << OP_RETURN << std::vector<unsigned char>{0x01}};
    const CScript watched{CScript{} << OP_TRUE};
    CKeyMetadata metadata;
    metadata.nVersion = 1;
    metadata.nCreateTime = -1;
    std::vector<LegacyWalletRecord> records{
        {Encode(std::string{"cscript"}, Hash160(empty)), Encode(empty)},
        {Encode(std::string{"watchmeta"}, empty), Encode(metadata)},
        {Encode(std::string{"cscript"}, Hash160(redeem)), Encode(redeem)},
        {Encode(std::string{"watchs"}, redeem), Encode(uint8_t{'0'})},
        {Encode(std::string{"watchs"}, watched), Encode(uint8_t{'1'})},
        {Encode(std::string{"flags"}), Encode(uint64_t{0x00000003fffffff8ULL})},
        {Encode(std::string{"version"}), Encode(int32_t{10300})},
        {Encode(std::string{"minversion"}), Encode(int32_t{130000})},
    };
    for (const auto family : {"tx","wtxvariant","acentry","acc","bestblock","bestblock_nomerkle","orderposnext","destdata","lockedutxo","setting","settings"}) {
        records.push_back({Encode(std::string{family}, uint8_t{1}), Encode(std::string{"omitted bytes must not be output"})});
    }
    OutputFixture fixture{m_path_root};
    const auto [result, bytes]{ExportRecords(fixture, records)};
    const auto read{ReadExport(bytes, "main")};
    BOOST_CHECK_EQUAL(result.state, "watch_only");
    BOOST_CHECK_EQUAL(result.counts.private_keys, 0U);
    BOOST_CHECK_EQUAL(result.counts.roots, 0U);
    BOOST_CHECK_EQUAL(result.counts.scripts, 3U);
    BOOST_CHECK_EQUAL(result.counts.omitted_records, 11U);
    BOOST_REQUIRE_EQUAL(read.payloads.size(), 1U);
    BOOST_CHECK(read.payloads.front().starts_with(HexStr(redeem) + " 1970-01-01T00:00:01Z script=1"));
    BOOST_CHECK(bytes.find("omitted bytes must not be output") == std::string::npos);
    bool found_empty{false};
    for (const auto& script : read.metadata.at("script")) {
        if (script["script_id"].get_str() != HexStr(CScriptID(empty))) continue;
        found_empty = true;
        BOOST_CHECK_EQUAL(script["script_hex"].get_str(), "");
        BOOST_CHECK(script["created_at"].isNull());
        BOOST_CHECK_EQUAL(script["metadata"]["created"].get_str(), "-1");
        BOOST_CHECK_EQUAL(script["kinds"].size(), 2U);
        BOOST_CHECK_EQUAL(script["kinds"][0].get_str(), "redeem");
        BOOST_CHECK_EQUAL(script["kinds"][1].get_str(), "watch_metadata");
        BOOST_CHECK(script["watch_enabled"].isNull());
    }
    BOOST_REQUIRE(found_empty);
    const auto& wallet{read.metadata.at("wallet").front()};
    BOOST_CHECK_EQUAL(wallet["flags"].get_str(), "00000003fffffff8");
    BOOST_CHECK_EQUAL(wallet["unknown_tolerable_flags"].get_str(), "00000000fffffff8");
    BOOST_CHECK_EQUAL(wallet["file_version"].getInt<int>(), 10300);
    BOOST_CHECK_EQUAL(read.metadata.at("omitted").size(), 11U);
    if (const auto* archive{std::getenv("LEGACY_EXPORT_ORACLE_DIR")}) {
        BOOST_REQUIRE(fs::copy_file(result.destination, fs::PathFromString(archive) / "main-empty-redeem.keys", fs::copy_options::none));
    }
}

BOOST_AUTO_TEST_CASE(format_known_empty_label_reserve_and_time_order)
{
    SelectParams(ChainType::MAIN);
    std::vector<LegacyWalletRecord> records;
    for (unsigned char value{1}; value <= 4; ++value) {
        const auto key{RecoveryTestKey(value)};
        const auto pubkey{key.GetPubKey()};
        records.push_back({Encode(std::string{"key"}, pubkey), Encode(key.GetPrivKey())});
        CKeyMetadata metadata;
        metadata.nVersion = 1;
        metadata.nCreateTime = value == 4 ? 10 : value == 3 ? 253402300800 : value == 2 ? -1 : 0;
        records.push_back({Encode(std::string{"keymeta"}, pubkey), Encode(metadata)});
        if (value == 4) records.push_back({Encode(std::string{"wkey"}, pubkey), Encode(int32_t{180100}, key.GetPrivKey(), int64_t{5}, int64_t{0}, std::string{})});
        if (value == 2) {
            records.push_back({Encode(std::string{"pool"}, int64_t{-1}), Encode(int32_t{130000}, int64_t{1231006505}, pubkey)});
            records.push_back({Encode(std::string{"name"}, EncodeDestination(PKHash(pubkey))), Encode(std::string{})});
        }
    }
    const auto public_only{RecoveryTestKey(9).GetPubKey()};
    records.push_back({Encode(std::string{"defaultkey"}), Encode(public_only)});
    OutputFixture fixture{m_path_root};
    const auto [result, bytes]{ExportRecords(fixture, records)};
    const auto read{ReadExport(bytes, "main")};
    BOOST_REQUIRE_EQUAL(read.payloads.size(), 4U);
    BOOST_CHECK(read.payloads.back().find("1970-01-01T00:00:05Z unclassified=1") != std::string::npos);
    std::vector<CPubKey> unknown;
    for (size_t i{0}; i < 3; ++i) {
        const auto& line{read.payloads[i]};
        BOOST_CHECK(line.find("1970-01-01T00:00:01Z") != std::string::npos);
        unknown.push_back(DecodeSecret(line.substr(0, line.find(' '))).GetPubKey());
    }
    BOOST_CHECK(std::is_sorted(unknown.begin(), unknown.end()));
    bool reserved_empty{false};
    for (const auto& line : read.payloads) {
        if (line.starts_with(EncodeSecret(RecoveryTestKey(2)) + " ")) {
            reserved_empty = true;
            BOOST_CHECK(line.find(" label= # addr=") != std::string::npos);
        }
    }
    BOOST_REQUIRE(reserved_empty);
    BOOST_CHECK_EQUAL(result.counts.private_keys, 4U);
    BOOST_CHECK_EQUAL(result.counts.public_key_identities, 5U);
    BOOST_CHECK_EQUAL(read.metadata.at("wallet").front()["default_pubkey"].get_str(), HexStr(public_only));
    BOOST_CHECK_EQUAL(read.metadata.at("key").size(), 5U);
}

BOOST_AUTO_TEST_CASE(format_public_only_unresolved_seed_and_disabled_watch)
{
    SelectParams(ChainType::MAIN);
    const auto pubkey{RecoveryTestKey(8).GetPubKey()};
    const auto missing_seed{RecoveryTestKey(9).GetPubKey().GetID()};
    CKeyMetadata metadata;
    metadata.nVersion = 10;
    metadata.nCreateTime = 0;
    metadata.hd_seed_id = missing_seed;
    metadata.hdKeypath = "m/0'/0'/1'";
    CHDChain chain;
    chain.seed_id = missing_seed;
    chain.nExternalChainCounter = 42;
    const CScript disabled{CScript{} << OP_RETURN};
    const auto frozen{Fixtures()};
    auto records{FixtureRecords(frozen["fixtures"][9])}; // Unused mkey slot.
    records.push_back({Encode(std::string{"keymeta"}, pubkey), Encode(metadata)});
    records.push_back({Encode(std::string{"hdchain"}), Encode(chain)});
    records.push_back({Encode(std::string{"watchs"}, disabled), Encode(uint8_t{'0'})});
    OutputFixture fixture{m_path_root};
    const auto [result, bytes]{ExportRecords(fixture, records)};
    const auto read{ReadExport(bytes, "main")};
    BOOST_CHECK_EQUAL(result.state, "metadata_only");
    BOOST_CHECK_EQUAL(result.counts.private_keys, 0U);
    BOOST_CHECK_EQUAL(result.counts.roots, 0U);
    BOOST_CHECK_EQUAL(result.counts.identified_seeds, 1U);
    BOOST_CHECK_EQUAL(result.counts.unverified_derivations, 1U);
    BOOST_CHECK(read.payloads.empty());
    BOOST_CHECK(read.active_root.empty());
    const auto& seed{read.metadata.at("seed").front()};
    BOOST_CHECK_EQUAL(seed["seed_id"].get_str(), HexStr(missing_seed));
    BOOST_CHECK(seed["pubkey"].isNull());
    BOOST_CHECK(seed["active"].get_bool());
    BOOST_CHECK(!seed["has_secret"].get_bool());
    BOOST_CHECK(seed["xprv"].isNull());
    BOOST_CHECK(!read.metadata.at("encryption").front()["verified_with_private_key"].get_bool());
    BOOST_CHECK(read.metadata.at("encryption").front()["used_for_pubkeys"].empty());
    BOOST_CHECK(!read.metadata.at("script").front()["watch_enabled"].get_bool());
    BOOST_CHECK_EQUAL(read.metadata.at("script").front()["kinds"][0].get_str(), "watch_only");
    BOOST_CHECK(!read.metadata.at("key").front()["has_private_key"].get_bool());
}

BOOST_AUTO_TEST_CASE(coverage_complete_independent_wif_sets)
{
    const auto fixtures{Fixtures()};
    for (const auto chain : {ChainType::MAIN, ChainType::REGTEST}) {
        SelectParams(chain);
        for (const auto& fixture : fixtures["fixtures"].getValues()) {
            const auto inventory{DecodeLegacyWalletRecords(FixtureRecords(fixture))};
            const auto secrets{RecoverLegacyWalletSecrets(inventory, SecureString{"test"})};
            const auto coverage{ValidateLegacyWalletCoverage(inventory, secrets)};
            BOOST_CHECK_EQUAL(coverage.keys.size(), inventory.keys.size());
            size_t private_count{0};
            for (const auto& [pubkey, key] : coverage.keys) private_count += !key.wif.empty();
            BOOST_CHECK_EQUAL(private_count, fixture["wifs"].size());
            for (const auto& expected : fixture["wifs"].getValues()) {
                const auto pubkey{PublicKey(expected["pubkey"].get_str())};
                const auto& key{coverage.keys.at(pubkey)};
                BOOST_CHECK((std::string_view{key.wif.data(), key.wif.size()} == expected[chain == ChainType::MAIN ? "main" : "test"].get_str()));
                BOOST_CHECK(DecodeSecret(key.wif) == secrets.keys.at(pubkey));
            }
            BOOST_CHECK_EQUAL(coverage.roots.seeds.size(), fixture["seeds"].size());
        }
    }
    SelectParams(ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(coverage_paths_reserves_and_original_facts)
{
    LegacyWalletInventory inventory;
    const auto seed{RecoveryTestKey(111)};
    MarkRecoverySeed(inventory, seed);
    inventory.hdchain.emplace();
    inventory.hdchain->seed_id = seed.GetPubKey().GetID();
    inventory.hdchain->nExternalChainCounter = 2; // Deliberately not inferred maximum.
    const auto receive{AddCoverageChild(inventory, seed, 0, 7)};
    const auto change{AddCoverageChild(inventory, seed, 1, 9)};
    inventory.pool.emplace(-2, LegacyPoolEntry{150000, 0, receive.GetPubKey(), false, true});
    inventory.pool.emplace(99, LegacyPoolEntry{180100, 7, receive.GetPubKey(), false, true, true, true});
    inventory.pool.emplace(100, LegacyPoolEntry{180100, 8, change.GetPubKey(), true, true});
    const auto raw_chain{Encode(*inventory.hdchain)};
    const auto raw_pool{inventory.pool};
    std::map<CPubKey, SerializeData> raw_metadata;
    for (const auto& [pubkey, entry] : inventory.keys) raw_metadata.emplace(pubkey, Encode(*entry.metadata));
    const auto secrets{RecoverLegacyWalletSecrets(inventory, SecureString{})};
    const auto coverage{ValidateLegacyWalletCoverage(inventory, secrets)};
    BOOST_CHECK(coverage.keys.at(seed.GetPubKey()).classification == LegacyKeyClassification::SEED);
    BOOST_CHECK(coverage.keys.at(receive.GetPubKey()).classification == LegacyKeyClassification::HD_RECEIVE);
    BOOST_CHECK(coverage.keys.at(change.GetPubKey()).classification == LegacyKeyClassification::HD_CHANGE);
    BOOST_CHECK(coverage.keys.at(change.GetPubKey()).derivation == LegacyKeyDerivation::VERIFIED);
    BOOST_CHECK((coverage.keys.at(receive.GetPubKey()).pool_indices == std::vector<int64_t>{-2, 99}));
    BOOST_CHECK(coverage.keys.at(change.GetPubKey()).pool_indices == std::vector<int64_t>{100});
    BOOST_CHECK_EQUAL(coverage.derived_maxima.at(seed.GetPubKey().GetID()).external.value(), 7U);
    BOOST_CHECK_EQUAL(coverage.derived_maxima.at(seed.GetPubKey().GetID()).internal.value(), 9U);
    BOOST_CHECK_EQUAL(coverage.derived_steps, 6U);
    BOOST_CHECK(Encode(*inventory.hdchain) == raw_chain);
    BOOST_CHECK(inventory.pool == raw_pool);
    for (const auto& [pubkey, entry] : inventory.keys) BOOST_CHECK(Encode(*entry.metadata) == raw_metadata.at(pubkey));
}

BOOST_AUTO_TEST_CASE(coverage_text_origin_and_fingerprint_conflicts)
{
    LegacyWalletInventory original;
    const auto seed{RecoveryTestKey(112)};
    MarkRecoverySeed(original, seed);
    const auto child{AddCoverageChild(original, seed, 0, 3)};
    const auto secrets{RecoverLegacyWalletSecrets(original, SecureString{})};
    auto conflict{original};
    auto& metadata{*conflict.keys.at(child.GetPubKey()).metadata};
    metadata.hdKeypath = "m/44'/0'/3'";
    RejectCoverage(conflict, secrets, "Conflicting legacy text and origin paths");
    metadata.key_origin.path = {44U | BIP32_HARDENED_FLAG, BIP32_HARDENED_FLAG, 3U | BIP32_HARDENED_FLAG};
    metadata.hdKeypath = "m/44'/1'/3'";
    RejectCoverage(conflict, secrets, "Conflicting legacy text and origin paths");
    metadata.hdKeypath = "m/44'/0'/3'";
    const auto unsupported{ValidateLegacyWalletCoverage(conflict, secrets)};
    BOOST_CHECK(unsupported.keys.at(child.GetPubKey()).derivation == LegacyKeyDerivation::UNRECOGNIZED);
    metadata.key_origin.fingerprint[0] ^= 1;
    RejectCoverage(conflict, secrets, "Legacy origin fingerprint mismatch");
    conflict = original;
    conflict.keys.at(child.GetPubKey()).metadata->key_origin.fingerprint[0] ^= 1;
    RejectCoverage(conflict, secrets, "Legacy origin fingerprint mismatch");
}

BOOST_AUTO_TEST_CASE(coverage_opaque_text_and_imported_origins)
{
    LegacyWalletInventory inventory;
    const auto seed{RecoveryTestKey(113)};
    MarkRecoverySeed(inventory, seed);
    const auto child{AddCoverageChild(inventory, seed, 1, 4)};
    auto& metadata{*inventory.keys.at(child.GetPubKey()).metadata};
    metadata.hdKeypath = std::string{"bad\0\xff", 5};
    const auto raw{Encode(metadata)};
    const auto secrets{RecoverLegacyWalletSecrets(inventory, SecureString{})};
    BOOST_CHECK(ValidateLegacyWalletCoverage(inventory, secrets).keys.at(child.GetPubKey()).derivation == LegacyKeyDerivation::VERIFIED);
    BOOST_CHECK(Encode(metadata) == raw);
    metadata.has_key_origin = false;
    BOOST_CHECK(ValidateLegacyWalletCoverage(inventory, secrets).keys.at(child.GetPubKey()).derivation == LegacyKeyDerivation::UNRECOGNIZED);
    metadata.has_key_origin = true;
    metadata.hd_seed_id.SetNull();
    BOOST_CHECK(ValidateLegacyWalletCoverage(inventory, secrets).keys.at(child.GetPubKey()).derivation == LegacyKeyDerivation::UNRECOGNIZED);
    metadata.hdKeypath.clear();
    BOOST_CHECK(ValidateLegacyWalletCoverage(inventory, secrets).keys.at(child.GetPubKey()).derivation == LegacyKeyDerivation::UNRECOGNIZED);
}

BOOST_AUTO_TEST_CASE(coverage_wrong_child_or_local_root_fails)
{
    LegacyWalletInventory original;
    const auto seed{RecoveryTestKey(114)};
    const auto other_seed{RecoveryTestKey(115)};
    MarkRecoverySeed(original, seed);
    MarkRecoverySeed(original, other_seed);
    const auto child{AddCoverageChild(original, seed, 0, 8)};
    const auto secrets{RecoverLegacyWalletSecrets(original, SecureString{})};
    for (const auto mutation : {0, 1, 2}) {
        auto inventory{original};
        auto& metadata{*inventory.keys.at(child.GetPubKey()).metadata};
        metadata.has_key_origin = false;
        switch (mutation) {
        case 0: metadata.hdKeypath = "m/0'/1'/8'"; break;
        case 1: metadata.hdKeypath = "m/0'/0'/9'"; break;
        case 2: metadata.hd_seed_id = other_seed.GetPubKey().GetID(); break;
        }
        RejectCoverage(inventory, secrets, "Legacy derived public key mismatch");
    }
}

BOOST_AUTO_TEST_CASE(coverage_unsplit_and_child_index_boundaries)
{
    LegacyWalletInventory inventory;
    const auto active{RecoveryTestKey(116)};
    const auto inactive{RecoveryTestKey(117)};
    MarkRecoverySeed(inventory, active, "m");
    MarkRecoverySeed(inventory, inactive);
    const auto receive{AddCoverageChild(inventory, active, 0, 0x7fffffff)};
    const auto inactive_change{AddCoverageChild(inventory, inactive, 1, 0)};
    inventory.hdchain.emplace();
    inventory.hdchain->nVersion = CHDChain::VERSION_HD_BASE;
    inventory.hdchain->seed_id = active.GetPubKey().GetID();
    auto secrets{RecoverLegacyWalletSecrets(inventory, SecureString{})};
    const auto coverage{ValidateLegacyWalletCoverage(inventory, secrets)};
    BOOST_CHECK(coverage.keys.at(receive.GetPubKey()).derivation == LegacyKeyDerivation::VERIFIED);
    BOOST_CHECK(coverage.keys.at(inactive_change.GetPubKey()).classification == LegacyKeyClassification::HD_CHANGE);
    auto& metadata{*inventory.keys.at(receive.GetPubKey()).metadata};
    metadata.has_key_origin = false;
    metadata.hdKeypath = "m/0'/0'/2147483648'";
    BOOST_CHECK(ValidateLegacyWalletCoverage(inventory, secrets).keys.at(receive.GetPubKey()).derivation == LegacyKeyDerivation::UNRECOGNIZED);
    metadata.hdKeypath = "m/0'/1'/2147483647'";
    RejectCoverage(inventory, secrets, "Legacy unsplit active chain has an internal child path");
}

BOOST_AUTO_TEST_CASE(coverage_public_and_compression_identities)
{
    LegacyWalletInventory inventory;
    const auto compressed{RecoveryTestKey(118)};
    const auto uncompressed{RecoveryTestKey(118, false)};
    const auto public_only{RecoveryTestKey(119)};
    inventory.keys[compressed.GetPubKey()].private_key = compressed;
    inventory.keys[uncompressed.GetPubKey()].private_key = uncompressed;
    inventory.keys[public_only.GetPubKey()];
    const auto secrets{RecoverLegacyWalletSecrets(inventory, SecureString{})};
    const auto coverage{ValidateLegacyWalletCoverage(inventory, secrets)};
    BOOST_REQUIRE_EQUAL(coverage.keys.size(), 3U);
    BOOST_CHECK(coverage.keys.at(public_only.GetPubKey()).wif.empty());
    BOOST_CHECK(coverage.keys.at(compressed.GetPubKey()).wif != coverage.keys.at(uncompressed.GetPubKey()).wif);
    for (const auto& [pubkey, key] : coverage.keys) {
        BOOST_CHECK(key.classification == LegacyKeyClassification::UNCLASSIFIED);
        BOOST_CHECK(key.derivation == LegacyKeyDerivation::NOT_APPLICABLE);
    }
}

BOOST_AUTO_TEST_CASE(roots_independent_fixture_oracles)
{
    const auto fixtures{Fixtures()};
    for (const auto& fixture : fixtures["fixtures"].getValues()) {
        const auto inventory{DecodeLegacyWalletRecords(FixtureRecords(fixture))};
        const auto secrets{RecoverLegacyWalletSecrets(inventory, SecureString{"test"})};
        const auto roots{DeriveLegacyWalletRoots(inventory, secrets)};
        BOOST_CHECK(roots.network == ChainType::MAIN);
        BOOST_CHECK_EQUAL(roots.seeds.size(), fixture["seeds"].size());
        BOOST_CHECK_EQUAL(secrets.keys.size(), fixture["keys"].size());
        for (const auto& expected : fixture["seeds"].getValues()) {
            const auto pubkey{PublicKey(expected["pubkey"].get_str())};
            const auto& seed{roots.seeds.at(pubkey.GetID())};
            CheckRootOracle(seed, expected, true);
            BOOST_CHECK(seed.evidence.contains("seed_marker"));
            BOOST_CHECK(seed.evidence.contains("metadata_reference"));
            BOOST_CHECK_EQUAL(seed.evidence.contains("active_hdchain"), seed.active);
            // The exact pure operation retained from v29.5 dumpwallet, also
            // checked against independent Python HMAC/BIP32 fixture answers.
            CExtKey donor_root;
            donor_root.SetSeed(secrets.keys.at(pubkey));
            BOOST_CHECK(*seed.root == donor_root);
            BOOST_CHECK((std::string_view{seed.extended_private_key.data(), seed.extended_private_key.size()} == EncodeExtKey(donor_root)));
        }
    }
}

BOOST_AUTO_TEST_CASE(roots_marker_and_reference_provenance)
{
    LegacyWalletInventory inventory;
    const auto active{RecoveryTestKey(61)};
    const auto inactive{RecoveryTestKey(62)};
    const auto reference_only{RecoveryTestKey(63)};
    const auto child{RecoveryTestKey(64)};
    MarkRecoverySeed(inventory, active, "m");
    inventory.keys.at(active.GetPubKey()).metadata->nVersion = CKeyMetadata::VERSION_WITH_HDDATA;
    MarkRecoverySeed(inventory, inactive);
    inventory.keys[reference_only.GetPubKey()].private_key = reference_only;
    inventory.keys[child.GetPubKey()].private_key = child;
    auto& metadata{inventory.keys[child.GetPubKey()].metadata.emplace()};
    metadata.hd_seed_id = reference_only.GetPubKey().GetID();
    metadata.hdKeypath = "unknown stored path";
    inventory.hdchain.emplace();
    inventory.hdchain->seed_id = active.GetPubKey().GetID();
    const auto original_metadata{Encode(metadata)};
    const auto secrets{RecoverLegacyWalletSecrets(inventory, SecureString{})};
    const auto roots{DeriveLegacyWalletRoots(inventory, secrets)};
    BOOST_REQUIRE_EQUAL(roots.seeds.size(), 3U);
    BOOST_CHECK(roots.seeds.at(active.GetPubKey().GetID()).active);
    BOOST_CHECK(!roots.seeds.at(inactive.GetPubKey().GetID()).active);
    const auto& referenced{roots.seeds.at(reference_only.GetPubKey().GetID())};
    BOOST_CHECK(!referenced.active);
    BOOST_CHECK(referenced.evidence == std::set<std::string>{"metadata_reference"});
    BOOST_CHECK(referenced.root.has_value());
    BOOST_CHECK_EQUAL(secrets.keys.size(), 4U);
    BOOST_CHECK(Encode(metadata) == original_metadata);
    // Metadata upgrade may add an origin to old "m" while retaining the marker.
    auto& upgraded{*inventory.keys.at(active.GetPubKey()).metadata};
    upgraded.nVersion = CKeyMetadata::VERSION_WITH_KEY_ORIGIN;
    upgraded.has_key_origin = true;
    upgraded.key_origin.fingerprint = *roots.seeds.at(active.GetPubKey().GetID()).fingerprint;
    BOOST_CHECK_NO_THROW(DeriveLegacyWalletRoots(inventory, secrets));
}

BOOST_AUTO_TEST_CASE(roots_missing_and_contradictory_evidence)
{
    const auto ordinary{RecoveryTestKey(71)};
    const auto missing{RecoveryTestKey(72)};
    LegacyWalletInventory inventory;
    inventory.keys[ordinary.GetPubKey()].private_key = ordinary;
    inventory.hdchain.emplace();
    inventory.hdchain->seed_id = missing.GetPubKey().GetID();
    const auto secrets{RecoverLegacyWalletSecrets(inventory, SecureString{})};
    RejectRoots(inventory, secrets, "Missing required legacy HD seed material");
    inventory.hdchain.reset();
    inventory.keys.at(ordinary.GetPubKey()).metadata.emplace();
    auto& metadata{*inventory.keys.at(ordinary.GetPubKey()).metadata};
    metadata.hd_seed_id = missing.GetPubKey().GetID();
    metadata.hdKeypath = "unrecognized";
    RejectRoots(inventory, secrets, "Missing required legacy HD seed material");
    for (const auto& marker : {"m", "s"}) {
        metadata.hdKeypath = marker;
        RejectRoots(inventory, secrets, "Contradictory legacy seed marker");
        metadata.hd_seed_id.SetNull();
        RejectRoots(inventory, secrets, "Contradictory legacy seed marker");
        metadata.hd_seed_id = missing.GetPubKey().GetID();
    }
    // A secret-free inventory preserves an unresolved relationship, not a root.
    metadata.hdKeypath = "unrecognized";
    inventory.keys.at(ordinary.GetPubKey()).private_key.reset();
    const auto unresolved{DeriveLegacyWalletRoots(inventory, {})};
    BOOST_REQUIRE_EQUAL(unresolved.seeds.size(), 1U);
    const auto& seed{unresolved.seeds.at(missing.GetPubKey().GetID())};
    BOOST_CHECK(!seed.pubkey && !seed.root && !seed.fingerprint);
    BOOST_CHECK(seed.wif.empty() && seed.extended_private_key.empty());
    inventory.keys[missing.GetPubKey()];
    const auto public_only{DeriveLegacyWalletRoots(inventory, {})};
    BOOST_CHECK(public_only.seeds.at(missing.GetPubKey().GetID()).pubkey == missing.GetPubKey());
    BOOST_CHECK(!public_only.seeds.at(missing.GetPubKey().GetID()).root);
}

BOOST_AUTO_TEST_CASE(roots_seed_only_and_zero_root_wallets)
{
    const auto key{RecoveryTestKey(81)};
    LegacyWalletInventory inventory;
    inventory.keys[key.GetPubKey()].private_key = key;
    const auto secrets{RecoverLegacyWalletSecrets(inventory, SecureString{})};
    BOOST_CHECK(DeriveLegacyWalletRoots(inventory, secrets).seeds.empty());
    inventory.hdchain.emplace();
    BOOST_CHECK(DeriveLegacyWalletRoots(inventory, secrets).seeds.empty());
    inventory.hdchain->seed_id = key.GetPubKey().GetID();
    const auto active{DeriveLegacyWalletRoots(inventory, secrets)};
    BOOST_REQUIRE_EQUAL(active.seeds.size(), 1U);
    BOOST_CHECK(active.seeds.at(key.GetPubKey().GetID()).evidence == std::set<std::string>{"active_hdchain"});
    BOOST_CHECK(active.seeds.at(key.GetPubKey().GetID()).root.has_value());
    inventory.hdchain.reset();
    MarkRecoverySeed(inventory, key);
    const auto inactive{DeriveLegacyWalletRoots(inventory, secrets)};
    BOOST_CHECK(!inactive.seeds.at(key.GetPubKey().GetID()).active);
    inventory.keys.at(key.GetPubKey()).private_key.reset();
    const auto public_only{DeriveLegacyWalletRoots(inventory, {})};
    BOOST_CHECK(!public_only.seeds.at(key.GetPubKey().GetID()).root);
    // An imported origin/fingerprint alone never invents a local seed.
    auto& metadata{*inventory.keys.at(key.GetPubKey()).metadata};
    metadata.hdKeypath.clear();
    metadata.hd_seed_id.SetNull();
    metadata.has_key_origin = true;
    metadata.key_origin.fingerprint = {1, 2, 3, 4};
    BOOST_CHECK(DeriveLegacyWalletRoots(inventory, {}).seeds.empty());
}

BOOST_AUTO_TEST_CASE(roots_fingerprints_are_not_identities)
{
    const auto compressed{RecoveryTestKey(91)};
    const auto uncompressed{RecoveryTestKey(91, false)};
    LegacyWalletInventory inventory;
    MarkRecoverySeed(inventory, compressed);
    MarkRecoverySeed(inventory, uncompressed, "m");
    const auto secrets{RecoverLegacyWalletSecrets(inventory, SecureString{})};
    const auto roots{DeriveLegacyWalletRoots(inventory, secrets)};
    BOOST_REQUIRE_EQUAL(roots.seeds.size(), 2U);
    const auto& first{roots.seeds.at(compressed.GetPubKey().GetID())};
    const auto& second{roots.seeds.at(uncompressed.GetPubKey().GetID())};
    BOOST_CHECK(first.fingerprint == second.fingerprint);
    BOOST_CHECK(*first.root == *second.root);
    BOOST_CHECK(first.pubkey != second.pubkey);
    BOOST_CHECK(first.wif != second.wif);
    BOOST_CHECK(DecodeSecret(first.wif) == compressed);
    BOOST_CHECK(DecodeSecret(second.wif) == uncompressed);
    BOOST_CHECK_EQUAL(secrets.keys.size(), 2U);
}

BOOST_AUTO_TEST_CASE(roots_network_encodings)
{
    const auto fixtures{Fixtures()};
    const auto& fixture{fixtures["fixtures"][4]};
    const auto inventory{DecodeLegacyWalletRecords(FixtureRecords(fixture))};
    const auto secrets{RecoverLegacyWalletSecrets(inventory, SecureString{})};
    for (const auto chain : {ChainType::MAIN, ChainType::TESTNET, ChainType::TESTNET4, ChainType::SIGNET, ChainType::REGTEST}) {
        SelectParams(chain);
        const auto roots{DeriveLegacyWalletRoots(inventory, secrets)};
        BOOST_CHECK(roots.network == chain);
        for (const auto& expected : fixture["seeds"].getValues()) {
            const auto pubkey{PublicKey(expected["pubkey"].get_str())};
            const auto& seed{roots.seeds.at(pubkey.GetID())};
            CheckRootOracle(seed, expected, chain == ChainType::MAIN);
            const std::string encoded{seed.extended_private_key.begin(), seed.extended_private_key.end()};
            BOOST_CHECK(DecodeExtKey(encoded) == *seed.root);
            BOOST_CHECK(DecodeSecret(seed.wif) == secrets.keys.at(pubkey));
        }
    }
    SelectParams(ChainType::MAIN);
}

BOOST_AUTO_TEST_CASE(roots_published_bip32_vector_four)
{
    // Published BIP32 vector4 has a valid 32-byte seed, unlike vector1's16.
    // Literal answer also appears in Core bip32_tests.cpp; no generated oracle.
    const auto scalar{ParseHex("3ddd5602285899a946114506157c7997e5444528f3003f6134712147db19b678")};
    CKey seed;
    seed.Set(scalar.begin(), scalar.end(), true);
    BOOST_REQUIRE(seed.IsValid());
    LegacyWalletInventory inventory;
    MarkRecoverySeed(inventory, seed);
    const auto secrets{RecoverLegacyWalletSecrets(inventory, SecureString{})};
    const auto roots{DeriveLegacyWalletRoots(inventory, secrets)};
    const auto& root{roots.seeds.at(seed.GetPubKey().GetID())};
    BOOST_CHECK((std::string_view{root.extended_private_key.data(), root.extended_private_key.size()} ==
                 "xprv9s21ZrQH143K48vGoLGRPxgo2JNkJ3J3fqkirQC2zVdk5Dgd5w14S7fRDyHH4dWNHUgkvsvNDCkvAwcSHNAQwhwgNMgZhLtQC63zxwhQmRv"));
    auto incomplete{secrets};
    incomplete.keys.clear();
    RejectRoots(inventory, incomplete, "Incomplete or inconsistent legacy recovered key set");
    auto extra{secrets};
    const auto unrelated{RecoveryTestKey(99)};
    extra.keys.emplace(unrelated.GetPubKey(), unrelated);
    RejectRoots(inventory, extra, "Incomplete or inconsistent legacy recovered key set");
}

BOOST_AUTO_TEST_CASE(recovery_independent_ciphertext_vectors)
{
    const auto fixtures{Fixtures()};
    for (const auto& fixture : fixtures["fixtures"].getValues()) {
        const auto inventory{DecodeLegacyWalletRecords(FixtureRecords(fixture))};
        const auto recovered{RecoverLegacyWalletSecrets(inventory, SecureString{"test"})};
        BOOST_CHECK_EQUAL(recovered.keys.size(), fixture["keys"].size());
        for (const auto& expected : fixture["keys"].getValues()) {
            const auto pubkey{PublicKey(expected["pubkey"].get_str())};
            BOOST_REQUIRE(recovered.keys.contains(pubkey));
            const auto bytes{ParseHex(expected["scalar"].get_str())};
            const auto& key{recovered.keys.at(pubkey)};
            BOOST_CHECK(std::equal(UCharCast(key.begin()), UCharCast(key.end()), bytes.begin(), bytes.end()));
            BOOST_CHECK(recovered.keys.at(pubkey).VerifyPubKey(pubkey));
        }
        size_t used{0};
        for (const auto& [id, keys] : recovered.master_key_usage) used += keys.size();
        BOOST_CHECK_EQUAL(used, fixture["encrypted"].get_bool() ? fixture["keys"].size() : 0U);
    }
}

BOOST_AUTO_TEST_CASE(recovery_exact_passphrase_bytes)
{
    const std::vector<SecureString> passphrases{
        SecureString{}, SecureString{" \t test \r "}, SecureString{"\xc3\xa9\xe2\x82\xac"},
        SecureString{"ab\0cd", 5}, SecureString(4096, 'p')};
    for (const auto& passphrase : passphrases) {
        LegacyWalletInventory inventory;
        const auto key{RecoveryTestKey(17)};
        AddRecoverySlot(inventory, 91, passphrase, 42, {key});
        const auto recovered{RecoverLegacyWalletSecrets(inventory, passphrase)};
        BOOST_CHECK(recovered.keys.at(key.GetPubKey()) == key);
        BOOST_CHECK(!inventory.keys.at(key.GetPubKey()).private_key);
        for (const auto& wrong : {SecureString{"wrong"}, SecureString{"ab"}, SecureString{"test"}}) {
            RejectRecovery(inventory, wrong, "Legacy decryption failed: incorrect passphrase or corrupt encrypted key material");
        }
        if (!passphrase.empty()) {
            RejectRecovery(inventory, SecureString{}, "Legacy decryption failed: incorrect passphrase or corrupt encrypted key material");
        }
    }
    RejectRecovery({}, SecureString(4097, 'p'), "Legacy passphrase exceeds supported limit");
}

BOOST_AUTO_TEST_CASE(recovery_multiple_master_candidates)
{
    const SecureString passphrase{"test"};
    LegacyWalletInventory inventory;
    const auto first{RecoveryTestKey(21)};
    const auto second{RecoveryTestKey(22, false)};
    const auto plain{RecoveryTestKey(23)};
    AddRecoverySlot(inventory, 0, SecureString{"other"}, 1, {});
    AddRecoverySlot(inventory, 300, passphrase, 2, {first});
    AddRecoverySlot(inventory, 9, passphrase, 3, {second});
    AddRecoverySlot(inventory, 400, passphrase, 4, {});
    inventory.keys[plain.GetPubKey()].private_key = plain;
    const auto recovered{RecoverLegacyWalletSecrets(inventory, passphrase)};
    BOOST_CHECK_EQUAL(recovered.keys.size(), 3U);
    BOOST_CHECK(recovered.keys.at(first.GetPubKey()) == first);
    BOOST_CHECK(recovered.keys.at(second.GetPubKey()) == second);
    BOOST_CHECK(recovered.keys.at(plain.GetPubKey()) == plain);
    BOOST_CHECK(recovered.master_key_usage.at(0).empty());
    BOOST_CHECK(recovered.master_key_usage.at(400).empty());
    BOOST_CHECK(recovered.master_key_usage.at(9) == std::set{second.GetPubKey()});
    BOOST_CHECK(recovered.master_key_usage.at(300) == std::set{first.GetPubKey()});
    inventory.keys[first.GetPubKey()].private_key = first;
    BOOST_CHECK_NO_THROW(RecoverLegacyWalletSecrets(inventory, passphrase));
    inventory.keys[first.GetPubKey()].private_key = plain;
    RejectRecovery(inventory, passphrase, "Invalid legacy private/public key pair");
}

BOOST_AUTO_TEST_CASE(recovery_no_partial_success)
{
    const SecureString passphrase{"test"};
    LegacyWalletInventory original;
    const auto first{RecoveryTestKey(31)};
    const auto second{RecoveryTestKey(32)};
    AddRecoverySlot(original, 1, passphrase, 5, {first, second});
    for (const auto mutation : {0, 1, 2, 3}) {
        auto inventory{original};
        switch (mutation) {
        case 0: inventory.keys.at(second.GetPubKey()).ciphertext->back() ^= 1; break;
        case 1: inventory.keys.at(second.GetPubKey()).ciphertext = inventory.keys.at(first.GetPubKey()).ciphertext; break;
        case 2: AddRecoverySlot(inventory, 2, SecureString{"other"}, 6, {second}); break;
        case 3: inventory.master_keys.at(1).vchCryptedKey.back() ^= 1; break;
        }
        RejectRecovery(inventory, passphrase, "Legacy decryption failed: incorrect passphrase or corrupt encrypted key material");
        for (const auto& [pubkey, entry] : inventory.keys) BOOST_CHECK(!entry.private_key);
    }
    auto missing{original};
    missing.master_keys.clear();
    RejectRecovery(missing, passphrase, "Missing legacy master key records");
}

BOOST_AUTO_TEST_CASE(recovery_parameter_validation)
{
    const SecureString passphrase{"test"};
    LegacyWalletInventory original;
    AddRecoverySlot(original, 1, passphrase, 7, {RecoveryTestKey(41)});
    for (const auto mutation : {0, 1, 2, 3, 4, 5, 6}) {
        auto inventory{original};
        auto& master{inventory.master_keys.at(1)};
        switch (mutation) {
        case 0: master.nDerivationMethod = 1; break;
        case 1: master.nDeriveIterations = 0; break;
        case 2: master.nDeriveIterations = 10'000'001; break;
        case 3: master.vchSalt.resize(7); break;
        case 4: master.vchCryptedKey.resize(47); break;
        case 5: master.vchOtherDerivationParameters.push_back(0); break;
        case 6: inventory.keys.begin()->second.ciphertext->resize(47); break;
        }
        RejectRecovery(inventory, passphrase,
                       mutation == 0 ? "Unsupported legacy master key derivation method" :
                       mutation == 6 ? "Invalid legacy encrypted private key length" :
                                       "Unsupported or corrupt legacy master key parameters");
    }
    auto slots{original};
    for (uint32_t id{2}; id <= 33; ++id) slots.master_keys.emplace(id, original.master_keys.at(1));
    RejectRecovery(slots, passphrase, "Legacy master key count exceeds supported limit");
}

BOOST_AUTO_TEST_CASE(recovery_rejects_padding_only_candidates)
{
    const SecureString passphrase{"test"};
    const auto key{RecoveryTestKey(44)};
    LegacyWalletInventory original;
    AddRecoverySlot(original, 1, passphrase, 7, {key});
    // AES padding success with the wrong master length is not authority.
    auto wrong_length{original};
    auto& master{wrong_length.master_keys.at(1)};
    CCrypter crypter;
    BOOST_REQUIRE(crypter.SetKeyFromPassphrase(passphrase, master.vchSalt, master.nDeriveIterations, 0));
    BOOST_REQUIRE(crypter.Encrypt(CKeyingMaterial(33, 7), master.vchCryptedKey));
    BOOST_REQUIRE_EQUAL(master.vchCryptedKey.size(), 48U);
    RejectRecovery(wrong_length, passphrase, "Legacy decryption failed: incorrect passphrase or corrupt encrypted key material");
    for (const auto size : {31U, 32U, 33U}) {
        auto invalid{original};
        // Zero32 is an invalid scalar; adjacent lengths are invalid secrets.
        BOOST_REQUIRE(EncryptSecret(CKeyingMaterial(32, 7), CKeyingMaterial(size, 0), key.GetPubKey().GetHash(),
                                    *invalid.keys.at(key.GetPubKey()).ciphertext));
        RejectRecovery(invalid, passphrase, size == 31 ? "Invalid legacy encrypted private key length" :
                                                       "Legacy decryption failed: incorrect passphrase or corrupt encrypted key material");
    }
}

BOOST_AUTO_TEST_CASE(recovery_kdf_work_bound, *boost::unit_test::timeout(60))
{
    LegacyWalletInventory inventory;
    AddRecoverySlot(inventory, 1, SecureString{"test"}, 8, {RecoveryTestKey(51)});
    auto master{inventory.master_keys.at(1)};
    master.nDeriveIterations = 10'000'000;
    inventory.master_keys = {{1, master}, {2, master}, {3, master}};
    RejectRecovery(inventory, SecureString{"wrong"}, "Legacy KDF work exceeds supported limit");
    // No encrypted private keys means no KDF attempts or password claim.
    inventory.keys.clear();
    const auto recovered{RecoverLegacyWalletSecrets(inventory, SecureString{"wrong"})};
    BOOST_CHECK(recovered.keys.empty());
    for (const auto& [id, keys] : recovered.master_key_usage) BOOST_CHECK(keys.empty());
}

BOOST_AUTO_TEST_CASE(output_private_at_creation_and_commit)
{
    OutputFixture fixture{m_path_root};
#ifndef WIN32
    OutputUmask mask{0};
#endif
    bool inspected{false};
    LegacyExportOutput output{*fixture.source, fixture.destination, [&](LegacyOutputStage stage) {
        if (stage != LegacyOutputStage::CREATED) return;
        const auto entries{Entries(fixture.directory)};
        BOOST_REQUIRE_EQUAL(entries.size(), 1U);
        const auto temporary{*entries.begin()};
        BOOST_CHECK(temporary != fixture.destination);
#ifndef WIN32
        struct stat st{};
        BOOST_REQUIRE_EQUAL(lstat(temporary.c_str(), &st), 0);
        BOOST_CHECK_EQUAL(st.st_mode & 0777, 0600);
        BOOST_CHECK_EQUAL(st.st_uid, geteuid());
        BOOST_CHECK_EQUAL(st.st_nlink, 1);
#else
        CheckPrivateOutputTestAcl(temporary);
#endif
        BOOST_CHECK(!fs::exists(fixture.destination));
        inspected = true;
    }};
    BOOST_REQUIRE(inspected);
    output.Write(std::string_view{}); // null data() must not reach fwrite.
    output.Write("validated ");
    output.Write("synthetic payload\n");
    BOOST_CHECK(!fs::exists(fixture.destination));
    output.Publish();
    BOOST_CHECK_EQUAL(ReadBinaryFile(fixture.destination).second, "validated synthetic payload\n");
    BOOST_CHECK(Entries(fixture.directory) == std::set<fs::path>{fixture.destination});
    BOOST_CHECK_THROW(output.Write("late"), std::runtime_error);
    BOOST_CHECK_THROW(output.Publish(), std::runtime_error);
    BOOST_CHECK_EQUAL(ReadBinaryFile(fixture.destination).second, "validated synthetic payload\n");
}

#ifdef WIN32
BOOST_AUTO_TEST_CASE(output_windows_physical_volume_root_gate)
{
    OutputFixture fixture{m_path_root};
    const auto root{fs::absolute(fixture.directory).root_path()};
    const HANDLE volume{CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    BOOST_REQUIRE(volume != INVALID_HANDLE_VALUE);
    const bool physical_root{LegacyExportOutputTestAccess::IsPhysicalVolumeRoot(volume)};
    BOOST_REQUIRE(CloseHandle(volume));
    // Native qualification uses an actual volume-root drive for its scratch.
    BOOST_CHECK(physical_root);
    const HANDLE subdirectory{CreateFileW(fixture.directory.c_str(), FILE_READ_ATTRIBUTES,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                          nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    BOOST_REQUIRE(subdirectory != INVALID_HANDLE_VALUE);
    const bool directory_is_root{LegacyExportOutputTestAccess::IsPhysicalVolumeRoot(subdirectory)};
    BOOST_REQUIRE(CloseHandle(subdirectory));
    BOOST_CHECK(!directory_is_root);
    BOOST_CHECK(!LegacyExportOutputTestAccess::IsPhysicalVolumeRoot(INVALID_HANDLE_VALUE));
    BOOST_CHECK(Entries(fixture.directory).empty());
}

BOOST_AUTO_TEST_CASE(output_windows_rejects_existing_paths_and_aliases)
{
    OutputFixture fixture{m_path_root};
    const auto existing{fixture.directory / "existing"};
    BOOST_REQUIRE(WriteBinaryFile(existing, "existing user bytes"));
    const auto subdirectory{fixture.directory / "directory"};
    fs::create_directory(subdirectory);
    const auto link{fixture.directory / "hardlink"};
    fs::create_hard_link(fixture.offline / "wallet.dat", link);
    const auto symlink{fixture.directory / "symlink"};
    fs::create_symlink(existing, symlink);
    const auto dangling{fixture.directory / "dangling"};
    fs::create_symlink(fixture.directory / "missing", dangling);
    const auto entries{Entries(fixture.directory)};
    for (const auto& path : {existing, subdirectory, link, symlink, dangling, fixture.offline / "wallet.dat"}) {
        BOOST_CHECK_THROW((LegacyExportOutput{*fixture.source, path}), std::runtime_error);
        BOOST_CHECK(Entries(fixture.directory) == entries);
        BOOST_CHECK_EQUAL(ReadBinaryFile(existing).second, "existing user bytes");
        fixture.source->VerifyUnchanged();
    }
}

BOOST_AUTO_TEST_CASE(output_windows_rejects_devices_streams_and_source_tree)
{
    OutputFixture fixture{m_path_root};
    fs::create_directory(fixture.offline / "sub");
    const auto alias{m_path_root / "parent-link"};
    fs::create_directory_symlink(fixture.directory, alias);
    const auto source_alias{m_path_root / "source-link"};
    fs::create_directory_symlink(fixture.offline, source_alias);
    const std::vector<fs::path> rejected{
        {}, "-", fixture.directory / "NUL", fixture.directory / "con.txt", fixture.directory / "COM1",
        fixture.directory / "LPT9.txt", fixture.directory / "export:stream", fixture.directory / "trailing.",
        fixture.directory / "trailing ", fixture.directory / "missing" / "out",
        fixture.offline / "new.keys", fixture.offline / "sub" / "new.keys", alias / "new.keys",
        source_alias / "sub" / "new.keys",
    };
    for (const auto& path : rejected) {
        BOOST_CHECK_THROW((LegacyExportOutput{*fixture.source, path}), std::runtime_error);
        BOOST_CHECK(Entries(fixture.directory).empty());
    }
}

BOOST_AUTO_TEST_CASE(output_windows_publication_races_do_not_replace)
{
    for (int kind{0}; kind < 5; ++kind) {
        OutputFixture fixture{m_path_root / fs::PathFromString(util::ToString(kind))};
        {
            LegacyExportOutput output{*fixture.source, fixture.destination, [&](LegacyOutputStage stage) {
                if (stage != LegacyOutputStage::PUBLISH) return;
                switch (kind) {
                case 0: BOOST_REQUIRE(WriteBinaryFile(fixture.destination, "race winner")); break;
                case 1: fs::create_directory(fixture.destination); break;
                case 2: fs::create_hard_link(fixture.offline / "wallet.dat", fixture.destination); break;
                case 3: fs::create_symlink(fixture.offline / "wallet.dat", fixture.destination); break;
                case 4: fs::create_symlink(fixture.directory / "missing", fixture.destination); break;
                }
            }};
            output.Write("validated synthetic output");
            BOOST_CHECK_THROW(output.Publish(), std::runtime_error);
            BOOST_CHECK_THROW(output.Publish(), std::runtime_error);
        }
        BOOST_CHECK(Entries(fixture.directory) == std::set<fs::path>{fixture.destination});
        if (kind == 0) BOOST_CHECK_EQUAL(ReadBinaryFile(fixture.destination).second, "race winner");
        fixture.source->VerifyUnchanged();
    }
}

BOOST_AUTO_TEST_CASE(output_windows_sharing_pins_parent_and_owned_temp)
{
    OutputFixture fixture{m_path_root};
    bool inspected{false};
    {
        LegacyExportOutput output{*fixture.source, fixture.destination, [&](LegacyOutputStage stage) {
            if (stage != LegacyOutputStage::CREATED) return;
            const auto temporary{*Entries(fixture.directory).begin()};
            CheckPrivateOutputTestAcl(temporary);
            BOOST_CHECK(!MoveFileW(fixture.directory.c_str(), (m_path_root / "moved").c_str()));
            BOOST_CHECK(!MoveFileW(temporary.c_str(), (fixture.directory / "moved-temp").c_str()));
            BOOST_CHECK(!DeleteFileW(temporary.c_str()));
            inspected = true;
        }};
        output.Write("validated synthetic output");
        output.Publish();
    }
    BOOST_REQUIRE(inspected);
    BOOST_CHECK_EQUAL(ReadBinaryFile(fixture.destination).second, "validated synthetic output");
    CheckPrivateOutputTestAcl(fixture.destination);
    const auto moved{m_path_root / "moved"};
    BOOST_REQUIRE(MoveFileW(fixture.directory.c_str(), moved.c_str()));
    BOOST_REQUIRE(MoveFileW(moved.c_str(), fixture.directory.c_str()));
}

BOOST_AUTO_TEST_CASE(output_windows_stage_failures_clean_owned_temp)
{
    for (const auto failure : {LegacyOutputStage::CREATED, LegacyOutputStage::WRITE, LegacyOutputStage::FLUSH,
                               LegacyOutputStage::SYNC, LegacyOutputStage::CLOSE, LegacyOutputStage::PUBLISH}) {
        OutputFixture fixture{m_path_root / fs::PathFromString(util::ToString(static_cast<int>(failure)))};
        bool injected{false};
        BOOST_CHECK_THROW(([&] {
            LegacyExportOutput output{*fixture.source, fixture.destination, [&](LegacyOutputStage stage) {
                if (stage == failure) { injected = true; throw std::runtime_error("injected output failure"); }
            }};
            output.Write("validated synthetic output");
            output.Publish();
        }()), std::runtime_error);
        BOOST_REQUIRE(injected);
        BOOST_CHECK(Entries(fixture.directory).empty());
        fixture.source->VerifyUnchanged();
    }
}

BOOST_AUTO_TEST_CASE(output_windows_parent_acl_and_inherited_reader)
{
    OutputFixture fixture{m_path_root};
    OutputTestSecurity private_acl;
    DWORD sid_size{SECURITY_MAX_SID_SIZE};
    std::vector<DWORD> world_storage((sid_size + sizeof(DWORD) - 1) / sizeof(DWORD));
    PSID world{world_storage.data()};
    BOOST_REQUIRE(CreateWellKnownSid(WinWorldSid, nullptr, world, &sid_size));
    for (const bool writable : {true, false}) {
        std::vector<DWORD> storage((sizeof(ACL) + 2 * sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(private_acl.Owner()) + sid_size + sizeof(DWORD) - 1) / sizeof(DWORD));
        auto* acl{reinterpret_cast<PACL>(storage.data())};
        BOOST_REQUIRE(InitializeAcl(acl, static_cast<DWORD>(storage.size() * sizeof(DWORD)), ACL_REVISION));
        BOOST_REQUIRE(AddAccessAllowedAce(acl, ACL_REVISION, FILE_ALL_ACCESS, private_acl.Owner()));
        BOOST_REQUIRE(AddAccessAllowedAceEx(acl, ACL_REVISION, OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE,
                                            writable ? FILE_ALL_ACCESS : FILE_GENERIC_READ, world));
        BOOST_REQUIRE_EQUAL(SetNamedSecurityInfoW(const_cast<wchar_t*>(fixture.directory.c_str()), SE_FILE_OBJECT,
                                                  DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                                  nullptr, nullptr, acl, nullptr), ERROR_SUCCESS);
        if (writable) {
            BOOST_CHECK_THROW((LegacyExportOutput{*fixture.source, fixture.destination}), std::runtime_error);
            BOOST_CHECK(Entries(fixture.directory).empty());
        } else {
            LegacyExportOutput output{*fixture.source, fixture.destination, [&](LegacyOutputStage stage) {
                if (stage == LegacyOutputStage::CREATED) CheckPrivateOutputTestAcl(*Entries(fixture.directory).begin());
            }};
            output.Write(std::string_view{});
            output.Write("validated synthetic output");
            output.Publish();
        }
    }
    CheckPrivateOutputTestAcl(fixture.destination);
    BOOST_CHECK_EQUAL(ReadBinaryFile(fixture.destination).second, "validated synthetic output");
}
#else
BOOST_AUTO_TEST_CASE(output_rejects_existing_destinations)
{
    OutputFixture fixture{m_path_root};
    const auto original{ReadBinaryFile(fixture.offline / "wallet.dat").second};
    const auto existing{fixture.directory / "existing"};
    BOOST_REQUIRE(WriteBinaryFile(existing, "existing user bytes"));
    const auto directory{fixture.directory / "directory"};
    fs::create_directory(directory);
    const auto symlink{fixture.directory / "symlink"};
    fs::create_symlink(existing, symlink);
    const auto dangling{fixture.directory / "dangling"};
    fs::create_symlink(fixture.directory / "absent", dangling);
    const auto hardlink{fixture.directory / "hardlink"};
    fs::create_hard_link(fixture.offline / "wallet.dat", hardlink);
    const auto fifo{fixture.directory / "fifo"};
    BOOST_REQUIRE_EQUAL(mkfifo(fifo.c_str(), 0600), 0);
    const auto entries{Entries(fixture.directory)};
    for (const auto& path : {existing, directory, symlink, dangling, hardlink, fifo, fs::path{"/dev/null"}}) {
        BOOST_CHECK_THROW((LegacyExportOutput{*fixture.source, path}), std::runtime_error);
        BOOST_CHECK(Entries(fixture.directory) == entries);
        BOOST_CHECK_EQUAL(ReadBinaryFile(existing).second, "existing user bytes");
        BOOST_CHECK(ReadBinaryFile(hardlink).second == original);
    }
    BOOST_CHECK(ReadBinaryFile(fixture.offline / "wallet.dat").second == original);
}

BOOST_AUTO_TEST_CASE(output_rejects_paths_and_source_tree)
{
    OutputFixture fixture{m_path_root};
    fs::create_directories(fixture.offline / "sub" / "deep");
    fs::create_directory_symlink(fixture.offline, m_path_root / "source-alias");
    fs::create_directory_symlink(fixture.directory, m_path_root / "parent-link");
    const std::vector<fs::path> rejected{
        {}, "-", fixture.directory / ".", fixture.directory / "..", fixture.directory / "",
        fixture.directory / "missing" / "output", fixture.offline / "new.keys",
        fixture.offline / "sub" / "deep" / "new.keys", m_path_root / "source-alias" / "sub" / "new.keys",
        m_path_root / "parent-link" / "output", fs::PathFromString(std::string{"bad\0name", 8}),
    };
    const auto entries{Entries(fixture.directory)};
    for (const auto& path : rejected) {
        BOOST_CHECK_THROW((LegacyExportOutput{*fixture.source, path}), std::runtime_error);
        BOOST_CHECK(Entries(fixture.directory) == entries);
    }
    BOOST_CHECK(Entries(fixture.offline / "sub" / "deep").empty());
}

BOOST_AUTO_TEST_CASE(output_rejects_untrusted_and_readonly_parents)
{
    OutputFixture fixture{m_path_root};
    RestorePermissions restore{fixture.directory};
    for (const auto permissions : {fs::perms::all, fs::perms::owner_all | fs::perms::group_write,
                                   fs::perms::owner_read | fs::perms::owner_exec}) {
        fs::permissions(fixture.directory, permissions);
        BOOST_CHECK_THROW((LegacyExportOutput{*fixture.source, fixture.destination}), std::runtime_error);
        BOOST_CHECK(Entries(fixture.directory).empty());
    }
}

BOOST_AUTO_TEST_CASE(output_destination_races_never_replace)
{
    for (int kind{0}; kind < 5; ++kind) {
        OutputFixture fixture{m_path_root / fs::PathFromString(util::ToString(kind))};
        const auto original{ReadBinaryFile(fixture.offline / "wallet.dat").second};
        {
            LegacyExportOutput output{*fixture.source, fixture.destination, [&](LegacyOutputStage stage) {
                if (stage != LegacyOutputStage::PUBLISH) return;
                switch (kind) {
                case 0: BOOST_REQUIRE(WriteBinaryFile(fixture.destination, "race winner")); break;
                case 1: fs::create_symlink(fixture.offline / "wallet.dat", fixture.destination); break;
                case 2: fs::create_symlink(fixture.directory / "absent", fixture.destination); break;
                case 3: fs::create_directory(fixture.destination); break;
                case 4: fs::create_hard_link(fixture.offline / "wallet.dat", fixture.destination); break;
                }
            }};
            output.Write("validated synthetic output");
            BOOST_CHECK_THROW(output.Publish(), std::runtime_error);
            BOOST_CHECK_THROW(output.Publish(), std::runtime_error);
        }
        BOOST_CHECK(Entries(fixture.directory) == std::set<fs::path>{fixture.destination});
        if (kind == 0) BOOST_CHECK_EQUAL(ReadBinaryFile(fixture.destination).second, "race winner");
        BOOST_CHECK(ReadBinaryFile(fixture.offline / "wallet.dat").second == original);
    }
}

BOOST_AUTO_TEST_CASE(output_parent_identity_race_is_anchored)
{
    OutputFixture fixture{m_path_root};
    const auto held{m_path_root / "held"};
    {
        LegacyExportOutput output{*fixture.source, fixture.destination, [&](LegacyOutputStage stage) {
            if (stage != LegacyOutputStage::CREATED) return;
            fs::rename(fixture.directory, held);
            fs::create_directory(fixture.directory);
            BOOST_REQUIRE(WriteBinaryFile(fixture.destination, "replacement directory file"));
        }};
        output.Write("still anchored to held directory");
        BOOST_CHECK_THROW(output.Publish(), std::runtime_error);
    }
    BOOST_CHECK(Entries(held).empty());
    BOOST_CHECK(Entries(fixture.directory) == std::set<fs::path>{fixture.destination});
    BOOST_CHECK_EQUAL(ReadBinaryFile(fixture.destination).second, "replacement directory file");
}

BOOST_AUTO_TEST_CASE(output_reparenting_through_ancestor_alias_fails)
{
    OutputFixture fixture{m_path_root};
    const auto outside{m_path_root / "outside"};
    fs::create_directory(outside);
    fs::rename(fixture.directory, outside / "exports");
    const auto alias{m_path_root / "alias"};
    fs::create_directory_symlink(outside, alias);
    const auto destination{alias / "exports" / "wallet.keys"};
    bool reparented{false};
    {
        LegacyExportOutput output{*fixture.source, destination, [&](LegacyOutputStage stage) {
            if (stage != LegacyOutputStage::PUBLISH) return;
            fs::rename(outside / "exports", fixture.offline / "exports");
            fs::remove(alias);
            fs::create_directory_symlink(fixture.offline, alias);
            reparented = true;
        }};
        output.Write("validated synthetic output");
        // The final parent still has the pinned identity at the same path,
        // but its actual ancestry now lies inside the offline source tree.
        BOOST_CHECK_THROW(output.Publish(), std::runtime_error);
    }
    BOOST_REQUIRE(reparented);
    BOOST_CHECK(Entries(fixture.offline / "exports").empty());
    BOOST_CHECK(!fs::exists(destination));
}

BOOST_AUTO_TEST_CASE(output_temporary_identity_and_link_races)
{
    for (int kind{0}; kind < 3; ++kind) {
        OutputFixture fixture{m_path_root / fs::PathFromString(util::ToString(kind))};
        fs::path temporary;
        const auto held{fixture.directory / "owned-moved"};
        const auto alias{fixture.directory / "extra-link"};
        {
            LegacyExportOutput output{*fixture.source, fixture.destination, [&](LegacyOutputStage stage) {
                if (stage != LegacyOutputStage::CREATED) return;
                temporary = *Entries(fixture.directory).begin();
                if (kind == 2) {
                    fs::create_hard_link(temporary, alias);
                } else {
                    fs::rename(temporary, held);
                    if (kind == 0) BOOST_REQUIRE(WriteBinaryFile(temporary, "unowned replacement"));
                    else fs::create_symlink(fixture.offline / "wallet.dat", temporary);
                }
            }};
            BOOST_CHECK_THROW(output.Write("must not publish"), std::runtime_error);
            BOOST_CHECK_THROW(output.Publish(), std::runtime_error);
        }
        BOOST_CHECK(!fs::exists(fixture.destination));
        if (kind == 0) BOOST_CHECK_EQUAL(ReadBinaryFile(temporary).second, "unowned replacement");
        if (kind == 1) BOOST_CHECK(fs::is_symlink(temporary));
        if (kind == 2) BOOST_CHECK(!fs::exists(temporary));
        // Deliberate same-user namespace tampering is outside parent trust;
        // never chase moved files or delete replacements to hide that attack.
        if (kind == 2) fs::remove(alias); else fs::remove(held);
    }
}

BOOST_AUTO_TEST_CASE(output_stage_failures_clean_owned_temporary)
{
    for (const auto failure : {LegacyOutputStage::CREATED, LegacyOutputStage::WRITE, LegacyOutputStage::FLUSH,
                               LegacyOutputStage::SYNC, LegacyOutputStage::CLOSE, LegacyOutputStage::PUBLISH}) {
        OutputFixture fixture{m_path_root / fs::PathFromString(util::ToString(static_cast<int>(failure)))};
        bool injected{false};
        BOOST_CHECK_THROW(([&] {
            LegacyExportOutput output{*fixture.source, fixture.destination, [&](LegacyOutputStage stage) {
                if (stage == failure) { injected = true; throw std::runtime_error("injected output failure"); }
            }};
            output.Write("validated synthetic output");
            output.Publish();
        }()), std::runtime_error);
        BOOST_REQUIRE(injected);
        BOOST_CHECK(Entries(fixture.directory).empty());
        fixture.source->VerifyUnchanged();
    }
    OutputFixture fixture{m_path_root / "changed-source"};
    {
        LegacyExportOutput output{*fixture.source, fixture.destination};
        output.Write("validated synthetic output");
        BOOST_REQUIRE(WriteBinaryFile(fixture.offline / "wallet.dat", LegacyFile()));
        BOOST_CHECK_THROW(output.Publish(), std::runtime_error);
    }
    BOOST_CHECK(Entries(fixture.directory).empty());
}

BOOST_AUTO_TEST_CASE(output_actual_file_size_limit_failure)
{
    OutputFixture fixture{m_path_root};
    // A real native short-write/ENOSPC-like failure, isolated from the runner.
    const pid_t child{fork()};
    BOOST_REQUIRE_NE(child, -1);
    if (child == 0) {
        std::signal(SIGXFSZ, SIG_IGN);
        struct rlimit limit{1, 1};
        if (setrlimit(RLIMIT_FSIZE, &limit) != 0) _exit(2);
        int result{3};
        try {
            LegacyExportOutput output{*fixture.source, fixture.destination};
            output.Write("larger than the file size limit");
        } catch (const std::runtime_error&) { result = Entries(fixture.directory).empty() ? 0 : 4; }
        _exit(result);
    }
    int status{0};
    BOOST_REQUIRE_EQUAL(waitpid(child, &status, 0), child);
    BOOST_REQUIRE(WIFEXITED(status));
    BOOST_CHECK_EQUAL(WEXITSTATUS(status), 0);
    BOOST_CHECK(Entries(fixture.directory).empty());
}

#ifdef __linux__
BOOST_AUTO_TEST_CASE(output_native_sync_and_close_errors)
{
    for (const auto failure : {LegacyOutputStage::SYNC, LegacyOutputStage::CLOSE}) {
        OutputFixture fixture{m_path_root / fs::PathFromString(util::ToString(static_cast<int>(failure)))};
        bool injected{false};
        {
            LegacyExportOutput output{*fixture.source, fixture.destination, [&](LegacyOutputStage stage) {
                if (stage != failure) return;
                const auto temporary{*Entries(fixture.directory).begin()};
                int descriptor{-1};
                for (const auto& entry : fs::directory_iterator("/proc/self/fd")) {
                    std::error_code error;
                    if (fs::read_symlink(entry.path(), error) == temporary && !error) {
                        const auto parsed{ToIntegral<int>(fs::PathToString(entry.path().filename()))};
                        BOOST_REQUIRE(parsed);
                        descriptor = *parsed;
                        break;
                    }
                }
                BOOST_REQUIRE_NE(descriptor, -1);
                // No exception from the hook: the real fsync/fclose must
                // observe EBADF and take its checked production error path.
                BOOST_REQUIRE_EQUAL(close(descriptor), 0);
                injected = true;
            }};
            output.Write("validated synthetic output");
            BOOST_CHECK_EXCEPTION(output.Publish(), std::runtime_error, [&](const auto& error) {
                return std::string_view{error.what()} == (failure == LegacyOutputStage::SYNC ?
                           "Cannot synchronize legacy output" : "Cannot close legacy output");
            });
            BOOST_CHECK_THROW(output.Publish(), std::runtime_error);
        }
        BOOST_REQUIRE(injected);
        BOOST_CHECK(Entries(fixture.directory).empty());
    }
}
#endif
#endif

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

BOOST_AUTO_TEST_CASE(passphrase_native_enter_finalization)
{
    using namespace WalletTool::detail;
    const std::vector<std::string> inputs{
        "", "ordinary", "\r", "literal trailing CR\r", std::string{"nul\0utf8\xc3\xa9\r", 11},
        std::string(4096, 'x'), std::string(4095, 'x') + '\r', std::string(4094, 'x') + "\xc3\xa9",
    };
    for (const auto& input : inputs) {
        PassphraseLine line;
        for (const char byte : input) BOOST_REQUIRE(!line.Push(byte));
        // Exactly the production Windows Enter finalization: no synthetic
        // CR or LF was appended to the accumulated data.
        const auto result{line.Finish(PassphraseEnd::NATIVE_ENTER)};
        BOOST_CHECK(result == SecureString(input.begin(), input.end()));
    }
}

BOOST_AUTO_TEST_CASE(passphrase_byte_line_and_eof_finalization)
{
    using namespace WalletTool::detail;
    const std::vector<std::pair<std::string, std::string>> inputs{
        {"", ""}, {"ordinary", "ordinary"}, {"ordinary\r", "ordinary"}, {"data\r\r", "data\r"},
        {std::string(4096, 'x'), std::string(4096, 'x')},
        {std::string(4096, 'x') + '\r', std::string(4096, 'x')},
        {std::string(4095, 'x') + "\r\r", std::string(4095, 'x') + '\r'},
    };
    for (const auto& [input, expected] : inputs) {
        PassphraseLine line;
        for (const char byte : input) BOOST_REQUIRE(!line.Push(byte));
        BOOST_REQUIRE(line.Push('\n'));
        const auto result{line.Finish(PassphraseEnd::BYTE_LINE)};
        BOOST_CHECK(result == SecureString(expected.begin(), expected.end()));
    }
    for (const auto& input : {std::string{"ordinary"}, std::string{"data\r"}, std::string(4095, 'x') + '\r'}) {
        PassphraseLine line;
        for (const char byte : input) BOOST_REQUIRE(!line.Push(byte));
        const auto result{line.Finish(PassphraseEnd::END_OF_FILE)};
        BOOST_CHECK(result == SecureString(input.begin(), input.end()));
    }
    PassphraseLine missing;
    BOOST_CHECK_EXCEPTION(missing.Finish(PassphraseEnd::END_OF_FILE), std::runtime_error, [](const auto& error) {
        return std::string_view{error.what()} == "Missing legacy passphrase input";
    });
}

BOOST_AUTO_TEST_CASE(passphrase_finalization_limits)
{
    using namespace WalletTool::detail;
    for (const auto end : {PassphraseEnd::NATIVE_ENTER, PassphraseEnd::END_OF_FILE}) {
        PassphraseLine line;
        for (size_t i{0}; i < 4096; ++i) BOOST_REQUIRE(!line.Push('x'));
        BOOST_REQUIRE(!line.Push('\r')); // Permitted only as a potential CRLF separator.
        BOOST_CHECK_EXCEPTION(line.Finish(end), std::runtime_error, [](const auto& error) {
            return std::string_view{error.what()} == "Legacy passphrase exceeds 4096-byte limit";
        });
    }
    PassphraseLine oversize;
    for (size_t i{0}; i < 4096; ++i) BOOST_REQUIRE(!oversize.Push('x'));
    BOOST_CHECK_THROW(oversize.Push('x'), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
