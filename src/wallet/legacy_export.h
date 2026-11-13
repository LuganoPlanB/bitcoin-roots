// Copyright (c) 2026 The Bitcoin Core developers
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
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
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
    std::optional<LegacyWKeyMetadata> wkey;
    // Original associations only: absent metadata never implies "imported".
    std::set<std::string> source_families;
};

struct LegacyExportScript {
    CScript script;
    bool redeem{false};
    std::optional<bool> watch_enabled;
    std::optional<CKeyMetadata> metadata;
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

/** Read-only records from an offline, clean legacy BDB backup.
 *
 * No wallet is loaded, upgraded or unlocked. The source must remain offline:
 * identity, size, mtime and content checks detect ordinary concurrent changes,
 * but cannot establish a snapshot against an adversarial concurrent writer.
 * Reading can update filesystem atime; use a read-only mount to preserve it.
 */
class LegacyWalletSource
{
    struct Impl;
    std::unique_ptr<Impl> m_impl;

public:
    /** Accept an explicit regular data file or a directory containing wallet.dat.
     * Throws on unsupported format, dirty LSNs, I/O errors or source changes.
     */
    explicit LegacyWalletSource(const fs::path& path);
    ~LegacyWalletSource();
    LegacyWalletSource(const LegacyWalletSource&) = delete;
    LegacyWalletSource& operator=(const LegacyWalletSource&) = delete;

    std::span<const LegacyWalletRecord> Records() const;
    /** Must be called again immediately before publishing an export. */
    void VerifyUnchanged() const;
};

} // namespace wallet

#endif // BITCOIN_WALLET_LEGACY_EXPORT_H
