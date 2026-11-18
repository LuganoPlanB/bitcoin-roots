// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <chrono>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace roots::gui;namespace a=app;namespace sh=ui::shell;
namespace {
constexpr std::string_view Password="fixture-native-lock-password";
int checks{},failures{};
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<why<<'\n';}}
struct Requests final:a::Requests {a::RecoverySession* recovery{};int effects{};a::RecoverySession* Recovery() override{return recovery;}a::Availability CanSubmit(const a::Request&) const override{return {false,"No daemon mutation port in native lock journey."};}a::Receipt Submit(const a::Request&) override{++effects;return {false,{}};}};
struct Observer final:platform::NativeObserver {
    sh::State state;sh::Facts facts;Requests requests;Display* display{};std::filesystem::path out,metadata_path;bool done{},cleaned{};uint64_t old_generation{},old_tab{};
    int(*motion)(Display*,int,int,int,unsigned long){};int(*button)(Display*,unsigned int,Bool,unsigned long){};int(*key)(Display*,unsigned int,Bool,unsigned long){};
    const auto static inline Timeout=std::chrono::seconds(15);const std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    bool CaptureFramebuffer() const override{return true;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override{if(std::chrono::steady_clock::now()-started>Timeout)throw std::runtime_error("native lock deadline");}
    void Key(KeySym sym,bool down){Check(key(display,XKeysymToKeycode(display,sym),down,0),"native lock keyboard event");XSync(display,False);}
    void Type(std::string_view text,int frame,int start){const int at=(frame-start)/2;if(frame>=start && at<int(text.size()))Key(text[at]=='-'?XK_minus:KeySym(text[at]),(frame-start)%2==0);}
    void Click(platform::NativeHost& host,const char* id,bool down){const auto& item=facts.document_actions.at(id);const auto& r=item.rect;Check(item.visible && r[0]>=0 && r[1]>=0 && r[2]<=960 && r[3]<=640,"lock action reachable at150percent");int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);Check(motion(display,-1,x+int((r[0]+r[2])/2),y+int((r[1]+r[3])/2),0),"native lock pointer");Check(button(display,1,down,0),"native lock button");XSync(display,False);}
    void Capture(const platform::NativeFrame& f,const char* name){std::ofstream file(out/(std::string(name)+".ppm"),std::ios::binary);file<<"P6\n"<<f.framebuffer_width<<' '<<f.framebuffer_height<<"\n255\n";for(int y=f.framebuffer_height-1;y>=0;--y)for(int x=0;x<f.framebuffer_width;++x)file.write(reinterpret_cast<const char*>(f.rgba.data()+(size_t(y)*f.framebuffer_width+x)*4),3);Check(bool(file),"native lock framebuffer retained");const auto snapshot=state.presentation.Snapshot();Check(snapshot.find(Password)==std::string::npos && snapshot.find("bad")==std::string::npos,"password absent from accessibility publication");std::ofstream(out/(std::string(name)+".semantics.txt"))<<snapshot;}
    void Frame(platform::NativeHost& host,const platform::NativeFrame& f) override{
        const int n=f.rendered_frames;
        if(n==2){const auto packet=state.presentation.exchange.Published();old_generation=packet->generation;for(const auto& entry:packet->nodes)if(entry.role==ui::components::ax::SemanticRole::Tab){old_tab=entry.id;break;}Check(old_tab!=0,"pre-lock accessibility tab exists");}
        if(n==3){Key(XK_Control_L,true);Key(XK_l,true);}if(n==4){Key(XK_l,false);Key(XK_Control_L,false);}
        if(n==7){Check(requests.recovery->Locked() && state.documents.Documents().empty(),"native shortcut locks and removes documents");Check(facts.tabs.empty() && state.presentation.Snapshot().find("Public watch-only wallet")==std::string::npos && state.presentation.Snapshot().find("Public fixture note")==std::string::npos,"locked tabs/notes absent from navigation and accessibility");Check(state.presentation.exchange.QueueNext(old_generation,old_tab,ui::components::ax::SemanticAction::Select).second==ui::components::ActionResult::Stale,"retired native accessibility action cannot reopen a locked tab");Check(f.decorated && f.font_texture && !f.gl_error && f.effective_scale==1.5F,"actual Linux960x640150lock renderer");Capture(f,"wallet-locked");}
        Type("bad",n,9);
        if(n==16 || n==17)Click(host,"reveal-password",n==16);
        if(n==18)Check(state.lock_panel.reveal && state.presentation.Snapshot().find("bad")==std::string::npos,"deliberate reveal never exports password through accessibility");
        if(n==20 || n==21)Click(host,"unlock-workspace",n==20);
        if(n==25){Check(requests.recovery->Locked() && !state.lock_panel.error.empty() && state.documents.Documents().empty(),"wrong password keeps protected documents concealed");Check(state.lock_panel.password[0]==0 && !state.lock_panel.reveal,"failed attempt clears secret and resets reveal");Capture(f,"unlock-failure");}
        Type(Password,n,28);
        const int ready=28+int(Password.size())*2+4;
        if(n==ready || n==ready+1)Click(host,"unlock-workspace",n==ready);
        if(n==ready+6){Check(requests.recovery->Locked() && requests.recovery->ConversionConflict() && state.documents.Documents().empty(),"native authenticated conversion conflict retains concealment");Capture(f,"conversion-conflict");}
        const int again=ready+9;Type(Password,n,again);const int resolved=again+int(Password.size())*2+4;
        if(n==resolved || n==resolved+1)Click(host,"resolve-conversion",n==resolved);
        if(n==resolved+6){Check(!std::filesystem::exists(metadata_path) && std::filesystem::exists(metadata_path.string()+".conversion-backup.protected"),"native deliberate resolution preserves other copy encrypted");Check(!requests.recovery->Locked() && state.documents.Documents().size()==3,"correct native password unlocks existing independent documents");Check(state.documents.Find(1)->sections[1].note.draft()=="Protected native draft" && state.privacy.hidden,"unlock restores draft with prior amount privacy");Check(requests.effects==0,"GUI lock/unlock sends no daemon request");Check(state.presentation.Tree().Validate(),"native unlocked semantic graph validates");done=true;host.RequestClose();}
    }
    void Teardown(const platform::NativeTeardown& t) override{cleaned=t.renderer_cleared && t.platform_cleared && t.textures_released && t.imgui_cleared && t.context_cleared;Check(cleaned,"native lock resources settled");}
};
}
int main(int argc,char** argv){if(argc!=4)return 2;const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);Display* display{};void* xtst{};
    try{const char* name=getenv("DISPLAY"),*owned=getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");if(!name || !owned || std::string(name)!=owned)throw std::runtime_error("owned display required");a::RecoverySession session(argv[3]);Observer o;o.out=out;o.requests.recovery=&session;o.state.documents=a::PublicFixtureDocuments();o.state.documents.Find(1)->sections[1].note.SetDraft("Protected native draft");o.state.privacy.hidden=true;session.SetPresentation(true,0);Check(session.Enable(o.state.documents),"native conversion fixture opts in explicitly");std::ifstream source(argv[3],std::ios::binary);const std::string interrupted{std::istreambuf_iterator<char>(source),{}};source.close();Check(session.Protect(o.state.documents,Password),"explicit synthetic native protection enabled");{std::ofstream copy(argv[3],std::ios::binary);copy.write(interrupted.data(),interrupted.size());Check(bool(copy),"owned native interrupted-conversion fixture retained");}std::filesystem::permissions(argv[3],std::filesystem::perms::owner_read|std::filesystem::perms::owner_write);o.metadata_path=argv[3];display=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);if(!display || !xtst)throw std::runtime_error("native input provider unavailable");o.display=display;o.motion=reinterpret_cast<decltype(o.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));o.button=reinterpret_cast<decltype(o.button)>(dlsym(xtst,"XTestFakeButtonEvent"));o.key=reinterpret_cast<decltype(o.key)>(dlsym(xtst,"XTestFakeKeyEvent"));if(!o.motion || !o.button || !o.key)throw std::runtime_error("native XTest unavailable");
        {platform::NativeOptions options{960,640,false,1.5F,argv[2]};options.fixed_delta_seconds=1.F/60;platform::NativeHost* current{};platform::NativeHost host(options,&o,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){o.facts=platform::DrawShellFrame(*current,o.state,o.requests,theme,appearance);});current=&host;host.Run();}Check(o.done && o.cleaned,"actual native lock journey completed");
    }catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<e.what()<<'\n';}
    if(display)XCloseDisplay(display);if(xtst)dlclose(xtst);std::ofstream(out/"result.json")<<"{\"selected\":1,\"executed\":1,\"passed\":"<<(failures?0:1)<<",\"failed\":"<<(failures?1:0)<<",\"skipped\":0,\"assertions\":"<<checks<<",\"assertionFailures\":"<<failures<<"}\n";return failures?1:0;
}
