// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/child_package.h"
#include "app/broadcast.h"
#include "services/funding.h"

#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace roots::gui::app {
namespace {
using J=rpc::Json;
constexpr int64_t MAX_MONEY=2100000000000000LL;
constexpr size_t MAX_EDGES=96;
constexpr size_t MAX_WIRE=256*1024;
constexpr size_t MAX_CLUSTER_MEMBERS=256;

void Require(bool ok,const char* reason)
{
    if (!ok) throw std::runtime_error(reason);
}
std::string_view Text(const J* value)
{
    return value && value->kind==J::Kind::String ? value->text : std::string_view{};
}
std::optional<int64_t> Integer(const J* value)
{
    if (!value || value->kind!=J::Kind::Number) return {};
    int64_t result{};
    const auto parsed=std::from_chars(value->text.data(),value->text.data()+value->text.size(),result);
    return parsed.ec==std::errc{} && parsed.ptr==value->text.data()+value->text.size() ? std::optional(result) : std::nullopt;
}
std::optional<Money> Amount(const J* value,bool signed_value=false)
{
    if (!value || (value->kind!=J::Kind::Number && value->kind!=J::Kind::String)) return {};
    const auto amount=Money::ParseJsonBtc(value->text);
    return amount && (signed_value || amount->sats()>=0) && amount->sats()>=-MAX_MONEY && amount->sats()<=MAX_MONEY
        ? amount : std::nullopt;
}
void Add(int64_t& sum,Money amount)
{
    Require(amount.sats()>=0 && amount.sats()<=MAX_MONEY-sum,"The package exceeds the supported monetary bound.");
    sum+=amount.sats();
}
size_t Size(const J* value,size_t maximum)
{
    const auto n=Integer(value);
    Require(n && *n>0 && uint64_t(*n)<=maximum,"Package size or policy limit is Unknown or unsupported.");
    return size_t(*n);
}
Hash256 Hash(std::string_view text)
{
    const auto id=Hash256::Parse(text);
    Require(id && id->hex()==text,"The mempool did not provide an exact transaction identity.");
    return *id;
}
std::vector<Hash256> Ids(const J* value,size_t maximum)
{
    Require(value && value->kind==J::Kind::Array && value->items.size()<=maximum,"The mempool dependency list is Unknown or exceeds the package bound.");
    std::vector<Hash256> ids;
    for (const auto& entry:value->items) ids.push_back(Hash(Text(&entry)));
    std::ranges::sort(ids,{},&Hash256::hex);
    Require(std::adjacent_find(ids.begin(),ids.end())==ids.end(),"The mempool reports duplicate package members.");
    return ids;
}
PackageTransaction Entry(const Hash256& txid,const J& value)
{
    const auto* fees=value.Find("fees");
    const auto base=Amount(fees ? fees->Find("base") : nullptr);
    const auto modified=Amount(fees ? fees->Find("modified") : nullptr,true);
    Require(base && modified,"Base or modified ancestor fee is Unknown.");
    const auto weight=Size(value.Find("weight"),4*protocol::MAX_TRANSACTION_WIRE);
    const auto vsize=Size(value.Find("vsize"),4*protocol::MAX_TRANSACTION_WIRE);
    return {txid,Hash(Text(value.Find("wtxid"))),*base,*modified,weight,(weight+3)/4,vsize,
        Ids(value.Find("depends"),MAX_CHILD_PACKAGE_TRANSACTIONS)};
}

using Entries=std::map<std::string,PackageTransaction>;
Entries ReadEntries(const PackageRead& read,const Hash256& parent)
{
    const auto id=rpc::Quote(parent.hex());
    const auto self=read("getmempoolentry","["+id+"]","The exact parent is no longer in this node's mempool.");
    const auto related=read("getmempoolancestors","["+id+",true]","Ancestor membership is Unknown on this node.");
    Require(related.kind==J::Kind::Object && related.members.size()<MAX_CHILD_PACKAGE_TRANSACTIONS,
        "Ancestor membership is Unknown or exceeds the 16-transaction limit including the parent.");
    Require(Size(self.Find("ancestorcount"),MAX_CHILD_PACKAGE_TRANSACTIONS)==related.members.size()+1,
        "Ancestor membership is incomplete or inconsistent.");
    Entries entries;
    entries.emplace(parent.hex(),Entry(parent,self));
    for (const auto& [txid,value]:related.members)
        Require(entries.emplace(txid,Entry(Hash(txid),value)).second,"The mempool reports duplicate package members.");
    return entries;
}

void CheckOutput(const PackageRead& read,const protocol::TransactionDocument& parent,uint32_t n,std::string_view tip)
{
    const auto& tx=*parent.transaction;
    const auto& output=tx.outputs[n];
    const auto result=read("gettxout","["+rpc::Quote(tx.txid->hex())+","+std::to_string(n)+",true]",
        "The selected parent output is unavailable.");
    const auto* script=result.Find("scriptPubKey");
    Require(Text(result.Find("bestblock"))==tip && Integer(result.Find("confirmations"))==0
        && Amount(result.Find("value"))==output.value && script && Text(script->Find("hex"))==output.script
        && result.Find("coinbase") && result.Find("coinbase")->kind==J::Kind::Boolean && result.Find("coinbase")->text=="false",
        "The selected output is spent, confirmed or unavailable on the current node.");
}

PackageSnapshot ReadPolicy(const PackageRead& read,const Hash256& parent,const Entries& entries)
{
    PackageSnapshot package;
    const auto info=read("getmempoolinfo","[]","Current relay and cluster policy is Unknown.");
    const auto relay=Amount(info.Find("minrelaytxfee")),floor=Amount(info.Find("mempoolminfee"));
    Require(relay && floor && floor->sats()>=relay->sats(),"Current relay and mempool fee floors are Unknown.");
    package.relay_floor=*relay;
    package.mempool_floor=*floor;
    package.cluster_count_limit=Size(info.Find("limitclustercount"),1<<20);
    package.cluster_vsize_limit=Size(info.Find("limitclustersize"),1<<28);
    const auto data=read("getmempoolcluster","["+rpc::Quote(parent.hex())+"]","The current parent cluster is Unknown.");
    const auto count=Size(data.Find("txcount"),MAX_CLUSTER_MEMBERS);
    package.cluster_weight=Size(data.Find("clusterweight"),4*package.cluster_vsize_limit);
    const auto* chunks=data.Find("chunks");
    Require(count<=package.cluster_count_limit && chunks && chunks->kind==J::Kind::Array && chunks->items.size()<=count,
        "The current cluster exceeds the supported inspection bounds.");
    size_t total{};
    for (const auto& chunk:chunks->items) {
        auto ids=Ids(chunk.Find("txs"),count);
        Require(!ids.empty() && ids.size()<=count-package.cluster.size(),"Cluster membership is incomplete or inconsistent.");
        package.cluster.insert(package.cluster.end(),ids.begin(),ids.end());
        total+=Size(chunk.Find("chunkweight"),package.cluster_weight);
    }
    std::ranges::sort(package.cluster,{},&Hash256::hex);
    Require(total==package.cluster_weight && package.cluster.size()==count
        && std::adjacent_find(package.cluster.begin(),package.cluster.end())==package.cluster.end()
        && std::ranges::all_of(entries,[&](const auto& item){
            return std::ranges::binary_search(package.cluster,item.first,{},&Hash256::hex);
        }),"Cluster membership or policy weight is inconsistent.");
    return package;
}

void Traverse(const Entries& entries,const std::string& id,size_t depth,std::set<std::string>& path,
    std::map<std::string,size_t>& visited)
{
    Require(depth<=MAX_CHILD_PACKAGE_DEPTH && !path.contains(id),"The ancestor graph is cyclic or exceeds the eight-level bound.");
    const auto entry=entries.find(id);
    Require(entry!=entries.end(),"Ancestor membership is incomplete.");
    if (const auto seen=visited.find(id);seen!=visited.end() && seen->second>=depth) return;
    visited[id]=depth;
    path.insert(id);
    for (const auto& parent:entry->second.parents) Traverse(entries,parent.hex(),depth+1,path,visited);
    path.erase(id);
}
} // namespace

