// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/broadcast.h"
#include "app/connection_observations.h"
#include "app/history_workspace.h"
#include "platform/shell_composition.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
using namespace std::chrono_literals;
namespace a=app;
namespace sh=ui::shell;
namespace c=ui::components;
namespace p=protocol;
namespace {
constexpr auto GENESIS="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
constexpr auto WALLET="fee-wallet";
struct Requests final : a::Requests {
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    ConnectionService connection{[this](const auto& candidate,std::stop_token stop){return ProbeCapabilities(transport,candidate,stop);}};
    RefreshService refresh{[this](const auto& value,std::stop_token stop){return FetchRefresh(transport,value,stop);}};
    a::Broadcaster broadcaster{transport,connection};
    a::Replacements replacements{transport,connection};
    a::RecoverySession recovery;
    mutable TransactionDocuments local;
    explicit Requests(const std::filesystem::path& home):recovery(home/"metadata.sqlite"){}
    ConnectionService* Connection() override {return &connection;}
    RefreshService* Refresh() override {return &refresh;}
    a::RecoverySession* Recovery() override {return &recovery;}
    a::Broadcaster* Broadcasts() override {return &broadcaster;}
    a::Replacements* ReplacementDrafts() override {return &replacements;}
    TransactionDocuments* LocalDocuments() const override {return &local;}
    a::HostObservations Observe() const override {return a::ConnectionObservations(connection,refresh);}
    a::Receipt Submit(const a::Request&) override {throw std::runtime_error("Unexpected fixture command.");}
};

std::string Read(const std::filesystem::path& path)
{
    std::ifstream input(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),{}};
}

struct Observer final : platform::NativeObserver {
    using Clock=std::chrono::steady_clock;
    const Clock::time_point deadline=Clock::now()+65s;
    std::filesystem::path output,source;
    Requests requests;
    sh::State state;
    sh::Facts facts;
    uint64_t wallet{},replacement{};
    int checks{},stage{},frames{};
    size_t failure{};
    bool settled{},checked_overlap{};
    uint64_t queued_action{};
    struct Trial {const char* mode;const char* row;const char* reason;};
    static constexpr Trial trials[]{
        {"nonreplaceable","plain","does not explicitly signal"},
        {"malformed","eligible","not the expected unsigned"},
        {"recipient","eligible","A recipient amount, script or order changed"},
        {"change","eligible","substituted or duplicated the original change script"},
        {"fee","eligible","exact reported fee does not reconcile"},
        {"insufficient","poor","Funds, the requested rate or current replacement policy may be insufficient"},
        {"drop","eligible","wallet reply was lost"},
        {"cancel","eligible","fee request was retired"},
    };

