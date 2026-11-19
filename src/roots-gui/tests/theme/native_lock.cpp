// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace roots::gui;namespace a=app;namespace sh=ui::shell;
namespace {
constexpr std::string_view Password="fixture-native-lock-password";
constexpr std::string_view ClipboardSentinel="public clipboard fixture";
int checks{},failures{};
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<why<<'\n';}}
struct Requests final:a::Requests {
    a::RecoverySession* recovery{};
    int effects{};
    std::atomic<unsigned> entered{};
    unsigned released{};
    std::mutex mutex;
    std::condition_variable gate;
    ConnectionService connection{[this](const ServerCandidate& candidate,std::stop_token) {
        std::unique_lock lock(mutex);
        const auto ticket=++entered;
        BackendCapabilities result;
        result.candidate=candidate;
        result.identity_verified=result.authenticated=true;
        result.genesis=candidate.expected_genesis;
        result.chain="regtest";
        if (!gate.wait_for(lock,std::chrono::seconds(7),[&]{return released>=ticket;}))
            result.error=rpc::Error::Timeout;
        return result;
    }};
    ~Requests() override{Release(UINT_MAX);}
    void Release(unsigned ticket){{std::lock_guard lock(mutex);released=ticket;}gate.notify_all();}
    ConnectionService* Connection() override{return &connection;}
    a::RecoverySession* Recovery() override{return recovery;}
    a::Availability CanSubmit(const a::Request&) const override{return {false,"No daemon mutation port in native lock journey."};}
    a::Receipt Submit(const a::Request&) override{++effects;return {false,{}};}
};
struct Observer final:platform::NativeObserver {
    sh::State state;sh::Facts facts;Requests requests;Display* display{};std::filesystem::path out,metadata_path;bool done{},cleaned{};uint64_t old_generation{},old_tab{};
    int(*motion)(Display*,int,int,int,unsigned long){};int(*button)(Display*,unsigned int,Bool,unsigned long){};int(*key)(Display*,unsigned int,Bool,unsigned long){};
    const auto static inline Timeout=std::chrono::seconds(15);const std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    static constexpr unsigned Rounds=8;
    unsigned round{};
    enum class StressPhase { Dispatch, Held, Navigate, Settle, Complete };
    StressPhase stress{StressPhase::Dispatch};
    int lock_start{};
    bool setup_only{};
    uint64_t clipboard_writes{};
    Window clipboard_receiver{};
    Atom clipboard{}, utf8{}, property{};
    bool Stress(const platform::NativeFrame& frame)
    {
        if (frame.rendered_frames<3) return false;
        auto& connection=requests.connection;
        switch (stress) {
        case StressPhase::Dispatch: {
            ServerCandidate draft;
            draft.connection.port=uint16_t(19000+round*2);
            draft.connection.credential.reference=(metadata_path.parent_path()/"unused-cookie-reference").string();
            draft.expected_genesis=std::string(64,'0');
            Check(connection.SetDraft(draft) && connection.Test(),"dispatch synthetic held test without network");
            stress=StressPhase::Held;
            return false;
        }
        case StressPhase::Held: {
            if (requests.entered!=round+1) return false;
            if (round==Rounds) {
                lock_start=frame.rendered_frames;
                stress=StressPhase::Complete;
                return true;
            }
            const auto packet=state.presentation.exchange.Published();
            const auto name=round%2==0 ? "Wallet 2" : "Wallet 1";
            const auto tab=std::ranges::find_if(packet->nodes,[&](const auto& node){
                return node.role==ui::components::ax::SemanticRole::Tab && node.name==name;
            });
            if (tab==packet->nodes.end()) throw std::runtime_error("live wallet tab missing");
            Check(state.presentation.exchange.QueueNext(packet->generation,tab->id,ui::components::ax::SemanticAction::Select).second
                ==ui::components::ActionResult::Queued,"queue exact live wallet tab during held test");
            stress=StressPhase::Navigate;
            return false;
        }
        case StressPhase::Navigate: {
            if (state.documents.ActiveId()!=(round%2==0?2:1)) return false;
            auto replacement=connection.Draft();
            ++replacement.connection.port;
            Check(connection.SetDraft(replacement),"edit replacement backend while previous test held");
            Check(connection.Busy() && !connection.CanApply(),"held test cannot become eligible for apply");
            requests.Release(round+1);
            stress=StressPhase::Settle;
            return false;
        }
        case StressPhase::Settle:
            if (connection.Busy()) return false;
            Check(!connection.Tested() && !connection.Active() && !connection.CanApply()
                && state.presentation.Tree().Validate(),"retired test cannot publish or replace backend");
            ++round;
            stress=StressPhase::Dispatch;
            return false;
        case StressPhase::Complete:
            return true;
        }
        return false;
    }
    bool CaptureFramebuffer() const override{return true;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override{if(std::chrono::steady_clock::now()-started>Timeout)throw std::runtime_error("native lock deadline");}
    void Key(KeySym sym,bool down){Check(key(display,XKeysymToKeycode(display,sym),down,0),"native lock keyboard event");XSync(display,False);}
    void Shortcut(KeySym sym,int frame,int start)
    {
        if(frame==start){Key(XK_Control_L,true);Key(sym,true);}
        if(frame==start+1){Key(sym,false);Key(XK_Control_L,false);}
    }
    void SeedClipboard(platform::NativeHost& host)
    {
        Check(host.Input().Copy(ClipboardSentinel),"write public clipboard sentinel through native adapter");
        clipboard_writes=host.InputState().clipboard_writes;
        clipboard_receiver=XCreateSimpleWindow(display,DefaultRootWindow(display),0,0,1,1,0,0,0);
        clipboard=XInternAtom(display,"CLIPBOARD",False);
        utf8=XInternAtom(display,"UTF8_STRING",False);
        property=XInternAtom(display,"ROOTS_GUI_LOCK_TEST_SELECTION",False);
    }
    void ReadClipboard()
    {
        XConvertSelection(display,clipboard,utf8,property,clipboard_receiver,CurrentTime);
        XSync(display,False);
    }
    bool VerifyClipboard(platform::NativeHost& host)
    {
        XEvent event{};
        if(!XCheckTypedWindowEvent(display,clipboard_receiver,SelectionNotify,&event))return false;
        Atom actual{};
        int format{};
        unsigned long count{},remaining{};
        unsigned char* bytes{};
        const bool ok=event.xselection.property==property
            && XGetWindowProperty(display,clipboard_receiver,property,0,64,True,utf8,
                &actual,&format,&count,&remaining,&bytes)==Success
            && actual==utf8 && format==8 && !remaining && bytes
            && std::string_view(reinterpret_cast<char*>(bytes),count)==ClipboardSentinel;
        if(bytes)XFree(bytes);
        XDestroyWindow(display,clipboard_receiver);
        clipboard_receiver=0;
        Check(ok && host.InputState().clipboard_writes==clipboard_writes,
            "independent X11 selection sees only public data after password editor retires");
        return true;
    }
    void Type(std::string_view text,int frame,int start){const int at=(frame-start)/2;if(frame>=start && at<int(text.size()))Key(text[at]=='-'?XK_minus:KeySym(text[at]),(frame-start)%2==0);}
    void Click(platform::NativeHost& host,const char* id,bool down){const auto& item=facts.document_actions.at(id);const auto& r=item.rect;Check(item.visible && r[0]>=0 && r[1]>=0 && r[2]<=960 && r[3]<=640,"lock action reachable at150percent");int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);Check(motion(display,-1,x+int((r[0]+r[2])/2),y+int((r[1]+r[3])/2),0),"native lock pointer");Check(button(display,1,down,0),"native lock button");XSync(display,False);}
    void Capture(const platform::NativeFrame& f,const char* name){std::ofstream file(out/(std::string(name)+".ppm"),std::ios::binary);file<<"P6\n"<<f.framebuffer_width<<' '<<f.framebuffer_height<<"\n255\n";for(int y=f.framebuffer_height-1;y>=0;--y)for(int x=0;x<f.framebuffer_width;++x)file.write(reinterpret_cast<const char*>(f.rgba.data()+(size_t(y)*f.framebuffer_width+x)*4),3);Check(bool(file),"native lock framebuffer retained");const auto snapshot=state.presentation.Snapshot();Check(snapshot.find(Password)==std::string::npos && snapshot.find("bad")==std::string::npos,"password absent from accessibility publication");std::ofstream(out/(std::string(name)+".semantics.txt"))<<snapshot;}
    void Frame(platform::NativeHost& host,const platform::NativeFrame& f) override{
        if(setup_only){
            const int n=f.rendered_frames;
            if(n==3){SeedClipboard(host);Check(sh::RequestPrivacyLock(state,requests),"open unprotected workspace setup");}
            Shortcut(XK_v,n,8);
            if(n==12)Check(std::string_view(state.lock_panel.password.data())==ClipboardSentinel,"masked setup keeps normal paste");
            Shortcut(XK_a,n,13);
            if(n==15 || n==16)Key(XK_BackSpace,n==15);
            Type("setup",n,18);
            if(n==30 || n==31)Click(host,"reveal-password",n==30);
            if(n==33 || n==34)Click(host,"###local-password",n==33);
            Shortcut(XK_x,n,36);
            if(n==39)Check(state.lock_panel.reveal && !state.lock_panel.password[0]
                && host.InputState().clipboard_writes==clipboard_writes,"revealed setup Cut erases without publishing");
            if(n==41 || n==42)Click(host,"cancel-protection",n==41);
            if(n==45){
                Check(!state.lock_panel.setup && !requests.recovery->Protected() && !state.lock_panel.password[0],
                    "Cancel clears the setup password and leaves protection disabled");
                ReadClipboard();
            }
            if(n>45 && VerifyClipboard(host)){done=true;host.RequestClose();}
            return;
        }
        if (!Stress(f)) return;
        const int n=f.rendered_frames-lock_start;
        if(n==1){Check(state.documents.ActiveId()==1 && state.privacy.hidden,"stress returns to exact concealed wallet");Capture(f,"concurrency-wallet-switch");}
        if(n==2){const auto packet=state.presentation.exchange.Published();old_generation=packet->generation;for(const auto& entry:packet->nodes)if(entry.role==ui::components::ax::SemanticRole::Tab){old_tab=entry.id;break;}Check(old_tab!=0,"pre-lock accessibility tab exists");}
        if(n==3){Key(XK_Control_L,true);Key(XK_l,true);}if(n==4){Key(XK_l,false);Key(XK_Control_L,false);}
        if(n==7){Check(requests.recovery->Locked() && state.documents.Documents().empty(),"native shortcut locks and removes documents");Check(facts.tabs.empty() && state.presentation.Snapshot().find("Public watch-only wallet")==std::string::npos && state.presentation.Snapshot().find("Public fixture note")==std::string::npos,"locked tabs/notes absent from navigation and accessibility");Check(state.presentation.exchange.QueueNext(old_generation,old_tab,ui::components::ax::SemanticAction::Select).second==ui::components::ActionResult::Stale,"retired native accessibility action cannot reopen a locked tab");Check(f.decorated && f.font_texture && !f.gl_error && f.effective_scale==1.5F,"actual Linux960x640150lock renderer");Capture(f,"wallet-locked");}
        if(n==7){Check(requests.connection.PrivacyLocked() && requests.connection.Busy(),"actual native lock preceded final held reply");requests.Release(round+1);}
        if(n==8)SeedClipboard(host);
        Type("bad",n,9);
        Shortcut(XK_c,n,16);
        Shortcut(XK_x,n,18);
        if(n==21)Check(std::string_view(state.lock_panel.password.data())=="bad"
            && host.InputState().clipboard_writes==clipboard_writes,"masked Copy and Cut keep protection and editor state");
        if(n==23 || n==24)Click(host,"reveal-password",n==23);
        if(n==25)Check(state.lock_panel.reveal && state.presentation.Snapshot().find("bad")==std::string::npos,"deliberate reveal never exports password through accessibility");
        if(n==27 || n==28)Click(host,"###local-password",n==27);
        Shortcut(XK_c,n,30);
        Shortcut(XK_x,n,32);
        if(n==35)Check(state.lock_panel.reveal && !state.lock_panel.password[0]
            && host.InputState().clipboard_writes==clipboard_writes,"revealed unlock Copy and Cut do not publish, and Cut erases");
        Type("bad",n,36);
        if(n==44 || n==45)Click(host,"unlock-workspace",n==44);
        if(n==49){Check(requests.recovery->Locked() && !state.lock_panel.error.empty() && state.documents.Documents().empty(),"wrong password keeps protected documents concealed");Check(state.lock_panel.password[0]==0 && !state.lock_panel.reveal && host.InputState().clipboard_writes==clipboard_writes,"failed attempt clears secret, leaves clipboard intact and resets reveal");Capture(f,"unlock-failure");}
        Type(Password,n,52);
        const int ready=52+int(Password.size())*2+4;
        if(n==ready || n==ready+1)Click(host,"unlock-workspace",n==ready);
        if(n==ready+6){Check(requests.recovery->Locked() && requests.recovery->ConversionConflict() && state.documents.Documents().empty(),"native authenticated conversion conflict retains concealment");Capture(f,"conversion-conflict");}
        const int again=ready+9;Type(Password,n,again);const int resolved=again+int(Password.size())*2+4;
        if(n==resolved || n==resolved+1)Click(host,"resolve-conversion",n==resolved);
        if(n==resolved+6){Check(!std::filesystem::exists(metadata_path) && std::filesystem::exists(metadata_path.string()+".conversion-backup.protected"),"native deliberate resolution preserves other copy encrypted");Check(!requests.recovery->Locked() && state.documents.Documents().size()==3,"correct native password unlocks existing independent documents");Check(state.documents.Find(1)->sections[1].note.draft()=="Protected native draft" && state.privacy.hidden,"unlock restores draft with prior amount privacy");Check(requests.effects==0,"GUI lock/unlock sends no daemon request");Check(state.presentation.Tree().Validate(),"native unlocked semantic graph validates");Check(!requests.connection.Busy() && !requests.connection.Tested() && !requests.connection.Active(),"final late probe settles without replay on unlock");ReadClipboard();}
        if(n>resolved+6 && VerifyClipboard(host)){done=true;host.RequestClose();}
    }
    void Teardown(const platform::NativeTeardown& t) override{cleaned=t.renderer_cleared && t.platform_cleared && t.textures_released && t.imgui_cleared && t.context_cleared;Check(cleaned,"native lock resources settled");}
};
}
int main(int argc,char** argv){if(argc!=4)return 2;const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);Display* display{};void* xtst{};
    try{const char* name=getenv("DISPLAY"),*owned=getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");if(!name || !owned || std::string(name)!=owned)throw std::runtime_error("owned display required");a::RecoverySession session(argv[3]);Observer o;o.out=out;o.requests.recovery=&session;o.state.documents=a::PublicFixtureDocuments();o.state.documents.Find(1)->sections[1].note.SetDraft("Protected native draft");o.state.privacy.hidden=true;session.SetPresentation(true,0);Check(session.Enable(o.state.documents),"native conversion fixture opts in explicitly");std::ifstream source(argv[3],std::ios::binary);const std::string interrupted{std::istreambuf_iterator<char>(source),{}};source.close();Check(session.Protect(o.state.documents,Password),"explicit synthetic native protection enabled");{std::ofstream copy(argv[3],std::ios::binary);copy.write(interrupted.data(),interrupted.size());Check(bool(copy),"owned native interrupted-conversion fixture retained");}std::filesystem::permissions(argv[3],std::filesystem::perms::owner_read|std::filesystem::perms::owner_write);o.metadata_path=argv[3];display=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);if(!display || !xtst)throw std::runtime_error("native input provider unavailable");o.display=display;o.motion=reinterpret_cast<decltype(o.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));o.button=reinterpret_cast<decltype(o.button)>(dlsym(xtst,"XTestFakeButtonEvent"));o.key=reinterpret_cast<decltype(o.key)>(dlsym(xtst,"XTestFakeKeyEvent"));if(!o.motion || !o.button || !o.key)throw std::runtime_error("native XTest unavailable");
        {platform::NativeOptions options{960,640,false,1.5F,argv[2]};options.fixed_delta_seconds=1.F/60;platform::NativeHost* current{};platform::NativeHost host(options,&o,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){o.facts=platform::DrawShellFrame(*current,o.state,o.requests,theme,appearance);});current=&host;host.Run();}Check(o.done && o.cleaned,"actual native lock journey completed");
        app::RecoverySession setup_session(std::filesystem::path(argv[3]).parent_path()/"setup.sqlite");
        Observer setup;
        setup.setup_only=true;setup.requests.recovery=&setup_session;
        setup.state.documents=a::PublicFixtureDocuments();setup.display=display;
        setup.motion=o.motion;setup.button=o.button;setup.key=o.key;
        {platform::NativeOptions options{960,640,false,1.5F,argv[2]};options.fixed_delta_seconds=1.F/60;platform::NativeHost* current{};platform::NativeHost host(options,&setup,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){setup.facts=platform::DrawShellFrame(*current,setup.state,setup.requests,theme,appearance);});current=&host;host.Run();}
        Check(setup.done && setup.cleaned,"native protection setup and Cancel settled");
    }catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<e.what()<<'\n';}
    if(display)XCloseDisplay(display);if(xtst)dlclose(xtst);std::ofstream(out/"result.json")<<"{\"selected\":1,\"executed\":1,\"passed\":"<<(failures?0:1)<<",\"failed\":"<<(failures?1:0)<<",\"skipped\":0,\"assertions\":"<<checks<<",\"stressWalletSwitches\":"<<(!failures?Observer::Rounds:0)<<",\"assertionFailures\":"<<failures<<"}\n";return failures?1:0;
}
