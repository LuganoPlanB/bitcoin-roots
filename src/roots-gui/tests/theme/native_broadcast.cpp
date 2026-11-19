// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/localization.h"
#include "adapters/rpc/json.h"
#include "app/broadcast.h"
#include "app/connection_observations.h"
#include "app/history_workspace.h"
#include "platform/shell_composition.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <algorithm>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
using namespace std::chrono_literals;
namespace sh=ui::shell;
namespace a=app;
namespace c=ui::components;
namespace p=protocol;
namespace {
constexpr auto GENESIS="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
constexpr auto WALLET="gui-broadcast-watch";
std::string Read(const std::filesystem::path& path)
{
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),{}};
}
struct Requests final : a::Requests {
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    ConnectionService connection{[this](const auto& candidate,std::stop_token stop){return ProbeCapabilities(transport,candidate,stop);}};
    RefreshService refresh{[this](const auto& value,std::stop_token stop){return FetchRefresh(transport,value,stop);}};
    a::Broadcaster broadcaster{transport,connection};
    a::RecoverySession recovery;
    mutable TransactionDocuments local;
    explicit Requests(const std::filesystem::path& file):recovery(file){}
    ConnectionService* Connection() override {return &connection;}
    RefreshService* Refresh() override {return &refresh;}
    a::RecoverySession* Recovery() override {return &recovery;}
    a::Broadcaster* Broadcasts() override {return &broadcaster;}
    TransactionDocuments* LocalDocuments() const override {return &local;}
    a::HostObservations Observe() const override {return a::ConnectionObservations(connection,refresh);}
    a::Receipt Submit(const a::Request&) override {throw std::runtime_error("Unexpected native host command.");}
};
struct Fixture {
    uint64_t document{},aliases{};
    std::shared_ptr<const p::TransactionDocument> original,collected,expected;
};
struct Observer final : platform::NativeObserver {
    std::filesystem::path output,fixtures,store;
    Requests requests;
    sh::State state;
    sh::Facts facts;
    using Clock=std::chrono::steady_clock;
    Clock::time_point started=Clock::now();
    Display* display{};
    void* xtst{};
    int(*motion)(Display*,int,int,int,unsigned long){};
    int(*button)(Display*,unsigned,Bool,unsigned long){};
    int stage{},checks{},frames{};
    int reader_step{-1};
    uint64_t wallet{};
    Fixture rejected,valid,lost,retry;
    std::optional<a::CoinDraft> reader_origin;
    std::string role;
    Clock::time_point offline_started;
    std::string mined_tip;
    bool settled{};
    int width{960}, height{640};
    float scale{1.F};
    bool dark{}, drawn_dark{};
    std::string renderer;

