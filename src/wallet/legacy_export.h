// Copyright (c) 2026 The Bitcoin Core developers
// Copyright (C) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_LEGACY_EXPORT_H
#define BITCOIN_WALLET_LEGACY_EXPORT_H

#include <support/allocators/zeroafterfree.h>
#include <util/chaintype.h>
#include <util/fs.h>
#include <wallet/crypter.h>
#include <wallet/walletdb.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace wallet {

struct LegacyWalletRecord {
    SerializeData key;
    SerializeData value;
};

struct LegacyWKeyMetadata {
    int32_t stream_version;
    int64_t created;
    int64_t expires;
    std::string comment;
    bool operator==(const LegacyWKeyMetadata&) const = default;
};

struct LegacyPoolEntry {
    int32_t stream_version;
    int64_t time;
    CPubKey pubkey;
    bool internal{false};
    bool internal_present{false};
    bool pre_split{false};
    bool pre_split_present{false};
    bool operator==(const LegacyPoolEntry&) const = default;
};

struct LegacyExportKey {
    std::optional<CKey> private_key;
    std::optional<std::vector<unsigned char>> ciphertext;
    std::optional<CKeyMetadata> metadata;
    // Knots metadata v2's opaque byte, absent in Core's metadata serializer.
    std::optional<uint8_t> unsupported_key_flags;
    std::optional<LegacyWKeyMetadata> wkey;
    // Original associations only: absent metadata never implies "imported".
    std::set<std::string> source_families;
};

struct LegacyExportScript {
    CScript script;
    bool redeem{false};
    std::optional<bool> watch_enabled;
    std::optional<CKeyMetadata> metadata;
    std::optional<uint8_t> unsupported_key_flags;
};

struct LegacyExportAddress {
    std::optional<std::string> label;
    std::optional<std::string> purpose;
};

struct LegacyRecordProvenance {
    std::string family;
    size_t key_bytes;
    size_t value_bytes;
    bool omitted;
};

/** Validated stored facts, without decryption, seed inference or derivation.
 * Records retains one accounting entry per input pair in the original order.
 * Raw record bytes remain available from the source; omitted values stay opaque.
 */
struct LegacyWalletInventory {
    std::map<CPubKey, LegacyExportKey> keys;
    std::map<uint32_t, CMasterKey> master_keys;
    std::optional<CHDChain> hdchain;
    std::map<int64_t, LegacyPoolEntry> pool;
    std::map<CScriptID, LegacyExportScript> scripts;
    std::map<std::string, LegacyExportAddress> addresses;
    std::optional<CPubKey> default_pubkey;
    std::optional<int32_t> file_version;
    std::optional<int32_t> min_version;
    std::optional<uint64_t> flags;
    std::map<std::string, size_t> omitted;
    std::vector<LegacyRecordProvenance> records;
};

/** Decode supported records completely, or throw a non-secret diagnostic.
 * Variable lengths are checked before allocating or invoking a serializer.
 */
LegacyWalletInventory DecodeLegacyWalletRecords(std::span<const LegacyWalletRecord> records);

/** Recovered keys remain independent of any HD hierarchy. Empty usage sets
 * mean the corresponding master-key slot was not verified by a private key.
 * CKey owns its secret in secure memory; no symmetric master is retained.
 */
struct LegacyRecoveredSecrets {
    std::map<CPubKey, CKey> keys;
    std::map<uint32_t, std::set<CPubKey>> master_key_usage;
};

/** Recover all required secrets in memory or throw a non-secret diagnostic.
 * The input is unchanged on success and failure. Passphrase bytes are used
 * exactly as supplied, including embedded NUL, with a 4096-byte limit.
 * No passphrase is verified for a wallet without encrypted private keys.
 */
LegacyRecoveredSecrets RecoverLegacyWalletSecrets(const LegacyWalletInventory& inventory, const SecureString& passphrase);

