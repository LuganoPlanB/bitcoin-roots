// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <algorithm>
#include <chrono>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
namespace sh=ui::shell;
namespace c=ui::components;
namespace a=app;
namespace {
struct Requests final:a::Requests {
    a::RecoverySession recovery;
    std::atomic<unsigned> probes{}, receives{}, commands{};
    ConnectionService connection{[this](const ServerCandidate&,std::stop_token){++probes;return BackendCapabilities{};}};
    ReceiveService receive{[this](const ReceiveRequest&,std::stop_token){++receives;return ReceiveReply{};}};
    explicit Requests(const std::filesystem::path& path):recovery(path) {}
    ConnectionService* Connection() override { return &connection; }
    ReceiveService* Receiving() override { return &receive; }
    a::RecoverySession* Recovery() override { return &recovery; }
    a::Receipt Submit(const a::Request&) override { ++commands;return {false,{}}; }
};
struct Observer final:platform::NativeObserver {
    Requests requests;
    sh::State state;
    sh::Facts facts;
    std::filesystem::path out;
    Display* display{};
    int(*motion)(Display*,int,int,int,unsigned long){};
    int(*button)(Display*,unsigned int,Bool,unsigned long){};
    unsigned step{},checks{},captures{};
    uint64_t wallet{}, focus_sequence{}, close_sequence{}, focus_received_frame{};
    bool cleaned{};
    const std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    Observer(const std::filesystem::path& path,const std::filesystem::path& metadata):requests(metadata),out(path) {}

