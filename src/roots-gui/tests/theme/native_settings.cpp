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
    Observer(std::filesystem::path directory,std::string descriptor)
        : out(std::move(directory)), multi(std::move(descriptor)), display(XOpenDisplay(nullptr)), xtst(dlopen("libXtst.so.6",RTLD_NOW))
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
    std::string multi, staged, staged_script, staged_address, original_export;
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
    int stage{},delay{},checks{},frames{},invalid_packets{};

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
    void Set(std::string_view label,std::string value)
    {
        const auto packet=state.presentation.exchange.Published();
        const auto found=std::ranges::find_if(packet->nodes,[&](const auto& item){
            return item.name==label && c::ax::Supports(item.actions,c::ax::SemanticAction::SetValue);
        });
        Check(found!=packet->nodes.end(),"actual numeric field supports accessible editing");
        Check(state.presentation.exchange.QueueNext(packet->generation,found->id,c::ax::SemanticAction::SetValue,std::move(value)).second
            ==c::ActionResult::Queued,"actual numeric accessibility input");
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
        const auto paste=[&] {
            const auto packet=state.presentation.exchange.Published();
            Check(packet && packet->valid,"current native settings publication");
            const auto field=std::ranges::find_if(packet->nodes,[](const auto& item){
                return item.name=="Paste public descriptor" && item.showing
                    && c::ax::Supports(item.actions,c::ax::SemanticAction::Press);
            });
            Check(field!=packet->nodes.end(),"reachable public descriptor paste action");
            Check(host.Input().Copy(multi),"fixture public descriptor clipboard");
            Check(state.presentation.exchange.QueueNext(packet->generation,field->id,c::ax::SemanticAction::Press).second
                ==c::ActionResult::Queued,"owned semantic paste action");
        };
        const auto publication=state.presentation.exchange.Published();
        switch(stage) {
        case 0:
            original=doc->id;
            Control(XK_6);
            break;
        case 1:
            if(!facts.settings.contains("read"))return;
            click("read");
            break;
        case 2:
            if(!current || !current->saved || !publication || !publication->valid)return;
            Check(!current->dirty && !current->multisig && current->script==protocol::ScriptTemplate::NativeSegwit
                && current->required==1 && current->signers==1,"saved single Native SegWit default");
            for(const auto& branch:current->saved->branches){
                if(!original_export.empty())original_export+='\n';
                original_export+=branch.descriptor.descriptor;
            }
            Capture(frame,"settings-single");
            paste();
            break;
        case 3:
            if(!current || !current->staged)return;
            if(!publication || !publication->valid){
                if(++invalid_packets<6)return;
                const auto tree=state.presentation.Tree();
                std::ofstream debug(out/"semantics-invalid.txt");
                debug << "valid=" << tree.Validate() << " overflow=" << tree.overflow
                      << " revision=" << current->revision << " currentScopeEqual=" << (publication && publication->scope==shell::SemanticScope(state)) << '\n';
                for(const auto& node:tree.nodes)debug << node.name << ':' << node.id << " parent=" << node.parent << " geometry="
                    << node.minimum.x << ',' << node.minimum.y << ".." << node.maximum.x << ',' << node.maximum.y << '\n';
                Check(false,"stable staged policy must publish valid semantics");
            }
            Check(current->multisig && current->required==2 && current->signers==2 && current->dirty,"staged multisig 2 of 2");
            Check(std::ranges::none_of(publication->nodes,[](const auto& node){return node.name=="Taproot key path";}),"no Taproot multisig selection");
            staged=current->staged->descriptors.front().descriptor;
            staged_script=current->staged->descriptors.front().script;
            staged_address=current->staged->descriptors.front().address;
            Capture(frame,"settings-multisig");
            clipboard_before=host.InputState().clipboard_writes;
            click("export");
            break;
        case 4:
            if(host.InputState().clipboard_writes==clipboard_before)return;
            Check(Clipboard(host)==original_export && original_export.find(staged)==std::string::npos,"Export ignores unacknowledged staging");
            Set("Required signatures (M)","3");
            break;
        case 5:
            if(!current || current->required!=3 || !publication || !publication->valid)return;
            Check(!current->staged && !current->invalid.empty() && facts.settings.at("apply").disabled,"out-of-bounds threshold is invalid");
            Capture(frame,"settings-invalid");
            Control(XK_1);
            break;
        case 6:
            if(doc->section!=app::Section::Transactions)return;
            Check(current->dirty && current->required==3,"staged edit survives section change");
            Control(XK_w);
            break;
        case 7:
            if(doc->section!=app::Section::Settings)return;
            Check(doc->id==original && current->dirty && current->status.find("retained")!=std::string::npos,"closing a staged policy returns to retained draft");
            click("revert");
            break;
        case 8:
            if(current->dirty)return;
            Check(current->required==1 && current->signers==1 && current->saved && facts.settings.at("apply").disabled,"Revert only restores saved policy");
            paste();
            break;
        case 9:
            if(!current->staged || !facts.settings.contains("apply") || facts.settings.at("apply").disabled)return;
            click("apply");
            break;
        case 10:
            if(!state.onboarding.policy_review || !facts.onboarding.contains("name"))return;
            Check(state.onboarding.preview && state.onboarding.preview->descriptors.front().descriptor==staged
                && state.documents.Documents().size()==1 && state.documents.ActiveId()==original,"consequence review preserves existing identity");
            Click(host,facts.onboarding.at("name"));
            break;
        case 11:
            Check(host.Input().Copy("settings-origin"),"owned duplicate wallet name");
            Control(XK_v);
            break;
        case 12:
            if(state.onboarding.name.draft()!="settings-origin" || !facts.onboarding.contains("import-wallet"))return;
            Click(host,facts.onboarding.at("import-wallet"));
            break;
        case 13:
            if(state.onboarding.error.find("already exists")==std::string::npos || requests.lifecycle.Busy())return;
            Check(doc->id==original && state.documents.Documents().size()==1 && current && current->dirty
                && current->saved && current->staged && current->staged->descriptors.front().descriptor==staged,
                "duplicate name failure retains original saved policy and exact staged draft");
            Click(host,facts.onboarding.at("name"));
            break;
        case 14:
            Control(XK_a);
            break;
        case 15:
            Check(host.Input().Copy("settings-watch"),"owned distinct wallet name");
            Control(XK_v);
            break;
        case 16:
            if(state.onboarding.name.draft()!="settings-watch" || !facts.onboarding.contains("import-wallet"))return;
            Click(host,facts.onboarding.at("import-wallet"));
            break;
        case 17:
            if(state.onboarding.dialog.open || state.onboarding.close_pending || doc->id==original || !current || !current->saved)return;
            Check(state.documents.Documents().size()==2 && doc->binding->DaemonWallet()=="settings-watch"
                && state.documents.Find(original)->binding->DaemonWallet()=="settings-origin","separate exact wallet tabs");
            Check(requests.connection.Active()->private_keys==false && !current->dirty
                && current->saved->branches.front().descriptor.descriptor==staged,"new saved policy only on acknowledgement");
            Check(!state.settings.at(original).dirty && state.settings.at(original).saved->branches.front().descriptor.descriptor!=staged,
                "original tab retains acknowledged saved state");
            clipboard_before=host.InputState().clipboard_writes;
            click("export");
            break;
        case 18:
            if(host.InputState().clipboard_writes==clipboard_before)return;
            Check(Clipboard(host)==staged,"new tab exports independently acknowledged descriptor");
            requests.refresh.Unbind();
            break;
        case 19:
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
    if(argc!=8)return 2;
    try{
        const auto* display=std::getenv("DISPLAY");
        const auto* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if(!display || !owned || std::string_view(display)!=owned)throw std::runtime_error("owned display required");
        std::filesystem::create_directories(argv[1]);
        Observer observer(argv[1],argv[7]);
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
        observer.Check(observer.stage==20 && observer.cleaned,"complete native Settings journey");
        std::ofstream out(observer.out/"result.json");
        out<<"{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":"<<observer.checks
            <<",\"frames\":"<<observer.frames<<",\"teardown\":true,\"originalExport\":"<<rpc::Quote(observer.original_export)
            <<",\"descriptor\":"<<rpc::Quote(observer.staged)<<",\"script\":"<<rpc::Quote(observer.staged_script)
            <<",\"address\":"<<rpc::Quote(observer.staged_address)<<"}\n";
        return out?0:1;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
