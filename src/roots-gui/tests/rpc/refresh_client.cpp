// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/refresh.h"
#include <iostream>
using namespace roots::gui;
int main(int argc,char** argv)
{
    if(argc!=5)return 2;
    ServerCandidate candidate;candidate.connection.port=uint16_t(std::stoi(argv[1]));candidate.connection.credential.reference=argv[2];candidate.expected_genesis=argv[3];
    if(std::string_view(argv[4])!="node")candidate.wallet=argv[4];
    RequestContext context{*BackendSessionId::Make("fixture"),{*Hash256::Parse(candidate.expected_genesis)},*WalletId::Make("fixture-wallet"),*DocumentId::Make("fixture-document"),{1},{1}};
    RefreshRequest request{{context,candidate,1},1,0,true,true,true,true,{}};
    rpc::CookieCredentials credentials;rpc::Transport transport(credentials);std::string command;
    while(std::getline(std::cin,command)) {
        if(command!="run" && command!="cancel")return 3;
        ++request.sequence;std::stop_source cancellation;
        std::jthread cancel;
        if(command=="cancel")cancel=std::jthread([&](std::stop_token){std::this_thread::sleep_for(std::chrono::milliseconds(100));cancellation.request_stop();});
        const auto reply=FetchRefresh(transport,request,cancellation.get_token());
        if(reply.history)request.cursor=reply.history->lastblock;
        std::cout<<"{\"errors\":["<<rpc::Quote(rpc::ErrorName(reply.errors[0]))<<','<<rpc::Quote(rpc::ErrorName(reply.errors[1]))<<','<<rpc::Quote(rpc::ErrorName(reply.errors[2]))
            <<"],\"node\":"<<(reply.node?"true":"false")<<",\"wallet\":"<<(reply.wallet?"true":"false")<<",\"history\":"<<(reply.history?"true":"false")
            <<",\"dated\":"<<(reply.observed_unix_ms>0?"true":"false")<<",\"trusted\":"<<(reply.wallet?reply.wallet->trusted.sats():-1)
            <<",\"pending\":"<<(reply.wallet?reply.wallet->pending.sats():-1)<<",\"immature\":"<<(reply.wallet?reply.wallet->immature.sats():-1)<<"}\n"<<std::flush;
    }
}
