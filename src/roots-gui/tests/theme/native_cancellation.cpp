// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/broadcast.h"
#include "app/connection_observations.h"
#include "app/daemon_signer.h"
#include "app/history_workspace.h"
#include "platform/shell_composition.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>

using namespace roots::gui;
using namespace std::chrono_literals;
namespace a=app;
namespace c=ui::components;
namespace p=protocol;
namespace sh=ui::shell;
namespace {
constexpr auto GENESIS="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
constexpr auto WALLET="fee-wallet";

struct Requests final : a::Requests {
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    ConnectionService connection{[this](const auto& candidate,std::stop_token stop){return ProbeCapabilities(transport,candidate,stop);}};
    RefreshService refresh{[this](const auto& value,std::stop_token stop){return FetchRefresh(transport,value,stop);}};
    a::Broadcaster broadcaster{transport,connection};
    a::DaemonSigner signer{transport,connection};
    a::Replacements replacements{transport,connection};
    a::RecoverySession recovery;
    mutable TransactionDocuments local;
    explicit Requests(const std::filesystem::path& home):recovery(home/"metadata.sqlite"){}
    ConnectionService* Connection() override {return &connection;}
    RefreshService* Refresh() override {return &refresh;}
    a::RecoverySession* Recovery() override {return &recovery;}
    a::Broadcaster* Broadcasts() override {return &broadcaster;}
    a::DaemonSigner* Signer() override {return &signer;}
    a::Replacements* ReplacementDrafts() override {return &replacements;}
    TransactionDocuments* LocalDocuments() const override {return &local;}
    a::HostObservations Observe() const override {return a::ConnectionObservations(connection,refresh);}
    a::Receipt Submit(const a::Request&) override {throw std::runtime_error("Unexpected fixture command.");}
};

std::string Read(const std::filesystem::path& path)
{
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),{}};
}

struct Observer final : platform::NativeObserver {
    using Clock=std::chrono::steady_clock;
    const Clock::time_point deadline=Clock::now()+55s;
    std::filesystem::path output,source;
    Requests requests;
    sh::State state;
    sh::Facts facts;
    uint64_t wallet{},candidate{},queued{};
    int stage{},checks{},frames{},trial{};
    bool overlap{},settled{};
    struct Trial {const char* mode; const char* row; const char* reason;};
    static constexpr Trial trials[]{
        {"nonreplaceable","plain","does not explicitly signal"},
        {"wrong-wallet","eligible","exact descriptor wallet changed"},
        {"held","eligible","cancellation draft request was retired"},
        {"valid","eligible",""},
    };

