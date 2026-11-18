// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/transaction.h"
#include "services/transaction_documents.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
using namespace roots::gui::protocol;
namespace {
int checks{};
void Check(bool ok,const char* message)
{
    if (!ok) throw std::runtime_error(message);
    ++checks;
}
std::string HexTx(std::string amount="0100000000000000")
{
    return "0200000001"+std::string(64,'1')+"0000000000feffffff01"+amount
        +"160014751e76e8199196d454941c45d1b3a323f1433bd600000000";
}
std::string Bytes(std::string_view hex)
{
    std::string out;
    for (size_t pos=0;pos<hex.size();pos+=2) out+=char(std::stoul(std::string(hex.substr(pos,2)),nullptr,16));
    return out;
}
std::string PsbtV0(std::string_view tx,std::string_view input={},std::string_view output={},std::string_view global={})
{
    const std::string_view raw=tx;
    Check(raw.size()<253,"test PSBT fixture is compact");
    return Bytes("70736274ff0100")+char(raw.size())+std::string(raw)+std::string(global)+'\0'
        +std::string(input)+'\0'+std::string(output)+'\0';
}
void Contracts()
{
    const std::string hex="  "+HexTx()+"\r\n";
    auto raw=ParseTransactionDocument(hex,DisplayNetwork::Unknown).document;
    Check(raw && raw->format==DocumentFormat::RawHex && raw->source==hex,"raw hex preserves exact text");
    Check(raw->transaction && raw->transaction->inputs.size()==1 && raw->transaction->outputs.front().value.sats()==1,"one satoshi exact");
    Check(TransactionHex(raw->wire)==HexTx(),"raw wire preserved");
    Check(raw->transaction->inputs.front().outpoint.index==0 && raw->transaction->inputs.front().sequence==0xfffffffe,"ordered prevout fields");
    const auto binary=ParseTransactionDocument(raw->wire,DisplayNetwork::Unknown).document;
    Check(binary && binary->source==raw->wire && binary->format==DocumentFormat::RawBinary,"binary raw preserved");
    Check(!ParseTransactionDocument(raw->wire,DisplayNetwork::Unknown,{},true).document,"clipboard only admits supported text");
    const auto spend=raw->wire;
    for (size_t n=0;n<spend.size();++n)
        Check(!ParseTransactionDocument(std::string_view(spend).substr(0,n),DisplayNetwork::Unknown).document,"raw truncation");
    Check(!ParseTransactionDocument(spend+std::string("\0x",2),DisplayNetwork::Mainnet).document,"trailing raw checked");
    const auto segregated=spend.substr(0,4)+Bytes("0001")+spend.substr(4,spend.size()-8)+Bytes("010101")+spend.substr(spend.size()-4);
    const auto wit=ParseTransactionDocument(segregated,DisplayNetwork::Mainnet).document;
    Check(wit && wit->transaction->witness && wit->transaction->txid==raw->transaction->txid
         && wit->wire==segregated,"raw witness preserved and actual txid excludes witness");
    Check(!ParseTransactionDocument(HexTx("ffffffffffffffff"),DisplayNetwork::Mainnet).document,"signed satoshi overflow");
    auto long_count=spend;
    long_count.replace(4,1,Bytes("fd0100"));
    Check(!ParseTransactionDocument(long_count,DisplayNetwork::Mainnet).document,"noncanonical compact size");
    long_count.replace(4,3,Bytes("fe00000100"));
    Check(ParseTransactionDocument(long_count,DisplayNetwork::Mainnet).error==DocumentError::Limit,"count limit before provider allocation");
    auto duplicate=spend;
    duplicate[4]=2;
    duplicate.insert(5,spend.substr(5,41));
    Check(ParseTransactionDocument(duplicate,DisplayNetwork::Mainnet).error==DocumentError::Duplicate,"duplicate input rejected");
    auto input=Bytes("04fc0161000301aa02");
    auto psbt=PsbtV0(spend,input,Bytes("012002beef"),Bytes("02100501ac"));
    auto parsed=ParseTransactionDocument(psbt,DisplayNetwork::Mainnet).document;
    Check(parsed && parsed->wire==psbt && parsed->source==psbt && parsed->psbt_entries==4,"unknown and proprietary maps remain lossless in original order");
    Check(!parsed->transaction->fee && !parsed->transaction->txid && parsed->transaction->inputs[0].evidence==PrevoutEvidence::Unknown,"prevout unknown and no approval identity");
    auto text="  "+TransactionBase64(psbt)+"\r\n";
    auto base64=ParseTransactionDocument(text,DisplayNetwork::Mainnet).document;
    Check(base64 && base64->source==text && base64->wire==psbt && base64->format==DocumentFormat::PsbtBase64,"Base64 source and exact wire round trip");
    Check(!DecodeTransactionBase64("AB==",1) && !DecodeTransactionBase64("AA=A",3),"bad padding and unused bits rejected");
    Check(ParseTransactionDocument(PsbtV0(spend,input+input),DisplayNetwork::Mainnet).error==DocumentError::Duplicate,"duplicate exact map key rejected");
    Check(!ParseTransactionDocument(psbt+"x",DisplayNetwork::Mainnet).document,"trailing PSBT checked");
    Check(ParseTransactionDocument(std::string(MAX_TRANSACTION_SOURCE+1,'a'),DisplayNetwork::Mainnet).error==DocumentError::Limit,
        "source size bound before decoder");
    Check(ParseTransactionDocument(PsbtV0(spend,{},Bytes("01060481c00100")),DisplayNetwork::Mainnet).error==DocumentError::Limit,
        "Taproot depth bound");
    Check(!ParseTransactionDocument(PsbtV0(spend,{},Bytes("01060401c00100")),DisplayNetwork::Mainnet).document,
        "orphaned Taproot tree node");
    Check(!ParseTransactionDocument(PsbtV0(spend,Bytes("02fc000161")),DisplayNetwork::Mainnet).document,
        "proprietary key must include subtype");
    for (size_t n=0;n<psbt.size();++n)
        Check(!ParseTransactionDocument(std::string_view(psbt).substr(0,n),DisplayNetwork::Mainnet).document,"PSBT truncation before every byte");
    const auto extra=Bytes("012002beef");
    Check(ParseTransactionDocument(PsbtV0(spend,{},extra+extra),DisplayNetwork::Mainnet).error==DocumentError::Duplicate,"duplicate output map key");
    Check(ParseTransactionDocument(PsbtV0(spend,{},{},extra+extra),DisplayNetwork::Mainnet).error==DocumentError::Duplicate,"duplicate global map key");
    auto future=ParseTransactionDocument(Bytes("70736274ff01fb040300000000"),DisplayNetwork::Unknown);
    Check(!future.document && future.error==DocumentError::UnsupportedVersion && future.unsupported_version==3,"future PSBT version reported without conversion");
    auto witness_only=PsbtV0(spend,Bytes("01011f")+spend.substr(spend.size()-35,31));
    auto supplied=ParseTransactionDocument(witness_only,DisplayNetwork::Mainnet).document;
    Check(supplied && supplied->transaction->inputs.front().evidence==PrevoutEvidence::Supplied
         && !supplied->transaction->fee,"witness-only amount is an unverified supplied fact");
    const auto genesis=ParseTransactionDocument(
        "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4d04ffff001d010445"
        "5468652054696d65732030332f4a616e2f32303039204368616e63656c6c6f72206f6e206272696e6b206f66207365636f"
        "6e64206261696c6f757420666f722062616e6b73ffffffff0100f2052a01000000434104678afdb0fe5548271967f1a67130"
        "b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf"
        "11d5fac00000000",DisplayNetwork::Mainnet).document;
    Check(genesis && genesis->transaction->coinbase && genesis->transaction->txid->hex()==
          "4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b","pinned genesis raw transaction txid");
    const auto prevout=genesis->wire;
    auto later=spend;
    auto prev_hash=Bytes(genesis->transaction->txid->hex());
    std::reverse(prev_hash.begin(),prev_hash.end());
    later.replace(5,32,prev_hash);
    const auto good=ParseTransactionDocument(PsbtV0(later,Bytes("0100")+char(prevout.size())+prevout),DisplayNetwork::Mainnet).document;
    Check(good && good->transaction->inputs.front().evidence==PrevoutEvidence::TxidMatched
         && good->transaction->fee->sats()==4999999999,"matched full prevout, exact fee");
    const auto mismatch=ParseTransactionDocument(PsbtV0(spend,Bytes("0100")+char(prevout.size())+prevout),DisplayNetwork::Mainnet).document;
    Check(mismatch && mismatch->transaction->inputs.front().evidence==PrevoutEvidence::Conflict
         && !mismatch->transaction->fee,"mismatched full prevout retains source without trusting values");
    const auto script=raw->transaction->outputs[0].script;
    const auto bc=OutputAddress(script,DisplayNetwork::Mainnet);
    const auto reg=OutputAddress(script,DisplayNetwork::Regtest);
    Check(bc=="bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4" && reg && reg->starts_with("bcrt1"),"independent known Bech32 address and local HRP");
    Check(!VerifyOutputAddress(*bc,script,DisplayNetwork::Regtest) && !OutputAddress(script,DisplayNetwork::Unknown),"wrong or missing network");
    const std::string descriptor="wpkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)";
    const auto public_desc=ParseTransactionDocument(descriptor,DisplayNetwork::Mainnet).document;
    Check(public_desc && public_desc->source==descriptor && MatchesPublicPolicy(script,DisplayNetwork::Mainnet,*public_desc->descriptor)
         && !MatchesPublicPolicy(script,DisplayNetwork::Regtest,*public_desc->descriptor),"verified script comparison respects explicit network");
    Check(!ParseTransactionDocument(descriptor,DisplayNetwork::Unknown).document,"descriptor network required");
    Check(!ParseTransactionDocument("wpkh(xprv-unsupported)",DisplayNetwork::Mainnet).document,"private material never admitted");
    std::stop_source stopped;
    stopped.request_stop();
    Check(ParseTransactionDocument(psbt,DisplayNetwork::Mainnet,stopped.get_token()).error==DocumentError::Cancelled,"cancelled task");
}
ByteSpan Field(const protocol::TransactionDocument& doc,InspectionBytes bytes,InspectionNode node,
    std::string_view name,size_t index=0)
{
    const auto fields=InspectionFields(doc,bytes,node,index);
    const auto found=std::ranges::find_if(fields,[name](const auto& item){return name==item.name;});
    if (found==fields.end()) throw std::runtime_error("inspection field missing: "+std::string(name));
    return found->bytes;
}
void InspectionContracts()
{
    using Rep=InspectionBytes;
    using Node=InspectionNode;
    const auto raw=Bytes(HexTx());
    const auto text="  "+HexTx()+"\r\n";
    const auto decoded=ParseTransactionDocument(text,DisplayNetwork::Regtest).document;
    Check(decoded && InspectionBuffer(*decoded,Rep::Source)==text && InspectionBuffer(*decoded,Rep::Wire)==raw,"distinct original text and binary wire");
    Check(InspectionBufferName(*decoded,Rep::Source)!=InspectionBufferName(*decoded,Rep::Wire),"representations are labelled separately");
    Check(decoded->transaction->txid->hex()=="c694f12193d204e4bbf0308a28e5b291c0242189299c6910c2beaba0f0383cec"
        && decoded->transaction->wtxid==decoded->transaction->txid,"independently double-SHA256 checked legacy identifier and identical no-witness wire ID");
    Check(Field(*decoded,Rep::Wire,Node::Overview,"Version")==ByteSpan{0,4},"raw version bytes");
    Check(Field(*decoded,Rep::Wire,Node::Input,"Previous transaction ID (wire order)")==ByteSpan{5,32},"raw previous txid bytes");
    Check(Field(*decoded,Rep::Wire,Node::Input,"Previous output index")==ByteSpan{37,4},"raw previous index bytes");
    Check(Field(*decoded,Rep::Wire,Node::Input,"scriptSig")==ByteSpan{42,0},"empty input script is represented explicitly");
    Check(Field(*decoded,Rep::Wire,Node::Input,"Sequence")==ByteSpan{42,4},"raw sequence bytes");
    const auto amount=Field(*decoded,Rep::Wire,Node::Output,"Output amount");
    Check(amount==ByteSpan{47,8} && raw.substr(amount.offset,amount.length)==Bytes("0100000000000000"),"known little-endian satoshi bytes");
    Check(Field(*decoded,Rep::Wire,Node::Output,"scriptPubKey")==ByteSpan{56,22},"raw script bytes exclude length prefix");
    Check(Field(*decoded,Rep::Wire,Node::Overview,"Locktime")==ByteSpan{78,4},"raw locktime bytes");
    auto source=InspectionSpan(*decoded,Rep::Source,amount);
    Check(source==ByteSpan{96,16} && text.substr(source->offset,source->length)=="0100000000000000","source maps exact ASCII bytes including leading spaces");
    Check(!InspectionSpan(*decoded,Rep::Wire,{80,3}) && !InspectionSpan(*decoded,Rep::Source,{SIZE_MAX,2}),"malformed ranges rejected");
    const auto segmented=raw.substr(0,4)+Bytes("0001")+raw.substr(4,raw.size()-8)+Bytes("010101")+raw.substr(raw.size()-4);
    const auto witness=ParseTransactionDocument(segmented,DisplayNetwork::Regtest).document;
    Check(witness && witness->transaction->txid==decoded->transaction->txid
        && witness->transaction->wtxid->hex()=="0e593fee5e6b7b912591f6afdd04035ce627f2b50c99c7504921ea15111efec3","witness id includes the exact segregated bytes");
    Check(Field(*witness,Rep::Wire,Node::Input,"Witness item 1")==ByteSpan{82,1}
        && segmented[82]==1,"witness item maps without its compact length");
    Check(Field(*witness,Rep::Wire,Node::Overview,"Locktime")==ByteSpan{83,4},"witness shifts locktime offsets correctly");
    auto taproot=Bytes("01012b0100000000000000225120")+std::string(32,'q');
    const auto psbt=PsbtV0(raw,taproot+Bytes("04fc0161000301aa02"));
    auto binary=ParseTransactionDocument(psbt,DisplayNetwork::Regtest).document;
    Check(binary && binary->transaction->inputs[0].evidence==PrevoutEvidence::Supplied
        && !binary->transaction->fee && ReviewSighash(*binary,0)==0,"supplied Taproot previous script yields default mode and no known fee");
    Check(binary->transaction->txid==std::nullopt && binary->transaction->wtxid==std::nullopt && TransactionCommitment(*binary).has_value(),"PSBT is not a signed identity");
    const auto embedded=Field(*binary,Rep::Wire,Node::Input,"Sequence");
    Check(embedded==ByteSpan{50,4},"PSBTv0 embedded input maps into the container at its own offset");
    const auto opaque=Field(*binary,Rep::Wire,Node::Input,"Proprietary field");
    Check(InspectionSpan(*binary,Rep::Source,opaque)==opaque && psbt.substr(opaque.offset,opaque.length)==Bytes("01aa02"),"opaque value remains exact");
    const auto encoded=ParseTransactionDocument("  "+TransactionBase64(psbt)+"\r\n",DisplayNetwork::Regtest).document;
    Check(encoded && !InspectionSpan(*encoded,Rep::Source,opaque) && InspectionSpan(*encoded,Rep::Wire,opaque)==opaque,"Base64 source refuses fictitious direct mapping");
    Check(encoded->unsigned_wire==raw && Field(*encoded,Rep::Unsigned,Node::Input,"Sequence")==ByteSpan{42,4},"derived unsigned layout has independent coordinates");
    Check(InspectionFields(*encoded,Rep::Unsigned,Node::Input,99).empty(),"missing input group stays unmapped");
    auto spending=raw;
    auto parent_hash=Bytes(decoded->transaction->txid->hex());
    std::ranges::reverse(parent_hash);
    spending.replace(5,32,parent_hash);
    const auto by_txid=ParseTransactionDocument(PsbtV0(spending,Bytes("0100")+char(raw.size())+raw),
        DisplayNetwork::Regtest).document;
    Check(by_txid && by_txid->transaction->inputs[0].evidence==PrevoutEvidence::TxidMatched,
        "full supplied transaction supplies independently matched previous output data");
    Check(Field(*by_txid,Rep::Wire,Node::Input,"Matched previous Output amount")==ByteSpan{141,8}
        && Field(*by_txid,Rep::Wire,Node::Input,"Matched previous scriptPubKey")==ByteSpan{150,22},
        "matched previous amount and script map inside the original PSBT");
    const auto version2=Bytes("70736274ff01fb040200000001020402000000010401010105010100010e20")+std::string(32,'\x11')
        +Bytes("010f0400000000011004030040000001030801000000000000000104160014751e76e8199196d454941c45d1b3a323f1433bd600");
    const auto v2=ParseTransactionDocument(version2,DisplayNetwork::Regtest).document;
    Check(v2 && v2->psbt_version==2 && v2->transaction->inputs[0].sequence==0x00400003
        && v2->transaction->locktime==0,"BIP370 sequence and locktime known");
    const auto mapped=Field(*v2,Rep::Wire,Node::Input,"Sequence");
    Check(mapped.offset!=42 && InspectionBuffer(*v2,Rep::Wire).substr(mapped.offset,mapped.length)==Bytes("03004000")
        && v2->unsigned_wire.substr(42,4)==Bytes("03004000"),"PSBTv2 and derived wire field values match with independent spans");
    std::string large="0200000001"+std::string(64,'1')+"0000000000fefffffffd0004";
    for (size_t i=0;i<1024;++i) large+="0000000000000000016a";
    large+="0065cd1d";
    const auto many=ParseTransactionDocument(large,DisplayNetwork::Regtest).document;
    Check(many && many->transaction->outputs.size()==1024 && many->transaction->outputs.back().value.sats()==0
        && many->transaction->locktime==500000000,"large import distinguishes known zero and time lock");
    Check(Field(*many,Rep::Wire,Node::Output,"Output amount",1023)==ByteSpan{10279,8},"large last output located directly");
    large.replace(92,6,"fd0104");
    Check(ParseTransactionDocument(large,DisplayNetwork::Regtest).error==DocumentError::Limit,"bounded large input count form");
}
void Files(const std::filesystem::path& root)
{
    using namespace std::chrono_literals;
    const auto path=root/"fixture.tx";
    const auto original="  "+HexTx()+"\r\n";
    {std::ofstream file(path);file<<original;Check(bool(file),"test file written");}
    TransactionDocuments documents;
    const LocalDocumentContext context{7,23,{5},{1}};
    auto changed=context;
    changed.epoch.value=2;
    const auto finish=[&](LocalDocumentContext wanted) {
        std::optional<LocalDocumentReply> reply;
        const auto deadline=std::chrono::steady_clock::now()+3s;
        while (documents.Busy() && std::chrono::steady_clock::now()<deadline) {
            reply=documents.Poll(wanted);
            std::this_thread::sleep_for(1ms);
        }
        Check(!documents.Busy(),"local reader settled");
        return reply;
    };
    Check(!documents.ReadFile(context,"relative.tx",DisplayNetwork::Unknown),"absolute local path required");
    Check(documents.ReadFile(context,path.string(),DisplayNetwork::Unknown),"read admitted");
    Check(!finish(changed),"lock-epoch change retires late file result");
    Check(documents.ReadText(context,original,DisplayNetwork::Unknown),"clipboard admitted");
    documents.Cancel();
    Check(!finish(context),"explicit cancel retires same-view parse");
    Check(documents.ReadFile(context,path.string(),DisplayNetwork::Unknown),"explicit read after cancellation");
    const auto good=finish(context);
    Check(good && good->parsed.document && good->parsed.document->source==original,"regular file read byte identical");
    const auto saved=root/"saved.tx";
    Check(SaveOriginalDocument(saved.string(),*good->parsed.document).empty(),"lossless local save");
    const auto before=SaveOriginalDocument(saved.string(),*good->parsed.document);
    Check(!before.empty(),"no clobber on explicit local save");
    std::ifstream written(saved,std::ios::binary);
    const std::string bytes(std::istreambuf_iterator<char>(written),{});
    Check(bytes==original,"saved exact original format and whitespace");
    const auto pipe=root/"not-a-regular.tx";
    Check(::mkfifo(pipe.c_str(),0600)==0,"owned nonregular-file fixture");
    Check(documents.ReadFile(context,pipe.string(),DisplayNetwork::Unknown),"dispatch nonregular path");
    const auto rejected=finish(context);
    Check(rejected && !rejected->parsed.document && !rejected->file_error.empty(),"pipe rejected with no blocking read");
    const auto link=root/"symbolic.tx";
    std::filesystem::create_symlink(path,link);
    Check(documents.ReadFile(context,link.string(),DisplayNetwork::Unknown),"dispatch symlink choice");
    const auto no_follow=finish(context);
    Check(no_follow && !no_follow->parsed.document && !no_follow->file_error.empty(),"file reader never follows final symlink");
}

}
int main(int argc,char** argv)
{
    try {
        if (argc==1) {
            Contracts();
            std::cout<<"{\"passed\":true,\"checks\":"<<checks<<"}\n";
            checks=0;
            InspectionContracts();
            std::cout<<"{\"passed\":true,\"inspectionChecks\":"<<checks<<"}\n";
        } else if (argc==3 && std::string_view(argv[1])=="--local") {
            Files(argv[2]);
            std::cout<<"{\"passed\":true,\"localChecks\":"<<checks<<"}\n";
        } else {
            if (argc!=3 || std::string_view(argv[1])!="--parse") return 2;
            const std::string_view origin=argv[2];
            const auto result=ParseTransactionDocument(origin,DisplayNetwork::Unknown);
            if (!result.document) std::cout<<"rejected "<<unsigned(result.error)<<'\n';
            else {
                Check(result.document->source==origin,"original text must remain byte identical");
                std::cout<<"accepted "<<TransactionBase64(result.document->wire)<<'\n';
            }
        }
        return 0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
