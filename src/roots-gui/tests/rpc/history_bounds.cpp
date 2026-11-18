// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/history.h"
#include <iostream>
using namespace roots::gui;
int main(int argc,char** argv){if(argc!=3)return 2;try{
 ServerCandidate profile;profile.connection.port=uint16_t(std::stoi(argv[1]));profile.connection.credential.reference=argv[2];profile.expected_genesis=std::string(64,'a');profile.wallet="bounds/札幌";
 RequestContext context{*BackendSessionId::Make("bounds"),{*Hash256::Parse(profile.expected_genesis)},*WalletId::Make("wallet"),*DocumentId::Make("document"),{1},{1}};
 const auto one=rpc::Quote(std::string(64,'1')),two=rpc::Quote(std::string(64,'2'));const auto since=*rpc::Json::Parse("{\"transactions\":[{\"txid\":"+two+"},{\"txid\":"+one+"},{\"txid\":"+one+"}],\"removed\":[],\"lastblock\":"+rpc::Quote(std::string(64,'b'))+"}");HistoryBalances balances;balances.trusted=Money::Satoshis(200000000);HistoryLimits limit;limit.transactions=1;rpc::CookieCredentials credentials;rpc::Transport transport(credentials);
 auto result=FetchHistory(transport,profile,context,since,balances,std::string(64,'b'),true,false,false,false,{}, {},std::chrono::steady_clock::now()+std::chrono::seconds(5),limit);if(!result.batch || !result.batch->limited || result.batch->transactions.size()!=1)return 3;
 HistoryLedger ledger(context,*profile.wallet);if(!ledger.Apply(*result.batch,1,1700000000000LL) || ledger.Complete() || !ledger.Limited() || ledger.Rows().begin()->second.running_balance || ledger.Totals().total->sats()!=200000000)return 4;
 std::stop_source cancellation;cancellation.request_stop();auto cancelled=FetchHistory(transport,profile,context,since,balances,std::string(64,'b'),true,false,false,false,{},cancellation.get_token(),std::chrono::steady_clock::now()+std::chrono::seconds(5),limit);if(cancelled.batch || cancelled.error!=rpc::Error::Cancelled)return 5;
 limit.entries=1;auto oversized=FetchHistory(transport,profile,context,since,balances,std::string(64,'b'),true,false,false,false,{}, {},std::chrono::steady_clock::now()+std::chrono::seconds(5),limit);if(oversized.batch || oversized.error!=rpc::Error::Limit)return 7;
 limit.entries=8192;auto changed=FetchHistory(transport,profile,context,since,balances,std::string(64,'b'),true,false,false,false,{}, {},std::chrono::steady_clock::now()+std::chrono::seconds(5),limit);if(changed.batch || changed.error!=rpc::Error::Identity)return 8;
 std::cout<<"{\"passed\":true,\"checks\":5,\"rows\":1,\"limited\":true,\"running\":null,\"cancelledBeforeRpc\":true}\n";return 0;
 }catch(...){return 6;}}
