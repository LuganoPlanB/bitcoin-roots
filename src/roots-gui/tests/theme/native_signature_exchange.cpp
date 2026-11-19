// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include "app/daemon_signer.h"
#include "app/history_workspace.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace roots::gui;
namespace shell=ui::shell;
namespace c=ui::components;
namespace p=protocol;
namespace {
enum class MixedOrder { Inactive, AirgapFirst, DaemonFirst };
constexpr auto GENESIS="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
constexpr auto WALLET="Offline multisig";
std::string Read(const std::filesystem::path& path)
{
    std::ifstream file(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(file),{}};
}
struct Requests final : app::Requests {
    mutable TransactionDocuments local;
    std::optional<ConnectionService> connection;
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    std::optional<app::DaemonSigner> signer;
    TransactionDocuments* LocalDocuments() const override {return &local;}
    ConnectionService* Connection() override {return connection ? &*connection : nullptr;}
    app::DaemonSigner* Signer() override {return signer ? &*signer : nullptr;}
    app::Receipt Submit(const app::Request&) override {throw std::runtime_error("Unexpected host command in offline signing.");}
};
struct Observer final : platform::NativeObserver {
    Requests requests;
    shell::State state;
    shell::Facts facts;
    std::filesystem::path output,fixtures;
    using Clock=std::chrono::steady_clock;
    const Clock::time_point started=Clock::now();
    Display* display{};
    void* xtst{};
    int(*motion)(Display*,int,int,int,unsigned long){};
    int(*button)(Display*,unsigned,Bool,unsigned long){};
    std::shared_ptr<const p::TransactionDocument> original,partial,complete;
    std::optional<app::ApprovedFinalTransaction> finalized;
    uint64_t source{},origin{},clipboard_writes{};
    int stage{},checks{},frames{};
    bool settled{},finalize{},qr{};
    MixedOrder mixed{MixedOrder::Inactive};
    bool online{},sign_requested{};
    uint64_t static_source{};
    size_t qr_captured{},qr_expected{},qr_received{},qr_total{};