struct LegacyExportSeed {
    std::optional<CPubKey> pubkey;
    bool active{false};
    std::set<std::string> evidence;
    std::optional<CExtKey> root;
    std::optional<KeyFingerprint> fingerprint;
    SecureString wif;
    SecureString extended_private_key;
};

struct LegacyWalletRoots {
    ChainType network;
    // Seed Hash160 is the stored identity; a four-byte fingerprint is not.
    std::map<CKeyID, LegacyExportSeed> seeds;
};

/** Identify stored seed evidence and derive every available root, without
 * changing the inventory or reducing the recovered individual-key set.
 * Uses the selected chain Params(), as do EncodeSecret/EncodeExtKey; this
 * records an encoding selection, not an intrinsic network of the source.
 * Missing required seeds fail for private-material inventories. Secret-free
 * inventories retain unresolved seed identities with null root/fingerprint.
 */
LegacyWalletRoots DeriveLegacyWalletRoots(const LegacyWalletInventory& inventory, const LegacyRecoveredSecrets& secrets);

enum class LegacyKeyClassification { SEED, HD_RECEIVE, HD_CHANGE, UNCLASSIFIED };
enum class LegacyKeyDerivation { SEED, VERIFIED, UNRECOGNIZED, NOT_APPLICABLE };

struct LegacyCoveredKey {
    SecureString wif; // Empty for an explicit public-only identity.
    LegacyKeyClassification classification{LegacyKeyClassification::UNCLASSIFIED};
    LegacyKeyDerivation derivation{LegacyKeyDerivation::NOT_APPLICABLE};
    std::vector<int64_t> pool_indices; // All original reserve associations.
    std::vector<uint32_t> verified_path;
};

struct LegacyDerivedMaxima {
    std::optional<uint32_t> external;
    std::optional<uint32_t> internal;
};

struct LegacyWalletCoverage {
    LegacyWalletRoots roots;
    std::map<CPubKey, LegacyCoveredKey> keys;
    // Observed verified child indices, separate from every stored counter.
    std::map<CKeyID, LegacyDerivedMaxima> derived_maxima;
    uint64_t derived_steps{0};
};

/** Account for every explicit identity and independent WIF, validating local
 * Core child paths/origins with checked derivation. Original metadata, pool
 * facts and counters remain in the const inventory, without inferred updates.
 */
LegacyWalletCoverage ValidateLegacyWalletCoverage(const LegacyWalletInventory& inventory, const LegacyRecoveredSecrets& secrets);

struct LegacySourceInfo {
    uint32_t page_size;
    bool big_endian;
    uint64_t source_bytes;
};

/** Read-only records from an offline, clean legacy BDB backup.
 *
 * No wallet is loaded, upgraded or unlocked. The source must remain offline:
 * identity, size, mtime and content checks detect ordinary concurrent changes,
 * but cannot establish a snapshot against an adversarial concurrent writer.
 * Reading can update filesystem atime; use a read-only mount to preserve it.
 */
class LegacyWalletSource
{
    friend class LegacyExportOutput;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    const fs::path& SourcePath() const;

public:
    /** Accept an explicit regular data file or a directory containing wallet.dat.
     * Throws on unsupported format, dirty LSNs, I/O errors or source changes.
     */
    explicit LegacyWalletSource(const fs::path& path);
    ~LegacyWalletSource();
    LegacyWalletSource(const LegacyWalletSource&) = delete;
    LegacyWalletSource& operator=(const LegacyWalletSource&) = delete;

    std::span<const LegacyWalletRecord> Records() const;
    LegacySourceInfo Info() const;
    /** Must be called again immediately before publishing an export. */
    void VerifyUnchanged() const;
};

/** Cancellation/failure seam at the actual output operations. A throwing hook
 * aborts before publication; it receives no secret data or temporary pathname.
 */
