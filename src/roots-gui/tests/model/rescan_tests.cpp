// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/rescan.h"
#include "adapters/storage/protected_file.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
using namespace std::chrono_literals;

namespace {
constexpr const char* GENESIS="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
int checks{};
void Check(bool condition,const char* why)
{
    if (!condition) throw std::runtime_error(why);
    ++checks;
}
BackendCapabilities Capabilities(const ServerCandidate& profile)
{
    BackendCapabilities caps;
    caps.authenticated=caps.identity_verified=true;
    caps.candidate=profile;
    caps.genesis=profile.expected_genesis;
    caps.chain="regtest";
    caps.wallet=WalletAvailability::Available;
    caps.descriptors=true;
    for (const auto* call:{"getwalletinfo","getblockchaininfo","getblockhash"})
        caps.methods.emplace(call,MethodSupport{true,true,std::string_view(call)=="getwalletinfo",false,{}});
    caps.methods.emplace("rescanblockchain",MethodSupport{true,true,true,false,{"start_height","stop_height"}});
    return caps;
}
void Bounds()
{
    ScanObservation pruned{14,12,4,true,false,false,false};
    Check(ValidateScanRange(pruned,{4,12}).empty(),"inclusive first retained block");
    Check(ValidateScanRange(pruned,{3,12}).find("pruned")!=std::string::npos,"pruned history rejected");
    Check(!ValidateScanRange(pruned,{5,13}).empty(),"beyond processed wallet tip");
    Check(!ValidateScanRange(pruned,{13,12}).empty(),"inverted range");
    Check(!ValidateScanRange(pruned,{-1,2}).empty(),"negative range");
    Check(!ValidateScanRange(pruned,{0,1LL<<31}).empty(),"RPC int limit");
    pruned.scanning=true;
    Check(ValidateScanRange(pruned,{4,12}).find("already")!=std::string::npos,"concurrent daemon scan");
    pruned.scanning=false;
    pruned.wallet_locked=true;
    Check(ValidateScanRange(pruned,{4,12}).find("daemon owner")!=std::string::npos,"wallet lock is daemon-owned");
    pruned.wallet_locked=false;
    pruned.initial_download=true;
    Check(ValidateScanRange(pruned,{4,12}).find("download")!=std::string::npos,"sync gate");
    const auto end=[](std::string_view response){return ReportedScanEnd(*rpc::Json::Parse(response),{5,7});};
    Check(end(R"({"start_height":5,"stop_height":7})")==7,"full acknowledgment");
    Check(end(R"({"start_height":5,"stop_height":6})")==6 && end(R"({"start_height":5,"stop_height":6})")!=7,"partial receipt");
    Check(!end(R"({"start_height":5,"stop_height":null})"),"unknown receipt");
    Check(!end(R"({"start_height":4,"stop_height":7})"),"wrong starting height");
    Check(!end(R"({"start_height":5,"stop_height":8})"),"outside reviewed endpoint");
    Check(!end(R"({"start_height":5,"stop_height":"7"})"),"coerced integer forbidden");
}
void Fences()
{
    ServerCandidate profile{{},GENESIS,std::string("")};
    profile.connection.credential.reference="/tmp/roots-gui-fixture-cookie";
    auto caps=Capabilities(profile);
    Check(CanStartScan(caps),"empty daemon wallet identity supported");
    caps.methods.at("rescanblockchain").arguments.erase("stop_height");
    Check(CanObserveScan(caps) && !CanStartScan(caps),"both named arguments required");
    caps.wallet=WalletAvailability::Unavailable;
    Check(!CanObserveScan(caps),"unloaded wallet");
    ConnectionService connection([](const ServerCandidate& candidate,std::stop_token){return Capabilities(candidate);});
    Check(connection.SetDraft(profile) && connection.Test(),"start capability observation");
    const auto deadline=std::chrono::steady_clock::now()+3s;
    while (connection.Busy() && std::chrono::steady_clock::now()<deadline) {
        connection.Poll();
        std::this_thread::sleep_for(1ms);
    }
    Check(connection.Apply(),"explicitly apply exact profile");
    RefreshIdentity identity{{*BackendSessionId::Make("connection-1"),{*Hash256::Parse(GENESIS)},
        *WalletId::Make("default"),*DocumentId::Make("wallet-document-3"),{1},{2}},profile,connection.Session()};
    ScanRequest request{ScanAction::Check,identity,connection.Revision()};
    std::atomic<unsigned> calls{};
    std::atomic<bool> release{},saw_cancel{};
    RescanService service([&](const ScanRequest&,std::stop_token stop) {
        ++calls;
        while (!release && !stop.stop_requested()) std::this_thread::sleep_for(1ms);
        saw_cancel=stop.stop_requested();
        return ScanReply{ScanObservation{}};
    });
    const auto wait=[&](const auto& current) {
        const auto deadline=std::chrono::steady_clock::now()+3s;
        std::optional<RescanService::Completion> response;
        while (service.Busy() && std::chrono::steady_clock::now()<deadline) {
            response=service.Poll(&connection,current);
            std::this_thread::sleep_for(1ms);
        }
        Check(!service.Busy(),"worker settled");
        return response;
    };
    auto mutation=request;
    mutation.action=ScanAction::Start;
    Check(!service.Start(mutation,connection),"mutation requires durable admission");
    Check(service.Start(request,connection) && !service.Start(request,connection),"only one in flight");
    auto changed=identity;
    changed.context.document=*DocumentId::Make("wallet-document-8");
    Check(!wait(changed) && saw_cancel && calls==1,"late wallet-tab completion retired without retry");
    Check(service.Start(request,connection),"explicit read after retirement");
    service.Cancel();
    Check(!wait(identity) && saw_cancel && calls==2,"local cancellation retires late result");
    Check(service.Start(request,connection),"fresh explicit wallet check");
    release=true;
    const auto result=wait(identity);
    Check(result && result->request.identity==identity && result->reply.observation && calls==3,"only exact result adopted");
    connection.SetOffline(true);
    Check(!service.Start(request,connection) && calls==3,"offline issues no request");
    connection.SetOffline(false);
    connection.SetPrivacyLocked(true);
    Check(!service.Start(request,connection) && calls==3,"privacy closure issues no request");
}
}

int main()
{
    try {
        Bounds();
        Fences();
        std::cout<<"{\"passed\":true,\"checks\":"<<checks<<",\"fixtureOnly\":true}\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
