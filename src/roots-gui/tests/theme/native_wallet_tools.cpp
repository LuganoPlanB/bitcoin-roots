// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include "app/connection_observations.h"
#include "tests/wallet_tools_fixture.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>

using namespace roots::gui;
namespace sh=ui::shell;
namespace a=app;
namespace c=ui::components;
namespace fx=test::wallet_tools;
namespace {
struct Requests final:a::Requests {
    explicit Requests(const std::filesystem::path& file):recovery(file){}
    a::RecoverySession recovery;
    fx::Wallets wallets;
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    RefreshService refresh{[this](const auto& request,std::stop_token stop){
        return request.identity.profile.wallet?wallets.Fetch(request):FetchRefresh(transport,request,stop);
    }};
    ConnectionService connection{[this](const auto& candidate,std::stop_token stop){return ProbeCapabilities(transport,candidate,stop);}};
    ConnectionService* Connection() override{return &connection;}
    RefreshService* Refresh() override{return &refresh;}
    AddressService* Addresses() override{return &wallets.addresses;}
    a::RecoverySession* Recovery() override{return &recovery;}
    a::HostObservations Observe() const override{
        auto result=a::ConnectionObservations(connection,refresh);
        if(!connection.Active()){
            result.activity={a::ServiceState::Loading,fx::RAW_DETAIL};
            result.tasks={{1,fx::RAW_DETAIL,fx::RAW_DETAIL,a::ServiceState::Loading,425}};
            result.log={fx::RAW_DETAIL};
        }
        return result;
    }
    a::Receipt Submit(const a::Request&) override{throw std::runtime_error("unexpected mutation in native wallet tools");}
};

struct Observer final:platform::NativeObserver {
    Observer(std::filesystem::path out,const std::filesystem::path& metadata,ServerCandidate candidate)
        :requests(metadata),output(std::move(out)),profile(std::move(candidate))
    {
        requests.recovery.Start();
        requests.wallets.Seed(state,requests.refresh,requests.recovery.Gate().Epoch());
    }
    Requests requests;
    sh::State state;
    sh::Facts facts;
    std::filesystem::path output;
    ServerCandidate profile;
    Display* display{};
    int (*motion)(Display*,int,int,int,unsigned long){};
    int (*button)(Display*,unsigned int,Bool,unsigned long){};
    int (*key)(Display*,unsigned int,Bool,unsigned long){};
    int stage{},assertions{},captures{},result_frames{},capture_stage{-1};
    uint64_t capture_after{};
    bool cleaned{},done{};
    const std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();

