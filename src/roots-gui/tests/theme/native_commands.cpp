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
using namespace roots::gui;
namespace sh=ui::shell;namespace a=app;
namespace {
int assertions{},failures{};
void Check(bool ok,const char* label){++assertions;if(!ok){++failures;std::fprintf(stderr,"FAIL %s\n",label);}}
const char* variants[]{"menuFile","menuView","menuTools","menuHelp","menuSelected","statusUnavailable","statusLoading","statusError","statusStale","statusOffline","statusLocked","statusLong","activityLoading","activityEmpty","activityError","activityStale","activityHidden","activityHover","activityPressed","activityFocus","gridEscape","unavailableRoute","failedAck"};
struct Requests final:a::Requests {
    a::HostObservations observations;int count{};bool handler{};a::Command last{};
    a::HostObservations Observe() const override{return observations;}
    a::Availability CanSubmit(const a::Request& r) const override{return {handler || r.command==a::Command::Component,"Explicit missing public fixture adapter."};}
    a::Receipt Submit(const a::Request& r) override {++count;last=r.command;if(r.command==a::Command::Component)return {true,"Public fixture selection acknowledged."};return {false,"Explicit fixture acknowledgement failed; document retained."};}
};
struct Observer final:platform::NativeObserver {
    sh::State state;sh::Facts facts;Requests requests;
    std::filesystem::path out;std::string variant,id,rendered;bool cleaned{},captured{},wrong{};
    Display* display{};int (*motion)(Display*,int,int,int,unsigned long){};int (*button)(Display*,unsigned int,Bool,unsigned long){};int (*key)(Display*,unsigned int,Bool,unsigned long){};
    const std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    bool CaptureFramebuffer() const override{return true;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override {if(std::chrono::steady_clock::now()-start>std::chrono::seconds(12))throw std::runtime_error("command native deadline");}
    void Pointer(platform::NativeHost& host,float x,float y){int wx{},wy{};glfwGetWindowPos(host.Window(),&wx,&wy);Check(motion(display,-1,wx+int(x),wy+int(y),0),"actual native command pointer");}
    void Key(KeySym sym,bool down){Check(key(display,XKeysymToKeycode(display,sym),down,0),"actual native command key");}
    void Frame(platform::NativeHost& host,const platform::NativeFrame& f) override {
        const int n=f.rendered_frames;
        const bool menu=variant.starts_with("menu"),status=variant.starts_with("status"),activity=variant.starts_with("activity");
        const bool pending_drawer=status || (activity && variant!="activityHover" && variant!="activityPressed");
        if(n==3 || n==4) {
            const bool down=n==3;
            if(menu){const float x=(variant=="menuFile"?20:variant=="menuTools"?109:variant=="menuHelp"?154:66)*f.effective_scale;Pointer(host,x,16*f.effective_scale);Check(button(display,1,down,0),"native menu header click");}
            else if(variant=="gridEscape") {const ui::components::ItemFacts* target{};for(const auto& row:facts.rows)if(row.visible && !row.disabled && row.rect[1]>=facts.table_hover[1] && row.rect[3]<=facts.table_hover[3]){target=&row;break;}if(!target)throw std::runtime_error("no fully visible native grid target");const auto& r=target->rect;Pointer(host,r[0]+32*f.effective_scale,(r[1]+r[3])/2);Check(button(display,1,down,0),"native grid row click");}
            else if(variant=="unavailableRoute" || variant=="failedAck"){if(down){Key(XK_Control_L,true);Key(XK_R,true);}else{Key(XK_R,false);Key(XK_Control_L,false);}}
            else {
                const auto& r=(status?facts.status_button:facts.activity_button).rect;Pointer(host,(r[0]+r[2])/2,(r[1]+r[3])/2);
                if(variant!="activityHover" && !(variant=="activityPressed" && !down))Check(button(display,1,down,0),"native status/activity click or held press");
                if(variant=="activityHidden"){if(down){Key(XK_Control_L,true);Key(XK_Shift_L,true);Key(XK_H,true);}else{Key(XK_H,false);Key(XK_Shift_L,false);Key(XK_Control_L,false);}}
            }XSync(display,False);
        }
        if(menu && (n==6 || n==7)){Key(XK_End,n==6);XSync(display,False);}
        if(variant=="gridEscape" && (n==7 || n==8)){if(n==7){Key(XK_Control_L,true);Key(XK_Tab,true);}else{Key(XK_Tab,false);Key(XK_Control_L,false);}XSync(display,False);}
        if(variant=="activityFocus" && (n==7 || n==8)){Key(XK_Escape,n==7);XSync(display,False);}
        if(variant=="activityFocus" && (n==10 || n==11 || n==13 || n==14)){Key(n<13?XK_Left:XK_Right,n==10 || n==13);XSync(display,False);}
        if((variant=="statusLong" || variant=="activityLoading") && n==9) {const auto& r=facts.drawer_body;Pointer(host,r.x+r.w/2,r.y+r.h/2);Check(button(display,5,True,0)&&button(display,5,False,0),"actual native drawer scroll");XSync(display,False);}
        if(n!=20)return;
        Check(f.decorated&&f.font_texture&&!f.gl_error,"actual decorated command renderer");Check(state.presentation.Tree().Validate(),"current command presentation validates");
        Check(state.documents.Documents().size()==3 && state.documents.ActiveId()==1,"workspace documents retained during activity/menu interaction");
        Check(state.documents.Find(1)->sections[4].note.draft()=="Retained command fixture draft" && state.documents.Find(1)->sections[4].selection.selected.contains(7),"background and failed commands preserve drafts/selection");
        Check(!facts.accounts_submitted && facts.layout.accounts.w==0,"zero invented account space");
        auto reachable=[&](const ui::components::ItemFacts& item){return item.visible && item.rect[0]>=0 && item.rect[1]>=0 && item.rect[2]<=f.width && item.rect[3]<=f.height;};
        Check(reachable(facts.activity_button)&&reachable(facts.status_button),"actual status actions reachable at scale");
        std::array<float,4> menu_rect{};bool menu_focused{},menu_visible{};int menu_target=-1;
        if(menu) {
            const auto target=variant=="menuFile"?a::Command::Quit:variant=="menuTools"?a::Command::Restart:variant=="menuHelp"?a::Command::About:a::Command::Refresh;
            Check(!facts.menu_items.empty() && ImGui::GetIO().NavVisible,"actual open menu keyboard navigation");
            const auto& row=facts.menu_rows.at(target);menu_rect=row.rect;menu_focused=row.focused;menu_visible=row.visible;menu_target=int(target);
            if(!row.focused || !reachable(row))std::fprintf(stderr,"MENU %s %dx%d scale%.1f rect %.3f %.3f %.3f %.3f focused%d visible%d\n",variant.c_str(),f.width,f.height,f.effective_scale,row.rect[0],row.rect[1],row.rect[2],row.rect[3],row.focused,row.visible);
            Check(row.focused && reachable(row),"actual End reaches last menu route at stress scale");
        }
        else if(pending_drawer && variant!="activityFocus") {
            Check(facts.drawer_visible && state.drawer==(status?sh::Drawer::Services:sh::Drawer::Activity),"intended status/activity target opened");
            Check(reachable(facts.drawer_close),"drawer primary/close reachable at stress scale");
            Check(facts.drawer_rect.y>facts.layout.tabs.y+facts.layout.tabs.h && facts.drawer_rect.h<facts.layout.content.h,"background drawer retains visible workspace regions");
            if(status){for(const char* label:{"Devices:","Proxy:","Updates:","Network:","Connection:","Backend height:","Last success:","Error:"})Check(rendered.find(label)!=std::string::npos,"actual typed status fields present");}
            else Check(rendered.find("Operational log")!=std::string::npos,"actual operational log drawer");
        }
        if(variant=="activityHidden")Check(state.privacy.hidden && rendered.find("123.45678901")==std::string::npos && state.presentation.Snapshot().find("123.45678901")==std::string::npos,"amount privacy applied before new log/task/status presentation");
        if(variant=="activityHover")Check(facts.activity_hovered&&!ImGui::GetIO().MouseDown[0]&&!facts.drawer_visible,"actual activity hover without activation");
        if(variant=="activityPressed")Check(facts.activity_held&&ImGui::GetIO().MouseDown[0]&&!facts.drawer_visible,"actual held activity press before release");
        if(variant=="activityFocus")Check(!facts.drawer_visible && facts.status_button.focused && ImGui::GetIO().NavVisible,"actual drawer Escape focus return and keyboard status target");
        if(variant=="gridEscape")Check(state.grid_exits==1 && facts.tabs.at(1).focused && ImGui::GetIO().NavVisible,"actual native CtrlTab leaves grid for intended document tab");
        if(variant=="statusLong" || variant=="activityLoading")Check(facts.drawer_scroll>0,"actual drawer wheel changes scroll position");
        if(variant=="unavailableRoute" || variant=="failedAck") {
            Check(state.invocation.has_value() && state.invocation->command==(wrong?a::Command::LockCurrent:a::Command::Refresh),"intended command target");
            Check(state.invocation && state.invocation->available==(variant=="failedAck") && state.invocation->dispatched==(variant=="failedAck") && !state.invocation->accepted,"capability rejection distinguished from failed acknowledgement");
            Check(!state.notice.empty(),"actual rejection dependency explained");
        }
        Check(requests.count==(variant=="gridEscape" || variant=="failedAck"?1:0),"only explicit fixture requests observed; no background effects");
        std::ofstream ppm(out/(id+".ppm"),std::ios::binary);ppm<<"P6\n"<<f.framebuffer_width<<' '<<f.framebuffer_height<<"\n255\n";
        for(int y=f.framebuffer_height-1;y>=0;--y)for(int x=0;x<f.framebuffer_width;++x)ppm.write(reinterpret_cast<const char*>(f.rgba.data()+(size_t(y)*f.framebuffer_width+x)*4),3);
        Check(bool(ppm),"native command framebuffer saved");std::ofstream(out/(id+".presentation.txt"))<<rendered;std::ofstream(out/(id+".semantics.txt"))<<state.presentation.Snapshot();
        const bool target=!state.invocation || state.invocation->command==(wrong?a::Command::LockCurrent:a::Command::Refresh);
        std::ofstream(out/(id+".json"))<<std::boolalpha<<"{\"variant\":\""<<variant<<"\",\"width\":"<<f.width<<",\"height\":"<<f.height<<",\"effectiveScale\":"<<f.effective_scale<<",\"drawerVisible\":"<<facts.drawer_visible<<",\"drawerScroll\":"<<facts.drawer_scroll<<",\"workspaceDocuments\":"<<state.documents.Documents().size()<<",\"accountWidth\":"<<facts.layout.accounts.w<<",\"targetAccepted\":"<<target<<",\"menuTarget\":"<<menu_target<<",\"menuFocused\":"<<menu_focused<<",\"menuVisible\":"<<menu_visible<<",\"menuRect\":["<<menu_rect[0]<<','<<menu_rect[1]<<','<<menu_rect[2]<<','<<menu_rect[3]<<']'<<",\"statusHit\":["<<facts.status_button.rect[0]<<','<<facts.status_button.rect[1]<<','<<facts.status_button.rect[2]<<','<<facts.status_button.rect[3]<<']'<<",\"statusVisual\":["<<facts.status_visual[0]<<','<<facts.status_visual[1]<<','<<facts.status_visual[2]<<','<<facts.status_visual[3]<<']'<<",\"statusHeight\":"<<facts.layout.status.h<<",\"tabFocused\":"<<facts.tabs.at(1).focused<<",\"statusFocused\":"<<facts.status_button.focused<<",\"activityHovered\":"<<facts.activity_hovered<<",\"activityHeld\":"<<facts.activity_held<<",\"navVisible\":"<<ImGui::GetIO().NavVisible<<",\"gridExits\":"<<state.grid_exits<<",\"requests\":"<<requests.count<<"}\n";
        if(variant=="activityPressed"){button(display,1,False,0);XSync(display,False);}captured=true;host.RequestClose();
    }
    void Teardown(const platform::NativeTeardown& t) override {cleaned=t.renderer_cleared&&t.platform_cleared&&t.textures_released&&t.imgui_cleared&&t.context_cleared;Check(cleaned,"native command context/backend resources released");std::ofstream(out/(id+".teardown.json"))<<"{\"cleaned\":"<<(cleaned?"true":"false")<<"}\n";}
};
}
int main(int argc,char** argv)
{
    if(argc!=3 && argc!=4)return 2;const std::string probe=argc==4?argv[3]:"";const bool wrong=probe=="wrong-target";const int selected=wrong?1:probe.empty()?int(std::size(variants))*12:probe=="menus"?60:12;int executed{},passed{};
    const std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);Display* display{};void* xtst{};
    try {
        const char* name=std::getenv("DISPLAY"),*owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");if(!name||!owned||std::string(name)!=owned)throw std::runtime_error("owned native display required");display=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);if(!display||!xtst)throw std::runtime_error("XTest unavailable");
        for(int width:{1200,960})for(bool dark:{false,true})for(float scale:{1.F,1.5F,2.F})for(const char* variant:variants) {
            if(wrong && (width!=960 || !dark || scale!=2 || std::string(variant)!="unavailableRoute"))continue;
            if(!wrong && !probe.empty() && (probe=="menus"?!std::string(variant).starts_with("menu"):probe!=variant))continue;
            Observer o;o.out=out;o.display=display;o.variant=variant;o.wrong=wrong;o.state.documents=a::PublicFixtureDocuments();o.state.documents.Find(1)->sections[4].note.SetDraft("Retained command fixture draft");o.state.documents.Find(1)->sections[4].selection.selected={7};
            o.motion=reinterpret_cast<decltype(o.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));o.button=reinterpret_cast<decltype(o.button)>(dlsym(xtst,"XTestFakeButtonEvent"));o.key=reinterpret_cast<decltype(o.key)>(dlsym(xtst,"XTestFakeKeyEvent"));if(!o.motion||!o.button||!o.key)throw std::runtime_error("native input symbols absent");
            auto& obs=o.requests.observations;obs.connection={a::ServiceState::Offline,"Explicit public fixture; no RPC."};obs.network={a::ServiceState::Ready,"regtest fixture"};obs.activity={a::ServiceState::Loading,"Public activity fixture."};obs.tasks={{101,"Public background fixture","No wallet effect.",a::ServiceState::Loading,425}};
            if(o.variant=="activityLoading"){
                for(unsigned i=0;i<40;++i){
                    obs.tasks.front().progress_per_mille=i*10;
                    o.state.activity_log.Observe(obs,1700000000000LL+1000*i);
                }
                obs.tasks.front().progress_per_mille=425;
            }
            if(o.variant=="statusUnavailable")obs={};
            if(o.variant=="statusLoading")obs.connection={a::ServiceState::Loading,"Explicit fixture connecting."};
            if(o.variant=="statusError" || o.variant=="activityError"){obs.connection={a::ServiceState::Error,"Explicit fixture transport failure."};obs.error="Explicit fixture failed request.";obs.activity.state=a::ServiceState::Error;obs.tasks[0].state=a::ServiceState::Error;}
            if(o.variant=="statusStale" || o.variant=="activityStale"){obs.connection={a::ServiceState::Stale,"Explicit fixture observations are stale."};obs.activity.state=a::ServiceState::Stale;obs.tasks[0].state=a::ServiceState::Stale;}
            if(o.variant=="statusLocked"){obs.connection={a::ServiceState::Locked,"Explicit protected fixture observation, not a daemon lock."};o.state.documents.Find(1)->observation=a::ObservationState::Locked;}
            if(o.variant=="statusLong"){std::string detail;for(int i=0;i<8;++i)detail+="Öffentliche Verbindungsbeobachtungen##2026### — Status und Abhängigkeiten. ";obs.devices.detail=obs.proxy.detail=obs.updates.detail=obs.network.detail=obs.connection.detail=detail;}
            if(o.variant=="activityEmpty"){obs.activity={a::ServiceState::Ready,"Explicit empty activity fixture."};obs.tasks.clear();obs.log.clear();}
            if(o.variant=="activityHidden"){obs.tasks[0].title=obs.tasks[0].detail="Sensitive 123.45678901";obs.log={"Sensitive 123.45678901"};}
            if(o.variant=="menuSelected")o.state.privacy.hidden=true;
            obs.backend_height=o.variant=="statusUnavailable"?std::nullopt:std::optional<int64_t>(123);if(o.variant!="statusUnavailable")obs.last_success="2026-11-16 08:00 UTC (fixture)";
            o.requests.handler=o.variant=="failedAck";
            for(auto& section:o.state.documents.Find(1)->sections)section.scroll_y=scale>1?160*scale:0;
            o.id="commands-"+std::to_string(width)+(dark?"-dark-":"-light-")+std::to_string(int(scale*100))+"-"+variant;const int before=failures;
            {platform::NativeOptions options{width,width==1200?820:640,dark,scale,argv[2]};options.fixed_delta_seconds=1.F/60;platform::NativeHost* current{};
             platform::NativeHost host(options,&o,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){auto& p=ImGui::GetPlatformIO();const auto fn=p.Platform_SetClipboardTextFn;void* user=p.Platform_ClipboardUserData;p.Platform_ClipboardUserData=&o;p.Platform_SetClipboardTextFn=[](ImGuiContext*,const char* text){static_cast<Observer*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->rendered=text;};ImGui::LogToClipboard();o.facts=platform::DrawShellFrame(*current,o.state,o.requests,theme,appearance);ImGui::LogFinish();p.Platform_SetClipboardTextFn=fn;p.Platform_ClipboardUserData=user;});current=&host;host.Run();}
            Check(o.captured&&o.cleaned,"native command scenario and teardown complete");++executed;if(before==failures)++passed;
        }
    }catch(const std::exception& error){++failures;std::fprintf(stderr,"FAIL %s\n",error.what());}
    if(display)XCloseDisplay(display);if(xtst)dlclose(xtst);
    std::ofstream(out/"result.json")<<"{\"selected\":"<<selected<<",\"executed\":"<<executed<<",\"passed\":"<<passed<<",\"failed\":"<<(executed-passed)<<",\"skipped\":0,\"assertions\":"<<assertions<<",\"assertionFailures\":"<<failures<<"}\n";return failures||executed!=selected?1:0;
}
