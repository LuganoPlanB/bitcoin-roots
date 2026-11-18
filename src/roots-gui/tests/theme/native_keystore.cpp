// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include "app/connection_observations.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
namespace {
namespace shell=ui::shell;
namespace c=ui::components;
using Clock=std::chrono::steady_clock;

struct Requests final:app::Requests {
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    ConnectionService connection{[this](const ServerCandidate& candidate,std::stop_token stop){return ProbeCapabilities(transport,candidate,stop);}};
    WalletLifecycle lifecycle{[this](const WalletOperation& work,std::stop_token stop){return RunWalletOperation(transport,work,stop);}};
    RefreshService refresh{[this](const RefreshRequest& work,std::stop_token stop){return FetchRefresh(transport,work,stop);}};
    ConnectionService* Connection() override{return &connection;}
    WalletLifecycle* Wallets() override{return &lifecycle;}
    RefreshService* Refresh() override{return &refresh;}
    app::HostObservations Observe() const override{return app::ConnectionObservations(connection,refresh);}
    app::Availability CanSubmit(const app::Request&) const override{return {};}
    app::Receipt Submit(const app::Request&) override{return {};}
};

struct Observer final:platform::NativeObserver {
    Observer(std::filesystem::path directory,std::string public_alias,std::string canonical_key)
        : out(std::move(directory)), alias(std::move(public_alias)), canonical(std::move(canonical_key)), display(XOpenDisplay(nullptr)), xtst(dlopen("libXtst.so.6",RTLD_NOW))
    {
        Check(display && xtst,"owned native input");
        motion=reinterpret_cast<decltype(motion)>(dlsym(xtst,"XTestFakeMotionEvent"));
        button=reinterpret_cast<decltype(button)>(dlsym(xtst,"XTestFakeButtonEvent"));
        key=reinterpret_cast<decltype(key)>(dlsym(xtst,"XTestFakeKeyEvent"));
        Check(motion && button && key,"owned native input entry points");
    }
    ~Observer() override{if(display)XCloseDisplay(display);if(xtst)dlclose(xtst);}
    Requests requests;
    shell::State state;
    shell::Facts facts;
    std::filesystem::path out;
    std::string alias, canonical, staged;
    size_t descendants{};
    Display* display;
    void* xtst;
    int(*motion)(Display*,int,int,int,unsigned long){};
    int(*button)(Display*,unsigned int,Bool,unsigned long){};
    int(*key)(Display*,unsigned int,Bool,unsigned long){};
    const Clock::time_point started{Clock::now()};
    uint64_t original{};
    uint64_t clipboard_before{};
    bool lift{}, release{}, cleaned{};
    KeySym held{};
    int stage{},delay{},checks{},frames{};

