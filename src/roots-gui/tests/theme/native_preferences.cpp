// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
namespace {
struct Requests final : app::Requests {
    explicit Requests(const std::filesystem::path& file):recovery(file){}
    app::RecoverySession recovery;
    std::atomic<int> probes{};
    int external_requests{};
    ConnectionService connection{[this](const ServerCandidate&,std::stop_token){++probes;return BackendCapabilities{};}};
    app::RecoverySession* Recovery() override{return &recovery;}
    ConnectionService* Connection() override{return &connection;}
    app::Receipt OpenExplorer(const app::ExplorerRequest&) override{++external_requests;return {false,"Fixture never opens a browser"};}
    app::Receipt Submit(const app::Request&) override{return {false,"Unexpected command"};}
};

struct Observer final : platform::NativeObserver {
    Observer(std::filesystem::path out,std::filesystem::path file,bool restart)
        :requests(file),output(std::move(out)),metadata(std::move(file)),reopened(restart)
    {
        state.documents=app::PublicFixtureDocuments();
    }
    Requests requests;
    ui::shell::State state;
    ui::shell::Facts facts;
    std::filesystem::path output,metadata;
    bool reopened{},captured{},cleaned{};
    int stage{},checks{},deadline_frame{};
    Display* display{};
    int (*motion)(Display*,int,int,int,unsigned long){};
    int (*button)(Display*,unsigned int,Bool,unsigned long){};
    int (*key)(Display*,unsigned int,Bool,unsigned long){};
    std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};

    bool CaptureFramebuffer() const override{return true;}
    void Check(bool value,const char* reason)
    {
        if(!value)throw std::runtime_error(std::string("native preferences stage ")+std::to_string(stage)+": "+reason);
        ++checks;
    }
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override
    {
        if(std::chrono::steady_clock::now()-started>std::chrono::seconds(15))
            throw std::runtime_error("native preferences timeout at stage "+std::to_string(stage));
    }
    void Key(KeySym symbol,bool down){Check(key(display,XKeysymToKeycode(display,symbol),down,0),"XTest key");}
    void Release(){Check(button(display,1,False,0),"XTest release");}
    void Press(platform::NativeHost& host,const char* id)
    {
        const auto& item=facts.preferences.at(id);
        Check(item.visible && !item.disabled,"requested preference is actionable");
        int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);
        Check(motion(display,-1,x+int((item.rect[0]+item.rect[2])/2),y+int((item.rect[1]+item.rect[3])/2),0),"XTest pointer");
        Check(button(display,1,True,0),"XTest press");
    }
    void Capture(const platform::NativeFrame& frame,const char* name)
    {
        Check(frame.width==960 && frame.height==640 && frame.effective_scale==1.5F && frame.decorated && !frame.gl_error,"native viewport/backend");
        const auto packet=state.presentation.exchange.Published();
        Check(packet && packet->valid && !packet->nodes.empty(),"native dialog semantics");
        const auto text=state.presentation.Snapshot();
        Check(text.find("General preferences")!=std::string::npos && text.find("Hide amounts")!=std::string::npos,"General route and privacy control");
        Check(text.find("No fiat exchange-rate provider")!=std::string::npos,"fiat dependency");
        Check(text.find("Public watch-only wallet")==std::string::npos && text.find("0.01000000")==std::string::npos,"dialog omits wallet and amount data");
        Check(!frame.rgba.empty(),"actual OpenGL framebuffer");
        std::ofstream ppm(output/(std::string(name)+".ppm"),std::ios::binary);
        ppm<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for(int y=frame.framebuffer_height-1;y>=0;--y)for(int x=0;x<frame.framebuffer_width;++x)
            ppm.write(reinterpret_cast<const char*>(frame.rgba.data()+(size_t(y)*frame.framebuffer_width+x)*4),3);
        Check(bool(ppm),"framebuffer written");
        std::ofstream(output/(std::string(name)+"-semantics.txt"))<<text;
        captured=true;
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override
    {
        if(frame.rendered_frames<4)return;
        const auto snapshot=state.presentation.Snapshot();
        switch(stage){
        case 0:
            Check(!requests.connection.Active() && requests.probes==0 && requests.external_requests==0,"startup is network-silent");
            Check(!requests.recovery.Enabled() && !requests.recovery.Pending(),"document recovery off");
            Check(state.privacy.hidden==reopened && state.unit==int(reopened) && state.privacy.trim_decimals==reopened,"fresh process has expected preferences");
            Check((snapshot.find("0.01000000")==std::string::npos)==reopened,"persisted privacy before opening Preferences");
            Check((snapshot.find("Public watch-only wallet")==std::string::npos)==reopened,"persisted privacy covers wallet tab names");
            Key(XK_Control_L,true);Key(XK_comma,true);stage=1;break;
        case 1:Key(XK_comma,false);Key(XK_Control_L,false);stage=2;break;
        case 2:
            if(!facts.preferences_visible)break;
            Check(!facts.server.visible,"keyboard Preferences opens General");
            Check(state.general.saved && state.general.saved->preferences.explorer==ExplorerMode::Disabled,"explorer disabled by default");
            if(reopened){
                Check(facts.preferences.at("Dark").disabled,"native theme persisted in fresh process");
                auto separate=storage::MetadataStore::Open(metadata);
                Check(bool(separate),"disposable second preferences writer");
                auto newer=(*separate.value)->LoadWorkspace();
                Check(bool(newer),"second writer reads saved revisions");
                newer.value->preferences.group_address_outputs=false;
                Check(bool((*separate.value)->SaveWorkspace(*newer.value)),"second writer advances revision");
                Press(host,"BTC");stage=20;break;
            }
            Press(host,"Satoshis");stage=3;break;
        case 3:Release();stage=4;break;
        case 4:if(state.unit==1){Press(host,"decimals");stage=5;}break;
        case 5:Release();stage=6;break;
        case 6:if(state.privacy.trim_decimals){Press(host,"Dark");stage=7;}break;
        case 7:Release();stage=8;break;
        case 8:if(facts.preferences.at("Dark").disabled){Press(host,"hide-amounts");stage=9;}break;
        case 9:Release();stage=10;deadline_frame=frame.rendered_frames+8;break;
        case 10:
            if(frame.rendered_frames<deadline_frame || !state.privacy.hidden)break;
            Check(state.general.saved && state.general.saved->revision==3 && state.general.saved->preferences.hide_balances,"immediate acknowledged save");
            Capture(frame,"preferences-general");
            Press(host,"close");stage=11;break;
        case 11:Release();stage=12;break;
        case 12:
            if(facts.preferences_visible)break;
            Check(state.privacy.hidden && snapshot.find("0.01000000")==std::string::npos,"live hidden amount stays hidden on close");
            host.RequestClose();break;
        case 20:Release();stage=21;deadline_frame=frame.rendered_frames+8;break;
        case 21:
            if(frame.rendered_frames<deadline_frame)break;
            Check(state.unit==1 && state.privacy.hidden && state.general.error.find("changed in another window")!=std::string::npos,"CAS failure retains current presentation");
            Capture(frame,"preferences-general-conflict");
            host.RequestClose();break;
        }
        XSync(display,False);
    }
    void Teardown(const platform::NativeTeardown& value) override
    {
        cleaned=value.renderer_cleared && value.platform_cleared && value.textures_released
            && value.imgui_cleared && value.context_cleared && value.input_cleared;
        Check(cleaned,"native teardown");
    }
};
}

