// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/broadcast.h"
#include "app/replacement.h"
#include "app/history_workspace.h"
#include "app/recovery.h"
#include <algorithm>
#include <charconv>
#include <cstdio>

namespace roots::gui::app {
namespace {
using Stage=BroadcastStage;
using J=rpc::Json;
using Clock=std::chrono::steady_clock;
constexpr auto PREFLIGHT_AGE=std::chrono::seconds(30);
constexpr auto STATUS_AGE=std::chrono::seconds(60);
constexpr auto MONITOR_INTERVAL=std::chrono::seconds(5);
constexpr auto MONITOR_MIN_GAP=std::chrono::seconds(2);
constexpr auto MONITOR_LIFETIME=std::chrono::minutes(10);

int64_t UnixTime()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string Utc(int64_t milliseconds)
{
    using namespace std::chrono;
    const sys_time<std::chrono::milliseconds> time{std::chrono::milliseconds(milliseconds)};
    const auto day=floor<days>(time);
    const year_month_day date(day);
    const hh_mm_ss clock(floor<seconds>(time-day));
    char text[40]{};
    std::snprintf(text,sizeof(text),"%04d-%02u-%02u %02d:%02d:%02d UTC",int(date.year()),unsigned(date.month()),unsigned(date.day()),
        int(clock.hours().count()),int(clock.minutes().count()),int(clock.seconds().count()));
    return text;
}

bool SameProvenance(const ApprovedFinalTransaction& a,const ApprovedFinalTransaction& b)
{
    return a.approval==b.approval && a.collected==b.collected && a.collected_source==b.collected_source
        && a.final_source==b.final_source && a.final.fee==b.final.fee
        && a.final.size==b.final.size && a.final.stripped_size==b.final.stripped_size
        && a.final.weight==b.final.weight && a.final.vsize==b.final.vsize;
}
bool EquivalentBytes(const ApprovedFinalTransaction& a,const ApprovedFinalTransaction& b)
{
    return a.final.document && b.final.document && a.final.document->wire==b.final.document->wire
        && a.final.document->transaction && b.final.document->transaction
        && a.final.document->transaction->txid==b.final.document->transaction->txid
        && a.final.document->transaction->wtxid==b.final.document->transaction->wtxid
        && SameProvenance(a,b);
}
bool Matches(const J* value,std::string_view exact)
{
    return value && value->kind==J::Kind::String && value->text==exact;
}
std::optional<int64_t> Integer(const J* value)
{
    if (!value || value->kind!=J::Kind::Number) return {};
    int64_t number{};
    const auto [end,error]=std::from_chars(value->text.data(),value->text.data()+value->text.size(),number);
    if (error!=std::errc{} || end!=value->text.data()+value->text.size()) return {};
    return number;
}
bool ExactRecord(const storage::BroadcastRecord& record,const protocol::TransactionDocument& raw)
{
    return raw.transaction && raw.transaction->txid==record.identity.txid && raw.transaction->wtxid==record.identity.wtxid
        && SigningDigest(raw.wire)==record.identity.wire_digest
        && (record.wire_hex.empty() || record.wire_hex==protocol::TransactionHex(raw.wire));
}
std::string Rejection(const J* reason)
{
    if (!reason || reason->kind!=J::Kind::String || reason->text.empty() || reason->text.size()>120
        || !std::ranges::all_of(reason->text,[](unsigned char c){
            return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='-' || c=='_' || c==' ' || c=='.';
        })) return "reason unavailable";
    return reason->text;
}
struct Wire {
    ServerCandidate target;
    std::string chain,genesis,hex,txid,wtxid;
    Money fee;
    std::optional<CancellationIntent> cancellation;
    std::optional<ChildPackageIntent> child_package;
};
rpc::Reply Call(rpc::Transport& transport,const Wire& wire,uint64_t id,std::string method,std::string params,
    rpc::Effect effect,std::stop_token stop)
{
    rpc::Limits limits;
    limits.total_timeout=std::chrono::seconds(5);
    limits.response_bytes=65536;
    return transport.Call(wire.target.connection,{id,std::move(method),std::move(params),{},effect},stop,limits);
}
std::pair<Stage,std::string> Test(rpc::Transport& transport,const Wire& wire,std::stop_token stop)
{
    const auto query=[&](uint64_t id,std::string method,std::string params="[]"){
        return Call(transport,wire,id,std::move(method),std::move(params),rpc::Effect::ReadOnly,stop);
    };
    const auto genesis=query(1,"getblockhash","[0]");
    if (genesis.error!=rpc::Error::None || !genesis.result || !Matches(&*genesis.result,wire.genesis))
        return {Stage::Refused,"The node did not prove the expected genesis. No transaction was submitted."};
    const auto chain=query(2,"getblockchaininfo");
    const auto* tip=chain.result ? chain.result->Find("bestblockhash") : nullptr;
    const auto* ibd=chain.result ? chain.result->Find("initialblockdownload") : nullptr;
    if (chain.error!=rpc::Error::None || !chain.result || !Matches(chain.result->Find("chain"),wire.chain)
        || !tip || tip->kind!=J::Kind::String || !Hash256::Parse(tip->text)
        || !ibd || ibd->kind!=J::Kind::Boolean || ibd->text!="false")
        return {Stage::Refused,"The node did not report a current tip for this network. No transaction was submitted."};
    const auto reply=query(3,"testmempoolaccept","[["+rpc::Quote(wire.hex)+"],"+
        Money::Satoshis(BROADCAST_MAX_SAT_PER_KVB).btc()+"]");
    if (reply.error!=rpc::Error::None || !reply.result || reply.result->kind!=J::Kind::Array || reply.result->items.size()!=1)
        return {Stage::Refused,"A bounded policy result was unavailable. No transaction was submitted."};
    const auto& item=reply.result->items.front();
    const auto* allowed=item.Find("allowed");
    if (!Matches(item.Find("txid"),wire.txid) || !Matches(item.Find("wtxid"),wire.wtxid)
        || !allowed || allowed->kind!=J::Kind::Boolean)
        return {Stage::Refused,"The policy result did not identify these exact final bytes. No transaction was submitted."};
    if (allowed->text=="false")
        return {Stage::Rejected,"Node policy rejected this transaction: "+Rejection(item.Find("reject-reason"))+
            ". This check reports this node's current admission decision."};
    const auto* fees=item.Find("fees");
    const auto* base=fees ? fees->Find("base") : nullptr;
    if (!base || (base->kind!=J::Kind::String && base->kind!=J::Kind::Number) || Money::ParseJsonBtc(base->text)!=wire.fee)
        return {Stage::Refused,"The node fee did not match the independently verified input and output amounts."};
    const auto current=query(4,"getbestblockhash");
    if (current.error!=rpc::Error::None || !current.result || !Matches(&*current.result,tip->text))
        return {Stage::Refused,"The node tip changed during the policy check. Run a fresh check."};
    return {Stage::Ready,"The node accepted these bytes in a policy check. Ready for a separate Broadcast action."};
}
std::pair<Stage,std::string> Send(rpc::Transport& transport,const Wire& wire,std::stop_token stop)
{
    const auto sent=Call(transport,wire,5,"sendrawtransaction","["+rpc::Quote(wire.hex)+","+
        Money::Satoshis(BROADCAST_MAX_SAT_PER_KVB).btc()+",0]",rpc::Effect::Mutation,stop);
    if (sent.error==rpc::Error::None && sent.result && Matches(&*sent.result,wire.txid))
        return {Stage::Acknowledged,"The node acknowledged the transaction ID. Confirmation and payment status are unknown."};
    // The exact typed RPC errors below are transaction rejection codes. An
    // already-known or other RPC error can coexist with an earlier admission.
    if (sent.error==rpc::Error::Rpc && (sent.rpc_code==-22 || sent.rpc_code==-26))
        return {Stage::Refused,"The node refused submission (RPC "+std::to_string(*sent.rpc_code)+"). The attempt remains recorded."};
    if (sent.error==rpc::Error::Rpc && sent.rpc_code==-27)
        return {Stage::Uncertain,"The node reports the transaction is already known. Check Status for independently matched evidence."};
    if (sent.error!=rpc::Error::None && !sent.mutation_uncertain)
        return {Stage::Unsent,"The submission could not be sent. The attempt remains recorded."};
    return {Stage::Uncertain,"The submission outcome is uncertain. The attempt remains recorded."};
}
Wire RequestWire(const ApprovedFinalTransaction& artifact,const BackendCapabilities& backend)
{
    const auto& raw=*artifact.final.document;
    const auto& tx=*raw.transaction;
    return {backend.candidate,backend.chain,backend.genesis,protocol::TransactionHex(raw.wire),
        tx.txid->hex(),tx.wtxid->hex(),artifact.final.fee,artifact.approval.reviewed.cancellation,artifact.approval.reviewed.child_package};
}

struct StatusRequest {
    ConnectionProfile profile;
    std::string genesis,chain,txid,wtxid,hex;
    protocol::DisplayNetwork network;
    std::optional<std::string> wallet,prior_block;
    std::vector<Outpoint> inputs;
    bool mempool{},raw{},header{},spenders{},mempool_spenders{},seen_mempool{};
};
struct StatusProbe {
    Stage stage;
    std::string message;
    std::optional<TransactionObservation> observation;
};
struct BlockFacts {
    int64_t confirmations{};
    std::optional<int64_t> height,time;
};
struct CompetingSpend {
    std::string txid,wtxid;
    Outpoint point;
    std::optional<std::string> block;
};
bool SchemaArgument(const BackendCapabilities& caps,std::string_view name)
{
    return caps.Supports("gettxspendingprevout",name)==SupportReason::Available
        || caps.Supports("gettxspendingprevout","options."+std::string(name))==SupportReason::Available;
}

StatusProbe Observe(rpc::Transport& transport,const StatusRequest& request,std::stop_token stop)
{
    using Location=TransactionLocation;
    const auto deadline=Clock::now()+std::chrono::seconds(12);
    uint64_t id{};
    const auto read=[&](std::string method,std::string params="[]",std::optional<std::string> wallet={}) {
        rpc::Limits limits;
        limits.total_timeout=std::min(std::chrono::milliseconds(4000),
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()));
        if (limits.total_timeout.count()<=0 || stop.stop_requested()) return rpc::Reply{rpc::Error::Cancelled};
        limits.connect_timeout=std::min(limits.total_timeout,std::chrono::milliseconds(2000));
        limits.response_bytes=512*1024;
        return transport.Call(request.profile,{++id,std::move(method),std::move(params),std::move(wallet),rpc::Effect::ReadOnly},stop,limits);
    };
    const auto genesis=read("getblockhash","[0]");
    if (!genesis.result || !Matches(&*genesis.result,request.genesis))
        return {Stage::StatusUnknown,"Status Unknown. The node did not prove the recorded genesis.",{}};
    const auto chain=read("getblockchaininfo");
    const auto* tip=chain.result ? chain.result->Find("bestblockhash") : nullptr;
    const auto* ibd=chain.result ? chain.result->Find("initialblockdownload") : nullptr;
    if (!chain.result || !Matches(chain.result->Find("chain"),request.chain) || !tip || tip->kind!=J::Kind::String
        || !Hash256::Parse(tip->text) || !ibd || ibd->kind!=J::Kind::Boolean || ibd->text!="false")
        return {Stage::StatusUnknown,"Status Unknown. The verified node has no current chain tip.",{}};
    const auto tip_height=Integer(chain.result->Find("blocks"));
    bool mismatch{};
    const auto header=[&](const std::string& hash)->std::optional<BlockFacts> {
        if (!request.header) return {};
        const auto reply=read("getblockheader","["+rpc::Quote(hash)+",true]");
        if (!reply.result) return {};
        const auto count=Integer(reply.result->Find("confirmations"));
        const auto height=Integer(reply.result->Find("height"));
        const auto time=Integer(reply.result->Find("time"));
        if (!Matches(reply.result->Find("hash"),hash) || !count || (*count!=-1 && *count<=0)
            || (height && (*height<0 || (*count>0 && tip_height && (*height>*tip_height || *count-1!=*tip_height-*height))))
            || (time && (*time<0 || *time>253402300799))) {
            mismatch=true;
            return {};
        }
        return BlockFacts{*count,height,time};
    };

