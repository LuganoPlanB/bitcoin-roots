// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include "tests/platform/camera_fixture.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

using namespace roots::gui;
using namespace std::chrono_literals;
namespace sh=ui::shell;
namespace c=ui::components;
namespace p=protocol;
namespace {
std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path,std::ios::binary);
    if (!input) throw std::runtime_error("Missing owned camera fixture.");
    return {std::istreambuf_iterator<char>(input),{}};
}
struct Requests final:app::Requests {
    std::shared_ptr<test::OwnedVideo> io=std::make_shared<test::OwnedVideo>();
    CameraService capture{platform::LinuxCamera(io)};
    mutable TransactionDocuments local;
    CameraService* Camera() override {return &capture;}
    TransactionDocuments* LocalDocuments() const override {return &local;}
    app::Receipt Submit(const app::Request&) override {throw std::runtime_error("Unexpected external camera action");}
};
struct Observer final:platform::NativeObserver {
    using Clock=std::chrono::steady_clock;
    Clock::time_point timeout=Clock::now()+27s,view_from{};
    Requests requests;
    sh::State state;
    std::filesystem::path evidence,fixtures;
    std::string address,uri;
    uint64_t wallet{},sequence{};
    unsigned stage{},substage{},checks{},images{};
    bool settled{},reported_focus{},reported_open{};
    unsigned last_reported_stage{UINT32_MAX};
    CameraStage reported_camera_stage{CameraStage::Idle};
    DraftRevision before_payment;

