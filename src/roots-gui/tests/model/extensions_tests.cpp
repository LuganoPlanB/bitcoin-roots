// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/extension_review.h"

#include <iostream>
#include <stdexcept>

using namespace roots::gui;
namespace a=app;
namespace p=protocol;
namespace {
constexpr auto GENESIS="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
constexpr auto MAIN="000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";
constexpr auto KEY="wpkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)";
constexpr auto CHANGE="wpkh(02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5)";
constexpr auto PAYMENT="bcrt1qrk5wn7dxwy8l6ru39h6xvmdhxnvdh8p0kpjlxw";
constexpr int64_t NOW=200'000;
constexpr auto NETWORK=p::DisplayNetwork::Regtest;
int checks{};

template<typename Value> void Check(const Value& ok,const char* why)
{
    if (!static_cast<bool>(ok)) throw std::runtime_error(why);
    ++checks;
}

NetworkIdentity Net(std::string_view genesis=GENESIS) {return {*Hash256::Parse(genesis)};}
Hash256 Tip(char digit='f') {return *Hash256::Parse(std::string(64,digit));}
RequestContext Owner()
{
    return {*BackendSessionId::Make("fixture-rpc"),Net(),*WalletId::Make("fixture-wallet"),
        *DocumentId::Make("wallet-document-1"),{1},{2}};
}

struct Index final:HistoryIndexAdapter {
    std::optional<HistoryIndexReply> reply;
    mutable unsigned calls{};
    bool public_queries{};
    std::stop_source* retire{};
    std::string_view Identity() const override {return "fixture-index";}
    bool Supports(IndexAccess access,NetworkIdentity network) const override {
        return (access==IndexAccess::Private || public_queries) && network==Net();
    }
    std::optional<HistoryIndexReply> Read(const HistoryIndexQuery&,std::stop_token) const override {
        ++calls;
        if (retire) retire->request_stop();
        return reply;
    }
};

struct Constructor final:TransactionConstructionAdapter {
    std::optional<ConstructionReply> reply;
    mutable unsigned calls{};
    std::string_view Identity() const override {return "fixture-construction";}
    bool Supports(ConstructionStrategy,NetworkIdentity network) const override {return network==Net();}
    std::optional<ConstructionReply> Prepare(const ConstructionIntent&,std::stop_token) const override {++calls;return reply;}
};

FundingCoin Coin(char source)
{
    const auto preview=p::PreviewPublicWallet(p::PublicImportKind::Descriptor,KEY,"regtest");
    Check(preview.preview && preview.preview->descriptors.size()==1,"derive independent fixture coin policy");
    const auto& descriptor=preview.preview->descriptors.front();
    auto previous="0200000001"+std::string(64,source)+"0000000000feffffff0180c3c9010000000016"
        +descriptor.script+"00000000";
    const auto parsed=p::ParseTransactionDocument(previous,NETWORK).document;
    Check(parsed && parsed->transaction->txid,"parse independent fixture previous wire");
    return {{*parsed->transaction->txid,0},Money::Satoshis(30'000'000),descriptor.script,
        {std::move(previous),descriptor.descriptor,{},{},NOW-50},6};
}

std::string SpendingWire(const FundingCoin& coin)
{
    const auto id=coin.point.txid.hex();
    std::string reversed;
    for (size_t i=id.size();i;i-=2) reversed+=id.substr(i-2,2);
    return "0200000001"+reversed+"0000000000feffffff0170c3c9010000000016"+coin.script+"00000000";
}

void History()
{
    const auto coin=Coin('1');
    HistoryIndexQuery query{Owner(),7,11,3,IndexAccess::Private,coin.point,Tip(),true};
    const auto& shipped=ShippedExtensions();
    Check(!HistoryIndexStatus(shipped,IndexAccess::Private,Net()).available
        && !HistoryIndexStatus(shipped,IndexAccess::Public,Net()).available,"shipped options unavailable");
    Check(!ReadIndexedHistory(shipped,query,query,NOW),"required RPC has no index port");
    Index index;
    ExtensionRegistry fixtures{&index,nullptr};
    Check(HistoryIndexStatus(fixtures,IndexAccess::Private,Net()).available
        && !HistoryIndexStatus(fixtures,IndexAccess::Public,Net()).available
        && !HistoryIndexStatus(fixtures,IndexAccess::Private,Net(MAIN)).available,"fixture capability mode and genesis");
    const auto proof=IndexedOutput{coin.point,coin.proof.previous_hex,SpendingWire(coin)};
    index.reply=HistoryIndexReply{query,{"fixture-index",Net(),Tip(),NOW-20},IndexCoverage::ProviderCompleteAtTip,0,false,{proof}};
    const auto complete=ReadIndexedHistory(fixtures,query,query,NOW);
    Check(complete && complete->coverage==IndexCoverage::ProviderCompleteAtTip && complete->outputs.size()==1,
          "authenticated provider claim retains exact output and spending proof");
    auto public_query=query;
    public_query.access=IndexAccess::Public;
    index.reply->query=public_query;
    Check(!ReadIndexedHistory(fixtures,public_query,public_query,NOW),"private-only client cannot route public queries");
    index.public_queries=true;
    Check(HistoryIndexStatus(fixtures,IndexAccess::Public,Net()).available
        && ReadIndexedHistory(fixtures,public_query,public_query,NOW),"explicit permission reaches only an eligible public fixture");
    index.reply->query=query;
    index.reply->has_more=true;
    const auto partial=ReadIndexedHistory(fixtures,query,query,NOW);
    Check(partial && partial->coverage==IndexCoverage::Partial,"bounded page cannot promote incomplete history");
    index.reply->has_more=false;
    index.reply->first_height=30;
    const auto incomplete=ReadIndexedHistory(fixtures,query,query,NOW);
    Check(incomplete && incomplete->coverage==IndexCoverage::Partial,"missing block range remains partial");
    index.reply->coverage=IndexCoverage::Partial;
    index.reply->first_height=0;
    index.reply->outputs.clear();
    const auto empty=ReadIndexedHistory(fixtures,query,query,NOW);
    Check(empty && empty->coverage==IndexCoverage::Partial && empty->outputs.empty(),"empty partial observation never becomes complete");
    index.reply->outputs={proof};
    auto script_query=query;
    script_query.subject=coin.script;
    index.reply->query=script_query;
    Check(ReadIndexedHistory(fixtures,script_query,script_query,NOW),"exact script lookup uses the same wire proof");
    index.reply->query=query;
    index.reply->observation.network=Net(MAIN);
    Check(!ReadIndexedHistory(fixtures,query,query,NOW),"foreign genesis rejected");
    index.reply->observation.network=Net();
    index.reply->observation.tip=Tip('d');
    Check(!ReadIndexedHistory(fixtures,query,query,NOW),"wrong observed tip rejected");
    index.reply->observation.tip=Tip();
    index.reply->observation.observed_ms=NOW-130'000;
    Check(!ReadIndexedHistory(fixtures,query,query,NOW),"stale observation unavailable");
    index.reply->observation.observed_ms=NOW-20;
    index.reply->outputs[0].spending_wire=SpendingWire(Coin('2'));
    Check(!ReadIndexedHistory(fixtures,query,query,NOW),"foreign spend cannot prove a link");
    index.reply->outputs[0]=proof;
    auto denied=query;
    denied.permission=false;
    auto replaced=query;
    ++replaced.owner.lock_epoch.value;
    const auto calls=index.calls;
    Check(!ReadIndexedHistory(fixtures,denied,denied,NOW) && !ReadIndexedHistory(fixtures,query,replaced,NOW)
        && index.calls==calls,"permission and current owner gate dispatch");
    Check(!ValidateIndexedHistory(fixtures,replaced,*complete,NOW)
        && !ValidateIndexedHistory(fixtures,query,*complete,NOW+130'000) && index.calls==calls,
        "owner rechecks epoch and freshness before adopting a completed result");
    std::stop_source stop;
    index.retire=&stop;
    Check(!ReadIndexedHistory(fixtures,query,query,NOW,stop.get_token()) && index.calls==calls+1,
          "retired operation cannot adopt even a valid reply");
}

std::string PrepareWire(const std::vector<FundingCoin>& coins,p::OfflineChange change,Money fee)
{
    p::OfflineDraft draft{NETWORK,{},{{PAYMENT,Money::Satoshis(10'000'000)}},fee,change};
    for (const auto& coin:coins) draft.inputs.push_back({coin.point,coin.amount,coin.script,coin.proof});
    const auto created=p::ConstructOfflineDraft(draft).document;
    Check(created,"construct independent fixture unsigned PSBT");
    auto wire=created->wire;
    wire.insert(5,"\x04\xfc\x01" "r" "\x01\x06" "opaque");
    return wire;
}

void Construction()
{
    ServerCandidate candidate;
    candidate.expected_genesis=GENESIS;
    candidate.wallet="fixture-wallet";
    const FundingScope scope{{Owner(),candidate,7},1,11,12,13,14,{1},Tip().hex(),1000,0};
    auto scope_later=scope;
    ++scope_later.draft.value;
    const auto public_change=p::PreviewPublicWallet(p::PublicImportKind::Descriptor,CHANGE,"regtest");
    Check(public_change.preview && public_change.preview->descriptors.size()==1,"independent change descriptor");
    const p::OfflineChange change{public_change.preview->descriptors.front().descriptor,0,NOW-15};
    const auto first=Coin('1'),second=Coin('2');
    const auto script=*p::AddressScript(PAYMENT,NETWORK);
    const auto one=PrepareWire({first},change,Money::Satoshis(300));
    const auto both=PrepareWire({first,second},change,Money::Satoshis(450));
    const ExtensionObservation observation{"fixture-construction",Net(),Tip(),NOW-20};
    ConstructionIntent intent{scope,ConstructionStrategy::Efficiency,FundingSelection::Restricted,
        {first,second},{{Money::Satoshis(10'000'000),script}},Money::Satoshis(600),change};
    Constructor service;
    const ExtensionRegistry enabled{nullptr,&service};
    const auto& shipped=ShippedExtensions();
    Check(!ConstructionStatus(shipped,ConstructionStrategy::AvoidPartialSpends,Net()).available
        && !PrepareUnsigned(shipped,intent,scope,NOW),"RPC profile keeps optional construction unavailable");
    Check(ConstructionStatus(enabled,ConstructionStrategy::AvoidPartialSpends,Net()).available
        && !ConstructionStatus(enabled,ConstructionStrategy::AvoidPartialSpends,Net(MAIN)).available,"exact fixture capability gate");
    service.reply=ConstructionReply{scope,intent.strategy,observation,p::TransactionBase64(one)};
    const auto prepared=PrepareUnsigned(enabled,intent,scope,NOW);
    Check(prepared && prepared->Document()->wire==one && prepared->Document()->source==service.reply->psbt
        && prepared->Proof().fee.sats()==300,"validated exact bytes retain original opaque map");
    std::optional<PreparedConstruction> slot;
    a::SigningReviews reviews;
    Check(a::StageUnsignedConstruction(2,scope,NOW,*prepared,slot,reviews),"stage validated unsigned proposal");
    const auto digest=*a::SigningDigest("independent policy");
    const a::SigningBasis old{2,1,1,7,11,{1},{1},{2},false,
        {Owner().backend,Owner().wallet,Net()},prepared->Document(),prepared->Proof(),1,digest,{},digest,{1},false};
    Check(reviews.Record(old) && reviews.Approve(old) && reviews.Authorize(old),"separate explicit original review and approval");
    intent.strategy=ConstructionStrategy::AvoidPartialSpends;
    service.reply->strategy=intent.strategy;
    Check(!PrepareUnsigned(enabled,intent,scope,NOW) && reviews.Authorize(old),"partial same-script group rejected without altering valid work");
    service.reply->psbt=p::TransactionBase64(both);
    auto result=PrepareUnsigned(enabled,intent,scope,NOW);
    Check(result && result->Proof().inputs.size()==2 && result->Proof().fee.sats()==450
        && result->Document()->wire==both,"grouped strategy uses more exact inputs and fee, preserving raw source");
    Check(!a::StageUnsignedConstruction(2,scope_later,NOW,*result,slot,reviews)
        && !a::StageUnsignedConstruction(2,scope,NOW+130'000,*result,slot,reviews) && reviews.Authorize(old),
        "revised or expired proposal cannot replace current work");
    Check(a::StageUnsignedConstruction(2,scope,NOW,*result,slot,reviews) && !reviews.Authorize(old) && !reviews.HasReview(2),
          "changed strategy and bytes retire old review");
    Check(!reviews.Finalize(old,prepared->Document()).artifact,"retired authority cannot finalize");
    auto fresh=old;
    fresh.content=result->Document();
    fresh.proof=result->Proof();
    fresh.sighash={1,1};
    Check(!reviews.Approve(fresh) && reviews.Record(fresh) && reviews.Approve(fresh),"new draft needs separate explicit review");
    intent.strategy=ConstructionStrategy::Efficiency;
    service.reply->strategy=intent.strategy;
    result=PrepareUnsigned(enabled,intent,scope,NOW);
    Check(result && a::StageUnsignedConstruction(2,scope,NOW,*result,slot,reviews) && !reviews.Authorize(fresh),
          "strategy change alone requires review even for identical unsigned bytes");
    intent.recipients[0].value=Money::Satoshis(10'000'001);
    Check(!PrepareUnsigned(enabled,intent,scope,NOW),"adapter cannot change exact intended payment");
    intent.recipients[0].value=Money::Satoshis(10'000'000);
    intent.maximum_fee=Money::Satoshis(449);
    Check(!PrepareUnsigned(enabled,intent,scope,NOW),"adapter cannot exceed independent exact fee cap");
    intent.maximum_fee=Money::Satoshis(600);
    intent.selection=FundingSelection::Exact;
    intent.coins={first};
    Check(!PrepareUnsigned(enabled,intent,scope,NOW),"adapter cannot enlarge an exact pin set");
    intent.coins={first,second};
    service.reply->observation.network=Net(MAIN);
    Check(!PrepareUnsigned(enabled,intent,scope,NOW),"adapter cannot relabel a foreign network");
}
}

int main()
{
    try {
        History();
        Construction();
        std::cout<<"{\"selected\":2,\"executed\":2,\"passed\":2,\"failed\":0,\"skipped\":0,\"assertions\":"<<checks<<"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"Extension contracts failed after "<<checks<<" checks: "<<error.what()<<'\n';
        return 1;
    }
}