    const auto key="["+rpc::Quote(request.txid)+"]";
    std::string wallet="Unavailable",mempool="Unavailable",chain_result="Unavailable";
    std::optional<std::pair<std::string,int64_t>> block;
    std::optional<std::string> wallet_replacement;
    bool in_mempool{},mempool_missing{};
    const auto check_bytes=[&](const J& transaction) {
        if (!Matches(transaction.Find("hex"),request.hex)) {
            mismatch=true;
            return false;
        }
        const auto confirmations=Integer(transaction.Find("confirmations"));
        if (confirmations && *confirmations>0) {
            const auto* hash=transaction.Find("blockhash");
            if (!hash || hash->kind!=J::Kind::String || !Hash256::Parse(hash->text)) {
                mismatch=true;
                return false;
            }
            const auto proof=std::pair{hash->text,*confirmations};
            if (block && *block!=proof) {
                mismatch=true;
                return false;
            }
            block=proof;
        }
        return true;
    };
    if (request.wallet) {
        const auto found=read("gettransaction",key,request.wallet);
        if (found.result) {
            const bool exact=check_bytes(*found.result);
            wallet=exact ? "Exact bytes recorded by the wallet" : "Response did not match";
            const auto* replacement=found.result->Find("replaced_by_txid");
            if (exact && replacement && replacement->kind==J::Kind::String && Hash256::Parse(replacement->text))
                wallet_replacement=replacement->text;
        } else if (found.error==rpc::Error::Rpc && found.rpc_code==-5) wallet="Lookup did not find a match";
    }
    if (request.mempool) {
        const auto found=read("getmempoolentry",key);
        if (found.result) {
            const auto* hash=found.result->Find("wtxid");
            if (Matches(hash,request.wtxid)) {
                in_mempool=true;
                mempool="Exact witness hash observed";
            } else if (hash) {
                mismatch=true;
                mempool="Response did not match";
            } else mempool="Witness identity unavailable";
        } else if (found.error==rpc::Error::Rpc && found.rpc_code==-5) {
            mempool_missing=true;
            mempool="Lookup did not find a match";
        }
    }
    if (request.raw) {
        const auto found=read("getrawtransaction","["+rpc::Quote(request.txid)+",true]");
        if (found.result) {
            if (!Matches(found.result->Find("txid"),request.txid) || !Matches(found.result->Find("hash"),request.wtxid)) {
                mismatch=true;
                chain_result="Response did not match";
            } else chain_result=check_bytes(*found.result) ? "Exact bytes found; active block unproved" : "Response did not match";
        } else if (found.error==rpc::Error::Rpc && found.rpc_code==-5) chain_result="Lookup did not find indexed bytes";
    }
    bool confirmed{};
    if (block) {
        const auto facts=header(block->first);
        if (facts && facts->confirmations==block->second) {
            confirmed=true;
            chain_result="Exact bytes anchored to an active block. Confirmations: "+std::to_string(block->second);
            if (facts->height) chain_result+="\nBlock height: "+std::to_string(*facts->height);
            if (facts->time) chain_result+="\nBlock time: "+Utc(*facts->time*1000);
            chain_result+="\nBlock hash: "+block->first;
        } else {
            mismatch|=facts.has_value();
            chain_result="Active block could not be verified";
        }
    }
    bool reorged{};
    if (request.prior_block && (!block || block->first!=*request.prior_block)) {
        const auto prior=header(*request.prior_block);
        reorged=prior && prior->confirmations==-1;
    }
    std::optional<CompetingSpend> competitor;
    if (request.spenders && !in_mempool && !confirmed && !mismatch && !request.inputs.empty()) {
        std::string outputs="[";
        for (const auto& point:request.inputs) {
            if (outputs.size()>1) outputs+=',';
            outputs+="{\"txid\":"+rpc::Quote(point.txid.hex())+",\"vout\":"+std::to_string(point.index)+"}";
        }
        outputs+=']';
        auto spent=read("gettxspendingprevout","["+outputs+",{\"return_spending_tx\":true}]");
        if (spent.error==rpc::Error::Rpc && spent.rpc_code==-1 && request.mempool_spenders)
            spent=read("gettxspendingprevout","["+outputs+",{\"return_spending_tx\":true,\"mempool_only\":true}]");
        if (spent.result) {
            if (spent.result->kind!=J::Kind::Array || spent.result->items.size()!=request.inputs.size()) mismatch=true;
            else for (size_t i=0;i<request.inputs.size();++i) {
                const auto& entry=spent.result->items[i];
                const auto& point=request.inputs[i];
                if (!Matches(entry.Find("txid"),point.txid.hex()) || Integer(entry.Find("vout"))!=point.index) {
                    mismatch=true;
                    break;
                }
                const auto* id=entry.Find("spendingtxid");
                const auto* bytes=entry.Find("spendingtx");
                const auto* at=entry.Find("blockhash");
                if (!id || Matches(id,request.txid)) continue;
                if (id->kind!=J::Kind::String || !Hash256::Parse(id->text) || !bytes || bytes->kind!=J::Kind::String
                    || (at && (at->kind!=J::Kind::String || !Hash256::Parse(at->text)))) {
                    mismatch=true;
                    break;
                }
                const auto parsed=protocol::ParseTransactionDocument(bytes->text,request.network);
                const auto* tx=parsed.document && !parsed.document->psbt_version && parsed.document->transaction
                    ? &*parsed.document->transaction : nullptr;
                if (!tx || !tx->txid || !tx->wtxid || !Matches(id,tx->txid->hex())
                    || std::ranges::none_of(tx->inputs,[&](const auto& input){return input.outpoint==point;})) {
                    mismatch=true;
                    break;
                }
                if (competitor) continue; // Keep one exact relationship and validate all returned input rows.
                if (at) {
                    const auto active=header(at->text);
                    if (active && active->confirmations>0)
                        competitor=CompetingSpend{id->text,tx->wtxid->hex(),point,at->text};
                } else if (request.mempool) {
                    const auto found=read("getmempoolentry","["+rpc::Quote(id->text)+"]");
                    if (found.result && Matches(found.result->Find("wtxid"),tx->wtxid->hex()))
                        competitor=CompetingSpend{id->text,tx->wtxid->hex(),point,{}};
                    else if (found.result && found.result->Find("wtxid")) mismatch=true;
                }
            }
        }
    }
    const auto final_tip=read("getbestblockhash");
    if (!final_tip.result || !Matches(&*final_tip.result,tip->text))
        return {Stage::StatusUnknown,"Status Unknown. The node tip changed or could not be rechecked.",{}};