enum class LegacyOutputStage { CREATED, WRITE, FLUSH, SYNC, CLOSE, PUBLISH };
using LegacyOutputHook = std::function<void(LegacyOutputStage)>;

/** Invocation-owned private output. The source must outlive this object.
 * Write only fully validated material, then Publish after rendering succeeds.
 * The destination's existing parent must be trusted (on POSIX, owned by the
 * current user and not group/other writable). All work stays anchored to the
 * opened directory. Unsupported native publication guarantees fail closed.
 * Ordinary destruction removes only the owned temporary; crashes can retain it.
 */
class LegacyExportOutput
{
    struct Impl;
    std::unique_ptr<Impl> m_impl;
#if defined(WIN32) || defined(__linux__)
    friend struct LegacyExportOutputTestAccess;
#endif
#ifdef WIN32
    /** Handle-based capability check, also exercised by native Windows tests. */
    static bool IsPhysicalWindowsVolumeRoot(void* directory);
#endif
#ifdef __linux__
    // Fixed-size kernel facts plus an independent fstat identity. Keeping this
    // proof separate permits deterministic capability/error tests without mounts.
    struct LinuxMountFacts {
        int status{-1};
        uint32_t mask{0};
        uint16_t mode{0};
        uint32_t device_major{0}, device_minor{0};
        uint32_t expected_major{0}, expected_minor{0};
        uint64_t inode{0}, expected_inode{0}, mount_id{0};
    };
    static LinuxMountFacts InspectLinuxMount(int directory);
    static bool SameLinuxMount(const LinuxMountFacts& source, const LinuxMountFacts& output);
#endif

public:
    LegacyExportOutput(const LegacyWalletSource& source, const fs::path& destination, LegacyOutputHook hook = {});
    ~LegacyExportOutput();
    LegacyExportOutput(const LegacyExportOutput&) = delete;
    LegacyExportOutput& operator=(const LegacyExportOutput&) = delete;

    void Write(std::string_view bytes);
    /** Checks source/parent/temp identity and every flush/sync/close before the
     * final native no-replace move. There is no fallible post-publication step.
     */
    void Publish();
};

struct LegacyExportCounts {
    uint64_t records{0};
    uint64_t private_keys{0};
    uint64_t public_key_identities{0};
    uint64_t identified_seeds{0};
    uint64_t roots{0};
    uint64_t scripts{0};
    uint64_t addresses{0};
    uint64_t reserve_entries{0};
    uint64_t encryption_keys{0};
    uint64_t omitted_records{0};
    uint64_t unverified_derivations{0};
};

struct LegacyExportResult {
    fs::path destination;
    std::string state;
    LegacyExportCounts counts;
};

/** Cooperative processing checkpoints. The synchronous callback receives only
 * a stage, retains no borrowed data, and may throw to cancel before publication.
 * It is called between bounded operations, not inside KDF/crypto primitives.
 * Exceptions propagate; callers must supply non-secret exception diagnostics.
 */
enum class LegacyExportStage {
    START, DECODED, RECOVERED, VALIDATED, ADDRESSES,
    WALLET, KEY_PAYLOAD, SCRIPT_PAYLOAD, HDCHAIN, KEY_METADATA,
    SEED_METADATA, POOL_METADATA, SCRIPT_METADATA, ADDRESS_METADATA,
    ENCRYPTION_METADATA, OMITTED_METADATA, RENDERED,
};
using LegacyExportHook = std::function<void(LegacyExportStage)>;

/** Validate/recover the complete inventory, render version1, then publish.
 * Returns only destination/state/counts. No wallet or chain lifecycle or
 * passphrase I/O is performed. Selected Params() determine encodings.
 */
LegacyExportResult WriteLegacyWalletExport(const LegacyWalletSource& source, const SecureString& passphrase,
                                          const fs::path& destination, LegacyOutputHook hook = {}, LegacyExportHook processing = {});

} // namespace wallet

#endif // BITCOIN_WALLET_LEGACY_EXPORT_H