    void Check(bool condition,const char* why)
    {
        if(!condition)throw std::runtime_error("stage "+std::to_string(stage)+": "+why);
        ++checks;
    }
    bool CaptureFramebuffer() const override{return true;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override
    {
        if(Clock::now()-started<std::chrono::seconds(42))return;
        std::ofstream(out/"deadline.txt")<<stage<<' '<<state.onboarding.error<<'\n'<<state.presentation.Snapshot();
        throw std::runtime_error("native settings deadline at stage "+std::to_string(stage));
    }
    void Capture(const platform::NativeFrame& f,const char* tag)
    {
        Check(f.width==960 && f.height==640 && f.effective_scale==1.5F && f.decorated && f.font_texture && !f.gl_error,"owned compact native geometry");
        const auto publication=state.presentation.exchange.Published();
        Check(publication && publication->valid && state.presentation.Tree().Validate() && !state.presentation.Tree().overflow,"current bounded semantic packet");
        std::ofstream image(out/(std::string(tag)+".ppm"),std::ios::binary);
        image<<"P6\n"<<f.framebuffer_width<<' '<<f.framebuffer_height<<"\n255\n";
        for(int y=f.framebuffer_height-1;y>=0;--y)for(int x=0;x<f.framebuffer_width;++x)
            image.write(reinterpret_cast<const char*>(f.rgba.data()+(size_t(y)*f.framebuffer_width+x)*4),3);
        Check(bool(image),"framebuffer written");
        std::ofstream semantics(out/(std::string(tag)+".semantics.txt"));
        semantics<<state.presentation.Snapshot();
        Check(bool(semantics),"semantics written");
        ++frames;
    }
    void Click(platform::NativeHost& host,const c::ItemFacts& item)
    {
        const auto& r=item.rect;
        if (!item.visible || item.disabled || r[0]<0 || r[1]<0 || r[2]>960 || r[3]>640) {
            std::ofstream diagnostic(out/"reachability.txt");
            for (const auto& [name,fact]:facts.settings) {
                diagnostic << name << ' ' << fact.visible << ' ' << fact.disabled;
                for (const auto value:fact.rect) diagnostic << ' ' << value;
                diagnostic << '\n';
            }
            diagnostic << state.presentation.Snapshot();
            Check(false,"reachable native control");
        }
        ++checks;
        int x{},y{};
        glfwGetWindowPos(host.Window(),&x,&y);
        Check(motion(display,-1,x+int((r[0]+r[2])/2),y+int((r[1]+r[3])/2),0) && button(display,1,True,0),"native press");
        lift=true;
        XSync(display,False);
    }
    void Key(KeySym symbol,bool down){Check(key(display,XKeysymToKeycode(display,symbol),down,0),"owned key input");}
    void Control(KeySym symbol)
    {
        held=symbol;Key(XK_Control_L,true);Key(symbol,true);release=true;XSync(display,False);
    }
    bool Value(std::string_view name,std::string_view value) const
    {
        const auto packet=state.presentation.exchange.Published();
        return packet && packet->valid && std::ranges::any_of(packet->nodes,[&](const auto& item) {
            return item.name==name && item.value==value;
        });
    }
    std::string Clipboard(platform::NativeHost& host)
    {
        constexpr uint32_t id=19543;
        Check(host.Input().RequestInspection(id),"inspect owned clipboard");
        const auto copy=host.Input().Paste(id,"",0,0,0,65537);
        Check(bool(copy),"read exact clipboard bytes");
        return *copy;
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override
    {
        if(lift){Check(button(display,1,False,0),"native release");lift=false;XSync(display,False);return;}
        if(release){Key(held,false);Key(XK_Control_L,false);release=false;XSync(display,False);return;}
        if(delay){--delay;return;}
        if(frame.rendered_frames<5 || !state.documents.Active())return;
        const auto* doc=state.documents.Active();
        const auto settings=state.settings.find(doc->id);
        const auto* current=settings==state.settings.end()?nullptr:&settings->second;
        const auto click=[&](const char* id){Click(host,facts.settings.at(id));};
        const auto publication=state.presentation.exchange.Published();
        switch(stage) {
        case 0:
            original=doc->id;
            Control(XK_6);
            break;
        case 1:
            if(!facts.settings.contains("toggle-keystores"))return;
            click("toggle-keystores");
            break;
        case 2:
            if(!facts.settings.contains("source-watch") || !publication || !publication->valid)return;
            Check(facts.settings.contains("source-connected") && facts.settings.contains("source-airgapped")
                && facts.settings.contains("source-daemon") && !current->signer_drafts,"four explicit source choices");
            Check(Value("Unconfigured signer","Choose a source to inspect public signer material."),"unconfigured public slot");
            Capture(frame,"keystore-empty");
            click("source-connected");
            break;
        case 3:
            if(!Value("Connected signer availability","Connected signer discovery requires a configured daemon external-signer service."))return;
            click("source-daemon");
            break;
        case 4:
            if(!Value("Daemon software custody","The daemon holds the private keys. Read its saved descriptors to inspect public keys."))return;
            click("source-airgapped");
            break;
        case 5:
            if(!facts.settings.contains("paste-signer"))return;
            Check(facts.settings.at("scan-signer").disabled,"QR intake requires its decoder");
            Check(host.Input().Copy("[DEADA00D/84'/1'/0']"+alias),"only public origin and alternate header supplied");
            click("paste-signer");
            break;
        case 6:
            if(!current->staged || !publication || !publication->valid)return;
            Check(current->dirty && current->signer_drafts && current->signer_drafts->size()==1
                && current->signer_drafts->begin()->second.source==shell::SignerSource::Airgapped,"configured public signer is staged");
            Check(Value("Type","Airgapped public key (supplied)") && Value("Label","Signer 1")
                && Value("Fingerprint","deada00d") && Value("Derivation","m/84h/1h/0h → key/0/*") && Value("Xpub",canonical),
                "structured public origin and canonical xpub");
            Check(facts.settings.at("show-signer-qr").disabled && facts.settings.at("scan-signer").disabled,"named unavailable QR intake and display");
            staged=current->staged->descriptors.front().descriptor;
            Check(staged.find(canonical)!=std::string::npos && staged.find(alias)==std::string::npos
                && !current->saved,"alternate encoding is normalized before descriptor staging");
            Capture(frame,"keystore-configured");
            clipboard_before=host.InputState().clipboard_writes;
            click("copy-signer");
            break;
        case 7:
            if(host.InputState().clipboard_writes==clipboard_before)return;
            Check(Clipboard(host)==canonical,"copy exact canonical public key");
            click("source-watch");
            break;
        case 8:
            if(current->signer_source!=shell::SignerSource::WatchOnly || !facts.settings.contains("paste-signer"))return;
            Check(host.Input().Copy("[01234567/0//1]"+canonical),"public malformed-origin fixture");
            click("paste-signer");
            break;
        case 9:
            if(current->signer_error.empty())return;
            Check(current->staged->descriptors.front().descriptor==staged
                && current->signer_drafts->begin()->second.key.fingerprint=="deada00d" && facts.settings.at("apply").disabled,
                "invalid derivation preserves previous draft and blocks Apply");
            Check(host.Input().Copy("[deada00d/84h/1h/0h]"+canonical),"public replacement requires an explicit paste");
            click("paste-signer");
            break;
        case 10:
            if(!current->signer_error.empty() || facts.settings.at("apply").disabled)return;
            Check(Value("Type","Watch-only public key") && current->staged->descriptors.front().descriptor==staged,
                "watch-only source uses the same checked public signer path");
            click("apply");
            break;
        case 11:
            if(!state.onboarding.policy_review || !facts.onboarding.contains("cancel"))return;
            Check(state.onboarding.preview && state.onboarding.preview->descriptors.front().descriptor==staged
                && state.documents.ActiveId()==original && state.documents.Documents().size()==1,"only immutable staged policy enters review");
            Click(host,facts.onboarding.at("cancel"));
            break;
        case 12:
            if(state.onboarding.dialog.open || state.onboarding.close_pending)return;
            Check(current->dirty && !current->saved,"cancelling review leaves the staged signer local");
            click("read");
            break;
        case 13:
            if(!current->saved || requests.lifecycle.Busy())return;
            descendants=current->saved->branches.size();
            Check(descendants>0 && current->staged->descriptors.front().descriptor==staged,"saved daemon read does not substitute a staged signer");
            click("revert");
            break;
        case 14:
            if(current->dirty)return;
            Check(!current->signer_drafts && !current->staged && Value("Type","Daemon software wallet")
                && doc->id==original && facts.settings.at("apply").disabled,"Revert restores acknowledged daemon source and custody");
            requests.refresh.Unbind();
            break;
        case 15:
            if(requests.lifecycle.Busy() || requests.refresh.Busy())return;
            host.RequestClose();
            break;
        default:return;
        }
        ++stage;
        delay=3;
    }
    void Teardown(const platform::NativeTeardown& result) override
    {
        cleaned=result.renderer_cleared && result.platform_cleared && result.textures_released && result.imgui_cleared
            && result.context_cleared && result.accessibility_cleared && result.input_cleared;
        Check(cleaned,"native resources settled");
    }
};
}

int main(int argc,char** argv)
{
    if(argc!=9)return 2;
    try{
        const auto* display=std::getenv("DISPLAY");
        const auto* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if(!display || !owned || std::string_view(display)!=owned)throw std::runtime_error("owned display required");
        std::filesystem::create_directories(argv[1]);
        Observer observer(argv[1],argv[7],argv[8]);
        ServerCandidate profile;
        profile.connection.port=uint16_t(std::stoi(argv[3]));
        profile.connection.credential.reference=argv[4];
        profile.expected_genesis=argv[5];
        profile.wallet=argv[6];
        auto& connection=observer.requests.connection;
        observer.Check(connection.SetDraft(profile) && connection.Test(),"explicit verified connection");
        const auto deadline=Clock::now()+std::chrono::seconds(10);
        while(connection.Busy() && Clock::now()<deadline){connection.Poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        observer.Check(connection.Apply() && CanReadWalletPolicy(*connection.Active()) && CanImportWallet(*connection.Active()),"actual policy read and import capabilities");
        platform::NativeOptions options{960,640,false,1.5F,argv[2]};
        options.fixed_delta_seconds=1.F/60;
        platform::NativeHost* active{};
        {
            platform::NativeHost host(options,&observer,[&](const auto& theme,const auto& appearance){
                observer.facts=platform::DrawShellFrame(*active,observer.state,observer.requests,theme,appearance);
            });
            active=&host;
            host.Run();
        }
        observer.Check(observer.stage==16 && observer.cleaned,"complete native keystore journey");
        std::ofstream out(observer.out/"result.json");
        out<<"{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":"<<observer.checks
            <<",\"frames\":"<<observer.frames<<",\"teardown\":true,\"descriptor\":"<<rpc::Quote(observer.staged)
            <<",\"canonicalXpub\":"<<rpc::Quote(observer.canonical)<<",\"savedBranches\":"<<observer.descendants<<"}\n";
        return out?0:1;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