PackageSnapshot ReadChildPackage(const PackageRead& read,const protocol::TransactionDocument& parent,uint32_t output,
    protocol::DisplayNetwork network,std::string_view tip,std::stop_token stop)
{
    Require(!parent.psbt_version && parent.transaction && parent.transaction->txid && parent.transaction->wtxid
        && parent.transaction->sizes && output<parent.transaction->outputs.size(),"The exact parent or selected outpoint is Unknown.");
    const auto& parent_id=*parent.transaction->txid;
    CheckOutput(read,parent,output,tip);
    const auto entries=ReadEntries(read,parent_id);
    std::set<std::string> path;
    std::map<std::string,size_t> visited;
    Traverse(entries,parent_id.hex(),1,path,visited);
    Require(visited.size()==entries.size(),"The mempool returned unrelated transactions as ancestors.");
    auto package=ReadPolicy(read,parent_id,entries);
    std::map<std::string,std::shared_ptr<const protocol::TransactionDocument>> raw;
    size_t total_bytes{},edges{};
    for (const auto& [txid,entry]:entries) {
        const auto response=read("getrawtransaction","["+rpc::Quote(txid)+",0]","Exact ancestor bytes are Unknown.");
        auto parsed=protocol::ParseTransactionDocument(Text(&response),network,stop,true).document;
        Require(parsed && !parsed->psbt_version && parsed->transaction && parsed->transaction->txid==entry.txid
            && parsed->transaction->wtxid==entry.wtxid && parsed->transaction->sizes
            && parsed->transaction->sizes->weight==entry.weight && !parsed->transaction->coinbase,
            "An ancestor's bytes, witness or weight do not match the current mempool.");
        Require(txid!=parent_id.hex() || parsed->wire==parent.wire,"The current parent bytes changed.");
        total_bytes+=parsed->wire.size();
        edges+=parsed->transaction->inputs.size();
        Require(total_bytes<=MAX_WIRE && edges<=MAX_EDGES,"Ancestor wire or dependency work exceeds the package bound.");
        raw.emplace(txid,std::move(parsed));
    }

