// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/history_workspace.h"
#include "platform/shell_composition.h"
#include "tests/message_fixture.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>

using namespace roots::gui;
namespace a=app;
namespace p=platform;
namespace sh=ui::shell;
namespace c=ui::components;
namespace fixture=test::message_fixture;
namespace {
struct Requests final:a::Requests {
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    ConnectionService connection{[this](const auto& candidate,std::stop_token stop){return ProbeCapabilities(transport,candidate,stop);}};
    a::MessageSigning messages{transport,connection};
    ConnectionService* Connection() override{return &connection;}
    a::MessageSigning* Messages() override{return &messages;}
    a::Receipt Submit(const a::Request&) override{throw std::runtime_error("unexpected native message host request");}
};
struct Observer final:p::NativeObserver {
    explicit Observer(std::filesystem::path folder):output(std::move(folder)){}
    Requests requests;
    sh::State state;
    sh::Facts facts;
    std::filesystem::path output;
    Display* display{};
    int(*motion)(Display*,int,int,int,unsigned long){};
    int(*button)(Display*,unsigned int,Bool,unsigned long){};
    int(*key)(Display*,unsigned int,Bool,unsigned long){};
    int stage{},checks{},captures{},stable_stage{-1},capture_at{};
    bool done{},cleaned{};
    const std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    void Check(bool ok,const char* reason){
        if(!ok)throw std::runtime_error("message stage "+std::to_string(stage)+": "+reason);
        ++checks;
    }
    bool CaptureFramebuffer()const override{return true;}
    void Idle(p::NativeHost&,const p::NativeFrame&) override{
        if(std::chrono::steady_clock::now()-start>std::chrono::seconds(15)){
            std::ofstream(output/"native-timeout.txt")<<"stage "<<stage<<'\n'<<state.presentation.Snapshot();
            throw std::runtime_error("native message deadline");
        }
    }
    void Key(KeySym sym,bool down){
        Check(key(display,XKeysymToKeycode(display,sym),down,0),"XTest key");XSync(display,False);
    }
    void Press(p::NativeHost& host,const char* id){
        const auto& item=facts.message.at(id);
        Check(item.visible && !item.disabled && item.rect[0]>=0 && item.rect[1]>=0 && item.rect[2]<=960 && item.rect[3]<=640,"reachable native message action");
        int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);
        Check(motion(display,-1,x+int((item.rect[0]+item.rect[2])/2),y+int((item.rect[1]+item.rect[3])/2),0),"XTest pointer");
        Check(button(display,1,True,0),"XTest press");XSync(display,False);
    }
    void Release(){Check(button(display,1,False,0),"XTest release");XSync(display,False);}
    bool Set(const char* field,std::string_view value){
        const auto packet=state.presentation.exchange.Published();
        if(!packet || !packet->valid)return false;
        for(const auto& node:packet->nodes)if(node.name==field)
            return state.presentation.exchange.QueueNext(packet->generation,node.id,c::ax::SemanticAction::SetValue,std::string(value)).second==c::ActionResult::Queued;
        return false;
    }
    bool Settled(const p::NativeFrame& frame){
        if(stable_stage!=stage){stable_stage=stage;capture_at=frame.rendered_frames+2;}
        const auto packet=state.presentation.exchange.Published();
        return frame.rendered_frames>=capture_at && packet && packet->valid && !packet->overflow;
    }
    void Capture(const p::NativeFrame& frame,const char* name,std::initializer_list<std::string_view> required){
        Check(frame.width==960 && frame.height==640 && frame.effective_scale==1.5F && frame.decorated && !frame.gl_error,"actual native geometry");
        const auto packet=state.presentation.exchange.Published();
        Check(packet && packet->valid && !packet->overflow && !frame.rgba.empty(),"bounded actual frame and semantic packet");
        std::string semantics;
        for(const auto& node:packet->nodes)semantics+=node.name+" | "+node.value+" | "+node.description+'\n';
        for(auto item:required)if(semantics.find(item)==std::string::npos)throw std::runtime_error(std::string(name)+" missing "+std::string(item));
        Check(semantics.find(fixture::ADDRESS)==std::string::npos && semantics.find(fixture::SIGNATURE)==std::string::npos
            && semantics.find("Café")==std::string::npos,"semantic publication excludes transient message payloads");
        std::ofstream ppm(output/(std::string(name)+".ppm"),std::ios::binary);
        ppm<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for(int y=frame.framebuffer_height-1;y>=0;--y)for(int x=0;x<frame.framebuffer_width;++x)
            ppm.write(reinterpret_cast<const char*>(frame.rgba.data()+(size_t(y)*frame.framebuffer_width+x)*4),3);
        Check(bool(ppm),"native framebuffer retained");
        std::ofstream(output/(std::string(name)+"-semantics.txt"))<<semantics;
        ++captures;
    }
    void Frame(p::NativeHost& host,const p::NativeFrame& frame) override{
        if(frame.rendered_frames<4)return;
        switch(stage){
        case 0:{
            if(requests.connection.Busy())break;
            Check(requests.connection.CanApply() && requests.connection.Apply(),"applied authenticated fixture profile");
            const auto binding=a::VerifiedWalletBinding(*requests.connection.Active());
            Check(binding.has_value(),"verified wallet binding");
            Check(state.documents.OpenWallet("Message fixture wallet",*binding,true)>0,"open exact fixture document");
            stage=1;break;
        }
        case 1:Key(XK_Control_L,true);Key(XK_m,true);stage=2;break;
        case 2:Key(XK_m,false);Key(XK_Control_L,false);stage=3;break;
        case 3:
            if(!facts.message_visible || !Settled(frame))break;
            Check(facts.message.at("address").focused,"keyboard shortcut focuses the address editor");
            Check(state.message.network==protocol::MessageNetwork::TestFamily && facts.message.at("sign-qr").disabled,"verified network default and unavailable QR contract");
            if(Set("Address",fixture::ADDRESS))stage=4;
            break;
        case 4:
            if(std::string_view(state.message.address.data())==fixture::ADDRESS && Set("Message",fixture::MESSAGE))stage=5;
            break;
        case 5:
            if(std::string_view(state.message.text.data())!=fixture::MESSAGE || !Settled(frame))break;
            Press(host,"sign");stage=6;break;
        case 6:Release();stage=7;break;
        case 7:
            if(!state.message.status_ok || !Settled(frame))break;
            Check(std::string_view(state.message.signature.data())==fixture::SIGNATURE && !requests.messages.Busy(),"exact independently verified daemon reply");
            Capture(frame,"sign-verify-message",{"Sign / Verify Message","Legacy Bitcoin Signed Message","Signed and verified locally","Sign by QR requires a message-signing QR protocol"});
            stage=8;break;
        case 8:if(Set("Message","changed exact message"))stage=9;break;
        case 9:
            if(std::string_view(state.message.text.data())!="changed exact message" || !Settled(frame))break;
            Check(!state.message.status_ok,"edited message retires previous result");
            Press(host,"verify");stage=10;break;
        case 10:Release();stage=11;break;
        case 11:
            if(state.message.status.empty() || !Settled(frame))break;
            Check(!state.message.status_ok,"local verifier rejects the changed text");
            Capture(frame,"sign-verify-message-mismatch",{"The signature does not match this address and exact message.","Base64 signature"});
            Press(host,"bip322");stage=12;break;
        case 12:Release();stage=13;break;
        case 13:
            if(state.message.format!=protocol::MessageFormat::Bip322 || !Settled(frame))break;
            Check(facts.message.at("sign").disabled && facts.message.at("sign-qr").disabled && facts.message.at("verify").disabled,
                "no unsupported message action");
            Capture(frame,"sign-verify-message-unsupported",{"BIP322 requires a dedicated message proof implementation","Sign by QR requires a message-signing QR protocol"});
            Press(host,"done");stage=14;break;
        case 14:Release();stage=15;break;
        case 15:
            if(facts.message_visible)break;
            Check(state.message.address.front()==0 && state.message.text.front()==0 && state.message.signature.front()==0
                && state.documents.Documents().size()==1 && !requests.messages.Busy(),"Done clears transient message state and preserves wallet document");
            done=true;host.RequestClose();break;
        }
    }
    void Teardown(const p::NativeTeardown& state) override{
        cleaned=state.renderer_cleared && state.platform_cleared && state.textures_released && state.imgui_cleared
            && state.context_cleared && state.input_cleared;
        Check(cleaned,"native resources settled");
    }
};
}
int main(int argc,char** argv)
{
    if(argc!=5)return 2;
    Display* display{};void* xtst{};
    try{
        const char* name=std::getenv("DISPLAY"),*owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if(!name || !owned || std::string_view(name)!=owned)throw std::runtime_error("owned X11 display required");
        display=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);
        if(!display || !xtst)throw std::runtime_error("XTest input unavailable");
        Observer observer(argv[1]);observer.display=display;
        observer.motion=reinterpret_cast<decltype(observer.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));
        observer.button=reinterpret_cast<decltype(observer.button)>(dlsym(xtst,"XTestFakeButtonEvent"));
        observer.key=reinterpret_cast<decltype(observer.key)>(dlsym(xtst,"XTestFakeKeyEvent"));
        if(!observer.motion || !observer.button || !observer.key)throw std::runtime_error("XTest entry points unavailable");
        std::filesystem::create_directories(observer.output);
        ServerCandidate candidate;
        candidate.connection.port=uint16_t(std::stoi(argv[3]));candidate.connection.credential.reference=argv[4];
        candidate.wallet=fixture::WALLET;candidate.expected_genesis=fixture::GENESIS;
        observer.Check(observer.requests.connection.SetDraft(candidate) && observer.requests.connection.Test(),"explicit controlled loopback Test");
        p::NativeOptions options{960,640,false,1.5F,argv[2]};
        options.fixed_delta_seconds=1.F/60;
        p::NativeHost* current{};
        {
            p::NativeHost host(options,&observer,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){
                observer.facts=p::DrawShellFrame(*current,observer.state,observer.requests,theme,appearance);
            });
            current=&host;host.Run();
        }
        observer.Check(observer.done && observer.cleaned && observer.captures==3 && !observer.requests.messages.Busy(),"journey and workers settled");
        std::ofstream(observer.output/"result.json")<<"{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"assertions\":"
            <<observer.checks<<",\"captures\":3,\"teardown\":true}\n";
        XCloseDisplay(display);dlclose(xtst);
        return 0;
    }catch(const std::exception& error){
        std::cerr<<error.what()<<'\n';if(display)XCloseDisplay(display);if(xtst)dlclose(xtst);return 1;
    }
}
