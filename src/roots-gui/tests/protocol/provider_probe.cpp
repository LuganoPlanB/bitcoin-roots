// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license; see COPYING.
// Public-data API adoption probe, not an application signing service.
#include <wally_address.h>
#include <wally_bip32.h>
#include <wally_core.h>
#include <wally_crypto.h>
#include <wally_descriptor.h>
#include <wally_psbt.h>
#include <wally_script.h>
#include <wally_transaction.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <memory>
#include <source_location>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Ok(int result, std::source_location at = std::source_location::current())
{ if (result != WALLY_OK) throw std::runtime_error("provider error " + std::to_string(result) + " line " + std::to_string(at.line())); }
void Require(bool value, std::source_location at = std::source_location::current())
{ if (!value) throw std::runtime_error("provider contract assertion line " + std::to_string(at.line())); }
using Psbt = std::unique_ptr<wally_psbt, decltype(&wally_psbt_free)>;
using Tx = std::unique_ptr<wally_tx, decltype(&wally_tx_free)>;
std::vector<unsigned char> Unhex(const char* text)
{
    const size_t length = std::strlen(text);
    std::vector<unsigned char> bytes(length / 2);
    size_t written;
    Ok(wally_hex_to_bytes(text, bytes.data(), bytes.size(), &written));
    Require(written == bytes.size());
    return bytes;
}
std::string Hex(const unsigned char* bytes, size_t length)
{
    char* raw = nullptr; Ok(wally_hex_from_bytes(bytes, length, &raw));
    std::string result(raw); wally_free_string(raw); return result;
}
Psbt Parse(const char* text)
{
    wally_psbt* raw = nullptr;
    const int result = wally_psbt_from_base64(text,
        WALLY_PSBT_PARSE_FLAG_STRICT | WALLY_PSBT_PARSE_FLAG_COMPLETE, &raw);
    return Psbt(result == WALLY_OK ? raw : nullptr, wally_psbt_free);
}
std::string Base64(const wally_psbt* psbt)
{
    char* raw = nullptr; Ok(wally_psbt_to_base64(psbt, 0, &raw));
    std::string result(raw); wally_free_string(raw); return result;
}
Tx Extract(const wally_psbt* psbt, uint32_t flags)
{
    wally_tx* raw = nullptr; Ok(wally_psbt_extract(psbt, flags, &raw));
    return Tx(raw, wally_tx_free);
}

