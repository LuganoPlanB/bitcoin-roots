// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/history_workspace.h"
#include "platform/shell_composition.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
using namespace std::chrono_literals;
namespace a=app;
namespace c=ui::components;
namespace p=protocol;
namespace sh=ui::shell;
namespace {
constexpr auto GENESIS="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
constexpr auto WALLET="usb fixture & test";

struct Requests final:a::Requests {
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    ConnectionService connection{[this](const auto& profile,std::stop_token stop){return ProbeCapabilities(transport,profile,stop);}};
    DeviceSigners devices{transport};
    ReceiveService receive{[this](const auto& request,std::stop_token stop){return FetchReceive(transport,request,stop);}};
    a::RecoverySession recovery;
    explicit Requests(const std::filesystem::path& scratch):recovery(scratch/"metadata.sqlite") {}
    ConnectionService* Connection() override {return &connection;}
    DeviceSigners* Devices() override {return &devices;}
    ReceiveService* Receiving() override {return &receive;}
    a::RecoverySession* Recovery() override {return &recovery;}
    a::Receipt Submit(const a::Request&) override {throw std::runtime_error("Unexpected native signer command.");}
};

struct Observer final:platform::NativeObserver {
    using Clock=std::chrono::steady_clock;
    const Clock::time_point deadline=Clock::now()+25s;
    std::filesystem::path output;
    Requests requests;
    sh::State state;
    ReceiveTarget target;
    uint64_t wallet{},queued{};
    unsigned stage{},checks{},frames{};
    bool waiting_frame{}, renderer_settled{};