    TransactionObservation data{Location::Unknown,tip->text,{},reorged};
    auto details="\nWallet: "+wallet+"\nMempool: "+mempool+"\nChain: "+chain_result;
    if (reorged) details+="\nReorg observed. The previously confirmed block is outside the active chain.\nFormer block: "+*request.prior_block;
    if (mismatch || (in_mempool && confirmed))
        return {Stage::StatusUnknown,"Status Unknown. The sources did not agree on the exact transaction."+details,data};
    if (confirmed) {
        data.location=Location::ActiveChain;
        data.active_block=block->first;
        return {Stage::StatusObserved,"The verified node reported the exact transaction in its active chain."+details,data};
    }
    if (in_mempool) {
        data.location=Location::Mempool;
        return {Stage::StatusObserved,"The exact transaction was observed in this node's mempool. Confirmation is Unknown."+details,data};
    }
    if (competitor) {
        data.location=Location::CompetingSpend;
        const auto& spend=*competitor;
        auto headline=spend.block ? "A conflicting spend was observed in the active chain."
            : request.seen_mempool && mempool_missing ? "The mempool now contains a different spend of the same input."
            : "A competing spend was observed in this node's mempool.";
        details+="\nShared input: "+spend.point.txid.hex()+":"+std::to_string(spend.point.index)
            +"\nCompeting transaction: "+spend.txid+"\nCompeting witness ID: "+spend.wtxid;
        if (spend.block) details+="\nConflicting block: "+*spend.block;
        if (wallet_replacement==spend.txid) details+="\nWallet reports this transaction was replaced by that exact spend.";
        return {Stage::StatusObserved,headline+details,data};
    }
    if (mempool_missing && request.seen_mempool) {
        data.location=Location::NoLongerObserved;
        return {Stage::StatusObserved,"No longer observed in this node's mempool. The reason and final outcome are Unknown."+details,data};
    }
    return {Stage::StatusUnknown,"Status Unknown. A missing or unavailable lookup does not prove that submission failed."+details,data};
}
}

