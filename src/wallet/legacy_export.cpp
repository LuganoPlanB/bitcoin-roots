// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/legacy_export.h>

#include <hash.h>
#include <streams.h>
#include <wallet/migrate.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <vector>

#ifndef WIN32
#include <sys/stat.h>
#include <unistd.h>
#else
#include <io.h>
#include <windows.h>
#endif

namespace wallet {
namespace {
constexpr uint64_t MAX_SOURCE_BYTES{512U * 1024 * 1024};

struct FileIdentity {
    uint64_t device;
    uint64_t file;
    bool operator==(const FileIdentity&) const = default;
};

FileIdentity Identity(FILE* file)
{
#ifndef WIN32
    struct stat st;
    if (fstat(fileno(file), &st) != 0 || !S_ISREG(st.st_mode)) {
        throw std::runtime_error("Legacy export requires a readable regular source file");
    }
    return {uint64_t(st.st_dev), uint64_t(st.st_ino)};
#else
    BY_HANDLE_FILE_INFORMATION info;
    const auto handle{reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file)))};
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        throw std::runtime_error("Legacy export requires a readable regular source file");
    }
    return {info.dwVolumeSerialNumber, (uint64_t{info.nFileIndexHigh} << 32) | info.nFileIndexLow};
#endif
}

// Observe the name independently of any previously opened handle. Following a
// final-component symlink would hide a change even when it targets that handle.
FileIdentity NamedIdentity(const fs::path& path)
{
#ifndef WIN32
    struct stat st;
    if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        throw std::runtime_error("Legacy export requires a non-symlink regular source file");
    }
    return {uint64_t(st.st_dev), uint64_t(st.st_ino)};
#else
    const HANDLE handle{CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (handle == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("Cannot inspect legacy export source identity");
    }
    BY_HANDLE_FILE_INFORMATION info;
    const bool regular{GetFileType(handle) == FILE_TYPE_DISK && GetFileInformationByHandle(handle, &info) &&
                       !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))};
    CloseHandle(handle);
    if (!regular) throw std::runtime_error("Legacy export requires a non-symlink regular source file");
    return {info.dwVolumeSerialNumber, (uint64_t{info.nFileIndexHigh} << 32) | info.nFileIndexLow};
#endif
}

FILE* OpenSource(const fs::path& path)
{
    // Reject static FIFOs/devices/symlinks before attempting a stdio open.
    NamedIdentity(path);
    return fsbridge::fopen(path, "rb");
}

const fs::path& CheckedSourcePath(const fs::path& path, const FileIdentity& expected)
{
    if (NamedIdentity(path) != expected) {
        throw std::runtime_error("Legacy export source changed; use an offline clean backup");
    }
    return path;
}

struct SourceState {
    FileIdentity identity;
    uint64_t size;
    fs::file_time_type mtime;
    uint256 hash;
    bool operator==(const SourceState&) const = default;
};

// Keep the initial handle alive to pin the source identity until publication.
// Reopen for each content check, avoiding stale stdio read-ahead buffers.
SourceState InspectSource(const fs::path& path, AutoFile& file, FILE* handle)
{
    const auto identity{Identity(handle)};
    CheckedSourcePath(path, identity);
    const auto size{fs::file_size(path)};
    const auto mtime{fs::last_write_time(path)};
    if (size < 4096 || size > MAX_SOURCE_BYTES) {
        throw std::runtime_error("Legacy export source size is outside supported limits");
    }
    CHash256 hasher;
    SerializeData buffer(65536);
    file.seek(0, SEEK_SET);
    for (uint64_t remaining{size}; remaining > 0;) {
        const auto count{static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()))};
        file.read(std::span{buffer}.first(count));
        hasher.Write(MakeUCharSpan(std::span{buffer}.first(count)));
        remaining -= count;
    }
    uint256 hash;
    hasher.Finalize(hash);
    if (file.size() != static_cast<int64_t>(size) || fs::file_size(path) != size ||
        fs::last_write_time(path) != mtime || Identity(handle) != identity || NamedIdentity(path) != identity) {
        throw std::runtime_error("Legacy export source changed; use an offline clean backup");
    }
    return {identity, size, mtime, hash};
}
} // namespace

