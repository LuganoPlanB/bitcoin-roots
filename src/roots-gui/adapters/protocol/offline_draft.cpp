// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/offline_draft.h"
#include <wally_core.h>
#include <wally_bip32.h>
#include <wally_address.h>
#include <wally_descriptor.h>
#include <wally_crypto.h>
#include <wally_psbt.h>
#include <wally_script.h>
#include <wally_transaction.h>
#include <algorithm>
#include <array>
#include <set>

namespace roots::gui::protocol {
namespace {
using Tx=std::unique_ptr<wally_tx,decltype(&wally_tx_free)>;
using Psbt=std::unique_ptr<wally_psbt,decltype(&wally_psbt_free)>;
constexpr uint64_t MAX_MONEY=2100000000000000;
struct Failure {const char* reason;};
void Need(bool ok,const char* reason)
{
    if (!ok) throw Failure{reason};
}
void Ok(int result,const char* reason)
{
    Need(result==WALLY_OK,reason);
}
const unsigned char* Bytes(std::string_view text)
{
    return reinterpret_cast<const unsigned char*>(text.data());
}
std::string_view Bytes(const unsigned char* data,size_t len)
{
    return len ? std::string_view(reinterpret_cast<const char*>(data),len) : std::string_view{};
}
std::string Decode(std::string_view hex,size_t max)
{
    Need(hex.size()%2==0 && hex.size()/2<=max && hex.find('\0')==hex.npos,"Malformed or oversized public script.");
    if (hex.empty()) return {};
    const std::string nul(hex);
    std::string decoded(hex.size()/2,'\0');
    size_t n{};
    Ok(wally_hex_to_bytes(nul.c_str(),reinterpret_cast<unsigned char*>(decoded.data()),decoded.size(),&n),"Malformed public script.");
    Need(n==decoded.size() && TransactionHex(decoded)==hex,"Public script is not canonical hex.");
    return decoded;
}
Tx ParseRaw(const TransactionDocument& document)
{
    wally_tx* raw{};
    const auto rc=wally_tx_from_bytes(Bytes(document.wire),document.wire.size(),WALLY_TX_FLAG_USE_WITNESS,&raw);
    Tx tx(raw,&wally_tx_free);
    Ok(rc,"The previous transaction is malformed.");
    Need(bool(tx),"The previous transaction is unavailable.");
    return tx;
}
Psbt ParsePsbt(const TransactionDocument& document)
{
    Need(document.psbt_version && document.transaction && document.transaction->locktime
         && !document.transaction->inputs.empty() && document.transaction->inputs.size()<=MAX_OFFLINE_INPUTS
         && !document.transaction->outputs.empty(),"Expected a bounded PSBT with a definite unsigned transaction.");
    wally_psbt* raw{};
    const auto code=wally_psbt_from_bytes(Bytes(document.wire),document.wire.size(),
        WALLY_PSBT_PARSE_FLAG_STRICT|WALLY_PSBT_PARSE_FLAG_COMPLETE,&raw);
    Psbt psbt(raw,&wally_psbt_free);
    Ok(code,"The PSBT failed strict provider parsing.");
    Need(bool(psbt),"The PSBT could not be opened.");
    return psbt;
}
Tx Unsigned(const wally_psbt& psbt)
{
    wally_tx* raw{};
    const auto result=wally_psbt_extract(&psbt,WALLY_PSBT_EXTRACT_NON_FINAL,&raw);
    Tx tx(raw,&wally_tx_free);
    Ok(result,"The PSBT has no exact unsigned transaction.");
    Need(bool(tx),"The PSBT has no exact unsigned transaction.");
    return tx;
}
std::string Serialize(const wally_tx& tx,uint32_t flags)
{
    size_t length{},written{};
    Ok(wally_tx_get_length(&tx,flags,&length),"The transaction could not be measured.");
    Need(length>0 && length<=MAX_TRANSACTION_WIRE,"The resulting transaction exceeds the offline size limit.");
    std::string data(length,'\0');
    Ok(wally_tx_to_bytes(&tx,flags,reinterpret_cast<unsigned char*>(data.data()),length,&written),"The transaction could not be serialized.");
    Need(written==length,"Unexpected transaction length.");
    return data;
}
std::string Serialize(const wally_psbt& psbt)
{
    size_t length{},written{};
    Ok(wally_psbt_get_length(&psbt,0,&length),"The PSBT could not be measured.");
    Need(length>0 && length<=MAX_TRANSACTION_WIRE,"The resulting PSBT exceeds the offline size limit.");
    std::string data(length,'\0');
    Ok(wally_psbt_to_bytes(&psbt,0,reinterpret_cast<unsigned char*>(data.data()),length,&written),"The PSBT could not be serialized.");
    Need(written==length,"Unexpected PSBT length.");
    return data;
}
std::shared_ptr<const TransactionDocument> Reparse(std::string data)
{
    auto document=ParseTransactionDocument(data,DisplayNetwork::Unknown).document;
    Need(bool(document),"The resulting document did not satisfy the offline codec.");
    return document;
}
size_t ScriptType(std::string_view script)
{
    size_t type{};
    if (wally_scriptpubkey_get_type(Bytes(script),script.size(),&type)!=WALLY_OK) return 0;
    return type;
}
bool Hash160(std::string_view bytes,std::string_view expected)
{
    std::array<unsigned char,20> hash{};
    return wally_hash160(Bytes(bytes),bytes.size(),hash.data(),hash.size())==WALLY_OK && Bytes(hash.data(),hash.size())==expected;
}
bool Sha256(std::string_view bytes,std::string_view expected)
{
    std::array<unsigned char,32> hash{};
    return wally_sha256(Bytes(bytes),bytes.size(),hash.data(),hash.size())==WALLY_OK && Bytes(hash.data(),hash.size())==expected;
}
struct ScriptPolicy {
    size_t type{},threshold{1};
    std::string_view script;
    std::set<std::string_view> signers;
};
ScriptPolicy Policy(std::string_view root,std::string_view redeem,std::string_view witness)
{
    auto script=root;
    auto type=ScriptType(script);
    const bool nested=type==WALLY_SCRIPT_TYPE_P2SH;
    if (nested) {
        Need(script.size()==23 && !redeem.empty() && Hash160(redeem,script.substr(2,20)),"Redeem script does not match the exact previous output.");
        script=redeem;
        type=ScriptType(script);
    } else Need(redeem.empty(),"Unexpected redeem script.");
    const bool witness_script=type==WALLY_SCRIPT_TYPE_P2WSH;
    if (witness_script) {
        Need(script.size()==34 && !witness.empty() && Sha256(witness,script.substr(2,32)),"Witness script does not match the committed witness program.");
        script=witness;
        type=ScriptType(script);
        Need(type==WALLY_SCRIPT_TYPE_MULTISIG,"Unsupported witness script policy.");
    } else Need(witness.empty(),"Unexpected witness script.");
    if (type==WALLY_SCRIPT_TYPE_P2WPKH)
        Need(script.size()==22,"Invalid witness key-hash script.");
    else if (type==WALLY_SCRIPT_TYPE_P2PKH)
        Need(!nested && script.size()==25,"Unsupported wrapped legacy single-key script.");
    else if (type==WALLY_SCRIPT_TYPE_P2TR)
        Need(!nested && script.size()==34,"Unsupported wrapped Taproot policy.");
    else Need(type==WALLY_SCRIPT_TYPE_MULTISIG && (nested || witness_script),"Unsupported public signature policy.");
    ScriptPolicy result{type,1,script,{}};
    if (type!=WALLY_SCRIPT_TYPE_MULTISIG) return result;
    Need(script.size()>3,"Empty multisig policy.");
    result.threshold=uint8_t(script.front())-0x50;
    for (size_t at=1;at<script.size()-2;) {
        const size_t n=uint8_t(script[at++]);
        Need((n==33 || n==65) && n<=script.size()-2-at,"Nonstandard multisig key push.");
        const auto key=script.substr(at,n);
        Need(result.signers.insert(key).second,"Duplicate key in multisig policy.");
        at+=n;
    }
    Need(result.threshold>=1 && result.threshold<=result.signers.size() && result.signers.size()<=15
         && uint8_t(script[script.size()-2])==0x50+result.signers.size(),"Unsupported multisig threshold.");
    return result;
}
uint64_t Total(uint64_t first,uint64_t second)
{
    Need(first<=MAX_MONEY && second<=MAX_MONEY-first,"Value exceeds the Bitcoin monetary bound.");
    return first+second;
}
Tx Previous(const OfflineInput& selected,DisplayNetwork network)
{
    Need(selected.proof && selected.value && selected.proof->observed_ms>0,"Previous output and wallet policy evidence are Unknown.");
    const auto& proof=*selected.proof;
    Need(proof.previous_hex.size()<=MAX_OFFLINE_PREVIOUS*2,"Previous transaction exceeds the offline cache limit.");
    const auto previous=ParseTransactionDocument(proof.previous_hex,network).document;
    Need(previous && previous->transaction && previous->transaction->txid==selected.point.txid
         && selected.point.index<previous->transaction->outputs.size(),"Previous transaction ID or output is Unknown.");
    const auto& output=previous->transaction->outputs[selected.point.index];
    Need(output.script==selected.script && output.value==selected.value,"Cached outpoint value or script conflicts with its previous transaction.");
    const auto branch=DerivePublicDescriptor(proof.descriptor,DocumentChain(network),0);
    Need(branch && !branch->ranged && branch->script==output.script,"Cached public descriptor does not prove this exact outpoint script.");
    const auto root=Decode(output.script,10000);
    const auto redeem=Decode(proof.redeem_script,10000);
    const auto witness=Decode(proof.witness_script,10000);
    const auto policy=Policy(root,redeem,witness);
    Need(policy.threshold==branch->choice.required,"Cached script and descriptor threshold disagree.");
    return ParseRaw(*previous);
}
std::string_view Field(const wally_psbt_input& input,uint32_t type)
{
    const auto* entry=wally_map_get_integer(&input.psbt_fields,type);
    return entry ? Bytes(entry->value,entry->value_len) : std::string_view{};
}
void SignatureHash(wally_psbt& psbt,size_t i,const wally_tx& tx,std::string_view script,uint32_t mode,unsigned char* hash)
{
    auto& input=psbt.inputs[i];
    Need(!input.sighash || input.sighash==mode,"Signature conflicts with the PSBT sighash policy.");
    const auto original=input.sighash;
    // libwally reads the sighash mode from this input while computing the hash.
    input.sighash=mode;
    const auto result=wally_psbt_get_input_signature_hash(&psbt,i,&tx,script.empty()?nullptr:Bytes(script),script.size(),0,hash,32);
    input.sighash=original;
    Ok(result,"Signature hash unavailable for this exact transaction.");
}
struct PublicOrigin {
    std::string key,fingerprint;
    std::vector<uint32_t> path;
    bool taproot{};
};
std::vector<PublicOrigin> Origins(std::string_view descriptor, DisplayNetwork network, uint32_t index)
{
    const auto chain=DocumentChain(network);
    const auto verified=DerivePublicDescriptor(descriptor,chain,index);
    Need(bool(verified),"Public descriptor origin is unavailable.");
    const uint32_t encoding=network==DisplayNetwork::Mainnet ? WALLY_NETWORK_BITCOIN_MAINNET
        : network==DisplayNetwork::Regtest ? WALLY_NETWORK_BITCOIN_REGTEST : WALLY_NETWORK_BITCOIN_TESTNET;
    wally_descriptor* raw{};
    const auto result=wally_descriptor_parse(verified->descriptor.c_str(),nullptr,encoding,
        (8U<<WALLY_MINISCRIPT_DEPTH_SHIFT)|WALLY_MINISCRIPT_REQUIRE_CHECKSUM,&raw);
    std::unique_ptr<wally_descriptor,decltype(&wally_descriptor_free)> parsed(raw,&wally_descriptor_free);
    Ok(result,"Could not open the verified public descriptor.");
    const bool taproot=verified->choice.script==ScriptTemplate::Taproot;
    std::vector<PublicOrigin> origins;
    for (size_t i=0;i<verified->signers.size();++i) {
        const auto signer=InspectPublicSigner(verified->signers[i],chain,taproot).signer;
        Need(bool(signer),"Could not read the verified public signer.");
        if (signer->fingerprint.empty() && !taproot) continue;
        ext_key key{};
        Ok(wally_descriptor_derive_bip32_key(parsed.get(),i,0,0,index,BIP32_FLAG_KEY_PUBLIC,&key),
            "Could not derive the public signer.");
        PublicOrigin origin{std::string(Bytes(key.pub_key+(taproot?1:0),taproot?32:33)),
            Decode(signer->fingerprint,4),{},taproot};
        // No derivation information is added when the descriptor does not prove it.
        if (!origin.fingerprint.empty()) {
            auto path=signer->origin_path+signer->derivation_suffix;
            if (path!="m") {
                std::array<uint32_t,BIP32_PATH_MAX_LEN> parts{};
                size_t count{};
                Ok(bip32_path_from_str(path.c_str(),index,0,BIP32_FLAG_STR_WILDCARD|BIP32_FLAG_ALLOW_UPPER,
                    parts.data(),parts.size(),&count),"Invalid public BIP32 origin path.");
                origin.path.assign(parts.begin(),parts.begin()+count);
            }
        }
        origins.push_back(std::move(origin));
    }
    return origins;
}
void InputOrigins(wally_psbt_input& input,const std::vector<PublicOrigin>& origins)
{
    for (const auto& origin:origins) {
        const auto* path=origin.path.empty()?nullptr:origin.path.data();
        if (origin.taproot) {
            Ok(wally_psbt_input_set_taproot_internal_key(&input,Bytes(origin.key),origin.key.size()),
                "Could not attach Taproot public internal key.");
            if (!origin.fingerprint.empty())
                Ok(wally_psbt_input_taproot_keypath_add(&input,Bytes(origin.key),origin.key.size(),nullptr,0,
                    Bytes(origin.fingerprint),origin.fingerprint.size(),path,origin.path.size()),"Could not attach Taproot public input origin.");
        } else Ok(wally_psbt_input_keypath_add(&input,Bytes(origin.key),origin.key.size(),Bytes(origin.fingerprint),
            origin.fingerprint.size(),path,origin.path.size()),"Could not attach public input origin.");
    }
}
void OutputOrigins(wally_psbt_output& output,const std::vector<PublicOrigin>& origins)
{
    for (const auto& origin:origins) {
        const auto* path=origin.path.empty()?nullptr:origin.path.data();
        if (origin.taproot) {
            Ok(wally_psbt_output_set_taproot_internal_key(&output,Bytes(origin.key),origin.key.size()),
                "Could not attach Taproot public change key.");
            if (!origin.fingerprint.empty())
                Ok(wally_psbt_output_taproot_keypath_add(&output,Bytes(origin.key),origin.key.size(),nullptr,0,
                    Bytes(origin.fingerprint),origin.fingerprint.size(),path,origin.path.size()),"Could not attach Taproot public change origin.");
        } else Ok(wally_psbt_output_keypath_add(&output,Bytes(origin.key),origin.key.size(),Bytes(origin.fingerprint),
            origin.fingerprint.size(),path,origin.path.size()),"Could not attach public change origin.");
    }
}
size_t SignatureCount(const wally_psbt& psbt)
{
    size_t count{};
    for (size_t i=0;i<psbt.num_inputs;++i)
        count+=psbt.inputs[i].signatures.num_items+size_t(!Field(psbt.inputs[i],0x13).empty());
    return count;
}
bool VerifyInput(wally_psbt& psbt,const wally_tx& tx,size_t i,uint64_t& in_total,InputSignatureProgress* progress=nullptr)
{
    const auto& input=psbt.inputs[i];
    const auto& spend=tx.inputs[i];
    Need(Field(input,7).empty() && !input.final_witness && !input.taproot_leaf_signatures.num_items
         && !input.taproot_leaf_scripts.num_items,"Import partial signatures on a supported policy. Pre-finalized or Taproot script-path inputs require independent verification.");
    std::array<unsigned char,32> hash{};
    Need(input.utxo && spend.index<input.utxo->num_outputs
         && wally_tx_get_txid(input.utxo,hash.data(),hash.size())==WALLY_OK
         && std::ranges::equal(hash,spend.txhash),"Fee and previous output are Unknown. A matching full previous transaction is required.");
    const auto& prev=input.utxo->outputs[spend.index];
    const auto root=Bytes(prev.script,prev.script_len);
    Need(!input.witness_utxo || (input.witness_utxo->satoshi==prev.satoshi
          && Bytes(input.witness_utxo->script,input.witness_utxo->script_len)==root),"Witness and full previous outputs conflict.");
    in_total=Total(in_total,prev.satoshi);
    const auto policy=Policy(root,Field(input,4),Field(input,5));
    if (progress) progress->threshold=policy.threshold;
    const auto tap_signature=Field(input,0x13);
    if (policy.type==WALLY_SCRIPT_TYPE_P2TR) {
        Need(!input.signatures.num_items && (input.sighash==0 || input.sighash==1),"Unsupported Taproot signature or sighash policy.");
        if (!tap_signature.empty()) {
            const uint32_t mode=tap_signature.size()==65 ? uint8_t(tap_signature.back()) : 0;
            Need(tap_signature.size()==64 || (tap_signature.size()==65 && mode==1),"Taproot signatures must commit to all outputs.");
            SignatureHash(psbt,i,tx,{},mode,hash.data());
            Ok(wally_ec_sig_verify(prev.script+2,32,hash.data(),hash.size(),EC_FLAG_SCHNORR,
                Bytes(tap_signature),64),"Taproot signature does not validate for this exact transaction.");
        }
        if (progress) progress->signers.push_back({TransactionHex(root.substr(2)),!tap_signature.empty()});
        return !tap_signature.empty();
    }
    Need(tap_signature.empty() && (input.sighash==0 || input.sighash==1),"Unsupported signature or sighash policy.");
    std::set<std::string_view> verified_keys;
    for (size_t j=0;j<input.signatures.num_items;++j) {
        const auto& sig=input.signatures.items[j];
        const auto key=Bytes(sig.key,sig.key_len);
        const auto serialized=Bytes(sig.value,sig.value_len);
        Need(serialized.size()>=2 && uint8_t(serialized.back())==1,"Only SIGHASH_ALL ECDSA signatures are supported.");
        auto scriptcode=policy.script;
        std::array<unsigned char,25> p2pkh{};
        if (policy.type==WALLY_SCRIPT_TYPE_MULTISIG) Need(policy.signers.contains(key),"Signature key is outside this multisig policy.");
        else {
            const bool witness=policy.type==WALLY_SCRIPT_TYPE_P2WPKH;
            Need(Hash160(key,policy.script.substr(witness?2:3,20)),"Signature key is outside this key-hash policy.");
            if (witness) {
                size_t written{};
                Ok(wally_scriptpubkey_p2pkh_from_bytes(sig.key,sig.key_len,WALLY_SCRIPT_HASH160,p2pkh.data(),p2pkh.size(),&written),
                    "Could not derive the signed script code.");
                scriptcode=Bytes(p2pkh.data(),written);
            }
        }
        SignatureHash(psbt,i,tx,scriptcode,1,hash.data());
        std::array<unsigned char,64> compact{};
        Ok(wally_ec_sig_from_der(sig.value,sig.value_len-1,compact.data(),compact.size()),"Invalid ECDSA signature encoding.");
        Ok(wally_ec_sig_verify(sig.key,sig.key_len,hash.data(),hash.size(),EC_FLAG_ECDSA,compact.data(),compact.size()),
            "Signature does not validate for this exact transaction.");
        Need(verified_keys.insert(key).second,"Duplicate signature key.");
    }
    if (progress) {
        const auto& keys=policy.signers.empty() ? verified_keys : policy.signers;
        for (const auto key:keys) progress->signers.push_back({TransactionHex(key),verified_keys.contains(key)});
    }
    return verified_keys.size()>=policy.threshold;
}
size_t VerifiedSatisfactions(wally_psbt& psbt,const wally_tx& tx)
{
    size_t satisfied{};
    uint64_t in_total{},out_total{};
    for (size_t i=0;i<tx.num_outputs;++i) out_total=Total(out_total,tx.outputs[i].satoshi);
    for (size_t i=0;i<tx.num_inputs;++i) satisfied+=VerifyInput(psbt,tx,i,in_total);
    Need(in_total>=out_total,"Input value does not cover the ordered outputs.");
    return satisfied;
}
}

size_t InputSignatureProgress::Verified() const
{
    return std::ranges::count_if(signers,[](const auto& signer){return signer.verified;});
}
bool InputSignatureProgress::Satisfied() const
{
    return unknown.empty() && threshold && Verified()>=threshold;
}
bool SignatureProgress::Complete() const
{
    return error.empty() && !inputs.empty() && std::ranges::all_of(inputs,&InputSignatureProgress::Satisfied);
}
SignatureProgress InspectSignatures(const TransactionDocument& document)
{
    try {
        Need(document.psbt_version && document.transaction,"A PSBT is required to inspect signature progress.");
        if (auto conflict=CompareTransactionReview(document,document);!conflict.empty()) return {{},std::move(conflict)};
        auto psbt=ParsePsbt(document);
        auto tx=Unsigned(*psbt);
        SignatureProgress result;
        result.inputs.resize(tx->num_inputs);
        uint64_t in_total{},out_total{};
        for (size_t i=0;i<tx->num_outputs;++i) out_total=Total(out_total,tx->outputs[i].satoshi);
        for (size_t i=0;i<tx->num_inputs;++i) {
            auto& input=result.inputs[i];
            try {VerifyInput(*psbt,*tx,i,in_total,&input);}
            catch (Failure failure) {input.signers.clear();input.unknown=failure.reason;}
        }
        if (std::ranges::all_of(result.inputs,[](const auto& input){return input.unknown.empty();}) && in_total<out_total)
            result.error="Input value does not cover the ordered outputs.";
        return result;
    } catch (Failure failure) {return {{},failure.reason};}
    catch (...) {return {{},"Signature inspection exceeded available resources."};}
}

bool ValidOfflineCoin(const OfflineInput& selected,DisplayNetwork network)
{
    try {return bool(Previous(selected,network));}
    catch (...) {return false;}
}

OfflineResult ConstructOfflineDraft(const OfflineDraft& request)
{
    try {
        Need(!DocumentChain(request.network).empty(),"Choose the exact network of the cached wallet.");
        Need(!request.inputs.empty() && request.inputs.size()<=MAX_OFFLINE_INPUTS,"A bounded set of exact pinned inputs is required.");
        Need(!request.recipients.empty() && request.recipients.size()<=16 && request.fee.sats()>=0,"Specify a recipient and an exact nonnegative fee.");
        OfflineReviewEvidence evidence{request.network,{}, {},request.fee};
        std::vector<Tx> previous;
        std::set<std::pair<std::string,uint32_t>> distinct;
        uint64_t total_in{},total_out{};
        wally_tx* raw_tx{};
        const auto allocated=wally_tx_init_alloc(2,0,request.inputs.size(),request.recipients.size()+1,&raw_tx);
        Tx tx(raw_tx,&wally_tx_free);
        Ok(allocated,"Could not allocate the bounded unsigned transaction.");
        for (const auto& input:request.inputs) {
            Need(distinct.emplace(input.point.txid.hex(),input.point.index).second,"Duplicate selected outpoint.");
            previous.push_back(Previous(input,request.network));
            evidence.inputs.push_back({input.point,*input.value,input.proof->descriptor});
            total_in=Total(total_in,uint64_t(input.value->sats()));
            auto hash=Decode(input.point.txid.hex(),32);
            std::ranges::reverse(hash);
            Ok(wally_tx_add_raw_input(tx.get(),Bytes(hash),hash.size(),input.point.index,0xfffffffd,nullptr,0,nullptr,0),
                "Could not add the exact pinned input.");
        }
        const auto output=[&](uint64_t value,std::string_view script) {
            const auto raw=Decode(script,10000);
            Need(!raw.empty(),"Empty output script.");
            Ok(wally_tx_add_raw_output(tx.get(),value,Bytes(raw),raw.size(),0),"Could not add the exact output.");
        };
        for (const auto& recipient:request.recipients) {
            Need(recipient.amount.sats()>0,"Every recipient needs a positive amount.");
            const auto script=AddressScript(recipient.address,request.network);
            Need(bool(script),"Recipient address is not canonical for the wallet network.");
            evidence.recipients.push_back({recipient.amount,*script});
            total_out=Total(total_out,uint64_t(recipient.amount.sats()));
            output(uint64_t(recipient.amount.sats()),*script);
        }
        const auto consumed=Total(total_out,uint64_t(request.fee.sats()));
        Need(total_in>=consumed,"Cached input value does not cover recipients and fee.");
        if (const auto change=total_in-consumed) {
            Need(request.change && request.change->observed_ms>0,"Change is Unknown. A separately cached internal wallet descriptor is required.");
            const auto descriptor=DerivePublicDescriptor(request.change->descriptor,DocumentChain(request.network),request.change->index);
            Need(bool(descriptor),"The cached internal descriptor cannot prove this change script.");
            output(change,descriptor->script);
            evidence.change=request.change;
        }
        wally_psbt* raw{};
        const auto initialized=wally_psbt_init_alloc(0,tx->num_inputs,tx->num_outputs,0,0,&raw);
        Psbt psbt(raw,&wally_psbt_free);
        Ok(initialized,"Could not allocate the unsigned PSBT.");
        Ok(wally_psbt_set_global_tx(psbt.get(),tx.get()),"Could not set the exact unsigned transaction.");
        for (size_t i=0;i<request.inputs.size();++i) {
            const auto& cached=request.inputs[i];
            auto& input=psbt->inputs[i];
            Ok(wally_psbt_input_set_utxo(&input,previous[i].get()),"Could not attach the full previous transaction.");
            const auto redeem=Decode(cached.proof->redeem_script,10000);
            const auto witness=Decode(cached.proof->witness_script,10000);
            if (!redeem.empty()) Ok(wally_psbt_input_set_redeem_script(&input,Bytes(redeem),redeem.size()),"Could not attach the proved redeem script.");
            if (!witness.empty()) Ok(wally_psbt_input_set_witness_script(&input,Bytes(witness),witness.size()),"Could not attach the proved witness script.");
            const auto& prev=previous[i]->outputs[cached.point.index];
            const auto root=Bytes(prev.script,prev.script_len);
            const auto type=ScriptType(redeem.empty()?root:std::string_view(redeem));
            if (type==WALLY_SCRIPT_TYPE_P2WPKH || type==WALLY_SCRIPT_TYPE_P2WSH || type==WALLY_SCRIPT_TYPE_P2TR)
                Ok(wally_psbt_input_set_witness_utxo(&input,&prev),"Could not attach the proved witness output.");
            InputOrigins(input,Origins(cached.proof->descriptor,request.network,0));
            if (type!=WALLY_SCRIPT_TYPE_P2TR) Ok(wally_psbt_input_set_sighash(&input,1),"Could not fix the SIGHASH_ALL policy.");
        }
        if (total_in>consumed)
            OutputOrigins(psbt->outputs[tx->num_outputs-1],Origins(request.change->descriptor,request.network,request.change->index));
        Need(VerifiedSatisfactions(*psbt,*tx)==0,"An unsigned draft must contain no signature satisfaction.");
        auto made=Reparse(Serialize(*psbt));
        Need(made->transaction->fee==request.fee,"The generated fee differs from the exact draft fee.");
        return {std::move(made),{},0,request.inputs.size(),std::move(evidence)};
    } catch (Failure f) {return {{},f.reason};}
    catch (...) {return {{},"The offline draft exceeded available resources."};}
}

namespace {
enum class MergeIntent { NewSignatures, Enrich, CollectVerified };
OfflineResult CombineVerified(const TransactionDocument& current,const TransactionDocument& external,MergeIntent intent)
{
    try {
        Need(current.psbt_version && external.psbt_version,"Choose an externally signed PSBT.");
        if (auto change=CompareTransactionReview(current,external,true); !change.empty())
            return {{},change,0,0,{},change};
        auto base=ParsePsbt(current);
        auto incoming=ParsePsbt(external);
        Need(base->version==incoming->version,"The transaction and supplied policy agree. Merge requires the working PSBT version.");
        const auto tx=Unsigned(*base);
        const auto signed_tx=Unsigned(*incoming);
        Need(Serialize(*tx,0)==Serialize(*signed_tx,0),"The signed PSBT refers to a different unsigned transaction.");
        auto original=VerifiedSatisfactions(*base,*tx);
        auto combined=MergePsbtMaps(current,external);
        Need(bool(combined),"Conflicting PSBT fields or merged size limit. Original bytes retained.");
        auto merged=Reparse(current.format==DocumentFormat::PsbtBase64 ? TransactionBase64(*combined) : std::move(*combined));
        auto checked=ParsePsbt(*merged);
        Need(checked->num_inputs==base->num_inputs && Serialize(*Unsigned(*checked),0)==Serialize(*tx,0),
             "The combined PSBT changed the unsigned transaction.");
        if (auto change=CompareTransactionReview(current,*merged); !change.empty())
            return {{},change,0,0,{},change};
        const auto valid=VerifiedSatisfactions(*checked,*tx);
        const auto before=SignatureCount(*base),after=SignatureCount(*checked);
        if (intent==MergeIntent::NewSignatures)
            Need(valid>=original && after>before,"The signer contributed no new verified signature.");
        else if (intent==MergeIntent::Enrich)
            Need(after==before,"The enrichment reply supplied unexpected signatures.");
        return {std::move(merged),{},valid,tx->num_inputs};
    } catch (Failure f) {return {{},f.reason};}
    catch (...) {return {{},"The PSBT merge exceeded available resources."};}
}
}

OfflineResult MergeOfflineSignatures(const TransactionDocument& current,const TransactionDocument& external)
{
    return CombineVerified(current,external,MergeIntent::NewSignatures);
}

OfflineResult CombineVerifiedCollections(const TransactionDocument& current,const TransactionDocument& signed_reply)
{
    return CombineVerified(current,signed_reply,MergeIntent::CollectVerified);
}

OfflineResult ApplyDaemonPsbt(const TransactionDocument& current,const TransactionDocument& reply,bool signing)
{
    if (std::ranges::any_of(reply.input_policy,[](const auto& policy){return policy.final_fields;}))
        return {{},"The daemon returned final scripts for an unfinalized PSBT request."};
    return CombineVerified(current,reply,signing ? MergeIntent::NewSignatures : MergeIntent::Enrich);
}

OfflineResult FinalizeOfflineDraft(const TransactionDocument& current)
{
    try {
        Need(CompareTransactionReview(current,current).empty(),"The PSBT contains contradictory review fields.");
        auto psbt=ParsePsbt(current);
        auto tx=Unsigned(*psbt);
        const auto valid=VerifiedSatisfactions(*psbt,*tx);
        Need(valid==tx->num_inputs,"One or more inputs lack a complete, verified signature policy.");
        Ok(wally_psbt_finalize(psbt.get(),WALLY_PSBT_FINALIZE_NO_CLEAR),"The provider could not assemble this supported policy.");
        size_t finalized{};
        Ok(wally_psbt_is_finalized(psbt.get(),&finalized),"The provider did not confirm finalization.");
        Need(finalized!=0,"The provider did not confirm complete finalization.");
        wally_tx* raw{};
        const auto extracted=wally_psbt_extract(psbt.get(),WALLY_PSBT_EXTRACT_FINAL,&raw);
        Tx result(raw,&wally_tx_free);
        Ok(extracted,"The provider could not extract the locally finalized transaction.");
        Need(bool(result),"The provider returned no finalized transaction.");
        auto decoded=Reparse(Serialize(*result,WALLY_TX_FLAG_USE_WITNESS));
        Need(TransactionCommitment(current)==TransactionCommitment(*decoded),"Finalization changed the transaction commitment.");
        return {std::move(decoded),{},valid,tx->num_inputs};
    } catch (Failure f) {return {{},f.reason};}
    catch (...) {return {{},"Finalization exceeded available resources."};}
}

OfflineReview InspectOfflineReview(const TransactionDocument& document,const OfflineReviewEvidence& evidence,
    std::optional<size_t> change_position)
{
    try {
        Need(!DocumentChain(evidence.network).empty(),"Wallet network is Unknown.");
        Need(document.psbt_version && document.transaction,"A working PSBT and independent payment evidence are required.");
        Need(!document.modifiable.value_or(0),"Transaction fields are marked modifiable.");
        Need(CompareTransactionReview(document,document).empty(),"The PSBT contains contradictory review fields.");
        const auto& tx=*document.transaction;
        Need(tx.fee && tx.fee==evidence.fee,"Fee or previous output is Unknown or differs from the recorded payment.");
        Need(tx.inputs.size()==evidence.inputs.size(),"The ordered input count differs from the pinned set.");
        for (size_t i=0;i<evidence.inputs.size();++i) {
            const auto& plan=evidence.inputs[i];
            const auto& input=tx.inputs[i];
            Need(input.outpoint==plan.point && input.evidence==PrevoutEvidence::TxidMatched && input.previous_value==plan.value,
                "The previous output differs from the independently pinned outpoint or value.");
            const auto descriptor=DerivePublicDescriptor(plan.descriptor,DocumentChain(evidence.network),0);
            Need(descriptor && !descriptor->ranged && descriptor->script==input.previous_script,
                "The previous output script is outside the independently pinned wallet policy.");
        }
        const auto count=evidence.recipients.size();
        Need(tx.outputs.size()==count+size_t(bool(evidence.change)),"Change is Unknown or the ordered output count changed.");
        const auto position=change_position.value_or(count);
        Need(!change_position || (evidence.change && position<tx.outputs.size()),"The reviewed change position is invalid.");
        for (size_t i=0;i<count;++i)
            Need(evidence.recipients[i]==tx.outputs[i+size_t(evidence.change && i>=position)],
                "A recipient amount, destination or order differs from the recorded payment.");
        if (evidence.change) {
            const auto& proof=*evidence.change;
            const auto descriptor=DerivePublicDescriptor(proof.descriptor,DocumentChain(evidence.network),proof.index);
            Need(proof.observed_ms>0 && descriptor && descriptor->script==tx.outputs.at(position).script,
                "Change is Unknown. The output does not match the independently read internal descriptor.");
        }
        auto psbt=ParsePsbt(document);
        VerifiedSatisfactions(*psbt,*Unsigned(*psbt));
        auto commitment=TransactionCommitment(document);
        Need(bool(commitment),"An exact transaction commitment is unavailable.");
        return {std::move(commitment),evidence.change ? std::optional{position} : std::nullopt,{}};
    } catch (Failure f) {return {{},{},f.reason};}
    catch (...) {return {{},{},"The local review exceeded available resources."};}
}

FinalizationResult FinalizeReviewedDraft(const TransactionDocument& original,const TransactionDocument& collected,
    const OfflineReviewEvidence& proof,std::optional<size_t> change_position)
{
    try {
        Need(original.psbt_version && original.psbt_version==collected.psbt_version,
            "Finalization requires the reviewed PSBT version.");
        if (auto change=CompareTransactionReview(original,collected);!change.empty()) return {{},std::move(change)};
        const auto review=InspectOfflineReview(collected,proof,change_position);
        if (!review.commitment) return {{},review.error};
        Need(review.change_output==change_position,"The exact reviewed change position is unavailable.");
        auto result=FinalizeOfflineDraft(collected);
        if (!result.document) return {{},std::move(result.error)};
        const auto& bytes=*result.document;
        Need(!bytes.psbt_version && bytes.transaction && bytes.transaction->txid && bytes.transaction->wtxid
            && TransactionCommitment(bytes)==review.commitment,"Finalization did not preserve the exact approved transaction.");
        const auto& tx=*bytes.transaction;
        uint64_t total_in{},total_out{};
        for (size_t i=0;i<tx.inputs.size();++i) {
            Need(tx.inputs[i].outpoint==proof.inputs[i].point,"A final input differs from its independently reviewed previous output.");
            total_in=Total(total_in,uint64_t(proof.inputs[i].value.sats()));
        }
        for (const auto& output:tx.outputs) total_out=Total(total_out,uint64_t(output.value.sats()));
        Need(total_in>=total_out && total_in-total_out==uint64_t(proof.fee.sats()),
            "The final bytes and independently reviewed previous outputs do not reconcile.");
        const auto parsed=ParseRaw(bytes);
        const auto size=bytes.wire.size();
        size_t stripped{};
        Ok(wally_tx_get_length(parsed.get(),0,&stripped),"The exact final transaction size is unavailable.");
        Need(stripped>0 && stripped<=size && (tx.witness || stripped==size),"Final transaction serialization does not reconcile.");
        const auto weight=3*stripped+size;
        return {FinalTransaction{std::move(result.document),size,stripped,weight,(weight+3)/4,proof.fee},{}};
    } catch (Failure failure) {return {{},failure.reason};}
    catch (...) {return {{}, "Final transaction inspection exceeded available resources."};}
}
}