// Representative public signature verification for standard legacy/segwit
// single-key/multisig and Taproot keypath inputs. This verifies partial
// signatures; threshold satisfaction is a separate policy operation.
bool VerifyPublicSignatures(wally_psbt* psbt)
{
    auto tx = Extract(psbt, WALLY_PSBT_EXTRACT_NON_FINAL);
    bool any = false;
    for (size_t index = 0; index < psbt->num_inputs; ++index) {
        auto& input = psbt->inputs[index];
        const uint32_t previous_index = psbt->version == 0 ? psbt->tx->inputs[index].index : input.index;
        const auto* previous_hash = psbt->version == 0 ? psbt->tx->inputs[index].txhash : input.txhash;
        const wally_tx_output* previous = nullptr;
        if (input.utxo) {
            std::array<unsigned char,32> txid;
            if (wally_tx_get_txid(input.utxo,txid.data(),txid.size()) != WALLY_OK ||
                std::memcmp(txid.data(),previous_hash,32) || previous_index >= input.utxo->num_outputs) return false;
            previous = &input.utxo->outputs[previous_index];
        }
        const auto* utxo = input.witness_utxo ? input.witness_utxo : previous;
        if (!utxo) return false;
        if (input.witness_utxo && previous && (utxo->satoshi != previous->satoshi ||
            utxo->script_len != previous->script_len || std::memcmp(utxo->script,previous->script,utxo->script_len))) return false;
        size_t type{0}; Ok(wally_scriptpubkey_get_type(utxo->script, utxo->script_len, &type));
        const auto* tap_sig = wally_map_get_integer(&input.psbt_fields, 0x13); // BIP371 PSBT_IN_TAP_KEY_SIG
        if (type == WALLY_SCRIPT_TYPE_P2TR) {
            if (!tap_sig || (tap_sig->value_len != 64 && tap_sig->value_len != 65) || utxo->script_len != 34) return false;
            const uint32_t sighash = tap_sig->value_len == 65 ? tap_sig->value[64] : 0;
            if (input.sighash && input.sighash != sighash) return false;
            const auto original = input.sighash; input.sighash = sighash;
            std::array<unsigned char,32> hash;
            const int result = wally_psbt_get_input_signature_hash(psbt,index,tx.get(),nullptr,0,0,hash.data(),hash.size());
            input.sighash = original;
            if (result != WALLY_OK || wally_ec_sig_verify(utxo->script+2,32,hash.data(),hash.size(),EC_FLAG_SCHNORR,tap_sig->value,64) != WALLY_OK) return false;
            any = true;
        } else {
            const unsigned char* script = utxo->script;
            size_t script_len = utxo->script_len;
            if (type == WALLY_SCRIPT_TYPE_P2SH) {
                const auto* redeem = wally_map_get_integer(&input.psbt_fields,0x04); // PSBT_IN_REDEEM_SCRIPT
                if (!redeem || script_len != 23) return false;
                std::array<unsigned char,20> hash;
                Ok(wally_hash160(redeem->value,redeem->value_len,hash.data(),hash.size()));
                if (std::memcmp(hash.data(),script+2,20)) return false;
                script=redeem->value; script_len=redeem->value_len;
                Ok(wally_scriptpubkey_get_type(script,script_len,&type));
            }
            bool witness = type == WALLY_SCRIPT_TYPE_P2WPKH || type == WALLY_SCRIPT_TYPE_P2WSH;
            if (type == WALLY_SCRIPT_TYPE_P2WSH) {
                const auto* witness_script = wally_map_get_integer(&input.psbt_fields,0x05); // PSBT_IN_WITNESS_SCRIPT
                if (!witness_script || script_len != 34) return false;
                std::array<unsigned char,32> hash;
                Ok(wally_sha256(witness_script->value,witness_script->value_len,hash.data(),hash.size()));
                if (std::memcmp(hash.data(),script+2,32)) return false;
                script=witness_script->value; script_len=witness_script->value_len;
                Ok(wally_scriptpubkey_get_type(script,script_len,&type));
            }
            if (!witness && !previous) return false; // legacy needs the full previous transaction
            if (!input.signatures.num_items) return false;
            std::vector<std::vector<unsigned char>> keys;
            if (type == WALLY_SCRIPT_TYPE_MULTISIG) {
                // Standard CHECKMULTISIG structure is first classified by the
                // provider. Walk only its canonical public-key pushes.
                for (size_t position=1; position+2<script_len;) {
                    const size_t length=script[position++];
                    if ((length!=33 && length!=65) || position+length>script_len-2) return false;
                    keys.emplace_back(script+position,script+position+length); position+=length;
                }
            } else if (type != WALLY_SCRIPT_TYPE_P2WPKH && type != WALLY_SCRIPT_TYPE_P2PKH) return false;
            for (size_t n = 0; n < input.signatures.num_items; ++n) {
                const auto& sig = input.signatures.items[n];
                if (sig.value_len < 2) return false;
                if (type == WALLY_SCRIPT_TYPE_MULTISIG) {
                    const bool member=std::any_of(keys.begin(),keys.end(),[&](const auto& key) {
                        return key.size()==sig.key_len && !std::memcmp(key.data(),sig.key,sig.key_len);
                    });
                    if (!member) return false;
                } else {
                    std::array<unsigned char,20> pubkey_hash;
                    Ok(wally_hash160(sig.key,sig.key_len,pubkey_hash.data(),pubkey_hash.size()));
                    const size_t offset=type==WALLY_SCRIPT_TYPE_P2WPKH ? 2 : 3;
                    if (script_len!=offset+20+(type==WALLY_SCRIPT_TYPE_P2PKH ? 2 : 0) ||
                        std::memcmp(pubkey_hash.data(),script+offset,20)) return false;
                }
                std::array<unsigned char,25> single_key_script;
                size_t written{0};
                const unsigned char* scriptcode=script;
                size_t scriptcode_len=script_len;
                if (type==WALLY_SCRIPT_TYPE_P2WPKH) {
                    Ok(wally_scriptpubkey_p2pkh_from_bytes(sig.key,sig.key_len,WALLY_SCRIPT_HASH160,single_key_script.data(),single_key_script.size(),&written));
                    scriptcode=single_key_script.data(); scriptcode_len=written;
                }
                const uint32_t sighash = sig.value[sig.value_len-1];
                if (input.sighash && input.sighash != sighash) return false;
                const auto original = input.sighash; input.sighash = sighash;
                std::array<unsigned char,32> hash;
                const int result = wally_psbt_get_input_signature_hash(psbt,index,tx.get(),scriptcode,scriptcode_len,0,hash.data(),hash.size());
                input.sighash = original;
                std::array<unsigned char,64> compact;
                if (result != WALLY_OK || wally_ec_sig_from_der(sig.value,sig.value_len-1,compact.data(),compact.size()) != WALLY_OK ||
                    wally_ec_sig_verify(sig.key,sig.key_len,hash.data(),hash.size(),EC_FLAG_ECDSA,compact.data(),compact.size()) != WALLY_OK) return false;
                any = true;
            }
        }
    }
    return any;
}

