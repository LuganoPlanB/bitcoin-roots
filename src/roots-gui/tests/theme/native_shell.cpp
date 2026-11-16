// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/native_host.h"
#include "ui/shell/workspace.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <cstdio>
#include <stdexcept>
using namespace roots::gui;
namespace sh=ui::shell;namespace a=app;
namespace {
int assertions{},failures{};
void Check(bool value,const char* label){++assertions;if(!value){++failures;std::fprintf(stderr,"FAIL %s\n",label);}}
struct FixtureHost final:a::Requests { a::Availability CanSubmit(const a::Request&) const override{return {true,"Explicit effect-free fixture rejection handler."};}
    int requests{};
    a::Receipt Submit(const a::Request& r) override {++requests;const auto* spec=a::Spec(r.command);return {false,spec?spec->dependency:"Fixture service unavailable."};}
};
struct Observer final:platform::NativeObserver {
    sh::State state;sh::Facts facts;FixtureHost service;
    std::filesystem::path output;std::string id,variant,rendered;
    int expected{},target{};bool wrong{},captured{},cleaned{};
    Display* display{};int (*motion)(Display*,int,int,int,unsigned long){};int (*button)(Display*,unsigned int,Bool,unsigned long){};int (*key)(Display*,unsigned int,Bool,unsigned long){};
    const auto Start() const { return std::chrono::steady_clock::now(); }
    std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
    bool scrolled{},hidden_requested{};int hidden_capture{};float hidden_target{};
    bool CaptureFramebuffer() const override{return true;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override {if(std::chrono::steady_clock::now()-start>std::chrono::seconds(12))throw std::runtime_error("shell native deadline");}
    void Frame(platform::NativeHost& host,const platform::NativeFrame& f) override {
        if(f.rendered_frames==3 || f.rendered_frames==4) {
            const bool down=f.rendered_frames==3;
            if(variant=="hidden") {
                for(KeySym sym:down?std::initializer_list<KeySym>{XK_Control_L,XK_Shift_L,XK_H}:std::initializer_list<KeySym>{XK_H,XK_Shift_L,XK_Control_L})Check(key(display,XKeysymToKeycode(display,sym),down,0),"native hide-amount shortcut");
            } else if(variant=="transaction") {
                Check(key(display,XKeysymToKeycode(display,XK_space),down,0),"native transaction tab keyboard activation");
            } else if(variant=="long") {
                int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);
                Check(motion(display,-1,x+int(facts.layout.tabs.x+78*f.effective_scale),y+int(facts.layout.tabs.y+18*f.effective_scale),0),"native long-name tab pointer");Check(button(display,1,down,0),"native long-name tab click");
            } else if(variant!="transaction" && variant!="workspaceEmpty") {
                const auto& r=facts.navigation[target].rect;int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);
                Check(motion(display,-1,x+int((r[0]+r[2])/2),y+int((r[1]+r[3])/2),0),"native section pointer");Check(button(display,1,down,0),"native section click");
            }
            XFlush(display);
        }
        if(f.rendered_frames==7 && facts.horizontal_scroll && variant!="long") {
            const auto& r=facts.table_hover;int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);
            Check(motion(display,-1,x+int((r[0]+r[2])/2),y+int((r[1]+r[3])/2),0),"native horizontal table pointer");
            Check(button(display,7,True,0)&&button(display,7,False,0),"native horizontal wheel");XFlush(display);scrolled=true;
        }
        if(f.rendered_frames>=9 && variant=="hidden" && !hidden_requested && facts.table_scroll_x>0) {
            // First real wheel event establishes this host's scroll step;
            // continue with real events to inspect the amount column itself.
            const int count=std::clamp(int((800*f.effective_scale-facts.table_scroll_x)/facts.table_scroll_x),0,100);
            hidden_target=std::min(facts.table_scroll_max_x,facts.table_scroll_x*(count+1));hidden_requested=true;
            for(int i=0;i<count;++i)Check(button(display,7,True,0)&&button(display,7,False,0),"native hidden amount-column inspection wheel");XSync(display,False);
        }
        if(variant=="transaction" && (f.rendered_frames==10 || f.rendered_frames==11 || f.rendered_frames==13 || f.rendered_frames==14)) {
            const bool left=f.rendered_frames<13,down=f.rendered_frames==10 || f.rendered_frames==13;
            Check(key(display,XKeysymToKeycode(display,left?XK_Left:XK_Right),down,0),"actual native keyboard traversal returns to transaction tab");XFlush(display);
        }
        if(variant=="hidden" && !hidden_capture && hidden_requested && std::abs(facts.table_scroll_x-hidden_target)<1.F)hidden_capture=f.rendered_frames+2;
        if(variant=="hidden" && !hidden_capture && f.rendered_frames>=60){Check(false,"bounded native hidden amount-column position");hidden_capture=f.rendered_frames;}
        if(f.rendered_frames!=(variant=="hidden"?hidden_capture:variant=="transaction"?18:14))return;
        if(scrolled)Check(facts.table_scroll_x>0,"actual horizontal table position changed");
        if(variant=="transaction")Check(facts.tabs.at(3).focused && ImGui::GetIO().NavVisible,"actual native transaction tab focus");
        Check(facts.section==expected,"intended section target selected");
        Check(facts.primary.visible && facts.primary.rect[0]>=0 && facts.primary.rect[1]>=0 && facts.primary.rect[2]<=f.width && facts.primary.rect[3]<=f.height,"actual primary reachable in native viewport");
        Check(!facts.accounts_submitted && facts.layout.accounts.w==0 && facts.layout.content.x==facts.layout.rail.x+facts.layout.rail.w,"absent account service allocates zero space");
        Check(facts.layout.rail.w==(f.width<1100?96:180)*f.effective_scale,"actual responsive rail width");
        Check(state.presentation.Tree().Validate(),"actual shell amount annotations validate");
        if(variant=="hidden")Check(state.privacy.hidden && rendered.find("0.01000000")==std::string::npos,"real native hide shortcut redacts amount text");
        if(variant=="locked")Check(rendered.find("Public fixture note")==std::string::npos && rendered.find("0.01000000")==std::string::npos && facts.primary.disabled,"locked fixture removes content");
        if(variant=="incompleteSend")Check(facts.primary.disabled && rendered.find("Complete the wallet policy")!=std::string::npos,"incomplete dependency explanation");
        if(variant=="incomplete")Check(facts.section==5 && rendered.find("Wallet policy is incomplete")!=std::string::npos,"new incomplete wallet starts Settings");
        Check(service.requests==0,"navigation capture performs no host effect");
        Check(f.decorated && f.frame_top>0 && f.font_texture && !f.gl_error,"actual native decorated renderer");
        std::ofstream ppm(output/(id+".ppm"),std::ios::binary);ppm<<"P6\n"<<f.framebuffer_width<<' '<<f.framebuffer_height<<"\n255\n";
        for(int y=f.framebuffer_height-1;y>=0;--y)for(int x=0;x<f.framebuffer_width;++x)ppm.write(reinterpret_cast<const char*>(f.rgba.data()+(size_t(y)*f.framebuffer_width+x)*4),3);
        Check(bool(ppm),"native framebuffer saved");std::ofstream(output/(id+".presentation.txt"))<<rendered;std::ofstream(output/(id+".semantics.txt"))<<state.presentation.Snapshot();
        std::ofstream meta(output/(id+".json"));meta<<std::boolalpha<<"{\"id\":\""<<id<<"\",\"width\":"<<f.width<<",\"height\":"<<f.height<<",\"effectiveScale\":"<<f.effective_scale
            <<",\"tabButtonHeight\":"<<(facts.tabs.empty()?0:facts.tabs.begin()->second.rect[3]-facts.tabs.begin()->second.rect[1])
            <<",\"hasDocuments\":"<<!facts.tabs.empty()<<",\"tabFocused\":"<<(facts.tabs.contains(3)&&facts.tabs.at(3).focused)
            <<",\"navVisible\":"<<ImGui::GetIO().NavVisible
            <<",\"tabFocusPrimary\":["<<(facts.tabs.contains(3)?facts.tabs.at(3).rect[0]:0)<<','<<(facts.tabs.contains(3)?facts.tabs.at(3).rect[1]:0)<<','<<(facts.tabs.contains(3)?facts.tabs.at(3).rect[2]:0)<<','<<(facts.tabs.contains(3)?facts.tabs.at(3).rect[3]:0)<<']'
            <<",\"section\":"<<facts.section<<",\"expectedSection\":"<<expected<<",\"targetAccepted\":"<<(facts.section==expected)<<",\"accountWidth\":"<<facts.layout.accounts.w
            <<",\"railX\":"<<facts.layout.rail.x<<",\"railWidth\":"<<facts.layout.rail.w<<",\"contentX\":"<<facts.layout.content.x<<",\"tabsHeight\":"<<facts.layout.tabs.h<<",\"statusHeight\":"<<facts.layout.status.h
            <<",\"primaryVisible\":"<<facts.primary.visible<<",\"primaryDisabled\":"<<facts.primary.disabled<<",\"primary\":["<<facts.primary.rect[0]<<','<<facts.primary.rect[1]<<','<<facts.primary.rect[2]<<','<<facts.primary.rect[3]
            <<"],\"privacyHidden\":"<<state.privacy.hidden<<",\"tableScrollX\":"<<facts.table_scroll_x<<",\"horizontalScroll\":"<<facts.horizontal_scroll<<",\"fixedDeltaSeconds\":"<<ImGui::GetIO().DeltaTime<<"}\n";
        captured=true;host.RequestClose();
    }
    void Teardown(const platform::NativeTeardown& t) override {
        cleaned=t.renderer_cleared&&t.platform_cleared&&t.textures_released&&t.imgui_cleared&&t.context_cleared;Check(cleaned,"shell native resources released");
        std::ofstream(output/(id+".teardown.json"))<<"{\"cleaned\":"<<(cleaned?"true":"false")<<"}\n";
    }
};
}
int main(int argc,char** argv)
{
    if(argc!=3 && argc!=4)return 2;const bool wrong=argc==4;const int selected=wrong?1:204;int executed{},passed{};
    Display* display{};void* xtst{};const std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);
    try {
        const char* name=std::getenv("DISPLAY"),*owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");if(!name || !owned || std::string(name)!=owned)throw std::runtime_error("owned authenticated display required");
        display=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);if(!display||!xtst)throw std::runtime_error("XTest unavailable");
        for(int width:{1200,960})for(bool dark:{false,true})for(float scale:{1.F,1.5F,2.F})for(const char* variant:{"section0","section1","section2","section3","section4","section5","loading","empty","error","stale","locked","hidden","long","incomplete","incompleteSend","transaction","workspaceEmpty"}) {
            if(wrong && (width!=960 || !dark || scale!=2 || std::string(variant)!="section0"))continue;
            Observer o;o.output=out;o.variant=variant;o.display=display;o.wrong=wrong;
            o.motion=reinterpret_cast<decltype(o.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));o.button=reinterpret_cast<decltype(o.button)>(dlsym(xtst,"XTestFakeButtonEvent"));o.key=reinterpret_cast<decltype(o.key)>(dlsym(xtst,"XTestFakeKeyEvent"));if(!o.motion||!o.button||!o.key)throw std::runtime_error("XTest symbols unavailable");
            o.state.documents=a::PublicFixtureDocuments();o.expected=o.variant.starts_with("section")?o.variant.back()-'0':o.variant=="incomplete"?5:o.variant=="incompleteSend"?1:(o.variant=="transaction" || o.variant=="workspaceEmpty")?-1:0;
            o.target=wrong?1:std::max(0,o.expected);o.state.reveal_navigation=o.target;
            if(o.variant=="incomplete" || o.variant=="incompleteSend") {
                const auto binding=o.state.documents.Find(1)->binding.value();o.state.documents=a::DocumentRegistry{};o.state.documents.OpenWallet("New public wallet",binding,false);
            } else if(o.variant=="transaction"){o.state.documents.Activate(3);o.state.focus_tab=3;}
            else if(o.variant.starts_with("section"))o.state.documents.Navigate(o.expected==0?a::Section::Settings:a::Section::Transactions);
            if(o.variant=="workspaceEmpty")o.state.documents=a::DocumentRegistry{};
            auto* d=o.state.documents.Active();
            if(d) {
            if(o.variant=="loading")d->observation=a::ObservationState::Loading;
            if(o.variant=="empty")d->observation=a::ObservationState::Empty;
            if(o.variant=="error")d->observation=a::ObservationState::Error;
            if(o.variant=="stale")d->observation=a::ObservationState::Stale;
            if(o.variant=="locked")d->observation=a::ObservationState::Locked;
            if(o.variant=="hidden")o.state.unit=1;
            if(o.variant=="long")d->name="Gemeinschaftskasse##2026 — Öffentliche Beobachtungen und länger übersetzter Walletname";
            for(auto& state:d->sections)state.scroll_y=d->complete && d->observation!=a::ObservationState::Empty && d->observation!=a::ObservationState::Locked && scale>1?160*scale:0;
            d->transaction.scroll_y=scale>1?160*scale:0;
            }
            o.id="shell-"+std::to_string(width)+(dark?"-dark-":"-light-")+std::to_string(int(scale*100))+"-"+variant;
            const int before=failures;
            {platform::NativeOptions options{width,width==1200?820:640,dark,scale,argv[2]};options.fixed_delta_seconds=1.F/60;
             platform::NativeHost host(options,&o,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){
                 auto& p=ImGui::GetPlatformIO();const auto callback=p.Platform_SetClipboardTextFn;void* const user=p.Platform_ClipboardUserData;p.Platform_ClipboardUserData=&o;
                 p.Platform_SetClipboardTextFn=[](ImGuiContext*,const char* text){static_cast<Observer*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->rendered=text;};
                 ImGui::LogToClipboard();o.facts=sh::DrawWorkspace(o.state,o.service,theme,appearance);ImGui::LogFinish();p.Platform_SetClipboardTextFn=callback;p.Platform_ClipboardUserData=user;
             });host.Run();}
            Check(o.captured&&o.cleaned,"shell scenario and teardown complete");++executed;if(failures==before)++passed;
        }
    }catch(const std::exception& error){++failures;std::fprintf(stderr,"FAIL %s\n",error.what());}
    if(display)XCloseDisplay(display);if(xtst)dlclose(xtst);
    std::ofstream(out/"result.json")<<"{\"selected\":"<<selected<<",\"executed\":"<<executed<<",\"passed\":"<<passed<<",\"failed\":"<<(executed-passed)<<",\"skipped\":0,\"assertions\":"<<assertions<<",\"assertionFailures\":"<<failures<<"}\n";
    return failures || executed!=selected?1:0;
}
