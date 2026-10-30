// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <wallet/export.h>

#include <hash.h>
#include <script/descriptor.h>
#include <script/interpreter.h>
#include <script/signingprovider.h>
#include <script/solver.h>
#include <util/expected.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/wallet.h>

#include <algorithm>
#include <map>
#include <span>

namespace wallet {
//! Merge only public script/key metadata, checking identities before any duplicate is discarded.
static bool MergeExportProvider(FlatSigningProvider& merged, const FlatSigningProvider& provider, bool taproot)
{
    for (const auto& [id, pubkey] : provider.pubkeys) {
        if (!pubkey.IsFullyValid()) return false;
        // Tapscript Miniscript also indexes keys by HASH160(x-only), for pk_h lookup.
        const bool xonly_id = taproot && pubkey.IsCompressed() && CKeyID{Hash160(XOnlyPubKey{pubkey})} == id;
        if (pubkey.GetID() != id && !xonly_id) return false;
        const auto [it, inserted] = merged.pubkeys.emplace(id, pubkey);
        if (!inserted && it->second != pubkey) {
            if (!xonly_id || XOnlyPubKey{it->second} != XOnlyPubKey{pubkey}) return false;
            // The x-only alias has no parity; retain a deterministic public representative.
            if (pubkey < it->second) it->second = pubkey;
        }
    }
    for (const auto& [id, script] : provider.scripts) {
        if (CScriptID{script} != id) return false;
        const auto [it, inserted] = merged.scripts.emplace(id, script);
        if (!inserted && it->second != script) return false;
    }
    return true;
}

//! Follow only Core's permitted non-Taproot wrappers; recognition of the inner script is separate.
static util::Expected<void, AddressKeyError> CheckExportWrappers(const CScript& script, const SigningProvider& provider)
{
    CScript inner{script};
    std::vector<std::vector<unsigned char>> data;
    if (Solver(inner, data) == TxoutType::SCRIPTHASH) {
        CScript redeem;
        if (!provider.GetCScript(CScriptID{uint160{data[0]}}, redeem)) return util::Unexpected{AddressKeyError::MISSING_METADATA};
        if (GetScriptForDestination(ScriptHash{redeem}) != inner) return util::Unexpected{AddressKeyError::INCONSISTENT};
        inner = std::move(redeem);
    }
    if (Solver(inner, data) == TxoutType::WITNESS_V0_SCRIPTHASH) {
        CScript witness;
        if (!provider.GetCScript(CScriptID{RIPEMD160(data[0])}, witness)) return util::Unexpected{AddressKeyError::MISSING_METADATA};
        if (GetScriptForDestination(WitnessV0ScriptHash{witness}) != inner) return util::Unexpected{AddressKeyError::INCONSISTENT};
        inner = std::move(witness);
    }
    const auto type = Solver(inner, data);
    if (inner != script && (type == TxoutType::PUBKEYHASH || type == TxoutType::WITNESS_V0_KEYHASH)) {
        CPubKey pubkey;
        if (!provider.GetPubKey(CKeyID{uint160{data[0]}}, pubkey)) return util::Unexpected{AddressKeyError::MISSING_METADATA};
    }
    return {};
}

util::Expected<std::map<XOnlyPubKey, std::set<AddressKeyRole>>, AddressKeyError> GetTaprootKeyCandidates(const XOnlyPubKey& output, const TaprootSpendData& spenddata, const SigningProvider& provider)
{
    if (spenddata.internal_key.IsNull()) return util::Unexpected{AddressKeyError::MISSING_METADATA};
    if (!output.IsFullyValid() || !spenddata.internal_key.IsFullyValid()) return util::Unexpected{AddressKeyError::INCONSISTENT};
    const auto tweaked = spenddata.internal_key.CreateTapTweak(spenddata.merkle_root.IsNull() ? nullptr : &spenddata.merkle_root);
    if (!tweaked || tweaked->first != output) return util::Unexpected{AddressKeyError::INCONSISTENT};

    std::map<XOnlyPubKey, std::set<AddressKeyRole>> candidates;
    candidates[spenddata.internal_key].insert(AddressKeyRole::TAPROOT_INTERNAL);
    for (const auto& [leaf, controls] : spenddata.scripts) {
        const auto& [bytes, version] = leaf;
        if (version < 0 || version > 0xfe || (version & 1) || controls.empty()) return util::Unexpected{AddressKeyError::INCONSISTENT};
        const auto leaf_hash = ComputeTapleafHash(version, bytes);
        // Check every supplied association, including non-preferred/repeated control blocks.
        // InferTaprootTree alone deliberately skips malformed records and does not check parity.
        for (const auto& control : controls) {
            if (control.size() < TAPROOT_CONTROL_BASE_SIZE || control.size() > TAPROOT_CONTROL_MAX_SIZE ||
                (control.size() - TAPROOT_CONTROL_BASE_SIZE) % TAPROOT_CONTROL_NODE_SIZE != 0 ||
                (control[0] & TAPROOT_LEAF_MASK) != version) return util::Unexpected{AddressKeyError::INCONSISTENT};
            const XOnlyPubKey internal{std::span{control}.subspan(1, 32)};
            if (internal != spenddata.internal_key || ComputeTaprootMerkleRoot(control, leaf_hash) != spenddata.merkle_root ||
                !output.CheckTapTweak(internal, spenddata.merkle_root, control[0] & 1)) return util::Unexpected{AddressKeyError::INCONSISTENT};
        }
        if (version != TAPROOT_LEAF_TAPSCRIPT) continue;
        const CScript script{bytes.begin(), bytes.end()};
        const auto descriptor = InferTaprootScript(script, provider);
        if (!descriptor) continue;
        FlatSigningProvider expanded;
        std::vector<CScript> scripts;
        if (!descriptor->Expand(0, provider, scripts, expanded) || scripts != std::vector{script}) return util::Unexpected{AddressKeyError::INCONSISTENT};
        std::set<CPubKey> pubkeys;
        std::set<CExtPubKey> ext_keys;
        descriptor->GetPubKeys(pubkeys, ext_keys);
        if (!ext_keys.empty()) return util::Unexpected{AddressKeyError::INCONSISTENT};
        for (const auto& pubkey : pubkeys) {
            if (!pubkey.IsFullyValid() || !pubkey.IsCompressed()) return util::Unexpected{AddressKeyError::INCONSISTENT};
            candidates[XOnlyPubKey{pubkey}].insert(AddressKeyRole::TAPROOT_SCRIPT);
        }
    }
    return candidates;
}

util::Expected<AddressKeyResult, AddressKeyError> ExportAddressKeys(const CWallet& wallet, const CTxDestination& destination, AddressKeyType type)
{
    AssertLockHeld(wallet.cs_wallet);
    const auto pkhash = std::get_if<PKHash>(&destination);
    const auto witness = std::get_if<WitnessV0KeyHash>(&destination);
    const auto taproot = std::get_if<WitnessV1Taproot>(&destination);
    if ((type == AddressKeyType::TAPROOT_OUTPUT && !taproot) || (!pkhash && !witness &&
        !taproot && !std::holds_alternative<ScriptHash>(destination) && !std::holds_alternative<WitnessV0ScriptHash>(destination))) {
        return util::Unexpected{AddressKeyError::UNSUPPORTED};
    }
    if (taproot && !taproot->IsFullyValid()) return util::Unexpected{AddressKeyError::UNSUPPORTED};

    const CScript script{GetScriptForDestination(destination)};
    FlatSigningProvider provider;
    std::vector<TaprootSpendData> taproot_data;
    std::vector<DescriptorMuSig2KeyInfo> musig2;
    bool direct_taproot_key{false};
    auto managers = wallet.GetAllScriptPubKeyMans();
    const auto matching = wallet.GetScriptPubKeyMans(script);
    managers.insert(matching.begin(), matching.end());
    for (const auto spk_man : managers) {
        const auto desc_spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man);
        if (!desc_spk_man) continue;
        LOCK(desc_spk_man->cs_desc_man);
        const auto exact = desc_spk_man->GetExportProvider(script, taproot ? &musig2 : nullptr);
        if (exact) {
            if (!MergeExportProvider(provider, *exact, taproot != nullptr)) return util::Unexpected{AddressKeyError::INCONSISTENT};
            if (taproot) {
                const auto tree = exact->tr_trees.find(*taproot);
                if (tree != exact->tr_trees.end()) {
                    if (!tree->second.IsComplete()) return util::Unexpected{AddressKeyError::INCONSISTENT};
                    taproot_data.push_back(tree->second.GetSpendData());
                }
            }
        } else if (exact.error() != AddressKeyError::NO_KEY) {
            return util::Unexpected{exact.error()};
        }
        if (pkhash || witness || taproot || std::holds_alternative<ScriptHash>(destination)) {
            for (const auto& pubkey : desc_spk_man->GetExportPubKeys(destination)) {
                if (!pubkey.IsFullyValid()) return util::Unexpected{AddressKeyError::INCONSISTENT};
                const auto [it, inserted] = provider.pubkeys.emplace(pubkey.GetID(), pubkey);
                if (!inserted && it->second != pubkey) return util::Unexpected{AddressKeyError::INCONSISTENT};
                if (taproot) direct_taproot_key = true;
                if (std::holds_alternative<ScriptHash>(destination)) {
                    const CScript redeem{GetScriptForDestination(WitnessV0KeyHash{pubkey})};
                    provider.scripts.emplace(CScriptID{redeem}, redeem);
                }
            }
        }
    }
    std::map<CPubKey, std::set<AddressKeyRole>> candidates;
    if (taproot) {
        std::map<XOnlyPubKey, std::set<AddressKeyRole>> xonly_candidates;
        for (const auto& data : taproot_data) {
            const auto associated = GetTaprootKeyCandidates(*taproot, data, provider);
            if (!associated) return util::Unexpected{associated.error()};
            for (const auto& [xonly, roles] : *associated) {
                xonly_candidates[xonly].insert(roles.begin(), roles.end());
            }
        }
        xonly_candidates[*taproot].insert(AddressKeyRole::TAPROOT_OUTPUT);
        for (const auto& entry : musig2) {
            // Original typed provenance must bind to a verified internal/output/recognized leaf key.
            // Participants keep their own concrete full-key identities, never aggregate roles.
            if (!xonly_candidates.contains(XOnlyPubKey{entry.derived})) return util::Unexpected{AddressKeyError::INCONSISTENT};
            for (const auto& participant : entry.participants) {
                candidates[participant].insert(AddressKeyRole::MUSIG2_PARTICIPANT);
                // The same full point may be held with uncompressed SEC serialization.
                // Decompression preserves Y parity; the opposite point is not a participant.
                CPubKey uncompressed{participant};
                if (!uncompressed.Decompress()) return util::Unexpected{AddressKeyError::INCONSISTENT};
                candidates[uncompressed].insert(AddressKeyRole::MUSIG2_PARTICIPANT);
            }
        }
        for (const auto& [xonly, roles] : xonly_candidates) {
            // Both parity representatives are valid. Resolve only actually held scalars,
            // preserving any known compressed or uncompressed public-key identity.
            for (const auto& pubkey : xonly.GetCPubKeys()) candidates[pubkey].insert(roles.begin(), roles.end());
            for (const auto spk_man : managers) {
                const auto desc_spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man);
                if (!desc_spk_man) continue;
                LOCK(desc_spk_man->cs_desc_man);
                for (const auto& pubkey : desc_spk_man->GetExportPubKeys(WitnessV1Taproot{xonly})) {
                    candidates[pubkey].insert(roles.begin(), roles.end());
                }
            }
        }
    } else {
        const auto wrappers = CheckExportWrappers(script, provider);
        if (!wrappers) return util::Unexpected{wrappers.error()};
        const auto concrete = InferDescriptor(script, provider);
        if (!concrete->IsSolvable()) {
            return util::Unexpected{pkhash || witness ? AddressKeyError::NO_KEY : AddressKeyError::UNSUPPORTED};
        }
        // Re-expansion binds the recognized semantics and every wrapper commitment to the output.
        FlatSigningProvider expanded;
        std::vector<CScript> scripts;
        if (!concrete->Expand(0, provider, scripts, expanded) ||
            std::find(scripts.begin(), scripts.end(), script) == scripts.end()) {
            return util::Unexpected{AddressKeyError::INCONSISTENT};
        }
        std::set<CPubKey> pubkeys;
        std::set<CExtPubKey> ext_keys;
        concrete->GetPubKeys(pubkeys, ext_keys);
        if (!ext_keys.empty()) return util::Unexpected{AddressKeyError::INCONSISTENT};
        for (const auto& pubkey : pubkeys) candidates[pubkey].insert(AddressKeyRole::SCRIPT);
    }
    std::map<CPubKey, AddressKey> records;
    for (const auto spk_man : managers) {
        const auto desc_spk_man = dynamic_cast<DescriptorScriptPubKeyMan*>(spk_man);
        if (!desc_spk_man) continue;
        LOCK(desc_spk_man->cs_desc_man);
        for (const auto& [pubkey, roles] : candidates) {
            if (type == AddressKeyType::TAPROOT_OUTPUT && !roles.contains(AddressKeyRole::TAPROOT_INTERNAL) &&
                !roles.contains(AddressKeyRole::TAPROOT_OUTPUT)) continue;
            if (!pubkey.IsFullyValid()) return util::Unexpected{AddressKeyError::INCONSISTENT};
            auto exported = desc_spk_man->ExportKey(pubkey);
            if (!exported) return util::Unexpected{exported.error()};
            if (!*exported) continue;
            CKey key{std::move(**exported)};
            if (!key.IsValid() || key.GetPubKey() != pubkey || (witness && !key.IsCompressed())) {
                return util::Unexpected{AddressKeyError::INCONSISTENT};
            }
            const auto [it, inserted] = records.try_emplace(pubkey, AddressKey{std::move(key), roles});
            if (!inserted) it->second.roles.insert(roles.begin(), roles.end());
        }
    }
    if (records.empty()) {
        if (taproot && taproot_data.empty() && !direct_taproot_key) return util::Unexpected{AddressKeyError::MISSING_METADATA};
        return util::Unexpected{type == AddressKeyType::TAPROOT_OUTPUT ? AddressKeyError::NO_TAPROOT_OUTPUT_KEY : AddressKeyError::NO_KEY};
    }
    if (type == AddressKeyType::TAPROOT_OUTPUT) {
        std::map<CPubKey, AddressKey> output_keys;
        for (auto& [pubkey, record] : records) {
            if (record.roles.contains(AddressKeyRole::TAPROOT_INTERNAL)) {
                for (const auto& data : taproot_data) {
                    if (XOnlyPubKey{pubkey} != data.internal_key) continue;
                    // A pointer to the known zero root is the tree-less TapTweak.
                    // nullptr would leave the private key untweaked in ComputeKeyPair.
                    auto output = record.key.ComputeKeyPair(&data.merkle_root).GetKey();
                    if (!output || XOnlyPubKey{output->GetPubKey()} != *taproot) return util::Unexpected{AddressKeyError::INCONSISTENT};
                    const auto output_pubkey = output->GetPubKey();
                    output_keys.try_emplace(output_pubkey, AddressKey{std::move(*output), {AddressKeyRole::TAPROOT_OUTPUT}});
                }
            }
            if (record.roles.contains(AddressKeyRole::TAPROOT_OUTPUT)) {
                if (XOnlyPubKey{pubkey} != *taproot) return util::Unexpected{AddressKeyError::INCONSISTENT};
                output_keys.try_emplace(pubkey, AddressKey{std::move(record.key), {AddressKeyRole::TAPROOT_OUTPUT}});
            }
        }
        if (output_keys.empty()) return util::Unexpected{AddressKeyError::NO_TAPROOT_OUTPUT_KEY};
        records = std::move(output_keys);
    }

    AddressKeyResult result;
    for (auto& [pubkey, record] : records) result.keys.push_back(std::move(record));
    // A single hash-bound key is the original RPC contract, never an arbitrary first record.
    result.legacy_aliases = pkhash || witness;
    if (result.legacy_aliases && result.keys.size() != 1) return util::Unexpected{AddressKeyError::INCONSISTENT};
    return result;
}

} // namespace wallet