    void Check(bool value,std::string_view label)
    {
        if (!value) throw std::runtime_error("Cancellation stage "+std::to_string(stage)+
            ", trial "+std::to_string(trial)+": "+std::string(label));
        ++checks;
    }
    Observer(std::filesystem::path out,std::filesystem::path home,const char* port,const char* cookie):
        output(std::move(out)),source(std::move(home)),requests(source)
    {
        Check(requests.recovery.Start(),"owned recovery store");
        ServerCandidate profile;
        profile.connection.port=uint16_t(std::stoul(port));
        profile.connection.credential.reference=cookie;
        profile.expected_genesis=GENESIS;
        profile.wallet=WALLET;
        Check(requests.connection.SetDraft(profile) && requests.connection.Test(),"owned node capability check");
        const auto end=Clock::now()+10s;
        while (requests.connection.Busy() && Clock::now()<end) {
            requests.connection.Poll();
            std::this_thread::sleep_for(2ms);
        }
        Check(requests.connection.Apply(),"verified exact wallet");
        const auto binding=a::VerifiedWalletBinding(*requests.connection.Active());
        Check(binding && binding->DaemonWallet()==WALLET,"exact wallet and genesis");
        wallet=state.documents.OpenWallet(WALLET,*binding,true);
        state.documents.Find(wallet)->history.enabled=true;
    }
    bool CaptureFramebuffer() const override {return true;}
    bool Has(std::string_view label) const {return state.presentation.Snapshot().find(label)!=std::string::npos;}
    std::string Row(std::string_view role) const {return Read(source/(std::string(role)+".txt"));}
    void Mode(std::string_view mode) {std::ofstream(source/"mode")<<mode;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override
    {
        if (Clock::now()<deadline) return;
        std::ofstream(output/"last.semantics.txt")<<state.presentation.Snapshot();
        Check(false,"native deadline");
    }
    bool Action(std::string_view label,c::ax::SemanticAction action=c::ax::SemanticAction::Press,std::string value={})
    {
        if (queued) {
            const auto receipts=state.presentation.exchange.Receipts();
            const auto found=std::ranges::find(receipts,queued,&c::SemanticReceipt::sequence);
            if (found==receipts.end()) return false;
            queued=0;
            if (found->result==c::ActionResult::Stale) return false;
            Check(found->result==c::ActionResult::Applied,"native action applied");
            return true;
        }
        const auto packet=state.presentation.exchange.Published();
        if (!packet || !packet->valid) return false;
        const auto node=std::ranges::find_if(packet->nodes,[&](const auto& item) {
            return item.name==label && !item.state.disabled && c::ax::Supports(item.actions,action);
        });
        if (node==packet->nodes.end()) return false;
        const auto [sequence,result]=state.presentation.exchange.QueueNext(packet->generation,node->id,action,std::move(value));
        Check(result==c::ActionResult::Queued,"native action queued");
        queued=sequence;
        return false;
    }
    a::Receipt Dispatch(a::Command command)
    {
        return sh::DispatchSigningCommand(state,requests,{command,candidate});
    }
    bool Awaiting() const {return requests.replacements.Busy() || requests.broadcaster.Busy() || requests.signer.Busy();}
    void Capture(const platform::NativeFrame& frame,const char* name)
    {
        Check(frame.width==960 && frame.height==640 && frame.effective_scale==1.F && frame.font_texture && !frame.gl_error,
            "actual owned X11 renderer");
        Check(state.presentation.Tree().Validate() && !state.presentation.Tree().overflow,"bounded native semantics");
        std::ofstream image(output/(std::string(name)+".ppm"),std::ios::binary);
        image<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for (int y=frame.framebuffer_height-1;y>=0;--y) for (int x=0;x<frame.framebuffer_width;++x)
            image.write(reinterpret_cast<const char*>(frame.rgba.data()+(size_t(y)*frame.framebuffer_width+x)*4),3);
        Check(bool(image),"captured real framebuffer");
        std::ofstream(output/(std::string(name)+".semantics.txt"))<<state.presentation.Snapshot();
        ++frames;
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override
    {
        auto* history=state.documents.Find(wallet);
        const auto view=a::HistoryForDocument(*history,requests.refresh,requests.recovery.Gate().Epoch());
        const bool current=view && view->chain_current && view->ledger->InventoryAvailable() && view->ledger->LocksAvailable();
        switch (stage) {
        case 0:
            if (!current || !view->ledger->Rows().contains(Row("eligible"))) return;
            history->history.selected=Row("eligible");
            ++stage;
            return;
        case 1:
            if (Action("Fee actions")) ++stage;
            return;
        case 2:
            if ((!current && !queued) || !Action("New fee rate (sat/vB)",c::ax::SemanticAction::SetValue,"12.345")) return;
            ++stage;
            return;
        case 3: {
            if (!current) return;
            const auto& next=trials[trial];
            Mode(next.mode);
            history->history.selected=Row(next.row);
            const a::FeeDraftRequest direct{view->identity->context,*Hash256::Parse(history->history.selected),
                a::FeeDraftKind::CancelPayment,view->generation,view->history_sequence,12345};
            Check(!sh::BeginReplacement(state,requests,direct).accepted,"injected action without warning refused");
            ++stage;
            return;
        }
        case 4:
            if (Action("Attempt cancellation")) ++stage;
            return;
        case 5:
            if (!queued && !Has("Cancellation is never guaranteed")) {
                if (!state.histories.at(wallet).cancellation_prompt) stage=4;
                return;
            }
            if (trial==3 && frames==0) Capture(frame,"attempt-cancellation");
            if (Action("Create competing self-payment")) ++stage;
            return;
        case 6:
            if (trial==2) {
                if (!std::filesystem::exists(source/"read-held")) return;
                if (!overlap) {
                    const auto target=requests.replacements.Running();
                    Check(target.has_value() && !requests.recovery.BroadcastLease(),"competing operation cannot overlap the submission lease");
                    const a::FeeDraftRequest stale{target->identity.context,target->txid,target->kind,
                        target->generation+1,target->sequence,target->rate_per_kvb};
                    Check(!requests.replacements.Start(state.documents,requests.refresh,requests.recovery,state.coin_controls,stale).accepted,
                        "injected stale generation and overlapping fee work refused");
                    overlap=true;
                }
                if (!Action("Cancel fee request")) return;
                std::ofstream(source/"release-read")<<"release";
            }
            ++stage;
            return;
        case 7:
            if (Awaiting()) return;
            if (trial<3) {
                const auto& status=state.histories.at(wallet).error;
                Check(status.find(trials[trial].reason)!=status.npos,"refusal or retirement: "+status);
                Check(state.documents.Documents().size()==1,"original retained and no unverified draft admitted");
                ++trial;
                stage=3;
                return;
            }
            Check(state.documents.ActiveId()!=wallet && state.documents.Documents().size()==2,"separate native self-payment admitted");
            candidate=state.documents.ActiveId();
            {
                const auto& draft=*state.imported_views.at(candidate).replacement;
                const auto& tx=*draft.working->transaction;
                Check(draft.cancellation && draft.scope.txid.hex()==Row("eligible") && !draft.returned
                    && tx.inputs.size()==draft.original->transaction->inputs.size() && tx.outputs==draft.proof.recipients,
                    "local draft retains original and exact single self-payment");
                Check(std::ranges::equal(tx.inputs,draft.original->transaction->inputs,{},
                    &p::TransactionInput::outpoint,&p::TransactionInput::outpoint),"every original input conflicts");
                Check(tx.outputs.size()==1 && tx.fee==draft.proof.fee && draft.proof.fee.sats()>draft.original_fee.sats()
                    && !p::InspectSignatures(*draft.working).Complete() && p::TransactionEditBlock(*draft.working).empty(),
                    "exact increased fee and editable unsigned source");
                Check(Has("Wallet-owned destination:") && Has("Cancellation is never guaranteed")
                    && !facts.transaction_document.contains("finalize") && !facts.transaction_document.contains("merge"),
                    "native cancellation intent and strict review route visible");
                Check(!sh::CurrentSigningApproval(state,requests,candidate)
                    && !sh::BeginLocalDocument(state,requests,sh::LocalDocumentMode::MergeText),
                    "no inherited approval or generic signing-material route");
                std::ofstream(output/"candidate.psbt",std::ios::binary)<<draft.working->wire;
                std::ofstream(output/"original.tx",std::ios::binary)<<draft.original->wire;
                std::ofstream(output/"fee-facts.txt")<<draft.original_fee.sats()<<' '<<draft.proof.fee.sats()<<' '
                    <<tx.outputs.front().value.sats()<<'\n';
            }
            Capture(frame,"competing-self-payment");
            ++stage;
            return;
        case 8:
            if (Action("Signing review")) ++stage;
            return;
        case 9:
            Check(!Dispatch(a::Command::ApproveSigning).accepted && !Dispatch(a::Command::SignTransaction).accepted
                && !Dispatch(a::Command::BroadcastTransaction).accepted,"injected authority refused before fresh review");
            Check(Dispatch(a::Command::ReviewForSigning).accepted,"explicit new review recorded");
            Mode("stale-witness");
            Check(Dispatch(a::Command::ApproveSigning).accepted
                && !sh::CurrentSigningApproval(state,requests,candidate),"approval waits for current original observation");
            ++stage;
            return;
        case 10:
            if (Awaiting()) return;
            Check(!sh::CurrentSigningApproval(state,requests,candidate)
                && state.signing_reviews.Reason(candidate).find("witness identity")!=std::string_view::npos,
                "conflicting witness observation refused and review retired");
            Mode("stale-owner-policy");
            Check(Dispatch(a::Command::ReviewForSigning).accepted && Dispatch(a::Command::ApproveSigning).accepted,
                "separate approval request with a withdrawn owned-destination policy");
            stage=17;
            return;
        case 17:
            if (Awaiting()) return;
            Check(!sh::CurrentSigningApproval(state,requests,candidate)
                && state.signing_reviews.Reason(candidate).find("outside supported listed public wallet policy")!=std::string_view::npos,
                "withdrawn independent destination proof retires approval");
            Mode("approval-held");
            Check(Dispatch(a::Command::ReviewForSigning).accepted && Dispatch(a::Command::ApproveSigning).accepted,
                "held fee approval begins with exact current review");
            stage=18;
            return;
        case 18:
            if (!std::filesystem::exists(source/"approval-held")) return;
            if (!queued) Check(requests.replacements.Busy() && !sh::CurrentSigningApproval(state,requests,candidate),
                "fee approval reply held before native Back to inspection");
            if (!Action("Back to inspection")) return;
            Check(!state.signing_views.at(candidate).open,"native Back retires review ownership");
            std::ofstream(source/"approval-release")<<"release";
            stage=19;
            return;
        case 19:
            if (Awaiting()) return;
            Check(!sh::CurrentSigningApproval(state,requests,candidate)
                && state.signing_reviews.Reason(candidate).find("Approval was retired")!=std::string_view::npos
                && state.documents.Documents().size()==2
                && state.imported_views.at(candidate).replacement.has_value(),
                "late fee approval settles without consent or loss of the unsigned draft");
            if (Action("Signing review")) stage=20;
            return;
        case 20:
            Mode("approve");
            Check(Dispatch(a::Command::ReviewForSigning).accepted && Dispatch(a::Command::ApproveSigning).accepted,
                "fresh separate review and approval request");
            stage=11;
            return;
        case 11:
            if (Awaiting()) return;
            Check(sh::CurrentSigningApproval(state,requests,candidate),"approved only after verified live original");
            Mode("explicit-sign");
            Check(Dispatch(a::Command::SignTransaction).accepted,"separate explicit signing request");
            ++stage;
            return;
        case 12:
            if (Awaiting()) return;
            Check(p::InspectSignatures(*sh::SigningMaterial(state,candidate)).Complete(),"exact signed collection verified");
            Check(Dispatch(a::Command::FinalizeSigning).accepted,"separate local finalization");
            {
                const auto* final=sh::FinalizedMaterial(state,candidate);
                Check(final && final->final.document->transaction->txid,"exact final bytes pinned");
                std::ofstream(output/"candidate-final.tx",std::ios::binary)<<final->final.document->wire;
                std::ofstream(source/"final-txid.txt")<<final->final.document->transaction->txid->hex();
            }
            Mode("preflight");
            Check(Dispatch(a::Command::CheckBroadcast).accepted,"separate policy check requested");
            ++stage;
            return;
        case 13:
            if (Awaiting()) return;
            Check(requests.broadcaster.View().stage==a::BroadcastStage::Ready,"actual preflight accepted the exact candidate");
            Mode("broadcast-race");
            Check(Dispatch(a::Command::BroadcastTransaction).accepted,"separate explicit broadcast requested");
            ++stage;
            return;
        case 14: {
            if (Awaiting() || !std::filesystem::exists(source/"race-mined")) return;
            const auto phase=requests.broadcaster.View().stage;
            Check(phase==a::BroadcastStage::Unsent,"last live check withheld send after real confirmation");
            Check(requests.broadcaster.View().message.find("No transaction was submitted")!=std::string::npos,
                "original confirmation refusal states known unsent");
            Check(state.documents.Documents().size()==2 && sh::FinalizedMaterial(state,candidate),
                "original view and exact finalized candidate work preserved");
            requests.refresh.RequestNow(RefreshMonotonicMs());
            ++stage;
            return;
        }
        case 15:
            if (!current || view->ledger->Rows().at(Row("eligible")).confirmations<=0) return;
            Check(!sh::CurrentSigningApproval(state,requests,candidate)
                && !Dispatch(a::Command::BroadcastTransaction).accepted,"observed confirmation retires approval and retry authority");
            Check(Has("The original payment was confirmed") && !Has("Approval recorded for the competing self-payment"),
                "native review explains the race and clears the earlier approval message");
            Capture(frame,"original-confirmed-first");
            ++stage;
            host.RequestClose();
            return;
        }
    }
    void Teardown(const platform::NativeTeardown& t) override
    {
        settled=t.renderer_cleared && t.platform_cleared && t.context_cleared && t.imgui_cleared && t.input_cleared;
        Check(settled,"owned renderer and input settled");
    }
    void Report()
    {
        Check(stage==16 && frames==3 && !Awaiting() && settled,"native cancellation journey and workers settled");
        std::ofstream(output/"native-result.json")<<"{\"checks\":"<<checks<<",\"stages\":"<<stage<<
            ",\"frames\":"<<frames<<",\"settled\":true}\n";
    }
};
}

int main(int argc,char** argv)
{
    if (argc!=6) return 2;
    try {
        const auto* display=std::getenv("DISPLAY");
        const auto* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if (!display || !owned || std::string_view(display)!=owned) throw std::runtime_error("Owned display required.");
        Observer observer(argv[1],argv[3],argv[4],argv[5]);
        platform::NativeOptions options{960,640,false,1.F,argv[2]};
        options.fixed_delta_seconds=1.F/60;
        options.accessibility=&observer.state.presentation.exchange;
        platform::NativeHost* active{};
        {
            platform::NativeHost host(options,&observer,[&](const auto& theme,const auto& appearance) {
                observer.facts=platform::DrawShellFrame(*active,observer.state,observer.requests,theme,appearance);
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