struct LegacyWalletSource::Impl {
    const fs::path path;
    FILE* const handle;
    AutoFile file;
    const SourceState state;
    std::vector<LegacyWalletRecord> records;

    explicit Impl(const fs::path& source)
        : path(source), handle(OpenSource(path)), file(handle), state(Inspect()) {}

    SourceState Inspect()
    {
        if (file.IsNull()) throw std::runtime_error("Cannot open legacy export source read-only");
        return InspectSource(path, file, handle);
    }
};

LegacyWalletSource::LegacyWalletSource(const fs::path& path)
{
    // Check the type before any open, so FIFOs/devices cannot block a read.
    const auto type{fs::status(path).type()};
    const fs::path source{fs::absolute(type == fs::file_type::directory ? path / "wallet.dat" : path)};
    m_impl = std::make_unique<Impl>(source);
    // No SQLite connection, Berkeley DB environment or operational wallet.
    if (!IsBDBFile(CheckedSourcePath(source, m_impl->state.identity))) {
        throw std::runtime_error("Legacy export requires a supported BDB wallet");
    }
    BerkeleyRODatabase database(CheckedSourcePath(source, m_impl->state.identity), /*open=*/true, BerkeleyROLimits{});
    BerkeleyROCursor cursor(database);
    while (true) {
        DataStream key, value;
        const auto status{cursor.Next(key, value)};
        if (status == DatabaseCursor::Status::DONE) break;
        if (status != DatabaseCursor::Status::MORE) throw std::runtime_error("Cannot read legacy export records");
        m_impl->records.push_back({SerializeData{key.begin(), key.end()}, SerializeData{value.begin(), value.end()}});
    }
    VerifyUnchanged();
}

LegacyWalletSource::~LegacyWalletSource() = default;

std::span<const LegacyWalletRecord> LegacyWalletSource::Records() const
{
    return m_impl->records;
}

void LegacyWalletSource::VerifyUnchanged() const
{
    FILE* handle{OpenSource(m_impl->path)};
    AutoFile file(handle);
    if (file.IsNull() || InspectSource(m_impl->path, file, handle) != m_impl->state) {
        throw std::runtime_error("Legacy export source changed; use an offline clean backup");
    }
}

