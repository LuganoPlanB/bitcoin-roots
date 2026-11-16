// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/native_host.h"
#include "ui/theme/gallery.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <stdexcept>
using namespace roots::gui;
namespace {
int checks{},failures{};
void Check(bool ok,const char* id) { ++checks;if (!ok) { ++failures;std::fprintf(stderr,"FAIL %s\n",id); } }
struct Observer final : platform::NativeObserver {
    std::filesystem::path output;std::string id,state;
    ui::ThemeGalleryFacts facts;ui::ThemeGalleryState gallery;
    Display* display{};
    int (*motion)(Display*,int,int,int,unsigned long){};
    int (*button)(Display*,unsigned int,Bool,unsigned long){};
    int (*key)(Display*,unsigned int,Bool,unsigned long){};
    bool captured{},cleaned{},probe{},primary_seen{};
    int capture_frame{},tab_release{},tab_presses{};
    const std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    bool CaptureFramebuffer() const override { return true; }
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override {
        if (std::chrono::steady_clock::now()-start>std::chrono::seconds(12)) throw std::runtime_error("gallery deadline");
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override {
        const int n=frame.rendered_frames;
        const bool focus=state=="focus";
        if (n==2 || n==6) {
            int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);
            const bool primary=n==6 && (state=="hover" || state=="pressed");
            const int target_x=n==2?int((facts.input[0]+facts.input[2])/2):primary?int((facts.primary[0]+facts.primary[2])/2):10;
            const int target_y=n==2?int((facts.input[1]+facts.input[3])/2):primary?int((facts.primary[1]+facts.primary[3])/2):10;
            Check(motion(display,-1,x+target_x,y+target_y,0),"native pointer move");
            if (n==2) {
                Check(button(display,1,true,0),"native input click establishes a different focus target");
            }
            XFlush(display);
        }
        if (n==3) {
            Check(button(display,1,false,0),"native input click release");XFlush(display);
        }
        if (n==4 || n==5) { Check(key(display,XKeysymToKeycode(display,XK_Return),n==4,0),"native Enter commits fixture text input");XFlush(display); }
        if (n==7 && state=="pressed") { Check(button(display,1,true,0),"native primary press");XFlush(display); }
        if (tab_release==n) { Check(key(display,XKeysymToKeycode(display,XK_Tab),false,0),"native Tab release");XFlush(display);tab_release=0; }
        if (focus && !capture_frame && n>=7) {
            const bool target=facts.focused && ImGui::GetIO().NavVisible;
            if (target) {
                primary_seen=true;
                if (!probe) capture_frame=n+2;
            }
            // The negative probe first reaches the real primary, then makes
            // one more real Tab move. Global NavVisible must not pass it.
            if (!capture_frame && (!target || probe) && (n-7)%3==0) {
                Check(key(display,XKeysymToKeycode(display,XK_Tab),true,0),"native Tab traversal");XFlush(display);
                ++tab_presses;tab_release=n+1;
                if (probe && primary_seen) capture_frame=n+4;
            }
            if (n>=80 && !capture_frame) { Check(false,"bounded native target-focus traversal");capture_frame=n; }
        }
        if (!focus && !capture_frame) capture_frame=12;
        if (n!=capture_frame) return;
        Check(facts.visible,"primary visible at stress scale");
        Check(facts.primary[0]>=0 && facts.primary[1]>=0 && facts.primary[2]<=frame.width && facts.primary[3]<=frame.height,"primary fully reachable");
        if (state=="normal") Check(!facts.hovered && !facts.active && !facts.focused && !ImGui::GetIO().NavVisible,"normal is not primary keyboard focus");
        if (state=="hover") Check(facts.hovered && !facts.active && !ImGui::GetIO().NavVisible,"actual pointer hover without keyboard focus");
        if (state=="pressed") Check(facts.hovered && facts.active && !ImGui::GetIO().NavVisible,"actual pointer press without keyboard focus");
        if (focus) {
            Check(tab_presses>0,"focus target reached using actual Tab traversal");
            if (probe) Check(primary_seen && !facts.focused && ImGui::GetIO().NavVisible,"wrong-target precondition: departed primary while navigation remains visible");
            Check(facts.focused && ImGui::GetIO().NavVisible && !facts.hovered && !facts.active,"intended primary has actual native keyboard focus");
        }
        Check(!facts.requested,"no fixture service side effects");
        Check(frame.decorated && frame.frame_top>0 && frame.font_texture && !frame.gl_error,"native decorated renderer");
        Check(frame.rgba.size()==std::size_t(frame.framebuffer_width)*frame.framebuffer_height*4,"real framebuffer");
        std::ofstream image(output/(id+".ppm"),std::ios::binary);
        image<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for (int y=frame.framebuffer_height-1;y>=0;--y) for (int x=0;x<frame.framebuffer_width;++x)
            image.write(reinterpret_cast<const char*>(frame.rgba.data()+(std::size_t(y)*frame.framebuffer_width+x)*4),3);
        Check(bool(image),"capture written");
        std::ofstream meta(output/(id+".json"));
        meta<<std::boolalpha<<"{\"id\":\""<<id<<"\",\"width\":"<<frame.framebuffer_width<<",\"height\":"<<frame.framebuffer_height
            <<",\"dpi\":"<<frame.dpi_x<<",\"effectiveScale\":"<<frame.effective_scale<<",\"hovered\":"<<facts.hovered
            <<",\"active\":"<<facts.active<<",\"focused\":"<<facts.focused<<",\"navVisible\":"<<ImGui::GetIO().NavVisible
            <<",\"targetFocusAccepted\":"<<(facts.focused && ImGui::GetIO().NavVisible)<<",\"tabPresses\":"<<tab_presses
            <<",\"primary\":["<<facts.primary[0]<<','<<facts.primary[1]<<','<<facts.primary[2]<<','<<facts.primary[3]<<']'
            <<",\"renderer\":\""<<host.Renderer()<<"\"}\n";
        if (state=="pressed") { button(display,1,false,0);XFlush(display); }
        captured=true;host.RequestClose();
    }
    void Teardown(const platform::NativeTeardown& t) override {
        cleaned=t.renderer_cleared && t.platform_cleared && t.textures_released && t.imgui_cleared && t.context_cleared;
        Check(cleaned,"resources settled");std::ofstream record(output/(id+".teardown.json"));record<<"{\"cleaned\":"<<(cleaned?"true":"false")<<"}\n";
    }
};
}
int main(int argc,char** argv)
{
    if (argc!=3 && argc!=4) return 2;
    const bool probe=argc==4 && std::string(argv[3])=="wrong-target";
    if (argc==4 && !probe) return 2;
    const int selected=probe?1:48;
    int executed{},passed{};Display* display{};void* xtst{};
    const std::filesystem::path output(argv[1]);std::filesystem::create_directories(output);
    try {
        const char* actual=std::getenv("DISPLAY"),*owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if (!actual || !owned || std::string(actual)!=owned) throw std::runtime_error("owned authenticated display required");
        display=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);
        if (!display || !xtst) throw std::runtime_error("native XTest required");
        for (int width : {1200,960}) for (bool dark : {false,true}) for (float scale : {1.F,1.5F,2.F}) for (const char* state : {"normal","hover","pressed","focus"}) {
            if (probe && (width!=960 || !dark || scale!=2 || std::string(state)!="focus")) continue;
            Observer observer;observer.display=display;observer.output=output;observer.state=state;
            observer.probe=probe;observer.gallery.initial_focus=false;
            observer.id="theme-gallery-"+std::to_string(width)+(dark?"-dark-":"-light-")+std::to_string(int(scale*100))+"-"+state;
            observer.motion=reinterpret_cast<decltype(observer.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));
            observer.button=reinterpret_cast<decltype(observer.button)>(dlsym(xtst,"XTestFakeButtonEvent"));
            observer.key=reinterpret_cast<decltype(observer.key)>(dlsym(xtst,"XTestFakeKeyEvent"));
            if (!observer.motion || !observer.button || !observer.key) throw std::runtime_error("XTest API missing");
            const auto before=failures;
            { platform::NativeHost host({width,width==1200?820:640,dark,scale,argv[2]},&observer,
                [&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance) { observer.facts=ui::DrawThemeGallery(theme,appearance,observer.gallery); });host.Run(); }
            Check(observer.captured && observer.cleaned,"capture and teardown");++executed;if (failures==before) ++passed;
        }
    } catch (const std::exception& e) { ++failures;std::fprintf(stderr,"FAIL %s\n",e.what()); }
    if (display) XCloseDisplay(display);if (xtst) dlclose(xtst);
    std::ofstream report(output/"result.json");report<<"{\"selected\":"<<selected<<",\"executed\":"<<executed<<",\"passed\":"<<passed<<",\"failed\":"<<(executed-passed)
        <<",\"skipped\":0,\"unexecuted\":"<<(selected-executed)<<",\"assertions\":"<<checks<<",\"assertionFailures\":"<<failures<<"}\n";
    std::printf("selected=%d executed=%d passed=%d failed=%d skipped=0 assertions=%d assertionFailures=%d\n",selected,executed,passed,executed-passed,checks,failures);
    return failures || executed!=selected ? 1 : 0;
}