std::string BroadcastRecordLabel(const storage::BroadcastRecord& entry)
{
    using Recorded=storage::BroadcastOutcome;
    const std::string name=entry.serial ? "Attempt "+std::to_string(entry.serial) : "Earlier attempt";
    const auto at=" · "+Utc(entry.outcome_ms.value_or(entry.attempted_ms));
    switch (entry.outcome) {
    case Recorded::Pending: return name+" · Outcome unrecorded; submission may have reached the node"+at;
    case Recorded::Unsent: return name+" · Request could not be sent"+at;
    case Recorded::Rejected: return name+" · Node refused this submission"+at;
    case Recorded::Acknowledged: return name+" · Node acknowledged the transaction ID"+at;
    case Recorded::Uncertain: return name+" · Submission outcome uncertain"+at;
    }
    return name+" · Outcome Unknown";
}

Broadcaster::~Broadcaster()
{
    if (worker.joinable()) worker.request_stop();
}
bool Broadcaster::Current(const SigningBasis& basis) const
{
    const auto& active=connection.Active();
    return !basis.offline && basis.coin_policy && active && !connection.Offline() && !connection.PrivacyLocked()
        && connection.Session()==basis.session && connection.Revision()==basis.profile && !connection.Busy()
        && active->candidate.connection==connection.Draft().connection
        && active->candidate.expected_genesis==connection.Draft().expected_genesis
        && active->genesis==basis.wallet.network.genesis.hex() && active->chain==protocol::DocumentChain(basis.proof.network)
        && VerifiedWalletBinding(*active)==basis.wallet;
}
bool Broadcaster::Current(const BroadcastStatusContext& scope) const
{
    const auto& active=connection.Active();
    return scope.document && scope.raw && !scope.raw->psbt_version && scope.raw->transaction
        && scope.raw->transaction->txid && scope.raw->transaction->wtxid && active && active->CanApply()
        && !connection.Offline() && !connection.PrivacyLocked() && !connection.Busy()
        && active->candidate.connection==connection.Draft().connection
        && active->candidate.expected_genesis==connection.Draft().expected_genesis
        && connection.Session()==scope.session && connection.Revision()==scope.profile
        && active->genesis==scope.network.genesis.hex() && active->chain==protocol::DocumentChain(protocol::DocumentNetwork(scope.network));
}
bool Broadcaster::Bound(const ApprovedFinalTransaction& artifact) const
{
    return attempt && attempt->final.document==artifact.final.document && SameProvenance(*attempt,artifact);
}
bool Broadcaster::Freezes(uint64_t document,const protocol::TransactionDocument* content) const
{
    if (!Busy() || !attempt || (view.stage!=Stage::Rechecking && view.stage!=Stage::Sending)) return false;
    return document==attempt->approval.reviewed.document || document==attempt->approval.reviewed.origin
        || document==attempt->approval.reviewed.wallet_document
        || (content && protocol::TransactionCommitment(*content)==attempt->approval.commitment);
}
Availability Broadcaster::Can(BroadcastAction action,const SigningApproval* approval,const ApprovedFinalTransaction* artifact,
    const RecoverySession* recovery) const
{
    if (std::this_thread::get_id()!=owner) return {false,"Use the current transaction owner."};
    if (Busy()) return {false,"A transaction policy check or submission is already pending."};
    if (!artifact || !approval || *approval!=artifact->approval)
        return {false,"Broadcast requires the final bytes from the current approved collection."};
    if (!Current(approval->reviewed) || !recovery || !recovery->BroadcastJournalAvailable()
        || recovery->Gate().Epoch()!=approval->reviewed.epoch)
        return {false,"A current online wallet, known input policy and durable local attempt storage are required."};
    const auto& caps=*connection.Active();
    for (const auto [method,arg]:{std::pair{"testmempoolaccept","rawtxs"},{"testmempoolaccept","maxfeerate"},
            {"sendrawtransaction","hexstring"},{"sendrawtransaction","maxfeerate"},{"sendrawtransaction","maxburnamount"}})
        if (caps.Supports(method,arg)!=SupportReason::Available)
            return {false,"This node has not proved support for bounded preflight and submission."};
    const auto& final=artifact->final;
    if (!final.document || !final.document->transaction || !final.document->transaction->txid
        || !final.document->transaction->wtxid || final.vsize==0 || final.vsize>protocol::MAX_TRANSACTION_WIRE
        || final.fee.sats()<0 || final.fee.sats()>int64_t(final.vsize)*(BROADCAST_MAX_SAT_PER_KVB/1000))
        return {false,"The exact fee must be within the 100 sat/vB submission limit."};
    const bool known=recorded==std::pair{approval->reviewed.wallet.network,*final.document->transaction->txid};
    if (action==BroadcastAction::Submit && known) return {false,"This transaction already has an attempt. Use Check Status."};
    if (action!=BroadcastAction::Check && (!Bound(*artifact) || view.stage!=Stage::Ready || Clock::now()-checked>PREFLIGHT_AGE))
        return {false,"Run a fresh node policy check before submitting these bytes."};
    if (action==BroadcastAction::Rebroadcast && !previous_serial)
        return {false,"A matching recorded attempt is required to rebroadcast."};
    return {true,{}};
}
bool Broadcaster::Launch(Stage stage,Work work)
{
    try {
        view.stage=stage;
        retired=false;
        worker=std::jthread([this,work=std::move(work),stage](std::stop_token stop){
            Outcome result{Stage::Refused,"The node policy check could not finish."};
            if (stage==Stage::Sending) result={Stage::Uncertain,"The submission outcome is uncertain."};
            if (stage==Stage::CheckingStatus) result={Stage::StatusUnknown,"Status Unknown. The read-only check could not finish."};
            try {result=work(stop);} catch (...) {}
            std::lock_guard lock(mutex);
            completed=std::move(result);
        });
        return true;
    } catch (...) {return false;}
}
void Broadcaster::Finish(Outcome outcome)
{
    view.stage=outcome.stage;
    view.message=std::move(outcome.message);
    view.observation=std::move(outcome.observation);
    view.original_retired=outcome.original_retired;
    if (view.stage==Stage::Ready) checked=Clock::now();
    else {
        attempt.reset();
        previous_serial.reset();
    }
}
std::optional<Broadcaster::Outcome> Broadcaster::TakeCompletion()
{
    std::optional<Outcome> reply;
    {
        std::lock_guard lock(mutex);
        reply=std::exchange(completed,{});
    }
    if (reply) worker.join();
    return reply;
}
void Broadcaster::FinishSend(Outcome outcome,RecoverySession* recovery)
{
    using Stored=storage::BroadcastOutcome;
    const auto result=outcome.stage==Stage::Acknowledged ? Stored::Acknowledged
        : outcome.stage==Stage::Refused ? Stored::Rejected : outcome.stage==Stage::Unsent ? Stored::Unsent : Stored::Uncertain;
    if (!sending || !recovery || !recovery->FinishBroadcastAttempt(sending->first,sending->second,result,
        std::max(sending->first.observed_ms,UnixTime())))
        outcome.message+=" The journal could not record the outcome; Check Status after unlocking or reopening.";
    sending.reset();
    send_lease.reset();
    Finish(std::move(outcome));
}
Receipt Broadcaster::Start(BroadcastAction action,const SigningReviews& reviews,const SigningBasis& basis,
    const ApprovedFinalTransaction& artifact,RecoverySession& recovery)
{
    if (std::this_thread::get_id()!=owner) return {false,"Use the current transaction owner."};
    const auto available=Can(action,reviews.Authorize(basis),&artifact,&recovery);
    if (!available.available) return {false,available.reason};
    const auto proven=reviews.Finalize(basis,artifact.collected);
    if (!proven.artifact || !EquivalentBytes(*proven.artifact,artifact))
        return {false,"The exact final bytes and independent signing proofs could not be reverified."};
    const auto& tx=*artifact.final.document->transaction;
    const auto stored=recovery.LoadBroadcastAttempt(basis.wallet.network,*tx.txid);
    if (!stored) return {false,"The local submission journal could not be verified."};
    const auto& prior=*stored.value;
    if (prior) {
        recorded=std::pair{basis.wallet.network,*tx.txid};
        if (!ExactRecord(*prior,*artifact.final.document))
            return {false,"The recorded transaction does not contain these exact final bytes."};
    }
    const auto serial=prior ? std::optional(prior->serial) : std::nullopt;
    if (action==BroadcastAction::Submit && prior)
        return {false,"This transaction already has an attempt. Choose Check Status or deliberately Rebroadcast the same bytes."};
    if (action==BroadcastAction::Rebroadcast && (!prior || serial!=previous_serial))
        return {false,"The recorded attempt changed. Check its status and node policy again."};
    auto wire=RequestWire(artifact,*connection.Active());
    status.reset();
    view.observation.reset();
    previous_serial=serial;
    attempt=artifact;
    view.document=basis.document;
    view.message=action==BroadcastAction::Check ? "Checking the node's policy for these exact bytes."
        : "Rechecking node policy before the requested submission.";
    if (!Launch(action==BroadcastAction::Check ? Stage::Checking : Stage::Rechecking,
        [this,wire=std::move(wire)](auto stop) {
            if (wire.cancellation || wire.child_package) {
                auto reason=CheckFeeRelationship(transport,wire.target,wire.cancellation,wire.child_package,stop);
                if (!reason.empty()) return Outcome{Stage::Refused,std::move(reason)+" No transaction was submitted.",{},true};
            }
            auto [stage,message]=Test(transport,wire,stop);
            return Outcome{stage,std::move(message)};
        })) {
        Finish({Stage::Refused,"The node policy check could not start."});
        return {false,view.message};
    }
    return {true,view.message};
}
void Broadcaster::Poll(const SigningReviews& reviews,const SigningApproval* approval,
    const ApprovedFinalTransaction* artifact,RecoverySession* recovery)
{
    if (std::this_thread::get_id()!=owner || status || (!Busy() && view.stage!=Stage::Ready)) return;
    const auto phase=view.stage;
    const bool current=approval && reviews.Authorize(approval->reviewed)==approval && artifact && Bound(*artifact)
        && *approval==artifact->approval && Current(approval->reviewed) && recovery && recovery->BroadcastJournalAvailable()
        && recovery->Gate().Epoch()==approval->reviewed.epoch;
    if (!Busy()) {
        if (!current || Clock::now()-checked>PREFLIGHT_AGE)
            Finish({Stage::Cancelled,"The prior policy check expired or its context changed. Run a fresh check."});
        return;
    }
    if (!current) Cancel();
    auto reply=TakeCompletion();
    if (!reply) return;
    if (phase==Stage::Sending) {
        if (retired && reply->stage!=Stage::Unsent)
            *reply={Stage::Uncertain,"Waiting ended after submission may have reached the node. The attempt remains recorded."};
        FinishSend(std::move(*reply),recovery);
        return;
    }
    if (retired) {
        Finish({Stage::Cancelled,"The policy check was retired. No transaction was submitted."});
        return;
    }
    if (phase!=Stage::Rechecking || reply->stage!=Stage::Ready) {
        if (reply->stage==Stage::Ready && previous_serial)
            reply->message="The node accepted these exact bytes in a policy check. You can choose Rebroadcast same bytes.";
        Finish(std::move(*reply));
        return;
    }
    const auto& tx=*artifact->final.document->transaction;
    const auto observed=UnixTime();
    auto wire=RequestWire(*artifact,*connection.Active());
    auto lease=recovery->BroadcastLease();
    if (!lease) {
        Finish({Stage::Refused,"The local submission lease is unavailable. No transaction was submitted."});
        return;
    }
    const auto& wallet=approval->reviewed.wallet;
    const storage::BroadcastAttempt identity{wallet.network,*tx.txid,*tx.wtxid,artifact->final_source,observed};
    const storage::BroadcastRecord record{identity,wire.hex,0,observed,storage::BroadcastOutcome::Pending,{},
        wallet.backend.value(),wallet.DaemonWallet()};
    const auto stored=recovery->BeginBroadcastAttempt(record,previous_serial);
    if (stored || stored.error==storage::Error::Conflict)
        recorded=std::pair{approval->reviewed.wallet.network,*tx.txid};
    if (!stored) {
        Finish(stored.error==storage::Error::Conflict
            ? Outcome{Stage::Recorded,"The recorded attempt changed. No transaction was submitted."}
            : Outcome{Stage::Refused,"The attempt could not be recorded durably. No transaction was submitted."});
        return;
    }
    send_lease=std::move(*lease.value);
    sending=std::pair{identity,*stored.value};
    view.message="Submission recorded. Waiting for acknowledgment from this node.";
    if (!Launch(Stage::Sending,[this,wire=std::move(wire)](auto stop){
        if (wire.cancellation || wire.child_package) {
            auto reason=CheckFeeRelationship(transport,wire.target,wire.cancellation,wire.child_package,stop);
            if (!reason.empty()) return Outcome{Stage::Unsent,std::move(reason)+" No transaction was submitted.",{},true};
        }
        auto [stage,message]=Send(transport,wire,stop);
        return Outcome{stage,std::move(message)};
    })) FinishSend({Stage::Unsent,"Submission could not start. The durable attempt remains recorded."},recovery);
}
Availability Broadcaster::CanCheckStatus(const BroadcastStatusContext& scope,const RecoverySession* recovery) const
{
    if (std::this_thread::get_id()!=owner || Busy())
        return {false,"Finish the current transaction operation first."};
    if (!Current(scope) || !recovery || !recovery->BroadcastJournalAvailable() || recovery->Gate().Epoch()!=scope.epoch)
        return {false,"Status Unknown while offline, locked, or on an unverified node."};
    const auto& backend=*connection.Active();
    for (const auto method:{"getblockhash","getblockchaininfo","getbestblockhash"})
        if (backend.Supports(method)!=SupportReason::Available)
            return {false,"The node cannot verify the network and tip for status."};
    return {true,{}};
}
Receipt Broadcaster::StartStatus(const BroadcastStatusContext& scope,RecoverySession& recovery,bool monitor)
{
    const auto permission=CanCheckStatus(scope,&recovery);
    if (!permission.available) return {false,permission.reason};
    const auto now=Clock::now();
    auto previous=std::move(status);
    status=StatusSession{scope,connection.Active()->candidate,monitor};
    status->began=now;
    if (previous && previous->scope==scope && previous->target==status->target) {
        status->continuous|=previous->continuous;
        if (previous->continuous) status->began=previous->began;
        status->prior_block=previous->prior_block;
        status->seen_mempool=previous->seen_mempool;
        status->last=previous->last;
        status->observed=previous->observed;
        status->refresh_tip=previous->refresh_tip;
    }
    auto started=LaunchStatus(scope,recovery);
    if (!started.accepted) status=std::move(previous);
    return started;
}
Receipt Broadcaster::LaunchStatus(const BroadcastStatusContext& scope,RecoverySession& recovery)
{
    const auto& tx=*scope.raw->transaction;
    const auto stored=recovery.LoadBroadcastAttempt(scope.network,*tx.txid);
    if (!stored || !*stored.value || !ExactRecord(**stored.value,*scope.raw))
        return {false,"No verifiable local submission attempt matches these exact bytes on this network."};
    const auto& entry=**stored.value;
    const auto& backend=*connection.Active();
    const auto wallet=VerifiedWalletBinding(backend);
    const auto supports=[&](std::string_view method){return backend.Supports(method)==SupportReason::Available;};
    StatusRequest request{backend.candidate.connection,backend.genesis,backend.chain,tx.txid->hex(),tx.wtxid->hex(),
        protocol::TransactionHex(scope.raw->wire),protocol::DocumentNetwork(scope.network),{},status->prior_block, {},
        supports("getmempoolentry"),supports("getrawtransaction"),supports("getblockheader"),
        backend.Supports("gettxspendingprevout","outputs")==SupportReason::Available && SchemaArgument(backend,"return_spending_tx"),
        SchemaArgument(backend,"mempool_only"),status->seen_mempool};
    if (wallet && wallet->backend.value()==entry.backend && wallet->DaemonWallet()==entry.wallet && supports("gettransaction"))
        request.wallet=entry.wallet;
    if (tx.inputs.size()<=protocol::MAX_OFFLINE_INPUTS && !tx.coinbase
        && std::ranges::all_of(tx.inputs,[](const auto& input){return input.outpoint.index<=INT32_MAX;}))
        for (const auto& input:tx.inputs) request.inputs.push_back(input.outpoint);

    attempt.reset();
    previous_serial.reset();
    status->scope=scope;
    status->launched=Clock::now();
    view.document=scope.document;
    const auto provenance=BroadcastRecordLabel(entry);
    const auto source=std::string(protocol::DocumentNetworkName(request.network))+" node "+
        request.profile.host+":"+std::to_string(request.profile.port)+"\nGenesis: "+request.genesis;
    if (status->last) ShowCachedStatus("Checking the current verified node. Current status is Unknown.");
    else view.message=provenance+"\nChecking the current verified node.";
    view.observation.reset();
    if (!Launch(Stage::CheckingStatus,[this,request=std::move(request),provenance,source](auto stop){
        auto result=Observe(transport,request,stop);
        auto message=provenance+"\n"+std::move(result.message)+"\nSource: "+source;
        if (result.observation) message+="\nObserved tip: "+result.observation->tip;
        message+="\nChecked on the current verified node at "+Utc(UnixTime())+".";
        return Outcome{result.stage,std::move(message),std::move(result.observation)};
    })) {
        Finish({Stage::StatusUnknown,"Status Unknown. The read-only check could not start."});
        return {false,view.message};
    }
    return {true,view.message};
}
void Broadcaster::ShowCachedStatus(std::string_view why)
{
    std::string message(why);
    if (status && status->last) {
        const auto age=std::chrono::duration_cast<std::chrono::seconds>(Clock::now()-status->observed).count();
        message+="\nLast observation (cached, "+std::to_string(age)+" s old):\n"+status->last->message;
    }
    Finish({Stage::StatusUnknown,std::move(message)});
}
void Broadcaster::RetireWorker()
{
    if (Busy() && !retired) {
        retired=true;
        worker.request_stop();
    }
}
void Broadcaster::PollStatus(const std::optional<BroadcastStatusContext>& scope,RecoverySession* recovery,
    const std::optional<std::string>& observed_tip)
{
    if (std::this_thread::get_id()!=owner || !status) return;
    const auto now=Clock::now();
    const auto& anchor=status->scope;
    const auto& backend=connection.Active();
    const bool same_view=scope && scope->document==anchor.document && scope->revision==anchor.revision
        && scope->epoch==anchor.epoch && scope->network==anchor.network && scope->raw==anchor.raw
        && backend && backend->candidate==status->target && recovery && recovery->BroadcastJournalAvailable();
    if (!same_view) {
        RetireWorker();
        Finish({Stage::StatusUnknown,"Status Unknown. The read-only check was retired."});
        if (Busy() && !TakeCompletion()) return;
        status.reset();
        return;
    }
    const bool current=Current(*scope);
    const bool tip_changed=observed_tip && status->refresh_tip && observed_tip!=status->refresh_tip;
    if (observed_tip) status->refresh_tip=observed_tip;
    const bool invalidate=tip_changed && (Busy() || !status->last || !status->last->observation
        || status->last->observation->tip!=*observed_tip);
    if (!current || *scope!=anchor || invalidate) {
        RetireWorker();
        if (!status->continuous && !invalidate && !status->last) {
            if (Busy() && !TakeCompletion()) return;
            status.reset();
            Finish({Stage::StatusUnknown,"Status Unknown. The read-only check was retired."});
            return;
        }
        const auto reason=!current && connection.Offline() ? "Offline. Current transaction status is Unknown."
            : invalidate ? "Node tip changed. Current transaction status is Unknown."
            : !status->continuous ? "Status Unknown. The read-only check was retired."
            : "Connection changed. Current transaction status is Unknown.";
        ShowCachedStatus(reason);
        status->next_due=std::max(now,status->launched+MONITOR_MIN_GAP);
        if (Busy() && !TakeCompletion()) return;
        if (!current) return;
        status->scope=*scope;
    }
    if (Busy()) {
        auto reply=TakeCompletion();
        if (!reply) return;
        if (retired) ShowCachedStatus(status->paused ? "Monitoring paused. Current transaction status is Unknown."
            : "Status Unknown. The read-only check was retired.");
        else {
            if (reply->observation) {
                if (reply->observation->active_block) status->prior_block=reply->observation->active_block;
                if (reply->observation->location==TransactionLocation::Mempool) status->seen_mempool=true;
            }
            status->observed=now;
            status->last=*reply;
            Finish(std::move(*reply));
        }
        status->next_due=now+MONITOR_INTERVAL;
        return;
    }
    if (status->continuous && now-status->began>=MONITOR_LIFETIME) {
        status->continuous=false;
        ShowCachedStatus("Monitoring paused after 10 minutes. Current status is Unknown.");
        return;
    }
    if (status->continuous && now>=status->next_due) {
        const auto permission=CanCheckStatus(*scope,recovery);
        if (permission.available && LaunchStatus(*scope,*recovery).accepted) return;
        status->next_due=now+MONITOR_INTERVAL;
        ShowCachedStatus("The current node or submission record could not be verified. Status Unknown.");
    } else if (!status->continuous && status->last && now-status->observed>STATUS_AGE) {
        ShowCachedStatus("Current transaction status is Unknown. Check Status for a fresh observation.");
    }
}
bool Broadcaster::StopMonitoring()
{
    if (std::this_thread::get_id()!=owner || !status || !status->continuous) return false;
    status->continuous=false;
    status->paused=true;
    RetireWorker();
    ShowCachedStatus("Monitoring paused. Current transaction status is Unknown.");
    return true;
}
bool Broadcaster::Cancel()
{
    if (std::this_thread::get_id()!=owner) return false;
    if (status) {
        status->continuous=false;
        status->paused=false;
        status->last.reset();
        if (Busy()) RetireWorker();
        else status.reset();
        Finish({Stage::StatusUnknown,"Status Unknown. The prior node observation was retired."});
        return true;
    }
    if (!Busy()) {
        if (view.stage!=Stage::Ready) return false;
        Finish({Stage::Cancelled,"The policy check was retired. Run a fresh check."});
        return true;
    }
    if (retired) return false;
    RetireWorker();
    return true;
}
}
