// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/refresh.h"
#include <iostream>
using namespace roots::gui;
namespace {
std::string MoneyJson(std::optional<Money> value){return value?std::to_string(value->sats()):"null";}
void Print(const RefreshService& service){const auto view=service.View();std::cout<<"{\"errors\":[";for(size_t i=0;i<3;++i){if(i)std::cout<<',';std::cout<<rpc::Quote(rpc::ErrorName(view.errors[i]));}std::cout<<"],\"offline\":"<<(view.offline?"true":"false")<<",\"locked\":"<<(view.locked?"true":"false")<<",\"freshness\":"<<int(view.freshness[2])<<",\"observed\":"<<view.observed[2].value_or(0)<<",\"ledger\":"<<(view.ledger?"true":"false");
 if(view.ledger){const auto& book=*view.ledger;const auto& total=book.Totals();std::cout<<",\"complete\":"<<(book.Complete()?"true":"false")<<",\"limited\":"<<(book.Limited()?"true":"false")<<",\"has_rpc\":"<<(total.has_rpc?"true":"false")<<",\"inventory_reconciled\":"<<(total.inventory_reconciled?"true":"false")<<",\"totals\":{\"trusted\":"<<total.rpc.trusted.sats()<<",\"pending\":"<<total.rpc.pending.sats()<<",\"immature\":"<<total.rpc.immature.sats()<<",\"nonmempool\":"<<total.rpc.nonmempool.sats()<<",\"total\":"<<MoneyJson(total.total)<<",\"confirmed\":"<<MoneyJson(total.confirmed)<<",\"mempool\":"<<MoneyJson(total.mempool)<<",\"rpc_fundable\":"<<MoneyJson(total.rpc_fundable)<<",\"spendable\":"<<MoneyJson(total.spendable)<<"},\"rows\":[";bool first=true;
  for(const auto& [id,tx]:book.Rows()){if(!first)std::cout<<',';first=false;std::cout<<"{\"txid\":"<<rpc::Quote(id)<<",\"delta\":"<<tx.wallet_delta.sats()<<",\"amount\":"<<tx.reported_amount.value.sats()<<",\"amountLexeme\":"<<rpc::Quote(tx.reported_amount.lexeme)<<",\"fee\":"<<MoneyJson(tx.fee?std::optional<Money>(tx.fee->value):std::nullopt)<<",\"feeLexeme\":"<<(tx.fee?rpc::Quote(tx.fee->lexeme):"null")<<",\"state\":"<<int(tx.state)<<",\"removed\":"<<(tx.removed_event?"true":"false")<<",\"confirmations\":"<<tx.confirmations<<",\"time\":"<<tx.time<<",\"running\":"<<MoneyJson(tx.running_balance)<<",\"details\":[";
   bool part=true;for(const auto& d:tx.details){if(!part)std::cout<<',';part=false;std::cout<<"{\"vout\":"<<d.vout<<",\"category\":"<<rpc::Quote(d.category)<<",\"amount\":"<<d.amount.value.sats()<<",\"amountLexeme\":"<<rpc::Quote(d.amount.lexeme)<<",\"fee\":"<<MoneyJson(d.repeated_fee?std::optional<Money>(d.repeated_fee->value):std::nullopt)<<'}';}std::cout<<"]}";
  }std::cout<<']';
 }std::cout<<"}\n"<<std::flush;
}
}
int main(int argc,char** argv){if(argc!=5)return 2;try{
 ServerCandidate profile;profile.connection.port=uint16_t(std::stoi(argv[1]));profile.connection.credential.reference=argv[2];profile.expected_genesis=argv[3];profile.wallet=argv[4];rpc::CookieCredentials credentials;rpc::Transport transport(credentials);auto caps=ProbeCapabilities(transport,profile,{});if(!caps.CanApply()){std::cerr<<rpc::ErrorName(caps.error)<<'\n';return 3;}
 RequestContext context{*BackendSessionId::Make("history-runtime"),{*Hash256::Parse(profile.expected_genesis)},*WalletId::Make(argv[4]),*DocumentId::Make(std::string("history-")+argv[4]),{1},{1}};RefreshIdentity identity{context,profile,1};RefreshService service([&](const RefreshRequest& r,std::stop_token stop){return FetchRefresh(transport,r,stop);});if(!service.Bind(identity,caps,false,0))return 4;std::cout<<"{\"ready\":true}\n"<<std::flush;std::string command;
 while(std::getline(std::cin,command)){
  if(command=="run"){if(service.RequestNow(0))service.Tick(0);}
  else if(command=="empty-policy"){if(!service.SetHistoryPolicy(identity,{true,{},{}}))return 5;}
  else if(command=="offline")service.SetOffline(true,0);
  else if(command=="online")service.SetOffline(false,0);
  else if(command=="lock")service.PrivacyLock();else return 6;
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(service.Busy() && std::chrono::steady_clock::now()<end){service.Tick(0);std::this_thread::sleep_for(std::chrono::milliseconds(1));}if(service.Busy())return 7;Print(service);
 }
 return 0;
 }catch(...){std::cerr<<"history-client-exception\n";return 8;}}
