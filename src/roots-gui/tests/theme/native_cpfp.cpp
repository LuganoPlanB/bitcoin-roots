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
    bool settled{};
    struct Trial {const char* mode; const char* output; const char* reason;};
    static constexpr Trial trials[]{
        {"spent","Select output #0 (120000 sat)","spent, confirmed or unavailable"},
        {"missing","Select output #1 (78000 sat)","Ancestor membership is incomplete"},
        {"policy-limit","Select output #1 (78000 sat)","exceed the reported cluster"},
        {"locked","Select output #1 (78000 sat)","currently daemon-locked"},
        {"valid","Select output #1 (78000 sat)",""},
    };

    void Check(bool value,std::string_view label)
    {
        if (!value) throw std::runtime_error("CPFP stage "+std::to_string(stage)+
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
        const auto* history=state.documents.Find(wallet);
        const auto view=a::HistoryForDocument(*history,requests.refresh,requests.recovery.Gate().Epoch());
        const bool current=view && view->chain_current && view->ledger->InventoryAvailable() && view->ledger->LocksAvailable();
        const auto parent=Row("parent");
        switch (stage) {
        case 0:
            if (!current || !view->ledger->Rows().contains(parent)) return;
            state.documents.Find(wallet)->history.selected=parent;
            ++stage;
            return;
        case 1:
            if (Action("Fee actions")) ++stage;
            return;
        case 2:
            if (Action("New fee rate (sat/vB)",c::ax::SemanticAction::SetValue,"12.345")) ++stage;
            return;
        case 3:
            if (!current) return;
            if (!queued) {
                Mode(trials[trial].mode);
                const auto txid=*Hash256::Parse(parent);
                const a::FeeDraftRequest unchecked{view->identity->context,txid,a::FeeDraftKind::ChildPaysForParent,
                    view->generation,view->history_sequence,12345};
                Check(!sh::BeginReplacement(state,requests,unchecked).accepted,"injected request without selected outpoint refused");
                auto stale=unchecked;
                stale.parent_output=1;
                ++stale.generation;
                Check(!requests.replacements.Start(state.documents,requests.refresh,requests.recovery,
                    state.coin_controls,stale).accepted,"stale injected outpoint refused");
            }
            if (queued ? Action("Increase effective fee") : (state.histories.at(wallet).child_parent || Action("Increase effective fee"))) ++stage;
            return;
        case 4:
            if (Action(trials[trial].output)) ++stage;
            return;
        case 5:
            if (Action("Create unsigned child")) ++stage;
            return;
        case 6:
            if (Awaiting()) return;
            if (trial<4) {
                const auto& error=state.histories.at(wallet).error;
                Check(error.find(trials[trial].reason)!=error.npos,"child draft refused: "+error);
                Check(state.documents.Documents().size()==1,"original retained alone after refusal");
                if (trial==1) Capture(frame,"unknown-package");
                ++trial;
                stage=3;
                return;
            }
            Check(state.documents.ActiveId()!=wallet && state.documents.Documents().size()==2,"separate native child admitted");
            candidate=state.documents.ActiveId();
            ++stage;
            return;
        case 7: {
            const auto& draft=*state.imported_views.at(candidate).replacement;
            Check(draft.child_package && !draft.cancellation && !draft.returned,"child relation, locally constructed");
            const auto& intent=*draft.child_package;
            const auto& tx=*draft.working->transaction;
            const auto& ancestors=intent.package.ancestors;
            Check(intent.output==1 && intent.parent->transaction->txid->hex()==parent && tx.inputs.size()==1
                && tx.inputs.front().outpoint==(Outpoint{draft.scope.txid,1})
                && tx.outputs.size()==1 && tx.outputs.front().script==intent.parent->transaction->outputs.at(1).script,
                "one exact selected outpoint and existing independently verified destination");
            Check(ancestors.size()==4 && intent.package.cluster.size()==5 && intent.package.base_fee.sats()==3000,
                "diamond ancestor fee counted once; sibling child occupies cluster only");
            const auto root=std::ranges::find_if(ancestors,[&](const auto& row){return row.txid.hex()==Row("root");});
            Check(root!=ancestors.end() && root->base_fee.sats()==1000 && root->modified_fee.sats()==1250,
                "node fee adjustment excluded from base fee");
            const int64_t fee=(int64_t(intent.package.vsize+intent.estimated_vsize)*12345+999)/1000-3000;
            Check(tx.fee==draft.proof.fee && tx.fee->sats()==fee && tx.outputs.front().value.sats()==78000-fee
                && p::TransactionEditBlock(*draft.working).empty() && !p::InspectSignatures(*draft.working).Complete(),
                "exact integer ceiling fee and editable unsigned child");
            Check(!sh::CurrentSigningApproval(state,requests,candidate)
                && !Dispatch(a::Command::ApproveSigning).accepted && !Dispatch(a::Command::SignTransaction).accepted
                && !Dispatch(a::Command::BroadcastTransaction).accepted
                && !sh::BeginLocalDocument(state,requests,sh::LocalDocumentMode::MergeText),
                "draft admission grants no approval, signing, broadcast or generic merge");
            Check(Has("4 unique transactions") && Has("(estimated)") && Has("Node cluster: 5/"),
                "native per-child and package estimate, independent node limit");
            std::ofstream(output/"candidate.psbt",std::ios::binary)<<draft.working->wire;
            std::ofstream(output/"parent.tx",std::ios::binary)<<intent.parent->wire;
            std::ofstream(output/"fee-facts.txt")<<intent.package.base_fee.sats()<<' '<<intent.package.vsize<<' '
                <<fee<<' '<<intent.estimated_vsize<<' '<<tx.outputs.front().value.sats()<<'\n';
            Capture(frame,"unsigned-child-estimate");
            ++stage;
            return;
        }
        case 8:
            if (Action("Signing review")) ++stage;
            return;
        case 9:
            Check(Dispatch(a::Command::ReviewForSigning).accepted,"explicit fresh review");
            Mode("stale-fee");
            Check(Dispatch(a::Command::ApproveSigning).accepted && !sh::CurrentSigningApproval(state,requests,candidate),
                "explicit approval waits for the same live package");
            ++stage;
            return;
        case 10:
            if (Awaiting()) return;
            Check(!sh::CurrentSigningApproval(state,requests,candidate)
                && state.signing_reviews.Reason(candidate).find("fees, sizes or policy changed")!=std::string_view::npos,
                "changed ancestor fee policy retires approval despite unchanged base fees");
            Mode("approve");
            Check(Dispatch(a::Command::ReviewForSigning).accepted && Dispatch(a::Command::ApproveSigning).accepted,
                "fresh separate review and approval after actor restores original fee policy");
            ++stage;
            return;
        case 11:
            if (Awaiting()) return;
            Check(sh::CurrentSigningApproval(state,requests,candidate),"approval recorded after current package verified");
            Mode("explicit-sign");
            Check(Dispatch(a::Command::SignTransaction).accepted,"explicit signing follows approval");
            ++stage;
            return;
        case 12: {
            if (Awaiting()) return;
            Check(p::InspectSignatures(*sh::SigningMaterial(state,candidate)).Complete(),"exact signed collection verified");
            Check(Dispatch(a::Command::FinalizeSigning).accepted,"separate local finalization");
            const auto* final=sh::FinalizedMaterial(state,candidate);
            Check(final && final->final.document->transaction->txid,"exact final bytes pinned");
            std::ofstream(output/"candidate-final.tx",std::ios::binary)<<final->final.document->wire;
            std::ofstream(source/"final-txid.txt")<<final->final.document->transaction->txid->hex();
            ++stage;
            return;
        }
        case 13:
            Check(Has("(actual BIP141)"),"native rate now uses exact signed child size");
            Capture(frame,"final-child-actual");
            Mode("preflight");
            Check(Dispatch(a::Command::CheckBroadcast).accepted,"separate policy check");
            ++stage;
            return;
        case 14:
            if (Awaiting()) return;
            Check(requests.broadcaster.View().stage==a::BroadcastStage::Ready,"daemon preflight accepted exact current bytes");
            Mode("broadcast-race");
            Check(Dispatch(a::Command::BroadcastTransaction).accepted,"separate explicit broadcast requested");
            ++stage;
            return;
        case 15:
            if (Awaiting() || !std::filesystem::exists(source/"race-mined")) return;
            Check(requests.broadcaster.View().stage==a::BroadcastStage::Unsent
                && requests.broadcaster.View().message.find("No transaction was submitted")!=std::string::npos,
                "actual updated parent state caused known unsent, not ambiguous retry");
            Check(state.documents.Documents().size()==2 && sh::FinalizedMaterial(state,candidate),
                "original and exact finalized child survive the race");
            requests.refresh.RequestNow(RefreshMonotonicMs());
            ++stage;
            return;
        case 16:
            if (!current || view->ledger->Rows().at(parent).confirmations<=0) return;
            Check(!sh::CurrentSigningApproval(state,requests,candidate)
                && !Dispatch(a::Command::BroadcastTransaction).accepted,"confirmed parent retires command authority");
            Check(Has("No transaction was submitted"),"known-unsent outcome still inspectable");
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
        Check(stage==17 && frames==3 && !Awaiting() && settled,"native CPFP journey and workers settled");
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