void SelfTest()
{
    wally_tx* tx_raw = nullptr; Ok(wally_tx_init_alloc(2,0,1,1,&tx_raw));
    Tx tx(tx_raw,wally_tx_free);
    std::array<unsigned char,32> hash{};
    hash.fill(0x11); // v2 uses an all-zero hash to denote an unset previous txid
    const auto script = Unhex("0014751e76e8199196d454941c45d1b3a323f1433bd6");
    Ok(wally_tx_add_raw_input(tx.get(),hash.data(),hash.size(),0,0xfffffffd,nullptr,0,nullptr,0));
    Ok(wally_tx_add_raw_output(tx.get(),9000,script.data(),script.size(),0));
    wally_psbt* raw = nullptr; Ok(wally_psbt_init_alloc(0,1,1,1,0,&raw));
    Psbt psbt(raw,wally_psbt_free); Ok(wally_psbt_set_global_tx(psbt.get(),tx.get()));
    const unsigned char key[]{0x70,0x01}, value[]{0x01,0x02,0x03};
    Ok(wally_map_add(&psbt->unknowns,key,sizeof(key),value,sizeof(value)));
    Ok(wally_map_add(&psbt->inputs[0].unknowns,key,sizeof(key),value,sizeof(value)));
    Ok(wally_map_add(&psbt->outputs[0].unknowns,key,sizeof(key),value,sizeof(value)));
    for (uint32_t version : {0U,2U}) {
        Ok(wally_psbt_set_version(psbt.get(),0,version));
        auto roundtrip = Parse(Base64(psbt.get()).c_str()); Require(roundtrip && roundtrip->version == version);
        Require(roundtrip->num_inputs == 1 && roundtrip->num_outputs == 1);
        Require((roundtrip->tx != nullptr) == (version == 0));
        for (const auto* map : {&roundtrip->unknowns,&roundtrip->inputs[0].unknowns,&roundtrip->outputs[0].unknowns}) {
            const auto* item = wally_map_get(map,key,sizeof(key));
            Require(item && item->value_len == sizeof(value) && !std::memcmp(item->value,value,sizeof(value)));
        }
        Ok(wally_psbt_combine(roundtrip.get(),psbt.get()));
        Require(roundtrip->unknowns.num_items == 1);
        const auto incomplete = wally_psbt_finalize(roundtrip.get(),WALLY_PSBT_FINALIZE_NO_CLEAR);
        size_t finalized = 1; Ok(wally_psbt_is_finalized(roundtrip.get(),&finalized));
        Require(incomplete == WALLY_OK && finalized == 0); // return zero does not mean complete
    }
    std::cout << "construction/version/unknown/merge/incomplete contracts passed\n";
}
} // namespace

