// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <chrono>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace roots::gui;
namespace sh=ui::shell;namespace a=app;
namespace {
int checks{},failures{};
void Check(bool ok,const char* why){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<why<<'\n';}}
struct Requests final:a::Requests {a::RecoverySession* recovery{};int effects{};a::RecoverySession* Recovery() override{return recovery;}a::Receipt Submit(const a::Request&) override{++effects;return {false,"No RPC or host mutation port in recovery journey."};}};
struct Observer final:platform::NativeObserver {
    sh::State state;sh::Facts facts;Requests requests;Display* display{};std::filesystem::path out;
    int(*motion)(Display*,int,int,int,unsigned long){};int(*button)(Display*,unsigned int,Bool,unsigned long){};
    bool captured{},cleaned{};std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    bool CaptureFramebuffer() const override{return true;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override{if(std::chrono::steady_clock::now()-started>std::chrono::seconds(12))throw std::runtime_error("recovery native deadline");}
    void Click(platform::NativeHost& host,const char* key,bool down){const auto& item=facts.document_actions.at(key);const auto& r=item.rect;Check(item.visible && !item.disabled && r[0]>=0 && r[1]>=0 && r[2]<=960 && r[3]<=640,"recovery action reachable at150percent");int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);Check(motion(display,-1,x+int((r[0]+r[2])/2),y+int((r[1]+r[3])/2),0),"native pointer");Check(button(display,1,down,0),"native recovery click");XSync(display,False);}
    void Capture(const platform::NativeFrame& f,const char* name){std::ofstream file(out/(std::string(name)+".ppm"),std::ios::binary);file<<"P6\n"<<f.framebuffer_width<<' '<<f.framebuffer_height<<"\n255\n";for(int y=f.framebuffer_height-1;y>=0;--y)for(int x=0;x<f.framebuffer_width;++x)file.write(reinterpret_cast<const char*>(f.rgba.data()+(size_t(y)*f.framebuffer_width+x)*4),3);Check(bool(file),"native recovery framebuffer captured");std::ofstream(out/(std::string(name)+".semantics.txt"))<<state.presentation.Snapshot();}
    void Frame(platform::NativeHost& host,const platform::NativeFrame& f) override{
        const auto n=f.rendered_frames;
        if(n==3){Check(state.documents.Documents().empty() && requests.recovery->Pending(),"startup prompts without reopening");Check(f.decorated && f.font_texture && !f.gl_error && f.width==960 && f.height==640 && f.effective_scale==1.5F,"actual native960x640150renderer");Capture(f,"session-recovery");}
        if(n==4 || n==5)Click(host,"restore-session",n==4);
        if(n==9){Check(state.documents.Documents().size()==4 && !requests.recovery->Pending(),"native restore opens two wallets and two transactions");Check(state.documents.Find(1)->binding!=state.documents.Find(2)->binding,"wallet binding isolation");for(const auto& d:state.documents.Documents())Check(d.observation==a::ObservationState::Offline,"native restore grants no live authority");state.recovery_dialog.open=true;state.recovery_dialog.initialFocus=true;}
        if(n==13 || n==14)Click(host,"clear-public-draft",n==13);
        if(n==18){Check(state.documents.Find(4)->transaction.note.draft().empty() && state.documents.Find(4)->selected_coins.empty(),"native Clear removes public draft");Capture(f,"clear-draft");}
        if(n==20 || n==21)Click(host,"recover-last",n==20);
        if(n==25){Check(state.documents.Find(4)->transaction.note.draft()=="Independent transaction two" && state.documents.Find(4)->selected_coins.size()==1,"native recover restores exact public draft and selection");Check(state.privacy.hidden && state.unit==2 && state.presentation.Snapshot().find("0.01000000")==std::string::npos,"native restore retains amount privacy before document disclosure");Check(requests.effects==0,"recovery prompts/actions dispatch no host RPC mutation");Check(state.presentation.Tree().Validate(),"recovery accessibility tree validates");captured=true;host.RequestClose();}
    }
    void Teardown(const platform::NativeTeardown& t) override{cleaned=t.renderer_cleared && t.platform_cleared && t.textures_released && t.imgui_cleared && t.context_cleared;Check(cleaned,"native recovery resources released");}
};
}
int main(int argc,char** argv){if(argc!=4)return 2;const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);Display* display{};void* xtst{};
    try{const char* name=getenv("DISPLAY"),*owned=getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");if(!name || !owned || std::string(name)!=owned)throw std::runtime_error("owned display required");
        const std::filesystem::path path=argv[3];{a::RecoverySession seed(path);a::DocumentRegistry docs;const NetworkIdentity network{*Hash256::Parse(std::string(64,'1'))};docs.OpenWallet("Daemon wallet A",{*BackendSessionId::Make("profile-A"),*WalletId::Make("wallet-A"),network},true);docs.OpenWallet("Daemon wallet B",{*BackendSessionId::Make("profile-B"),*WalletId::Make("wallet-B"),network},true);docs.OpenTransaction("Transaction one");docs.OpenTransaction("Transaction two");docs.Find(1)->sections[1].note.SetDraft("Independent wallet A");docs.Find(2)->sections[2].note.SetDraft("Independent wallet B");docs.Find(3)->transaction.note.SetDraft("Independent transaction one");docs.Find(4)->transaction.note.SetDraft("Independent transaction two");docs.Find(4)->selected_coins={{*Hash256::Parse(std::string(64,'a')),7}};seed.SetPresentation(true,2);Check(seed.Enable(docs),"explicit native fixture optin save");}
        a::RecoverySession recovery(path);Check(recovery.Start() && recovery.Pending(),"native startup loads recovery candidate");Observer o;o.out=out;o.requests.recovery=&recovery;display=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);if(!display || !xtst)throw std::runtime_error("native XTest unavailable");o.display=display;o.motion=reinterpret_cast<decltype(o.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));o.button=reinterpret_cast<decltype(o.button)>(dlsym(xtst,"XTestFakeButtonEvent"));if(!o.motion || !o.button)throw std::runtime_error("native input symbols unavailable");
        {platform::NativeOptions options{960,640,false,1.5F,argv[2]};options.fixed_delta_seconds=1.F/60;platform::NativeHost* current{};platform::NativeHost host(options,&o,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){o.facts=platform::DrawShellFrame(*current,o.state,o.requests,theme,appearance);});current=&host;host.Run();}
        Check(o.captured && o.cleaned,"native recovery journey completed");
    }catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<e.what()<<'\n';}
    if(display)XCloseDisplay(display);if(xtst)dlclose(xtst);std::ofstream(out/"result.json")<<"{\"selected\":1,\"executed\":1,\"passed\":"<<(failures?0:1)<<",\"failed\":"<<(failures?1:0)<<",\"skipped\":0,\"assertions\":"<<checks<<",\"assertionFailures\":"<<failures<<"}\n";return failures?1:0;
}
