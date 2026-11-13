// Copyright (c) 2009-present The Bitcoin Core developers
// Copyright (C) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/legacy_export.h>

#include <addresstype.h>
#include <crypto/common.h>
#include <key_io.h>
#include <support/cleanse.h>
#include <util/bip32.h>
#include <util/strencodings.h>
#include <util/string.h>
#include <util/time.h>

#include <univalue.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

namespace wallet {
namespace {
constexpr size_t MAX_METADATA_LINE{32U * 1024 * 1024};
constexpr std::string_view META_PREFIX{"# roots-meta-v1 "};
// Publication is the final fallible operation, including the return path.
static_assert(std::is_nothrow_move_constructible_v<LegacyExportResult>);

void FormatRequire(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void Checkpoint(const LegacyExportHook& hook, LegacyExportStage stage)
{
    if (hook) hook(stage);
}

// The original objects are mutable and invocation-owned. This traversal never
// copies a value. In particular the seed xprv node is populated once, after
// building all other members, and its only allocation is cleansed here.
void CleanJson(const UniValue& value) noexcept
{
    const auto& text{value.getValStr()};
    if (!text.empty()) memory_cleanse(const_cast<char*>(text.data()), text.size());
    if (value.isObject() || value.isArray()) {
        for (const auto& child : value.getValues()) CleanJson(child);
    }
}

struct JsonLine {
    UniValue object{UniValue::VOBJ};
    ~JsonLine() { CleanJson(object); }
};

struct OwnedText {
    std::string value;
    ~OwnedText() { if (!value.empty()) memory_cleanse(value.data(), value.size()); }
};

void WriteMetadata(LegacyExportOutput& output, const UniValue& object)
{
    size_t bytes{META_PREFIX.size() + 1};
    object.writeTo([&](std::string_view part) {
        FormatRequire(part.size() <= MAX_METADATA_LINE - bytes, "Legacy metadata line exceeds supported size limit");
        bytes += part.size();
    });
    output.Write(META_PREFIX);
    object.writeTo([&](std::string_view part) { output.Write(part); });
    output.Write("\n");
}

bool ValidUtf8(std::string_view text)
{
    for (size_t pos{0}; pos < text.size();) {
        const auto first{static_cast<unsigned char>(text[pos++])};
        if (first < 0x80) continue;
        unsigned count;
        uint32_t code;
        uint32_t minimum;
        if (first >= 0xc2 && first <= 0xdf) { count = 1; code = first & 31; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { count = 2; code = first & 15; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { count = 3; code = first & 7; minimum = 0x10000; }
        else return false;
        if (count > text.size() - pos) return false;
        for (unsigned next{0}; next < count; ++next) {
            const auto byte{static_cast<unsigned char>(text[pos++])};
            if ((byte & 0xc0) != 0x80) return false;
            code = (code << 6) | (byte & 63);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}

UniValue ByteString(const std::string& text)
{
    UniValue result{UniValue::VOBJ};
    const bool utf8{ValidUtf8(text)};
    result.pushKVEnd("encoding", utf8 ? "utf8" : "hex");
    result.pushKVEnd("value", utf8 ? text : HexStr(MakeUCharSpan(text)));
    return result;
}

UniValue OptionalBytes(const std::optional<std::string>& value)
{
    return value ? ByteString(*value) : UniValue{};
}

UniValue TimeValue(std::optional<int64_t> time)
{
    return time ? UniValue{util::ToString(*time)} : UniValue{};
}

std::optional<int64_t> UsableTime(int64_t time)
{
    return time > 0 && time <= 253402300799 ? std::optional{time} : std::nullopt;
}

std::optional<int64_t> KeyTime(const LegacyExportKey& key)
{
    auto time{key.metadata ? UsableTime(key.metadata->nCreateTime) : std::nullopt};
    const auto old{key.wkey ? UsableTime(key.wkey->created) : std::nullopt};
    if (old && (!time || *old < *time)) time = old;
    return time;
}

std::string FlagString(uint64_t flags)
{
    std::array<unsigned char, 8> bytes{};
    WriteBE64(bytes.data(), flags);
    return HexStr(bytes);
}

UniValue Metadata(const std::optional<CKeyMetadata>& metadata)
{
    if (!metadata) return {};
    const auto& fact{*metadata};
    UniValue result{UniValue::VOBJ};
    result.pushKVEnd("version", fact.nVersion);
    result.pushKVEnd("created", util::ToString(fact.nCreateTime));
    result.pushKVEnd("hd_path", fact.nVersion >= 10 ? ByteString(fact.hdKeypath) : UniValue{});
    result.pushKVEnd("hd_seed_id", fact.nVersion >= 10 && !fact.hd_seed_id.IsNull() ? UniValue{HexStr(fact.hd_seed_id)} : UniValue{});
    UniValue origin;
    if (fact.nVersion >= 12) {
        origin.setObject();
        origin.pushKVEnd("has_origin", fact.has_key_origin);
        origin.pushKVEnd("fingerprint", HexStr(fact.key_origin.fingerprint));
        UniValue path{UniValue::VARR};
        for (const auto index : fact.key_origin.path) path.push_back(index);
        origin.pushKVEnd("path", std::move(path));
    }
    result.pushKVEnd("origin", std::move(origin));
    return result;
}

// Adapted from Core v29.5 backup.cpp EncodeDumpString (16ca532c8212d691daed14940da0f71f84f7b3fc).
// Preserve all byte/percent semantics; stream to checked private output instead
// of an ordinary stringstream, and never interpolate raw metadata into a line.
void WriteDumpString(LegacyExportOutput& output, std::string_view text)
{
    constexpr char HEX[]{"0123456789abcdef"};
    size_t begin{0};
    for (size_t pos{0}; pos < text.size(); ++pos) {
        const auto byte{static_cast<unsigned char>(text[pos])};
        if (byte > 32 && byte < 128 && byte != '%') continue;
        output.Write(text.substr(begin, pos - begin));
        const char escaped[]{'%', HEX[byte >> 4], HEX[byte & 15]};
        output.Write(std::string_view{escaped, sizeof(escaped)});
        begin = pos + 1;
    }
    output.Write(text.substr(begin));
}

struct AddressFacts {
    std::map<std::string, std::set<CPubKey>> pubkeys;
    std::map<CPubKey, std::set<std::string>> addresses;
    std::map<CPubKey, const std::string*> labels;
};

AddressFacts MatchAddresses(const LegacyWalletInventory& inventory)
{
    AddressFacts result;
    std::map<CTxDestination, std::set<CPubKey>> destinations;
    for (const auto& [pubkey, key] : inventory.keys) {
        // Adapt v29.5 outputtype.cpp GetAllDestinationsForKey; the current
        // tree retains destination types/encoders but no longer that helper.
        destinations[PKHash(pubkey)].insert(pubkey);
        if (pubkey.IsCompressed()) {
            const WitnessV0KeyHash witness{pubkey};
            destinations[witness].insert(pubkey);
            destinations[ScriptHash(GetScriptForDestination(witness))].insert(pubkey);
        }
    }
    std::map<CPubKey, std::pair<std::string, std::string>> label_order;
    for (const auto& [raw, entry] : inventory.addresses) {
        const auto destination{DecodeDestination(raw)};
        const auto found{destinations.find(destination)};
        if (!IsValidDestination(destination) || found == destinations.end()) continue;
        const auto encoded{EncodeDestination(destination)};
        result.pubkeys[raw] = found->second;
        for (const auto& pubkey : found->second) {
            result.addresses[pubkey].insert(encoded);
            const auto order{std::pair{encoded, raw}};
            if (entry.label && (!label_order.contains(pubkey) || order < label_order.at(pubkey))) {
                label_order[pubkey] = order;
                result.labels[pubkey] = &*entry.label;
            }
        }
    }
    return result;
}

std::string_view Classification(LegacyKeyClassification role)
{
    switch (role) {
    case LegacyKeyClassification::SEED: return "seed";
    case LegacyKeyClassification::HD_RECEIVE: return "hd_receive";
    case LegacyKeyClassification::HD_CHANGE: return "hd_change";
    case LegacyKeyClassification::UNCLASSIFIED: return "unclassified";
    }
    throw std::runtime_error("Invalid legacy key classification");
}

std::string_view Derivation(LegacyKeyDerivation role)
{
    switch (role) {
    case LegacyKeyDerivation::SEED: return "seed";
    case LegacyKeyDerivation::VERIFIED: return "verified";
    case LegacyKeyDerivation::UNRECOGNIZED: return "unrecognized";
    case LegacyKeyDerivation::NOT_APPLICABLE: return "not_applicable";
    }
    throw std::runtime_error("Invalid legacy key derivation");
}

std::string WalletState(const LegacyWalletInventory& inventory, const LegacyExportCounts& counts)
{
    if (counts.private_keys) return "private";
    for (const auto& [id, script] : inventory.scripts) {
        if (script.watch_enabled.value_or(false)) return "watch_only";
    }
    return counts.public_key_identities || counts.scripts || counts.addresses || counts.identified_seeds || counts.encryption_keys ?
               "metadata_only" : "empty";
}

LegacyExportCounts Count(const LegacyWalletInventory& inventory, const LegacyRecoveredSecrets& secrets, const LegacyWalletCoverage& coverage)
{
    LegacyExportCounts result;
    result.records = inventory.records.size();
    result.private_keys = secrets.keys.size();
    result.public_key_identities = inventory.keys.size();
    result.identified_seeds = coverage.roots.seeds.size();
    for (const auto& [id, seed] : coverage.roots.seeds) result.roots += seed.root.has_value();
    result.scripts = inventory.scripts.size();
    result.addresses = inventory.addresses.size();
    result.reserve_entries = inventory.pool.size();
    result.encryption_keys = inventory.master_keys.size();
    for (const auto& [family, records] : inventory.omitted) result.omitted_records += records;
    for (const auto& [pubkey, key] : coverage.keys) result.unverified_derivations += key.derivation == LegacyKeyDerivation::UNRECOGNIZED;
    return result;
}

UniValue CountObject(const LegacyExportCounts& counts)
{
    UniValue object{UniValue::VOBJ};
    object.pushKVEnd("records", counts.records);
    object.pushKVEnd("private_keys", counts.private_keys);
    object.pushKVEnd("public_key_identities", counts.public_key_identities);
    object.pushKVEnd("identified_seeds", counts.identified_seeds);
    object.pushKVEnd("roots", counts.roots);
    object.pushKVEnd("scripts", counts.scripts);
    object.pushKVEnd("addresses", counts.addresses);
    object.pushKVEnd("reserve_entries", counts.reserve_entries);
    object.pushKVEnd("encryption_keys", counts.encryption_keys);
    object.pushKVEnd("omitted_records", counts.omitted_records);
    object.pushKVEnd("unverified_derivations", counts.unverified_derivations);
    return object;
}

void Render(LegacyExportOutput& output, const LegacySourceInfo& source, const LegacyWalletInventory& inventory,
            const LegacyRecoveredSecrets& secrets, const LegacyWalletCoverage& coverage, const LegacyExportResult& result,
            const LegacyExportHook& processing)
{
    const auto addresses{MatchAddresses(inventory)};
    Checkpoint(processing, LegacyExportStage::ADDRESSES);
    output.Write("# Bitcoin Roots offline legacy key export\n# roots-dumpwallet-version: 1\n# roots-network: ");
    output.Write(ChainTypeToString(coverage.roots.network));
    output.Write("\n");
    {
        JsonLine line;
        auto& j{line.object};
        j.pushKVEnd("type", "wallet");
        j.pushKVEnd("network", ChainTypeToString(coverage.roots.network));
        j.pushKVEnd("format", "bdb-v9");
        j.pushKVEnd("page_size", source.page_size);
        j.pushKVEnd("byte_order", source.big_endian ? "big" : "little");
        j.pushKVEnd("source_bytes", source.source_bytes);
        j.pushKVEnd("file_version", inventory.file_version ? UniValue{*inventory.file_version} : UniValue{});
        j.pushKVEnd("min_version", inventory.min_version ? UniValue{*inventory.min_version} : UniValue{});
        j.pushKVEnd("flags", inventory.flags ? UniValue{FlagString(*inventory.flags)} : UniValue{});
        j.pushKVEnd("unknown_tolerable_flags", FlagString(inventory.flags.value_or(0) & 0xfffffff8U));
        j.pushKVEnd("default_pubkey", inventory.default_pubkey ? UniValue{HexStr(*inventory.default_pubkey)} : UniValue{});
        j.pushKVEnd("state", result.state);
        j.pushKVEnd("counts", CountObject(result.counts));
        Checkpoint(processing, LegacyExportStage::WALLET);
        WriteMetadata(output, j);
    }
    for (const auto& [id, seed] : coverage.roots.seeds) {
        if (!seed.active || !seed.root) continue;
        output.Write("# extended private masterkey: ");
        output.Write(std::string_view{seed.extended_private_key.data(), seed.extended_private_key.size()});
        output.Write("\n");
    }
    std::vector<std::pair<int64_t, CPubKey>> order;
    for (const auto& [pubkey, key] : coverage.keys) {
        if (!key.wif.empty()) order.emplace_back(KeyTime(inventory.keys.at(pubkey)).value_or(0), pubkey);
    }
    std::sort(order.begin(), order.end());
    // Pure WIF/time/token/script payload adapted from v29.5 backup.cpp
    // dumpwallet. Offline metadata replaces chain-derived times and headers;
    // unclassified=1 is ignored by its importer, preserving the empty label.
    for (const auto& [time, pubkey] : order) {
        Checkpoint(processing, LegacyExportStage::KEY_PAYLOAD);
        const auto& key{coverage.keys.at(pubkey)};
        output.Write(std::string_view{key.wif.data(), key.wif.size()});
        output.Write(" " + FormatISO8601DateTime(time ? time : 1) + " ");
        const auto seed{coverage.roots.seeds.find(pubkey.GetID())};
        if (addresses.labels.contains(pubkey)) {
            output.Write("label=");
            WriteDumpString(output, *addresses.labels.at(pubkey));
        } else if (seed != coverage.roots.seeds.end() && seed->second.active) output.Write("hdseed=1");
        else if (!key.pool_indices.empty()) output.Write("reserve=1");
        else if (seed != coverage.roots.seeds.end()) output.Write("inactivehdseed=1");
        else if (key.classification == LegacyKeyClassification::HD_CHANGE) output.Write("change=1");
        else output.Write("unclassified=1");
        output.Write(" # addr=");
        if (addresses.addresses.contains(pubkey)) {
            bool first{true};
            for (const auto& address : addresses.addresses.at(pubkey)) {
                if (!first) output.Write(",");
                output.Write(address);
                first = false;
            }
        } else output.Write(EncodeDestination(PKHash(pubkey)));
        if (key.derivation == LegacyKeyDerivation::VERIFIED) {
            output.Write(" hdkeypath=");
            WriteDumpString(output, WriteHDKeypath(key.verified_path, true));
        }
        output.Write("\n");
    }
    for (const auto& [id, script] : inventory.scripts) {
        // v29.5 importwallet IsHex rejects an empty token. Preserve a valid
        // empty redeem script only in typed metadata; never invent its bytes.
        if (!script.redeem || script.script.empty()) continue;
        OwnedText hex{HexStr(script.script)};
        Checkpoint(processing, LegacyExportStage::SCRIPT_PAYLOAD);
        output.Write(hex.value);
        output.Write(" " + FormatISO8601DateTime(script.metadata ? UsableTime(script.metadata->nCreateTime).value_or(1) : 1));
        output.Write(" script=1 # addr=" + EncodeDestination(ScriptHash(id)) + "\n");
    }
    if (inventory.hdchain) {
        JsonLine line;
        auto& j{line.object};
        const auto& fact{*inventory.hdchain};
        j.pushKVEnd("type", "hdchain");
        j.pushKVEnd("version", fact.nVersion);
        j.pushKVEnd("seed_id", HexStr(fact.seed_id));
        j.pushKVEnd("external_counter", fact.nExternalChainCounter);
        j.pushKVEnd("internal_counter", fact.nVersion == 1 ? UniValue{} : UniValue{fact.nInternalChainCounter});
        Checkpoint(processing, LegacyExportStage::HDCHAIN);
        WriteMetadata(output, j);
    }
    for (const auto& [pubkey, key] : inventory.keys) {
        JsonLine line;
        auto& j{line.object};
        const auto& covered{coverage.keys.at(pubkey)};
        j.pushKVEnd("type", "key");
        j.pushKVEnd("pubkey", HexStr(pubkey));
        j.pushKVEnd("key_id", HexStr(pubkey.GetID()));
        j.pushKVEnd("has_private_key", !covered.wif.empty());
        UniValue families{UniValue::VARR};
        for (const auto& family : key.source_families) families.push_back(family);
        j.pushKVEnd("source_families", std::move(families));
        j.pushKVEnd("encrypted_in_source", key.ciphertext.has_value());
        j.pushKVEnd("classification", Classification(covered.classification));
        j.pushKVEnd("reserve", !covered.pool_indices.empty());
        j.pushKVEnd("created_at", TimeValue(KeyTime(key)));
        j.pushKVEnd("metadata", Metadata(key.metadata));
        UniValue old;
        if (key.wkey) {
            old.setObject();
            old.pushKVEnd("stream_version", key.wkey->stream_version);
            old.pushKVEnd("created", util::ToString(key.wkey->created));
            old.pushKVEnd("expires", util::ToString(key.wkey->expires));
            old.pushKVEnd("comment", ByteString(key.wkey->comment));
        }
        j.pushKVEnd("wkey", std::move(old));
        j.pushKVEnd("derivation", Derivation(covered.derivation));
        Checkpoint(processing, LegacyExportStage::KEY_METADATA);
        WriteMetadata(output, j);
    }
    for (const auto& [id, seed] : coverage.roots.seeds) {
        JsonLine line;
        auto& j{line.object};
        j.pushKVEnd("type", "seed");
        j.pushKVEnd("seed_id", HexStr(id));
        j.pushKVEnd("pubkey", seed.pubkey ? UniValue{HexStr(*seed.pubkey)} : UniValue{});
        j.pushKVEnd("active", seed.active);
        j.pushKVEnd("has_secret", seed.root.has_value());
        j.pushKVEnd("xprv", UniValue{});
        UniValue evidence{UniValue::VARR};
        for (const auto& fact : seed.evidence) evidence.push_back(fact);
        j.pushKVEnd("evidence", std::move(evidence));
        if (seed.root) {
            // Finish every allocation/member insertion before transferring the
            // single owned xprv string into its final node. No ordinary JSON
            // serialization scratch is created by UniValue::writeTo.
            OwnedText encoded;
            encoded.value.reserve(seed.extended_private_key.size());
            encoded.value.assign(seed.extended_private_key.data(), seed.extended_private_key.size());
            const_cast<UniValue&>(j["xprv"]).setStr(std::move(encoded.value));
        }
        Checkpoint(processing, LegacyExportStage::SEED_METADATA);
        WriteMetadata(output, j);
    }
    for (const auto& [index, entry] : inventory.pool) {
        JsonLine line;
        auto& j{line.object};
        j.pushKVEnd("type", "pool");
        j.pushKVEnd("index", util::ToString(index));
        j.pushKVEnd("stream_version", entry.stream_version);
        j.pushKVEnd("pubkey", HexStr(entry.pubkey));
        j.pushKVEnd("time", util::ToString(entry.time));
        j.pushKVEnd("internal", entry.internal);
        j.pushKVEnd("internal_present", entry.internal_present);
        j.pushKVEnd("pre_split", entry.pre_split);
        j.pushKVEnd("pre_split_present", entry.pre_split_present);
        Checkpoint(processing, LegacyExportStage::POOL_METADATA);
        WriteMetadata(output, j);
    }
    for (const auto& [id, script] : inventory.scripts) {
        JsonLine line;
        auto& j{line.object};
        j.pushKVEnd("type", "script");
        j.pushKVEnd("script_id", HexStr(id));
        j.pushKVEnd("script_hex", HexStr(script.script));
        UniValue kinds{UniValue::VARR};
        if (script.redeem) kinds.push_back("redeem");
        if (script.metadata) kinds.push_back("watch_metadata");
        if (script.watch_enabled) kinds.push_back("watch_only");
        j.pushKVEnd("kinds", std::move(kinds));
        j.pushKVEnd("watch_enabled", script.watch_enabled ? UniValue{*script.watch_enabled} : UniValue{});
        j.pushKVEnd("created_at", TimeValue(script.metadata ? UsableTime(script.metadata->nCreateTime) : std::nullopt));
        j.pushKVEnd("metadata", Metadata(script.metadata));
        Checkpoint(processing, LegacyExportStage::SCRIPT_METADATA);
        WriteMetadata(output, j);
    }
    for (const auto& [raw, entry] : inventory.addresses) {
        JsonLine line;
        auto& j{line.object};
        j.pushKVEnd("type", "address");
        j.pushKVEnd("address", ByteString(raw));
        j.pushKVEnd("label", OptionalBytes(entry.label));
        j.pushKVEnd("purpose", OptionalBytes(entry.purpose));
        UniValue matches{UniValue::VARR};
        if (addresses.pubkeys.contains(raw)) {
            for (const auto& pubkey : addresses.pubkeys.at(raw)) matches.push_back(HexStr(pubkey));
        }
        j.pushKVEnd("pubkeys", std::move(matches));
        Checkpoint(processing, LegacyExportStage::ADDRESS_METADATA);
        WriteMetadata(output, j);
    }
    for (const auto& [id, entry] : inventory.master_keys) {
        JsonLine line;
        auto& j{line.object};
        const auto& used{secrets.master_key_usage.at(id)};
        j.pushKVEnd("type", "encryption");
        j.pushKVEnd("id", id);
        j.pushKVEnd("method", entry.nDerivationMethod);
        j.pushKVEnd("iterations", entry.nDeriveIterations);
        j.pushKVEnd("salt", HexStr(entry.vchSalt));
        j.pushKVEnd("verified_with_private_key", !used.empty());
        UniValue keys{UniValue::VARR};
        for (const auto& pubkey : used) keys.push_back(HexStr(pubkey));
        j.pushKVEnd("used_for_pubkeys", std::move(keys));
        Checkpoint(processing, LegacyExportStage::ENCRYPTION_METADATA);
        WriteMetadata(output, j);
    }
    for (const auto& [family, records] : inventory.omitted) {
        JsonLine line;
        auto& j{line.object};
        j.pushKVEnd("type", "omitted");
        j.pushKVEnd("family", family);
        j.pushKVEnd("records", records);
        Checkpoint(processing, LegacyExportStage::OMITTED_METADATA);
        WriteMetadata(output, j);
    }
    output.Write("# End of dump\n# roots-export-complete: 1\n");
}
} // namespace

LegacyExportResult WriteLegacyWalletExport(const LegacyWalletSource& source, const SecureString& passphrase,
                                          const fs::path& destination, LegacyOutputHook hook, LegacyExportHook processing)
{
    Checkpoint(processing, LegacyExportStage::START);
    const auto inventory{DecodeLegacyWalletRecords(source.Records())};
    Checkpoint(processing, LegacyExportStage::DECODED);
    const auto secrets{RecoverLegacyWalletSecrets(inventory, passphrase)};
    Checkpoint(processing, LegacyExportStage::RECOVERED);
    const auto coverage{ValidateLegacyWalletCoverage(inventory, secrets)};
    Checkpoint(processing, LegacyExportStage::VALIDATED);
    const auto counts{Count(inventory, secrets, coverage)};
    LegacyExportResult result{destination, WalletState(inventory, counts), counts};
    LegacyExportOutput output{source, destination, std::move(hook)};
    Render(output, source.Info(), inventory, secrets, coverage, result, processing);
    Checkpoint(processing, LegacyExportStage::RENDERED);
    output.Publish();
    return result;
}
} // namespace wallet