int main(int argc,char** argv)
{
    if(argc!=6)return 2;
    const std::string_view mode=argv[5];
    if(mode!="write" && mode!="read")return 2;
    Observer observer(argv[1],argv[3],mode=="read");
    std::filesystem::create_directories(observer.output);
    void* xtst{};
    int result{};
    try {
        const auto* display=std::getenv("DISPLAY");
        const auto* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if(!display || !owned || std::string_view(display)!=owned)throw std::runtime_error("owned native display required");
        observer.display=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);
        if(!observer.display || !xtst)throw std::runtime_error("native input unavailable");
        observer.motion=reinterpret_cast<decltype(observer.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));
        observer.button=reinterpret_cast<decltype(observer.button)>(dlsym(xtst,"XTestFakeButtonEvent"));
        observer.key=reinterpret_cast<decltype(observer.key)>(dlsym(xtst,"XTestFakeKeyEvent"));
        if(!observer.motion || !observer.button || !observer.key)throw std::runtime_error("native input entry points unavailable");
        platform::NativeOptions options{960,640,false,1.5F,argv[2],argv[4]};
        options.fixed_delta_seconds=1.F/60;
        platform::NativeHost* current{};
        {
            platform::NativeHost host(options,&observer,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){
                observer.facts=platform::DrawShellFrame(*current,observer.state,observer.requests,theme,appearance);
            });
            current=&host;host.Run();
        }
        observer.Check(observer.captured && observer.cleaned,"capture resources settled");
        observer.Check(observer.requests.probes==0 && observer.requests.external_requests==0 && !observer.requests.connection.Busy(),"General sends no network requests");
        std::ofstream(observer.output/"result.json")<<"{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"assertions\":"
            <<observer.checks<<",\"startupNetworkCalls\":0,\"teardown\":true,\"run\":\""<<mode<<"\"}\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';result=1;}
    if(observer.display)XCloseDisplay(observer.display);
    if(xtst)dlclose(xtst);
    return result;
}