    Observer(std::filesystem::path out,std::filesystem::path data)
        :evidence(std::move(out)),fixtures(std::move(data))
    {
        const app::WalletBinding binding{*BackendSessionId::Make("offline-camera-fixture"),
            *WalletId::Make("Camera test wallet"),
            {*Hash256::Parse("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")},{}};
        wallet=state.documents.OpenWallet("Camera test wallet",binding,true);
        Check(wallet && state.documents.Navigate(app::Section::Send),"exact active Regtest Send draft");
        state.documents.Find(wallet)->history.enabled=true;
        address=Read(fixtures/"address.txt");
        uri=Read(fixtures/"uri.txt");
        Check(!ParsePaymentRequest(uri,p::DisplayNetwork::Regtest).error.size(),"valid exact Regtest fixture URI");
        const auto wrong=Read(fixtures/"foreign.txt");
        Check(ParsePaymentRequest(wrong,p::DisplayNetwork::Mainnet).error.empty(),"independently valid foreign-chain address");
        requests.io->Payload(wrong);
        requests.io->Fault(test::CameraFault::Permission);
        before_payment=state.documents.Active()->draft_revision;
    }
    void Check(bool result,std::string_view reason)
    {
        if (!result) throw std::runtime_error("Camera stage "+std::to_string(stage)+"."+std::to_string(substage)+": "+std::string(reason));
        ++checks;
    }
    bool Has(std::string_view text) const {return state.presentation.Snapshot().find(text)!=std::string::npos;}
    bool Action(std::string_view name)
    {
        if (sequence) {
            const auto receipts=state.presentation.exchange.Receipts();
            const auto found=std::ranges::find(receipts,sequence,&c::SemanticReceipt::sequence);
            if (found==receipts.end()) return false;
            sequence=0;
            if (found->result==c::ActionResult::Stale) return false;
            Check(found->result==c::ActionResult::Applied,"production action applied");
            return true;
        }
        const auto packet=state.presentation.exchange.Published();
        if (!packet || !packet->valid) return false;
        const auto node=std::ranges::find_if(packet->nodes,[&](const auto& item) {
            return item.name==name && !item.state.disabled && c::ax::Supports(item.actions,c::ax::SemanticAction::Press);
        });
        if (node==packet->nodes.end()) return false;
        const auto [queued,result]=state.presentation.exchange.QueueNext(packet->generation,node->id,c::ax::SemanticAction::Press);
        Check(result==c::ActionResult::Queued,"native observed control queued");
        sequence=queued;
        return false;
    }
    const SendRecipient& Recipient() const
    {
        const auto& draft=*state.documents.Find(wallet);
        return *draft.recipients.Find(draft.recipients.selected);
    }
    bool Released() const
    {
        const auto stats=requests.io->Observe();
        return !stats.opened && !stats.streaming && stats.opens==stats.closes && stats.maps==stats.unmaps && stats.starts==stats.stops;
    }
    bool CaptureFramebuffer() const override {return true;}
    void Capture(const platform::NativeFrame& frame,const char* file)
    {
        const auto screenshot=evidence/(std::string(file)+".ppm");
        if (std::filesystem::exists(screenshot)) return;
        Check(frame.width==960 && frame.height==640 && frame.font_texture && !frame.gl_error,"real native frame");
        Check(state.presentation.Tree().Validate() && !state.presentation.Tree().overflow,"bounded valid semantics");
        std::ofstream rgb(screenshot,std::ios::binary);
        rgb<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for (int y=frame.framebuffer_height-1;y>=0;--y) for (int x=0;x<frame.framebuffer_width;++x)
            rgb.write(reinterpret_cast<const char*>(frame.rgba.data()+4*(size_t(y)*frame.framebuffer_width+x)),3);
        Check(bool(rgb),"actual preview pixels written");
        std::ofstream(evidence/(std::string(file)+".semantics.txt"))<<state.presentation.Snapshot();
        ++images;
    }
    bool Configure(std::string_view dimensions)
    {
        switch (substage) {
        case 0: if (Action("Find cameras")) ++substage; return false;
        case 1: if (Action("Select Fixture video0")) ++substage; return false;
        case 2: if (Action("Open selected camera")) ++substage; return false;
        case 3: if (Action("Select "+std::string(dimensions)+" · YUYV")) ++substage; return false;
        case 4:
            if (!Action("Start capture")) return false;
            substage=0;
            return true;
        }
        return false;
    }
    void Idle(platform::NativeHost&,const platform::NativeFrame&)
    {
        if (Clock::now()<timeout) return;
        std::ofstream(evidence/"deadline.txt")<<stage<<'.'<<substage<<'\n'<<state.presentation.Snapshot();
        Check(false,"native deadline");
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override
    {
        auto& panel=state.local_document;
        const auto camera=requests.capture.View();
        const bool native_focus=glfwGetWindowAttrib(host.Window(),GLFW_FOCUSED);
        if (last_reported_stage!=stage || reported_focus!=native_focus || reported_camera_stage!=camera.stage
            || reported_open!=panel.dialog.open) {
            std::ofstream(evidence/"stages.log",std::ios::app)<<stage<<'.'<<substage<<" native_focus="<<native_focus
                <<" dialog="<<panel.dialog.open<<" camera="<<int(camera.stage)<<" generation="<<camera.generation
                <<" notice="<<state.notice<<'\n';
            last_reported_stage=stage;
            reported_focus=native_focus;
            reported_camera_stage=camera.stage;
            reported_open=panel.dialog.open;
        }
        switch (stage) {
        case 0:
            if (!native_focus) {glfwFocusWindow(host.Window());return;}
            if (!Action("Import payment QR")) return;
            break;
        case 1:
            if (!panel.dialog.open) return;
            Check(requests.io->Observe().opens==0 && requests.io->Observe().starts==0,"dialog opening performs no camera access");
            if (!Action("Find cameras")) return;
            break;
        case 2:
            if (!Action("Select Fixture video0")) return;
            Check(requests.io->Observe().opens==0,"device discovery and selection remain passive");
            break;
        case 3: if (!Action("Open selected camera")) return; break;
        case 4:
            if (!Has("Camera access was denied") || !Has("Read QR image")) return;
            Check(Released() && Recipient().address.empty(),"permission failure keeps image fallback and draft");
            Capture(frame,"camera-permission-denied");
            requests.io->Fault(test::CameraFault::None);
            break;
        case 5: if (!Action("Open selected camera")) return; break;
        case 6:
            if (!Action("Select 320 × 240 · YUYV")) return;
            Check(requests.io->Observe().starts==0,"opening and mode selection do not capture");
            break;
        case 7:
            if (!Action("Start capture")) return;
            view_from=Clock::now();
            break;
        case 8:
            if (!Has("The address is invalid for this wallet network.") || !Has("Live camera preview")
                || Clock::now()-view_from<250ms) return;
            Check(Recipient().address.empty() && state.documents.Active()->draft_revision==before_payment
                && !Has("Use this payment request"),"foreign-chain QR cannot reach the Send draft");
            Capture(frame,"qr-scan-error");
            requests.io->Payload(uri);
            view_from=Clock::now();
            break;
        case 9:
            if (!sequence && (!Has("Validated payment request") || Clock::now()-view_from<250ms)) return;
            if (!sequence) {
                Check(Recipient().address.empty() && state.documents.Active()->draft_revision==before_payment,
                    "detected and validated payment still awaits deliberate Use");
                Capture(frame,"qr-scan");
            }
            if (!Action("Use this payment request")) return;
            break;
        case 10:
            if (panel.close_pending || panel.dialog.open || !Released()) return;
            Check(Recipient().address==address && Recipient().amount=="0.00001234"
                && state.documents.Active()->draft_revision!=before_payment,"explicit apply copies exact payment into current draft");
            Check(sh::BeginLocalDocument(state,requests,sh::LocalDocumentMode::Qr),"separate explicit PSBT QR import");
            requests.io->Payload(Read(fixtures/"part-first.txt"));
            break;
        case 11: if (!Configure("320 × 240")) return; break;
        case 12:
            if (!Has("Received 1 / 2 frames.")) return;
            Check(state.documents.Documents().size()==1 && !panel.camera_result,"partial stream cannot import");
            requests.io->Payload(Read(fixtures/"part-mixed.txt"));
            view_from=Clock::now();
            break;
        case 13:
            if (!Has("belongs to a different session") || !Has("Received 1 / 2 frames.")
                || Clock::now()-view_from<350ms) return;
            Check(!panel.camera_result && !Has("Import validated PSBT"),"mixed stream rejected without adoption");
            Capture(frame,"camera-mixed-fragments");
            requests.io->Payload(Read(fixtures/"part-last.txt"));
            break;
        case 14:
            if (!Has("PSBT structure validated.") || !panel.camera_result) return;
            Check(state.documents.Documents().size()==1 && panel.camera_result->wire==Read(fixtures/"public.psbt"),
                "complete PSBT validates but remains staged");
            requests.io->Fault(test::CameraFault::Unplug);
            break;
        case 15:
            if (!Has("was disconnected") || !Released()) return;
            Check(!panel.camera_result && !camera.frame && !Has("Import validated PSBT") && Has("Read QR image")
                && state.documents.Documents().size()==1 && Recipient().address==address,
                "unplug retires staged PSBT and leaves the draft and image route");
            requests.io->Fault(test::CameraFault::None);
            requests.io->Payload(Read(fixtures/"part-first.txt"));
            break;
        case 16: if (!Configure("640 × 240")) return; break;
        case 17:
            if (!Has("Received 1 / 2 frames.") || !camera.frame || camera.frame->width!=640) return;
            requests.io->Payload(Read(fixtures/"part-last.txt"));
            break;
        case 18:
            if (!sequence && (!panel.camera_result || !Has("Import validated PSBT"))) return;
            if (!Action("Import validated PSBT")) return;
            break;
        case 19:
            if (panel.dialog.open || panel.close_pending || !Released()) return;
            Check(state.documents.Documents().size()==2 && state.documents.ActiveId()!=wallet
                && state.documents.Active()->imported && state.documents.Active()->imported->parsed->wire==Read(fixtures/"public.psbt")
                && !sh::CurrentSigningApproval(state,requests,state.documents.ActiveId())
                && Recipient().address==address,"exact import confers no approval; original wallet draft retained");
            Check(sh::BeginLocalDocument(state,requests,sh::LocalDocumentMode::Qr),"fresh import for privacy retirement");
            requests.io->Payload(Read(fixtures/"part-first.txt"));
            break;
        case 20: if (!Configure("320 × 240")) return; break;
        case 21:
            if (!Has("Live camera preview") || !camera.frame) return;
            state.semantic_hide_toggle=true;
            break;
        case 22:
            if (panel.dialog.open || panel.close_pending || !Released()) return;
            Check(!camera.frame && !Has("Live camera preview") && state.documents.Documents().size()==2,
                "privacy retires preview, camera, assembly and validation without losing work");
            host.RequestClose();
            break;
        }
        ++stage;
    }
    void Teardown(const platform::NativeTeardown& result) override
    {
        settled=result.renderer_cleared && result.platform_cleared && result.context_cleared
            && result.imgui_cleared && result.input_cleared && result.textures_released;
        Check(settled,"renderer and input settled");
    }
    void Report()
    {
        std::ofstream(evidence/"settlement.txt")<<"stage="<<stage<<" frames="<<images<<" settled="<<settled
            <<" released="<<Released()<<" local_busy="<<requests.local.Busy()<<'\n';
        Check(stage==23 && images==4 && settled && Released() && !requests.local.Busy(),"terminal native capture and map cleanup");
        const auto result=requests.io->Observe();
        Check(result.opens==4 && result.starts==4 && result.closes==4 && result.stops==4
            && result.maps==8 && result.unmaps==8 && result.releases==4 && result.queues==result.dequeues+result.maps,"exact resource pairs across four explicit streams");
        std::ofstream trace(evidence/"controlled-v4l2.log");
        for (const auto& sentence:requests.io->Trace()) trace<<sentence<<'\n';
        std::ofstream(evidence/"native-result.json")<<"{\"checks\":"<<checks<<",\"frames\":"<<images
            <<",\"explicitStreams\":"<<result.starts<<",\"matchedMappings\":"<<result.maps
            <<",\"controlledSyscalls\":true,\"networkDenied\":true,\"settled\":true}\n";
    }
};
}
int main(int argc,char** argv)
{
    if (argc!=4) return 2;
    try {
        const auto* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        const auto* display=std::getenv("DISPLAY");
        if (!owned || !display || std::string_view(display)!=owned) throw std::runtime_error("Owned display required.");
        for (const int family:{AF_INET,AF_INET6}) {
            const int fd=socket(family,SOCK_STREAM,0);
            const bool denied=fd<0 && errno==EPERM;
            if (fd>=0) close(fd);
            if (!denied) throw std::runtime_error("Network fence required.");
        }
        Observer observer(argv[1],argv[3]);
        platform::NativeOptions options{960,640,false,1.F,argv[2]};
        options.fixed_delta_seconds=1.F/60;
        options.accessibility=&observer.state.presentation.exchange;
        options.on_draw_suspended=[&] {
            if (observer.state.local_document.camera_generation) {
                observer.state.notice="Fixture window drawing suspended.";
                sh::RetireLocalDocument(observer.state,observer.requests);
            }
        };
        platform::NativeHost* active{};
        {
            platform::NativeHost host(options,&observer,[&](const auto& theme,const auto& appearance) {
                const auto before=observer.state.local_document.context;
                const auto capturing=observer.state.local_document.camera_generation;
                auto* owner=observer.state.documents.Active();
                const auto before_revision=owner ? owner->draft_revision.value : 0;
                platform::DrawShellFrame(*active,observer.state,observer.requests,theme,appearance);
                if (capturing && !observer.state.local_document.dialog.open) {
                    owner=observer.state.documents.Active();
                    std::ofstream(observer.evidence/"retired.log",std::ios::app)
                        <<"retired stage "<<observer.stage<<" focus="<<glfwGetWindowAttrib(active->Window(),GLFW_FOCUSED)
                        <<" context="<<before.generation<<'/'<<before.document<<'/'<<before.revision.value
                        <<" observed_before="<<before_revision<<" observed_after="<<(owner ? owner->draft_revision.value : 0)
                        <<" reason="<<observer.state.notice<<'\n';
                }
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
