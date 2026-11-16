// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <chrono>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <stdexcept>
#include <thread>
using namespace roots::gui;
namespace sh=ui::shell;namespace a=app;
namespace {
int assertions{},failures{};
void Check(bool ok,const char* label){++assertions;if(!ok){++failures;std::fprintf(stderr,"FAIL %s\n",label);}}
struct Requests final:a::Requests {int count{}; a::Availability CanSubmit(const a::Request&) const override{return {true,"Explicit effect-free fixture rejection handler."};}a::Receipt Submit(const a::Request&) override {++count;std::string reason;for(int i=0;i<12;++i)reason+="Fixture dependency unavailable; document and draft retained. ";return {false,reason};}};
struct Observer final:platform::NativeObserver {
    sh::State state;sh::Facts facts;Requests requests;
    std::filesystem::path out;std::string variant,id,rendered;bool cleaned{},captured{},wrong{};
    Display* display{};int (*motion)(Display*,int,int,int,unsigned long){};int (*button)(Display*,unsigned int,Bool,unsigned long){};int (*key)(Display*,unsigned int,Bool,unsigned long){};
    const std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    bool CaptureFramebuffer() const override{return true;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override {if(std::chrono::steady_clock::now()-start>std::chrono::seconds(12))throw std::runtime_error("document native deadline");}
    void Pointer(platform::NativeHost& host,float x,float y) {int wx{},wy{};glfwGetWindowPos(host.Window(),&wx,&wy);Check(motion(display,-1,wx+int(x),wy+int(y),0),"native document pointer");}
    void Frame(platform::NativeHost& host,const platform::NativeFrame& f) override {
        const int n=f.rendered_frames;
        if(n==2 && variant=="focus") {
            bool rejected=false;std::thread foreign([&]{try{host.TakeDroppedPaths();}catch(const std::logic_error&){rejected=true;}});foreign.join();Check(rejected,"foreign thread cannot drain native document input");
            rejected=false;try{platform::NativeHost second({});}catch(const std::logic_error&){rejected=true;}Check(rejected,"second native context owner is explicitly rejected without transferring documents");
        }
        if(n==3 || n==4) {
            const bool down=n==3;
            if(variant=="close") {
                for(KeySym sym:down?std::initializer_list<KeySym>{XK_Control_L,XK_W}:std::initializer_list<KeySym>{XK_W,XK_Control_L})Check(key(display,XKeysymToKeycode(display,sym),down,0),"native dirty-close shortcut");
            } else if(variant=="focus") {
                const auto& r=facts.tabs.at(1).rect;Pointer(host,(r[0]+r[2])/2,(r[1]+r[3])/2);Check(button(display,1,down,0),"native tab focus click");
            }
            else if(variant=="hover" || variant=="pressed" || variant=="long") {
                const auto& r=facts.tabs.at(1).rect;Pointer(host,(r[0]+r[2])/2,(r[1]+r[3])/2);
                if(variant=="pressed" && down)Check(button(display,1,True,0),"native tab held press");
            } else Pointer(host,f.width-2,f.height-2);
            XSync(display,False);
        }
        if(variant=="focus" && (n==7 || n==8 || n==10 || n==11)) {
            Check(key(display,XKeysymToKeycode(display,n<10?XK_Right:XK_Left),n==7 || n==10,0),"real native arrow traversal restores intended tab focus");XSync(display,False);
        }
        if(n==6 && variant=="metadataError") {const auto& r=facts.document_actions.at("apply-name").rect;Pointer(host,(r[0]+r[2])/2,(r[1]+r[3])/2);Check(button(display,1,True,0)&&button(display,1,False,0),"native rejected metadata request");XSync(display,False);}
        if(n==5 && variant=="drop") {
            // Exercise the exact installed native callback with borrowed bytes,
            // then mutate those bytes. This is callback-boundary evidence, not
            // an OS drag gesture or native file chooser qualification.
            const auto r=facts.drop_target;glfwSetCursorPos(host.Window(),r.x+r.w/2,r.y+r.h/2);
            auto callback=glfwSetDropCallback(host.Window(),nullptr);Check(callback!=nullptr,"actual native drop callback installed");glfwSetDropCallback(host.Window(),callback);
            std::string source="/public/Invoice##2026.json";const char* paths[]{source.c_str()};callback(host.Window(),1,paths);source.assign(source.size(),'x');
        }
        if(n!=16)return;
        const bool modal=variant=="close" || variant=="rename" || variant=="delete" || variant=="import" || variant=="metadataError" || variant=="drop";
        Check(f.decorated&&f.font_texture&&!f.gl_error,"actual decorated native renderer");
        Check(!facts.accounts_submitted && facts.layout.accounts.w==0,"zero account-strip allocation");
        Check(state.presentation.Tree().Validate(),"document presentation validates");
        Check(facts.document_dialog_visible==modal,"intended dialog presence");
        auto reachable=[&](const ui::components::ItemFacts& item){return item.visible && item.rect[0]>=0 && item.rect[1]>=0 && item.rect[2]<=f.width && item.rect[3]<=f.height;};
        if(modal) {
            Check(reachable(facts.document_actions.at("cancel")),"modal cancel reachable at scale");
            const char* action=variant=="close"?"save-close":variant=="delete"?"confirm-delete":variant=="import" || variant=="drop"?"import":"apply-name";
            Check(reachable(facts.document_actions.at(action)),"modal primary reachable at scale");
            if(variant=="close")Check(state.documents.Find(1)->Dirty() && state.documents.Documents().size()==3,"dirty close retains every draft and document pending choice");
            if(variant=="import" || variant=="drop")Check(state.actions.format==-1 && facts.document_actions.at("import").disabled,"ambiguous format requires explicit selection");
            if(variant=="drop")Check(state.actions.candidate.source=="/public/Invoice##2026.json","native borrowed drop path copied before callback return");
            if(variant=="metadataError") {
                Check(!state.actions.error.empty()&&requests.count==1&&state.documents.Find(1)->name=="Public watch-only wallet","failed actual metadata acknowledgement retains identity/name");
                Check(!ImGui::GetIO().NavVisible,"actual mouse activation retains pointer mode after active field");
            }
        } else {
            Check(reachable(facts.primary),"document primary reachable at scale");
            if(variant=="empty")Check(facts.drop_target.w>0 && facts.drop_target.h>0 && rendered.find("Drop a public wallet")!=std::string::npos,"actual empty drop target and three actions");
            else Check(state.documents.ActiveId()==(wrong?2:1),"intended native document target");
            if(variant=="long")Check(rendered.find("Gemeinschaftskasse##2026### — Öffentliche Beobachtungen")!=std::string::npos,"full literal translated name available in actual tooltip");
            if(variant=="hover")Check(facts.tab_hovered.at(1) && !ImGui::GetIO().MouseDown[0],"actual intended-tab hover without held button");
            if(variant=="pressed")Check(facts.tab_held.at(1) && ImGui::GetIO().MouseDown[0],"actual intended-tab held pressed state");
            if(variant=="focus")Check(facts.tabs.at(1).focused&&ImGui::GetIO().NavVisible,"actual target focus with native activation");
        }
        if(variant!="metadataError")Check(requests.count==0,"presentation and pending choice have no host effect");
        if(variant=="hidden")Check(rendered.find("0.01000000")==std::string::npos && state.presentation.Snapshot().find("0.01000000")==std::string::npos,"hidden document amounts absent");
        std::ofstream ppm(out/(id+".ppm"),std::ios::binary);ppm<<"P6\n"<<f.framebuffer_width<<' '<<f.framebuffer_height<<"\n255\n";
        for(int y=f.framebuffer_height-1;y>=0;--y)for(int x=0;x<f.framebuffer_width;++x)ppm.write(reinterpret_cast<const char*>(f.rgba.data()+(size_t(y)*f.framebuffer_width+x)*4),3);
        Check(bool(ppm),"native document framebuffer saved");std::ofstream(out/(id+".presentation.txt"))<<rendered;std::ofstream(out/(id+".semantics.txt"))<<state.presentation.Snapshot();
        std::ofstream(out/(id+".json"))<<std::boolalpha<<"{\"variant\":\""<<variant<<"\",\"width\":"<<f.width<<",\"height\":"<<f.height<<",\"effectiveScale\":"<<f.effective_scale<<",\"dialog\":"<<modal<<",\"active\":"<<state.documents.ActiveId()<<",\"expectedActive\":"<<(wrong?2:1)<<",\"ownedDocuments\":"<<state.documents.Documents().size()<<",\"nativeWindowOwners\":1,\"targetFocused\":"<<(facts.tabs.contains(1)&&facts.tabs.at(1).focused)<<",\"navVisible\":"<<ImGui::GetIO().NavVisible<<",\"mouseDown\":"<<ImGui::GetIO().MouseDown[0]<<",\"targetHovered\":"<<(facts.tab_hovered.contains(1)&&facts.tab_hovered.at(1))<<",\"targetHeld\":"<<(facts.tab_held.contains(1)&&facts.tab_held.at(1))<<",\"accountsWidth\":"<<facts.layout.accounts.w<<"}\n";
        if(variant=="pressed"){button(display,1,False,0);XSync(display,False);}captured=true;host.RequestClose();
    }
    void Teardown(const platform::NativeTeardown& t) override {cleaned=t.renderer_cleared&&t.platform_cleared&&t.textures_released&&t.imgui_cleared&&t.context_cleared;Check(cleaned,"document native owner/backend resources released");std::ofstream(out/(id+".teardown.json"))<<"{\"cleaned\":"<<(cleaned?"true":"false")<<"}\n";}
};
}
int main(int argc,char** argv)
{
    if(argc!=3 && argc!=4)return 2;const std::string probe=argc==4?argv[3]:"";const bool wrong=probe=="wrong-target";const int selected=wrong?1:probe.empty()?168:12;int executed{},passed{};
    const std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);Display* display{};void* xtst{};
    try {
        const char* name=std::getenv("DISPLAY"),*owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");if(!name||!owned||std::string(name)!=owned)throw std::runtime_error("owned native display required");
        display=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);if(!display||!xtst)throw std::runtime_error("XTest unavailable");
        for(int width:{1200,960})for(bool dark:{false,true})for(float scale:{1.F,1.5F,2.F})for(const char* variant:{"empty","tabs","long","hover","pressed","focus","close","rename","delete","import","metadataError","drop","locked","hidden"}) {
            if(wrong && (width!=960 || !dark || scale!=2 || std::string(variant)!="focus"))continue;
            if(!wrong && !probe.empty() && probe!=variant)continue;
            Observer o;o.out=out;o.display=display;o.variant=variant;o.wrong=wrong;
            o.motion=reinterpret_cast<decltype(o.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));o.button=reinterpret_cast<decltype(o.button)>(dlsym(xtst,"XTestFakeButtonEvent"));o.key=reinterpret_cast<decltype(o.key)>(dlsym(xtst,"XTestFakeKeyEvent"));if(!o.motion||!o.button||!o.key)throw std::runtime_error("native input symbols absent");
            if(o.variant!="empty" && o.variant!="import" && o.variant!="drop")o.state.documents=a::PublicFixtureDocuments();
            if(o.variant=="focus"){o.state.focus_tab=1;}
            if(o.variant=="long")o.state.documents.Find(1)->name="Gemeinschaftskasse##2026### — Öffentliche Beobachtungen und länger übersetzter Walletname";
            if(o.variant=="close")o.state.documents.Find(1)->sections[4].note.SetDraft("Unsaved fixture draft");
            if(o.variant=="rename" || o.variant=="metadataError")sh::BeginRename(o.state,1);
            if(o.variant=="delete")sh::BeginDelete(o.state,1);
            if(o.variant=="import")sh::BeginImport(o.state,"/public/Invoice##2026.json");
            if(o.variant=="locked")o.state.documents.Find(1)->observation=a::ObservationState::Locked;
            if(o.variant=="hidden")o.state.privacy.hidden=true;
            if(auto* d=o.state.documents.Active())for(auto& s:d->sections)s.scroll_y=scale>1?160*scale:0;
            o.id="documents-"+std::to_string(width)+(dark?"-dark-":"-light-")+std::to_string(int(scale*100))+"-"+variant;
            const int before=failures;
            {platform::NativeOptions options{width,width==1200?820:640,dark,scale,argv[2]};options.fixed_delta_seconds=1.F/60;platform::NativeHost* current{};
             platform::NativeHost host(options,&o,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){
                auto& p=ImGui::GetPlatformIO();const auto callback=p.Platform_SetClipboardTextFn;void* const user=p.Platform_ClipboardUserData;p.Platform_ClipboardUserData=&o;p.Platform_SetClipboardTextFn=[](ImGuiContext*,const char* text){static_cast<Observer*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->rendered=text;};
                ImGui::LogToClipboard();o.facts=platform::DrawShellFrame(*current,o.state,o.requests,theme,appearance);ImGui::LogFinish();p.Platform_SetClipboardTextFn=callback;p.Platform_ClipboardUserData=user;
             });current=&host;host.Run();}
            Check(o.captured&&o.cleaned,"native document scenario and teardown completed");++executed;if(before==failures)++passed;
        }
    }catch(const std::exception& error){++failures;std::fprintf(stderr,"FAIL %s\n",error.what());}
    if(display)XCloseDisplay(display);if(xtst)dlclose(xtst);
    std::ofstream(out/"result.json")<<"{\"selected\":"<<selected<<",\"executed\":"<<executed<<",\"passed\":"<<passed<<",\"failed\":"<<(executed-passed)<<",\"skipped\":0,\"assertions\":"<<assertions<<",\"assertionFailures\":"<<failures<<"}\n";return failures||executed!=selected?1:0;
}