    void Check(bool valid,const char* reason)
    {
        if (!valid) throw std::runtime_error("Broadcast stage "+std::to_string(stage)+": "+reason);
        ++checks;
    }
    Observer(std::filesystem::path out,std::filesystem::path source,const char* port,const char* cookie,std::string mode):
        output(std::move(out)),fixtures(std::move(source)),store(fixtures/"metadata.sqlite"),requests(store),role(std::move(mode))
    {
        Check(requests.recovery.Start(),"owned metadata store");
        ServerCandidate candidate;
        candidate.connection.port=uint16_t(std::stoul(port));
        candidate.connection.credential.reference=cookie;
        candidate.expected_genesis=GENESIS;
        candidate.wallet=WALLET;
        Check(requests.connection.SetDraft(candidate) && requests.connection.Test(),"real connection capability test");
        const auto until=Clock::now()+12s;
        while (requests.connection.Busy() && Clock::now()<until) {
            requests.connection.Poll();
            std::this_thread::sleep_for(2ms);
        }
        Check(requests.connection.Apply(),"exact owned node capability application");
        const auto& caps=*requests.connection.Active();
        const auto binding=a::VerifiedWalletBinding(caps);
        Check(binding && binding->DaemonWallet()==WALLET && caps.genesis==GENESIS,"fixture wallet and regtest genesis");
        wallet=state.documents.OpenWallet(WALLET,*binding,true);
        auto* view=state.documents.Find(wallet);
        view->history.enabled=true;
        view->coin_draft=a::CoinDraft{*binding,{}};
        const auto policy=p::PreviewPublicWallet(p::PublicImportKind::Descriptor,Read(fixtures/"descriptor.txt"),"regtest");
        Check(policy.preview && policy.preview->descriptors.size()==1,"independent public multisig descriptor");
        const auto& descriptor=policy.preview->descriptors.front();
        const auto admit=[&](const char* name) {
            const auto folder=fixtures/name;
            Fixture f;
            f.original=p::ParseTransactionDocument(Read(folder/"original.psbt"),p::DisplayNetwork::Regtest).document;
            const auto signed_psbt=p::ParseTransactionDocument(Read(folder/"signed.psbt"),p::DisplayNetwork::Regtest).document;
            f.expected=p::ParseTransactionDocument(Read(folder/"expected.tx"),p::DisplayNetwork::Regtest).document;
            Check(f.original && signed_psbt && f.expected,"accepted precomputed public fixture bytes");
            const auto& tx=*f.original->transaction;
            const auto& input=tx.inputs.front();
            Check(tx.inputs.size()==1 && tx.fee==Money::Satoshis(1000) && input.previous_value
                && input.previous_script==descriptor.script,"exact previous-output amount and script");
            auto* origin=state.documents.Find(wallet);
            const auto revision=origin->draft_revision;
            origin->coin_draft->inputs.push_back({input.outpoint,input.previous_value,input.previous_script,{},{},
                p::OfflineCoinProof{Read(folder/"previous.hex"),descriptor.descriptor,Read(fixtures/"redeem.hex"),{},1700000000000}});
            f.collected=p::MergeOfflineSignatures(*f.original,*signed_psbt).document;
            Check(f.collected && p::InspectSignatures(*f.collected).Complete(),"strict fixture signature collection");
            f.document=state.documents.OpenImportedDocument({f.original,p::DisplayNetwork::Regtest});
            f.aliases=state.documents.OpenImportedDocument({f.expected,p::DisplayNetwork::Regtest});
            auto& imported=state.imported_views[f.document];
            imported.wallet=*binding;
            imported.review_evidence=p::OfflineReviewEvidence{p::DisplayNetwork::Regtest,
                {{input.outpoint,*input.previous_value,descriptor.descriptor}},tx.outputs,*tx.fee,{}};
            imported.offline_review_origin=std::pair{wallet,revision};
            auto& signing=state.signing_views[f.document];
            if (role!="reader") {
                signing.external_source=f.original;
                signing.external_signed=f.collected;
            }
            return f;
        };
        if (role!="reader") rejected=admit("locktime");
        valid=admit("accepted");
        if (role!="reader") {
            lost=admit("lost-reply");
        } else {
            auto* origin=state.documents.Find(wallet);
            reader_origin=std::exchange(origin->coin_draft,std::nullopt);
        }
        if (role=="interrupt" || role=="restart") retry=admit("retry");
        const auto initial = role=="reader" ? wallet : role=="interrupt" ? retry.document
            : role=="monitor" ? valid.document : rejected.document;
        Check(state.documents.Activate(initial),
            "initial fixture document");
        display=XOpenDisplay(nullptr);
        xtst=dlopen("libXtst.so.6",RTLD_NOW);
        Check(display && xtst,"owned X11 display");
        motion=reinterpret_cast<decltype(motion)>(dlsym(xtst,"XTestFakeMotionEvent"));
        button=reinterpret_cast<decltype(button)>(dlsym(xtst,"XTestFakeButtonEvent"));
        Check(motion && button,"owned physical scroll");
    }
    ~Observer() override {if (display) XCloseDisplay(display); if (xtst) dlclose(xtst);}
    bool CaptureFramebuffer() const override
    {
        return role!="reader" || stage==2 || stage==5 || stage==8 || stage==9 || stage==12;
    }
    bool Has(std::string_view phrase) const {return state.presentation.Snapshot().find(phrase)!=std::string::npos;}
    bool Press(std::string_view label)
    {
        const auto packet=state.presentation.exchange.Published();
        if (!packet || !packet->valid) return false;
        const auto found=std::ranges::find_if(packet->nodes,[&](const auto& node){
            return node.name==label && !node.state.disabled && c::ax::Supports(node.actions,c::ax::SemanticAction::Press);
        });
        if (found==packet->nodes.end()) return false;
        Check(state.presentation.exchange.QueueNext(packet->generation,found->id,c::ax::SemanticAction::Press).second
            ==c::ActionResult::Queued,"native semantic operation admitted");
        return true;
    }
    void Scroll(platform::NativeHost& host)
    {
        int x{},y{};
        glfwGetWindowPos(host.Window(),&x,&y);
        Check(motion(display,-1,x+750,y+538,0),"pointer in native workbench");
        for (int i=0;i<18;++i) Check(button(display,5,True,0) && button(display,5,False,0),"native downward scroll");
        XSync(display,False);
    }
    void Capture(const platform::NativeFrame& frame,const char* name)
    {
        Check(frame.width==width && frame.height==height && frame.effective_scale==scale && drawn_dark==dark && frame.decorated
            && frame.font_texture && !frame.gl_error,"actual X11 renderer");
        const auto packet=state.presentation.exchange.Published();
        Check(packet && packet->valid && !packet->overflow && state.presentation.Tree().Validate() && !state.presentation.Tree().overflow,
            "fresh bounded native semantics");
        std::ofstream image(output/(std::string(name)+".ppm"),std::ios::binary);
        image<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for (int y=frame.framebuffer_height-1;y>=0;--y) for (int x=0;x<frame.framebuffer_width;++x)
            image.write(reinterpret_cast<const char*>(frame.rgba.data()+(size_t(y)*frame.framebuffer_width+x)*4),3);
        Check(bool(image),"captured framebuffer");
        std::ofstream(output/(std::string(name)+".semantics.txt"))<<state.presentation.Snapshot();
        std::ofstream metadata(output/(std::string(name)+".native.json"));
        metadata<<std::boolalpha<<"{\"width\":"<<frame.width<<",\"height\":"<<frame.height
            <<",\"dpi\":"<<frame.dpi_x<<",\"scale\":"<<frame.effective_scale<<",\"dark\":"<<drawn_dark
            <<",\"renderer\":"<<rpc::Quote(renderer)<<",\"semanticGeneration\":"<<packet->generation<<"}\n";
        Check(bool(metadata),"captured native facts");
        ++frames;
    }
    bool Dispatch(a::Command cmd,uint64_t id)
    {
        return sh::DispatchSigningCommand(state,requests,{cmd,id}).accepted;
    }
    bool Recorded(const Fixture& fixture)
    {
        const auto result=requests.recovery.HasBroadcastAttempt({*Hash256::Parse(GENESIS)},*fixture.expected->transaction->txid);
        Check(bool(result),"journal read");
        return *result.value;
    }
    bool Begin(const Fixture& fixture,int phase,bool previous=false)
    {
        switch (phase) {
        case 0: {
            const auto refresh=requests.refresh.View();
            if (!refresh.chain_current || !refresh.ledger || !refresh.ledger->InventoryAvailable()
                || !refresh.ledger->LocksAvailable()) return false;
            return Press("Signing review");
        }
        case 1: return Press("Record signing review");
        case 2: return Press("Approve for signing");
        case 3:
            if (!Has("Approved for this exact transaction.")) return false;
            Check(!sh::FinalizedMaterial(state,fixture.document) && !Dispatch(a::Command::BroadcastTransaction,fixture.document),
                "approval alone cannot finalize or submit");
            return Press("Finalize transaction");
        case 4: {
            const auto* artifact=sh::FinalizedMaterial(state,fixture.document);
            if (!artifact) return false;
            const auto* approval=sh::CurrentSigningApproval(state,requests,fixture.document);
            Check(approval && artifact->approval==*approval && artifact->collected==fixture.collected
                && artifact->final.document->wire==fixture.expected->wire && Recorded(fixture)==previous,
                "exact reviewed bytes and proofs before the deliberate policy check");
            a::Request injected{a::Command::BroadcastTransaction,fixture.document};
            injected.value="injected wire bytes";
            Check(!sh::DispatchSigningCommand(state,requests,injected).accepted
                && !Dispatch(a::Command::BroadcastTransaction,fixture.document),"injected and unchecked submit refused");
            return Press("Check node policy");
        }
        }
        return false;
    }
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override
    {
        if (Clock::now()-started<(role=="reader" ? 195s : role=="monitor" ? 92s : 35s)) return;
        std::ofstream(output/"deadline.txt")<<stage<<'\n'<<state.presentation.Snapshot();
        throw std::runtime_error("Native broadcast deadline at stage "+std::to_string(stage));
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override
    {
        if (frame.rendered_frames<5) return;
        if (role=="interrupt") {Interrupt(host,frame);return;}
        if (role=="restart") {Restart(host,frame);return;}
        if (role=="monitor") {Monitor(host,frame);return;}
        if (role=="reader") {Reader(host,frame);return;}
        using Stage=a::BroadcastStage;
        const auto current=requests.broadcaster.View().stage;
        if (stage<=4) {
            if (!Begin(rejected,stage)) return;
        } else if (stage>=7 && stage<=11) {
            if (!Begin(valid,stage-7)) return;
        } else if (stage>=19 && stage<=23) {
            if (!Begin(lost,stage-19)) return;
        } else switch(stage) {
        case 5:
            if (current!=Stage::Rejected) return;
            Check(Has("Node policy rejected this transaction: non-final.") && !Dispatch(a::Command::BroadcastTransaction,rejected.document)
                && !Recorded(rejected),"future absolute locktime yields a policy rejection and no submission");
            Scroll(host);
            break;
        case 6:
            if (!facts.transaction_document.at("broadcast-submit").visible) return;
            Capture(frame,"broadcast-rejected");
            Check(state.documents.Activate(valid.document),"next exact draft");
            break;
        case 12: {
            if (current!=Stage::Ready) return;
            const auto* approved=sh::CurrentSigningApproval(state,requests,valid.document);
            const auto* artifact=sh::FinalizedMaterial(state,valid.document);
            Check(approved && artifact,"exact current artifact for fee and thread guard tests");
            auto altered=*artifact;
            altered.final.fee=Money::Satoshis(100000);
            Check(!requests.broadcaster.Can(a::BroadcastAction::Check,approved,&altered,&requests.recovery).available,
                "unbounded fee refused locally");
            altered.final.fee=Money::Satoshis(999);
            Check(!requests.broadcaster.Start(a::BroadcastAction::Check,state.signing_reviews,approved->reviewed,altered,requests.recovery).accepted,
                "injected final facts refused by service");
            bool foreign=false;
            std::thread outsider([&]{foreign=requests.broadcaster.Start(a::BroadcastAction::Submit,
                state.signing_reviews,approved->reviewed,*artifact,requests.recovery).accepted;});
            outsider.join();
            Check(!foreign && !Recorded(valid),"non-owner concurrent command cannot submit");
            const auto before=approved->generation;
            sh::RevokeSigningReview(state,valid.document,"Test retiring exact approval.");
            Check(Dispatch(a::Command::ReviewForSigning,valid.document) && Dispatch(a::Command::ApproveSigning,valid.document)
                && sh::CurrentSigningApproval(state,requests,valid.document)->generation!=before
                && !Dispatch(a::Command::BroadcastTransaction,valid.document),"ready result cannot survive even identical re-approval");
            break;
        }
        case 13: if (!Press("Finalize transaction")) return; break;
        case 14: if (!Press("Check node policy")) return; break;
        case 15: {
            if (current!=Stage::Ready) return;
            Check(Dispatch(a::Command::BroadcastTransaction,valid.document)
                && requests.broadcaster.View().stage==Stage::Rechecking,"explicit submit starts with a fresh check");
            Check(!Dispatch(a::Command::BroadcastTransaction,valid.document)
                && !Dispatch(a::Command::FinalizeSigning,valid.document)
                && !sh::DispatchTransactionEdit(state,requests,{a::Command::BeginTransactionEdit,valid.document}).accepted
                && !sh::BeginLocalDocument(state,requests,sh::LocalDocumentMode::Merge),"double-click, edit, collect and finalization are refused");
            Check(state.documents.Activate(valid.aliases) && sh::BroadcastFreezes(state,valid.aliases)
                && !Dispatch(a::Command::BroadcastTransaction,valid.aliases)
                && !sh::DispatchTransactionEdit(state,requests,{a::Command::BeginTransactionEdit,valid.aliases}).accepted,
                "raw alias cannot bypass the pending operation");
            Check(state.documents.Activate(valid.document) && Dispatch(a::Command::CancelBroadcast,valid.document),
                "cancel the local wait before any mutation launch");
            break;
        }
        case 16:
            if (current!=Stage::Cancelled) return;
            Check(!Recorded(valid),"cancelled recheck never records or submits");
            if (!Press("Check node policy")) return;
            break;
        case 17:
            if (current!=Stage::Ready) return;
            Scroll(host);
            break;
        case 18:
            if (current==Stage::Acknowledged) {
                Check(Recorded(valid) && Has("Confirmation and payment status are unknown.")
                    && !Dispatch(a::Command::BroadcastTransaction,valid.document),"one acknowledged submission needs no repeat");
                Check(state.documents.Activate(lost.document),"next independently approved transaction");
                break;
            }
            if (frames==2 || current!=Stage::Ready || !facts.transaction_document.at("broadcast-submit").visible) return;
            if (!Press("Broadcast")) return;
            Capture(frame,"broadcast-ready");
            return; // remain at this stage until the exact acknowledgment
        case 24: if (current!=Stage::Ready || !Press("Broadcast")) return; break;
        case 25:
            if (current!=Stage::Uncertain) return;
            Check(Recorded(lost) && Has("The submission outcome is uncertain.")
                && !Dispatch(a::Command::BroadcastTransaction,lost.document),"lost response is uncertain and refuses duplicate submission");
            {
                a::RecoverySession another(store);
                Check(another.Start(),"reopen the separate durable attempt store");
                const auto prior=another.HasBroadcastAttempt({*Hash256::Parse(GENESIS)},*lost.expected->transaction->txid);
                Check(prior && *prior.value,"the exact exclusion identity survived reopening");
            }
            host.RequestClose();
            break;
        default:return;
        }
        ++stage;
    }
    storage::BroadcastRecord Attempt(const Fixture& fixture)
    {
        const auto& tx=*fixture.expected->transaction;
        const auto found=requests.recovery.LoadBroadcastAttempt({*Hash256::Parse(GENESIS)},*tx.txid);
        Check(found && *found.value && (**found.value).identity.wtxid==tx.wtxid
            && (**found.value).wire_hex==p::TransactionHex(fixture.expected->wire),"exact durable attempt record");
        return **found.value;
    }
    void Interrupt(platform::NativeHost& host,const platform::NativeFrame& frame)
    {
        using Stage=a::BroadcastStage;
        const auto phase=requests.broadcaster.View().stage;
        if (stage<=4) {
            if (!Begin(retry,stage)) return;
        } else if (stage>=8 && stage<=12) {
            if (!Begin(lost,stage-8)) return;
        } else switch(stage) {
        case 5: if (phase!=Stage::Ready || !Press("Broadcast")) return; break;
        case 6:
            if (phase!=Stage::Uncertain) return;
            Check(Has("The submission outcome is uncertain.") && Attempt(retry).outcome==storage::BroadcastOutcome::Uncertain,
                "disconnected send reports uncertainty with no automatic retry");
            Scroll(host);
            break;
        case 7:
            if (!facts.transaction_document.at("check-broadcast-status").visible) return;
            Capture(frame,"submission-uncertain");
            Check(state.documents.Activate(lost.document),"activate separate crash fixture");
            break;
        case 13: if (phase!=Stage::Ready || !Press("Broadcast")) return; break;
        case 14:
            if (!std::filesystem::exists(fixtures/"accepted-before-exit")) return;
            Check(phase==Stage::Sending && Attempt(lost).outcome==storage::BroadcastOutcome::Pending,
                "interruption occurs after upstream acceptance and before a reply");
            std::ofstream(output/"interruption.json")<<"{\"exit\":71,\"checks\":"<<checks
                <<",\"frames\":"<<frames<<",\"recordedPending\":true,\"rendererGracefulTeardown\":false}\n";
            std::_Exit(71);
        default: return;
        }
        ++stage;
    }
    void Mode(std::string_view value)
    {
        std::ofstream(fixtures/"status-mode")<<value;
    }
    void Restart(platform::NativeHost& host,const platform::NativeFrame& frame)
    {
        using Stage=a::BroadcastStage;
        const auto phase=requests.broadcaster.View().stage;
        const auto raw=state.documents.ActiveId();
        if (stage>=9 && stage<=13) {
            if (!Begin(retry,stage-9,true)) return;
        } else switch(stage) {
        case 0:
            Check(Attempt(lost).outcome==storage::BroadcastOutcome::Pending
                && Attempt(retry).outcome==storage::BroadcastOutcome::Uncertain && phase==Stage::Idle,
                "fresh process restores records without actions");
            if (!sh::BeginLocalDocument(state,requests,sh::LocalDocumentMode::BroadcastAttempts)) return;
            break;
        case 1:
            if (!Has("Outcome unrecorded; submission may have reached the node") || !Press("Open recorded transaction")) return;
            break;
        case 2:
            if (state.local_document.dialog.open || state.local_document.close_pending) return;
            Check(state.documents.Active()->imported->parsed->wire==lost.expected->wire
                && !sh::CurrentSigningApproval(state,requests,raw) && !Dispatch(a::Command::RebroadcastTransaction,raw),
                "recovered wire has no submission authority");
            Mode("mismatch");
            if (!Press("Check Status")) return;
            break;
        case 3: {
            if (phase!=Stage::StatusUnknown || !Has("The sources did not agree on the exact transaction.")) return;
            Mode("");
            Check(Dispatch(a::Command::CheckBroadcastStatus,raw),"new read-only status request");
            const auto previous=requests.connection.Draft();
            auto different=previous;
            different.connection.port=previous.connection.port==65535 ? 65534 : previous.connection.port+1;
            Check(requests.connection.SetOffline(true) && requests.connection.SetDraft(different)
                && requests.connection.SetDraft(previous) && requests.connection.SetOffline(false),
                "retire via disconnect and a new profile revision");
            break;
        }
        case 4:
            if (phase!=Stage::StatusUnknown || !Has("The read-only check was retired.")) return;
            if (!Press("Check Status")) return;
            break;
        case 5:
            if (phase!=Stage::StatusObserved || !Has("The exact transaction was observed in this node's mempool.")) return;
            Check(Has("Confirmation is Unknown.") && !Dispatch(a::Command::RebroadcastTransaction,raw),
                "observation remains read-only and carries no approval");
            break;
        case 6:
            Capture(frame,"submission-recovered");
            Check(state.documents.Activate(retry.aliases),"open exact recorded bytes for absent lookup");
            Mode("unavailable");
            break;
        case 7: if (!Press("Check Status")) return; break;
        case 8:
            if (phase!=Stage::StatusUnknown || !Has("A missing or unavailable lookup does not prove that submission failed.")) return;
            Check(Has("Mempool: Lookup did not find a match") && Has("Chain: Unavailable")
                && Attempt(retry).serial==1 && !Dispatch(a::Command::RebroadcastTransaction,raw),
                "absence and unavailable chain data preserve uncertainty and raw permission denial");
            Mode("");
            Check(state.documents.Activate(retry.document),"return to independent public draft for a new live review");
            break;
        case 14:
            if (phase!=Stage::Ready) return;
            Check(!Dispatch(a::Command::BroadcastTransaction,retry.document),"prior attempt requires separate retry command");
            if (!Press("Rebroadcast same bytes")) return;
            break;
        case 15:
            if (phase!=Stage::Uncertain || !Has("The node reports the transaction is already known.")) return;
            Check(Attempt(retry).serial==2 && Attempt(retry).outcome==storage::BroadcastOutcome::Uncertain,
                "already-known response remains uncertain on same-byte retry");
            if (!Press("Check Status")) return;
            break;
        case 16:
            if (phase!=Stage::StatusObserved || !Has("The exact transaction was observed in this node's mempool.")) return;
            Check(Attempt(lost).serial==1 && Attempt(retry).serial==2 && Has("Confirmation is Unknown."),
                "only the originally approved bytes were retried; no inferred confirmation");
            std::ofstream(fixtures/"mine-after-observation")<<"ready\n";
            break;
        case 17:
            if (!std::filesystem::exists(fixtures/"mined-block")) return;
            if (!Press("Check Status")) return;
            break;
        case 18:
            if (phase!=Stage::StatusObserved || !Has("The verified node reported the exact transaction in its active chain.")) return;
            Check(Has("Exact bytes anchored to an active block. Confirmations: 1"),
                "separate explicit status check proves exact bytes and active block");
            host.RequestClose();
            break;
        default:return;
        }
        ++stage;
    }
    void AdvanceNode(std::string_view step)
    {
        std::ofstream(fixtures/("request-"+std::string(step)))<<"ready\n";
    }
    std::string NodeResult(std::string_view step)
    {
        return Read(fixtures/("result-"+std::string(step)));
    }
    bool Location(a::TransactionLocation wanted) const
    {
        const auto& view=requests.broadcaster.View();
        return view.observation && view.observation->location==wanted;
    }
    void ReaderAction(const std::string& label, const char* role, std::string_view text = {})
    {
        if (reader_step==stage) return;
        const auto destination=output/("keyboard-"+std::to_string(stage)+".json");
        const auto pending=destination.string()+".pending";
        {
            std::ofstream instruction(pending);
            instruction<<"{\"name\":"<<rpc::Quote(label)<<",\"role\":"<<rpc::Quote(role)
                <<",\"locale\":"<<rpc::Quote(ui::locale::LanguageTag())<<",\"text\":"<<rpc::Quote(text)<<"}\n";
            Check(bool(instruction),"recorded keyboard navigation target");
        }
        std::ofstream(output/("keyboard-"+std::to_string(stage)+".semantics.txt")) << state.presentation.Snapshot();
        std::filesystem::rename(pending,destination);
        reader_step=stage;
    }
    bool ReaderCapture(const platform::NativeFrame& frame,const char* name)
    {
        const auto packet=state.presentation.exchange.Published();
        if (!packet || !packet->valid || packet->overflow) return false;
        Capture(frame,name);
        return true;
    }
    void Reader(platform::NativeHost& host,const platform::NativeFrame& frame)
    {
        const auto& tx=*valid.expected->transaction;
        const auto address=p::OutputAddress(tx.outputs.front().script,p::DisplayNetwork::Regtest);
        if (!address) throw std::runtime_error("Invalid synthetic destination");
        const auto amount=ui::locale::Btc(tx.outputs.front().value);
        const auto* draft=state.documents.Find(wallet);
        const auto* row=draft->recipients.Find(draft->recipients.selected);
        const auto* imported=state.documents.Find(valid.document);
        const auto* approval=sh::CurrentSigningApproval(state,requests,valid.document);
        const auto* artifact=sh::FinalizedMaterial(state,valid.document);
        const auto& submission=requests.broadcaster.View();
        using Stage=a::BroadcastStage;
        const auto key=[&](const char* english,const char* role="button",std::string_view text={}) {
            ReaderAction(ui::locale::Text(english),role,text);
        };
        switch (stage) {
        case 0:
            key("Send","radio button");
            if (draft->section!=a::Section::Send) return;
            break;
        case 1:
            key("Pay to","entry",*address);
            if (!row || row->address!=address) return;
            break;
        case 2:
            key("Amount (BTC)","entry",amount);
            if (!row || row->amount!=amount || ParseRecipientAmount(row->amount,row->unit)!=tx.outputs.front().value
                || !std::filesystem::exists(output/"keyboard-2-enter")) return;
            if (!ReaderCapture(frame,"reader-send")) return;
            break;
        case 3:
            ReaderAction(imported->name,"page tab");
            if (state.documents.ActiveId()!=valid.document) return;
            Check(row && row->address==address && ParseRecipientAmount(row->amount,row->unit)==tx.outputs.front().value,
                "offline public fixture matches keyboard-entered payment intent");
            state.documents.Find(wallet)->coin_draft=std::move(reader_origin);
            state.imported_views.at(valid.document).offline_review_origin=std::pair{wallet,draft->draft_revision};
            break;
        case 4:
            key("Signing review");
            if (!state.signing_views.at(valid.document).open) return;
            break;
        case 5:
            key("Record signing review");
            if (!Has("Reviewed commitment:")) return;
            if (!ReaderCapture(frame,"reader-review")) return;
            Check(!approval && !artifact && !Recorded(valid),"recording is separate from approval and signing");
            break;
        case 6:
            key("Approve for signing");
            if (!approval) return;
            Check(!artifact && !Recorded(valid),"approval alone leaves signed bytes and submission absent");
            break;
        case 7:
            key("Merge signed text");
            if (!state.local_document.dialog.open) return;
            Check(host.Input().Copy(p::TransactionBase64(Read(fixtures/"accepted/signed.psbt")),true),
                "private clipboard holds independently signed public fixture");
            break;
        case 8: {
            key("Validate clipboard signatures");
            const auto material=sh::SigningMaterial(state,valid.document);
            if (!material || !p::InspectSignatures(*material).Complete() || state.local_document.dialog.open) return;
            if (!ReaderCapture(frame,"reader-signed")) return;
            Check(material->wire==valid.collected->wire && !artifact && !Recorded(valid),
                "native clipboard validation collected expected signatures without finalizing or sending");
            break;
        }
        case 9:
            key("Finalize transaction");
            if (!artifact) return;
            if (!ReaderCapture(frame,"reader-final")) return;
            Check(artifact->final.document->wire==valid.expected->wire && !Recorded(valid),
                "explicit finalization produced exact independent bytes");
            break;
        case 10:
            key("Check node policy");
            if (submission.stage!=Stage::Ready) return;
            Check(!Recorded(valid),"policy check is read-only");
            break;
        case 11:
            key("Broadcast");
            if (submission.stage!=Stage::Acknowledged) return;
            Check(Recorded(valid) && !submission.observation,"exact durable submission before independent monitoring");
            break;
        case 12:
            key("Monitor transaction");
            if (!Location(a::TransactionLocation::Mempool)) return;
            if (!ReaderCapture(frame,"reader-monitor")) return;
            Check(FinalizedBytesRetained() && Attempt(valid).serial==1,"exact current mempool observation without another send");
            break;
        case 13:
            key("Stop monitoring");
            if (!Has("Monitoring paused.") || requests.broadcaster.Busy()) return;
            host.RequestClose();
            break;
        default: return;
        }
        ++stage;
    }
    void Monitor(platform::NativeHost& host,const platform::NativeFrame& frame)
    {
        using Place=a::TransactionLocation;
        using Stage=a::BroadcastStage;
        const auto& view=requests.broadcaster.View();
        if (stage<=4) {
            if (!Begin(valid,stage)) return;
        } else switch(stage) {
        case 5: if (view.stage!=Stage::Ready || !Press("Broadcast")) return; break;
        case 6:
            if (view.stage!=Stage::Acknowledged) return;
            Check(!view.observation && Recorded(valid),"acknowledgment alone supplies no observation");
            if (!Press("Monitor transaction")) return;
            break;
        case 7:
            if (!Location(Place::Mempool)) return;
            Check(Has("Confirmation is Unknown.") && Has("Observed tip:"),"exact mempool observation with source tip");
            Scroll(host);
            break;
        case 8:
            if (!Location(Place::Mempool) || !facts.transaction_document.at("monitor-broadcast").visible) return;
            Capture(frame,"in-mempool");
            AdvanceNode("mine");
            break;
        case 9:
            mined_tip=NodeResult("mine");
            if (mined_tip.empty() || !Location(Place::ActiveChain)) return;
            Check(view.observation->tip==mined_tip && view.observation->active_block==mined_tip
                && Has("Confirmations: 1") && Has("Block height:") && Has("Block time:"),
                "active exact block reports independent confirmations, height and time");
            Scroll(host);
            break;
        case 10:
            if (!Location(Place::ActiveChain) || !facts.transaction_document.at("monitor-broadcast").visible) return;
            Capture(frame,"confirmed");
            AdvanceNode("invalidate");
            break;
        case 11:
            if (NodeResult("invalidate").empty() || !requests.refresh.RequestNow(RefreshMonotonicMs())) return;
            break;
        case 12: {
            const auto refresh=requests.refresh.View();
            if (!refresh.node || refresh.node->tip!=NodeResult("invalidate")) return;
            Check(!Location(Place::ActiveChain),"known tip change retires the former confirmation");
            break;
        }
        case 13:
            if (!Location(Place::Mempool) || !view.observation->former_block_removed) return;
            Check(Has("Reorg observed.") && Has("Former block: "+mined_tip),"reorg header and mempool observation remain distinct");
            Scroll(host);
            break;
        case 14:
            if (!Location(Place::Mempool) || !view.observation->former_block_removed
                || !facts.transaction_document.at("monitor-broadcast").visible) return;
            Capture(frame,"reorged");
            AdvanceNode("reconsider");
            break;
        case 15:
            if (NodeResult("reconsider").empty() || !Location(Place::ActiveChain)) return;
            Check(view.observation->active_block==mined_tip,"reconsidered active block is re-proved");
            Check(requests.connection.SetOffline(true),"explicit offline transition");
            offline_started=Clock::now();
            break;
        case 16:
            if (!Has("Offline. Current transaction status is Unknown.")) return;
            Check(!view.observation && Has("Last observation (cached,") && Has("s old):")
                && Attempt(valid).serial==1,"offline retains historical evidence with age and no current success");
            AdvanceNode("offline");
            break;
        case 17:
            if (Clock::now()-offline_started<3s || NodeResult("offline").empty()) return;
            Check(!view.observation && Has("Offline. Current transaction status is Unknown."),"no current success during offline window");
            Check(requests.connection.SetOffline(false),"reconnect the same verified profile");
            break;
        case 18:
            if (!Location(Place::ActiveChain)) return;
            Check(view.observation->tip==NodeResult("reconsider") && Attempt(valid).serial==1
                && requests.broadcaster.Monitoring(valid.document),"reconnect resumes only exact-node observation");
            AdvanceNode("remove-again");
            break;
        case 19:
            if (NodeResult("remove-again").empty() || !Location(Place::Mempool) || !view.observation->former_block_removed) return;
            AdvanceNode("expire");
            break;
        case 20:
            if (NodeResult("expire").empty() || !Location(Place::NoLongerObserved)) return;
            Check(Has("No longer observed in this node's mempool.") && Has("The reason and final outcome are Unknown."),
                "no-match observations never claim application-proved eviction or failed submission");
            AdvanceNode("refill");
            break;
        case 21:
            if (NodeResult("refill").empty() || !Location(Place::Mempool)) return;
            AdvanceNode("replace");
            break;
        case 22:
            if (NodeResult("replace").empty() || !Location(Place::CompetingSpend)) return;
            Check(Has("The mempool now contains a different spend of the same input.") && Has("Shared input:")
                && Has("Competing transaction: "+NodeResult("replace")) && Has("Competing witness ID:"),
                "replacement observation proves the shared input and independent witness identity");
            AdvanceNode("conflict");
            break;
        case 23:
            if (NodeResult("conflict").empty() || !Location(Place::CompetingSpend)
                || !Has("A conflicting spend was observed in the active chain.")) return;
            Check(Has("Conflicting block: "+NodeResult("conflict")) && !Location(Place::ActiveChain)
                && Attempt(valid).serial==1 && FinalizedBytesRetained(),"active competing spend never becomes this transaction's confirmation");
            if (!Press("Stop monitoring")) return;
            break;
        case 24: {
            if (requests.broadcaster.Monitoring(valid.document) || requests.broadcaster.Busy() || !Has("Monitoring paused.")) return;
            Check(!view.observation && Has("Monitoring paused."),"stopping retires current evidence without submitting");
            Check(Dispatch(a::Command::CheckBroadcastStatus,valid.document),"begin a separate read-only check");
            const auto original=requests.connection.Draft();
            auto different=original;
            different.connection.port=original.connection.port==65535 ? 65534 : original.connection.port+1;
            Check(requests.connection.SetDraft(different) && requests.connection.SetDraft(original),"retire result on profile revision change");
            break;
        }
        case 25:
            if (requests.broadcaster.Busy() || !Has("The read-only check was retired.")) return;
            Check(!view.observation && !requests.broadcaster.Monitoring(valid.document) && FinalizedBytesRetained(),
                "stale result cannot restore current status and exact bytes stay inspectable");
            host.RequestClose();
            break;
        default:return;
        }
        ++stage;
    }
    bool FinalizedBytesRetained()
    {
        const auto* artifact=sh::FinalizedMaterial(state,valid.document);
        return artifact && artifact->final.document->wire==valid.expected->wire
            && facts.transaction_document.contains("save-final-transaction");
    }
    void Teardown(const platform::NativeTeardown& state) override
    {
        settled=state.renderer_cleared && state.platform_cleared && state.context_cleared && state.input_cleared;
        if (role=="reader") settled=settled && state.accessibility_cleared && state.imgui_cleared && state.textures_released;
        Check(settled,"native renderer and input teardown");
    }
    void Report()
    {
        if (role=="restart") {
            Check(stage==19 && frames==1 && settled && !requests.broadcaster.Busy(),"all restart stages and renderer settled");
            std::ofstream(output/"native-result.json")<<"{\"journey\":\"submission-recovery\",\"checks\":"<<checks<<
                ",\"frames\":"<<frames<<",\"settled\":true,\"staleNodeRetired\":true,\"explicitRetries\":1,\"activeBlockVerified\":true}\n";
            return;
        }
        if (role=="reader") {
            Check(stage==14 && frames==5 && settled && !requests.broadcaster.Busy(),"reader journey and native resources settled");
            std::ofstream(output/"native-result.json")<<"{\"journey\":\"reader-send-monitor\",\"checks\":"<<checks
                <<",\"frames\":"<<frames<<",\"settled\":true,\"stages\":"<<stage<<"}\n";
            return;
        }
        if (role=="monitor") {
            Check(stage==26 && frames==3 && settled && !requests.broadcaster.Busy(),"lifecycle journey and owned renderer settled");
            std::ofstream(output/"native-result.json")<<"{\"journey\":\"broadcast-monitor\",\"checks\":"<<checks
                <<",\"frames\":"<<frames<<",\"settled\":true,\"stages\":"<<stage<<"}\n";
            return;
        }
        Check(stage==26 && frames==2 && settled && !requests.broadcaster.Busy(),"all intended stages settled");
        std::ofstream(output/"native-result.json")<<"{\"checks\":"<<checks<<",\"frames\":"<<frames
            <<",\"settled\":true,\"acknowledged\":1,\"uncertain\":1,\"policyRejected\":1,\"cancelledReadOnly\":1}\n";
    }
};
}
int main(int argc,char** argv)
{
    if (argc!=6 && !(argc==7 && (std::string_view(argv[6])=="interrupt" || std::string_view(argv[6])=="restart"
        || std::string_view(argv[6])=="monitor" || std::string_view(argv[6])=="reader"))) return 2;
    try {
        const auto* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        const auto* display=std::getenv("DISPLAY");
        if (!owned || !display || std::string_view(display)!=owned) throw std::runtime_error("Owned X11 display required.");
        Observer observer(argv[1],argv[3],argv[4],argv[5],argc==7 ? argv[6] : "");
        if (const auto* requested=std::getenv("ROOTS_GUI_BROADCAST_VISUAL_PROFILE")) {
            const std::string_view profile(requested);
            const bool reference=profile=="reference-light" || profile=="reference-dark";
            const bool compact=profile=="compact-light" || profile=="compact-dark";
            if (!reference && !compact) throw std::runtime_error("Unknown broadcast visual profile.");
            observer.width=reference ? 1200 : 960;
            observer.height=reference ? 820 : 640;
            observer.scale=reference ? 1.F : 1.5F;
            observer.dark=profile.ends_with("-dark");
        }
        platform::NativeOptions options{observer.width,observer.height,observer.dark,observer.scale,argv[2]};
        options.fixed_delta_seconds=1.F/60;
        if (observer.role=="reader") options.fixed_delta_seconds=0;
        options.accessibility=&observer.state.presentation.exchange;
        platform::NativeHost* active{};
        {
            platform::NativeHost host(options,&observer,[&](const auto& theme,const auto& appearance) {
                observer.drawn_dark=appearance.dark;
                observer.facts=platform::DrawShellFrame(*active,observer.state,observer.requests,theme,appearance);
            });
            active=&host;
            observer.renderer=host.Renderer();
            host.Run();
        }
        observer.Report();
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
