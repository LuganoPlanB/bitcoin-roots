// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include "ui/theme/appearance.h"
#include <chrono>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
using namespace roots::gui;
namespace {
int assertions{},failures{};
void Check(bool ok,const char* id){++assertions;if(!ok){++failures;std::fprintf(stderr,"FAIL %s\n",id);}}
std::string Read(const std::filesystem::path& file){std::ifstream f(file,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
struct Requests final:app::Requests { int count{};app::Receipt Submit(const app::Request&) override {++count;return {false,"Unavailable fixture service."};} };
struct Observer final:platform::NativeObserver {
    std::filesystem::path out,preference;std::string mode,rendered;
    std::optional<bool> system_dark{false};ui::BootstrapAppearance appearance;
    ui::shell::State workspace;ui::shell::Facts facts;Requests requests;
    int captures{};bool cleaned{};const std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
    bool CaptureFramebuffer() const override{return true;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override {if(std::chrono::steady_clock::now()-start>std::chrono::seconds(12))throw std::runtime_error("appearance deadline");}
    void Capture(platform::NativeHost& host,const platform::NativeFrame& f,const char* phase,int choice,bool dark,bool invalid,bool save_failed,bool unavailable,const char* saved)
    {
        ++captures;const auto status=host.AppearanceState();
        Check(status.choice==choice && status.invalid_saved==invalid && status.save_failed==save_failed && status.system_unavailable==unavailable,"actual public host state");
        Check(appearance.status.choice==status.choice && appearance.status.invalid_saved==status.invalid_saved && appearance.status.save_failed==status.save_failed && appearance.status.system_unavailable==status.system_unavailable,"current per-frame callback snapshot");
        Check(appearance.dark==dark,"actual projected color");
        Check((rendered.find("Saved appearance is invalid")!=std::string::npos)==invalid,"invalid saved preference visibly reported");
        Check((rendered.find("Appearance could not be saved")!=std::string::npos)==save_failed,"actual save failure and retry presentation");
        Check((rendered.find("System appearance is unavailable")!=std::string::npos)==unavailable,"live System capability visibility");
        Check(facts.appearance_notice_count==int(invalid)+int(save_failed)+int(unavailable),"only current warnings published");
        if(invalid||save_failed||unavailable)Check(facts.appearance_notice_visible,"complete appearance warning actually visible");
        Check(facts.primary.visible && facts.primary.rect[0]>=0 && facts.primary.rect[1]>=0 && facts.primary.rect[2]<=f.width && facts.primary.rect[3]<=f.height,"primary remains reachable with warning");
        Check(workspace.presentation.Tree().Validate(),"actual production appearance annotations validate");
        const auto semantic=workspace.presentation.Snapshot();
        Check((semantic.find("System appearance is unavailable")!=std::string::npos)==unavailable,"current fallback annotation");
        if(saved)Check(Read(preference)==saved,"saved appearance ID preserved until explicit choice");
        Check(requests.count==0,"no wallet or external service effect");Check(f.decorated && f.font_texture && !f.gl_error,"actual native renderer");
        const auto id="shell-appearance-"+mode+"-"+phase;
        std::ofstream ppm(out/(id+".ppm"),std::ios::binary);ppm<<"P6\n"<<f.framebuffer_width<<' '<<f.framebuffer_height<<"\n255\n";
        for(int y=f.framebuffer_height-1;y>=0;--y)for(int x=0;x<f.framebuffer_width;++x)ppm.write(reinterpret_cast<const char*>(f.rgba.data()+(size_t(y)*f.framebuffer_width+x)*4),3);
        Check(bool(ppm),"appearance framebuffer saved");std::ofstream(out/(id+".presentation.txt"))<<rendered;std::ofstream(out/(id+".semantics.txt"))<<semantic;
        std::ofstream(out/(id+".json"))<<std::boolalpha<<"{\"choice\":"<<choice<<",\"dark\":"<<appearance.dark<<",\"invalidSaved\":"<<status.invalid_saved<<",\"saveFailed\":"<<status.save_failed<<",\"systemUnavailable\":"<<status.system_unavailable<<",\"warningVisible\":"<<facts.appearance_notice_visible<<",\"width\":"<<f.width<<",\"height\":"<<f.height<<",\"effectiveScale\":"<<f.effective_scale<<"}\n";
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& f) override {
        const int n=f.rendered_frames;
        if(mode=="invalid") {
            if(n==6){Capture(host,f,"invalid",0,false,true,false,false,"invalid-choice\n");Check(host.SetAppearance(0),"explicit replacement acknowledged");}
            if(n==10){Capture(host,f,"replaced",0,false,false,false,false,"light\n");host.RequestClose();}
        } else if(mode=="missing") {
            if(n==6){Capture(host,f,"fallback",2,false,false,false,true,"system\n");Check(host.SetAppearance(1),"explicit Dark overrides unavailable System");}
            if(n==10){Capture(host,f,"explicit-dark",1,true,false,false,false,"dark\n");Check(host.SetAppearance(2),"explicit System choice acknowledged");}
            if(n==14){Capture(host,f,"system-again",2,false,false,false,true,"system\n");host.RequestClose();}
        } else if(mode=="live") {
            if(n==6){Capture(host,f,"known-light",2,false,false,false,false,"system\n");system_dark.reset();}
            if(n==10){Capture(host,f,"lost-light",2,false,false,false,true,"system\n");system_dark=true;}
            if(n==14){Capture(host,f,"known-dark",2,true,false,false,false,"system\n");system_dark.reset();}
            if(n==18){Capture(host,f,"lost-dark",2,false,false,false,true,"system\n");host.RequestClose();}
        } else if(mode=="retry") {
            if(n==4)Check(!host.SetAppearance(1),"blocked write is rejected");
            if(n==6){Capture(host,f,"write-failed",0,false,false,true,false,"light\n");Check(Read(out/"pending-target")=="untouched","failed save preserves pending-link target");auto pending=preference;pending+=".pending";std::filesystem::remove(pending);Check(host.SetAppearance(2),"retry acknowledges actual saved System");}
            if(n==10){Capture(host,f,"retry-cleared",2,false,false,false,false,"system\n");host.RequestClose();}
        } else if(n==6) {
            const bool dark=mode=="dark";Capture(host,f,"explicit",dark?1:0,dark,false,false,false,dark?"dark\n":"light\n");host.RequestClose();
        }
    }
    void Teardown(const platform::NativeTeardown& t) override {
        cleaned=t.renderer_cleared&&t.platform_cleared&&t.textures_released&&t.imgui_cleared&&t.context_cleared;Check(cleaned,"appearance native resources settled");
        std::ofstream(out/(mode+".teardown.json"))<<"{\"cleaned\":"<<(cleaned?"true":"false")<<"}\n";
    }
};
}
int main(int argc,char** argv)
{
    if(argc!=3 || !std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY"))return 2;
    const std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);int executed{},passed{},captures{};
    try {
        for(const char* mode:{"invalid","missing","live","retry","light","dark"}) {
            Observer o;o.out=out;o.mode=mode;o.preference=out/(o.mode+"-appearance");
            if(o.mode=="invalid")std::ofstream(o.preference)<<"invalid-choice\n";
            else if(o.mode=="retry") {Check(ui::theme::SaveAppearance(o.preference,ui::theme::Appearance::Light),"prepare valid preference before blocked write");const auto target=out/"pending-target";std::ofstream(target)<<"untouched";auto pending=o.preference;pending+=".pending";std::filesystem::create_symlink(target,pending);}
            else Check(ui::theme::SaveAppearance(o.preference,o.mode=="light"?ui::theme::Appearance::Light:o.mode=="dark"?ui::theme::Appearance::Dark:ui::theme::Appearance::System),"prepare actual isolated preference");
            if(o.mode=="missing" || o.mode=="light" || o.mode=="dark")o.system_dark.reset();
            const int before=failures;
            { platform::NativeOptions options;options.font_directory=argv[2];options.appearance_file=o.preference;options.fixed_delta_seconds=1.F/60;
              if(o.mode=="invalid" || o.mode=="missing"){options.width=960;options.height=640;options.font_scale=2;}
              options.system_appearance_reader=[&]{return o.system_dark;};platform::NativeHost* current{};
              platform::NativeHost host(options,&o,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){
                  o.appearance=appearance;auto& p=ImGui::GetPlatformIO();const auto callback=p.Platform_SetClipboardTextFn;void* const user=p.Platform_ClipboardUserData;
                  p.Platform_ClipboardUserData=&o;p.Platform_SetClipboardTextFn=[](ImGuiContext*,const char* text){static_cast<Observer*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->rendered=text;};
                  ImGui::LogToClipboard();o.facts=platform::DrawShellFrame(*current,o.workspace,o.requests,theme,appearance);ImGui::LogFinish();p.Platform_SetClipboardTextFn=callback;p.Platform_ClipboardUserData=user;
              });current=&host;host.Run(); }
            Check(o.cleaned,"appearance host lifetime complete");captures+=o.captures;++executed;if(failures==before)++passed;
        }
    } catch(const std::exception& error){++failures;std::fprintf(stderr,"FAIL %s\n",error.what());}
    std::ofstream(out/"result.json")<<"{\"selected\":6,\"executed\":"<<executed<<",\"passed\":"<<passed<<",\"failed\":"<<(executed-passed)<<",\"skipped\":0,\"captures\":"<<captures<<",\"assertions\":"<<assertions<<",\"assertionFailures\":"<<failures<<"}\n";
    return failures || executed!=6 || captures!=13?1:0;
}
