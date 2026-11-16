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
#include <thread>
#include <stdexcept>
using namespace roots::gui;
namespace sh=ui::shell;namespace a=app;namespace c=ui::components;
namespace {
int assertions{},failures{};
void Check(bool ok,const char* label){++assertions;if(!ok){++failures;std::fprintf(stderr,"FAIL %s\n",label);}}
const char* variants[]{"focus","pointer","keyboard","external","failedAck","hidden","locked","long"};
struct Requests final:a::Requests {
    bool ack{true};std::vector<a::Request> requests;
    a::Availability CanSubmit(const a::Request&) const override{return {true,"Explicit effect-free native fixture."};}
    a::Receipt Submit(const a::Request& r) override{requests.push_back(r);return {ack,ack?"Explicit fixture acknowledged.":"Explicit fixture rejected."};}
};
struct Observer final:platform::NativeObserver {
    sh::State state;sh::Facts facts;Requests service;std::filesystem::path out;std::string id,variant;bool wrong{},captured{},cleaned{};
    std::shared_ptr<const c::SemanticPacket> retained;std::string retained_name;uint64_t sequence{};c::ActionResult queued{};
    Display* display{};int (*motion)(Display*,int,int,int,unsigned long){};int (*button)(Display*,unsigned int,Bool,unsigned long){};int (*key)(Display*,unsigned int,Bool,unsigned long){};
    const std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    bool CaptureFramebuffer() const override{return true;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override {if(std::chrono::steady_clock::now()-start>std::chrono::seconds(12))throw std::runtime_error("native semantic deadline");}
    void Queue(c::ax::StableId target,c::ax::SemanticAction action){
        const auto packet=state.presentation.exchange.Published();Check(packet && packet->valid,"actual immutable production packet");
        std::thread external([&]{queued=state.presentation.exchange.Queue({++sequence,packet->generation,target,action,{}});});external.join();Check(queued==c::ActionResult::Queued,"external thread enqueues immutable action");
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& f) override {
        const int n=f.rendered_frames;
        if(n==3){retained=state.presentation.exchange.Published();retained_name=retained->nodes.front().name;
            if(variant=="locked"){Queue(facts.rows.at(0).id,c::ax::SemanticAction::Select);state.documents.Find(1)->observation=a::ObservationState::Locked;}
            else if(variant=="long")Queue(facts.tabs.at(2).id,c::ax::SemanticAction::Select);
            else if(variant=="external" || variant=="failedAck")Queue(facts.primary.id,c::ax::SemanticAction::Press);
            else if(variant=="hidden")Queue(facts.rows.at(0).id,c::ax::SemanticAction::Select);
            else if(variant=="focus" || variant=="keyboard")Queue(wrong?facts.tabs.at(1).id:facts.primary.id,c::ax::SemanticAction::Focus);
        }
        if(variant=="pointer" && (n==4 || n==5)){const auto& r=facts.primary.rect;int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);Check(motion(display,-1,x+int((r[0]+r[2])/2),y+int((r[1]+r[3])/2),0),"actual native primary pointer");Check(button(display,1,n==4,0),"actual native primary click");XSync(display,False);}
        if(variant=="keyboard" && (n==7 || n==8)){Check(key(display,XKeysymToKeycode(display,XK_space),n==7,0),"actual native primary Space activation");XSync(display,False);}
        if(n!=16)return;
        const auto packet=state.presentation.exchange.Published();Check(packet && packet->valid,"current owner-published immutable packet validates");Check(retained->nodes.front().name==retained_name && retained->generation<packet->generation,"retained packet survives subsequent frames");
        Check(f.decorated && f.font_texture && !f.gl_error,"native decorated semantic rendering");Check(facts.primary.visible && facts.primary.rect[0]>=0 && facts.primary.rect[2]<=f.width && facts.primary.rect[3]<=f.height,"semantic primary reachable at stress scale");
        if(variant=="focus")Check(facts.primary.focused && ImGui::GetIO().NavVisible,"intended actual primary focus target");
        if(variant=="pointer" || variant=="keyboard" || variant=="external" || variant=="failedAck"){
            Check(service.requests.size()==1 && service.requests[0].command==a::Command::Component && service.requests[0].document==1,"pointer keyboard external share typed command identity and cardinality");
            if(!service.requests.empty()){const auto* command=std::get_if<c::Action>(&*service.requests[0].component);Check(command && command->id=="primary" && command->kind==c::ActionKind::Primary,"same actual primary validation and acknowledgement route");}
        }
        if(variant=="failedAck")Check(!state.notice.empty() && state.documents.ActiveId()==1,"failed external acknowledgement retains workspace");
        if(variant=="locked")Check(service.requests.empty() && facts.rows.empty() && state.presentation.exchange.Receipts().back().result==c::ActionResult::Stale,"lock invalidates queued native action before any effect");
        if(variant=="locked")Check(facts.tabs.at(1).focused && packet->Find(facts.tabs.at(1).id)->state.focused,"locked body returns actual focus to stable document tab");
        if(variant=="long")Check(state.documents.ActiveId()==2 && facts.tabs.at(2).focused,"external tab selection deliberately focuses stable target");
        if(variant=="hidden")for(const auto& node:packet->nodes)Check(node.name.find("0.01000000")==std::string::npos && node.description.find("0.01000000")==std::string::npos && node.value.find("0.01000000")==std::string::npos && node.numeric==0,"hidden amount absent from immutable packet channels");
        if(variant=="focus" || variant=="external" || variant=="keyboard" || variant=="failedAck"){
            const auto receipts=state.presentation.exchange.Receipts();Check(!receipts.empty() && receipts.back().result==(variant=="failedAck"?c::ActionResult::Rejected:c::ActionResult::Applied),"external action terminal result reflects actual acknowledgement");
        }
        Check(state.documents.Find(1)->Current().note.draft()=="Public fixture note","native action preserves unrelated draft");
        std::ofstream ppm(out/(id+".ppm"),std::ios::binary);ppm<<"P6\n"<<f.framebuffer_width<<' '<<f.framebuffer_height<<"\n255\n";
        for(int y=f.framebuffer_height-1;y>=0;--y)for(int x=0;x<f.framebuffer_width;++x)ppm.write(reinterpret_cast<const char*>(f.rgba.data()+(size_t(y)*f.framebuffer_width+x)*4),3);
        std::ofstream meta(out/(id+".json"));meta<<std::boolalpha<<"{\"id\":\""<<id<<"\",\"variant\":\""<<variant<<"\",\"effectiveScale\":"<<f.effective_scale<<",\"focused\":"<<facts.primary.focused<<",\"navVisible\":"<<ImGui::GetIO().NavVisible<<",\"focusCanvas\":["<<int(ImGui::GetStyle().Colors[ImGuiCol_ChildBg].x*255+.5F)<<','<<int(ImGui::GetStyle().Colors[ImGuiCol_ChildBg].y*255+.5F)<<','<<int(ImGui::GetStyle().Colors[ImGuiCol_ChildBg].z*255+.5F)<<"],\"generation\":"<<packet->generation<<",\"nodes\":"<<packet->nodes.size()<<",\"requests\":"<<service.requests.size()<<",\"primary\":["<<facts.primary.rect[0]<<','<<facts.primary.rect[1]<<','<<facts.primary.rect[2]<<','<<facts.primary.rect[3]<<"]}\n";
        std::ofstream transcript(out/(id+".semantics.txt"));for(const auto& node:packet->nodes)transcript<<node.id<<" parent="<<node.parent<<" role="<<int(node.role)<<" actions="<<unsigned(node.actions)<<" | "<<node.name<<" | "<<node.value<<" | "<<node.description<<'\n';
        captured=true;host.RequestClose();
    }
    void Teardown(const platform::NativeTeardown& t) override {Check(state.presentation.exchange.Close(),"native owner closes semantic generation");Check(!state.presentation.exchange.Published()->valid,"closed native packet is explicitly invalid");if(retained)Check(state.presentation.exchange.Queue({++sequence,retained->generation,facts.primary.id,c::ax::SemanticAction::Press,{}})==c::ActionResult::Stale,"native teardown rejects retained-generation action");cleaned=t.renderer_cleared&&t.platform_cleared&&t.textures_released&&t.imgui_cleared&&t.context_cleared;Check(cleaned,"semantic native resources released");std::ofstream(out/(id+".teardown.json"))<<"{\"cleaned\":"<<(cleaned?"true":"false")<<"}\n";}
};
}
int main(int argc,char** argv){
    if(argc!=3 && argc!=4)return 2;const std::string probe=argc==4?argv[3]:"";const bool wrong=probe=="wrong-target";const int selected=wrong?1:probe.empty()?96:12;int executed{},passed{};
    const std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);Display* display{};void* xtst{};
    try {
        const char* name=std::getenv("DISPLAY"),*owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");if(!name||!owned||std::string(name)!=owned)throw std::runtime_error("owned authenticated display required");display=XOpenDisplay(nullptr);xtst=dlopen("libXtst.so.6",RTLD_NOW);if(!display||!xtst)throw std::runtime_error("native XTest unavailable");
        for(int width:{1200,960})for(bool dark:{false,true})for(float scale:{1.F,1.5F,2.F})for(const char* variant:variants){
            if(wrong && (width!=960 || !dark || scale!=2 || std::string(variant)!="focus"))continue;if(!wrong && !probe.empty() && probe!=variant)continue;
            Observer o;o.out=out;o.variant=variant;o.wrong=wrong;o.display=display;o.state.documents=a::PublicFixtureDocuments();for(auto& section:o.state.documents.Find(1)->sections)section.scroll_y=scale>1?160*scale:0;
            o.service.ack=o.variant!="failedAck";o.state.privacy.hidden=o.variant=="hidden";
            if(o.variant=="long")o.state.documents.Rename(2,"Öffentliche Wallet-Beobachtungen##2026### — "+std::string(160,'L'));
            o.motion=reinterpret_cast<decltype(o.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));o.button=reinterpret_cast<decltype(o.button)>(dlsym(xtst,"XTestFakeButtonEvent"));o.key=reinterpret_cast<decltype(o.key)>(dlsym(xtst,"XTestFakeKeyEvent"));if(!o.motion||!o.button||!o.key)throw std::runtime_error("native input symbols unavailable");
            o.id="semantics-"+std::to_string(width)+(dark?"-dark-":"-light-")+std::to_string(int(scale*100))+"-"+variant;const int before=failures;
            {platform::NativeOptions options{width,width==1200?820:640,dark,scale,argv[2]};options.fixed_delta_seconds=1.F/60;platform::NativeHost* current{};platform::NativeHost host(options,&o,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){o.facts=platform::DrawShellFrame(*current,o.state,o.service,theme,appearance);});current=&host;host.Run();}
            Check(o.captured && o.cleaned,"native semantic scenario completes and settles");++executed;if(before==failures)++passed;
        }
    }catch(const std::exception& error){++failures;std::fprintf(stderr,"FAIL %s\n",error.what());}
    if(display)XCloseDisplay(display);if(xtst)dlclose(xtst);std::ofstream(out/"result.json")<<"{\"selected\":"<<selected<<",\"executed\":"<<executed<<",\"passed\":"<<passed<<",\"failed\":"<<(executed-passed)<<",\"skipped\":0,\"assertions\":"<<assertions<<",\"assertionFailures\":"<<failures<<"}\n";return failures||executed!=selected?1:0;
}