int main(int argc, char** argv)
{
    try {
        Ok(wally_init(0));
        struct Cleanup { ~Cleanup() { wally_cleanup(0); } } cleanup;
        if (argc == 2 && std::string(argv[1]) == "self-test") { SelfTest(); return 0; }
        if (argc == 3 && std::string(argv[1]) == "xpub-regtest") {
            ext_key key; Ok(bip32_key_from_base58(argv[2],&key));
            Require(key.version==BIP32_VER_MAIN_PUBLIC);
            key.version=BIP32_VER_TEST_PUBLIC;
            char* encoded=nullptr; Ok(bip32_key_to_base58(&key,BIP32_FLAG_KEY_PUBLIC,&encoded));
            std::cout << encoded << '\n'; wally_free_string(encoded); return 0;
        }
        if (argc == 4 && (std::string(argv[1]) == "descriptor" || std::string(argv[1]) == "descriptor-regtest")) {
            wally_descriptor* raw = nullptr;
            const uint32_t network = std::string(argv[1]) == "descriptor-regtest" ? WALLY_NETWORK_BITCOIN_REGTEST : WALLY_NETWORK_BITCOIN_MAINNET;
            Ok(wally_descriptor_parse(argv[2],nullptr,network,32U<<16,&raw));
            std::unique_ptr<wally_descriptor,decltype(&wally_descriptor_free)> descriptor(raw,wally_descriptor_free);
            char* address = nullptr; const auto child = static_cast<uint32_t>(std::stoul(argv[3]));
            Ok(wally_descriptor_to_address(raw,0,0,child,0,&address));
            std::vector<unsigned char> script(10000); size_t written;
            Ok(wally_descriptor_to_script(raw,0,0,0,0,child,0,script.data(),script.size(),&written));
            std::cout << address << '\t' << Hex(script.data(),written) << '\n';
            wally_free_string(address); return 0;
        }
        if (argc == 5 && std::string(argv[1]) == "schnorr") {
            auto key=Unhex(argv[2]), hash=Unhex(argv[3]), sig=Unhex(argv[4]);
            const bool valid = wally_ec_sig_verify(key.data(),key.size(),hash.data(),hash.size(),EC_FLAG_SCHNORR,sig.data(),sig.size()) == WALLY_OK;
            std::cout << (valid ? "valid" : "invalid") << '\n'; return 0;
        }
        if (argc < 3 || argc > 4) return 2;
        const std::string operation(argv[1]); auto psbt=Parse(argv[2]);
        if (operation == "parse") { std::cout << (psbt ? "accepted" : "rejected") << '\n'; return 0; }
        Require(psbt != nullptr);
        if (operation == "roundtrip") { std::cout << Base64(psbt.get()) << '\n'; return 0; }
        if (operation == "combine" && argc == 4) {
            auto other=Parse(argv[3]); Require(other != nullptr);
            Ok(wally_psbt_combine(psbt.get(),other.get())); std::cout << Base64(psbt.get()) << '\n'; return 0;
        }
        if (operation == "verify") { std::cout << (VerifyPublicSignatures(psbt.get()) ? "valid" : "invalid") << '\n'; return 0; }
        if (operation == "verify-changed-output") {
            if (psbt->version == 0) ++psbt->tx->outputs[0].satoshi;
            else ++psbt->outputs[0].amount;
            std::cout << (VerifyPublicSignatures(psbt.get()) ? "valid" : "invalid") << '\n'; return 0;
        }
        if (operation == "finalize") {
            Ok(wally_psbt_finalize(psbt.get(),WALLY_PSBT_FINALIZE_NO_CLEAR));
            size_t finalized=0; Ok(wally_psbt_is_finalized(psbt.get(),&finalized));
            if (!finalized) { std::cout << "incomplete\n"; return 0; }
            auto tx=Extract(psbt.get(),0); char* hex=nullptr;
            Ok(wally_tx_to_hex(tx.get(),WALLY_TX_FLAG_USE_WITNESS,&hex));
            std::cout << hex << '\n'; wally_free_string(hex); return 0;
        }
        return 2;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
