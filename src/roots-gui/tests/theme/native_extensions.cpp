// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
namespace sh=ui::shell;
namespace a=app;
namespace {
struct Requests final:a::Requests {
    std::atomic<unsigned> probes{}, preparations{};
    unsigned commands{};
    ConnectionService connection{[this](const ServerCandidate& draft,std::stop_token){
        ++probes;
        BackendCapabilities reply;
        reply.candidate=draft;
        reply.error=rpc::Error::Configuration;
        return reply;
    }};
    FundingService funding{[this](const FundingRequest&,std::stop_token){
        ++preparations;
        return FundingReply{{},"No funding fixture reply"};
    }};
    ConnectionService* Connection() override{return &connection;}
    FundingService* Funding() override{return &funding;}
    a::Receipt Submit(const a::Request&) override{++commands; return {false,{}};}
};

struct Observer final:platform::NativeObserver {
    sh::State state;
    sh::Facts facts;
    Requests requests;
    std::filesystem::path out;
    Display* display{};
    int(*motion)(Display*,int,int,int,unsigned long){};
    int(*button)(Display*,unsigned int,Bool,unsigned long){};
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    unsigned stage{},checks{},images{};
    bool cleaned{};

    void Check(bool ok,const char* error)
    {
        if (!ok) throw std::runtime_error(error);
        ++checks;
    }
    bool CaptureFramebuffer() const override{return true;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override
    {
        if (std::chrono::steady_clock::now()-start>std::chrono::seconds(12))
            throw std::runtime_error("native extension deadline at stage "+std::to_string(stage));
    }
    void Click(platform::NativeHost& host,const ui::components::ItemFacts& item,bool down)
    {
        Check(item.visible && !item.disabled,"native input reaches an enabled control");
        int x{},y{};
        glfwGetWindowPos(host.Window(),&x,&y);
        const auto& r=item.rect;
        Check(motion(display,-1,x+int((r[0]+r[2])/2),y+int((r[1]+r[3])/2),0)
            && button(display,1,down,0),"owned native click");
        XSync(display,False);
    }
    bool Has(std::string_view name) const {return state.presentation.Snapshot().find(name)!=std::string::npos;}
    bool Published() const
    {
        const auto packet=state.presentation.exchange.Published();
        return packet && packet->valid && !packet->overflow;
    }
    void Capture(const platform::NativeFrame& f,const char* name)
    {
        Check(f.width==960 && f.height==640 && f.effective_scale==1.F && f.decorated && !f.gl_error,
            "actual 960x640 Linux rendering");
        Check(state.presentation.Tree().Validate(),"current semantic tree");
        const auto packet=state.presentation.exchange.Published();
        Check(packet && packet->valid && !packet->overflow,"actual published accessibility packet");
        if (facts.server.visible) {
            const auto has=[&](std::string_view label){
                return std::ranges::any_of(packet->nodes,[&](const auto& node){return node.name==label;});
            };
            Check(has("History index unavailable") && !has("Funding preview"),"index modal owns accessibility scope");
        }
        std::ofstream ppm(out/(std::string(name)+".ppm"),std::ios::binary);
        ppm<<"P6\n"<<f.framebuffer_width<<' '<<f.framebuffer_height<<"\n255\n";
        for (int y=f.framebuffer_height-1;y>=0;--y)
            for (int x=0;x<f.framebuffer_width;++x)
                ppm.write(reinterpret_cast<const char*>(f.rgba.data()+(size_t(y)*f.framebuffer_width+x)*4),3);
        Check(bool(ppm),"retain frame bytes");
        std::ofstream(out/(std::string(name)+".semantics.txt"))<<state.presentation.Snapshot();
        std::ofstream accessible(out/(std::string(name)+".published.txt"));
        for (const auto& entry:packet->nodes) {
            accessible<<entry.name<<'|'<<entry.value<<'|'<<entry.description
                <<" | focused="<<entry.state.focused<<" selected="<<entry.state.selected
                <<" disabled="<<entry.state.disabled<<" showing="<<entry.showing<<'\n';
        }
        ++images;
    }
    void Index(std::string_view expected)
    {
        Check(Has("HistoryIndexAdapter") && Has(expected) && Has("Retained mempool samples")
            && Has("Arbitrary address or output history"),"scope and missing client named on actual screen");
        Check(!facts.server.items.contains("host") && !facts.server.items.contains("credential")
            && facts.server.items.at("test").disabled && facts.server.items.at("apply").disabled
            && !facts.server.items.at("cancel").disabled,"unavailable index has no connection configuration or Test/Apply");
        Check(!requests.connection.Active() && !requests.connection.Busy(),"no implicit index or RPC activity");
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override
    {
        if (frame.rendered_frames<3) return;
        switch(stage) {
        case 0: if (!facts.server.visible) return;Click(host,facts.server.items.at("Private index"),true);break;
        case 1: Click(host,facts.server.items.at("Private index"),false);break;
        case 2:
            if (!Has("Private indexing is off") || !Published()) return;
            Index("Private indexing is off");
            Capture(frame,"indexing-unavailable");
            Click(host,facts.server.items.at("Public index"),true);
            break;
        case 3: Click(host,facts.server.items.at("Public index"),false);break;
        case 4:
            if (!Has("Public indexing is off") || !Published()) return;
            Index("Public indexing is off");
            Capture(frame,"public-index-unavailable");
            Click(host,facts.server.items.at("cancel"),true);
            break;
        case 5: Click(host,facts.server.items.at("cancel"),false);break;
        case 6:
            if (state.server.dialog.open || !facts.funding.contains("toggle")) return;
            Click(host,facts.funding.at("toggle"),true);
            break;
        case 7: Click(host,facts.funding.at("toggle"),false);break;
        case 8:
            if (!Has("Privacy strategy unavailable") || !Published()) return;
            Check(Has("TransactionConstructionAdapter") && Has("Avoid partial spends") && Has("no anonymity guarantee"),
                "actual funding screen shows exact strategy and boundaries");
            Check(!facts.funding.contains("privacy") && facts.funding.at("preview").disabled
                && !requests.preparations && !requests.probes && !requests.commands,"no optional action, funding or network effect");
            Check(facts.funding.at("toggle").focused,"funding toggle holds focus after its label changes");
            Capture(frame,"privacy-unavailable");
            host.RequestClose();
            break;
        default: return;
        }
        ++stage;
    }
    void Teardown(const platform::NativeTeardown& value) override
    {
        cleaned=value.renderer_cleared && value.platform_cleared && value.textures_released && value.imgui_cleared
            && value.input_cleared && value.context_cleared;
        Check(cleaned,"owned renderer resources released");
    }
};
}

int main(int argc,char** argv)
{
    if (argc!=4) return 2;
    Observer observer;
    observer.out=argv[1];
    std::filesystem::create_directories(observer.out);
    void* xtst{};
    int exit=1;
    try {
        const char* display=std::getenv("DISPLAY");
        const char* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if (!display || !owned || std::string_view(display)!=owned) throw std::runtime_error("owned display required");
        observer.display=XOpenDisplay(nullptr);
        xtst=dlopen("libXtst.so.6",RTLD_NOW);
        if (!observer.display || !xtst) throw std::runtime_error("native input unavailable");
        observer.motion=reinterpret_cast<decltype(observer.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));
        observer.button=reinterpret_cast<decltype(observer.button)>(dlsym(xtst,"XTestFakeButtonEvent"));
        if (!observer.motion || !observer.button) throw std::runtime_error("owned native input unavailable");
        observer.state.documents=a::PublicFixtureDocuments();
        observer.state.documents.Active()->history.enabled=true;
        observer.state.documents.Navigate(a::Section::Send);
        ui::OpenServer(observer.state.server,observer.requests.connection);
        platform::NativeOptions options{960,640,false,1.F,argv[2]};
        options.fixed_delta_seconds=1.F/60;
        platform::NativeHost* current{};
        {
            platform::NativeHost host(options,&observer,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){
                observer.facts=platform::DrawShellFrame(*current,observer.state,observer.requests,theme,appearance);
            });
            current=&host;
            host.Run();
        }
        observer.Check(observer.cleaned && observer.images==3 && observer.stage==9,"complete native extension journey");
        exit=0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n'<<observer.state.presentation.Snapshot();
    }
    if (observer.display) XCloseDisplay(observer.display);
    if (xtst) dlclose(xtst);
    std::ofstream(observer.out/"result.json")<<"{\"selected\":1,\"executed\":1,\"passed\":"<<(exit==0)
        <<",\"failed\":"<<(exit!=0)<<",\"skipped\":0,\"assertions\":"<<observer.checks
        <<",\"captures\":"<<observer.images<<",\"teardown\":"<<(observer.cleaned?"true":"false")<<"}\n";
    return exit;
}
