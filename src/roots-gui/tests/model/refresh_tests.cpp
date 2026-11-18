// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/refresh.h"
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <stdexcept>
using namespace roots::gui;
namespace {
BackendCapabilities Caps(std::string wallet="one",char network='1') {
    BackendCapabilities c;c.candidate.connection.credential.reference="/explicit/fixture-cookie";c.candidate.wallet=wallet;c.candidate.expected_genesis=std::string(64,network);c.genesis=c.candidate.expected_genesis;
    c.authenticated=c.identity_verified=true;c.wallet=WalletAvailability::Available;
    for(const char* m:{"getwalletinfo","getbalances","listsinceblock"})c.methods[m]={true,true,true,false,{"blockhash","include_removed"}};
    return c;
}
RefreshIdentity Identity(const BackendCapabilities& c,uint64_t revision=1,uint64_t epoch=0) {
    return {{*BackendSessionId::Make("backend-"+c.genesis.substr(0,1)),{*Hash256::Parse(c.genesis)},*WalletId::Make(*c.candidate.wallet),*DocumentId::Make("document"),{revision},{epoch}},c.candidate,1};
}
struct Source {
    std::atomic<int> calls{},active{},peak{},fail{},wrong{};
    std::atomic<bool> hold{},entered{},release{};
    std::mutex mutex;std::condition_variable cv;
    std::vector<RefreshRequest> requests;
    RefreshReply operator()(const RefreshRequest& r,std::stop_token) {
        ++calls;int running=++active;peak=std::max(peak.load(),running);
        {std::lock_guard lock(mutex);requests.push_back(r);}
        if(hold){entered=true;std::unique_lock lock(mutex);if(!cv.wait_for(lock,std::chrono::seconds(2),[&]{return release.load();}))throw std::runtime_error("held source deadline");}
        RefreshReply reply{r.identity,r.generation,r.sequence};reply.observed_unix_ms=1700000000000LL+int64_t(r.sequence)*1000;
        if(fail)reply.errors.fill(fail==1?rpc::Error::Connection:rpc::Error::Authentication);
        else {reply.node=NodeObservation{*r.identity.profile.wallet=="two"?20:10,std::string(64,'a'),false,false};reply.wallet=WalletObservation{Money::Satoshis(1),Money::Satoshis(2),Money::Satoshis(3)};
            if(r.fetch_history)reply.history=HistoryDelta{*rpc::Json::Parse("[]"),*rpc::Json::Parse("[]"),std::string(64,'a')};}
        if(wrong)reply.identity.context.revision.value++;
        --active;return reply;
    }
    void Release(){hold=false;release=true;cv.notify_all();}
    ~Source(){Release();}
};
}
int main(){
    int checks{};const auto check=[&](bool ok){if(!ok)throw std::runtime_error("refresh contract "+std::to_string(checks+1));++checks;};
    Source source;RefreshService service([&](const auto& r,std::stop_token t){return source(r,t);});
    const auto settle=[&](int64_t now){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(service.Busy() && std::chrono::steady_clock::now()<end){service.Tick(now);std::this_thread::yield();}check(!service.Busy());};
    const auto held=[&]{const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(1);while(!source.entered && std::chrono::steady_clock::now()<end)std::this_thread::yield();check(source.entered);};
    auto a=Caps();auto ia=Identity(a);check(service.Bind(ia,a,false,0));service.Tick(0);settle(0);auto first=service.View();
    check(first.node && first.node->height==10 && first.wallet->trusted.sats()==1 && first.freshness[0]==Freshness::Live);
    const auto observed=first.observed[0];check(ObservationDate(observed).find("UTC")!=std::string::npos);
    service.Tick(4999);check(source.calls==1);service.Tick(5000);settle(5000);check(source.calls==2 && !source.requests.back().fetch_history);
    check(service.View().history && service.View().freshness[2]==Freshness::Cached);
    check(!service.AcknowledgeHistory(ia,999,std::string(64,'a')));check(service.AcknowledgeHistory(ia,service.PendingHistorySequence(),std::string(64,'a')));
    service.RequestNow(5000);service.Tick(5000);settle(5000);check(source.requests.back().cursor==std::string(64,'a') && source.requests.back().fetch_history);
    source.hold=true;source.release=false;source.entered=false;service.RequestNow(5000);service.Tick(5000);held();
    auto b=Caps("two");auto ib=Identity(b);check(service.Bind(ib,b,false,5000));check(!service.View().node);service.Tick(5000);check(source.active==1);source.Release();settle(5000);
    check(service.View().node && service.View().node->height==20 && service.View().identity->context.wallet==ib.context.wallet && source.peak==1);
    check(service.Bind(ia,a,false,5000));check(service.View().node->height==10 && service.View().freshness[0]==Freshness::Cached);
    source.hold=true;source.release=false;source.entered=false;service.RequestNow(5000);service.Tick(5000);held();
    auto c=Caps("one",'2');auto ic=Identity(c);check(service.Bind(ic,c,false,5000));check(!service.View().node);source.Release();settle(5000);check(service.View().identity->context.network==ic.context.network && service.View().node);
    const auto before=service.View().observed[0];const int prior=source.calls;
    source.hold=true;source.release=false;source.entered=false;service.RequestNow(5000);service.Tick(5000);held();service.SetOffline(true,5000);
    for(int i=0;i<50;++i)service.Tick(1000000);check(source.calls==prior+1 && service.View().freshness[0]==Freshness::Offline && service.View().observed[0]==before);
    source.Release();settle(1000000);check(source.calls==prior+1 && service.View().observed[0]==before && !service.RequestNow(1000000));
    service.SetOffline(false,1000000);service.Tick(1000000);settle(1000000);check(service.View().freshness[0]==Freshness::Live);
    source.fail=1;service.RequestNow(1000000);service.Tick(1000000);settle(1000000);check(service.View().node && service.View().freshness[0]==Freshness::Stale && service.View().next_due_ms==1005000);
    service.Tick(1005000);settle(1005000);check(service.View().next_due_ms==1015000);
    for(int i=0;i<5;++i){const auto due=service.View().next_due_ms;service.Tick(due);settle(due);check(service.View().next_due_ms-due<=60000);}
    source.fail=0;source.wrong=1;auto due=service.View().next_due_ms;service.Tick(due);settle(due);check(service.View().errors[0]==rpc::Error::Identity && service.View().freshness[0]==Freshness::Stale);
    source.wrong=0;service.RequestNow(due);service.Tick(due);settle(due);check(service.View().freshness[0]==Freshness::Live);
    source.hold=true;source.release=false;source.entered=false;service.RequestNow(due);service.Tick(due);held();auto locked=Identity(c,2,1);check(service.Bind(locked,c,true,due));check(!service.View().identity && !service.View().node && !service.View().wallet);source.Release();settle(due);const int count=source.calls;service.Tick(due+100000);check(source.calls==count);
    check(service.Bind(Identity(c,3,2),c,false,due));service.Tick(due);settle(due);check(service.View().node);
    service.Cancel(due);service.Tick(due+1);check(!service.Busy() && service.View().freshness[0]==Freshness::Cached);
    auto invalid=c;invalid.error=rpc::Error::Identity;check(!service.Bind(ic,invalid,false,due));check(!service.View().identity);
    check(source.active==0 && source.peak==1);std::cout<<"Refresh model checks: "<<checks<<" passed; all held operations released and workers settled\n";
}