    void Check(bool valid,const char* reason)
    {
        if (!valid) throw std::runtime_error("Signature exchange stage "+std::to_string(stage)+": "+reason);
        ++checks;
    }
    Observer(std::filesystem::path out,std::filesystem::path folder,bool finish,bool transport,bool connected=false):
        output(std::move(out)),fixtures(std::move(folder)),finalize(finish),qr(transport),online(connected)
    {
        if (finalize) requests.connection.emplace(ConnectionService::Probe{});
        original=p::ParseTransactionDocument(Read(fixtures/"original.psbt"),p::DisplayNetwork::Regtest).document;
        Check(original && original->transaction && original->transaction->fee,"fixture original and committed fee");
        const auto& tx=*original->transaction;
        const auto descriptor=p::PreviewPublicWallet(p::PublicImportKind::Descriptor,Read(fixtures/"descriptor.txt"),"regtest");
        Check(descriptor.preview && descriptor.preview->descriptors.size()==1,"independent multisig descriptor");
        const auto& proof=descriptor.preview->descriptors.front();
        app::WalletBinding wallet{*BackendSessionId::Make("offline-signature-collection"),
            *WalletId::Make(WALLET),{*Hash256::Parse(GENESIS)},{}};
        if (online) {
            requests.connection.emplace([](const ServerCandidate& candidate,std::stop_token) {
                BackendCapabilities caps;
                caps.candidate=candidate;
                caps.chain="regtest";
                caps.genesis=GENESIS;
                caps.authenticated=caps.identity_verified=true;
                caps.wallet=WalletAvailability::Available;
                caps.descriptors=caps.private_keys=true;
                caps.external_signer=false;
                caps.methods["walletprocesspsbt"]={true,true,true,false,{"psbt","sign","finalize","bip32derivs"}};
                return caps;
            });
            ServerCandidate profile;
            const auto port=std::stoul(Read(fixtures.parent_path()/"loopback-port.txt"));
            Check(port>0 && port<=UINT16_MAX,"owned loopback signer port");
            profile.connection.host="127.0.0.1";
            profile.connection.port=uint16_t(port);
            profile.connection.credential.reference=(fixtures.parent_path()/"fixture.cookie").string();
            profile.expected_genesis=GENESIS;
            profile.wallet=WALLET;
            auto& connection=*requests.connection;
            Check(connection.SetDraft(profile) && connection.Test(),"test scoped synthetic capability records");
            while(connection.Busy() && Clock::now()-started<std::chrono::seconds(3)) {
                connection.Poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            Check(connection.Apply(),"apply only the owned loopback wallet");
            const auto binding=app::VerifiedWalletBinding(*connection.Active());
            Check(binding.has_value(),"exact signing wallet identified");
            wallet=*binding;
            requests.signer.emplace(requests.transport,connection);
        }
        origin=state.documents.OpenWallet(WALLET,wallet,true);
        auto* draft=state.documents.Find(origin);
        app::CoinDraft pin{wallet,{},{}};
        p::OfflineReviewEvidence review{p::DisplayNetwork::Regtest,{},tx.outputs,*tx.fee,{}};
        for (size_t i=0;i<tx.inputs.size();++i) {
            const auto& input=tx.inputs[i];
            Check(input.previous_value && input.previous_script==proof.script,"independent full previous output script");
            pin.inputs.push_back({input.outpoint,input.previous_value,input.previous_script,{},{},
                p::OfflineCoinProof{Read(fixtures/("previous-"+std::to_string(i)+".hex")),proof.descriptor,
                    Read(fixtures/"redeem.hex"),{},1700000000000}});
            review.inputs.push_back({input.outpoint,*input.previous_value,proof.descriptor});
        }
        draft->coin_draft=std::move(pin);
        const auto revision=draft->draft_revision;
        source=state.documents.OpenImportedDocument({original,p::DisplayNetwork::Regtest});
        auto& admitted=state.imported_views[source];
        admitted.wallet=wallet;
        admitted.review_evidence=std::move(review);
        admitted.offline_review_origin=std::pair{origin,revision};
        if (qr) {
            auto small=p::ParseTransactionDocument(Read(fixtures/"static.psbt"),p::DisplayNetwork::Regtest).document;
            Check(small && small->psbt_version==0,"published static PSBT vector");
            static_source=state.documents.OpenImportedDocument({small,p::DisplayNetwork::Regtest});
            Check(static_source && state.documents.ActiveId()==static_source,"separate static inspection document");
            qr_total=std::stoul(Read(fixtures/"frame-count.txt"));
        }
        display=XOpenDisplay(nullptr);
        xtst=dlopen("libXtst.so.6",RTLD_NOW);
        Check(display && xtst,"owned X11 input");
        motion=reinterpret_cast<decltype(motion)>(dlsym(xtst,"XTestFakeMotionEvent"));
        button=reinterpret_cast<decltype(button)>(dlsym(xtst,"XTestFakeButtonEvent"));
        Check(motion && button,"physical scroll entry points");
    }
    ~Observer() override {if(display) XCloseDisplay(display); if(xtst) dlclose(xtst);}
    bool CaptureFramebuffer() const override {return true;}
    bool Has(std::string_view label) const {return state.presentation.Snapshot().find(label)!=std::string::npos;}
    bool Action(std::string_view name,c::ax::SemanticAction action,std::string value={})
    {
        const auto packet=state.presentation.exchange.Published();
        if (!packet || !packet->valid) return false;
        const auto item=std::ranges::find_if(packet->nodes,[&](const auto& node){
            return node.name==name && !node.state.disabled && c::ax::Supports(node.actions,action);
        });
        if (item==packet->nodes.end()) return false;
        Check(state.presentation.exchange.QueueNext(packet->generation,item->id,action,std::move(value)).second
            ==c::ActionResult::Queued,"native bounded semantic action");
        return true;
    }
    bool Press(std::string_view label) {return Action(label,c::ax::SemanticAction::Press);}
    bool Path(const std::filesystem::path& path) {return Action("Local file path",c::ax::SemanticAction::SetValue,path.string());}
    void Scroll(platform::NativeHost& host)
    {
        int x{},y{};
        glfwGetWindowPos(host.Window(),&x,&y);
        Check(motion(display,-1,x+750,y+538,0),"move pointer into workbench");
        for (int i=0;i<11;++i) Check(button(display,5,True,0) && button(display,5,False,0),"native downward scroll");
        XSync(display,False);
    }
    void Capture(const platform::NativeFrame& frame,const char* name)
    {
        Check(frame.width==960 && frame.height==640 && frame.effective_scale==1.F && frame.decorated
            && frame.font_texture && !frame.gl_error,"actual compact renderer");
        Check(state.presentation.Tree().Validate() && !state.presentation.Tree().overflow,"bounded valid semantic tree");
        std::ofstream image(output/(std::string(name)+".ppm"),std::ios::binary);
        image<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for (int y=frame.framebuffer_height-1;y>=0;--y) for (int x=0;x<frame.framebuffer_width;++x)
            image.write(reinterpret_cast<const char*>(frame.rgba.data()+(size_t(y)*frame.framebuffer_width+x)*4),3);
        Check(bool(image),"captured framebuffer");
        std::ofstream(output/(std::string(name)+".semantics.txt"))<<state.presentation.Snapshot();
        ++frames;
    }
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override
    {
        if (Clock::now()-started<std::chrono::seconds(25)) return;
        std::ofstream(output/"deadline.txt")<<stage<<'\n'<<state.presentation.Snapshot();
        throw std::runtime_error("Native signature exchange stage "+std::to_string(stage)+" timed out.");
    }
    void QrFrame(platform::NativeHost& host,const platform::NativeFrame& frame)
    {
        const auto& panel=state.local_document;
        switch (stage) {
        case 0: if (!Press("Show PSBT QR")) return; break;
        case 1:
            if (Clock::now()-panel.qr_started<std::chrono::milliseconds(250)
                || !Has("Static QR · Base64 PSBT") || !Has("Frame 1 / 1") || !Press("Close QR")) return;
            Check(!shell::CurrentSigningApproval(state,requests,static_source),"static inspection confers no approval");
            Capture(frame,"qr-display-static");
            break;
        case 2:
            if (panel.dialog.open || panel.close_pending) return;
            Check(state.documents.Activate(source),"return to separate original PSBT");
            break;
        case 3:
            Check(!shell::DispatchSigningCommand(state,requests,{app::Command::SignTransaction,source}).accepted
                && !shell::BeginLocalDocument(state,requests,shell::LocalDocumentMode::MergeQr,{},true),
                "injected QR collection cannot bypass separate approval");
            if (!Press("Signing review")) return;
            break;
        case 4: if (!Press("Record signing review")) return; break;
        case 5: if (!Press("Approve for signing")) return; break;
        case 6:
            if (!shell::CurrentSigningApproval(state,requests,source) || !Press("Show PSBT QR")) return;
            break;
        case 7:
            if (!Has("Sequential UR v2 · psbt") || !Has("omitted 1 global xpub records") || panel.qr_position==0) return;
            qr_expected=panel.qr_export.frames.size();
            Check(qr_expected>1 && !panel.qr_paused,"automatic animation displayed a later frame");
            if (!Press("Restart display")) return;
            break;
        case 8:
            if (!Has("Frame 1 / "+std::to_string(qr_expected)) || !Press("Pause animation")) return;
            break;
        case 9: {
            if (!panel.qr_paused || !Has("Resume animation")
                || !Has("Frame "+std::to_string(qr_captured+1)+" / "+std::to_string(qr_expected))) return;
            if (qr_captured==0) Capture(frame,"qr-display-animated");
            Capture(frame,("qr-out-"+std::to_string(qr_captured)).c_str());
            ++qr_captured;
            if (qr_captured==qr_expected) {
                stage=10;
                return;
            }
            Check(Press("Next frame"),"advance the paused native symbol");
            return;
        }
        case 10: if (!Press("Close QR")) return; break;
        case 11:
            if (panel.dialog.open || panel.close_pending || !Has("Approved for this exact transaction.")
                || !Press("Import signature QR")) return;
            break;
        case 12:
            if (!Path(fixtures/"inbound-0.png")) return;
            break;
        case 13: if (!Press("Read QR image")) return; break;
        case 14:
            if (requests.local.Busy() || panel.qr_assembly.Collected()!=1) return;
            Check(panel.qr_assembly.Expected()==qr_total && shell::SigningMaterial(state,source)==original,
                "one actual PNG fragment remains incomplete and changes no signing material");
            qr_received=1;
            break;
        case 15:
            if (requests.local.Busy()) return;
            if (!Action("Decoded QR text",c::ax::SemanticAction::SetValue,
                        Read(fixtures/("inbound-"+std::to_string(qr_received)+".txt")))) return;
            break;
        case 16:
            if (!Press("Read QR text")) return;
            ++qr_received;
            if (qr_received<qr_total) {stage=15; return;}
            break;
        case 17:
            if (requests.local.Busy() || panel.dialog.open || panel.close_pending) return;
            if (!Has("Complete · 2 of 2 input policies satisfied")) return;
            complete=shell::SigningMaterial(state,source);
            Check(complete && complete!=original && state.documents.Active()->imported->parsed==original
                && shell::CurrentSigningApproval(state,requests,source)
                && p::InspectSignatures(*complete).Complete(),
                "independent airgapped signatures merged through exact current approval");
            Check(!shell::FinalizedMaterial(state,source),"QR import did not finalize");
            std::ofstream(output/"collected.psbt",std::ios::binary)<<complete->wire;
            Capture(frame,"qr-collected");
            break;
        case 18:
            if (!Press("Finalize transaction")) return;
            Scroll(host);
            break;
        case 19: {
            const auto* artifact=shell::FinalizedMaterial(state,source);
            if (!artifact || !Has("Finalized transaction") || !Press("Show PSBT QR")) return;
            Check(artifact->approval.reviewed.content==original && artifact->collected==complete
                && artifact->collected_source==app::SigningDigest(complete->source)
                && artifact->final_source==app::SigningDigest(artifact->final.document->wire)
                && artifact->final.fee.sats()==500 && !artifact->final.document->psbt_version,
                "explicit local finalization preserves original, collected material, and exact fee");
            finalized=*artifact;
            std::ofstream(output/"airgapped-final.tx",std::ios::binary)<<artifact->final.document->wire;
            Capture(frame,"qr-finalized");
            break;
        }
        case 20:
            if (!Has("Sequential UR v2 · psbt") || !panel.dialog.open) return;
            state.semantic_hide_toggle=true;
            break;
        case 21:
            if (panel.dialog.open || panel.close_pending) return;
            Check(!Has("QR encoding and size") && panel.qr_export.frames.empty()
                && state.documents.Find(source)->imported->parsed==original,
                "privacy retires the outbound QR and retains the exact original");
            host.RequestClose();
            break;
        }
        ++stage;
    }
    void SoftwareReply(std::string_view file)
    {
        const auto* approval=shell::CurrentSigningApproval(state,requests,source);
        Check(approval!=nullptr,"software reply requires an exact current approval");
        const auto returned=p::ParseTransactionDocument(Read(fixtures/file),p::DisplayNetwork::Regtest).document;
        Check(returned!=nullptr,"independently signed daemon fixture parsed");
        const auto result=p::ApplyDaemonPsbt(*approval->reviewed.content,*returned,true);
        Check(result.document!=nullptr,"production daemon result verification passed");
        shell::CollectDaemonSigningResult(state,requests,*approval,
            {app::DaemonSignAction::Sign,result.document,"Verified fixture daemon reply.",true});
        Check(!shell::FinalizedMaterial(state,source),"collecting daemon material does not finalize");
    }

    void MixedFrame(platform::NativeHost& host)
    {
        const bool airgap_first=mixed==MixedOrder::AirgapFirst;
        switch (stage) {
        case 0: if (!Press("Signing review")) return; break;
        case 1: if (!Press("Record signing review")) return; break;
        case 2: if (!Press("Approve for signing")) return; break;
        case 3:
            if (!shell::CurrentSigningApproval(state,requests,source)) return;
            if (!airgap_first && shell::SigningMaterial(state,source)==original) {
                if (online) {
                    if (!sign_requested && Press("Sign with daemon")) sign_requested=true;
                    return;
                }
                SoftwareReply("daemon.psbt");
            }
            if (!Press("Merge signed file")) return;
            break;
        case 4: if (!Path(fixtures/"airgap.psbt")) return; break;
        case 5: if (!Press("Validate and merge")) return; break;
        case 6: {
            if (state.local_document.dialog.open || state.local_document.close_pending) return;
            const auto current=shell::SigningMaterial(state,source);
            Check(current && current!=original && shell::CurrentSigningApproval(state,requests,source),
                "airgapped adoption kept the exact approved original");
            if (!airgap_first) {stage=9;break;}
            Check(!p::InspectSignatures(*current).Complete(),"each airgapped input still needs the daemon key");
            if (online) {stage=8;break;}
            SoftwareReply("airgap.psbt");
            Check(shell::SigningMaterial(state,source)==current && shell::CurrentSigningApproval(state,requests,source),
                "duplicate collection does not replace material or retire approval");
            SoftwareReply("daemon-conflict.psbt");
            Check(shell::SigningMaterial(state,source)==current && !shell::CurrentSigningApproval(state,requests,source),
                "conflict retires approval and keeps the prior verified collection");
            break;
        }
        case 7: if (!Press("Record signing review")) return; break;
        case 8: if (!Press("Approve for signing")) return; break;
        case 9:
            if (!shell::CurrentSigningApproval(state,requests,source)) return;
            if (online) {
                if (!sign_requested && Press("Sign with daemon")) sign_requested=true;
                if (!p::InspectSignatures(*shell::SigningMaterial(state,source)).Complete()) return;
            } else SoftwareReply("daemon.psbt");
            break;
        case 10: {
            const auto current=shell::SigningMaterial(state,source);
            const auto checked=p::InspectSignatures(*current);
            Check(state.documents.Active()->imported->parsed==original
                && checked.Complete() && checked.inputs.size()==2 && current->wire.find("external signer opaque material")!=std::string::npos
                && current->wire.find("original input A")!=std::string::npos,"both independent inputs complete with opaque fields and unchanged original");
            Check(shell::DispatchSigningCommand(state,requests,{app::Command::FinalizeSigning,source}).accepted,
                "explicit production finalization succeeds with combined collection");
            const auto* artifact=shell::FinalizedMaterial(state,source);
            Check(artifact && artifact->collected==current && artifact->final.document->wire==Read(fixtures/"expected.tx"),
                "combined collection final wire matches independently serialized fixture bytes");
            host.RequestClose();
            break;
        }
        default: return;
        }
        ++stage;
    }

    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override
    {
        if (frame.rendered_frames<5) return;
        if (mixed!=MixedOrder::Inactive) {MixedFrame(host);return;}
        if (qr) {QrFrame(host,frame);return;}
        switch (stage) {
        case 0: if (!Press("Signing review")) return; break;
        case 1:
            if (finalize) Check(!shell::DispatchSigningCommand(state,requests,{app::Command::FinalizeSigning,source}).accepted
                && !shell::FinalizedMaterial(state,source),"direct finalization requires separate review and approval");
            if (!Press("Record signing review")) return;
            break;
        case 2: if (!Press("Approve for signing")) return; break;
        case 3:
            if (!Has("Approved for this exact transaction.")) return;
            Check(!facts.transaction_document.at("psbt-qr-import").disabled
                && !facts.transaction_document.at("psbt-qr-export").disabled,"verified QR routes available from current approval");
            if (!Press("Merge signed file")) return;
            break;
        case 4: if (!Path(fixtures/"partial.psbt")) return; break;
        case 5: if (!Press("Validate and merge")) return; break;
        case 6:
            if (!Has("Partial · 1 of 2 input policies satisfied")) return;
            partial=shell::SigningMaterial(state,source);
            Check(partial && partial!=original && state.documents.ActiveId()==source
                && state.documents.Active()->imported->parsed==original
                && shell::CurrentSigningApproval(state,requests,source),"same exact approval and original after verified merge");
            if (finalize) {
                Check(facts.transaction_document.at("finalize-signing").disabled
                    && !shell::DispatchSigningCommand(state,requests,{app::Command::FinalizeSigning,source}).accepted
                    && !shell::FinalizedMaterial(state,source),"partial collection cannot finalize through the actual command");
                stage=16;
                break;
            }
            if (!Press("Next signature input")) return;
            Scroll(host);
            break;
        case 7:
            if (!Has("Input 2 of 2 · 1 verified · 2 required")) return;
            if (!facts.transaction_document.at("signature-previous").visible) return;
            if (!Press("Merge signed file")) return;
            Capture(frame,"signing-partial");
            break;
        case 8: if (!Path(fixtures/"wrong.psbt")) return; break;
        case 9: if (!Press("Validate and merge")) return; break;
        case 10:
            if (!Has("Output 1 amount or order changed.")) return;
            Check(shell::SigningMaterial(state,source)==partial && state.documents.Active()->imported->parsed==original
                && !state.signing_reviews.HasReview(source),"material conflict revokes exact approval while retaining verified bytes");
            if (!Press("Cancel")) return;
            Capture(frame,"signing-import-rejected");
            break;
        case 11: if (!Press("Record signing review")) return; break;
        case 12: if (!Press("Approve for signing")) return; break;
        case 13:
            clipboard_writes=host.InputState().clipboard_writes;
            if (!Press("Copy PSBT text")) return;
            break;
        case 14:
            if (host.InputState().clipboard_writes!=clipboard_writes+1) return;
            if (!Press("Merge signed text")) return;
            break;
        case 15: if (!Press("Validate clipboard signatures")) return; break;
        case 16:
            if (!Has("no new verified signature")) return;
            Check(shell::SigningMaterial(state,source)==partial && state.signing_reviews.HasReview(source),
                "copied public text round trip has no new signature material");
            if (!Press("Cancel")) return;
            break;
        case 17:
            if (state.local_document.dialog.open || state.local_document.close_pending) return;
            if (!Press("Merge signed text")) return;
            Check(host.Input().Copy(p::TransactionBase64(Read(fixtures/"complete.psbt")),true),"owned external signer places text on clipboard");
            break;
        case 18: if (!Press("Validate clipboard signatures")) return; break;
        case 19:
            if (!Has("Complete · 2 of 2 input policies satisfied")) return;
            complete=shell::SigningMaterial(state,source);
            Check(complete && complete!=partial && state.documents.Active()->imported->parsed==original
                && shell::CurrentSigningApproval(state,requests,source),"new validated text completes every input and retains exact approval");
            if (finalize) {
                app::Request injected{app::Command::FinalizeSigning,source};
                injected.value="untrusted final bytes";
                Check(!shell::FinalizedMaterial(state,source) && !shell::DispatchSigningCommand(state,requests,injected).accepted,
                    "complete collection remains a PSBT until explicit finalization and injected bytes are rejected");
                stage=26;
                break;
            }
            if (!Press("Save collected PSBT")) return;
            break;
        case 20: if (!Path(fixtures/"exported.psbt")) return; break;
        case 21: if (!Press("Save collected PSBT")) return; break;
        case 22: {
            if (state.local_document.dialog.open || state.local_document.close_pending) return;
            const auto saved=Read(fixtures/"exported.psbt");
            Check(saved==complete->source,"exact public signed PSBT file export");
            shell::State fresh;
            const auto id=fresh.documents.OpenImportedDocument({complete,p::DisplayNetwork::Regtest});
            Check(id && !shell::CurrentSigningApproval(fresh,requests,id),"copied or freshly loaded signed data cannot supply approval");
            if (!Press("Merge signed file")) return;
            break;
        }
        case 23: if (!Path(fixtures/"partial.psbt")) return; break;
        case 24: if (!Press("Validate and merge")) return; break;
        case 25:
            if (!requests.local.Busy()) return;
            ++state.documents.Find(origin)->draft_revision.value;
            break;
        case 26:
            if (state.local_document.dialog.open || state.local_document.close_pending || requests.local.Busy()) return;
            Check(!state.signing_reviews.HasReview(source) && shell::SigningMaterial(state,source)==complete
                && state.documents.Active()->imported->parsed==original,
                "pending file reply cannot adopt after independent origin revision changes");
            host.RequestClose();
            break;
        case 27: if (!Press("Finalize transaction")) return; break;
        case 28: {
            const auto* artifact=shell::FinalizedMaterial(state,source);
            if (!artifact || !Has("Finalized transaction")) return;
            const auto* approved=shell::CurrentSigningApproval(state,requests,source);
            Check(approved && artifact->approval==*approved
                && artifact->approval.reviewed.content==original && artifact->collected==complete
                && artifact->final.document!=complete && !artifact->final.document->psbt_version
                && artifact->collected_source==app::SigningDigest(complete->source)
                && artifact->final_source==app::SigningDigest(artifact->final.document->wire),
                "exact approval, original, collection and final bytes have separate provenance");
            Check(facts.transaction_document.at("finalize-signing").disabled
                && !facts.transaction_document.at("final-transaction-qr").disabled
                && Has("Final fee rate: 500 sats / "),"actual fee rate and explicit final QR route");
            finalized=*artifact;
            Scroll(host);
            break;
        }
        case 29:
            if (!facts.transaction_document.at("save-final-transaction").visible) return;
            if (!Press("Save final transaction")) return;
            Capture(frame,"finalized-transaction");
            break;
        case 30:
            if (!Path(output/"stale-final.tx")) return;
            Check(requests.connection->SetOffline(true),"explicit offline transition while export is open");
            break;
        case 31: {
            if (state.local_document.dialog.open || state.local_document.close_pending) return;
            if (!Has("These bytes came from an earlier approved collection.")) return;
            const auto* artifact=shell::FinalizedMaterial(state,source);
            Check(!shell::CurrentSigningApproval(state,requests,source)
                && !shell::DispatchSigningCommand(state,requests,{app::Command::FinalizeSigning,source}).accepted
                && !std::filesystem::exists(output/"stale-final.tx")
                && artifact && artifact->final.document==finalized->final.document,
                "connection change retires approval and old export while retaining exact public artifact");
            if (!Press("Save final transaction")) return;
            break;
        }
        case 32: if (!Path(output/"final.tx")) return; break;
        case 33: if (!Press("Save final transaction")) return; break;
        case 34:
            if (state.local_document.dialog.open || state.local_document.close_pending) return;
            Check(Read(output/"final.tx")==finalized->final.document->wire
                && state.documents.Find(source)->imported->parsed==original && shell::SigningMaterial(state,source)==complete,
                "binary export matches final wire and leaves both PSBTs intact");
            if (!Press("View final transaction")) return;
            break;
        case 35:
            if (state.documents.ActiveId()==source) return;
            Check(state.documents.Active()->imported->parsed==finalized->final.document
                && !shell::CurrentSigningApproval(state,requests,state.documents.ActiveId())
                && !shell::DispatchSigningCommand(state,requests,{app::Command::FinalizeSigning,state.documents.ActiveId()}).accepted,
                "offline raw view carries no approval or execution authority");
            host.RequestClose();
            break;
        default:return;
        }
        ++stage;
    }
    void Teardown(const platform::NativeTeardown& state) override
    {
        settled=state.renderer_cleared && state.platform_cleared && state.context_cleared && state.input_cleared;
        Check(settled,"settled renderer and native input");
    }
    void Report()
    {
        if (mixed!=MixedOrder::Inactive) {
            Check(stage==11 && settled && !requests.local.Busy() && (!online || (sign_requested && !requests.signer->Busy())),
                "both input policies and native teardown completed");
            std::ofstream(output/"native-result.json")<<"{\"checks\":"<<checks<<",\"networkDenied\":"<<(online?"false":"true")
                <<",\"productionSignerPoll\":"<<(online?"true":"false")<<",\"settled\":true}\n";
            return;
        }
        if (qr) {
            Check(stage==22 && frames==int(qr_expected)+4 && settled && !requests.local.Busy() && finalized,
                "static and animated QR, approved collection, explicit finalization and teardown complete");
            const auto& final=finalized->final;
            const auto& tx=*final.document->transaction;
            std::ofstream(output/"native-result.json")<<"{\"checks\":"<<checks<<",\"frames\":"<<frames
                <<",\"animatedFrames\":"<<qr_expected<<",\"networkDenied\":true,\"settled\":true"
                <<",\"final\":{\"txid\":"<<rpc::Quote(tx.txid->hex())<<",\"wtxid\":"<<rpc::Quote(tx.wtxid->hex())
                <<",\"size\":"<<final.size<<",\"stripped\":"<<final.stripped_size<<",\"weight\":"<<final.weight
                <<",\"vsize\":"<<final.vsize<<",\"fee\":"<<final.fee.sats()<<",\"sha256\":"<<rpc::Quote(finalized->final_source.hex())<<"}}\n";
            return;
        }
        Check(stage==(finalize?36:27) && frames==(finalize?1:2) && settled && !requests.local.Busy(),"all intended stages ran and settled");
        std::ofstream result(output/"native-result.json");
        result<<"{\"journey\":"<<rpc::Quote(finalize?"explicit-offline-finalization":"approved-file-and-text-signature-collection")<<",\"checks\":"<<checks
            <<",\"frames\":"<<frames<<",\"networkDenied\":true,\"settled\":true,\"commitment\":"
            <<rpc::Quote(p::TransactionCommitment(*complete)->hex())<<",\"originalSha256\":"
            <<rpc::Quote(app::SigningDigest(original->source)->hex())<<",\"partialSha256\":"
            <<rpc::Quote(app::SigningDigest(partial->source)->hex())<<",\"completeSha256\":"
            <<rpc::Quote(app::SigningDigest(complete->source)->hex());
        if (finalized) {
            const auto& final=finalized->final;
            const auto& tx=*final.document->transaction;
            result<<",\"final\":{\"txid\":"<<rpc::Quote(tx.txid->hex())<<",\"wtxid\":"<<rpc::Quote(tx.wtxid->hex())
                <<",\"size\":"<<final.size<<",\"stripped\":"<<final.stripped_size<<",\"weight\":"<<final.weight
                <<",\"vsize\":"<<final.vsize<<",\"fee\":"<<final.fee.sats()<<",\"sha256\":"<<rpc::Quote(finalized->final_source.hex())<<"}";
        }
        result<<"}\n";
    }
};
}
int main(int argc,char** argv)
{
    const std::string_view mode=argc==5 ? argv[4] : "";
    if (argc!=4 && !(argc==5 && (mode=="--finalize" || mode=="--qr" || mode=="--mixed-online"))) return 2;
    try {
        const auto* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        const auto* display=std::getenv("DISPLAY");
        if (!owned || !display || std::string_view(display)!=owned) throw std::runtime_error("Owned display required.");
        if (mode!="--mixed-online") for (const int family:{AF_INET,AF_INET6}) {
            const int fd=socket(family,SOCK_STREAM,0);
            const bool denied=fd<0 && errno==EPERM;
            if (fd>=0) close(fd);
            if (!denied) throw std::runtime_error("Network fence required.");
        }
        const auto run=[&](Observer& observer) {
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
        };
        if (mode!="--mixed-online") {
            Observer observer(argv[1],argv[3],mode=="--finalize",mode=="--qr");
            run(observer);
        }
        if (mode=="--finalize" || mode=="--mixed-online") {
            for (const auto [name,order] : {std::pair{"airgap-first",MixedOrder::AirgapFirst}, {"daemon-first",MixedOrder::DaemonFirst}}) {
                auto directory=std::filesystem::path(argv[1])/name;
                std::filesystem::create_directory(directory);
                Observer mixed(directory,argv[3],false,false,mode=="--mixed-online");
                mixed.mixed=order;
                run(mixed);
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
