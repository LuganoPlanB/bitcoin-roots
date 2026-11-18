// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/transaction.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
namespace {
using protocol::DisplayNetwork;
using protocol::InspectionBytes;
using protocol::InspectionNode;
using protocol::TransactionEdit;
constexpr auto NET=DisplayNetwork::Regtest;
constexpr auto FIRST="0014751e76e8199196d454941c45d1b3a323f1433bd6";
constexpr auto SECOND="001406afd46bcdfd22ef94ac122aa11f241244a37ecc";
int checks{};
void Check(bool pass,std::string_view why)
{
    if (!pass) throw std::runtime_error(std::string(why));
    ++checks;
}
std::string Bytes(std::string_view hex)
{
    std::string result;
    for (size_t n=0;n<hex.size();n+=2) result+=char(std::stoul(std::string(hex.substr(n,2)),nullptr,16));
    return result;
}
void Field(std::string& map,std::string_view key,std::string_view value)
{
    if (key.size()>=253 || value.size()>=253) throw std::runtime_error("fixture short field");
    map+=char(key.size());
    map+=key;
    map+=char(value.size());
    map+=value;
}
void Opaque(std::string& map,std::string_view value) { Field(map,Bytes("fc016100"),value); }
void Field(std::string& map,uint8_t tag,std::string_view value)
{
    const char type=char(tag);
    Field(map,std::string_view(&type,1),value);
}
std::string Container(bool opaque=true,bool negotiated=false,bool final=false,bool locked=false)
{
    auto wire=Bytes("70736274ff01fb0402000000010204020000000104010201050102");
    if (opaque) Opaque(wire,"global opaque");
    if (negotiated) Field(wire,6,Bytes("03"));
    wire+='\0';
    for (size_t i=0;i<2;++i) {
        Field(wire,0x0e,std::string(32,char(i==0 ? 0x11 : 0x22)));
        Field(wire,0x0f,Bytes("00000000"));
        Field(wire,0x10,Bytes("fdffffff"));
        if (opaque) Opaque(wire,i==0?"input A":"input B");
        if (i==0 && locked) Field(wire,0x12,Bytes("20dd0d00")); // height 908576
        if (i==0 && final) Field(wire,7,Bytes("51"));
        wire+='\0';
    }
    for (size_t i=0;i<2;++i) {
        Field(wire,3,Bytes(i==0 ? "f401000000000000" : "e803000000000000"));
        Field(wire,4,Bytes(i==0 ? FIRST : SECOND));
        if (opaque) Opaque(wire,i==0?"output A":"output B");
        wire+='\0';
    }
    return wire;
}
std::string Metadata(const protocol::TransactionDocument& tx,InspectionNode node,size_t index=0)
{
    const auto fields=protocol::InspectionFields(tx,InspectionBytes::Wire,node,index);
    const auto found=std::ranges::find_if(fields,[](const auto& field){return field.name=="Proprietary field (serialized PSBT entry)";});
    return found==fields.end() ? "" : tx.wire.substr(found->bytes.offset,found->bytes.length);
}
void Edits()
{
    const auto raw=Container();
    const std::string origin=" \n"+protocol::TransactionBase64(raw)+"\r\n";
    const auto doc=protocol::ParseTransactionDocument(origin,NET).document;
    Check(doc && doc->psbt_version==2 && doc->source==origin,"exact original encoded PSBT");
    const auto& before=*doc->transaction;
    TransactionEdit request;
    request.version=3;
    request.locktime=900000;
    request.input=protocol::EditInput{0,before.inputs[0].outpoint,42};
    request.output=protocol::EditOutput{1,{Money::Satoshis(1001),before.outputs[1].script}};
    const auto edited=protocol::EditTransaction(*doc,NET,request);
    Check(bool(edited.document),edited.error);
    Check(edited.document->format==doc->format && edited.document->transaction->version==3
        && edited.document->transaction->locktime==900000 && edited.document->transaction->inputs[0].sequence==42
        && edited.document->transaction->outputs[1].value==Money::Satoshis(1001),"atomic material header/input/output changes");
    Check(edited.affected=="transaction version, locktime, input 1 sequence, output 2 amount","exact affected-field classes");
    Check(!edited.document->transaction->fee && !edited.document->transaction->inputs[0].previous_value,"unknown fee and prevout remain unknown");
    Check(doc->source==origin && doc->wire==raw && before.outputs[1].value==Money::Satoshis(1000),"original remains untouched");
    Check(protocol::EditTransaction(*doc,NET,{.version=1}).error.find("PSBTv2 provider")!=std::string::npos,
        "version one is unavailable under the pinned v2 provider");
    for (const auto group:{InspectionNode::Input,InspectionNode::Output}) {
        TransactionEdit move;
        move.reorder=protocol::ReorderTransactionItem{group,1,0};
        const auto changed=protocol::EditTransaction(*doc,NET,move);
        Check(bool(changed.document),"reordered document reparses");
        Check(Metadata(*changed.document,group,0)==Metadata(*doc,group,1)
            && Metadata(*changed.document,group,1)==Metadata(*doc,group,0)
            && Metadata(*changed.document,InspectionNode::Overview)==Metadata(*doc,InspectionNode::Overview),
            "complete opaque map entries move with their own row");
        const auto& current=*changed.document->transaction;
        Check(group==InspectionNode::Input ? current.inputs[0].outpoint==before.inputs[1].outpoint
            : current.outputs[0]==before.outputs[1],"known transaction row moves with maps");
    }
    TransactionEdit unsafe;
    unsafe.input=protocol::EditInput{0,before.inputs[1].outpoint,1};
    Check(protocol::EditTransaction(*doc,NET,unsafe).error.find("attached metadata")!=std::string::npos,"outpoint replacement preserves bound or opaque metadata");
    unsafe={};
    unsafe.output=protocol::EditOutput{0,{Money::Satoshis(0),SECOND}};
    Check(protocol::EditTransaction(*doc,NET,unsafe).error.find("attached metadata")!=std::string::npos,"script replacement cannot detach output metadata");
    const auto clean=protocol::ParseTransactionDocument(Container(false),NET).document;
    Check(clean && protocol::EditTransaction(*clean,NET,unsafe).document->transaction->outputs[0].value.sats()==0,
        "metadata-free script edit preserves genuine zero");
    unsafe.output->replacement.value=Money::Satoshis(21000000LL*100000000);
    Check(!protocol::EditTransaction(*clean,NET,unsafe).document,"sum overflow aborts the whole edit");
    unsafe={};
    unsafe.input=protocol::EditInput{0,before.inputs[1].outpoint,1};
    Check(!protocol::EditTransaction(*clean,NET,unsafe).document,"duplicate input aborts");
    Check(protocol::EditTransaction(*doc,NET,{}).error.find("No material")!=std::string::npos,"no-op does not create a new copy");
}
void Representations()
{
    const auto clean=protocol::ParseTransactionDocument(Container(false),NET).document;
    Check(clean && !clean->unsigned_wire.empty(),"wire fixture derived");
    auto v0=Bytes("70736274ff");
    Field(v0,0,clean->unsigned_wire);
    Opaque(v0,"global V0");
    v0+='\0';
    Opaque(v0,"input first");
    v0+='\0';
    Opaque(v0,"input second");
    v0+='\0';
    Opaque(v0,"output first");
    v0+='\0';
    Opaque(v0,"output second");
    v0+='\0';
    const auto legacy=protocol::ParseTransactionDocument(v0,NET).document;
    Check(legacy && legacy->psbt_version==0,"bounded unsigned v0 fixture");
    TransactionEdit reorder;
    reorder.reorder=protocol::ReorderTransactionItem{InspectionNode::Output,1,0};
    const auto edited=protocol::EditTransaction(*legacy,NET,reorder);
    Check(edited.document && edited.document->psbt_version==0
        && edited.document->transaction->outputs[0]==legacy->transaction->outputs[1]
        && Metadata(*edited.document,InspectionNode::Output,0)==Metadata(*legacy,InspectionNode::Output,1),
        "v0 keeps embedded unsigned structure and reorders its corresponding maps");
    const auto unsigned_raw=protocol::ParseTransactionDocument(protocol::TransactionHex(clean->unsigned_wire),NET).document;
    const auto raw_move=protocol::EditTransaction(*unsigned_raw,NET,reorder);
    Check(raw_move.document && raw_move.document->format==protocol::DocumentFormat::RawHex
        && raw_move.document->transaction->outputs[0]==unsigned_raw->transaction->outputs[1]
        && raw_move.document->transaction->txid!=unsigned_raw->transaction->txid,"unsigned raw edit has distinct wire and exact ID");
    for (const auto& source:{Container(false,true),Container(false,false,true)}) {
        const auto object=protocol::ParseTransactionDocument(source,NET).document;
        Check(object && !protocol::TransactionEditBlock(*object).empty() && !protocol::EditTransaction(*object,NET,reorder).document,
            "negotiated or finalized PSBT remains byte-identical and read-only");
    }
    const auto fixed=protocol::ParseTransactionDocument(Container(false,false,false,true),NET).document;
    TransactionEdit time;
    time.locktime=1670000000;
    Check(fixed && protocol::EditTransaction(*fixed,NET,time).error.find("input-required")!=std::string::npos,
        "required input locktime cannot be silently overridden");
    auto script_sig=unsigned_raw->wire;
    script_sig.replace(41,1,Bytes("0151"));
    const auto signed_raw=protocol::ParseTransactionDocument(script_sig,NET).document;
    Check(signed_raw && signed_raw->transaction->script_sig && !protocol::EditTransaction(*signed_raw,NET,reorder).document,
        "raw input script material keeps its original read-only");
}
}
int main()
{
    try {
        Edits();
        Representations();
        std::cout<<"{\"passed\":true,\"transactionEditChecks\":"<<checks<<"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"After "<<checks<<" checks: "<<error.what()<<'\n';
        return 1;
    }
}
