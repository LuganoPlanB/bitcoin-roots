// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/desktop.h"
#include "platform/restart.h"
#include "platform/shell_composition.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

using namespace roots::gui;
namespace p=platform;
namespace a=app;
namespace sh=ui::shell;
namespace {
unsigned assertions{};
void Check(bool condition,const char* message)
{
    if(!condition)throw std::runtime_error(message);
    ++assertions;
}
template<class Update,class Done>
void Await(Update update,Done done)
{
    using namespace std::chrono_literals;
    for(int i=0;i<1500;++i){
        update();
        if(done())return;
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error("owned desktop operation failed to settle");
}
void Help()
{
    for(auto target:{a::HelpTarget::Documentation,a::HelpTarget::Support,a::HelpTarget::Issues,a::HelpTarget::Releases,a::HelpTarget::Source}){
        const auto url=a::HelpUrl(target);
        Check(url.starts_with("https://") && a::ConfirmedHelpUrl(target,url),"HTTPS allowlist");
        Check(!a::ConfirmedHelpUrl(target,std::string(url)+"?endpoint=fixture"),"query injection rejected");
        Check(!a::ConfirmedHelpUrl(target,std::string(url)+"#unsafe"),"fragment rejected");
        Check(!a::ConfirmedHelpUrl(target,"file:///tmp/example"),"unsafe schemes rejected");
    }
    Check(!a::ConfirmedHelpUrl(a::HelpTarget(200),""),"unknown target rejected");
    int opens{};
    p::DesktopRuntime runtime({{},{},[&opens](auto,std::string_view){++opens;return false;}});
    Check(!runtime.Open(a::HelpTarget::Documentation,"https://invalid.example").accepted && opens==0,"invalid input cannot invoke opener");
    Check(!runtime.Open(a::HelpTarget::Documentation,a::HelpUrl(a::HelpTarget::Documentation)).accepted && opens==1,"native opener failure acknowledged once");
}
void Sleep()
{
    std::atomic<int> acquired{-1},calls{};
    p::DesktopRuntime runtime({[&](std::stop_token){
        ++calls;
        const int fd=open("/dev/null",O_RDONLY|O_CLOEXEC);
        acquired=fd;
        return p::SleepLease(fd);
    }});
    runtime.CameraGeneration(0);
    Check(calls==0,"idle performs no inhibition request");
    runtime.CameraGeneration(19);
    Await([&]{runtime.Poll();},[&]{return !runtime.Busy();});
    Check(calls==1 && runtime.Holding() && fcntl(acquired,F_GETFD)>=0,"live operation owns one acquired lease");
    runtime.CameraGeneration(19);
    Check(calls==1,"stable generation never duplicates the request");
    runtime.CameraGeneration(0);
    Check(!runtime.Holding() && fcntl(acquired,F_GETFD)==-1,"completion or cancel releases the descriptor");
    int denied{};
    p::DesktopRuntime failing({[&](std::stop_token){++denied;return p::SleepLease();}});
    failing.CameraGeneration(31);
    Await([&]{failing.Poll();},[&]{return !failing.Busy();});
    Check(!failing.Holding() && failing.Notice().find("denied")!=std::string::npos,"failure is acknowledged");
    failing.CameraGeneration(31);Check(denied==1,"denial never retries on the same generation");
    std::atomic<bool> grant{};
    std::atomic<int> late{-1};
    p::DesktopRuntime delayed({[&](std::stop_token){
        grant.wait(false);
        const int fd=open("/dev/null",O_RDONLY|O_CLOEXEC);late=fd;return p::SleepLease(fd);
    }});
    delayed.CameraGeneration(41);
    delayed.Suspend();
    grant=true;grant.notify_all();
    Await([&]{delayed.Poll();},[&]{return !delayed.Busy();});
    Check(late>=0 && fcntl(late,F_GETFD)==-1 && !delayed.Holding(),"privacy retirement discards a late grant");
    int during_teardown=-1;
    {
        p::DesktopRuntime active({[&](std::stop_token){
            during_teardown=open("/dev/null",O_RDONLY|O_CLOEXEC);return p::SleepLease(during_teardown);
        }});
        active.CameraGeneration(51);
        Await([&]{active.Poll();},[&]{return !active.Busy();});
        Check(active.Holding(),"teardown fixture holds lease");
    }
    Check(during_teardown>=0 && fcntl(during_teardown,F_GETFD)==-1,"teardown releases lease");
    std::atomic<int> delivered{};
    p::DesktopRuntime alerts({{},[&](p::DesktopNotification kind,std::stop_token){
        if(kind==p::DesktopNotification::CameraReady)++delivered;
        return true;
    }});
    alerts.CameraFeedbackScope(17);
    alerts.CameraReady(16);Check(!alerts.Busy(),"stale operation cannot enqueue notification");
    alerts.CameraReady(17);alerts.CameraReady(17);
    Await([&]{alerts.Poll();},[&]{return !alerts.Busy();});
    Check(delivered==1,"one typed generic notification per current capture");
    std::atomic<bool> notification_entered{},notification_release{},notification_retired{};
    p::DesktopRuntime late_alert({{},[&](p::DesktopNotification,std::stop_token stop){
        notification_entered=true;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!notification_release && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        notification_retired=notification_release && stop.stop_requested();
        return true;
    }});
    late_alert.CameraFeedbackScope(21);
    late_alert.CameraReady(21);
    Await([]{},[&]{return notification_entered.load();});
    late_alert.Suspend();
    notification_release=true;
    Await([&]{late_alert.Poll();},[&]{return !late_alert.Busy();});
    Check(notification_retired && late_alert.Notice().empty(),"retired notification settles without publishing its late acknowledgment");
    std::cout<<"desktop notification completion retired after suspend\n";
}
struct Requests final:a::Requests {
    int probes{};
    ConnectionService connection{[this](const auto&,std::stop_token stop){
        ++probes;
        while(!stop.stop_requested())std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return BackendCapabilities{};
    }};
    ConnectionService* Connection() override{return &connection;}
    a::Receipt Submit(const a::Request&) override{return {false,"no effects in this test"};}
};
void Restart()
{
    ServerCandidate candidate;
    candidate.expected_genesis=std::string(64,'a');
    candidate.wallet="controlled public wallet";
    candidate.connection={"localhost",18443,true,"/owned/real-ca.pem",{Authentication::Cookie,"/owned/profile/regtest/.cookie"},
        ProxyKind::Socks5RemoteDns,"127.0.0.1",19050};
    p::RestartContext context{candidate,true};
    const auto encoded=p::EncodeRestartContext(context);
    Check(encoded && p::DecodeRestartContext(*encoded)==context,"exact genesis, TLS, proxy, directory and cookie reference round-trip");
    Check(p::DecodeRestartContext(*p::EncodeRestartContext({}))==p::RestartContext{},"empty launch context round-trip");
    auto malformed=*encoded;malformed.insert(malformed.size()-1,",\"password\":\"fixture\"");
    Check(!p::DecodeRestartContext(malformed),"unrecognized secret field rejected");
    malformed=*encoded;malformed.replace(malformed.find("/owned/profile"),14,"relative");
    Check(!p::DecodeRestartContext(malformed),"relative cookie references rejected");
    Check(!p::DecodeRestartContext(std::string(16385,'a')),"oversized restart payload rejected");
    Check(!p::ConsumeRestartContext("--resume-gui-fd=1"),"ordinary descriptor rejected");
    sh::State state;
    Requests requests;
    Check(p::GuiRestartAvailability(state,requests).available,"empty GUI can restart through configured Linux adapter");
    Check(p::AdoptGuiRestart(state,requests,context) && requests.connection.Draft()==candidate && requests.connection.Offline(),"resume prefills exact draft and offline status");
    Check(!requests.connection.Active() && !requests.connection.Tested() && requests.probes==0,"resume grants no test or applied authority");
    requests.connection.SetOffline(false);Check(requests.connection.Test(),"controlled test worker starts");
    Check(!p::GuiRestartAvailability(state,requests).available,"in-flight GUI work blocks restart");
    requests.connection.Cancel();
    Await([&]{requests.connection.Poll();},[&]{return !requests.connection.Busy();});
    state.documents.OpenTransaction("Public view");
    Check(!p::GuiRestartAvailability(state,requests).available,"opted-out documents block restart");
}
}
int main()
{
    try{
        Help();Sleep();Restart();
        std::printf("{\"selected\":3,\"executed\":3,\"passed\":3,\"failed\":0,\"skipped\":0,\"assertions\":%u}\n",assertions);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