    Observer(std::filesystem::path out,const std::filesystem::path& scratch,const char* port,const char* cookie,const char* descriptor)
        :output(std::move(out)),requests(scratch)
    {
        Check(requests.recovery.Start(),"owned disposable metadata");
        ServerCandidate profile;
        profile.connection.port=uint16_t(std::stoul(port));
        profile.connection.credential.reference=cookie;
        profile.expected_genesis=GENESIS;
        profile.wallet=WALLET;
        auto& connection=requests.connection;
        Check(connection.SetDraft(profile) && connection.Test(),"real loopback capability probe");
        while (connection.Busy() && Clock::now()<deadline) {
            connection.Poll();
            std::this_thread::sleep_for(2ms);
        }
        Check(connection.Apply() && CanVerifyReceiveOnDevice(*connection.Active()),"proof of exact emulated RPC methods and wallet");
        const auto binding=a::VerifiedWalletBinding(*connection.Active());
        Check(binding.has_value(),"verified wallet context");
        wallet=state.documents.OpenWallet(WALLET,*binding,true);
        Check(wallet && state.documents.Navigate(a::Section::Receive),"current Receive view");
        state.documents.Find(wallet)->history.enabled=true;
        const auto validated=p::DerivePublicDescriptor(descriptor,"regtest",0);
        Check(validated.has_value(),"exact owned address fixture");
        target={validated->address,validated->script,validated->descriptor,"m/0/0","regtest",{},1};
        Check(p::VerifyReceiveTarget(target),"independent exact Receive address script");
        auto& runtime=state.receives[wallet];
        runtime.scope=a::HistoryMetadataScope(*binding);
        runtime.selection=sh::ReceiveSelection{target,sh::ReceiveSource::Inventory};
    }
    void Check(bool accepted,std::string_view label)
    {
        if (!accepted) throw std::runtime_error("device stage "+std::to_string(stage)+": "+std::string(label));
        ++checks;
    }
    bool CaptureFramebuffer() const override {return true;}
    bool Has(std::string_view label) const {return state.presentation.Snapshot().find(label)!=std::string::npos;}
    bool Press(std::string_view label)
    {
        if (queued) {
            const auto receipts=state.presentation.exchange.Receipts();
            const auto matching=std::ranges::find(receipts,queued,&c::SemanticReceipt::sequence);
            if (matching==receipts.end()) return false;
            queued=0;
            if (matching->result==c::ActionResult::Stale) return false;
            Check(matching->result==c::ActionResult::Applied,"native action applied");
            return true;
        }
        const auto tree=state.presentation.exchange.Published();
        if (!tree || !tree->valid) return false;
        const auto node=std::ranges::find_if(tree->nodes,[&](const auto& item) {
            return item.name==label && !item.state.disabled && c::ax::Supports(item.actions,c::ax::SemanticAction::Press);
        });
        if (node==tree->nodes.end()) return false;
        const auto [sequence,result]=state.presentation.exchange.QueueNext(tree->generation,node->id,c::ax::SemanticAction::Press);
        Check(result==c::ActionResult::Queued,"actual native semantic button queued");
        queued=sequence;
        return false;
    }
    void Capture(const platform::NativeFrame& frame,const char* name)
    {
        Check(frame.width==960 && frame.height==640 && frame.font_texture && !frame.gl_error,"actual owned native renderer");
        Check(state.presentation.Tree().Validate() && !state.presentation.Tree().overflow,"current bounded semantics");
        std::ofstream image(output/(std::string(name)+".ppm"),std::ios::binary);
        image<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for (int y=frame.framebuffer_height-1;y>=0;--y) for (int x=0;x<frame.framebuffer_width;++x)
            image.write(reinterpret_cast<const char*>(frame.rgba.data()+4*(size_t(y)*frame.framebuffer_width+x)),3);
        Check(bool(image),"actual framebuffer retained");
        std::ofstream(output/(std::string(name)+".semantics.txt"))<<state.presentation.Snapshot();
        ++frames;
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override
    {
        if (frame.rendered_frames<5) return;
        auto& runtime=state.receives.at(wallet);
        switch (stage) {
        case 0:
            if (!glfwGetWindowAttrib(host.Window(),GLFW_FOCUSED)) {glfwFocusWindow(host.Window());return;}
            Check(!requests.devices.Current(&requests.connection),"connection never grants device authority");
            if (!Press("Rescan devices")) return;
            break;
        case 1:
            if (!Has("Select matching signer") || !Has("Controlled signer emulator")) return;
            Capture(frame,"device-selection");
            break;
        case 2:
            if (!Press("Select matching signer")) return;
            break;
        case 3:
            if (!Press("Verify on device")) return;
            break;
        case 4:
            if (!Has("Check this exact address on the device") || !requests.receive.Busy()) return;
            Capture(frame,"device-confirmation");
            waiting_frame=true;
            break;
        case 5:
            if (!Has("The daemon signer echoed this exact address")) return;
            Check(waiting_frame && runtime.selection && runtime.selection->target==target,"exact selected address retained");
            Capture(frame,"device-address-echo");
            break;
        case 6:
            if (!Press("Verify on device")) return;
            break;
        case 7:
            if (!Has("The daemon signer request failed") || requests.receive.Busy()) return;
            Check(runtime.selection && runtime.selection->target==target && !requests.devices.Current(&requests.connection),
                "wrong echo rejected and existing Receive address retained");
            Capture(frame,"device-address-rejected");
            state.semantic_hide_toggle=true;
            break;
        case 8:
            if (!state.privacy.hidden) return;
            Check(!requests.devices.Current(&requests.connection) && !Has("Daemon signer status")
                && !Has("Controlled signer emulator") && runtime.selection && runtime.selection->target == target,
                "Hide Amounts retires device selection and keeps the existing Receive address");
            host.RequestClose();
            break;
        }
        ++stage;
    }
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override
    {
        if (Clock::now()<deadline) return;
        std::ofstream(output/"deadline.txt")<<stage<<'\n'<<state.presentation.Snapshot();
        Check(false,"native signer deadline");
    }
    void Teardown(const platform::NativeTeardown& result) override
    {
        renderer_settled=result.renderer_cleared && result.platform_cleared && result.context_cleared && result.input_cleared;
        Check(renderer_settled,"settled native renderer and input");
    }
    void Report()
    {
        Check(stage==9 && frames==4 && !requests.receive.Busy() && renderer_settled,"all real view stages settled");
        std::ofstream(output/"native-result.json")<<"{\"journey\":\"device-selection-and-confirmation\",\"checks\":"<<checks
            <<",\"actualNativeFrames\":"<<frames<<",\"controlledRpc\":true,\"settled\":true}\n";
    }
};
}

int main(int argc,char** argv)
{
    if (argc!=7) return 2;
    try {
        const auto* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        const auto* display=std::getenv("DISPLAY");
        if (!owned || !display || std::string_view(owned)!=display) throw std::runtime_error("Owned display required.");
        Observer observer(argv[1],argv[3],argv[4],argv[5],argv[6]);
        platform::NativeOptions options{960,640,false,1.F,argv[2]};
        options.fixed_delta_seconds=1.F/60;
        options.accessibility=&observer.state.presentation.exchange;
        platform::NativeHost* active{};
        {
            platform::NativeHost host(options,&observer,[&](const auto& theme,const auto& appearance) {
                platform::DrawShellFrame(*active,observer.state,observer.requests,theme,appearance);
            });
            active=&host;
            host.Run();
        }
        observer.Report();
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
