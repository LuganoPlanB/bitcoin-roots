// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/send_batch.h"
#include "services/transaction_documents.h"
#include "adapters/protocol/public_wallet.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>

using namespace roots::gui;
namespace {
constexpr auto MAIN=protocol::DisplayNetwork::Mainnet;
constexpr std::string_view A="1BitcoinEaterAddressDontSendf59kuE",B="1BoatSLRHtKNngkdXEeobR76b53LETtpyT";
constexpr std::string_view HEADER="Address,Amount,Label,Unit\r\n";
int checks{};

void Check(bool good,const char* name)
{
    if (!good) throw std::runtime_error(name);
    ++checks;
}
std::string Payment(std::string_view address,std::string_view amount,std::string_view label,std::string_view unit)
{
    return std::string(address)+","+std::string(amount)+","+std::string(label)+","+std::string(unit)+"\r\n";
}
BatchValidation Validate(std::string_view lines,std::optional<Money> dust={})
{
    const auto csv=ReadBatchCsv(std::string(HEADER)+std::string(lines),9);
    if (!csv.staged) throw std::runtime_error(csv.error);
    return ValidateBatch(*csv.staged,MAIN,dust);
}
void Grammar()
{
    const auto a=Payment(A,"\"0,00012345\"","\"Jörg, \"\"支付\"\"\"","BTC");
    const auto b=Payment(B,"6789","'literal","sat");
    auto in=ReadBatchCsv("\xef\xbb\xbf"+std::string(HEADER)+a+b,9);
    Check(in.staged && in.staged->rows.size()==2 && in.staged->rows.front().id==9 && in.staged->next==11,"UTF-8 BOM and stable row identities");
    auto valid=ValidateBatch(*in.staged,MAIN);
    Check(valid.ready && valid.fixed==Money::Satoshis(19134),"quoted decimal comma and sat sum");
    Check(valid.ready->rows[0].label=="Jörg, \"支付\"" && valid.ready->rows[1].label=="'literal","quotes and plain leading apostrophe");
    const auto multiline=ReadBatchCsv(std::string(HEADER)+Payment(A,"1000","\"one\r\ntwo\nthree\"","sat"),1);
    Check(multiline.staged && multiline.staged->rows[0].label=="one\r\ntwo\nthree","quoted newline belongs to one field");
    const auto invalid=ValidateBatch(*multiline.staged,MAIN);
    Check(!invalid.ready && !invalid.rows[0].label.empty(),"existing one-line label contract enforced");
    auto lf=std::string("Address,Amount,Label,Unit\n")+std::string(A)+",1000,,sat";
    Check(ReadBatchCsv(lf,1).staged.has_value(),"LF and final record without terminator");
    const std::string nul("y\0z",3);
    for (const auto& suffix:{std::string("\"missing"),std::string("one,two"),std::string("1,2,3,4,5"),
             std::string("1,2,\"3\"x,4"),std::string("1,2,\"3\",4\r"),std::string("1,2,un\"quoted,4"),
             Payment(B,"1",std::string(MAX_BATCH_CELL+1,'x'),"sat"),Payment(B,"1",nul,"sat")}) {
        auto rejected=ReadBatchCsv(std::string(HEADER)+a+suffix,1);
        Check(!rejected.staged && rejected.error.starts_with("CSV record 3:"),"failure preserves atomic CSV parsing and record identity");
    }
    for (const auto& text:{std::string(HEADER),std::string("Address,Amount,Label\nrow"),std::string("\xff"),
             std::string(MAX_BATCH_CSV+1,'x')}) Check(!ReadBatchCsv(text,1).staged,"reject empty, invalid header, UTF-8 or over bound");
    std::stop_source stop;
    stop.request_stop();
    Check(!ReadBatchCsv(std::string(HEADER)+a,1,stop.get_token()).staged,"cancelled parse");
}
void Payments()
{
    const auto exact=Validate(Payment(A,"Max","invoice","sat")+Payment(B,"0.00001234","","BTC"));
    Check(exact.ready && exact.fixed==Money::Satoshis(1234) && exact.ready->rows[0].maximum && exact.ready->rows[0].amount.empty(),"one exclusive Max and exact fixed amount");
    Check(SamePayments(*exact.ready,*ValidateBatch(StageRecipients(*exact.ready),MAIN).ready),"staging is a reversible public representation");
    auto bad=Validate(Payment(A,"Max","","BTC")+Payment(B,"Max","","sat"));
    Check(!bad.ready && !bad.rows[0].amount.empty() && !bad.rows[1].amount.empty(),"both Max conflicts identified");
    constexpr auto witness="bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4";
    bad=Validate(Payment(witness,"1000","","sat")+Payment("BC1QW508D6QEJXTDG4Y5R3ZARVARY0C5XW7KV8F3T4","1000","","sat"));
    Check(!bad.ready && bad.errors==2 && !bad.rows[0].address.empty() && !bad.rows[1].address.empty(),"duplicate canonical payment scripts");
    bad=Validate(Payment(A,"21000000","","BTC")+Payment(B,"1","","sat"));
    Check(!bad.ready && !bad.error.empty(),"sum overflow at inclusive monetary limit");
    bad=Validate(Payment(A,"01.50","","sat")+Payment(B,"1000","","mBTC"));
    Check(!bad.ready && !bad.rows[0].amount.empty() && !bad.rows[1].unit.empty(),"unsupported precision and unit");
    bad=Validate(Payment("bitcoin:"+std::string(A),"0.000000001","","BTC")+Payment("","","","BTC"));
    Check(!bad.ready && bad.errors==2 && !bad.rows[0].address.empty() && !bad.rows[0].amount.empty(),"URI, empty row and excess precision");
    auto tiny=Validate(Payment(A,"1","","sat"));
    Check(tiny.ready && tiny.rows[0].reference_dust,"reference dust without trusted policy stays a warning");
    tiny=Validate(Payment(A,"1","","sat"),Money::Satoshis(546));
    Check(!tiny.ready && !tiny.rows[0].amount.empty(),"known dust policy rejects below minimum");
    const auto foreign=ReadBatchCsv(std::string(HEADER)+Payment(A,"1000","","sat"),1);
    Check(foreign.staged && !ValidateBatch(*foreign.staged,protocol::DisplayNetwork::Regtest).ready,"network checked independently from CSV");
}
void ExportAndLarge()
{
    auto exact=Validate(Payment(A,"1000","","sat"));
    Check(exact.ready.has_value(),"initial export fixture");
    for (const std::string label:{"=1+2","+ONE","-3","@SUM","  =leading","\u00a0\u200b+leading","\ufeff@leading","\uff1dwide","'original"}) {
        exact.ready->rows[0].label=label;
        const auto csv=WriteBatchCsv(*exact.ready);
        Check(csv && csv->find("\"'"+label+"\"")!=std::string::npos,"formula-safe reversible text");
        const auto staged=ReadBatchCsv(*csv,77);
        const auto restored=staged.staged ? ValidateBatch(*staged.staged,MAIN) : BatchValidation{};
        Check(restored.ready && SamePayments(*exact.ready,*restored.ready),"protected file round trip restores exact payment semantics");
    }
    const std::string protected_header="Address,Amount,Label (Roots text v1),Unit\n";
    Check(!ReadBatchCsv(protected_header+Payment(A,"1000","=malformed","sat"),1).staged,"protected schema requires formula escape");
    Check(!ReadBatchCsv(protected_header+Payment(A,"1000","'malformed","sat"),1).staged,"protected schema rejects noncanonical escape");
    std::string batch(HEADER);
    constexpr char hex[]="0123456789abcdef";
    for (size_t i=0;i<MAX_SEND_RECIPIENTS;++i) {
        std::string script="0014"+std::string(40,'0');
        script[4]=hex[(i>>8)&15];
        script[5]=hex[(i>>4)&15];
        script[6]=hex[i&15];
        const auto address=protocol::OutputAddress(script,MAIN);
        Check(address.has_value(),"generate distinct valid witness address");
        batch+=Payment(*address,"1000","row","sat");
    }
    const auto read=ReadBatchCsv(batch,1);
    const auto large=ValidateBatch(*read.staged,MAIN);
    Check(large.ready && large.ready->rows.size()==MAX_SEND_RECIPIENTS && large.fixed==Money::Satoshis(1024000),"bounded 1,024 row batch has exact sum");
    Check(!ReadBatchCsv(batch+Payment(A,"1000","","sat"),1).staged,"record 1,025 rejected without truncation");
}
void Local(const std::filesystem::path& dir)
{
    const auto path=dir/"export.csv", parent_alias=dir/"alias", leaf=dir/"leaf.csv", subdir=dir/"local";
    const auto csv=std::string(HEADER)+Payment(A,"1234","File","sat");
    Check(SavePublicFile(path.string(),csv,MAX_BATCH_CSV,true).empty(),"exclusive local CSV saved");
    struct stat file{};
    Check(stat(path.c_str(),&file)==0 && (file.st_mode&0777)==0600,"export mode 0600");
    Check(!SavePublicFile(path.string(),"other",MAX_BATCH_CSV,true).empty(),"existing path preserved");
    Check(ReadPublicFile(path.string(),MAX_BATCH_CSV,{},true).bytes==csv,"read exact exported bytes");
    std::filesystem::create_directory(subdir);
    std::filesystem::create_directory_symlink(dir,parent_alias);
    std::filesystem::create_symlink(path,leaf);
    Check(!ReadPublicFile(leaf.string(),MAX_BATCH_CSV,{},true).error.empty(),"reject symlink leaf");
    Check(!ReadPublicFile((parent_alias/"export.csv").string(),MAX_BATCH_CSV,{},true).error.empty(),"reject symlink directory read");
    Check(!SavePublicFile((parent_alias/"must-not-exist").string(),csv,MAX_BATCH_CSV,true).empty()
        && !std::filesystem::exists(dir/"must-not-exist"),"reject symlink directory export");
    Check(!ReadPublicFile(subdir.string(),MAX_BATCH_CSV,{},true).error.empty(),"reject nonregular source");
    Check(ReadPublicFile(path.string(),7,{},true).limited,"bounded source cannot be truncated into success");
    std::stop_source stop;
    stop.request_stop();
    Check(ReadPublicFile(path.string(),MAX_BATCH_CSV,stop.get_token(),true).cancelled,"cancelled descriptor read");
}
}
int main(int argc,char** argv)
{
    try {
        if (argc!=2) throw std::runtime_error("owner-controlled test directory required");
        Grammar();
        Payments();
        ExportAndLarge();
        Local(argv[1]);
        std::cout<<"Batch model and local file checks passed: "<<checks<<'\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<"Batch model failure after "<<checks<<" positive checks: "<<e.what()<<'\n';
        return 1;
    }
}
