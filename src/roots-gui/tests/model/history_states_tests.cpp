// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/history_workspace.h"
#include <atomic>
#include <condition_variable>
#include <iostream>
using namespace roots::gui;
namespace {
Hash256 H(char c){return *Hash256::Parse(std::string(64,c));}
int checks{};
void Check(bool value,const char* name){++checks;if(!value)throw std::runtime_error(name);}
}
int main(){try{
    BackendCapabilities caps;caps.candidate.connection.credential.reference="/explicit/fixture-cookie";caps.candidate.wallet="owned states";caps.genesis=caps.candidate.expected_genesis=H('a').hex();caps.authenticated=caps.identity_verified=true;caps.wallet=WalletAvailability::Available;
    for(const char* m:{"getwalletinfo","getbalances","listsinceblock","gettransaction"})caps.methods[m]={true,true,true,false,{"blockhash","include_removed","txid"}};
    RefreshIdentity identity{{*BackendSessionId::Make("connection-1"),{H('a')},*WalletId::Make("owned states"),*DocumentId::Make("wallet-document-1"),{1},{2}},caps.candidate,1};
    int phase{},calls{};bool no_progress{},pruned{},scanning{};std::vector<RefreshRequest> requests;
    std::atomic<bool> hold{},entered{},release{};std::mutex mutex;std::condition_variable cv;
    RefreshService service([&](const RefreshRequest& r,std::stop_token){
        ++calls;requests.push_back(r);if(hold){entered=true;std::unique_lock lock(mutex);if(!cv.wait_for(lock,std::chrono::seconds(2),[&]{return release.load();}))throw std::runtime_error("held deadline");}
        RefreshReply result{r.identity,r.generation,r.sequence};result.observed_unix_ms=1700000000000LL+r.sequence;
        result.node=NodeObservation{10,H(phase==2?'c':'b').hex(),false,pruned};
        if(phase==2){result.errors[2]=rpc::Error::Connection;return result;}
        if(!r.fetch_history)return result;
        HistoryBatch batch{r.identity.context,*r.identity.profile.wallet,H('b').hex()};batch.height=10;batch.full_query=!r.cursor;batch.stable=true;batch.pruned=pruned;batch.scanning=scanning;batch.limited=phase==4 || (phase==0 && (r.history_limits.transactions<512 || no_progress));batch.balances.trusted=Money::Satoshis(2);
        for(int n=1;n<=(phase==4?int(r.history_limits.transactions/256):(batch.limited?1:2));++n){HistoryTransaction tx{H(phase==4?"0123456789abcdef"[n-1]:char('0'+n))};tx.time=1700000000+n;tx.wallet_delta=Money::Satoshis(1);tx.confirmations=2;tx.block_height=9;tx.state=HistoryState::Confirmed;if(phase==3){tx.confirmations=0;tx.block_height.reset();tx.state=n==1?HistoryState::Replaced:HistoryState::Pending;if(n==1)tx.replaced_by=H('2');else tx.io={{"Input",H('4').hex()+":0","",{}}};}batch.transactions.push_back(std::move(tx));}
        if(phase==3)batch.balances.trusted=Money::Satoshis(1);
        result.history=HistoryDelta{{},{},H('b').hex(),std::move(batch)};return result;
    });
    const auto settle=[&]{const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(service.Busy() && std::chrono::steady_clock::now()<end){service.Tick(0);std::this_thread::yield();}Check(!service.Busy(),"worker settled");};
    Check(service.Bind(identity,caps,false,0),"verified binding");service.Tick(0);settle();
    Check(service.View().ledger->Limited() && service.View().ledger->Rows().size()==1 && !service.View().ledger->Complete(),"limited partial retains known parent");
    Check(!service.View().chain_current,"limited rows never assert whole current chain");
    Check(!service.AcknowledgeHistory(identity,service.PendingHistorySequence(),H('b').hex()),"limited delta cannot be acknowledged");
    auto wrong=identity;wrong.context.revision.value++;
    Check(!service.ScanEarlier(wrong,0),"query fence");service.SetOffline(true,0);Check(!service.ScanEarlier(identity,0),"offline scan suppressed");service.SetOffline(false,0);
    const auto pending=service.PendingHistorySequence();phase=2;Check(service.ScanEarlier(identity,0),"deliberate continuation before transport failure");service.Tick(0);settle();Check(service.View().history && service.PendingHistorySequence()==pending && service.View().ledger->Rows().size()==1,"failed larger scan retains original unacknowledged delta and rows");service.RequestNow(0);service.Tick(0);settle();Check(!requests.back().fetch_history && service.View().history,"ordinary refresh never silently retries failed continuation");phase=0;
    Check(service.ScanEarlier(identity,0),"deliberate continuation retry");service.Tick(0);settle();
    Check(requests.back().history_limits.transactions==1024 && !requests.back().cursor,"larger budget same unacknowledged full range");
    Check(service.View().ledger->Rows().size()==2 && service.View().ledger->Complete() && service.View().chain_current,"bounded progress reconciles exact total");
    Check(!service.ScanEarlier(identity,0),"complete observation has no invented earlier history");
    phase=2;service.RequestNow(0);service.Tick(0);settle();
    Check(service.View().ledger->Rows().size()==2 && service.View().freshness[2]==Freshness::Stale && !service.View().chain_current,"new chain plus failed history retains dated rows without old success");
    Check(app::HistoryStatus(*service.View().ledger,service.View().ledger->Rows().begin()->second,false).find("Current chain state Unknown")!=std::string::npos,"state is dated, not current confirmed");
    phase=3;service.RequestNow(0);service.Tick(0);settle();
    Check(requests.back().cursor==H('b').hex(),"failed refresh never advances cursor");
    Check(service.View().ledger->Rows().at(H('1').hex()).state==HistoryState::Replaced && service.View().ledger->Rows().at(H('2').hex()).confirmations==0,"reorg replacement changes current state");
    auto batch=HistoryBatch{identity.context,*identity.profile.wallet,H('b').hex()};batch.full_query=batch.stable=true;HistoryTransaction parent{H('4')};parent.time=1700000000;parent.state=HistoryState::Pending;batch.transactions={parent,service.View().ledger->Rows().at(H('2').hex())};HistoryLedger family(identity.context,*identity.profile.wallet);Check(family.Apply(batch,1,1),"parent fixture adopted");Check(app::HistoryStatus(family,family.Rows().at(H('2').hex()),true).find("unconfirmed parent")!=std::string::npos,"known unconfirmed ancestor explicit");
    auto conflict=parent;conflict.confirmations=-2;conflict.state=HistoryState::Conflicted;Check(app::HistoryStatus(family,conflict,true).find("Conflicted / -2")!=std::string::npos,"conflict depth explicit");
    auto rows=app::OrderedHistory(*service.View().ledger,{},{});auto csv=app::HistoryCsv(*service.View().ledger,rows,{},false,false);Check(csv && csv->find("Unknown; last observed")!=std::string::npos,"stale CSV never asserts old current confirmations");
    Check(!app::ExplorerUrl({H('a')},H('1')),"unknown genesis never routed to mainnet");auto main=*Hash256::Parse("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");auto test=*Hash256::Parse("00000000da84f2bafbbc53dee25a72ae507ff4914b867c565be350b0da8bf043");Check(app::ExplorerUrl({main},H('1'))=="https://mempool.space/tx/"+H('1').hex(),"exact mainnet public txid");Check(app::ExplorerUrl({test},H('2'))=="https://mempool.space/testnet4/tx/"+H('2').hex(),"testnet4 distinct route");
    PublicPreferences explorer;
    Check(!app::ExplorerUrl(explorer,{main},H('1')),"configured explorer permission defaults off");
    explorer.explorer=ExplorerMode::Custom;explorer.explorer_template="https://explorer.example/tx/{txid}";explorer.explorer_genesis=main.hex();
    Check(app::ExplorerUrl(explorer,{main},H('1'))=="https://explorer.example/tx/"+H('1').hex(),"custom template expands the confirmed exact-network txid");
    Check(!app::ExplorerUrl(explorer,{test},H('1')),"custom template never crosses genesis");
    service.Unbind();identity.context.revision.value++;phase=0;no_progress=true;Check(service.Bind(identity,caps,false,0),"new limited query");service.Tick(0);settle();Check(service.ScanEarlier(identity,0),"one larger retry");service.Tick(0);settle();Check(service.View().scan_blocked && !service.ScanEarlier(identity,0),"no progress stops further retries and preserves pending range");
    service.Unbind();identity.context.revision.value++;phase=4;no_progress=false;Check(service.Bind(identity,caps,false,0),"hard bound query binding");service.Tick(0);settle();for(size_t budget=512;budget<=4096;budget*=2){Check(service.ScanEarlier(identity,0),"explicit larger bounded step");service.Tick(0);settle();Check(service.View().scan_budget==budget && service.View().ledger->Rows().size()==budget/256,"each bounded step makes progress");}Check(!service.ScanEarlier(identity,0) && service.View().history && !service.AcknowledgeHistory(identity,service.PendingHistorySequence(),H('b').hex()),"4096 bound retains unacknowledged range, never wraps or retries");phase=0;
    service.Unbind();identity.context.revision.value++;pruned=true;no_progress=false;Check(service.Bind(identity,caps,false,0),"pruned query binding");service.Tick(0);settle();Check(service.View().ledger->Pruned() && !service.ScanEarlier(identity,0),"pruned source bound explicit, no daemon rescan");
    service.Unbind();identity.context.revision.value++;pruned=false;scanning=true;Check(service.Bind(identity,caps,false,0),"scanning query binding");service.Tick(0);settle();Check(service.View().ledger->Scanning() && !service.ScanEarlier(identity,0),"active daemon rescan cannot be bypassed");
    hold=true;service.RequestNow(0);service.Tick(0);const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(1);while(!entered && std::chrono::steady_clock::now()<end)std::this_thread::yield();Check(entered,"held normalized refresh entered");const auto observed=service.View().ledger->Observed();service.SetOffline(true,0);release=true;cv.notify_all();settle();Check(service.View().ledger->Observed()==observed && !service.View().chain_current,"disconnect during held refresh discards completion");const auto prior=calls;service.Tick(999999);Check(calls==prior,"offline never transports or retries");service.PrivacyLock();Check(service.HistoryViews().empty() && !service.ScanEarlier(identity,0),"lock retires scan and observations");
    std::cout<<"History state checks: "<<checks<<" passed; held worker settled; no RPC mutations or external requests\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<" after "<<checks<<" checks\n";return 1;}}
