// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <unistd.h>

using namespace roots::gui;
namespace p=platform;
namespace a=app;
namespace sh=ui::shell;
namespace c=ui::components;
namespace {
constexpr const char* GENESIS="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
struct Requests final:a::Requests {
    explicit Requests(const std::filesystem::path& metadata):recovery(metadata){}
    p::NativeHost* host{};
    sh::State* state{};
    a::RecoverySession recovery;
    std::atomic<unsigned> probes{};
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    ConnectionService connection{[this](const auto& candidate,std::stop_token stop){
        ++probes;return ProbeCapabilities(transport,candidate,stop);
    }};
    std::optional<p::RestartContext> restart;
    ConnectionService* Connection() override{return &connection;}
    a::RecoverySession* Recovery() override{return &recovery;}
    a::Availability CanSubmit(const a::Request& request) const override{
        if(request.command!=a::Command::Restart)return {false,"fixture permits GUI restart only"};
        return p::GuiRestartAvailability(*state,*const_cast<Requests*>(this));
    }
    a::Receipt Submit(const a::Request& request) override{
        if(request.command!=a::Command::Restart)throw std::runtime_error("unexpected GUI host request");
        std::string error;
        restart=p::PrepareGuiRestart(*state,*this,error);
        if(!restart)return {false,error};
        host->RequestClose();return {true,"Owned GUI restart requested"};
    }
};
struct Observer final:p::NativeObserver {
    Observer(std::filesystem::path out,const std::filesystem::path& metadata,int port,std::optional<p::RestartContext> context)
        :requests(metadata),output(std::move(out)),metadata(metadata),port(port),resumed(std::move(context))
    {
        requests.state=&state;
        Check(requests.recovery.Start(),"fixture metadata opens");
        if(resumed)return;
        const auto id=state.documents.OpenTransaction("Owned public transaction view");
        state.documents.Find(id)->transaction.note=c::Edit("Exact saved GUI note");
        Check(requests.recovery.Enable(state.documents),"explicit public workspace recovery opt-in");
        ServerCandidate candidate;
        candidate.connection.port=uint16_t(port);
        candidate.connection.credential.reference=(metadata.parent_path()/"rpc-cookie").string();
        candidate.expected_genesis=GENESIS;
        Check(requests.connection.SetDraft(candidate) && requests.connection.Test(),"explicit authenticated loopback fixture probe");
    }
    Requests requests;
    sh::State state;
    sh::Facts facts;
    std::filesystem::path output,metadata;
    int port;
    std::optional<p::RestartContext> resumed;
    std::atomic<unsigned> notifications{};
    unsigned opens{};
    int stage{},assertions{},captures{},stable_stage{-1},capture_at{};
    bool iconified{},cleaned{},done{},resumed_applied{};
    Display* display{};
    int(*motion)(Display*,int,int,int,unsigned long){};
    int(*button)(Display*,unsigned int,Bool,unsigned long){};
    int(*key)(Display*,unsigned int,Bool,unsigned long){};
    const std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();