    void Check(bool passed,const char* label){
        if(!passed)throw std::runtime_error("stage "+std::to_string(stage)+": "+label);
        ++assertions;
    }
    bool CaptureFramebuffer() const override{return true;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override{
        if(std::chrono::steady_clock::now()-start>std::chrono::seconds(18))
            throw std::runtime_error("native wallet tools timeout at stage "+std::to_string(stage));
    }
    void Key(KeySym symbol,bool down){Check(key(display,XKeysymToKeycode(display,symbol),down,0),"native key");}
    void Release(){Check(button(display,1,False,0),"native click release");}
    void Press(platform::NativeHost& host,const c::ItemFacts& item){
        Check(item.visible && !item.disabled && item.rect[0]>=0 && item.rect[1]>=0 && item.rect[2]<=960 && item.rect[3]<=640,
              "reachable native control in viewport");
        int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);
        Check(motion(display,-1,x+int((item.rect[0]+item.rect[2])/2),y+int((item.rect[1]+item.rect[3])/2),0),"native pointer");
        Check(button(display,1,True,0),"native click press");
    }
    bool Queue(const char* name,c::ax::SemanticAction action){
        const auto packet=state.presentation.exchange.Published();
        if(!packet || !packet->valid)return false;
        for(const auto& node:packet->nodes)if(node.name==name){
            return state.presentation.exchange.QueueNext(packet->generation,node.id,action).second==c::ActionResult::Queued;
        }
        return false;
    }
    bool Capturable(const platform::NativeFrame& frame){
        if(stage!=capture_stage){
            capture_stage=stage;
            // ImGui can publish a just-opened auto-fit dialog's semantics
            // while its first layout frame is still hidden from the renderer.
            capture_after=frame.rendered_frames+2;
        }
        const auto packet=state.presentation.exchange.Published();
        return frame.rendered_frames>=capture_after && packet && packet->valid && !packet->overflow;
    }
    void Capture(const platform::NativeFrame& frame,const char* name,std::initializer_list<std::string_view> expect){
        Check(frame.width==960 && frame.height==640 && frame.effective_scale==1.5F && frame.decorated && !frame.gl_error,"actual native 960x640 at 150 percent");
        const auto tree=state.presentation.Tree();
        Check(tree.Validate() && !tree.overflow && !frame.rgba.empty(),"bounded native semantics and actual framebuffer");
        const auto packet=state.presentation.exchange.Published();
        Check(packet && packet->valid && !packet->overflow,"native semantics published");
        std::string text;
        for(const auto& node:packet->nodes)text+=node.name+" | "+node.value+" | "+node.description+"\n";
        for(const auto part:expect)if(text.find(part)==std::string::npos)
            throw std::runtime_error(std::string(name)+" missing semantic text: "+std::string(part));
        Check(text.find(fx::RAW_DETAIL)==std::string::npos && text.find("9.87654321")==std::string::npos,"raw activity never published");
        std::ofstream ppm(output/(std::string(name)+".ppm"),std::ios::binary);
        ppm<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for(int y=frame.framebuffer_height-1;y>=0;--y)for(int x=0;x<frame.framebuffer_width;++x)
            ppm.write(reinterpret_cast<const char*>(frame.rgba.data()+(size_t(y)*frame.framebuffer_width+x)*4),3);
        Check(bool(ppm),"framebuffer bytes retained");
        std::ofstream(output/(std::string(name)+"-semantics.txt"))<<text;
        ++captures;
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override{
        if(frame.rendered_frames<4)return;
        switch(stage){
        case 0:
            Check(!requests.connection.Active() && !requests.connection.Busy(),"native startup does not connect");
            Key(XK_Control_L,true);Key(XK_Shift_L,true);Key(XK_Alt_L,true);Key(XK_s,true);stage=1;break;
        case 1:Key(XK_s,false);Key(XK_Alt_L,false);Key(XK_Shift_L,false);Key(XK_Control_L,false);stage=2;break;
        case 2:
            if(!facts.tools_visible)break;
            Check(state.wallet_tools.tool==sh::WalletTool::AllSearch && state.wallet_tools.show_wallet,"native shortcut opens all-wallet search");
            Check(facts.tools.at("query").focused,"search input receives native focus");
            Key(XK_a,true);stage=3;break;
        case 3:Key(XK_a,false);stage=4;break;
        case 4:Press(host,facts.tools.at("Transaction"));stage=5;break;
        case 5:Release();stage=6;break;
        case 6:Press(host,facts.tools.at("search"));stage=7;break;
        case 7:Release();stage=8;break;
        case 8:
            if(!state.wallet_tools.results)break;
            Check(state.wallet_tools.results->hits.size()==2 && state.wallet_tools.search.draft()=="a","native typed query excludes observed locked wallet");
            if(facts.tools.contains("hit-0")){
                const auto& item=facts.tools.at("hit-0");
                std::ofstream geometry(output/"search-hit-geometry.txt",std::ios::app);
                for(const auto& [name,control]:facts.tools){
                    geometry<<name<<' ';
                    for(float edge:control.rect)geometry<<edge<<' ';
                    geometry<<control.visible<<' '<<control.disabled<<'\n';
                }
                if(!item.visible && ++result_frames<4)break;
            }
            Press(host,facts.tools.at("hit-0"));stage=9;break;
        case 9:Release();stage=10;break;
        case 10:
            if(!state.wallet_tools.selected || !Capturable(frame))break;
            Capture(frame,"search-wallets",{"All open unlocked wallets","2 matches","Shared coffee","Archived wallet","Dated cache"});
            Check(state.presentation.Snapshot().find("Locked wallet canary")==std::string::npos,"locked name absent from search semantics");
            Press(host,facts.tools.at("show"));stage=11;break;
        case 11:Release();stage=12;break;
        case 12:
            if(facts.tools_visible)break;
            Check(state.documents.ActiveId()==requests.wallets.ids[0] && state.documents.Active()->history.selected==fx::Hash('a').hex(),
                "native Show activates exact wallet and transaction");
            if(Queue("View",c::ax::SemanticAction::Press))stage=13;
            break;
        case 13:
            if(facts.menu_rows.contains(a::Command::Summaries) && Queue("Summaries",c::ax::SemanticAction::Press))stage=14;
            break;
        case 14:
            if(!facts.tools_visible || !facts.menu_rows.empty() || !Capturable(frame))break;
            Capture(frame,"wallet-summary",{"Wallet balance summary","Dated cache","Partial while wallet is scanning","1.00000001 BTC","Unavailable"});
            Check(state.presentation.Snapshot().find("Locked wallet canary")==std::string::npos,"locked wallet absent from summary");
            Key(XK_Escape,true);stage=15;break;
        case 15:Key(XK_Escape,false);stage=16;break;
        case 16:
            if(facts.tools_visible)break;
            Press(host,facts.activity_button);stage=17;break;
        case 17:Release();stage=18;break;
        case 18:
            if(state.drawer!=sh::Drawer::Activity || !Capturable(frame))break;
            Capture(frame,"activity-log",{"Operational log","Background work","UTC","Export activity CSV"});
            Press(host,facts.drawer_close);stage=19;break;
        case 19:Release();stage=20;break;
        case 20:Press(host,facts.status_button);stage=21;break;
        case 21:Release();stage=22;break;
        case 22:
            if(!facts.tools.contains("node"))break;
            Press(host,facts.tools.at("node"));stage=23;break;
        case 23:Release();stage=24;break;
        case 24:
            if(!facts.tools_visible || !Capturable(frame))break;
            Capture(frame,"node-info-unavailable",{"Node information unavailable","Test and apply a direct Bitcoin RPC profile"});
            Check(!requests.connection.Active() && !requests.connection.Busy(),"opening Node information sends no request");
            Press(host,facts.tools.at("tool-close"));stage=25;break;
        case 25:Release();stage=26;break;
        case 26:
            if(facts.tools_visible)break;
            Check(requests.connection.SetDraft(profile) && requests.connection.Test(),"explicit authenticated node-only test");
            stage=27;break;
        case 27:
            if(requests.connection.Busy())break;
            Check(requests.connection.CanApply() && requests.connection.Apply(),"explicit node-only Test and Apply");
            stage=28;break;
        case 28:
            if(requests.refresh.View().freshness[0]!=Freshness::Live)break;
            Press(host,facts.status_button);stage=29;break;
        case 29:Release();stage=30;break;
        case 30:
            if(!facts.tools.contains("node"))break;
            Press(host,facts.tools.at("node"));stage=31;break;
        case 31:Release();stage=32;break;
        case 32:
            if(!facts.tools_visible || !Capturable(frame))break;
            Capture(frame,"node-info",{"Node information","Current RPC","regtest","Block height  19","320100","Block data pruned"});
            Check(requests.refresh.View().node && requests.refresh.View().node->tip==fx::Hash('b').hex(),"verified live loopback tip");
            Check(requests.wallets.fetches==3 && requests.wallets.address_fetches==3,"tools do not fetch or unlock any fixture wallet");
            done=true;host.RequestClose();break;
        }
        XSync(display,False);
    }
    void Teardown(const platform::NativeTeardown& result) override{
        cleaned=result.renderer_cleared && result.platform_cleared && result.textures_released
            && result.imgui_cleared && result.context_cleared && result.input_cleared;
        Check(cleaned,"native contexts, textures and input released");
    }
};
}

int main(int argc,char** argv)
{
    if(argc!=6)return 2;
    Display* display{};void* xtst{};
    int status{};
    try {
        const char* name=std::getenv("DISPLAY"),*owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if(!name || !owned || std::string_view(name)!=owned)throw std::runtime_error("owned X11 required");
        display=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);
        if(!display || !xtst)throw std::runtime_error("XTest unavailable");
        ServerCandidate profile;
        profile.connection.host="127.0.0.1";profile.connection.port=std::stoi(argv[4]);
        profile.connection.credential.reference=argv[5];profile.expected_genesis=fx::REGTEST;
        Observer observer(argv[1],argv[3],std::move(profile));observer.display=display;
        std::filesystem::create_directories(observer.output);
        observer.motion=reinterpret_cast<decltype(observer.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));
        observer.button=reinterpret_cast<decltype(observer.button)>(dlsym(xtst,"XTestFakeButtonEvent"));
        observer.key=reinterpret_cast<decltype(observer.key)>(dlsym(xtst,"XTestFakeKeyEvent"));
        if(!observer.motion || !observer.button || !observer.key)throw std::runtime_error("XTest entry points unavailable");
        platform::NativeOptions options{960,640,false,1.5F,argv[2]};options.fixed_delta_seconds=1.F/60;
        platform::NativeHost* current{};
        {
            platform::NativeHost host(options,&observer,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){
                observer.facts=platform::DrawShellFrame(*current,observer.state,observer.requests,theme,appearance);
            });
            current=&host;host.Run();
        }
        observer.Check(observer.done && observer.cleaned && observer.captures==5 && !observer.requests.refresh.Busy() && !observer.requests.connection.Busy(),
            "native journey and workers settled");
        std::ofstream(observer.output/"result.json")<<"{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"assertions\":"
            <<observer.assertions<<",\"captures\":"<<observer.captures<<",\"teardown\":true}\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';status=1;}
    if(display)XCloseDisplay(display);
    if(xtst)dlclose(xtst);
    return status;
}
