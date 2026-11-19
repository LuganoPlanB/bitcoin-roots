// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/send_batch.h"
#include "adapters/rpc/json.h"
#include "services/transaction_documents.h"

#include <algorithm>
#include <map>
#include <sodium.h>

namespace roots::gui {
namespace {
constexpr std::string_view LABEL="Label";
constexpr std::string_view SAFE_LABEL="Label (Roots text v1)";

bool PublicText(std::string_view text,size_t maximum)
{
    return text.size()<=maximum && rpc::ValidUtf8(text)
        && std::ranges::none_of(text,[](unsigned char c){return c<32 || c==127;});
}
// Recognize spreadsheet formula markers after trim/formatting characters.
// Input is validated UTF-8 before this byte-level code point walk.
bool Formula(std::string_view text)
{
    while (!text.empty()) {
        const auto lead=static_cast<unsigned char>(text.front());
        const size_t count=lead<0x80 ? 1 : lead<0xe0 ? 2 : lead<0xf0 ? 3 : 4;
        if (count>text.size()) return true;
        uint32_t cp=lead & (count==1 ? 0x7f : count==2 ? 0x1f : count==3 ? 0x0f : 0x07);
        for (size_t i=1;i<count;++i) cp=(cp<<6)|(static_cast<unsigned char>(text[i])&0x3f);
        if (cp=='=' || cp=='+' || cp=='-' || cp=='@' || cp=='\t' || cp=='\r' || cp=='\n'
            || cp==0xff1d || cp==0xff0b || cp==0xff0d || cp==0xff20) return true;
        if (!(cp<=0x20 || (cp>=0x7f && cp<=0xa0) || cp==0x1680 || cp==0x180e
            || (cp>=0x2000 && cp<=0x200f) || (cp>=0x2028 && cp<=0x202f) || (cp>=0x205f && cp<=0x206f)
            || cp==0x3000 || cp==0xfeff)) return false;
        text.remove_prefix(count);
    }
    return false;
}
std::string Cell(std::string_view text)
{
    std::string out="\"";
    if (text.starts_with('\'') || Formula(text)) out+='\'';
    for (const char c:text) {
        if (c=='"') out+='"';
        out+=c;
    }
    out+='"';
    return out;
}

enum class CsvState { Start, Bare, Quoted, Closed };
}

SendRecipients StageRecipients(const SendRecipients& recipients)
{
    auto staged=recipients;
    for (auto& row:staged.rows) {
        if (row.maximum) row.amount="Max";
    }
    return staged;
}
BatchValidation ValidateBatch(const SendRecipients& staged,protocol::DisplayNetwork network,std::optional<Money> dust)
{
    BatchValidation result;
    if (staged.rows.empty() || staged.rows.size()>MAX_SEND_RECIPIENTS) {
        result.error="Provide between 1 and 1,024 payment rows.";
        return result;
    }
    SendRecipients normalized=staged;
    result.rows.resize(normalized.rows.size());
    std::map<std::string,size_t> scripts;
    std::optional<size_t> maximum;
    for (size_t i=0;i<normalized.rows.size();++i) {
        auto& row=normalized.rows[i];
        auto& issue=result.rows[i];
        const auto parsed=ParsePaymentRequest(row.pay_to,network);
        if (!PublicText(row.pay_to,128) || row.pay_to.find(':')!=std::string::npos)
            issue.address="Enter a Bitcoin address. Apply payment URIs in Send.";
        else if (!parsed.error.empty() || parsed.address.empty()) issue.address=parsed.error.empty()
            ? "Enter an address for this wallet network." : parsed.error;
        if (!PublicText(row.label,MAX_RECIPIENT_LABEL)) issue.label="Use one line of UTF-8 text, at most 255 bytes, without control characters.";
        if (row.unit!=RecipientUnit::BTC && row.unit!=RecipientUnit::Sats) issue.unit="Choose BTC or sat.";
        row.maximum=row.amount=="Max";
        if (row.maximum) {
            row.amount.clear();
            if (maximum) {
                issue.amount="Use Max on only one row.";
                result.rows[*maximum].amount=issue.amount;
            } else maximum=i;
        } else if (!PublicText(row.amount,MAX_AMOUNT_TEXT)) issue.amount="Enter an exact amount or Max, at most 128 bytes.";
        if (issue.address.empty()) {
            row.pay_to=row.address=parsed.address;
            const auto script=protocol::AddressScript(row.address,network);
            if (!script) issue.address="Enter an address for this wallet network.";
            else if (auto [at,inserted]=scripts.emplace(*script,i); !inserted) {
                issue.address="Duplicate payment script. Keep one row per destination.";
                result.rows[at->second].address=issue.address;
            }
        }
        if (issue.amount.empty() && issue.unit.empty()) {
            const auto checked=CheckRecipient(row,network,dust);
            issue.amount=checked.amount_error;
            issue.reference_dust=checked.below_reference_dust;
        }
    }
    result.maximum=maximum.has_value();
    result.errors=std::ranges::count_if(result.rows,&BatchIssue::Invalid);
    if (result.errors) return result;
    const auto sum=CheckSendFunds(normalized,{});
    if (sum.above_money_limit || !sum.fixed_outputs) {
        result.error="The fixed recipient total exceeds the Bitcoin monetary limit or is invalid.";
        return result;
    }
    if (!normalized.ValidPublicData(network)) {
        result.error="The staged rows have invalid public values or identities.";
        return result;
    }
    result.fixed=sum.fixed_outputs;
    result.ready=std::move(normalized);
    return result;
}
bool SamePayments(const SendRecipients& lhs,const SendRecipients& rhs)
{
    return std::ranges::equal(lhs.rows,rhs.rows,[](const auto& a,const auto& b){
        return a.address==b.address && a.pay_to==b.pay_to && a.label==b.label
            && a.unit==b.unit && a.maximum==b.maximum && (a.maximum || a.amount==b.amount);
    });
}

BatchCsv ReadBatchCsv(std::string_view bytes,uint64_t next_id,std::stop_token stop)
{
    if (bytes.empty() || bytes.size()>MAX_BATCH_CSV || !rpc::ValidUtf8(bytes))
        return {{}, "Choose a nonempty UTF-8 CSV up to 1 MiB."};
    if (bytes.starts_with("\xef\xbb\xbf")) bytes.remove_prefix(3);
    SendRecipients out;
    out.rows.clear();
    out.next=next_id;
    std::vector<std::string> fields;
    std::string field,error;
    CsvState state=CsvState::Start;
    bool header{},protected_text{};
    size_t record=1;
    const auto fail=[&](std::string_view reason) {
        return BatchCsv{{},"CSV record "+std::to_string(record)+": "+std::string(reason)};
    };
    const auto push=[&] {
        if (fields.size()>=4) return false;
        fields.push_back(std::move(field));
        field.clear();
        state=CsvState::Start;
        return true;
    };
    const auto finish=[&] {
        if (!push() || fields.size()!=4) {error="expected four columns.";return false;}
        if (!header) {
            if (fields[0]!="Address" || fields[1]!="Amount" || (fields[2]!=LABEL && fields[2]!=SAFE_LABEL) || fields[3]!="Unit") {
                error="expected the Address,Amount,Label,Unit schema.";return false;
            }
            protected_text=fields[2]==SAFE_LABEL;
            header=true;
        } else {
            if (out.rows.size()>=MAX_SEND_RECIPIENTS || !out.next || out.next==UINT64_MAX) {
                error="the recipient or row identity limit was exceeded.";return false;
            }
            if (protected_text) for (auto& cell:fields) {
                if (cell.starts_with('\'')) {
                    const std::string_view decoded=std::string_view(cell).substr(1);
                    if (!decoded.starts_with('\'') && !Formula(decoded)) {error="invalid protected text cell.";return false;}
                    cell.erase(0,1);
                } else if (Formula(cell)) {error="unescaped formula-like protected text cell.";return false;}
            }
            const auto unit=fields[3]=="BTC" ? RecipientUnit::BTC : fields[3]=="sat" ? RecipientUnit::Sats : RecipientUnit(255);
            out.rows.push_back({out.next++,std::move(fields[0]),{},std::move(fields[2]),std::move(fields[1]),unit});
        }
        fields.clear();
        ++record;
        return true;
    };
    for (size_t i=0;i<bytes.size();++i) {
        if ((i&4095)==0 && stop.stop_requested()) return {{}, "CSV read was cancelled."};
        const char c=bytes[i];
        if (c=='\0') return fail("NUL is not supported in payment fields.");
        if (state==CsvState::Quoted) {
            if (c=='"') state=CsvState::Closed;
            else field+=c;
        } else if (state==CsvState::Closed && c=='"') {
            field+='"';
            state=CsvState::Quoted;
        } else if (c==',') {
            if (!push()) return fail("too many columns.");
        } else if (c=='\n' || c=='\r') {
            if (c=='\r' && (i+1>=bytes.size() || bytes[++i]!='\n')) return fail("expected CRLF or LF.");
            if (!finish()) return fail(error);
        } else if (c=='"' && state==CsvState::Start) {
            state=CsvState::Quoted;
        } else {
            if (c=='"' || state==CsvState::Closed) return fail("misplaced quote.");
            state=CsvState::Bare;
            field+=c;
        }
        if (field.size()>MAX_BATCH_CELL) return fail("a field exceeds 4,096 bytes.");
    }
    if (stop.stop_requested()) return {{}, "CSV read was cancelled."};
    if (state==CsvState::Quoted) return fail("unterminated quoted field.");
    if ((state!=CsvState::Start || !fields.empty()) && !finish()) return fail(error);
    if (out.rows.empty()) return {{}, "CSV contains no payment rows."};
    out.selected=out.rows.front().id;
    return {std::move(out),{}};
}
std::optional<std::string> WriteBatchCsv(const SendRecipients& exact)
{
    if (exact.rows.empty() || exact.rows.size()>MAX_SEND_RECIPIENTS) return {};
    std::string result="Address,Amount,"+std::string(SAFE_LABEL)+",Unit\r\n";
    for (const auto& row:exact.rows) {
        if (!PublicText(row.address,128) || !PublicText(row.label,MAX_RECIPIENT_LABEL) || !PublicText(row.amount,MAX_AMOUNT_TEXT)
            || (row.unit!=RecipientUnit::BTC && row.unit!=RecipientUnit::Sats)) return {};
        result+=Cell(row.address)+","+Cell(row.maximum ? "Max" : row.amount)+","+Cell(row.label)+","
            +Cell(row.unit==RecipientUnit::BTC ? "BTC" : "sat")+"\r\n";
        if (result.size()>MAX_BATCH_CSV) return {};
    }
    return result;
}
void WipeRecipients(SendRecipients& rows)
{
    for (auto& row:rows.rows) {
        for (auto* field:{&row.pay_to,&row.address,&row.label,&row.amount})
            if (!field->empty()) sodium_memzero(field->data(),field->size());
    }
    rows.rows.clear();
}
BatchFileRead::BatchFileRead(std::string path,uint64_t next_id)
    : worker([this,path=std::move(path),next_id](std::stop_token stop) mutable {
        BatchCsv parsed;
        PublicFileRead file;
        try {
            file=ReadPublicFile(path,MAX_BATCH_CSV,stop,true);
            if (file.cancelled) parsed.error="CSV read was cancelled.";
            else if (file.limited) parsed.error="CSV exceeds the size or read deadline.";
            else if (!file.error.empty()) parsed.error=std::move(file.error);
            else parsed=ReadBatchCsv(file.bytes,next_id,stop);
        } catch (...) {
            parsed.error="CSV could not be read within available resources.";
        }
        if (!file.bytes.empty()) sodium_memzero(file.bytes.data(),file.bytes.size());
        if (!path.empty()) sodium_memzero(path.data(),path.size());
        std::lock_guard guard(mutex);
        result=std::move(parsed);
    })
{
}
BatchFileRead::~BatchFileRead()
{
    worker.request_stop();
    if (worker.joinable()) worker.join();
    if (result && result->staged) WipeRecipients(*result->staged);
}
void BatchFileRead::Cancel()
{
    if (std::this_thread::get_id()==owner) worker.request_stop();
}
std::optional<BatchCsv> BatchFileRead::Take()
{
    if (std::this_thread::get_id()!=owner) return {};
    std::optional<BatchCsv> ready;
    {
        std::lock_guard guard(mutex);
        ready=std::exchange(result,std::nullopt);
    }
    if (ready && worker.joinable()) worker.join();
    return ready;
}
}
