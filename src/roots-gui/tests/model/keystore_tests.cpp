// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/public_wallet.h"
#include <wally_bip32.h>
#include <wally_core.h>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace roots::gui::protocol;
namespace {
int checks{};
void Check(bool valid,const char* detail)
{
    if (!valid) throw std::runtime_error(detail);
    ++checks;
}
std::string FixtureKey(unsigned char tag,bool private_key)
{
    std::array<unsigned char,32> seed{};
    seed[0]=tag;
    ext_key* raw=nullptr;
    const auto status=bip32_key_from_seed_alloc(seed.data(),seed.size(),BIP32_VER_TEST_PRIVATE,0,&raw);
    wally_bzero(seed.data(),seed.size());
    const std::unique_ptr<ext_key,decltype(&bip32_key_free)> key(raw,&bip32_key_free);
    Check(status==WALLY_OK && key,"generate test-only key");
    char* output=nullptr;
    const auto kind=private_key?BIP32_FLAG_KEY_PRIVATE:BIP32_FLAG_KEY_PUBLIC;
    Check(bip32_key_to_base58(key.get(),kind,&output)==WALLY_OK && output,"encode fixture key");
    const std::unique_ptr<char,decltype(&wally_free_string)> encoded(output,&wally_free_string);
    return encoded.get();
}
std::string Header(const std::string& input,uint32_t version,bool alternate_chain=false)
{
    std::array<unsigned char,82> data{};
    size_t length{};
    Check(wally_base58_to_bytes(input.c_str(),BASE58_FLAG_CHECKSUM,data.data(),data.size(),&length)==WALLY_OK
        && length==BIP32_SERIALIZED_LEN,"decode public fixture");
    for (size_t i=0;i<4;++i) data[i]=(version>>((3-i)*8))&255;
    if (alternate_chain) data[13]^=1;
    char* output=nullptr;
    Check(wally_base58_from_bytes(data.data(),length,BASE58_FLAG_CHECKSUM,&output)==WALLY_OK && output,"re-encode public fixture");
    const std::unique_ptr<char,decltype(&wally_free_string)> encoded(output,&wally_free_string);
    return encoded.get();
}
PublicSigner Read(const std::string& expression)
{
    auto parsed=InspectPublicSigner(expression,"regtest");
    Check(parsed.signer.has_value(),"admit public signer");
    return std::move(*parsed.signer);
}
void PublicVectors()
{
    const auto first=FixtureKey(7,false);
    const auto second=FixtureKey(8,false);
    const auto standard=Read("[12345678/84h/1h/0h]"+first+"/0/*");
    const auto conflicting_origin=Read("[87654321/49h/1h/1h]"+first+"/1/*");
    const auto same_point_new_chain=Read(Header(first,BIP32_VER_TEST_PUBLIC,true)+"/0/*");
    const auto shared_fingerprint=Read("[12345678/84h/1h/0h]"+second+"/0/*");
    Check(standard.fingerprint=="12345678" && standard.origin_path=="m/84h/1h/0h"
        && standard.derivation_suffix=="/0/*" && standard.public_key==first,"structured exact public origin");
    Check(standard.material_id==conflicting_origin.material_id
        && standard.fingerprint!=conflicting_origin.fingerprint,"origin labels do not establish physical signer identity");
    Check(standard.material_id!=shared_fingerprint.material_id
        && standard.fingerprint==shared_fingerprint.fingerprint,"same fingerprint is allowed for distinct material");
    Check(same_point_new_chain.material_id!=standard.material_id
        && same_point_new_chain.material_id.substr(64)==standard.material_id.substr(64),"chain code participates in identity");
    const auto native_multi=PolicyChoice{ScriptTemplate::NativeSegwit,2,2,true,true};
    Check(!StageSignerPolicy(native_multi,"regtest",std::array{standard,conflicting_origin}).preview,"reject duplicate keys across origins");
    Check(StageSignerPolicy(native_multi,"regtest",std::array{standard,shared_fingerprint}).preview.has_value(),"same fingerprint can compile distinct signers");
    Check(StageSignerPolicy(native_multi,"regtest",std::array{standard,same_point_new_chain}).preview.has_value(),"different chain codes derive distinct keys");

    const auto alias=Header(first,0x02575483);
    auto normalized=Read("[FEEDFACE/84'/1'/0']"+alias+"/0/*");
    Check(normalized.encoding==SignerEncoding::NativeMulti && normalized.public_key==first
        && normalized.material_id==standard.material_id && normalized.expression.find(alias)==std::string::npos,"normalized Vpub preserves exact material");
    Check(!StageSignerPolicy(native_multi,"regtest",std::array{standard,normalized}).preview,"reject normalization alias duplicate");
    Check(StageSignerPolicy(native_multi,"regtest",std::array{normalized,shared_fingerprint}).preview.has_value(),"valid compatible alias compiles");
    Check(!StageSignerPolicy(PolicyChoice{},"regtest",std::array{normalized}).preview,"multisig header cannot stage a single-sig script");
    auto single=Read(Header(first,0x045f1cf6)+"/0/*");
    Check(single.encoding==SignerEncoding::NativeSingle
        && StageSignerPolicy(PolicyChoice{},"regtest",std::array{single}).preview.has_value(),"vpub works for native single signature");
    Check(!InspectPublicSigner(alias,"main").signer && !InspectPublicSigner(first,"unknown").signer,"reject wrong or unknown network");
    for (const auto& malformed : {"[1234567]", "[12345678/0//1]", "[12345678/00]", "[12345678/*]", "[12345678/2147483648]", "[12345678/0h"}) {
        Check(!InspectPublicSigner(std::string(malformed)+first,"regtest").signer,"reject malformed origin");
    }
    for (const auto& malformed : {"/0h/*","/0'/*","/*/1","/2147483648","/0//1","/0/"}) {
        Check(!InspectPublicSigner(first+malformed,"regtest").signer,"reject malformed or hardened public suffix");
    }
    auto private_key=FixtureKey(11,true);
    const auto rejected=InspectPublicSigner(private_key,"regtest");
    wally_bzero(private_key.data(),private_key.size());
    Check(!rejected.signer && !rejected.error.empty(),"private material rejected without retention");
}
}

int main()
{
    try {
        PublicVectors();
        std::cout << "{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":" << checks << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