    void Check(bool ok,const char* reason){
        if(!ok)throw std::runtime_error("desktop stage "+std::to_string(stage)+": "+reason);
        ++assertions;
    }
    bool CaptureFramebuffer() const override{return true;}
    void Idle(p::NativeHost& host,const p::NativeFrame& frame) override{
        if(std::chrono::steady_clock::now()-start>std::chrono::seconds(17)){
            std::ofstream debug(output/"native-timeout.txt");
            debug<<"stage "<<stage<<" window focus "<<glfwGetWindowAttrib(host.Window(),GLFW_FOCUSED)
                <<" desktop "<<facts.desktop_visible<<" menu rows "<<facts.menu_rows.size()<<'\n'<<state.presentation.Snapshot();
            throw std::runtime_error("desktop journey deadline at stage "+std::to_string(stage));
        }
        if(stage==13 && frame.iconified){
            iconified=true;glfwRestoreWindow(host.Window());glfwFocusWindow(host.Window());stage=14;
        }
    }
    bool Queue(const char* label,c::ax::SemanticAction action){
        const auto packet=state.presentation.exchange.Published();
        if(!packet || !packet->valid)return false;
        for(const auto& node:packet->nodes)if(node.name==label)
            return state.presentation.exchange.QueueNext(packet->generation,node.id,action).second==c::ActionResult::Queued;
        return false;
    }
    bool Settled(const p::NativeFrame& frame){
        if(stable_stage!=stage){stable_stage=stage;capture_at=frame.rendered_frames+2;}
        const auto packet=state.presentation.exchange.Published();
        return frame.rendered_frames>=capture_at && packet && packet->valid && !packet->overflow;
    }
    void Press(p::NativeHost& host,const c::ItemFacts& item){
        Check(item.visible && !item.disabled && item.rect[0]>=0 && item.rect[1]>=0 && item.rect[2]<=960 && item.rect[3]<=640,"reachable native control");
        int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);
        Check(motion(display,-1,x+int((item.rect[0]+item.rect[2])/2),y+int((item.rect[1]+item.rect[3])/2),0),"XTest pointer");
        Check(button(display,1,True,0),"XTest click");XSync(display,False);
    }
    void Release(){Check(button(display,1,False,0),"XTest release");XSync(display,False);}
    void Key(KeySym name,bool down){
        Check(key(display,XKeysymToKeycode(display,name),down,0),"XTest key");XSync(display,False);
    }
    bool PressMenu(p::NativeHost& host,const char* name){
        const auto packet=state.presentation.exchange.Published();
        if(!packet || !packet->valid)return false;
        for(const auto& node:packet->nodes)if(node.name==name && node.role==c::ax::SemanticRole::Menu){
            Press(host,{{node.minimum.x,node.minimum.y,node.maximum.x,node.maximum.y},0,false,false,false,false,node.showing});
            return true;
        }
        return false;
    }
    void Capture(const p::NativeFrame& frame,const char* id,std::initializer_list<std::string_view> required){
        Check(frame.width==960 && frame.height==640 && frame.effective_scale==1.5F && frame.decorated && !frame.gl_error,"native geometry");
        const auto packet=state.presentation.exchange.Published();
        Check(packet && packet->valid && !packet->overflow,"bounded current semantic packet");
        std::string semantics;
        for(const auto& item:packet->nodes)semantics+=item.name+" | "+item.value+" | "+item.description+'\n';
        for(auto text:required)if(semantics.find(text)==std::string::npos)throw std::runtime_error(std::string(id)+" missing "+std::string(text));
        Check(!frame.rgba.empty() && semantics.find("rpc-cookie")==std::string::npos,"capture is credential-reference free");
        std::ofstream ppm(output/(std::string(id)+".ppm"),std::ios::binary);
        ppm<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for(int y=frame.framebuffer_height-1;y>=0;--y)for(int x=0;x<frame.framebuffer_width;++x)
            ppm.write(reinterpret_cast<const char*>(frame.rgba.data()+(size_t(y)*frame.framebuffer_width+x)*4),3);
        Check(bool(ppm),"framebuffer retained");
        std::ofstream(output/(std::string(id)+"-semantics.txt"))<<semantics;
        ++captures;
    }
    void Frame(p::NativeHost& host,const p::NativeFrame& frame) override{
        if(frame.rendered_frames<4)return;
        if(resumed){ResumedFrame(host,frame);return;}
        switch(stage){
        case 0:
            if(requests.connection.Busy())break;
            Check(requests.connection.CanApply() && requests.connection.Apply(),"fixture explicitly applies authenticated profile");
            Check(state.general.saved && !state.general.saved->preferences.desktop_notifications && !state.general.saved->preferences.camera_keep_awake
                && !notifications && !opens,"desktop integrations are opt-in");
            stage=30;break;
        case 30:if(Queue("Help",c::ax::SemanticAction::Press))stage=1;break;
        case 1:if(Queue("About",c::ax::SemanticAction::Press))stage=2;break;
        case 2:
            if(!facts.desktop_visible || !Settled(frame))break;
            Capture(frame,"help-about",{"About Bitcoin Roots","32.1-roots.1","Plan-B Foundation","Bitcoin Core v32.1"});
            Press(host,facts.desktop.at("source"));stage=3;break;
        case 3:Release();stage=4;break;
        case 4:
            if(!state.desktop.target || !Settled(frame))break;
            Check(opens==0 && state.desktop.confirmed_url==a::HelpUrl(a::HelpTarget::Source),"exact URL awaits deliberate confirmation");
            Press(host,facts.desktop.at("open-link"));stage=5;break;
        case 5:Release();stage=6;break;
        case 6:
            if(opens!=1)break;
            Press(host,facts.desktop.at("close"));stage=7;break;
        case 7:Release();stage=8;break;
        case 8:if(!facts.desktop_visible && Queue("View",c::ax::SemanticAction::Press))stage=9;break;
        case 9:if(Queue("Tray",c::ax::SemanticAction::Press))stage=10;break;
        case 10:
            if(!facts.desktop_visible || !Settled(frame))break;
            Check(facts.desktop.at("notification-test").disabled,"test notification starts unavailable until opt-in");
            if(Queue("Allow generic desktop notifications",c::ax::SemanticAction::Toggle))stage=11;
            break;
        case 11:
            if(!state.general.saved->preferences.desktop_notifications || !Settled(frame))break;
            Check(notifications==0,"saving opt-in starts no notification");
            if(Queue("Keep awake during camera QR capture",c::ax::SemanticAction::Toggle))stage=12;
            break;
        case 12:
            if(!state.general.saved->preferences.camera_keep_awake || !Settled(frame))break;
            Capture(frame,"desktop-preferences",{"Desktop preferences","Minimize window","System tray needs a tray backend","Keep awake during camera QR capture","Allow generic desktop notifications"});
            Press(host,facts.desktop.at("notification-test"));stage=20;break;
        case 20:Release();stage=21;break;
        case 21:
            if(notifications!=1 || host.Desktop()->Busy())break;
            Press(host,facts.desktop.at("minimize"));stage=22;break;
        case 22:Release();stage=13;break;
        case 14:
            if(facts.desktop_visible)break;
            Check(iconified && state.documents.Documents().size()==1 && state.documents.Active()->transaction.note.draft()=="Exact saved GUI note",
                "real minimize/restore preserves exact document");
            state.documents.Active()->transaction.note.SetDraft("Owned unsaved work");
            Check(requests.connection.SetOffline(true),"offline policy intentionally selected before restart");
            stage=31;break;
        case 31:if(PressMenu(host,"Tools"))stage=32;break;
        case 32:Release();stage=33;break;
        case 33:if(facts.menu_rows.contains(a::Command::Restart)){Key(XK_End,true);stage=34;}break;
        case 34:Key(XK_End,false);stage=15;break;
        case 15:{
            const auto item=facts.menu_rows.find(a::Command::Restart);
            if(item==facts.menu_rows.end() || !item->second.visible)break;
            Press(host,item->second);stage=35;break;
        }
        case 35:Release();stage=16;break;
        case 16:
            if(!facts.desktop_visible || !Settled(frame))break;
            Check(facts.desktop.at("confirm-restart").disabled && !requests.restart && state.documents.Active()->transaction.note.dirty(),
                "actual restart panel blocks unsaved work without discarding it");
            state.documents.Active()->transaction.note.SetDraft("Exact saved GUI note");
            stage=17;break;
        case 17:
            if(facts.desktop.at("confirm-restart").disabled)break;
            Press(host,facts.desktop.at("confirm-restart"));stage=18;break;
        case 18:Release();stage=19;break;
        }
    }
    void ResumedFrame(p::NativeHost& host,const p::NativeFrame& frame){
        switch(stage){
        case 0:
            Check(requests.recovery.Pending() && state.documents.Documents().empty() && !resumed_applied,"restart requires an explicit recovery choice");
            Check(requests.probes==0 && !requests.connection.Active(),"restart performs no network request");
            if(!facts.document_actions.contains("restore-session"))break;
            Press(host,facts.document_actions.at("restore-session"));stage=1;break;
        case 1:Release();stage=2;break;
        case 2:
            if(!resumed_applied || !Settled(frame))break;
            Check(state.documents.Documents().size()==1 && state.documents.Active()->transaction.note.draft()=="Exact saved GUI note"
                && state.documents.Active()->observation==a::ObservationState::Offline,"exact explicit offline recovery");
            Check(state.general.saved && state.general.saved->preferences.desktop_notifications && state.general.saved->preferences.camera_keep_awake,
                "fresh process read acknowledged desktop preferences");
            Check(requests.connection.Draft()==resumed->profile && state.server.Candidate()==resumed->profile
                && requests.connection.Draft().connection.port==port && requests.connection.Draft().expected_genesis==GENESIS
                && requests.connection.Draft().connection.credential.reference==(metadata.parent_path()/"rpc-cookie").string()
                && requests.connection.Offline(),"exact profile, genesis, datadir/cookie reference and offline policy carried");
            Check(requests.probes==0 && !requests.connection.Active() && !requests.connection.Tested() && !requests.connection.CanApply()
                && !notifications && !opens,"restart performs no test, activation, notification or external replay");
            done=true;host.RequestClose();break;
        }
    }
    void Teardown(const p::NativeTeardown& state) override{
        cleaned=state.renderer_cleared && state.platform_cleared && state.textures_released && state.imgui_cleared && state.context_cleared && state.input_cleared;
        Check(cleaned,"native resources released before returning or replacing GUI");
    }
};
}
int main(int argc,char** argv)
{
    Display* display{};void* xtst{};
    try{
        const bool after=argc==6 && std::string_view(argv[1]).starts_with("--resume-gui-fd=");
        if((!after && argc!=5) || (after && argc!=6))return 2;
        std::optional<p::RestartContext> context;
        if(after){context=p::ConsumeRestartContext(argv[1]);if(!context || !context->profile)throw std::runtime_error("retained context invalid");}
        const int offset=int(after);
        const char* name=std::getenv("DISPLAY"),*owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if(!name || !owned || std::string_view(name)!=owned)throw std::runtime_error("owned X11 display required");
        display=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);
        if(!display || !xtst)throw std::runtime_error("XTest unavailable");
        Observer observer(argv[1+offset],argv[3+offset],std::stoi(argv[4+offset]),std::move(context));
        observer.display=display;
        observer.motion=reinterpret_cast<decltype(observer.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));
        observer.button=reinterpret_cast<decltype(observer.button)>(dlsym(xtst,"XTestFakeButtonEvent"));
        observer.key=reinterpret_cast<decltype(observer.key)>(dlsym(xtst,"XTestFakeKeyEvent"));
        if(!observer.motion || !observer.button || !observer.key)throw std::runtime_error("XTest input unavailable");
        std::filesystem::create_directories(observer.output);
        p::NativeOptions options{960,640,false,1.5F,argv[2+offset]};options.fixed_delta_seconds=1.F/60;
        options.desktop_ports=p::DesktopPorts{[](std::stop_token){return p::SleepLease();},
            [&observer](p::DesktopNotification,std::stop_token){++observer.notifications;return true;},
            [&observer](a::HelpTarget target,std::string_view url){++observer.opens;return a::ConfirmedHelpUrl(target,url);}};
        {
            p::NativeHost host(options,&observer,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){
                if(observer.resumed && !observer.resumed_applied)
                    observer.resumed_applied=p::AdoptGuiRestart(observer.state,observer.requests,*observer.resumed);
                observer.facts=p::DrawShellFrame(*observer.requests.host,observer.state,observer.requests,theme,appearance);
            });
            observer.requests.host=&host;host.Run();
        }
        XCloseDisplay(display);display=nullptr;dlclose(xtst);xtst=nullptr;
        if(after){
            observer.Check(observer.done && observer.cleaned,"replacement GUI settled");
            std::ofstream(observer.output/"restart-after.json")<<"{\"pid\":"<<::getpid()<<",\"assertions\":"<<observer.assertions
                <<",\"probes\":"<<observer.requests.probes<<",\"teardown\":true}\n";
            return 0;
        }
        observer.Check(observer.requests.restart && observer.cleaned && observer.captures==2,"initial GUI settled before re-exec");
        std::ofstream(observer.output/"restart-before.json")<<"{\"pid\":"<<::getpid()<<",\"assertions\":"<<observer.assertions
            <<",\"probes\":"<<observer.requests.probes<<",\"teardown\":true,\"captures\":2}\n";
        const std::array<std::string,4> args{argv[1],argv[2],argv[3],argv[4]};
        const auto error=p::ReplaceGui(*observer.requests.restart,args);
        throw std::runtime_error(error.detail);
    }catch(const std::exception& error){
        std::cerr<<error.what()<<'\n';
        if(display)XCloseDisplay(display);
        if(xtst)dlclose(xtst);
        return 1;
    }
}
