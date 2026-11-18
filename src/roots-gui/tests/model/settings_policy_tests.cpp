// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/wallet_lifecycle.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace roots::gui;
using namespace protocol;
int checks{};
void Check(bool valid,const char* detail)
{
    if (!valid) throw std::runtime_error(detail);
    ++checks;
}
constexpr auto A="0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";
constexpr auto B="0379be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";

void Policies()
{
    auto single=PreviewPublicWallet(PublicImportKind::Descriptor,std::string("wpkh(")+A+")","regtest");
    Check(single.preview && single.preview->descriptors.front().choice==PolicyChoice{},"default native single policy");
    auto multi=PreviewPublicWallet(PublicImportKind::Descriptor,std::string("wsh(sortedmulti(2,")+A+','+B+"))","regtest");
    Check(multi.preview && multi.preview->descriptors.front().choice==PolicyChoice{ScriptTemplate::NativeSegwit,2,2,true,true},"typed native multisig");
    auto lower=StageWalletPolicy({ScriptTemplate::NativeSegwit,1,2,true,true},*multi.preview);
    Check(lower.preview && lower.preview->descriptors.front().script!=multi.preview->descriptors.front().script
        && lower.preview->descriptors.front().signers==multi.preview->descriptors.front().signers
        && VerifyPublicPreview(*lower.preview,"regtest"),"threshold changes actual script and retains exact signers");
    for (const auto script:{ScriptTemplate::Legacy,ScriptTemplate::NestedSegwit}) {
        auto changed=StageWalletPolicy({script,2,2,true,true},*multi.preview);
        Check(changed.preview && changed.preview->descriptors.front().choice.script==script
            && changed.preview->descriptors.front().address!=multi.preview->descriptors.front().address,"implemented multisig wrappers");
    }
    auto taproot=StageWalletPolicy({ScriptTemplate::Taproot,1,1,false,true},*single.preview);
    Check(taproot.preview && taproot.preview->descriptors.front().descriptor.starts_with("tr("),"single taproot implemented");
    for (const auto& invalid : {PolicyChoice{ScriptTemplate::Taproot,2,2,true,true},PolicyChoice{ScriptTemplate::NativeSegwit,0,2,true,true},
        PolicyChoice{ScriptTemplate::NativeSegwit,3,2,true,true},PolicyChoice{ScriptTemplate::NativeSegwit,1,1,true,true},
        PolicyChoice{ScriptTemplate::NativeSegwit,15,16,true,true},PolicyChoice{ScriptTemplate::NativeSegwit,2,2,false,true}}) {
        Check(!SupportedPolicy(invalid) && !StageWalletPolicy(invalid,*multi.preview).preview,"reject unsupported combination");
    }
    Check(SupportedPolicy({ScriptTemplate::NativeSegwit,15,15,true,true}),"declared upper bound");
    Check(!StageWalletPolicy({ScriptTemplate::NativeSegwit,2,3,true,true},*multi.preview).preview,"never fabricate missing signer");
    auto checksum=multi.preview->descriptors.front().descriptor;
    checksum.back()=checksum.back()=='a'?'b':'a';
    Check(!PreviewPublicWallet(PublicImportKind::Descriptor,checksum,"regtest").preview,"explicit bad checksum fails");
    auto forged=*multi.preview;
    forged.descriptors.front().choice.required=1;
    Check(!VerifyPublicPreview(forged,"regtest"),"forged staged metadata fails exact proof");
}
void Ranges()
{
    constexpr auto XPUB="tpubDDEqdTw1mdzqcciRhB87KhYmkcvfN2RGdaySHSMNArDZLqquFfq55pLGgjBXNZqgPV7KLXKLViLK5nE7jFTSxfuEkZr99TYkqEEVvyQH3Aj";
    // The fixed path ensures inactive imported descriptors without a
    // classified receive/change direction remain viewable.
    const auto fixed=PreviewPublicWallet(PublicImportKind::Descriptor,std::string("wpkh(")+A+")","regtest");
    const auto object=[](std::string name,std::string entry) {
        return *rpc::Json::Parse("{\"wallet_name\":"+rpc::Quote(name)+",\"descriptors\":["+entry+"]}");
    };
    const std::string fixed_entry="{\"desc\":"+rpc::Quote(fixed.preview->descriptors.front().descriptor)+",\"active\":false}";
    const auto fixed_wallet=ValidateWalletPolicy(object("exact",fixed_entry),"exact","regtest");
    Check(fixed_wallet && fixed_wallet->branches.size()==1 && !fixed_wallet->branches.front().classified,"inactive direction remains unreported");
    Check(!ValidateWalletPolicy(object("wrong",fixed_entry),"exact","regtest"),"wallet identity cannot be substituted");
    Check(!ValidateWalletPolicy(object("exact",fixed_entry+','+fixed_entry),"exact","regtest"),"duplicate daemon descriptor rejected");

    const auto ranged=PreviewPublicWallet(PublicImportKind::ExtendedKey,XPUB,"regtest");
    Check(bool(ranged.preview),"known public extended key");
    const auto row="{\"desc\":"+rpc::Quote(ranged.preview->descriptors.front().descriptor)+",\"active\":true,\"internal\":false,";
    const auto range=[&](std::string suffix) {return ValidateWalletPolicy(object("exact",row+suffix+'}'),"exact","regtest");};
    const auto good=range("\"range\":[0,999],\"next_index\":2,\"next\":2");
    Check(good && good->branches.front().range==PolicyRange{0,999,2},"exact inclusive daemon range");
    Check(range("\"range\":[4,9],\"next_index\":10").has_value(),"exhausted cursor after inclusive range");
    for (const auto* invalid : {"\"range\":[4,1],\"next_index\":4","\"range\":[0,9],\"next_index\":11",
        "\"range\":[4,9],\"next_index\":3","\"range\":[0,9],\"next_index\":2,\"next\":3",
        "\"range\":[0,9.0],\"next_index\":0","\"range\":[0,2147483648],\"next_index\":0","\"range\":[0,9]"})
        Check(!range(invalid),"malformed or conflicting range rejected");
}
}
int main()
{
    try {
        Policies();
        Ranges();
        std::cout << "{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":" << checks << "}\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
