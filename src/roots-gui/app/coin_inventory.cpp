// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/coin_inventory.h"
#include <algorithm>
#include <charconv>
#include <fcntl.h>
#include <unistd.h>
namespace roots::gui::app {
std::string CoinKey(const Outpoint& p){return p.txid.hex()+":"+std::to_string(p.index);}
std::optional<Outpoint> ParseCoinKey(std::string_view key){if(key.size()<66 || key.size()>75 || key[64]!=':')return {};auto h=Hash256::Parse(key.substr(0,64));uint32_t n{};auto p=std::from_chars(key.data()+65,key.data()+key.size(),n);if(!h || h->hex()!=key.substr(0,64) || p.ec!=std::errc{} || p.ptr!=key.data()+key.size() || std::to_string(n)!=key.substr(65))return {};return Outpoint{*h,n};}
void CoinInventory::MarkDated(){current=false;for(auto& [key,row]:rows){row.current=false;row.eligible.reset();row.reason="Current state Unknown; dated wallet observation";}}
bool CoinInventory::Apply(const HistoryLedger& ledger,uint64_t next,bool chain_current,const CoinPolicy* policy){
    if(ledger.Context()!=context || ledger.DaemonWallet()!=daemon_wallet || !ledger.Observed() || !next || next<sequence)return false;
    const bool live=chain_current && ledger.InventoryAvailable();
    const auto locked=[&](const Outpoint& p){return ledger.LocksAvailable() && std::find(ledger.DaemonLocked().begin(),ledger.DaemonLocked().end(),p)!=ledger.DaemonLocked().end();};auto candidate=rows;for(auto& [key,row]:candidate){row.present=false;row.current=false;row.eligible.reset();row.reason="Unavailable: absent from latest wallet inventory; spent, locked or missing data. No input substitution.";if(locked(row.outpoint))row.reason="Unavailable: external daemon lock; this GUI does not own or release it";}
    std::set<std::string> seen;
    for(const auto& coin:ledger.Coins()){
        const auto key=CoinKey(coin.outpoint);if(!seen.insert(key).second || coin.amount.sats()<0)return false;
        CoinRow row{coin.outpoint,coin.amount,coin.address,coin.label,coin.script,"",{},ledger.Observed()};row.present=true;row.current=live;row.reused=coin.reused;row.small_value=coin.amount.sats()<546;
        const auto parent=ledger.Rows().find(coin.outpoint.txid.hex());bool mismatch=false,prevout_verified=false;
        if(parent!=ledger.Rows().end()){
            row.date=parent->second.time;row.immature=parent->second.state==HistoryState::Immature;
            if(parent->second.decoded_available){const auto output=std::find_if(parent->second.io.begin(),parent->second.io.end(),[&](const auto& io){return io.kind=="Output" && io.reference==std::to_string(coin.outpoint.index);});
                if(output!=parent->second.io.end()){mismatch=(output->value && *output->value!=coin.amount) || (!coin.script.empty() && !output->script.empty() && output->script!=coin.script);prevout_verified=!mismatch && output->value && !coin.script.empty() && coin.script==output->script;if(!mismatch && row.address.empty() && !output->script.empty() && coin.script==output->script)row.address=output->destination;}
            }
        }
        if(mismatch){row.current=false;row.reason="Unknown: contradictory output amount or locking script";}
        else if(!live)row.reason="Current state Unknown; dated or incomplete wallet inventory";
        else if(parent==ledger.Rows().end() || !prevout_verified || !coin.daemon_spendable){row.reason="Eligibility Unknown: missing parent, script or daemon spendability evidence";}
        else if(locked(row.outpoint)){row.eligible=false;row.reason="Unavailable: external daemon lock; this GUI does not own or release it";}
        else if(row.immature){row.eligible=false;row.reason="Unavailable: immature coinbase";}
        else if(parent->second.state!=HistoryState::Confirmed && parent->second.state!=HistoryState::Pending){row.eligible=false;row.reason="Unavailable: "+std::string(HistoryStateText(parent->second.state));}
        else if(!*coin.daemon_spendable || !coin.safe || !coin.solvable){row.eligible=false;row.reason="Unavailable: daemon reports unsafe, unsolvable or not spendable; custody is separate";}
        else if(policy && policy->context==context && policy->sequence==next && policy->rules.contains(key)){
            switch(policy->rules.at(key)){case CoinRule::Allowed:row.eligible=true;row.reason="Eligible in this exact scoped policy observation";break;case CoinRule::Frozen:row.eligible=false;row.reason="Unavailable: locally frozen";break;case CoinRule::Reserved:row.eligible=false;row.reason="Unavailable: owned draft reservation";break;case CoinRule::Unavailable:row.eligible=false;row.reason="Unavailable: scoped policy exclusion";break;}
        }else row.reason="Eligibility Unknown: local policy or required output evidence is not proven";
        candidate.insert_or_assign(key,std::move(row));
    }
    // Bounded history retention: never silently replace selected identities at
    // this boundary; callers retain selection and show missing-selection counts.
    if(candidate.size()>8192)return false;
    rows=std::move(candidate);sequence=next;current=live;complete_inventory=ledger.InventoryAvailable();return true;
}
namespace {
void Add(std::optional<Money>& sum,std::optional<Money> n){if(sum)sum=n?sum->Add(*n):std::nullopt;}
std::string Cell(std::string s,bool numeric=false){if(!numeric){auto p=s.find_first_not_of(' ');if(p!=std::string::npos && std::string_view("=+-@\t\r\n").find(s[p])!=std::string_view::npos)s.insert(s.begin(),'\'');}std::string out="\"";for(char c:s){out+=c;if(c=='"')out+='"';}return out+'"';}
bool Text(std::string_view s){return rpc::ValidUtf8(s) && std::none_of(s.begin(),s.end(),[](unsigned char c){return (c<32 && c!='\r' && c!='\n' && c!='\t') || c==127;});}
}
CoinSummary CoinInventory::Summary(const std::set<std::string>& selected) const {CoinSummary s;for(const auto& [key,row]:rows){if(row.present){++s.present;Add(s.total,row.value);Add(s.eligible,row.current && row.eligible?std::optional(*row.eligible?row.value:Money::Satoshis(0)):std::nullopt);}if(selected.contains(key)){++s.selected;Add(s.selected_value,row.value);if(!row.present || !row.current || !row.eligible || !*row.eligible)++s.unavailable_selected;Add(s.selected_eligible,row.present && row.current && row.eligible?std::optional(*row.eligible?row.value:Money::Satoshis(0)):std::nullopt);}}s.missing_selected=selected.size()-s.selected;s.selected=selected.size();if(s.missing_selected){s.selected_value.reset();s.selected_eligible.reset();}if(!current)s.eligible.reset();if(!complete_inventory)s.total.reset();return s;}
std::vector<const CoinRow*> CoinInventory::Ordered(bool by_value,bool descending,bool hidden) const
{
    std::vector<std::pair<std::string_view,const CoinRow*>> keyed;
    keyed.reserve(rows.size());
    for (const auto& [key,row]:rows) keyed.emplace_back(key,&row);
    std::ranges::sort(keyed,[&](const auto& a,const auto& b) {
        if (by_value && !hidden && a.second->value!=b.second->value)
            return descending ? a.second->value>b.second->value : a.second->value<b.second->value;
        if (!by_value && a.second->date!=b.second->date)
            return descending ? a.second->date>b.second->date : a.second->date<b.second->date;
        return a.first<b.first;
    });
    std::vector<const CoinRow*> ordered;
    ordered.reserve(keyed.size());
    for (const auto& [key,row]:keyed) ordered.push_back(row);
    return ordered;
}
std::optional<std::vector<CoinGroup>> CoinInventory::Chart() const {if(!complete_inventory)return {};std::vector<const CoinRow*> order;for(const auto& [key,row]:rows)if(row.present)order.push_back(&row);std::sort(order.begin(),order.end(),[](auto a,auto b){return a->value!=b->value?a->value>b->value:CoinKey(a->outpoint)<CoinKey(b->outpoint);});std::vector<CoinGroup> out;for(size_t i=0;i<order.size();++i){const auto key=CoinKey(order[i]->outpoint);if(i<9)out.push_back({"#"+std::to_string(i+1),order[i]->value,{key}});else if(i==9)out.push_back({"Other",order[i]->value,{key}});else{auto sum=out.back().value.Add(order[i]->value);if(!sum)return {};out.back().value=*sum;out.back().keys.push_back(key);}}return out;}
std::optional<std::string> CoinCsv(const CoinInventory& inventory,std::span<const CoinRow* const> rows,const std::map<std::string,std::string>& labels,bool hidden){if(hidden || rows.size()>8192)return {};std::string csv="Date (UTC),Output,Address,Label,Value (BTC),Observed (ms UTC),Current,Reason,Heuristics\r\n";for(const auto* row:rows){if(!row)return {};const auto key=CoinKey(row->outpoint);const auto i=inventory.Rows().find(key);if(i==inventory.Rows().end() || &i->second!=row)return {};const auto label=labels.contains(key)?labels.at(key):row->label;if(!Text(label) || !Text(row->address))return {};csv+=Cell(row->date?HistoryDate(*row->date,true):"Unknown")+','+Cell(key)+','+Cell(row->address)+','+Cell(label)+','+Cell(row->value.btc(),true)+','+Cell(std::to_string(row->observed),true)+','+Cell(row->current?"Current wallet observation":"Unknown; dated")+','+Cell(row->reason)+','+Cell(std::string(row->reused?"Address reuse heuristic; ":"")+(row->small_value?"Small-value heuristic (<546 sats); not relay policy":""))+"\r\n";if(csv.size()>8*1024*1024)return {};}return csv;}
bool WriteCoinCsv(const std::string& path,const std::string& data,std::string& error){const std::filesystem::path target(path);if(!target.is_absolute() || data.empty()){error="Choose an absolute path for a new CSV file.";return false;}const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0){error="CSV was not saved. Choose a new filename; existing files are retained.";return false;}size_t offset{};bool ok=true;while(offset<data.size()){const auto n=::write(fd,data.data()+offset,data.size()-offset);if(n<0 && errno==EINTR)continue;if(n<=0){ok=false;break;}offset+=size_t(n);}if(::fsync(fd)!=0)ok=false;if(::close(fd)!=0)ok=false;if(!ok){error="CSV write failed; partial file may remain. Choose a new filename to retry.";return false;}const int dir=::open(target.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);const bool durable=dir>=0 && ::fsync(dir)==0;if(dir>=0)::close(dir);error=durable?"CSV saved locally.":"CSV written; directory durability unconfirmed. Check the selected file.";return durable;}
}
