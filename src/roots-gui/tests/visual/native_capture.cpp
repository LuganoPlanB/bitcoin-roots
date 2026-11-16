// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/native_host.h"
#include "ui/preferences.h"
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <dlfcn.h>
#include <fstream>
#include <stdexcept>
using namespace roots::gui;
namespace {
int assertions{}, assertion_failures{};
void Check(bool ok,const char* id) { ++assertions; if (!ok) { ++assertion_failures; std::fprintf(stderr,"FAIL %s\n",id); } }
struct Unavailable : std::runtime_error { using std::runtime_error::runtime_error; };
struct Service final : PreferenceRequests {
    int saves{},cancels{};bool accept{true};
    bool Save(const PublicPreferences&) override { ++saves;return accept; }
    void Cancel() override { ++cancels; }
};
struct Observer final : platform::NativeObserver {
    std::filesystem::path out;std::string name;ui::PreferenceFacts facts;ui::PreferencePanel panel;Service service;
    bool failure{},missing{},captured{},cleaned{};Display* display{};
    using Motion=int(*)(Display*,int,int,int,unsigned long);using Button=int(*)(Display*,unsigned int,Bool,unsigned long);
    Motion motion{};Button button{};
    std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
    bool CaptureFramebuffer() const override { return !missing; }
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override {
        if (std::chrono::steady_clock::now()-start>std::chrono::seconds(10)) throw std::runtime_error("visual scenario deadline");
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& f) override {
        if (failure && (f.rendered_frames==3 || f.rendered_frames==4)) {
            int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);
            Check(motion(display,-1,x+int((facts.save[0]+facts.save[2])/2),y+int((facts.save[1]+facts.save[3])/2),0),"actual pointer move");
            Check(button(display,1,f.rendered_frames==3,0),"actual pointer button");XFlush(display);
        }
        if (f.rendered_frames!=8) return;
        if (f.rgba.size()!=std::size_t(f.framebuffer_width)*f.framebuffer_height*4) throw Unavailable("actual framebuffer unavailable");
        Check(f.decorated && f.frame_top>0,"native frame retained");
        Check(facts.save_visible && facts.cancel_visible,"Save and Cancel visible");
        for (const auto& rect : {facts.save,facts.cancel})
            Check(rect[0]>=0 && rect[1]>=0 && rect[2]<=f.width && rect[3]<=f.height,"primary actions reachable");
        Check(std::fabs(facts.font_size-std::round(14*f.effective_scale))<.05F,"single font/DPI scale");
        Check(!f.gl_error && f.font_texture && f.live_textures>0,"real renderer resources");
        std::ofstream image(out/(name+".ppm"),std::ios::binary);
        image<<"P6\n"<<f.framebuffer_width<<' '<<f.framebuffer_height<<"\n255\n";
        for (int y=f.framebuffer_height-1;y>=0;--y) for (int x=0;x<f.framebuffer_width;++x)
            image.write(reinterpret_cast<const char*>(f.rgba.data()+(std::size_t(y)*f.framebuffer_width+x)*4),3);
        if (!image) throw std::runtime_error("actual capture write failed");
        std::ofstream meta(out/(name+".json"));
        meta<<std::boolalpha<<"{\"name\":\""<<name<<"\",\"width\":"<<f.framebuffer_width<<",\"height\":"<<f.framebuffer_height
            <<",\"dpi\":"<<f.dpi_x<<",\"fontSize\":"<<facts.font_size<<",\"saveVisible\":"<<facts.save_visible<<",\"cancelVisible\":"<<facts.cancel_visible
            <<",\"saveRequests\":"<<service.saves<<",\"saveRejected\":"<<panel.save_rejected<<",\"renderer\":\""<<host.Renderer()<<"\"}\n";
        if (failure) {
            Check(service.saves==1 && panel.save_rejected,"real Save rejection shown before failure screenshot");
            Check(false,"intentional rendered assertion failure");
        } else Check(service.saves==0 && service.cancels==0,"visual capture has no service side effects");
        captured=true;host.RequestClose();
    }
    void Teardown(const platform::NativeTeardown& s) override {
        cleaned=s.renderer_cleared && s.platform_cleared && s.textures_released && s.imgui_cleared && s.context_cleared;
        Check(cleaned,"all native resources settled");
        std::ofstream record(out/(name+".teardown.json"));record<<std::boolalpha<<"{\"cleaned\":"<<cleaned<<"}\n";
        Check(bool(record),"teardown evidence written");
    }
};
}
int main(int argc,char** argv)
{
    if (argc<3 || argc>4) return 2;
    const std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);
    const std::string mode=argc==4?argv[3]:"matrix";
    if (mode!="matrix" && mode!="failure" && mode!="missing-framebuffer") return 2;
    int selected=mode=="matrix"?12:1,executed{},passed{},failed{},unavailable{};Display* display{};void* xtst{};
    try {
        if (!std::getenv("DISPLAY") || !std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY") || std::string(std::getenv("DISPLAY"))!=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY")) throw Unavailable("authenticated owned display unavailable");
        display=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);
        if (!display || !xtst) throw Unavailable("native X11/XTest capability unavailable");
        for (int width : {1200,960}) for (bool dark : {false,true}) for (float scale : {1.F,1.5F,2.F}) {
            if (mode!="matrix" && executed) continue;
            Observer observer;observer.out=out;observer.failure=mode=="failure";observer.missing=mode=="missing-framebuffer";
            observer.name=mode=="matrix"?"test-panel-"+std::to_string(width)+"x"+std::to_string(width==1200?820:640)+(dark?"-dark-":"-light-")+std::to_string(int(scale*100)):mode;
            observer.display=display;observer.motion=reinterpret_cast<Observer::Motion>(dlsym(xtst,"XTestFakeMotionEvent"));observer.button=reinterpret_cast<Observer::Button>(dlsym(xtst,"XTestFakeButtonEvent"));
            if (!observer.motion || !observer.button) throw Unavailable("XTest API unavailable");
            observer.service.accept=!observer.failure;
            int before=assertion_failures;
            { platform::NativeHost host({width,width==1200?820:640,dark,scale,argv[2]},&observer,
                [&](const imkit::Theme& theme,const ui::BootstrapAppearance&) {
                    const auto* vp=ImGui::GetMainViewport();const float pad=24*scale;
                    ImGui::SetNextWindowPos({vp->WorkPos.x+pad,vp->WorkPos.y+pad},ImGuiCond_Always);
                    ImGui::SetNextWindowSize({vp->WorkSize.x-2*pad,vp->WorkSize.y-2*pad},ImGuiCond_Always);
                    observer.facts=ui::DrawPreferences(observer.panel,observer.service,theme);
                });host.Run(); }
            Check(observer.captured && observer.cleaned,"capture and teardown completed");
            ++executed;if (assertion_failures==before) ++passed;else ++failed;
        }
    } catch (const Unavailable& e) { unavailable=selected-executed;std::fprintf(stderr,"UNAVAILABLE %s\n",e.what()); }
    catch (const std::exception& e) { ++executed;++failed;std::fprintf(stderr,"FAIL %s\n",e.what()); }
    if (display) XCloseDisplay(display);if (xtst) dlclose(xtst);
    std::ofstream report(out/"result.json");
    report<<"{\"selected\":"<<selected<<",\"executed\":"<<executed<<",\"passed\":"<<passed<<",\"failed\":"<<failed<<",\"skipped\":0,\"unavailable\":"<<unavailable<<",\"assertions\":"<<assertions<<",\"assertionFailures\":"<<assertion_failures<<"}\n";
    std::printf("selected=%d executed=%d passed=%d failed=%d skipped=0 unavailable=%d assertions=%d assertion_failures=%d\n",selected,executed,passed,failed,unavailable,assertions,assertion_failures);
    return failed || assertion_failures ? 1 : unavailable || executed!=selected ? 2 : 0;
}
