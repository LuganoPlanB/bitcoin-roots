// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"

#include <atomic>
#include <iostream>
#include <numeric>
#include <semaphore>

using namespace roots::gui;
namespace {
int checks{};
void Check(bool ok, const char* reason)
{
    if (!ok) throw std::runtime_error(reason);
    ++checks;
}
rpc::Json Json(const std::string& text) { return *rpc::Json::Parse(text); }
FeeScope Scope()
{
    const auto genesis=*Hash256::Parse(std::string(64,'a'));
    ServerCandidate profile;
    profile.expected_genesis=genesis.hex();
    profile.wallet="actual wallet";
    return {{{{*BackendSessionId::Make("connection-1"),{genesis},*WalletId::Make("actual wallet"),
        *DocumentId::Make("wallet-document-10"),{3},{4}},profile,1},5,10},{6},std::string(64,'b'),3,{}};
}
void Rates()
{
    const auto valid=ReadFeeQuote(Json(R"({"feerate":0.00002125,"blocks":2,"estimator":"block_policy"})"));
    Check(valid.rate==2125 && valid.blocks==2 && valid.estimator=="block_policy" && valid.error.empty(),"exact BTC/kvB decoding");
    Check(FeeRateText(*valid.rate)=="2.125" && FeeRateText(0)=="0.000","exact display conversion including known zero");
    Check(EffectiveFundingRate("",3,&valid)==2125,"target supplies an exact live fee");
    Check(EffectiveFundingRate("7.001",3,&valid)==7001,"manual overrides target");
    Check(!EffectiveFundingRate("7,001",3,&valid) && !EffectiveFundingRate("0",3,&valid)
        && !EffectiveFundingRate("7.0001",3,&valid),"invalid entered amount cannot fall back");
    Check(!EffectiveFundingRate("",0,&valid) && !EffectiveFundingRate("",3,nullptr),"manual mode and missing target remain Unknown");
    const auto missing=ReadFeeQuote(Json(R"({"blocks":2,"errors":["Insufficient data"]})"));
    Check(!missing.rate && missing.blocks==2 && missing.error=="Insufficient data","real returned target and errors retained");
    const auto zero=ReadFeeQuote(Json(R"({"blocks":0,"errors":["No feerate found"]})"));
    Check(zero.blocks==0 && !zero.rate && !EffectiveFundingRate("",3,&zero),"zero returned target is retained with no effective quote");
    Check(!ReadFeeQuote(Json(R"({"feerate":0.000000001,"blocks":3})")).rate,"fractional satoshi rejected");
    Check(!ReadFeeQuote(Json(R"({"feerate":0.00001000,"blocks":3,"errors":["inconsistent"]})")).rate,"conflicting response refused");
    const auto policy=ReadFeePolicy(Json(R"({"loaded":true,"minrelaytxfee":0.00001000,"mempoolminfee":"0.00002000","incrementalrelayfee":"0.00000000"})"));
    Check(policy && policy->relay==1000 && policy->mempool==2000 && policy->incremental==0,"policy keeps distinct exact floors and known zero");
    Check(!MeetsFeeMinimum(1999,*policy) && MeetsFeeMinimum(2000,*policy),"inclusive known policy boundary");
    Check(!ReadFeePolicy(Json(R"({"loaded":true,"minrelaytxfee":0.00002000,"mempoolminfee":0.00001000})")),"conflicting floors refused");
    Check(!ReadFeePolicy(Json(R"({"loaded":false,"minrelaytxfee":0})")),"unloaded mempool has no trusted zero");
    const auto sat=[](int64_t x){return Money::Satoshis(x);};
    Check(!HighFundingFee(sat(99999),sat(10000000)) && HighFundingFee(sat(100000),sat(10000000)),"absolute high fee boundary");
    Check(!HighFundingFee(sat(500),sat(5001)) && HighFundingFee(sat(500),sat(5000)),"exact ten-percent high fee boundary");
    Check(!HighFundingFee(sat(0),sat(1)) && !HighFundingFee(sat(1),sat(0)),"no ratio warning from missing or zero totals");
}
void Mempool()
{
    const std::string a(64,'a'), b(64,'b'), c(64,'c');
    const auto entry=[](const std::string& id,const char* fee,std::string parents) {
        return rpc::Quote(id) + ":{" + "\"vsize\":100,\"fees\":{\"base\":" + fee
            + "},\"depends\":[" + parents + "]}";
    };
    const auto data=Json("{" + entry(a,"0.00000099","") + "," + entry(b,"0.00000100",rpc::Quote(a))
        + "," + entry(c,"0.00002500",rpc::Quote(a)) + "}");
    const auto view=ReadMempoolFees(data,std::array{b,c,a,b});
    Check(view && view->bands.vbytes==std::array<uint64_t,6>{100,100,0,0,0,100},"virtual bytes grouped at exact fee boundaries");
    Check(view->ancestors && view->ancestors->sats==2699 && view->ancestors->vbytes==300
        && view->ancestors->transactions==3,"ancestor closure deduplicates shared parents and selected roots");
    Check(!ReadMempoolFees(data,std::array{std::string(64,'d')})->ancestors,"absent unconfirmed parent remains Unknown");
    const auto incomplete=ReadMempoolFees(Json("{" + entry(b,"0.00000100",rpc::Quote(a)) + "}"),std::array{b});
    Check(incomplete && !incomplete->ancestors,"a missing edge does not invent a package");
    const auto cycle=ReadMempoolFees(Json("{" + entry(a,"0.00000100",rpc::Quote(b)) + "," + entry(b,"0.00000100",rpc::Quote(a))+"}"),std::array{b});
    Check(cycle && !cycle->ancestors,"conflicting ancestor cycle denied");
    const auto disagreement=ReadMempoolFees(Json("{"+rpc::Quote(a)
        +R"(:{"vsize":100,"fees":{"base":0.00000099},"depends":[],"ancestorcount":2,"ancestorsize":100}})"),std::array{a});
    Check(disagreement && !disagreement->ancestors,"closure must agree with the reported ancestor count and size");
    const auto empty=ReadMempoolFees(Json("{}"),{});
    Check(empty && std::accumulate(empty->bands.vbytes.begin(),empty->bands.vbytes.end(),0ULL)==0 && !empty->ancestors,
        "actual empty mempool is a known zero histogram");
    Check(!ReadMempoolFees(Json(R"({"not-a-txid":{"vsize":100,"fees":{"base":0}}})"),{}),"malformed public snapshot fails closed");
}
void History()
{
    constexpr int64_t start=100000000;
    constexpr int64_t unix_start=1800000000000;
    FeeHistory history;
    history.Append({unix_start,start,FeeBandSnapshot{}});
    Check(!history.Coverage(start,2).sufficient && !history.Coverage(start,24).sufficient,"single real point does not cover either range");
    history.Append({unix_start+1000,start+1000,FeeBandSnapshot{}});
    Check(history.Samples().size()==1,"excess reads are not counted as independent 5-minute coverage");
    for (int64_t i=1;i<=288;++i)
        history.Append({unix_start+i*FEE_SAMPLE_INTERVAL,start+i*FEE_SAMPLE_INTERVAL,FeeBandSnapshot{}});
    const auto now=start+288*FEE_SAMPLE_INTERVAL;
    Check(history.Coverage(now,2).sufficient && history.Coverage(now,24).sufficient,"actual sufficient two-hour and daily windows");
    Check(!history.Coverage(now+FEE_SAMPLE_GAP+1,2).sufficient,"stale endpoint disables range");
    history.Append({unix_start+289*FEE_SAMPLE_INTERVAL,start+289*FEE_SAMPLE_INTERVAL,{}});
    const auto gaps=history.Coverage(now+FEE_SAMPLE_INTERVAL,2);
    Check(!gaps.sufficient && gaps.gaps==1,"failed sample creates a gap");
    for (int64_t i=290;i<=310;++i)
        history.Append({unix_start+i*FEE_SAMPLE_INTERVAL,start+i*FEE_SAMPLE_INTERVAL,FeeBandSnapshot{}});
    Check(history.Samples().size()<=291 && history.Samples().back().monotonic_ms==start+310*FEE_SAMPLE_INTERVAL,
        "bounded 24-hour cache evicts earliest samples");
    const FeeObservation quote{FeeRead::Target,unix_start,start};
    Check(FeeObservationLive(quote,start+FEE_SAMPLE_INTERVAL) && !FeeObservationLive(quote,start+FEE_SAMPLE_INTERVAL+1)
        && !FeeObservationLive(quote,start-1),"monotonic quote freshness does not use wall clock");
}
void ScopeAndWorker()
{
    auto scope=Scope();
    ui::shell::State state;
    app::Document draft{10,app::DocumentKind::Wallet,"draft",{}};
    draft.funding_target=3;
    draft.draft_revision=scope.draft;
    state.fees.owner=scope.owner;
    state.fees.quoted=scope;
    state.fees.target=FeeObservation{FeeRead::Target,1800000000000,RefreshMonotonicMs(),scope.tip,
        FeeQuote{2125,2},FeePolicy{1000,2000,1000}};
    const auto terms=ui::shell::FeeTerms(state,draft,scope.owner,scope.tip);
    Check(terms.rate==2125 && terms.quote_observation>0 && !terms.below_minimum,"draft quote comes from one live scope");
    draft.funding_rate="1.999";
    const auto override=ui::shell::FeeTerms(state,draft,scope.owner,scope.tip);
    Check(override.rate==1999 && override.below_minimum && override.quote_observation==0,"manual rate precedes quote and remains below policy");
    draft.funding_rate.clear();
    auto changed=scope.owner;
    ++changed.wallet.context.lock_epoch.value;
    Check(!ui::shell::FeeTerms(state,draft,changed,scope.tip).rate
        && !ui::shell::FeeTerms(state,draft,scope.owner,std::string(64,'c')).rate,"another lock epoch or chain tip retires the rate");
    ++draft.draft_revision.value;
    Check(!ui::shell::FeeTerms(state,draft,scope.owner,scope.tip).rate,"another draft revision cannot reuse the quote");

    std::binary_semaphore entered(0), finish(0);
    std::atomic<bool> stopped{};
    FeeService service([&](const FeeRequest&,std::stop_token stop){
        entered.release();
        finish.try_acquire_for(std::chrono::seconds(2));
        stopped=stop.stop_requested();
        return FeeObservation{FeeRead::Target,1800000000000,RefreshMonotonicMs(),scope.tip,FeeQuote{2125,2}};
    });
    Check(service.Start({scope,FeeRead::Target,{true}}),"worker accepts explicit read");
    Check(entered.try_acquire_for(std::chrono::seconds(2)),"owned worker entered before retiring its scope");
    auto revised=scope;
    ++revised.draft.value;
    const auto early=service.Poll(revised);
    finish.release();
    if (early) throw std::runtime_error("retired draft published early");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    bool published{};
    while (service.Busy() && std::chrono::steady_clock::now()<deadline) {
        published=service.Poll(revised).has_value() || published;
        std::this_thread::yield();
    }
    Check(!published && !service.Busy() && stopped,"late response cancels monotonically and settles");
}
}
int main()
{
    try {
        Rates(); Mempool(); History(); ScopeAndWorker();
        std::cout << checks << " fee observation assertions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << " after " << checks << " assertions\n";
        return 1;
    }
}
