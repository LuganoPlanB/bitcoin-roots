// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include "app/history_workspace.h"

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
namespace ax=ui::components::ax;
namespace a=app;
namespace {
constexpr auto GENESIS="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
struct Requests final:a::Requests {
    a::RecoverySession recovery;
    std::atomic<unsigned> probes{},wallets{},receives{},commands{};
    ConnectionService connection{[this](const ServerCandidate&,std::stop_token){++probes;return BackendCapabilities{};}};
    WalletLifecycle lifecycle{[this](const WalletOperation&,std::stop_token){++wallets;return WalletResult{};}};
    ReceiveService receive{[this](const ReceiveRequest&,std::stop_token){++receives;return ReceiveReply{};}};
    explicit Requests(const std::filesystem::path& path):recovery(path) {}
    ConnectionService* Connection() override{return &connection;}
    WalletLifecycle* Wallets() override{return &lifecycle;}
    ReceiveService* Receiving() override{return &receive;}
    a::RecoverySession* Recovery() override{return &recovery;}
    a::Receipt Submit(const a::Request&) override{++commands;return {false,{}};}
};

struct Observer final:platform::NativeObserver {
    Requests host;
    sh::State state;
    sh::Facts facts;
    std::filesystem::path out;
    Display* display{};
    int(*motion)(Display*,int,int,int,unsigned long){};
    int(*button)(Display*,unsigned int,Bool,unsigned long){};
    unsigned step{},checks{},images{};
    uint64_t wallet{}, press{};
    bool cleaned{};
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    Observer(const std::filesystem::path& output,const std::filesystem::path& metadata):host(metadata),out(output) {}

