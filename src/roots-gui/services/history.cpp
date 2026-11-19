// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/history.h"
#include "adapters/protocol/offline_draft.h"
#include <algorithm>
#include <charconv>
#include <limits>
#include <set>

namespace roots::gui {
namespace {
using J=rpc::Json;
struct Invalid {};
struct ResourceLimit {};
void Need(bool value){if(!value)throw Invalid{};}
std::string Text(const J* j,size_t max=4096){Need(j && j->kind==J::Kind::String && j->text.size()<=max && rpc::ValidUtf8(j->text));return j->text;}
int64_t Integer(const J* j){Need(j && j->kind==J::Kind::Number);int64_t n{};const auto r=std::from_chars(j->text.data(),j->text.data()+j->text.size(),n);Need(r.ec==std::errc{} && r.ptr==j->text.data()+j->text.size());return n;}
bool Boolean(const J* j){Need(j && j->kind==J::Kind::Boolean);return j->text=="true";}
Hash256 Hash(const J* j){const auto s=Text(j,64);auto h=Hash256::Parse(s);Need(h && h->hex()==s);return *h;}
HistoryAmount Amount(const J* j){Need(j && (j->kind==J::Kind::Number || j->kind==J::Kind::String));auto m=Money::ParseJsonBtc(j->text);Need(bool(m));return {*m,j->text,j->kind==J::Kind::String};}
const std::vector<J>& Array(const J* j,size_t max){Need(j && j->kind==J::Kind::Array);if(j->items.size()>max)throw ResourceLimit{};return j->items;}
void Add(Money& value,Money delta){auto sum=value.Add(delta);Need(bool(sum));value=*sum;}
std::string Key(const Outpoint& p){return p.txid.hex()+":"+std::to_string(p.index);}
bool Active(HistoryState state){return state==HistoryState::Confirmed || state==HistoryState::Pending || state==HistoryState::Immature;}
HistoryBalances Balances(const J& j){const auto* mine=j.Find("mine");Need(mine);HistoryBalances b;b.trusted=Amount(mine->Find("trusted")).value;b.pending=Amount(mine->Find("untrusted_pending")).value;b.immature=Amount(mine->Find("immature")).value;if(const auto* n=mine->Find("nonmempool"))b.nonmempool=Amount(n).value;if(const auto* used=mine->Find("used"))b.used=Amount(used).value;Need(b.trusted.sats()>=0 && b.pending.sats()>=0 && b.immature.sats()>=0 && (!b.used || b.used->sats()>=0));return b;}
}
std::optional<HistoryTransaction> NormalizeHistoryTransaction(const J& j,std::string_view expected,size_t max_details)
{
    try{
        auto txid=Hash(j.Find("txid"));Need(txid.hex()==expected);HistoryTransaction tx{std::move(txid)};tx.reported_amount=Amount(j.Find("amount"));tx.time=Integer(j.Find("time"));tx.confirmations=Integer(j.Find("confirmations"));Need(tx.time>0 && tx.confirmations>=-INT32_MAX && tx.confirmations<=INT32_MAX);
        if (const auto* witness=j.Find("wtxid")) tx.wire_witness=Hash(witness);
        if(const auto* fee=j.Find("fee")){tx.fee=Amount(fee);Need(tx.fee->value.sats()<=0);}
        if(const auto* generated=j.Find("generated"))tx.generated=Boolean(generated);
        if(const auto* height=j.Find("blockheight")){tx.block_height=Integer(height);Need(*tx.block_height>=0 && *tx.block_height<=INT32_MAX);}
        if(const auto* block=j.Find("blockhash"))tx.block_hash=Hash(block);
        if(const auto* h=j.Find("replaced_by_txid"))tx.replaced_by=Hash(h);
        if(const auto* h=j.Find("replaces_txid"))tx.replaces=Hash(h);
        if(const auto* conflicts=j.Find("walletconflicts"))for(const auto& h:Array(conflicts,4096))tx.conflicts.push_back(Hash(&h));
        if(const auto* conflicts=j.Find("mempoolconflicts"))for(const auto& h:Array(conflicts,4096))tx.mempool_conflicts.push_back(Hash(&h));
        Money detail_sum=Money::Satoshis(0),reward=Money::Satoshis(0);bool immature{},orphan{},abandoned{};std::set<std::pair<std::string,uint32_t>> seen;
        for(const auto& row:Array(j.Find("details"),max_details)){
            HistoryDetail d;const auto vout=Integer(row.Find("vout"));Need(vout>=0 && uint64_t(vout)<=UINT32_MAX);d.vout=uint32_t(vout);d.category=Text(row.Find("category"),16);d.amount=Amount(row.Find("amount"));
            Need(seen.emplace(d.category,d.vout).second);if(const auto* label=row.Find("label"))d.label=Text(label);if(const auto* address=row.Find("address"))d.address=Text(address,1024);
            const bool sent=d.category=="send";Need(sent || d.category=="receive" || d.category=="generate" || d.category=="immature" || d.category=="orphan");Need(sent?d.amount.value.sats()<=0:d.amount.value.sats()>=0);
            if(const auto* fee=row.Find("fee")){d.repeated_fee=Amount(fee);Need(sent && tx.fee && d.repeated_fee->value==tx.fee->value);}
            if(const auto* a=row.Find("abandoned"))abandoned=abandoned || Boolean(a);
            Add(detail_sum,d.amount.value);if(d.category=="generate" || d.category=="immature" || d.category=="orphan"){Need(tx.generated);Add(reward,d.amount.value);}
            immature=immature || d.category=="immature";orphan=orphan || d.category=="orphan";tx.details.push_back(std::move(d));
        }
        tx.state=tx.confirmations>0?(immature?HistoryState::Immature:HistoryState::Confirmed):tx.replaced_by?HistoryState::Replaced:(tx.confirmations<0 || !tx.mempool_conflicts.empty())?HistoryState::Conflicted:orphan?HistoryState::Orphan:abandoned?HistoryState::Abandoned:HistoryState::Pending;
        // gettransaction.amount excludes the fee. For immature coinbases its
        // credit is zero, while the detail carries the still-immature reward.
        tx.wallet_delta=tx.generated?reward:tx.reported_amount.value;if(!tx.generated && tx.fee)Add(tx.wallet_delta,tx.fee->value);
        if(Active(tx.state) && !tx.generated)Need(detail_sum==tx.reported_amount.value);
        if(tx.generated && Active(tx.state))Need(reward.sats()>0 && !tx.fee);
        if(const auto* decoded=j.Find("decoded")){
            Need(decoded->kind==J::Kind::Object && Hash(decoded->Find("txid"))==tx.txid);tx.decoded_available=true;
            if (const auto* witness=decoded->Find("hash")) {
                const auto decoded_witness=Hash(witness);
                Need(!tx.wire_witness || tx.wire_witness==decoded_witness);
                tx.wire_witness=decoded_witness;
            }
            for(const auto& input:Array(decoded->Find("vin"),max_details)){
                HistoryIO io;io.kind="Input";
                if(const auto* coinbase=input.Find("coinbase")){Text(coinbase,8192);io.reference="Coinbase";}
                else {const auto hash=Hash(input.Find("txid"));const auto n=Integer(input.Find("vout"));Need(n>=0 && uint64_t(n)<=UINT32_MAX);io.reference=hash.hex()+":"+std::to_string(n);}
                tx.io.push_back(std::move(io));
            }
            std::set<uint32_t> outputs;
            for(const auto& output:Array(decoded->Find("vout"),max_details)){
                const auto n=Integer(output.Find("n"));Need(n>=0 && uint64_t(n)<=UINT32_MAX && outputs.insert(uint32_t(n)).second);HistoryIO io;io.kind="Output";io.reference=std::to_string(n);io.value=Amount(output.Find("value")).value;Need(io.value->sats()>=0);
                const auto* script=output.Find("scriptPubKey");Need(script && script->kind==J::Kind::Object);if(const auto* hex=script->Find("hex"))io.script=Text(hex,10000);if(const auto* address=script->Find("address"))io.destination=Text(address,1024);else io.destination="Script (no address)";tx.io.push_back(std::move(io));
            }
            Need(tx.io.size()<=max_details);
        }
        if (const auto* raw=j.Find("hex"); raw && raw->kind==J::Kind::String
            && raw->text.size()<=protocol::MAX_OFFLINE_PREVIOUS*2
            && !raw->text.empty() && raw->text.size()%2==0
            && std::ranges::all_of(raw->text,[](unsigned char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}))
            tx.previous_hex=raw->text;
        return tx;
    }catch(...){return {};}
}
HistoryFetch FetchHistory(rpc::Transport& transport,const ServerCandidate& profile,const RequestContext& context,
    const J& since,const HistoryBalances& before,std::string_view tip,bool full_query,bool scanning,bool pruned,
    bool inventory_supported,const std::vector<std::string>& revisit,std::stop_token stop,
    std::chrono::steady_clock::time_point deadline,HistoryLimits limits,bool decoded_supported,bool locks_supported)
{
    HistoryFetch result;
    try{
        Need(profile.wallet && limits.transactions>0 && limits.transactions<=4096 && limits.entries>0 && limits.entries<=65536 && limits.coins>0 && limits.coins<=65536);
        HistoryBatch batch{context,*profile.wallet,Hash(since.Find("lastblock")).hex()};batch.full_query=full_query;batch.scanning=scanning;batch.pruned=pruned;std::set<std::string> ids,removed;
        for(const auto& row:Array(since.Find("transactions"),limits.entries))ids.insert(Hash(row.Find("txid")).hex());
        for(const auto& row:Array(since.Find("removed"),limits.entries)){auto h=Hash(row.Find("txid")).hex();ids.insert(h);removed.insert(std::move(h));}
        for(const auto& h:revisit){auto hash=Hash256::Parse(h);Need(hash && hash->hex()==h);ids.insert(h);}
        uint64_t request_id=1000;rpc::Error error=rpc::Error::None;
        const auto call=[&](std::string method,std::string params="[]",bool wallet=true)->std::optional<J>{
            if(stop.stop_requested()){error=rpc::Error::Cancelled;return {};}
            const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now());if(left.count()<=0){error=rpc::Error::Timeout;return {};}
            rpc::Limits bound;bound.total_timeout=left;bound.connect_timeout=std::min(left,std::chrono::milliseconds(3000));auto reply=transport.Call(profile.connection,{++request_id,std::move(method),std::move(params),wallet?profile.wallet:std::nullopt,rpc::Effect::ReadOnly},stop,bound);if(reply.error!=rpc::Error::None || !reply.result){error=reply.error==rpc::Error::None?rpc::Error::Json:reply.error;return {};}return std::move(reply.result);
        };
        if(inventory_supported){
            auto inventory=call("listunspent","{\"minconf\":0,\"include_unsafe\":true,\"query_options\":{\"include_immature_coinbase\":true,\"maximumCount\":"+std::to_string(limits.coins+1)+"}}");if(!inventory)return {{},error};
            const auto& entries=Array(&*inventory,limits.coins+1);batch.limited=entries.size()>limits.coins;std::set<std::string> outputs;
            for(size_t i=0;i<std::min(entries.size(),limits.coins);++i){const auto& row=entries[i];auto hash=Hash(row.Find("txid"));const auto n=Integer(row.Find("vout")),confirmations=Integer(row.Find("confirmations"));Need(n>=0 && uint64_t(n)<=UINT32_MAX && confirmations>=0);HistoryCoin coin{{hash,uint32_t(n)},Amount(row.Find("amount")).value,confirmations,Boolean(row.Find("safe")),Boolean(row.Find("solvable")),false};Need(coin.amount.sats()>=0 && outputs.insert(Key(coin.outpoint)).second);if(const auto* used=row.Find("reused"))coin.reused=Boolean(used);ids.insert(hash.hex());if(const auto* script=row.Find("scriptPubKey"))coin.script=Text(script,10000);if(const auto* address=row.Find("address"))coin.address=Text(address,1024);if(const auto* label=row.Find("label"))coin.label=Text(label,4096);if(const auto* spendable=row.Find("spendable"))coin.daemon_spendable=Boolean(spendable);const auto public_string=[&](const char* key,size_t bound) {
                    const auto* field=row.Find(key);
                    return field && field->kind==J::Kind::String && field->text.size()<=bound ? field->text : std::string{};
                };
                coin.descriptor=public_string("desc",4096);coin.redeem_script=public_string("redeemScript",10000);
                coin.witness_script=public_string("witnessScript",10000);
                batch.coins.push_back(std::move(coin));}
            batch.inventory_available=!batch.limited;
        }
        if(locks_supported){auto locked=call("listlockunspent");if(!locked)return {{},error};std::set<std::string> unique;for(const auto& row:Array(&*locked,limits.coins)){auto h=Hash(row.Find("txid"));auto n=Integer(row.Find("vout"));Need(n>=0 && uint64_t(n)<=UINT32_MAX);Outpoint p{h,uint32_t(n)};Need(unique.insert(Key(p)).second);batch.daemon_locked.push_back(p);}batch.locks_available=true;}
        batch.limited=batch.limited || ids.size()>limits.transactions;
        size_t count{},detail_count{};for(const auto& id:ids){if(count++==limits.transactions)break;auto raw=call("gettransaction","{\"txid\":"+rpc::Quote(id)+(decoded_supported?",\"verbose\":true}":"}"));if(!raw)return {{},error};auto tx=NormalizeHistoryTransaction(*raw,id);if(!tx)return {{},rpc::Error::Json};if(tx->details.size()+tx->io.size()>limits.entries-detail_count){batch.limited=true;break;}detail_count+=tx->details.size()+tx->io.size();tx->removed_event=removed.contains(id);batch.transactions.push_back(std::move(*tx));}
        auto balances=call("getbalances");if(!balances)return {{},error};batch.balances=Balances(*balances);auto chain=call("getblockchaininfo","[]",false);if(!chain)return {{},error};batch.height=Integer(chain->Find("blocks"));Need(batch.height>=0 && batch.height<=INT32_MAX);const auto end_tip=Hash(chain->Find("bestblockhash")).hex();batch.stable=end_tip==tip && batch.lastblock==end_tip && batch.balances==before;
        if(const auto* block=balances->Find("lastprocessedblock"))batch.stable=batch.stable && Hash(block->Find("hash")).hex()==end_tip;
        auto genesis=call("getblockhash","[0]",false);if(!genesis)return {{},error};if(Hash(&*genesis)!=context.network.genesis || genesis->text!=profile.expected_genesis)return {{},rpc::Error::Identity};
        if(stop.stop_requested())return {{},rpc::Error::Cancelled};result.batch=std::move(batch);return result;
    }catch(const Invalid&){result.error=rpc::Error::Json;}catch(...){result.error=rpc::Error::Limit;}return result;
}
HistoryLedger::HistoryLedger(RequestContext c,std::string wallet,HistoryLimits bound):context(std::move(c)),daemon_wallet(std::move(wallet)),limits(bound){}
bool HistoryLedger::SetPolicy(HistoryPolicy next){
    if(revision==UINT64_MAX)return false;
    try{std::set<std::string> seen;for(const auto& p:next.frozen)Need(seen.insert(Key(p)).second);for(const auto& p:next.reserved)Need(seen.insert(Key(p)).second);auto copy=*this;copy.policy=std::move(next);if(!copy.Recalculate())return false;++copy.revision;*this=std::move(copy);return true;}catch(...){return false;}
}
bool HistoryLedger::Apply(const HistoryBatch& batch,uint64_t seq,int64_t date){
    if(revision==UINT64_MAX || batch.context!=context || batch.daemon_wallet!=daemon_wallet || seq<=sequence || date<=0 || batch.height<0 || batch.height>INT32_MAX)return false;
    try{auto copy=*this;std::set<std::string> unique;for(const auto& tx:batch.transactions){Need(unique.insert(tx.txid.hex()).second);const auto previous=copy.rows.find(tx.txid.hex());const bool removed=tx.removed_event || (previous!=copy.rows.end() && previous->second.removed_event);copy.rows.insert_or_assign(tx.txid.hex(),tx);auto& accepted=copy.rows.at(tx.txid.hex());accepted.removed_event=removed;if(accepted.confirmations<0 && batch.stable){const auto height=batch.height+accepted.confirmations+1;if(height>=0)accepted.conflict_height=height;}}Need(copy.rows.size()<=limits.ledger);size_t details{};for(const auto& [id,tx]:copy.rows){Need(tx.details.size()+tx.io.size()<=limits.entries-details);details+=tx.details.size()+tx.io.size();}for(auto& [id,tx]:copy.rows)if(tx.state==HistoryState::Confirmed && tx.block_height && batch.height>=*tx.block_height)tx.confirmations=batch.height-*tx.block_height+1;else if(tx.conflict_height && batch.height>=*tx.conflict_height)tx.confirmations=-(batch.height-*tx.conflict_height+1);
        // Keep only bounded candidate prevouts that belong to current observed coins.
        // The offline pin revalidates the bytes, exact output, and descriptor.
        size_t remaining=512*1024;
        std::set<std::string> funded;
        for (const auto& coin:batch.coins) funded.insert(coin.outpoint.txid.hex());
        for (auto& [id,row]:copy.rows) {
            if (!funded.contains(id) || row.previous_hex.size()>remaining) row.previous_hex.clear();
            else remaining-=row.previous_hex.size();
        }
        Need(batch.daemon_locked.size()<=limits.coins);std::set<std::string> locks;for(const auto& p:batch.daemon_locked)Need(locks.insert(Key(p)).second);copy.tip=batch.lastblock;copy.totals.rpc=batch.balances;copy.coins=batch.coins;copy.inventory_available=batch.inventory_available;copy.daemon_locked=batch.daemon_locked;copy.locks_available=batch.locks_available;copy.limited=batch.limited;copy.scanning=batch.scanning;copy.pruned=batch.pruned;copy.stable=batch.stable;copy.full_seen=copy.full_seen || (batch.full_query && !batch.limited);copy.sequence=seq;copy.observed=date;if(!copy.Recalculate())return false;++copy.revision;*this=std::move(copy);return true;}catch(...){return false;}
}
std::vector<std::string> HistoryLedger::Revisit() const {std::vector<std::string> ids;for(const auto& [id,tx]:rows)if(tx.state==HistoryState::Pending || tx.state==HistoryState::Immature || ((tx.state==HistoryState::Conflicted || tx.state==HistoryState::Replaced) && tx.confirmations>=0))ids.push_back(id);return ids;}
bool HistoryLedger::Recalculate(){
    try{
        if(observed<=0){totals=HistoryTotals{};complete=false;return true;}
        const auto rpc=totals.rpc;Need(rpc.trusted.sats()>=0 && rpc.pending.sats()>=0 && rpc.immature.sats()>=0);totals=HistoryTotals{};totals.rpc=rpc;totals.has_rpc=true;Money total=rpc.trusted;Add(total,rpc.pending);Add(total,rpc.immature);totals.total=total;
        std::vector<HistoryTransaction*> order;Money sum=Money::Satoshis(0);for(auto& [id,tx]:rows){tx.running_balance.reset();order.push_back(&tx);if(Active(tx.state))Add(sum,tx.wallet_delta);}
        complete=full_seen && !limited && !scanning && !pruned && stable && rpc.nonmempool.sats()==0 && sum==total;
        std::sort(order.begin(),order.end(),[](const auto* a,const auto* b){return a->time==b->time?a->txid.hex()<b->txid.hex():a->time<b->time;});
        if(complete){Money balance=Money::Satoshis(0);for(auto* tx:order){if(Active(tx->state))Add(balance,tx->wallet_delta);tx->running_balance=balance;}}
        if(!inventory_available || limited || !stable || scanning)return true;
        Money confirmed=Money::Satoshis(0),pending=confirmed,immature=confirmed,fundable=confirmed,spendable=confirmed,frozen=confirmed,reserved=confirmed,all=confirmed;std::set<std::string> outputs,freeze,reserve;
        for(const auto& p:policy.frozen)freeze.insert(Key(p));for(const auto& p:policy.reserved)reserve.insert(Key(p));
        for(const auto& coin:coins){Need(coin.amount.sats()>=0 && outputs.insert(Key(coin.outpoint)).second);const auto found=rows.find(coin.outpoint.txid.hex());if(found==rows.end() || !Active(found->second.state))return true;if(rpc.used && coin.reused)continue;
            Add(all,coin.amount);const bool is_immature=found->second.state==HistoryState::Immature;if(is_immature)Add(immature,coin.amount);else if(coin.confirmations>0)Add(confirmed,coin.amount);else Add(pending,coin.amount);
            const bool eligible=!is_immature && coin.safe && coin.solvable && std::find(daemon_locked.begin(),daemon_locked.end(),coin.outpoint)==daemon_locked.end();const auto key=Key(coin.outpoint);if(eligible)Add(fundable,coin.amount);if(freeze.contains(key))Add(frozen,coin.amount);if(reserve.contains(key))Add(reserved,coin.amount);if(eligible && !freeze.contains(key) && !reserve.contains(key))Add(spendable,coin.amount);
        }
        if(all!=total || immature!=rpc.immature)return true;
        totals.inventory_reconciled=true;totals.confirmed=confirmed;totals.mempool=pending;totals.rpc_fundable=fundable;if(policy.known){totals.spendable=spendable;totals.frozen=frozen;totals.reserved=reserved;}return true;
    }catch(...){return false;}
}
}
