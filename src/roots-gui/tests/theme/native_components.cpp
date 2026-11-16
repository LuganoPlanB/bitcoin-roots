// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/native_host.h"
#include "ui/components/gallery.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <stdexcept>
using namespace roots::gui;
namespace c=ui::components;
namespace {
int assertions{},failures{};
void Check(bool ok,const char* id) { ++assertions;if (!ok) { ++failures;std::fprintf(stderr,"FAIL %s\n",id); } }
struct FixtureHost final:c::Commands {
    std::vector<c::Command> requests;
    bool Submit(const c::Command& value) override { requests.push_back(value);return true; }
};
struct Observer final:platform::NativeObserver {
    std::filesystem::path output;std::string id,variant;c::Gallery gallery;c::GalleryFacts facts;FixtureHost service;
    std::string rendered;bool captured{},cleaned{};Display* display{};
    int (*motion)(Display*,int,int,int,unsigned long){};int (*button)(Display*,unsigned int,Bool,unsigned long){};
    const std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    bool CaptureFramebuffer() const override { return true; }
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override {
        if (std::chrono::steady_clock::now()-start>std::chrono::seconds(12)) throw std::runtime_error("component capture deadline");
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override {
        if (gallery.review_fixture && frame.rendered_frames==7) gallery.reveal_rows=true;
        const bool privacy_variant=variant=="hidden" || variant=="hiddenReduced" || variant=="reviewHidden" || variant=="reviewShown";
        const bool motion_variant=variant=="reduced" || variant=="hiddenReduced";
        const int motion_start=variant=="hiddenReduced"?5:3;
        const bool privacy_gesture=privacy_variant && (frame.rendered_frames==3 || frame.rendered_frames==4);
        const bool motion_gesture=motion_variant && (frame.rendered_frames==motion_start || frame.rendered_frames==motion_start+1);
        if (privacy_gesture || motion_gesture) {
            const auto& rect=privacy_gesture?facts.privacy_toggle:facts.motion_toggle;
            int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);
            Check(motion(display,-1,x+int((rect[0]+rect[2])/2),y+int((rect[1]+rect[3])/2),0),"actual native privacy/motion pointer");
            Check(button(display,1,frame.rendered_frames==(privacy_gesture?3:motion_start),0),"actual native privacy/motion gesture");XFlush(display);
        }
        if (variant=="public" && (frame.rendered_frames==3 || frame.rendered_frames==4)) {
            int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);
            Check(motion(display,-1,x+int((facts.copy.rect[0]+facts.copy.rect[2])/2),y+int((facts.copy.rect[1]+facts.copy.rect[3])/2),0),"actual native Copy pointer");
            Check(button(display,1,frame.rendered_frames==3,0),"actual native Copy button");XFlush(display);
        }
        if (frame.rendered_frames!=(privacy_variant || motion_variant?14:10)) return;
        const auto primary=variant=="dialog"?facts.dialog:facts.primary;
        Check(primary.visible && primary.rect[2]>primary.rect[0] && primary.rect[3]>primary.rect[1],"primary actually visible");
        Check(primary.rect[0]>=0 && primary.rect[1]>=0 && primary.rect[2]<=frame.width && primary.rect[3]<=frame.height,"primary action reachable at scale");
        if (variant=="disabled") Check(primary.disabled,"disabled primary semantics");
        if (variant=="invalid") Check(gallery.amount.dirty() && !gallery.amount.error.empty() && primary.disabled,"invalid draft retained and Apply disabled");
        if (variant=="table") {
            Check(facts.rows.size()==3,"real table rows visible");
            Check(std::any_of(facts.rows.begin(),facts.rows.end(),[](const auto& r){return r.key==202 && r.selected && r.visible;}),"selected stable model row actually visible");
            Check(std::any_of(facts.rows.begin(),facts.rows.end(),[](const auto& r){return r.key==303 && r.disabled && r.visible;}),"disabled row remains visible");
        }
        if (variant=="hidden" || variant=="hiddenReduced" || variant=="reviewHidden") {
            Check(gallery.privacy.hidden && facts.amount.disabled && facts.fee.disabled && facts.amount_copy.disabled,"hidden fields/copy unavailable");
            Check(gallery.privacy.Chart(std::array{gallery.rows[0].amount}).percentages.empty(),"hidden chart receives no values");
            Check(rendered.find("0.01000000")==std::string::npos && rendered.find("0.02000000")==std::string::npos && rendered.find("0.03000000")==std::string::npos && rendered.find("0.06000000")==std::string::npos && rendered.find("1.25")==std::string::npos,"hidden rendered text excludes amounts");
        }
        if (gallery.review_fixture) {
            Check(facts.review_visible,"full review content actually visible inside native child clip");
            Check(gallery.privacy.hidden==(variant=="reviewHidden"),"review follows actual explicit privacy toggle");
            Check(rendered.find("bcrt1qfull-destination###literal-%s-000000000000000000000001")!=std::string::npos,"full review destination remains rendered");
            Check(gallery.presentation.Snapshot().find("bcrt1qfull-destination###literal-%s-000000000000000000000001")!=std::string::npos,"full review destination remains annotated");
        }
        if (motion_variant) Check(gallery.reduced_motion,"actual native reduced-motion toggle adopted");
        Check(gallery.presentation.Tree().Validate(),"actual amount annotation tree validates");
        if (gallery.reduced_motion) Check(gallery.privacy_feedback==(gallery.privacy.hidden?1.F:0.F),"reduced motion snaps to useful resting feedback");
        std::ofstream(output/(id+".presentation.txt"))<<rendered;
        std::ofstream(output/(id+".semantics.txt"))<<gallery.presentation.Snapshot();
        if (variant=="public") {
            Check(service.requests.size()==1 && std::holds_alternative<c::CopyIdentifier>(service.requests[0]),"one real Copy host request");
            if (service.requests.size()==1) Check(std::get<c::CopyIdentifier>(service.requests[0]).value=="bcrt1qpublic-fixture-identifier-000000000000000000000000000000000001","Copy retains full public value");
        } else Check(service.requests.empty(),"capture fixture has no service side effects");
        Check(frame.decorated && frame.frame_top>0 && frame.font_texture && !frame.gl_error,"actual decorated framebuffer");
        Check(frame.rgba.size()==std::size_t(frame.framebuffer_width)*frame.framebuffer_height*4,"readback available");
        std::ofstream image(output/(id+".ppm"),std::ios::binary);image<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for (int y=frame.framebuffer_height-1;y>=0;--y) for (int x=0;x<frame.framebuffer_width;++x)
            image.write(reinterpret_cast<const char*>(frame.rgba.data()+(std::size_t(y)*frame.framebuffer_width+x)*4),3);
        Check(bool(image),"real component framebuffer written");
        std::ofstream meta(output/(id+".json"));
        meta<<std::boolalpha<<"{\"id\":\""<<id<<"\",\"width\":"<<frame.framebuffer_width<<",\"height\":"<<frame.framebuffer_height
            <<",\"effectiveScale\":"<<frame.effective_scale<<",\"primaryDisabled\":"<<primary.disabled<<",\"primaryFocused\":"<<primary.focused
            <<",\"primary\":["<<primary.rect[0]<<','<<primary.rect[1]<<','<<primary.rect[2]<<','<<primary.rect[3]<<"],\"requests\":"<<service.requests.size()
            <<",\"privacyHidden\":"<<gallery.privacy.hidden<<",\"reducedMotion\":"<<gallery.reduced_motion<<",\"privacyFeedback\":"<<gallery.privacy_feedback<<",\"identityAccents\":"<<(gallery.polish.accent_used?1:0)
            <<",\"reviewSurface\":"<<gallery.review_fixture
            <<",\"chartSamples\":"<<gallery.presentation.ChartSamples().size()
            <<",\"fixedDeltaSeconds\":"<<ImGui::GetIO().DeltaTime
            <<",\"renderer\":\""<<host.Renderer()<<"\"}\n";
        captured=true;host.RequestClose();
    }
    void Teardown(const platform::NativeTeardown& t) override {
        cleaned=t.renderer_cleared && t.platform_cleared && t.textures_released && t.imgui_cleared && t.context_cleared;
        Check(cleaned,"all component native resources settled");std::ofstream record(output/(id+".teardown.json"));record<<"{\"cleaned\":"<<(cleaned?"true":"false")<<"}\n";
    }
};
}
int main(int argc,char** argv)
{
    if (argc!=3) return 2;
    int executed{},passed{};Display* display{};void* xtst{};const std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);
    try {
        const char* display_name=std::getenv("DISPLAY"),*owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if (!display_name || !owned || std::string(display_name)!=owned) throw std::runtime_error("owned authenticated display required");
        display=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);
        if (!display || !xtst) throw std::runtime_error("native Copy XTest unavailable");
        for (int width:{1200,960}) for (bool dark:{false,true}) for (float scale:{1.F,1.5F,2.F}) for (const char* variant:{"public","invalid","disabled","dialog","empty","table","hidden","reduced","hiddenReduced","reviewHidden","reviewShown"}) {
            Observer observer;observer.output=out;observer.variant=variant;observer.display=display;
            observer.motion=reinterpret_cast<decltype(observer.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));
            observer.button=reinterpret_cast<decltype(observer.button)>(dlsym(xtst,"XTestFakeButtonEvent"));
            if (!observer.motion || !observer.button) throw std::runtime_error("XTest entry unavailable");
            observer.id="component-gallery-"+std::to_string(width)+(dark?"-dark-":"-light-")+std::to_string(int(scale*100))+"-"+variant;
            observer.gallery.selection.selected={202};
            if (observer.variant=="invalid") {
                observer.gallery.amount.SetDraft("0.000000001");c::CommitAmountEdit("amount",observer.gallery.amount,observer.service);
                observer.gallery.status.failed=true;observer.gallery.status.detail="Fixture request failed; invalid draft retained.";
            } else if (observer.variant=="disabled") { observer.gallery.enabled=false;observer.gallery.status.detail="Loading public observations; host busy."; }
            else if (observer.variant=="dialog") { observer.gallery.dialog.state={true,true,false};observer.gallery.status.freshness=Freshness::Stale; }
            else if (observer.variant=="empty") { observer.gallery.rows.clear();observer.gallery.reveal_rows=true; }
            else if (observer.variant=="table") {
                observer.gallery.reveal_rows=true;
                observer.gallery.rows[0].label="Öffentliche##Beobachtung###2026 / längere übersetzte Beschriftung";
                observer.gallery.label=c::Edit{"Notes###Q1"};
                observer.gallery.rows[2].label="Invoice##2026";observer.gallery.rows[2].disabled_reason="Needs###review";
            }
            if (observer.variant=="hidden" || observer.variant=="reduced" || observer.variant=="hiddenReduced") observer.gallery.reveal_rows=true;
            if (observer.variant=="reviewHidden" || observer.variant=="reviewShown") {
                observer.gallery.review_fixture=true;observer.gallery.reveal_rows=true;
                observer.gallery.privacy.hidden=observer.variant=="reviewShown";
            }
            const int before=failures;
            { platform::NativeOptions options{width,width==1200?820:640,dark,scale,argv[2]};options.fixed_delta_seconds=1.F/60;
              platform::NativeHost host(options,&observer,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance) {
                auto& io=ImGui::GetPlatformIO();const auto callback=io.Platform_SetClipboardTextFn;void* const user=io.Platform_ClipboardUserData;
                io.Platform_ClipboardUserData=&observer;io.Platform_SetClipboardTextFn=[](ImGuiContext*,const char* text) {
                    static_cast<Observer*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->rendered=text;
                };
                ImGui::LogToClipboard();
                observer.facts=c::DrawGallery(observer.gallery,observer.service,theme,appearance);
                ImGui::LogFinish();io.Platform_SetClipboardTextFn=callback;io.Platform_ClipboardUserData=user;
            });host.Run(); }
            Check(observer.captured && observer.cleaned,"component capture and teardown complete");++executed;if (failures==before) ++passed;
        }
    } catch (const std::exception& error) { ++failures;std::fprintf(stderr,"FAIL %s\n",error.what()); }
    if (display) XCloseDisplay(display);if (xtst) dlclose(xtst);
    std::ofstream result(out/"result.json");result<<"{\"selected\":132,\"executed\":"<<executed<<",\"passed\":"<<passed<<",\"failed\":"<<(executed-passed)<<",\"skipped\":0,\"assertions\":"<<assertions<<",\"assertionFailures\":"<<failures<<"}\n";
    std::printf("selected=132 executed=%d passed=%d failed=%d skipped=0 assertions=%d assertionFailures=%d\n",executed,passed,executed-passed,assertions,failures);
    return failures || executed!=132?1:0;
}