namespace {
constexpr size_t MAX_RECORDS{1'000'000};
constexpr size_t MAX_PRIVATE_IDENTITIES{100'000};
constexpr size_t MAX_DATABASE_KEY_BYTES{1024 * 1024};
constexpr size_t MAX_PAIR_BYTES{16 * 1024 * 1024};
constexpr size_t MAX_DECODED_BYTES{256 * 1024 * 1024};
constexpr size_t MAX_SCRIPT_BYTES{8 * 1024 * 1024};
constexpr size_t MAX_TEXT_BYTES{65536};
constexpr size_t MAX_DER_BYTES{4096};

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename T>
T Read(SpanReader& stream)
{
    T value;
    stream >> value;
    return value;
}

size_t BoundedSize(SpanReader& stream, size_t limit)
{
    const auto size{ReadCompactSize(stream)};
    Require(size <= limit && size <= stream.size(), "Legacy record length exceeds supported limits or available bytes");
    return static_cast<size_t>(size);
}

void SkipBytes(SpanReader& stream, size_t limit)
{
    const auto size{BoundedSize(stream, limit)};
    stream.ignore(size);
}

template <typename T>
T ReadBytes(SpanReader& stream, size_t limit)
{
    const auto size{BoundedSize(stream, limit)};
    T value;
    value.resize(size);
    stream.read(std::as_writable_bytes(std::span{value.data(), value.size()}));
    return value;
}

bool ReadBool(SpanReader& stream)
{
    const auto value{Read<uint8_t>(stream)};
    Require(value <= 1, "Invalid legacy record boolean");
    return value != 0;
}

CPubKey ReadPubKey(SpanReader& stream)
{
    auto check{stream};
    const auto size{BoundedSize(check, CPubKey::SIZE)};
    Require(size == 33 || size == 65, "Invalid legacy public key length");
    const auto pubkey{Read<CPubKey>(stream)};
    Require(pubkey.IsFullyValid(), "Invalid legacy public key");
    return pubkey;
}

void CheckChecksum(SpanReader& stream, const uint256& expected)
{
    if (stream.empty()) return;
    Require(stream.size() == 32, "Invalid legacy checksum or trailing fields");
    Require(Read<uint256>(stream) == expected, "Invalid legacy record checksum");
}

// Reuse current CKeyMetadata serialization, identical to v29.5 walletdb.h
// (16ca532c8212d691daed14940da0f71f84f7b3fc) and v0.18.1 walletdb.h.
// v0.13/v0.15's v1/v10 wire fields match. This allocation-free preflight bounds
// variable fields and rejects future versions/non-boolean flags before reuse.
CKeyMetadata ReadMetadata(SpanReader& stream)
{
    auto check{stream};
    const auto version{Read<int32_t>(check)};
    Require(version == 1 || version == 10 || version == 12, "Unsupported legacy key metadata version");
    check.ignore(8);
    if (version >= 10) {
        SkipBytes(check, MAX_TEXT_BYTES);
        check.ignore(20);
    }
    if (version >= 12) {
        check.ignore(4);
        const auto count{ReadCompactSize(check)};
        Require(count <= 255 && count <= check.size() / 4, "Legacy origin path exceeds supported limits or available bytes");
        check.ignore(count * 4);
        ReadBool(check);
    }
    Require(check.empty(), "Trailing legacy metadata fields");
    return Read<CKeyMetadata>(stream);
}

// Adapted CWalletKey::SerializationOp, Core v0.18.1 wallet.h:599,
// fa27a0760792b251585f2a70eccdd547f915b7e4. Preserve disk version/private-key/
// creation/expiry/comment order and the original comment bound. Only the pure
// schema survives; wallet loading and write/lifecycle operations are absent.
struct HistoricalWalletKey {
    CPrivKey private_key;
    LegacyWKeyMetadata metadata;
    SERIALIZE_METHODS(HistoricalWalletKey, obj)
    {
        READWRITE(obj.metadata.stream_version, obj.private_key, obj.metadata.created,
                  obj.metadata.expires, LIMITED_STRING(obj.metadata.comment, 65536));
    }
};

HistoricalWalletKey ReadWalletKey(SpanReader& stream)
{
    auto check{stream};
    check.ignore(4);
    SkipBytes(check, MAX_DER_BYTES);
    check.ignore(16);
    SkipBytes(check, MAX_TEXT_BYTES);
    Require(check.empty() || check.size() == 32, "Invalid legacy wkey trailing fields");
    return Read<HistoricalWalletKey>(stream);
}

CMasterKey ReadMasterKey(SpanReader& stream)
{
    // Current CMasterKey preserves v0.4.0 crypter.h's field order (c7eb151a...)
    // and v29.5's LoadEncryptionKey schema. Bound vectors before deserialization.
    auto check{stream};
    SkipBytes(check, 48);
    SkipBytes(check, 8);
    check.ignore(8);
    SkipBytes(check, 0);
    Require(check.empty(), "Trailing legacy master key fields");
    auto master{Read<CMasterKey>(stream)};
    Require(master.vchCryptedKey.size() == 48 && master.vchSalt.size() == 8 &&
                master.nDerivationMethod == 0 && master.nDeriveIterations >= 1 &&
                master.nDeriveIterations <= 10'000'000,
            "Unsupported legacy master key parameters");
    return master;
}

// Adapted CKeyPool::Unserialize, Core v29.5 scriptpubkeyman.h:117,
// 16ca532c8212d691daed14940da0f71f84f7b3fc; v0.13/v0.15/v0.18 wallet.h
// establish the absent/internal/pre-split suffixes. Unlike broad EOF fallback,
// accept only zero/one/two complete canonical bool bytes and retain presence.
LegacyPoolEntry ReadPool(SpanReader& stream)
{
    LegacyPoolEntry pool;
    pool.stream_version = Read<int32_t>(stream);
    pool.time = Read<int64_t>(stream);
    pool.pubkey = ReadPubKey(stream);
    Require(stream.size() <= 2, "Trailing legacy pool fields");
    if (!stream.empty()) {
        pool.internal_present = true;
        pool.internal = ReadBool(stream);
    }
    if (!stream.empty()) {
        pool.pre_split_present = true;
        pool.pre_split = ReadBool(stream);
    }
    return pool;
}

CScript ReadScript(SpanReader& stream)
{
    auto script{ReadBytes<CScript>(stream, MAX_SCRIPT_BYTES)};
    CScript::const_iterator position{script.begin()};
    opcodetype opcode;
    while (position != script.end()) {
        Require(script.GetOp(position, opcode), "Invalid legacy script push-data boundaries");
    }
    return script;
}

template <typename T>
void AssignConsistent(std::optional<T>& target, T value)
{
    Require(!target || *target == value, "Conflicting legacy semantic records");
    target = std::move(value);
}

template <typename T>
void AssignSerialized(std::optional<T>& target, T value)
{
    // CHDChain's existing equality compares only seed_id, not stored counters.
    // Compare complete serializer bytes for all metadata conflict detection.
    if (target) {
        DataStream before, after;
        before << *target;
        after << value;
        Require(std::ranges::equal(before, after), "Conflicting legacy metadata records");
    }
    target = std::move(value);
}

LegacyExportScript& ScriptEntry(LegacyWalletInventory& inventory, const CScript& script)
{
    auto [entry, inserted]{inventory.scripts.try_emplace(CScriptID(script))};
    Require(inserted || entry->second.script == script, "Conflicting legacy script identities");
    if (inserted) entry->second.script = script;
    return entry->second;
}

constexpr std::string_view OMITTED_FAMILIES[]{
    "tx", "wtxvariant", "acentry", "acc", "bestblock", "bestblock_nomerkle",
    "orderposnext", "destdata", "lockedutxo", "setting", "settings",
};
} // namespace

LegacyWalletInventory DecodeLegacyWalletRecords(std::span<const LegacyWalletRecord> records)
{
    Require(records.size() <= MAX_RECORDS, "Legacy record count exceeds supported limit");
    size_t total_bytes{0};
    for (const auto& record : records) {
        Require(record.key.size() <= MAX_DATABASE_KEY_BYTES && record.value.size() <= MAX_PAIR_BYTES &&
                    record.key.size() <= MAX_PAIR_BYTES - record.value.size(),
                "Legacy record size exceeds supported limit");
        const auto bytes{record.key.size() + record.value.size()};
        Require(bytes <= MAX_DECODED_BYTES - total_bytes, "Legacy decoded bytes exceed supported limit");
        total_bytes += bytes;
    }
    LegacyWalletInventory inventory;
    inventory.records.reserve(records.size());
    size_t private_identities{0};
    for (const auto& record : records) {
        SpanReader key{record.key}, value{record.value};
        const auto family{ReadBytes<std::string>(key, MAX_TEXT_BYTES)};
        const bool omitted{std::ranges::find(OMITTED_FAMILIES, family) != std::end(OMITTED_FAMILIES)};
        if (omitted) {
            ++inventory.omitted[family];
        } else if (family == "key" || family == "wkey" || family == "ckey" || family == "keymeta") {
            const auto pubkey{ReadPubKey(key)};
            const auto existing{inventory.keys.find(pubkey)};
            if (family != "keymeta" && (existing == inventory.keys.end() ||
                                       (!existing->second.private_key && !existing->second.ciphertext))) {
                Require(private_identities < MAX_PRIVATE_IDENTITIES, "Legacy private key count exceeds supported limit");
                ++private_identities;
            }
            auto& entry{inventory.keys[pubkey]};
            entry.source_families.insert(family);
            if (family == "keymeta") {
                AssignSerialized(entry.metadata, ReadMetadata(value));
            } else {
                if (family == "ckey") {
                    auto ciphertext{ReadBytes<std::vector<unsigned char>>(value, 48)};
                    Require(ciphertext.size() == 48, "Invalid legacy encrypted private key length");
                    CheckChecksum(value, Hash(ciphertext));
                    AssignConsistent(entry.ciphertext, std::move(ciphertext));
                } else {
                    // Adapt LoadKey/ReadKeyValue key+wkey branches from current
                    // walletdb.cpp, v29.5 walletdb.cpp and v0.18.1 walletdb.cpp.
                    // Preserve optional Hash(pubkey, DER); deliberately always
                    // verify the private/public pair, even with a valid hash.
                    CPrivKey priv;
                    if (family == "wkey") {
                        auto old{ReadWalletKey(value)};
                        priv = std::move(old.private_key);
                        AssignConsistent(entry.wkey, std::move(old.metadata));
                    } else {
                        priv = ReadBytes<CPrivKey>(value, MAX_DER_BYTES);
                    }
                    CheckChecksum(value, Hash(pubkey, priv));
                    CKey secret;
                    Require(secret.Load(priv, pubkey, /*fSkipCheck=*/false), "Invalid legacy private/public key pair");
                    AssignConsistent(entry.private_key, std::move(secret));
                }
            }
        } else if (family == "mkey") {
            const auto id{Read<uint32_t>(key)};
            Require(inventory.master_keys.size() < 32, "Legacy master key count exceeds supported limit");
            Require(inventory.master_keys.emplace(id, ReadMasterKey(value)).second, "Duplicate legacy master key ID");
        } else if (family == "hdchain") {
            auto check{value};
            const auto version{Read<int32_t>(check)};
            Require(version == 1 || version == 2, "Unsupported legacy HD chain version");
            AssignSerialized(inventory.hdchain, Read<CHDChain>(value));
        } else if (family == "pool") {
            const auto index{Read<int64_t>(key)};
            auto pool{ReadPool(value)};
            inventory.keys[pool.pubkey].source_families.insert(family);
            auto [entry, inserted]{inventory.pool.emplace(index, pool)};
            Require(inserted || entry->second == pool, "Conflicting legacy pool records");
        } else if (family == "cscript") {
            const auto id{Read<uint160>(key)};
            const auto script{ReadScript(value)};
            Require(Hash160(script) == id, "Legacy script hash mismatch");
            ScriptEntry(inventory, script).redeem = true;
        } else if (family == "watchs" || family == "watchmeta") {
            auto& script{ScriptEntry(inventory, ReadScript(key))};
            if (family == "watchmeta") {
                AssignSerialized(script.metadata, ReadMetadata(value));
            } else {
                const auto enabled{Read<uint8_t>(value)};
                Require(enabled == '0' || enabled == '1', "Invalid legacy watch-only marker");
                AssignConsistent(script.watch_enabled, enabled == '1');
            }
        } else if (family == "name" || family == "purpose") {
            const auto address{ReadBytes<std::string>(key, MAX_TEXT_BYTES)};
            auto text{ReadBytes<std::string>(value, MAX_TEXT_BYTES)};
            auto& entry{inventory.addresses[address]};
            AssignConsistent(family == "name" ? entry.label : entry.purpose, std::move(text));
        } else if (family == "defaultkey") {
            auto pubkey{ReadPubKey(value)};
            inventory.keys[pubkey].source_families.insert(family);
            AssignConsistent(inventory.default_pubkey, std::move(pubkey));
        } else if (family == "version" || family == "minversion") {
            const auto version{Read<int32_t>(value)};
            Require(version >= 0 && (family != "minversion" || version <= 169900), "Unsupported legacy wallet version");
            AssignConsistent(family == "version" ? inventory.file_version : inventory.min_version, version);
        } else if (family == "flags") {
            const auto flags{Read<uint64_t>(value)};
            constexpr uint64_t ALLOWED_FLAGS{0xffffffffULL | WALLET_FLAG_DISABLE_PRIVATE_KEYS | WALLET_FLAG_BLANK_WALLET};
            Require((flags & ~ALLOWED_FLAGS) == 0, "Unsupported legacy wallet flags");
            AssignConsistent(inventory.flags, flags);
        } else {
            throw std::runtime_error("Unsupported legacy wallet record family");
        }
        Require(omitted || (key.empty() && value.empty()), "Trailing legacy record fields");
        inventory.records.push_back({family, record.key.size(), record.value.size(), omitted});
    }
    Require(!(inventory.flags.value_or(0) & WALLET_FLAG_DISABLE_PRIVATE_KEYS) || private_identities == 0,
            "Legacy disable-private-keys flag conflicts with private records");
    return inventory;
}

} // namespace wallet
