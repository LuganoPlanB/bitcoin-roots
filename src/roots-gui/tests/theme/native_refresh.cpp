// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include "app/connection_observations.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace roots::gui;
namespace {
struct Requests final : app::Requests {
    rpc::CookieCredentials credentials;rpc::Transport transport{credentials};
    RefreshService refresh{[this](const auto& r,std::stop_token s){return FetchRefresh(transport,r,s);}};
    ConnectionService connection{[this](const auto& c,std::stop_token s){return ProbeCapabilities(transport,c,s);},[this](bool b){refresh.SetOffline(b,RefreshMonotonicMs());}};
    int refreshes{};
    ConnectionService* Connection() override{return &connection;}
    RefreshService* Refresh() override{return &refresh;}
    app::HostObservations Observe() const override{return app::ConnectionObservations(connection,refresh);}
    app::Availability CanSubmit(const app::Request& r) const override{return {r.command==app::Command::Server || (r.command==app::Command::Refresh && connection.Active() && !connection.Offline()),"Fixture service unavailable"};}
    app::Receipt Submit(const app::Request& r) override {if(r.command==app::Command::Refresh){++refreshes;return {refresh.RequestNow(RefreshMonotonicMs()),{}};}return {false,"Unavailable"};}
};
struct Observer final : platform::NativeObserver {
    Requests requests;ui::shell::State state;ui::shell::Facts facts;std::filesystem::path output;
    Display* display{};int(*motion)(Display*,int,int,int,unsigned long){};int(*button)(Display*,unsigned int,Bool,unsigned long){};int(*key)(Display*,unsigned int,Bool,unsigned long){};
    int stage{},tick{},assertions{};ImGuiID offline_id{};bool captured{},cleaned{};std::optional<int64_t> original_date;
    std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
    bool CaptureFramebuffer() const override{return true;}
    void Check(bool ok){if(!ok)throw std::runtime_error("native refresh contract "+std::to_string(assertions+1));++assertions;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override{if(std::chrono::steady_clock::now()-started>std::chrono::seconds(15))throw std::runtime_error("native refresh deadline stage "+std::to_string(stage));}
    void Key(KeySym symbol,bool down){Check(key(display,XKeysymToKeycode(display,symbol),down,0));}
    void Point(platform::NativeHost& host,const ui::components::ItemFacts& item){Check(item.visible);int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);Check(motion(display,-1,x+int((item.rect[0]+item.rect[2])/2),y+int((item.rect[1]+item.rect[3])/2),0));}
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override{
        if(frame.rendered_frames<3)return;const auto view=requests.refresh.View();
        switch(stage){
        case 0:if(view.node && view.freshness[0]==Freshness::Live){Check(view.node->height==7 && view.wallet && view.wallet->trusted.sats()==1);original_date=view.observed[0];Key(XK_Control_L,true);Key(XK_r,true);stage=1;}break;
        case 1:Key(XK_r,false);Key(XK_Control_L,false);stage=2;break;
        case 2:if(view.freshness[0]==Freshness::Stale && !view.busy){Check(requests.refreshes==1 && view.node && view.node->height==7 && view.observed[0]==original_date);Check(view.errors[0]==rpc::Error::Http);Point(host,facts.status_button);Check(button(display,1,True,0));stage=3;}break;
        case 3:Check(button(display,1,False,0));stage=4;break;
        case 4:if(facts.drawer_visible && facts.document_actions.contains("offline")){offline_id=facts.document_actions.at("offline").id;Point(host,facts.document_actions.at("offline"));Check(button(display,1,True,0));stage=5;}break;
        case 5:Check(button(display,1,False,0));tick=frame.rendered_frames;stage=6;break;
        case 6:
            if(!requests.connection.Offline() || frame.rendered_frames<tick+20)break;
            Check(view.offline && view.freshness[0]==Freshness::Offline && view.node && view.node->height==7 && view.observed[0]==original_date);
            Check(!view.busy && !requests.refresh.RequestNow(RefreshMonotonicMs()) && !requests.connection.Test() && !requests.connection.CanApply());
            Check(state.observations.connection.state==app::ServiceState::Offline && state.observations.last_success==ObservationDate(original_date));
            Check(offline_id && facts.document_actions.at("offline").id==offline_id);
            Check(frame.width==960 && frame.height==640 && frame.effective_scale==1.5F && frame.decorated && !frame.gl_error);
            const auto* wallet=state.documents.Active();
            const bool correct_wallet=state.documents.Documents().size()==1 && wallet && wallet->binding
                && wallet->binding->DaemonWallet()=="fixture-public";
            if(!correct_wallet || !state.presentation.Tree().Validate()){
                std::ofstream diagnostic(output/"offline-tree-diagnostic.txt");
                diagnostic<<"documents="<<state.documents.Documents().size()<<" treeValid="
                          <<state.presentation.Tree().Validate()<<" overflow="<<state.presentation.Tree().overflow
                          <<'\n'<<state.presentation.Snapshot();
            }
            Check(correct_wallet && state.presentation.Tree().Validate());
            {
                const auto snapshot=state.presentation.Snapshot();Check(snapshot.find("Offline")!=std::string::npos && snapshot.find(ObservationDate(original_date))!=std::string::npos);
                std::ofstream(output/"connection-offline-stale-semantics.txt")<<snapshot;
                std::ofstream ppm(output/"connection-offline-stale.ppm",std::ios::binary);ppm<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
                for(int y=frame.framebuffer_height-1;y>=0;--y)for(int x=0;x<frame.framebuffer_width;++x)ppm.write(reinterpret_cast<const char*>(frame.rgba.data()+(size_t(y)*frame.framebuffer_width+x)*4),3);
                Check(bool(ppm));
            }
            captured=true;host.RequestClose();stage=7;break;
        }
        XSync(display,False);
    }
    void Teardown(const platform::NativeTeardown& v) override{cleaned=v.renderer_cleared && v.platform_cleared && v.textures_released && v.imgui_cleared && v.context_cleared;Check(cleaned);}
};
}
int main(int argc,char** argv){
    if(argc!=6)return 2;Observer observer;observer.output=argv[1];std::filesystem::create_directories(observer.output);void* xtst{};
    try{
        const char* display=std::getenv("DISPLAY"),*owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");if(!display || !owned || std::string(display)!=owned)throw std::runtime_error("owned display required");
        observer.display=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);if(!observer.display || !xtst)throw std::runtime_error("native input unavailable");
        observer.motion=reinterpret_cast<decltype(observer.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));observer.button=reinterpret_cast<decltype(observer.button)>(dlsym(xtst,"XTestFakeButtonEvent"));observer.key=reinterpret_cast<decltype(observer.key)>(dlsym(xtst,"XTestFakeKeyEvent"));if(!observer.motion || !observer.button || !observer.key)throw std::runtime_error("native input unavailable");
        ServerCandidate candidate;candidate.connection.port=uint16_t(std::stoi(argv[3]));candidate.connection.credential.reference=argv[4];candidate.expected_genesis=argv[5];candidate.wallet="fixture-public";
        auto& service=observer.requests.connection;observer.Check(service.SetDraft(candidate) && service.Test());
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(service.Busy() && std::chrono::steady_clock::now()<deadline){service.Poll();std::this_thread::yield();}observer.Check(service.Apply());
        platform::NativeOptions options{960,640,false,1.5F,argv[2]};options.fixed_delta_seconds=1.F/60;
        platform::NativeHost* current{};{platform::NativeHost host(options,&observer,[&](const imkit::Theme& t,const ui::BootstrapAppearance& a){observer.facts=platform::DrawShellFrame(*current,observer.state,observer.requests,t,a);});current=&host;host.Run();}
        observer.Check(observer.captured && observer.cleaned && !service.Busy() && !observer.requests.refresh.Busy());
        std::ofstream(observer.output/"result.json")<<"{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"assertions\":"<<observer.assertions<<",\"cachedDateRetained\":true,\"offline\":true,\"teardown\":true}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(observer.display)XCloseDisplay(observer.display);if(xtst)dlclose(xtst);return 1;}
    if(observer.display)XCloseDisplay(observer.display);if(xtst)dlclose(xtst);return 0;
}