    void Check(bool ok,const char* reason)
    {
        if (!ok) throw std::runtime_error(reason);
        ++checks;
    }
    bool CaptureFramebuffer() const override { return true; }
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override
    {
        if (std::chrono::steady_clock::now()-started>std::chrono::seconds(15)) {
            for (const auto& receipt:state.presentation.exchange.Receipts())
                std::cerr<<"Native semantic receipt "<<receipt.sequence<<" result="<<int(receipt.result)<<'\n';
            throw std::runtime_error("native payment protocol deadline at stage "+std::to_string(step));
        }
    }
    void Click(platform::NativeHost& host,const c::ItemFacts& item,bool down)
    {
        const auto& rect=item.rect;
        Check(item.visible && !item.disabled && rect[0]>=0 && rect[1]>=0 && rect[2]<=960 && rect[3]<=640,
            "current Contacts action is reachable");
        int x{},y{};
        glfwGetWindowPos(host.Window(),&x,&y);
        Check(motion(display,-1,x+int((rect[0]+rect[2])/2),y+int((rect[1]+rect[3])/2),0)
            && button(display,1,down,0),"owned native pointer action");
        XSync(display,False);
    }
    void Capture(const platform::NativeFrame& frame,const c::SemanticPacket& packet,const char* name)
    {
        Check(frame.width==960 && frame.height==640 && frame.effective_scale==1.F && frame.decorated && !frame.gl_error,
            "actual Linux X11 framebuffer");
        std::ofstream image(out/(std::string(name)+".ppm"),std::ios::binary);
        image<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for (int y=frame.framebuffer_height-1;y>=0;--y)
            for (int x=0;x<frame.framebuffer_width;++x)
                image.write(reinterpret_cast<const char*>(frame.rgba.data()+(size_t(y)*frame.framebuffer_width+x)*4),3);
        Check(bool(image),"framebuffer retained");
        std::ofstream text(out/(std::string(name)+".published.txt"));
        for (const auto& node:packet.nodes)
            text<<node.name<<'|'<<node.value<<'|'<<node.description<<" | focused="<<node.state.focused
                <<" disabled="<<node.state.disabled<<" showing="<<node.showing<<" actions="<<static_cast<unsigned>(node.actions)<<'\n';
        Check(bool(text),"published semantic packet retained");
        ++captures;
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override
    {
        if (frame.rendered_frames<4) return;
        const auto packet=state.presentation.exchange.Published();
        const auto node=[&](std::string_view name)->const c::SemanticEntry* {
            if (!packet || !packet->valid || packet->overflow) return nullptr;
            const auto found=std::ranges::find(packet->nodes,name,&c::SemanticEntry::name);
            return found==packet->nodes.end() ? nullptr : &*found;
        };
        const auto applied=[&](uint64_t sequence) {
            const auto receipts=state.presentation.exchange.Receipts();
            const auto receipt=std::ranges::find(receipts,sequence,&c::SemanticReceipt::sequence);
            return receipt!=receipts.end() && receipt->result==c::ActionResult::Applied;
        };
        switch(step) {
        case 0:
            if (!node("Tools")) return;
            Check(state.presentation.exchange.QueueNext(packet->generation,node("Tools")->id,c::ax::SemanticAction::Press).second
                ==c::ActionResult::Queued,"open the real Tools menu");
            break;
        case 1: {
            const auto menu=facts.menu_rows.find(a::Command::Contacts);
            if (menu==facts.menu_rows.end() || !menu->second.visible) return;
            Click(host,menu->second,true);
            break;
        }
        case 2: Click(host,facts.menu_rows.at(a::Command::Contacts),false);break;
        case 3: {
            const auto* close=node("Close");
            if (!facts.payment_protocol_visible || !close || !close->state.focused) return;
            const auto [sequence,status]=state.presentation.exchange.QueueNext(packet->generation,close->id,c::ax::SemanticAction::Focus);
            Check(status==c::ActionResult::Queued,"focus the current Close button from its published packet");
            focus_sequence=sequence;
            break;
        }
        case 4: {
            const auto* close=node("Close");
            if (!facts.payment_protocol_visible || !close || !close->state.focused || !applied(focus_sequence)) return;
            // Applying the semantic request queues toolkit focus for the next
            // frame. Capture once that focus is reflected in the framebuffer.
            if (!focus_received_frame) focus_received_frame=frame.rendered_frames;
            if (uint64_t(frame.rendered_frames)<focus_received_frame+2) return;
            Check(close->showing && !close->state.disabled,"focus applied to the visible current Close button");
            Check(state.invocation && state.invocation->command==a::Command::Contacts
                && state.invocation->accepted && !state.invocation->dispatched,"navigation never dispatches a protocol operation");
            Check(node("Payment protocols unavailable") && node("Payjoin") && node("Payment-code contacts")
                && node("Reusable private addresses") && node("Disclosure and review"),"accurate missing protocol and permission explanations");
            Check(std::ranges::none_of(packet->nodes,[&](const auto& item){
                return item.role==c::ax::SemanticRole::TextField || (unsigned(item.actions) && item.id!=close->id);
            }),"Close is the only interactive action and no endpoint or contact input is published");
            Check(!requests.probes && !requests.receives && !requests.commands,"no connection, Receive, or host operation during navigation");
            Capture(frame,*packet,"payment-protocol-unavailable");
            const auto [sequence,status]=state.presentation.exchange.QueueNext(packet->generation,close->id,c::ax::SemanticAction::Press);
            Check(status==c::ActionResult::Queued,"current Close action is reachable");
            close_sequence=sequence;
            break;
        }
        case 5: {
            if (state.payment_protocol.dialog.open || !node("Tools")) return;
            Check(applied(close_sequence),"current modal closed through its published action");
            const auto* document=state.documents.Active();
            Check(document && document->id==wallet && document->sections[4].note.draft()=="Public draft retained"
                && document->section==a::Section::Receive,"exact wallet, draft and Receive view preserved");
            Check(!requests.probes && !requests.receives && !requests.commands,"close creates no host or transport action");
            host.RequestClose();
            break;
        }
        default: return;
        }
        ++step;
    }
    void Teardown(const platform::NativeTeardown& result) override
    {
        cleaned=result.renderer_cleared && result.platform_cleared && result.textures_released
            && result.input_cleared && result.imgui_cleared && result.context_cleared;
        Check(cleaned,"owned native resources released");
    }
};
}
int main(int argc,char** argv)
{
    if (argc!=4) return 2;
    Observer observer(argv[1],argv[3]);
    std::filesystem::create_directories(observer.out);
    void* xtst{};
    int exit=1;
    try {
        const char* display=std::getenv("DISPLAY");
        const char* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if (!display || !owned || std::string_view(display)!=owned) throw std::runtime_error("owned display required");
        observer.display=XOpenDisplay(nullptr);
        xtst=dlopen("libXtst.so.6",RTLD_NOW);
        if (!observer.display || !xtst) throw std::runtime_error("owned input unavailable");
        observer.motion=reinterpret_cast<decltype(observer.motion)>(dlsym(xtst,"XTestFakeMotionEvent"));
        observer.button=reinterpret_cast<decltype(observer.button)>(dlsym(xtst,"XTestFakeButtonEvent"));
        if (!observer.motion || !observer.button) throw std::runtime_error("owned input symbols unavailable");
        observer.wallet=observer.state.documents.OpenWallet("Regtest protocol fixture",
            {*BackendSessionId::Make("fixture-rpc"),*WalletId::Make("fixture-wallet"),
                {*Hash256::Parse("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")}},true);
        observer.state.documents.Active()->history.enabled=true;
        observer.state.documents.Active()->sections[4].note.SetDraft("Public draft retained");
        observer.state.documents.Navigate(a::Section::Receive);
        platform::NativeOptions options{960,640,false,1.F,argv[2]};
        options.fixed_delta_seconds=1.F/60;
        platform::NativeHost* current{};
        {
            platform::NativeHost native(options,&observer,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){
                observer.facts=platform::DrawShellFrame(*current,observer.state,observer.requests,theme,appearance);
            });
            current=&native;
            native.Run();
        }
        observer.Check(observer.cleaned && observer.captures==1 && observer.step==6,"complete actual unavailable journey");
        exit=0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n'<<observer.state.presentation.Snapshot();
    }
    if (observer.display) XCloseDisplay(observer.display);
    if (xtst) dlclose(xtst);
    std::ofstream(observer.out/"result.json")<<"{\"selected\":1,\"executed\":1,\"passed\":"<<(exit==0)
        <<",\"failed\":"<<(exit!=0)<<",\"skipped\":0,\"assertions\":"<<observer.checks
        <<",\"captures\":"<<observer.captures<<",\"teardown\":"<<(observer.cleaned?"true":"false")<<"}\n";
    return exit;
}