    void Check(bool ok,const char* why)
    {
        if (!ok) throw std::runtime_error(why);
        ++checks;
    }
    bool CaptureFramebuffer() const override{return true;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override
    {
        if (std::chrono::steady_clock::now()-started>std::chrono::seconds(15))
            throw std::runtime_error("native custody deadline at stage "+std::to_string(step));
    }
    void Click(platform::NativeHost& native,const ui::components::ItemFacts& item,bool down)
    {
        const auto& rect=item.rect;
        Check(item.visible && !item.disabled && rect[0]>=0 && rect[1]>=0 && rect[2]<=960 && rect[3]<=640,
            "reachable current native control");
        int x{},y{};
        glfwGetWindowPos(native.Window(),&x,&y);
        Check(motion(display,-1,x+int((rect[0]+rect[2])/2),y+int((rect[1]+rect[3])/2),0)
            && button(display,1,down,0),"owned pointer action");
        XSync(display,False);
    }
    void Capture(const platform::NativeFrame& frame,const ui::components::SemanticPacket& packet,const char* name)
    {
        Check(frame.width==960 && frame.height==640 && frame.effective_scale==1.0F && frame.decorated && !frame.gl_error,
            "actual Linux X11 viewport");
        std::ofstream image(out/(std::string(name)+".ppm"),std::ios::binary);
        image<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for (int y=frame.framebuffer_height-1;y>=0;--y)
            for (int x=0;x<frame.framebuffer_width;++x)
                image.write(reinterpret_cast<const char*>(frame.rgba.data()+(size_t(y)*frame.framebuffer_width+x)*4),3);
        Check(bool(image),"capture framebuffer");
        std::ofstream text(out/(std::string(name)+".published.txt"));
        for (const auto& node:packet.nodes)
            text<<node.name<<'|'<<node.value<<'|'<<node.description
                <<" | focused="<<node.state.focused<<" disabled="<<node.state.disabled
                <<" showing="<<node.showing<<" actions="<<static_cast<unsigned>(node.actions)<<'\n';
        ++images;
    }
    void Frame(platform::NativeHost& native,const platform::NativeFrame& frame) override
    {
        if (frame.rendered_frames<3) return;
        const auto packet=state.presentation.exchange.Published();
        const auto node=[&](std::string_view name)->const ui::components::SemanticEntry* {
            if (!packet || !packet->valid || packet->overflow) return nullptr;
            const auto found=std::ranges::find(packet->nodes,name,&ui::components::SemanticEntry::name);
            return found==packet->nodes.end() ? nullptr : &*found;
        };
        const auto press_current=[&](const ui::components::SemanticEntry& action) {
            const auto [sequence, status]=state.presentation.exchange.QueueNext(packet->generation,action.id,ax::SemanticAction::Press);
            Check(status==ui::components::ActionResult::Queued && action.showing && !action.state.disabled,
                "current published Receive action is reachable");
            press=sequence;
        };
        const auto verify_press=[&] {
            const auto receipts=state.presentation.exchange.Receipts();
            const auto applied=std::ranges::find(receipts,press,&ui::components::SemanticReceipt::sequence);
            Check(applied!=receipts.end() && applied->result==ui::components::ActionResult::Applied,
                "current Receive action completed through the shell");
        };
        switch(step) {
        case 0: if (!facts.onboarding.contains("method-words")) return;Click(native,facts.onboarding.at("method-words"),true);break;
        case 1: Click(native,facts.onboarding.at("method-words"),false);break;
        case 2:
            if (!node("Local software wallet unavailable") || !node("Choose another method")) return;
            Check(state.presentation.exchange.QueueNext(packet->generation,node("Choose another method")->id,ax::SemanticAction::Focus).second
                ==ui::components::ActionResult::Queued,"credential-free route supports native focus");
            break;
        case 3:
            if (!node("Choose another method") || !node("Choose another method")->state.focused) return;
            Check(node("Local software wallet unavailable")->value.find("LocalSignerService")!=std::string::npos,
                "actual missing isolated provider is named");
            Check(node("Trusted signer entry") && node("Trusted signer entry")->value.find("mnemonic passphrase")!=std::string::npos,
                "typed ownership explanation in current scope");
            Check(std::ranges::none_of(packet->nodes,[](const auto& item) {return item.role==ax::SemanticRole::TextField;}),
                "no credential fields in the published modal");
            Capture(frame,*packet,"local-software-wallet-unavailable");
            Click(native,facts.onboarding.at("cancel"),true);
            break;
        case 4: Click(native,facts.onboarding.at("cancel"),false);break;
        case 5:
            if (state.onboarding.dialog.open || !node("Reusable policies")) return;
            press_current(*node("Reusable policies"));
            break;
        case 6:
            if (!node("Reusable policy unavailable") || !node("Back to standard Receive") || !node("Back to standard Receive")->state.focused)
                return;
            verify_press();
            Check(node("Fixed descriptor") && node("Silent payments") && node("Payment code"),
                "real Receive view lists public fields by family");
            Check(!node("Get Next Address") && !node("Use reserved address") && !node("Receive address"),
                "reusable policy has no derived address or ordinary pool actions");
            Check(!host.probes && !host.wallets && !host.receives && !host.commands,"unsupported views dispatch no host request");
            Capture(frame,*packet,"reusable-policy-unavailable");
            press_current(*node("Back to standard Receive"));
            break;
        case 7: {
            if (!node("Reusable policies") || !node("Reusable policies")->state.focused) return;
            verify_press();
            const auto* current=state.documents.Active();
            const auto pool=host.recovery.ReceivePool(a::HistoryMetadataScope(*current->binding));
            Check(current->id==wallet && pool && pool.value->retained==0 && pool.value->pending==0,
                "return to same wallet without touching reserved pool");
            Check(!host.probes && !host.wallets && !host.receives && !host.commands,"navigation alone has no transport or mutation");
            native.RequestClose();
            break;
        }
        default: return;
        }
        ++step;
    }
    void Teardown(const platform::NativeTeardown& state) override
    {
        cleaned=state.renderer_cleared && state.platform_cleared && state.textures_released
            && state.input_cleared && state.imgui_cleared && state.context_cleared;
        Check(cleaned,"owned renderer teardown");
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
        observer.wallet=observer.state.documents.OpenWallet("Public Regtest test wallet",
            {*BackendSessionId::Make("fixture-rpc"),*WalletId::Make("fixture-wallet"),{*Hash256::Parse(GENESIS)}},true);
        observer.state.documents.Active()->history.enabled=true;
        observer.state.documents.Navigate(a::Section::Receive);
        sh::BeginOnboarding(observer.state,sh::OnboardingPage::Import,observer.host);
        platform::NativeOptions options{960,640,false,1.F,argv[2]};
        options.fixed_delta_seconds=1.F/60;
        platform::NativeHost* current{};
        {
            platform::NativeHost native(options,&observer,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){
                observer.facts=platform::DrawShellFrame(*current,observer.state,observer.host,theme,appearance);
            });
            current=&native;
            native.Run();
        }
        observer.Check(observer.cleaned && observer.images==2 && observer.step==8,"complete native custody journey");
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