    void Check(bool value,std::string_view message)
    {
        if (!value) throw std::runtime_error("Replacement stage "+std::to_string(stage)+", trial "+
            std::to_string(failure)+": "+std::string(message));
        ++checks;
    }
    Observer(std::filesystem::path out,std::filesystem::path home,const char* port,const char* cookie):
        output(std::move(out)),source(std::move(home)),requests(source)
    {
        Check(requests.recovery.Start(),"local recovery state available");
        ServerCandidate candidate;
        candidate.connection.port=uint16_t(std::stoul(port));
        candidate.connection.credential.reference=cookie;
        candidate.expected_genesis=GENESIS;
        candidate.wallet=WALLET;
        Check(requests.connection.SetDraft(candidate) && requests.connection.Test(),"production capability probe started");
        const auto until=Clock::now()+12s;
        while (requests.connection.Busy() && Clock::now()<until) {
            requests.connection.Poll();
            std::this_thread::sleep_for(2ms);
        }
        Check(requests.connection.Apply(),"exact fixture capability profile applied");
        const auto binding=a::VerifiedWalletBinding(*requests.connection.Active());
        Check(binding && binding->DaemonWallet()==WALLET,"verified Roots regtest wallet");
        wallet=state.documents.OpenWallet(WALLET,*binding,true);
        state.documents.Find(wallet)->history.enabled=true;
    }
    bool CaptureFramebuffer() const override {return true;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override
    {
        if (Clock::now()<deadline) return;
        std::ofstream(output/"last.semantics.txt")<<state.presentation.Snapshot();
        Check(false,"native deadline");
    }
    bool Has(std::string_view phrase) const {return state.presentation.Snapshot().find(phrase)!=std::string::npos;}
    bool Action(std::string_view label,c::ax::SemanticAction action,std::string_view text={})
    {
        if (queued_action) {
            const auto receipts=state.presentation.exchange.Receipts();
            const auto found=std::ranges::find(receipts,queued_action,&c::SemanticReceipt::sequence);
            if (found==receipts.end()) return false;
            queued_action=0;
            if (found->result==c::ActionResult::Stale) return false;
            Check(found->result==c::ActionResult::Applied,"native semantic action applied");
            return true;
        }
        const auto packet=state.presentation.exchange.Published();
        if (!packet || !packet->valid) return false;
        const auto node=std::ranges::find_if(packet->nodes,[&](const auto& value) {
            return value.name==label && !value.state.disabled && c::ax::Supports(value.actions,action);
        });
        if (node==packet->nodes.end()) return false;
        const auto [sequence,result]=state.presentation.exchange.QueueNext(packet->generation,node->id,action,std::string(text));
        Check(result==c::ActionResult::Queued,"native semantic action queued");
        queued_action=sequence;
        return false;
    }
    bool Press(std::string_view name) {return Action(name,c::ax::SemanticAction::Press);}
    void Mode(std::string_view name) {std::ofstream(source/"mode")<<name;}
    std::string Row(std::string_view name) const {return Read(source/(std::string(name)+".txt"));}
    void Select(std::string_view name) {state.documents.Find(wallet)->history.selected=Row(name);}
    void Capture(const platform::NativeFrame& frame,const char* title)
    {
        Check(frame.width==960 && frame.height==640 && frame.effective_scale==1.F && frame.font_texture && !frame.gl_error,
            "actual X11 rendering");
        Check(state.presentation.Tree().Validate() && !state.presentation.Tree().overflow,"bounded native semantic tree");
        std::ofstream image(output/(std::string(title)+".ppm"),std::ios::binary);
        image<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for (int y=frame.framebuffer_height-1;y>=0;--y) for (int x=0;x<frame.framebuffer_width;++x)
            image.write(reinterpret_cast<const char*>(frame.rgba.data()+(size_t(y)*frame.framebuffer_width+x)*4),3);
        Check(bool(image),"captured native framebuffer");
        std::ofstream(output/(std::string(title)+".semantics.txt"))<<state.presentation.Snapshot();
        ++frames;
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override
    {
        auto* doc=state.documents.Find(wallet);
        const auto view=a::HistoryForDocument(*doc,requests.refresh,requests.recovery.Gate().Epoch());
        const bool current=view && view->chain_current && view->ledger && view->ledger->InventoryAvailable() && view->ledger->LocksAvailable();
        switch (stage) {
        case 0:
            if (!current || !view->ledger->Rows().contains(Row("eligible"))) return;
            Select("eligible");
            ++stage;
            return;
        case 1:
            if (Press("Fee actions")) ++stage;
            return;
        case 2: {
            if ((!current && !queued_action) || !Action("New fee rate (sat/vB)",c::ax::SemanticAction::SetValue,"12.345")) return;
            Check(state.histories.at(wallet).replacement_rate=="12.345","fee rate applied as entered");
            stage=15;
            return;
        }
        case 15: {
            if (!current || !facts.history.contains("fee-rbf") || facts.history.at("fee-rbf").disabled) return;
            Capture(frame,"increase-fee");
            const auto txid=*Hash256::Parse(Row("eligible"));
            const a::FeeDraftRequest stale{view->identity->context,txid,a::FeeDraftKind::ReplaceByFee,view->generation+1,
                view->history_sequence,12345};
            Check(!sh::BeginReplacement(state,requests,stale).accepted,"stale injected fee request refused");
            Select("replaced");
            stage=3;
            return;
        }
        case 3:
            if (!Has("already been replaced")) return;
            Check(facts.history.at("fee-rbf").disabled,"actual already-replaced original unavailable");
            ++stage;
            return;
        case 4:
            if (failure==std::size(trials)) {
                Mode("eligible");
                Select("eligible");
                stage=8;
                return;
            }
            Mode(trials[failure].mode);
            Select(trials[failure].row);
            ++stage;
            return;
        case 5:
            if ((!current && !queued_action) || !Press("Increase Fee")) return;
            ++stage;
            return;
        case 6:
            if (std::string_view(trials[failure].mode)=="cancel") {
                if (!std::filesystem::exists(source/"wallet-prepared")) return;
                if (!checked_overlap) {
                    const auto scope=requests.replacements.Running();
                    Check(scope.has_value(),"an exact fee request is still awaiting a reply");
                    const a::FeeDraftRequest duplicate{scope->identity.context,scope->txid,a::FeeDraftKind::ReplaceByFee,
                        scope->generation,scope->sequence,scope->rate_per_kvb};
                    Check(!requests.replacements.Start(state.documents,requests.refresh,requests.recovery,state.coin_controls,duplicate).accepted,
                        "second fee request refused while the first is pending");
                    Check(!requests.recovery.BroadcastLease(),"the same mutation lease also excludes submission");
                    checked_overlap=true;
                }
                if (!Press("Cancel fee request")) return;
                std::ofstream(source/"release-reply")<<"release";
            }
            ++stage;
            return;
        case 7: {
            if (requests.replacements.Busy()) return;
            const auto& status=state.histories.at(wallet).error;
            Check(status.find(trials[failure].reason)!=status.npos,"unexpected fee response: "+status);
            Check(Has(trials[failure].reason),"rejection reason rendered in native semantics");
            Check(state.documents.Documents().size()==1,"rejected or retired result did not admit any draft");
            ++failure;
            stage=4;
            return;
        }
        case 8:
            if (!queued_action && (!current || !facts.history.contains("fee-rbf") || facts.history.at("fee-rbf").disabled)) return;
            if (Press("Increase Fee")) ++stage;
            return;
        case 9: {
            if (state.documents.ActiveId()==wallet) return;
            replacement=state.documents.ActiveId();
            const auto& imported=state.imported_views.at(replacement);
            Check(imported.replacement.has_value(),"separate transaction has live replacement provenance");
            const auto& proof=*imported.replacement;
            Check(proof.scope.txid.hex()==Row("eligible") && proof.original->transaction->txid==proof.scope.txid
                && proof.returned->psbt_version==2 && proof.working->unsigned_wire==proof.returned->unsigned_wire,
                "exact original, returned source and verified working PSBT retained");
            Check(proof.proof.fee.sats()>proof.original_fee.sats() && proof.proposed_change.sats()<proof.original_change.sats()
                && proof.working->transaction->fee==proof.proof.fee,"exact increased fee and decreased change reconciled");
            Check(p::TransactionEditBlock(*proof.working).empty() && !p::InspectSignatures(*proof.working).Complete()
                && !sh::CurrentSigningApproval(state,requests,replacement),"proposal is editable, unsigned and unapproved");
            Check(doc->history.selected==Row("eligible") && doc->id!=replacement,"original wallet selection retained");
            Check(Has("Original txid:") && Has("Original wtxid:") && Has("Save unsigned PSBT")
                && !Has("Local review unavailable") && !facts.transaction_document.contains("finalize")
                && !facts.transaction_document.contains("merge"),"native draft presents only its approved signing route");
            Check(!sh::BeginLocalDocument(state,requests,sh::LocalDocumentMode::MergeText),
                "generic merge cannot bypass replacement review");
            std::ofstream(output/"working.psbt",std::ios::binary)<<proof.working->wire;
            std::ofstream(output/"returned.psbt.txt")<<proof.returned->source;
            std::ofstream(output/"fee-facts.txt")<<proof.original_fee.sats()<<' '<<proof.proof.fee.sats()<<' '
                <<proof.original_change.sats()<<' '<<proof.proposed_change.sats()<<'\n';
            Capture(frame,"replacement-draft");
            ++stage;
            return;
        }
        case 10:
            if (Press("Signing review")) ++stage;
            return;
        case 11:
            if (!Has("Review and approval are separate actions.")) return;
            Check(sh::SigningCommandAvailability(state,requests,{a::Command::ReviewForSigning,replacement}).available,
                "fresh independent signing review is available");
            Check(!sh::SigningCommandAvailability(state,requests,{a::Command::ApproveSigning,replacement}).available
                && !sh::SigningCommandAvailability(state,requests,{a::Command::SignTransaction,replacement}).available
                && !sh::SigningCommandAvailability(state,requests,{a::Command::BroadcastTransaction,replacement}).available,
                "approval, signing and broadcast refused without new review");
            Check(!sh::DispatchSigningCommand(state,requests,{a::Command::BroadcastTransaction,replacement}).accepted,
                "injected submit cannot use original relationship as authority");
            Check(state.documents.Activate(wallet),"return to original wallet");
            Select("race");
            Mode("race");
            ++stage;
            return;
        case 12:
            if ((current || queued_action) && Press("Increase Fee")) ++stage;
            return;
        case 13:
            if (requests.replacements.Busy() || !std::filesystem::exists(source/"race-mined")
                || (!Has("chain tip changed") && !Has("fee request was retired"))) return;
            Check(state.documents.Documents().size()==2 && state.documents.ActiveId()==wallet
                && !sh::CurrentSigningApproval(state,requests,replacement),"concurrent confirmation admitted no extra payment");
            ++stage;
            host.RequestClose();
            return;
        }
    }
    void Teardown(const platform::NativeTeardown& t) override
    {
        settled=t.renderer_cleared && t.context_cleared && t.imgui_cleared && t.platform_cleared && t.input_cleared;
        Check(settled,"native resources settled");
    }
    void Report()
    {
        Check(stage==14 && frames==2 && !requests.replacements.Busy() && !requests.broadcaster.Busy() && settled,
            "native journey and mutation workers settled");
        std::ofstream(output/"native-result.json")<<"{\"stages\":"<<stage<<",\"checks\":"<<checks
            <<",\"frames\":"<<frames<<",\"rejectedTrials\":"<<failure<<",\"settled\":true}\n";
    }
};
}

int main(int argc,char** argv)
{
    if (argc!=6) return 2;
    try {
        const char* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY"),*display=std::getenv("DISPLAY");
        if (!owned || !display || std::string_view(display)!=owned) throw std::runtime_error("Owned X11 display required.");
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
