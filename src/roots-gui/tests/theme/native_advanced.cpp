// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include "app/connection_observations.h"
#include "app/history_workspace.h"
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
    app::RecoverySession* recovery{};
    RescanService rescan{[this](const ScanRequest& work,std::stop_token stop){return FetchScan(transport,work,stop);}};
    RefreshService refresh{[this](const RefreshRequest& work,std::stop_token stop){return FetchRefresh(transport,work,stop);}};
    ConnectionService* Connection() override{return &connection;}
    WalletLifecycle* Wallets() override{return &lifecycle;}
    RescanService* Rescanning() override{return &rescan;}
    app::RecoverySession* Recovery() override{return recovery;}
    RefreshService* Refresh() override{return &refresh;}
    app::HostObservations Observe() const override{return app::ConnectionObservations(connection,refresh);}
    app::Availability CanSubmit(const app::Request&) const override{return {};}
    app::Receipt Submit(const app::Request&) override{return {};}
};

struct Observer final:platform::NativeObserver {
    Observer(std::filesystem::path directory,std::string descriptor,std::filesystem::path accepted)
        : out(std::move(directory)), scan_accepted(std::move(accepted)), descriptor(std::move(descriptor)), display(XOpenDisplay(nullptr)), xtst(dlopen("libXtst.so.6",RTLD_NOW))
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
    std::filesystem::path out,scan_accepted;
    std::string descriptor, old_export,watch_export;
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
    bool account_strip_seen{};

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
        throw std::runtime_error("native rescan deadline at stage "+std::to_string(stage));
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
            for (const auto& [name,fact]:facts.advanced) {
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
        const auto* doc=state.documents.Active();
        if(frame.rendered_frames<5 || !doc)return;
        const auto id=doc->id;
        const auto settings=state.settings.find(id);
        const auto* policy=settings==state.settings.end()?nullptr:&settings->second;
        const auto advanced=state.advanced.find(id);
        const auto* scan=advanced==state.advanced.end()?nullptr:&advanced->second;
        const auto click=[&](const char* key){Click(host,facts.settings.at(key));};
        const auto adv=[&](const char* key){Click(host,facts.advanced.at(key));};
        const auto dialog=[&](const char* key){Click(host,facts.onboarding.at(key));};
        const auto status=[&](std::string_view word){return scan && scan->status.find(word)!=std::string::npos;};
        const auto output=state.presentation.exchange.Published();
        const auto semantic=[&](std::string_view value){
            return output && output->valid && std::ranges::any_of(output->nodes,[&](const auto& item){
                return item.name.find(value)!=std::string::npos || item.value.find(value)!=std::string::npos;
            });
        };
        account_strip_seen |= facts.accounts_submitted || facts.layout.accounts.h!=0;
        switch(stage) {
        case 0:
            original=id;
            Control(XK_6);
            break;
        case 1:
            if (!facts.settings.contains("read"))return;
            click("read");
            break;
        case 2:
            if (!policy || !policy->saved)return;
            clipboard_before=host.InputState().clipboard_writes;
            click("export");
            break;
        case 3:
            if (host.InputState().clipboard_writes==clipboard_before)return;
            old_export=Clipboard(host);
            Check(!old_export.empty(),"original wallet has acknowledged public export");
            click("add-account");
            break;
        case 4:
            if (!semantic("real account service") || !facts.advanced.contains("account-open"))return;
            Check(doc->binding->DaemonWallet()=="rescan-origin" && state.documents.Documents().size()==1
                  && !facts.advanced.at("account-new").disabled,"New and Open are separate from Add Account");
            Capture(frame,"account-service-unavailable");
            adv("account-open");
            break;
        case 5:
            if (!facts.onboarding.contains("cancel"))return;
            Check(state.onboarding.page==shell::OnboardingPage::Open,"Open action routes to real wallet chooser");
            dialog("cancel");
            break;
        case 6:
            if (state.onboarding.close_pending || !facts.advanced.contains("account-new"))return;
            adv("account-new");
            break;
        case 7:
            if (!facts.onboarding.contains("cancel"))return;
            Check(state.onboarding.page==shell::OnboardingPage::Create,"New action routes to real wallet creation");
            dialog("cancel");
            break;
        case 8:
            if (state.onboarding.close_pending || !facts.advanced.contains("account-back"))return;
            adv("account-back");
            break;
        case 9:
            if (!facts.settings.contains("paste"))return;
            Check(host.Input().Copy(descriptor),"owned public descriptor input");
            click("paste");
            break;
        case 10:
            if (!policy || !policy->staged || !facts.settings.contains("apply"))return;
            click("apply");
            break;
        case 11:
            if (!state.onboarding.policy_review || !facts.onboarding.contains("name"))return;
            Check(doc->id==original,"policy review keeps original wallet selected");
            dialog("name");
            break;
        case 12:
            Check(host.Input().Copy("rescan-watch"),"owned watch-only wallet name");
            Control(XK_v);
            break;
        case 13:
            if (state.onboarding.name.draft()!="rescan-watch" || !facts.onboarding.contains("import-wallet"))return;
            dialog("import-wallet");
            break;
        case 14:
            if (state.onboarding.dialog.open || state.onboarding.close_pending || id==original || !policy || !policy->saved)return;
            Check(state.documents.Documents().size()==2 && doc->binding->DaemonWallet()=="rescan-watch"
                && state.documents.Find(original)->binding->DaemonWallet()=="rescan-origin","two exact, distinct daemon wallets");
            Check(requests.connection.Active()->private_keys==false && !policy->dirty
                && state.settings.at(original).saved->branches.front().descriptor.descriptor!=policy->saved->branches.front().descriptor.descriptor,
                "a distinct watch-only policy was acknowledged");
            clipboard_before=host.InputState().clipboard_writes;
            click("export");
            break;
        case 15:
            if (host.InputState().clipboard_writes==clipboard_before)return;
            watch_export=Clipboard(host);
            Check(watch_export==policy->saved->branches.front().descriptor.descriptor && watch_export!=old_export,
                "independent saved export from imported wallet");
            Check(requests.recovery->Enable(state.documents),"explicit opted-in public recovery");
            click("advanced");
            break;
        case 16:
            if (!facts.advanced.contains("check") || facts.advanced.at("check").disabled)return;
            adv("check");
            break;
        case 17:
            if (!scan || !scan->observation || requests.rescan.Busy() || !semantic("Start block"))return;
            Check(scan->observation->wallet_height>=2 && scan->observation->birthtime.has_value()
                && scan->observation->receive_keys && scan->observation->active_ranges && scan->observation->active_ranges->size()==1,
                "actual daemon bounds, birth, keypool and descriptor range observed");
            Set("Start block","1");
            break;
        case 18:
            if (!scan || scan->range.first!=1 || !semantic("End block"))return;
            Set("End block","2");
            break;
        case 19:
            if (!scan || scan->range!=ScanRange{1,2} || !facts.advanced.contains("review"))return;
            adv("review");
            break;
        case 20:
            if (!scan || !scan->review || !semantic("Scan blocks 1 to 2"))return;
            Check(scan->review->range==ScanRange{1,2} && doc->binding->DaemonWallet()=="rescan-watch","reviewed exact wallet and block range");
            if (!scan->show_ranges) {
                adv("descriptor-ranges");
                return;
            }
            if (!semantic("next index"))return;
            Capture(frame,"advanced-rescan");
            adv("confirm");
            break;
        case 21:
            if (!status("Daemon confirmed blocks 1 to 2") || !facts.advanced.contains("review"))return;
            Check(!requests.recovery->PendingScan(app::HistoryMetadataScope(*doc->binding)).value->has_value()
                && state.documents.Find(original)->history.query_revision==1 && doc->history.query_revision>1,"completed receipt clears only its own scope");
            Check(!state.advanced.contains(original),"first wallet never inherits the imported wallet's Advanced state");
            Set("Start block","2");
            break;
        case 22:
            if (!scan || scan->range!=ScanRange{2,2})return;
            adv("review");
            break;
        case 23:
            if (!scan || !scan->review || !semantic("Scan blocks 2 to 2"))return;
            adv("confirm");
            break;
        case 24:
            if (!std::filesystem::exists(scan_accepted) || !facts.advanced.contains("cancel"))return;
            adv("cancel");
            break;
        case 25:
            if (requests.rescan.Busy() || !status("Stopped waiting"))return;
            Check(semantic("Previous request for blocks 2 to 2") && !facts.advanced.contains("confirm"),
                "after an accepted daemon mutation, local cancel preserves unconfirmed scope");
            Capture(frame,"advanced-cancelled");
            Click(host,facts.tabs.at(original));
            break;
        case 26:
            if (id!=original || !facts.settings.contains("advanced"))return;
            click("advanced");
            break;
        case 27:
            if (!facts.advanced.contains("check"))return;
            Check(!semantic("Previous request for blocks") && facts.advanced.at("check").disabled,
                "disconnected original tab cannot inspect or rescan the other wallet");
            Check(!requests.recovery->PendingScan(app::HistoryMetadataScope(*doc->binding)).value->has_value(),
                "original wallet has no scan ticket");
            Check(requests.recovery->Checkpoint(state.documents),"persist both tabs without action authority");
            requests.refresh.Unbind();
            break;
        case 28:
            if (requests.lifecycle.Busy() || requests.rescan.Busy() || requests.refresh.Busy())return;
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

int Restore(const std::filesystem::path& path)
{
    app::RecoverySession recovery(path);
    app::DocumentRegistry documents;
    if (!recovery.Start() || !recovery.Pending() || !recovery.Restore(documents) || documents.Documents().size()!=2) return 1;
    bool original{}, watch{};
    for (const auto& doc:documents.Documents()) {
        if (!doc.binding || doc.observation!=app::ObservationState::Offline) return 1;
        const auto& name=doc.binding->DaemonWallet();
        const auto pending=recovery.PendingScan(app::HistoryMetadataScope(*doc.binding));
        if (!pending) return 1;
        if (name=="rescan-origin") original=!pending.value->has_value();
        if (name=="rescan-watch") watch=pending.value->has_value() && (**pending.value).range==ScanRange{2,2};
    }
    // Recovery contains only public documents and local scan observations.
    // No connection, credentials, active worker or submission was created here.
    if (!original || !watch) return 1;
    std::cout<<"{\"exactOfflineWallets\":2,\"pendingRange\":[2,2],\"commandsRestored\":0}\n";
    return 0;
}
}

int main(int argc,char** argv)
{
    if(argc==3 && std::string_view(argv[1])=="--restore")return Restore(argv[2]);
    if(argc!=9)return 2;
    try {
        const auto* display=std::getenv("DISPLAY");
        const auto* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if(!display || !owned || std::string_view(display)!=owned)throw std::runtime_error("owned display required");
        std::filesystem::create_directories(argv[1]);
        Observer observer(argv[1],argv[6],argv[8]);
        app::RecoverySession recovery(argv[7]);
        observer.Check(recovery.Start() && !recovery.Pending(),"empty owned metadata store");
        observer.requests.recovery=&recovery;
        ServerCandidate profile;
        profile.connection.port=uint16_t(std::stoi(argv[3]));
        profile.connection.credential.reference=argv[4];
        profile.expected_genesis=argv[5];
        profile.wallet="rescan-origin";
        auto& connection=observer.requests.connection;
        observer.Check(connection.SetDraft(profile) && connection.Test(),"explicit verified connection");
        const auto deadline=Clock::now()+std::chrono::seconds(10);
        while(connection.Busy() && Clock::now()<deadline){connection.Poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        observer.Check(connection.Apply() && CanStartScan(*connection.Active()) && CanImportWallet(*connection.Active()),
            "actual daemon proves both scan parameters and public import");
        platform::NativeOptions options{960,640,false,1.5F,argv[2]};
        options.fixed_delta_seconds=1.F/60;
        options.accessibility=&observer.state.presentation.exchange;
        platform::NativeHost* active{};
        {
            platform::NativeHost host(options,&observer,[&](const auto& theme,const auto& appearance){
                observer.facts=platform::DrawShellFrame(*active,observer.state,observer.requests,theme,appearance);
            });
            active=&host;
            host.Run();
        }
        observer.Check(observer.stage==29 && observer.cleaned && !observer.account_strip_seen,"complete native Advanced journey");
        std::ofstream out(observer.out/"result.json");
        out<<"{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":"<<observer.checks
            <<",\"frames\":"<<observer.frames<<",\"teardown\":true,\"oldExport\":"<<rpc::Quote(observer.old_export)
            <<",\"watchExport\":"<<rpc::Quote(observer.watch_export)<<"}\n";
        return out?0:1;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