    int64_t total_fee{};
    std::set<std::pair<std::string,uint32_t>> spent;
    for (const auto& [txid,entry]:entries) {
        const auto& tx=*raw.at(txid)->transaction;
        std::set<Hash256,decltype([](const Hash256& a,const Hash256& b){return a.hex()<b.hex();})> parents;
        int64_t in{},out{};
        for (const auto& input:tx.inputs) {
            const auto& point=input.outpoint;
            Require(spent.emplace(point.txid.hex(),point.index).second,"Two ancestor transactions claim the same previous output.");
            if (const auto ancestor=raw.find(point.txid.hex());ancestor!=raw.end()) {
                const auto& outputs=ancestor->second->transaction->outputs;
                Require(point.index<outputs.size(),"An ancestor dependency refers outside the exact parent outputs.");
                parents.insert(point.txid);
                Add(in,outputs[point.index].value);
            } else {
                // Consult the active chain UTXO set without mempool effects.
                // An omitted mempool ancestor cannot establish confirmation here.
                const auto known=read("gettxout","["+rpc::Quote(point.txid.hex())+","+std::to_string(point.index)+",false]",
                    "A confirmed package-boundary input is Unknown.");
                const auto amount=Amount(known.Find("value"));
                const auto* script=known.Find("scriptPubKey");
                Require(Text(known.Find("bestblock"))==tip && Integer(known.Find("confirmations")).value_or(0)>0
                    && amount && script && !Text(script->Find("hex")).empty(),
                    "A package-boundary input is Unknown or belongs to omitted ancestry.");
                Add(in,*amount);
            }
        }
        Require(std::ranges::equal(parents,entry.parents),"The exact input links disagree with the announced ancestors.");
        for (const auto& output:tx.outputs) Add(out,output.value);
        Require(in>=out && in-out==entry.base_fee.sats(),"An ancestor base fee does not reconcile with its exact inputs and outputs.");
        Add(total_fee,entry.base_fee);
        package.vsize+=tx.sizes->vsize;
        package.ancestors.push_back(entry);
    }
    package.base_fee=Money::Satoshis(total_fee);
    CheckOutput(read,parent,output,tip);
    Require(ReadEntries(read,parent_id)==entries,"The parent or ancestor membership, fees or sizes changed during the package check.");
    auto latest=ReadPolicy(read,parent_id,entries);
    latest.ancestors=package.ancestors;
    latest.base_fee=package.base_fee;
    latest.vsize=package.vsize;
    Require(latest==package,"The cluster or its policy changed during the package check.");
    return package;
}

ChildFee EstimateChildFee(const PackageSnapshot& package,uint32_t target,std::string_view script,Money available)
{
    const auto estimated=12+9+script.size()/2+FundingInputVsizeBound(script,{});
    const auto minimum_size=10+41+9+script.size()/2; // Unsigned wire lower bound.
    const auto cluster_vsize=(package.cluster_weight+3)/4;
    Require(package.cluster.size()<package.cluster_count_limit && cluster_vsize+estimated<=package.cluster_vsize_limit,
        "The estimated child would exceed the reported cluster transaction or size limit.");
    const int64_t current=package.base_fee.sats();
    Require(target && int64_t(target)*int64_t(package.vsize)>current*1000,
        "The currently observed package already reaches the requested base fee rate.");
    const int64_t package_fee=(int64_t(target)*int64_t(package.vsize+estimated)+999)/1000-current;
    const int64_t floor=package.mempool_floor.sats();
    Require(floor<=BROADCAST_MAX_SAT_PER_KVB,"The current mempool fee floor exceeds the supported own fee rate.");
    const int64_t own_floor=(floor*int64_t(estimated)+999)/1000;
    const int64_t fee=std::max(package_fee,own_floor);
    Require(fee>0 && fee*1000<=BROADCAST_MAX_SAT_PER_KVB*int64_t(minimum_size),
        "The child could exceed the 100 sat/vB own-transaction cap. Choose a lower package target.");
    Require(fee<available.sats() && current<=MAX_MONEY-fee,
        "The selected output cannot cover this child fee or the package exceeds the monetary bound.");
    return {Money::Satoshis(fee),estimated};
}
} // namespace roots::gui::app
