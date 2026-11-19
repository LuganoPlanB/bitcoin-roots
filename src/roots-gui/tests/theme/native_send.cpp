// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include "app/connection_observations.h"
#include "app/daemon_signer.h"

#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

using namespace roots::gui;
namespace {
namespace shell = ui::shell;
namespace c = ui::components;
constexpr auto ADDRESS = "1BitcoinEaterAddressDontSendf59kuE";
constexpr std::string_view FIXTURE_PASSPHRASE="signnativefixture";

struct Requests final : app::Requests {
    bool funding_enabled{};
    bool fees_enabled{};
    bool live{};
    bool signer_enabled{};
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    ConnectionService connection{[this](const ServerCandidate& candidate, std::stop_token stop) {
        return ProbeCapabilities(transport, candidate, stop);
    }};
    RefreshService refresh{[this](const RefreshRequest& request, std::stop_token stop) {
        return FetchRefresh(transport, request, stop);
    }};
    FundingService funding{[this](const FundingRequest& request, std::stop_token stop) {
        if (!live) throw std::runtime_error("Offline funding must be unavailable");
        return FetchFunding(transport, request, stop);
    }};
    FeeService fees{[this](const FeeRequest& request, std::stop_token stop) {return FetchFees(transport,request,stop);}};
    mutable TransactionDocuments local_documents;
    bool local_enabled{};
    std::unique_ptr<app::RecoverySession> recovery;
    FundingService* Funding() override {return funding_enabled ? &funding : nullptr;}
    app::DaemonSigner signer{transport,connection};
    app::DaemonSigner* Signer() override {return signer_enabled ? &signer : nullptr;}
    FeeService* Fees() override {return fees_enabled ? &fees : nullptr;}
    ConnectionService* Connection() override {return live ? &connection : nullptr;}
    RefreshService* Refresh() override {return live ? &refresh : nullptr;}
    app::RecoverySession* Recovery() override {return recovery.get();}
    TransactionDocuments* LocalDocuments() const override {return local_enabled ? &local_documents : nullptr;}
    app::HostObservations Observe() const override
    {
        return live ? app::ConnectionObservations(connection, refresh) : app::HostObservations{};
    }
    app::Receipt Submit(const app::Request&) override { throw std::runtime_error("Unexpected external command"); }
};

class Observer final : public platform::NativeObserver {
    const std::filesystem::path output;
    Display* display{};
    void* xtst{};
    int (*motion)(Display*, int, int, int, unsigned long){};
    int (*button)(Display*, unsigned, Bool, unsigned long){};
    int (*key)(Display*, unsigned, Bool, unsigned long){};
    bool release{};
    bool key_release{};
    int stage{}, checks{}, frames{};
    size_t unlock_characters_sent{};
    bool unlock_input_clicked{};
    unsigned unlock_key_pressed{};
    uint64_t unlock_action{};
    DraftRevision unlock_revision{};
    uint64_t last_edit_diagnostic{};
    bool cleaned{};
    bool create{};
    bool fees{};
    bool flow{};
    bool batch{};
    bool review{};
    bool edit{};
    bool sign{};
    uint64_t source_id{}, created_id{};
    uint64_t working_id{};
    std::optional<Hash256> first_review;
    DraftRevision source_revision{};
    std::string accepted;
    std::string destination;
    std::vector<std::string> flow_destinations;
    std::optional<FundingChange> chosen_change;
    DraftRevision flow_revision;
    std::filesystem::path metadata_path;

    void Check(bool ok, const char* reason)
    {
        if (!ok) throw std::runtime_error("Send stage " + std::to_string(stage) + ": " + reason);
        ++checks;
    }
    void Click(platform::NativeHost& host, const c::ItemFacts& target)
    {
        const auto& box = target.rect;
        const bool reachable=target.visible && !target.disabled && box[0]>=0 && box[1]>=0 && box[2]<=960 && box[3]<=640;
        if (!reachable) std::cerr << "Native control rectangle " << box[0] << ',' << box[1] << ','
            << box[2] << ',' << box[3] << " visible=" << target.visible << " disabled=" << target.disabled << '\n';
        Check(reachable,"Reachable native control");
        int x{}, y{};
        glfwGetWindowPos(host.Window(), &x, &y);
        Check(motion(display, -1, x + int((box[0] + box[2]) / 2), y + int((box[1] + box[3]) / 2), 0)
            && button(display, 1, True, 0), "Native mouse press");
        release = true;
        XSync(display, False);
    }
    bool EnterField(std::string text, std::string_view label = "Pay to")
    {
        const auto packet = state.presentation.exchange.Published();
        // Scope changes retire the transition frame's packet. Wait for the
        // first coherent publication before delivering input to the new tab.
        if (!packet || !packet->valid) return false;
        const auto input = std::ranges::find_if(packet->nodes, [label](const auto& node) {
            return node.name == label && c::ax::Supports(node.actions, c::ax::SemanticAction::SetValue);
        });
        Check(input != packet->nodes.end(), "Native destination editor");
        Check(state.presentation.exchange.QueueNext(packet->generation, input->id, c::ax::SemanticAction::SetValue,
            std::move(text)).second == c::ActionResult::Queued, "Owned destination edit");
        return true;
    }
    uint64_t Press(std::string_view label)
    {
        const auto packet = state.presentation.exchange.Published();
        const auto control = std::ranges::find_if(packet->nodes, [label](const auto& node) {
            return node.name == label && c::ax::Supports(node.actions, c::ax::SemanticAction::Press);
        });
        Check(control != packet->nodes.end(), "Current semantic action");
        const auto [sequence,result]=state.presentation.exchange.QueueNext(packet->generation,control->id,c::ax::SemanticAction::Press);
        Check(result==c::ActionResult::Queued,"Owned semantic press");
        return sequence;
    }
    void ClickMenu(platform::NativeHost& host, std::string_view label)
    {
        const auto packet = state.presentation.exchange.Published();
        const auto menu = std::ranges::find_if(packet->nodes, [label](const auto& node) {
            return node.name == label && node.role == c::ax::SemanticRole::Menu;
        });
        Check(menu != packet->nodes.end(), "Current native menu bounds");
        Click(host, {{menu->minimum.x,menu->minimum.y,menu->maximum.x,menu->maximum.y},
            0,false,menu->state.disabled,false,false,menu->showing});
    }
    void Capture(const platform::NativeFrame& frame, const char* name)
    {
        Check(frame.decorated && frame.font_texture && !frame.gl_error && frame.width == 960
            && frame.height == 640 && frame.effective_scale == (review ? 1.0F : 1.5F), "Compact production renderer");
        Check(state.presentation.Tree().Validate() && !state.presentation.Tree().overflow, "Valid semantic tree");
        std::ofstream image(output / (std::string(name) + ".ppm"), std::ios::binary);
        image << "P6\n" << frame.framebuffer_width << ' ' << frame.framebuffer_height << "\n255\n";
        for (int y = frame.framebuffer_height - 1; y >= 0; --y) {
            for (int x = 0; x < frame.framebuffer_width; ++x)
                image.write(reinterpret_cast<const char*>(frame.rgba.data() +
                    (size_t(y) * frame.framebuffer_width + x) * 4), 3);
        }
        std::ofstream semantics(output / (std::string(name) + ".semantics.txt"));
        semantics << state.presentation.Snapshot();
        Check(bool(image) && bool(semantics), "Retained pixels and semantic snapshot");
        ++frames;
    }
    void AdmissionRegressions(const app::ImportedDocument& imported)
    {
        app::RecoverySession memory(std::filesystem::path{});
        std::string reason;
        auto full = state.documents;
        Check(full.Close(std::array{created_id}) && full.Activate(source_id), "Isolated admission fixture");
        ++full.Active()->draft_revision.value;
        Check(!memory.CreateFunded(full, imported, reason), "Create rejects stale draft revision");
        --full.Active()->draft_revision.value;
        while (full.Documents().size() < 32) full.OpenTransaction("Unsaved fixture tab");
        full.Activate(source_id);
        Check(!memory.CreateFunded(full, imported, reason) && full.ActiveId() == source_id
            && full.Active()->draft_revision == source_revision, "Full admission leaves current draft intact");
        auto focused = state.documents;
        focused.Find(created_id)->transaction.note.SetDraft("Unsaved inspect notes");
        while (focused.Documents().size() < 32) focused.OpenTransaction("Unsaved fixture tab");
        focused.Activate(source_id);
        Check(memory.CreateFunded(focused, imported, reason) == created_id
            && focused.Active()->transaction.note.draft() == "Unsaved inspect notes",
            "Create focuses the exact existing PSBT at capacity without losing edits");

        auto winner = state.documents;
        winner.Close(std::array{created_id});
        winner.Activate(source_id);
        const auto path = metadata_path.parent_path() / "create-cas.sqlite";
        app::RecoverySession writer(path), stale(path);
        app::DocumentRegistry conflicted;
        Check(writer.Enable(winner) && stale.Start() && stale.Restore(conflicted), "Isolated CAS fixture");
        winner.Active()->Current().note.SetDraft("Winning recovery");
        Check(writer.Checkpoint(winner), "Winning recovery revision acknowledged");
        Check(!stale.CreateFunded(conflicted, imported, reason) && stale.Error()=="metadata-conflict"
            && conflicted.ActiveId()==source_id && conflicted.Documents().size()==1
            && conflicted.Active()->draft_revision==source_revision, "Create CAS refusal leaves the draft in place");
    }
    void FeeFrame(platform::NativeHost& host, const platform::NativeFrame& frame)
    {
        auto* document=state.documents.Active();
        switch (stage) {
        case 0: {
            const auto view=requests.refresh.View();
            if (!document || !view.chain_current || !view.ledger || !view.ledger->InventoryAvailable()) return;
            const auto selected=app::ParseCoinKey(destination);
            const auto* ledger=view.ledger;
            Check(selected && std::ranges::any_of(ledger->Coins(),[&](const auto& coin) {
                return coin.outpoint==*selected && coin.confirmations==0;
            }), "Exact unconfirmed fixture output observed");
            document->selected_coins={*selected};
            ++document->draft_revision.value;
            Click(host,facts.navigation[size_t(app::Section::Send)]);
            break;
        }
        case 1: Click(host,facts.fee.at("open")); break;
        case 2: Click(host,facts.fee.at("target")); break;
        case 3:
            if (document->funding_target!=6) return;
            if (!EnterField("3","Target blocks")) return;
            break;
        case 4:
            if (document->funding_target!=3) return;
            Click(host,facts.fee.at("read"));
            break;
        case 5: {
            if (!state.fees.target) return;
            const auto& value=*state.fees.target;
            Check(value.error.empty() && value.quote.blocks && (value.quote.rate || !value.quote.error.empty()),
                "Roots estimate returns actual target and rate or error");
            Check(value.policy && value.policy->relay && value.policy->mempool
                && *value.policy->mempool>=*value.policy->relay,"Actual node fee policy has separate floors");
            const auto snapshot=state.presentation.Snapshot();
            Check(snapshot.find("estimatesmartfee (BTC/kvB)")!=std::string::npos
                && snapshot.find("Requested 3 blocks")!=std::string::npos
                && snapshot.find("No provider is configured")!=std::string::npos,"Source, units and next-block availability");
            Capture(frame,"fees-target");
            Click(host,facts.fee.at("mempool-tab"));
            break;
        }
        case 6:
            if (state.fees.tab!=FeeRead::Mempool) return;
            Click(host,facts.fee.at("read"));
            break;
        case 7: {
            if (!state.fees.mempool) return;
            const auto& value=*state.fees.mempool;
            Check(value.mempool && value.mempool->ancestors && value.mempool->ancestors->transactions==1,
                "Selected ancestor package is complete and independently observed");
            const auto& bands=value.mempool->bands.vbytes;
            Check(std::accumulate(bands.begin(),bands.end(),uint64_t{})>0
                && facts.fee.at("two-hours").disabled && facts.fee.at("day").disabled,
                "Actual mempool vbytes and insufficient longer history");
            Check(state.presentation.Snapshot().find("Selected ancestor package:")!=std::string::npos,
                "Accessible package ratio and fee-band values");
            Capture(frame,"fees-mempool");
            Click(host,facts.fee.at("blocks-tab"));
            break;
        }
        case 8:
            if (state.fees.tab!=FeeRead::Recent) return;
            Click(host,facts.fee.at("read"));
            break;
        case 9: {
            if (!state.fees.recent) return;
            const auto& value=*state.fees.recent;
            Check(value.recent.size()==6 && value.recent.front().hash==requests.refresh.View().node->tip,
                "Bound to the current six-block chain");
            Press("Earlier block");
            break;
        }
        case 10:
            if (state.fees.block!=1) return;
            Check(state.presentation.Snapshot().find("Selected block hash")!=std::string::npos,
                "Keyboard-equivalent selection exposes exact text");
            Capture(frame,"fees-recent");
            requests.connection.SetOffline(true);
            break;
        case 11:
            Check(facts.fee.at("read").disabled && state.presentation.Snapshot().find("Recent block summaries: Unknown")!=std::string::npos
                && state.presentation.Snapshot().find("Selected block hash")==std::string::npos,
                "Offline retirement suppresses stale provider state");
            Check(!requests.funding.Busy() && !state.funding.preview,"Fee inspection creates no funding or draft preview");
            host.RequestClose();
            break;
        }
        std::cerr << "Completed native fee stage " << stage << '\n';
        ++stage;
    }
    void ReviewFrame(platform::NativeHost& host,const platform::NativeFrame& frame)
    {
        const auto* doc=state.documents.Active();
        const auto has=[&](std::string_view text){return state.presentation.Snapshot().find(text)!=std::string::npos;};
        const auto denied=[&] {
            const auto reply=shell::DispatchSigningCommand(state,requests,{app::Command::SignTransaction,created_id});
            Check(!reply.accepted,"no signing implementation is dispatched");
            return reply.reason;
        };
        switch (stage) {
        case 7:
            if (!doc || !doc->imported) return;
            created_id=doc->id;
            Check(doc->imported->parsed->source==accepted && app::ValidCreatedFunding(*doc->imported)
                && doc->imported->funding->draft_revision==source_revision,"current exact Create proof and original bytes");
            Check(!shell::CurrentSigningApproval(state,requests,created_id)
                && denied().find("matching current review and explicit approval")!=std::string::npos,
                "direct sign refuses just-created transaction");
            Click(host,facts.transaction_document.at("review-open"));
            break;
        case 8:
            if (!facts.transaction_document.contains("review-record")) return;
            Check(has("Signing wallet: funding & exact wallet") && has("Requested funding: 2.125 sat/vB")
                && has("Final fee rate: Unknown") && has("Proved intended change") && has(destination)
                && !facts.transaction_document.at("review-record").disabled
                && facts.transaction_document.at("review-approve").disabled,
                "proved change and wallet, exact recipient, qualified final fee rate, separate unreviewed controls");
            Press("Record signing review");
            break;
        case 9:
            if (!has("Review recorded. Approve separately.")) return;
            Check(!shell::CurrentSigningApproval(state,requests,created_id)
                && denied().find("matching current review and explicit approval")!=std::string::npos,
                "review alone grants no signing approval");
            Press("Approve for signing");
            break;
        case 10: {
            if (!has("Approved for this exact transaction.")) return;
            const auto* approval=shell::CurrentSigningApproval(state,requests,created_id);
            Check(approval && approval->commitment==protocol::TransactionCommitment(*doc->imported->parsed)
                && approval->original_source==app::SigningDigest(accepted)
                && approval->reviewed.origin==source_id && approval->reviewed.origin_revision==source_revision
                && approval->reviewed.proof.change && approval->reviewed.proof.recipients.size()==1
                && approval->reviewed.proof.recipients[0].value==Money::Satoshis(12345678),
                "approval records exact source, commitment and independent provenance");
            if (!sign) Check(denied()==app::MissingDependency(app::Command::SignTransaction),"matching approval still has no signer");
            const auto& recorded=approval->reviewed;
            std::ofstream bound(output/"approval-provenance.json");
            bound<<"{\"commitment\":"<<rpc::Quote(approval->commitment.hex())<<",\"originalSourceSha256\":"
                <<rpc::Quote(approval->original_source.hex())<<",\"proofSha256\":"<<rpc::Quote(recorded.proof_digest.hex())
                <<",\"wallet\":"<<rpc::Quote(recorded.wallet.DaemonWallet())<<",\"genesis\":"
                <<rpc::Quote(recorded.wallet.network.genesis.hex())<<",\"sourceDocument\":"<<recorded.origin
                <<",\"sourceRevision\":"<<recorded.origin_revision.value<<",\"reviewDocument\":"<<recorded.document
                <<",\"session\":"<<recorded.session<<",\"lockEpoch\":"<<recorded.epoch.value
                <<",\"feeSats\":"<<recorded.proof.fee.sats()<<",\"recipientSats\":"<<recorded.proof.recipients[0].value.sats()
                <<",\"intendedChangeProved\":true}\n";
            Check(bool(bound),"retained approval provenance");
            if (sign) {
                app::Request injection{app::Command::SignTransaction,created_id};
                injection.value="injected replacement";
                Check(!shell::DispatchSigningCommand(state,requests,injection).accepted
                    && !requests.signer.Busy(),"direct signing rejects caller-supplied PSBT bytes");
                Press("Enrich PSBT");
            } else if (edit) {
                first_review=approval->commitment;
                app::Request injection{app::Command::ApplyTransactionEdit,created_id};
                injection.edit=app::TransactionEditRequest{1,doc->draft_revision,{.locktime=900000}};
                Check(!shell::TransactionEditAvailability(state,requests,injection).available
                    && !shell::DispatchTransactionEdit(state,requests,injection).accepted,"approved document refuses direct edit");
                Click(host,facts.transaction_document.at("edit-open"));
            } else {
                Capture(frame,"transaction-review");
                Press("Advanced sighash");
            }
            break;
        }
        case 11: {
            if (!has("Advanced sighash commitments")) return;
            Check(has("NONE commits to no outputs") && has("SINGLE commits only") && has("ANYONECANPAY commits")
                && has("Taproot DEFAULT commits like ALL"),"concrete signature commitments and unsupported paths");
            Capture(frame,"advanced-sighash");
            const auto session=app::EncodePublicSession({state.documents,{}});
            const auto restored=session ? app::DecodePublicSession(*session) : std::nullopt;
            Check(restored && restored->documents.Active()->imported->parsed->source==accepted,"public source survives recovery");
            shell::State recreated;
            recreated.documents=restored->documents;
            Check(!shell::CurrentSigningApproval(recreated,requests,created_id)
                && shell::DispatchSigningCommand(recreated,requests,{app::Command::SignTransaction,created_id}).reason.find(
                    "matching current review and explicit approval")!=std::string::npos,"public recovery restores no approval");
            host.RequestClose();
            break;
        }
        }
        std::cerr<<"Completed native review stage "<<stage<<'\n';
        ++stage;
    }

    void SignFrame(platform::NativeHost& host,const platform::NativeFrame& frame)
    {
        auto& view=state.signing_views.at(created_id);
        switch (stage) {
        case 11:
            if (view.daemon_status.starts_with("Waiting")) return;
            if (!view.daemon_enriched) throw std::runtime_error("Enrichment result: "+view.daemon_status);
            Check(protocol::CompareTransactionReview(*state.documents.Active()->imported->parsed,*view.daemon_enriched).empty()
                && !view.daemon_enriched->input_policy[0].signature_fields,
                "Explicit enrichment retains exact review and adds no signatures");
            Press("Sign with daemon");
            break;
        case 12:
            if (view.daemon_status.starts_with("Waiting") || view.daemon_status.starts_with("Enriched")) return;
            Check(view.daemon_status.find("wallet is locked")!=std::string::npos,"Locked wallet refuses sign");
            unlock_revision=state.documents.Active()->draft_revision;
            unlock_action=Press("Unlock daemon wallet");
            break;
        case 13:
        {
            if (!facts.transaction_document.contains("daemon-wallet-passphrase")) {
                const auto receipts=state.presentation.exchange.Receipts();
                const auto receipt=std::ranges::find(receipts,unlock_action,&c::SemanticReceipt::sequence);
                if (receipt!=receipts.end() && !state.daemon_unlock.owner)
                    throw std::runtime_error("Unlock action receipt: "+std::to_string(int(receipt->result))
                        +" / "+state.notice+" / "+std::string(state.signing_reviews.Reason(created_id))
                        +" / current approval "+std::to_string(bool(shell::CurrentSigningApproval(state,requests,created_id))));
                return;
            }
            const std::string_view entered(state.daemon_unlock.passphrase.data());
            const auto& field=facts.transaction_document.at("daemon-wallet-passphrase");
            if(!field.activated && !unlock_input_clicked){
                if(!field.visible || field.rect[3]>=facts.layout.status.y){
                    if(frame.rendered_frames>last_edit_diagnostic+8){
                        last_edit_diagnostic=frame.rendered_frames;
                        int x{},y{};
                        glfwGetWindowPos(host.Window(),&x,&y);
                        const auto& pane=facts.layout.content;
                        const auto wheel=field.rect[1]<pane.y?4U:5U;
                        Check(motion(display,-1,int(x+pane.x+pane.w/2),int(y+pane.y+pane.h/2),0)
                                && button(display,wheel,True,0) && button(display,wheel,False,0),
                            "native scroll to protected input");
                        XSync(display,False);
                    }
                    return;
                }
                Click(host,field);
                unlock_input_clicked=true;
                return;
            }
            if (!field.activated || !glfwGetWindowAttrib(host.Window(),GLFW_FOCUSED)) {
                if (frame.rendered_frames>last_edit_diagnostic+15) {
                    Capture(frame,"daemon-unlock-input-stalled");
                    throw std::runtime_error("Native focused passphrase input is inactive");
                }
                return;
            }
            if(unlock_key_pressed){
                Check(key(display,unlock_key_pressed,False,0),"Native protected key release");
                unlock_key_pressed=0;
                XSync(display,False);
                return;
            }
            if(entered.size()!=unlock_characters_sent){
                if(frame.rendered_frames>last_edit_diagnostic+120){
                    last_edit_diagnostic=frame.rendered_frames;
                    std::cerr<<"Protected editor progress: "<<entered.size()<<" acknowledged of "
                             <<unlock_characters_sent<<" events; focused="<<field.focused<<'\n';
                }
                return;
            }
            if(entered.size()<FIXTURE_PASSPHRASE.size()){
                const char name[]{FIXTURE_PASSPHRASE[unlock_characters_sent++],'\0'};
                const auto code=XKeysymToKeycode(display,XStringToKeysym(name));
                Check(code && key(display,code,True,0),"Native protected keyboard input");
                unlock_key_pressed=code;
                XSync(display,False);
                return;
            }
            break;
        }
        case 14: {
            const std::string_view entered(state.daemon_unlock.passphrase.data());
            // The pinned backend trickles queued down/up events across frames.
            if (entered.size()<FIXTURE_PASSPHRASE.size()) return;
            const auto& submit=facts.transaction_document.at("submit-daemon-unlock");
            if (!submit.visible || submit.rect[3]>=facts.layout.status.y) {
                int x{},y{};
                glfwGetWindowPos(host.Window(),&x,&y);
                Check(motion(display,-1,x+facts.layout.content.x+facts.layout.content.w/2,
                             y+facts.layout.content.y+facts.layout.content.h/2,0)
                        && button(display,5,True,0) && button(display,5,False,0),
                    "native scroll reveals the separate daemon unlock confirmation");
                XSync(display,False);
                return;
            }
            Check(entered==FIXTURE_PASSPHRASE,"Complete native protected edit delivered before submit");
            Check(state.documents.Active()->draft_revision==unlock_revision
                && shell::CurrentSigningApproval(state,requests,created_id),
                "Revealing protected input retains the exact approved draft revision");
            const auto packet=state.presentation.exchange.Published();
            const auto field=std::ranges::find_if(packet->nodes,[](const auto& node){return node.name=="Wallet passphrase";});
            Check(field!=packet->nodes.end() && field->value.empty()
                && !c::ax::Supports(field->actions,c::ax::SemanticAction::SetValue)
                && state.presentation.Snapshot().find(FIXTURE_PASSPHRASE)==std::string::npos,
                "Masked passphrase has no semantic value or injection route");
            Capture(frame,"daemon-unlock");
            Click(host,submit);
            break;
        }
        case 15:
            if (state.daemon_unlock.owner || view.daemon_status.starts_with("Waiting")) return;
            if (!view.daemon_status.starts_with("The daemon accepted")) throw std::runtime_error("Unlock result: "+view.daemon_status);
            Check(!view.daemon_signed && std::ranges::all_of(state.daemon_unlock.passphrase,[](char c){return !c;}),
                "Unlock ends with cleared input and does not sign");
            Press("Sign with daemon");
            break;
        case 16: {
            if (view.daemon_status.starts_with("Waiting") || view.daemon_status.starts_with("The daemon accepted")) return;
            if (!view.daemon_signed) throw std::runtime_error("Signature result: "+view.daemon_status);
            const auto* approval=shell::CurrentSigningApproval(state,requests,created_id);
            const auto original=state.documents.Active()->imported->parsed;
            Check(approval && original->source==accepted && view.daemon_signed->input_policy[0].signature_fields
                && !view.daemon_signed->input_policy[0].final_fields
                && protocol::TransactionCommitment(*view.daemon_signed)==approval->commitment,
                "Exact approved original and separately verified unfinalized signed PSBT");
            const auto public_data=app::EncodePublicSession({state.documents,{}});
            const auto restored=public_data ? app::DecodePublicSession(*public_data) : std::nullopt;
            shell::State recreated;
            Check(restored && restored->documents.Active()->imported->parsed->source==accepted
                && public_data->find(FIXTURE_PASSPHRASE)==std::string::npos,"Public recovery retains exact original and no secret");
            recreated.documents=restored->documents;
            Check(!shell::DispatchSigningCommand(recreated,requests,{app::Command::SignTransaction,created_id}).accepted,
                "Restart does not restore signing authority");
            std::ofstream result(output/"signing-provenance.json");
            result<<"{\"source\":"<<rpc::Quote(original->source)<<",\"signedPsbt\":"<<rpc::Quote(view.daemon_signed->source)
                <<",\"commitment\":"<<rpc::Quote(approval->commitment.hex())<<"}\n";
            Check(bool(result),"Exact signed PSBT evidence retained");
            std::ofstream semantics(output/"daemon-signed.semantics.txt");
            semantics<<state.presentation.Snapshot();
            Press("Open verified signed PSBT");
            break;
        }
        case 17:
            if (state.documents.ActiveId()==created_id) return;
            Check(state.documents.Active()->imported->parsed==view.daemon_signed
                && state.documents.Find(created_id)->imported->parsed->source==accepted,"Explicit signed copy opens without replacing original");
            host.RequestClose();
            break;
        }
        std::cerr<<"Completed native signing stage "<<stage<<'\n';
        ++stage;
    }

    void EditFrame(platform::NativeHost& host,const platform::NativeFrame& frame)
    {
        const auto* doc=state.documents.Active();
        const auto old=state.documents.Find(created_id)->imported->parsed;
        const auto has=[&](std::string_view text){return state.presentation.Snapshot().find(text)!=std::string::npos;};
        const auto deny=[&](uint64_t id){
            return shell::DispatchSigningCommand(state,requests,{app::Command::SignTransaction,id});
        };
        switch (stage) {
        case 11:
            if (!has("Prior signing approval or review was retired.")) return;
            Check(!shell::CurrentSigningApproval(state,requests,created_id) && !deny(created_id).accepted,
                "explicit return to editing revokes the exact earlier approval");
            if (!EnterField("900000","Transaction locktime")) return;
            break;
        case 12:
            if (!has("Block height 900000")) return;
            Check(has("chain maturity is Unknown"),"future locktime qualification");
            Press("Apply fields");
            break;
        case 13:
            if (doc->id==created_id) return;
            working_id=doc->id;
            Check(doc->imported->parsed->transaction->locktime==900000 && doc->imported->parsed!=old
                && old->source==accepted && !doc->imported->funding,"recovery-ready working copy retains exact original");
            Click(host,facts.transaction_document.at("edit-outputs"));
            break;
        case 14:
            if (!has("Output 1 of 2")) return;
            Check(key && key(display,XKeysymToKeycode(display,XK_Control_L),True,0)
                && key(display,XKeysymToKeycode(display,XK_Down),True,0),"native Ctrl+Down output reorder");
            XSync(display,False);
            key_release=true;
            break;
        case 15:
            if (!has("Output 2 of 2")) return;
            Check(doc->id==working_id && doc->imported->parsed->transaction->outputs.front()==old->transaction->outputs.back()
                && doc->imported->parsed->transaction->outputs.back()==old->transaction->outputs.front(),
                "native reorder preserves amounts and moves the original change row");
            Press("Back to inspection");
            break;
        case 16:
            if (state.transaction_edits.contains(working_id)) return;
            Check(!shell::CurrentSigningApproval(state,requests,working_id)
                && deny(working_id).reason.find("matching current review and explicit approval")!=std::string::npos,
                "the edited working document cannot inherit approval");
            Click(host,facts.transaction_document.at("review-open"));
            break;
        case 17:
            if (!has("This working revision needs its own review and approval.")) return;
            Check(has("Proved intended change") && has("Locktime 900000") && has("chain maturity Unknown")
                && facts.transaction_document.at("review-approve").disabled,"exact moved payment proof with renewed review required");
            Capture(frame,"review-invalidated");
            Press("Record signing review");
            break;
        case 18:
            if (!has("Review recorded. Approve separately.")) return;
            Check(!deny(working_id).accepted,"new review alone still denies sign");
            Press("Approve for signing");
            break;
        case 19: {
            if (!has("Approved for this exact transaction.")) return;
            const auto* approval=shell::CurrentSigningApproval(state,requests,working_id);
            Check(approval && approval->commitment!=first_review && approval->reviewed.change_output==0
                && approval->reviewed.proof.recipients.size()==1 && old->transaction->locktime==0
                && !deny(working_id).accepted,"new approval binds working bytes and moved proved change");
            std::ofstream record(output/"edit-provenance.json");
            record<<"{\"originalId\":"<<created_id<<",\"workingId\":"<<working_id
                <<",\"workingRevision\":"<<doc->draft_revision.value<<",\"oldCommitment\":"<<rpc::Quote(first_review->hex())
                <<",\"newCommitment\":"<<rpc::Quote(approval->commitment.hex())<<",\"originalSource\":"<<rpc::Quote(old->source)
                <<",\"workingSource\":"<<rpc::Quote(doc->imported->parsed->source)<<",\"locktime\":900000,\"changeIndex\":0}\n";
            Check(bool(record),"retained edit provenance");
            const auto session=app::EncodePublicSession({state.documents,{}});
            const auto restored=session ? app::DecodePublicSession(*session) : std::nullopt;
            Check(restored && restored->documents.Find(created_id)->imported->parsed->source==accepted
                && restored->documents.Find(working_id)->imported->parsed->source==doc->imported->parsed->source,
                "public recovery preserves independent original and working bytes");
            host.RequestClose();
            break;
        }
        }
        std::cerr<<"Completed native edit stage "<<stage<<'\n';
        ++stage;
    }

    void BatchFrame(platform::NativeHost& host,const platform::NativeFrame& frame)
    {
        const auto* draft=state.documents.Active();
        const auto files=metadata_path.parent_path();
        const auto has=[&](std::string_view text){return state.presentation.Snapshot().find(text)!=std::string::npos;};
        switch (stage) {
        case 0:
            if (!draft || !requests.refresh.View().chain_current) return;
            source_revision=draft->draft_revision;
            Click(host,facts.navigation[size_t(app::Section::Send)]);
            break;
        case 1:
            Click(host,facts.batch.at("open"));
            break;
        case 2:
            if (!state.batch || !facts.batch.contains("files")) return;
            Click(host,facts.batch.at("files"));
            break;
        case 3:
            if (!facts.batch.contains("csv-path") || !EnterField((files/"batch-in.csv").string(),"Local CSV path")) return;
            break;
        case 4:
            if (facts.batch.at("load").disabled) return;
            Press("Load CSV");
            break;
        case 5:
            if (!has("CSV staged. Inspect rows before applying.")) return;
            Check(state.batch && state.batch->checked && state.batch->checked->fixed==Money::Satoshis(62000000)
                && state.batch->work.rows.size()==2 && draft->recipients.rows[0].address.empty()
                && draft->draft_revision==source_revision && !requests.funding.Busy(),"CSV staging leaves live payments and funding intact");
            if (!EnterField((files/"batch-out.csv").string(),"Local CSV path")) return;
            break;
        case 6:
            if (facts.batch.at("export").disabled) return;
            Press("Export CSV");
            break;
        case 7:
            if (!has("CSV saved as a new local file.")) return;
            Check(std::filesystem::is_regular_file(files/"batch-out.csv"),"Actual exclusive local CSV export");
            Press("Hide CSV files");
            break;
        case 8:
            if (state.batch->file_panel) return;
            Check(has("Fixed total 0.62000000 BTC") && has("Row 2 label"),"Exact grid values and keyboard-accessible row identity");
            Capture(frame,"send-to-many");
            Click(host,facts.batch.at("apply"));
            break;
        case 9:
            if (state.batch) return;
            Check(draft->recipients.rows.size()==2 && draft->draft_revision.value==source_revision.value+1
                && draft->recipients.rows[0].address==destination
                && draft->recipients.rows[0].label=="München, 支付"
                && draft->recipients.rows[1].label=="  =1+2"
                && draft->recipients.rows[1].unit==RecipientUnit::Sats
                && !state.funding.preview && !requests.funding.Busy(),"Native Apply publishes exact ordered recipients once without funding");
            Click(host,facts.funding.at("toggle"));
            break;
        case 10:
            if (!EnterField("2.125","Fee rate (sat/vB)")) return;
            break;
        case 11:
            Click(host,facts.funding.at("preview"));
            break;
        case 12:
            if (!state.funding.preview) {
                if (!requests.funding.Busy()) throw std::runtime_error("Batch funding preview unavailable: "+state.funding.status);
                return;
            }
            Check(state.funding.preview->flow && state.funding.preview->flow->total_recipients==Money::Satoshis(62000000),
                "Independent unsigned preview matches the applied total");
            Click(host,facts.batch.at("analysis"));
            break;
        case 13:
            if (!state.send_analysis) return;
            Check(has("Current verified unsigned preview") && has("1 of 2 recipient scripts match earlier outputs")
                && has("2 inputs from 2 previous transactions") && has("P2PKH") && has("P2WPKH"),
                "Wallet-script observation, exact input linkage and named script mix");
            Capture(frame,"transaction-analysis");
            Click(host,facts.batch.at("analysis-back"));
            break;
        case 14:
            if (state.send_analysis) return;
            Check(draft->recipients.rows.size()==2 && state.documents.Documents().size()==1
                && state.funding.preview && !requests.funding.Busy(),"Analysis leaves preview and documents intact");
            host.RequestClose();
            break;
        }
        std::cerr<<"Completed native batch stage "<<stage<<'\n';
        ++stage;
    }

    void FlowFrame(platform::NativeHost& host,const platform::NativeFrame& frame)
    {
        auto* draft=state.documents.Active();
        const auto snapshot=state.funding.preview;
        switch (stage) {
        case 6:
            if (!snapshot) {
                if (!requests.funding.Busy()) throw std::runtime_error("Small flow unavailable: "+state.funding.status);
                return;
            }
            Check(snapshot->flow && snapshot->flow->total_recipients==Money::Satoshis(12345678)
                && snapshot->flow->outputs.size()==3 && snapshot->change_choices.size()>1,"Exact small flow and eligible alternatives");
            flow_revision=draft->draft_revision;
            Press("Inspect transaction flow");
            break;
        case 7:
            if (!facts.flow.contains("diagram-output-0")) return;
            if (const auto& item=facts.flow.at("diagram-output-0"); !item.visible || item.rect[2]>960 || item.rect[3]>640)
                Capture(frame,"flow-layout-diagnosis");
            Click(host,facts.flow.at("diagram-output-0"));
            break;
        case 8:
            if (state.funding.flow.selected!=FlowKey{false,0}) return;
            Check(state.presentation.Snapshot().find(destination)!=std::string::npos
                && facts.flow.at("qr").disabled,"Full verified address and honest QR capability");
            Capture(frame,"transaction-flow-small");
            Press("Copy address");
            break;
        case 9: {
            if (!host.InputState().clipboard_writes) return;
            Check(host.InputState().clipboard_writes==1 && XGetSelectionOwner(display,XInternAtom(display,"CLIPBOARD",False))!=0,
                "Native clipboard acknowledged and owns the selection");
            Click(host,facts.flow.at("diagram-output-1"));
            break;
        }
        case 10:
            if (!facts.flow.contains("change-choices")) return;
            Press("Choose change");
            break;
        case 11:
            if (!facts.flow.contains("change-next")) return;
            Press("Later choice");
            break;
        case 12:
            if (state.funding.flow.change_index!=1) return;
            chosen_change=snapshot->change_choices.at(1);
            Press("Use this change address");
            break;
        case 13:
            if (snapshot || state.funding.flow.open) return;
            Check(draft && draft->funding_change==chosen_change && draft->draft_revision!=flow_revision
                && !requests.funding.Busy(),"Change preference retires the complete preview without dispatch");
            draft->recipients={}; // Seed additional distinct public destinations from the owned node.
            for (size_t i=0;i<flow_destinations.size();++i) {
                if (i) Check(draft->recipients.Add(draft->draft_revision),"Large draft fixture row");
                Check(draft->recipients.ApplyDestination(i+1,"bitcoin:"+flow_destinations[i]+"?amount=0.01",
                    protocol::DisplayNetwork::Regtest,draft->draft_revision),"Large draft fixture destination");
            }
            break;
        case 14: Press("Preview funding"); break;
        case 15:
            if (!snapshot) {
                if (!requests.funding.Busy()) throw std::runtime_error("Large flow unavailable: "+state.funding.status);
                return;
            }
            Check(snapshot->change==chosen_change && snapshot->flow->outputs.size()==flow_destinations.size()+2
                && snapshot->flow->total_recipients==Money::Satoshis(8000000),"Fresh funding honors exact chosen change and recipients");
            Press("Inspect transaction flow");
            break;
        case 16:
            if (!facts.flow.contains("output-next")) return;
            Click(host,facts.flow.at("output-next"));
            break;
        case 17:
            if (!facts.flow.contains("diagram-output-5")) return;
            Check(!facts.flow.contains("diagram-output-0"),"Only the selected compact group is shown");
            Click(host,facts.flow.at("diagram-output-5"));
            break;
        case 18:
            if (state.funding.flow.selected!=FlowKey{false,5}) return;
            Check(state.presentation.Snapshot().find(flow_destinations.at(5))!=std::string::npos,
                "Exact individual identity survives grouping");
            Capture(frame,"transaction-flow-large");
            ClickMenu(host,"View");
            break;
        case 19:
            if (!facts.menu_rows.contains(app::Command::HideAmounts)) return;
            Click(host,facts.menu_rows.at(app::Command::HideAmounts));
            break;
        case 20: {
            if (!state.privacy.hidden) return;
            const auto text=state.presentation.Snapshot();
            Check(!snapshot && facts.flow.empty() && text.find("Transaction flow hidden")!=std::string::npos
                && text.find(flow_destinations.at(5))==std::string::npos && text.find("Verified totals")==std::string::npos,
                "Privacy retires flow, values and actionable semantics");
            Check(XGetSelectionOwner(display,XInternAtom(display,"CLIPBOARD",False))==0,"Sensitive flow clipboard retired");
            Capture(frame,"flow-hidden");
            host.RequestClose();
            break;
        }
        }
        std::cerr << "Completed native flow stage " << stage << '\n';
        ++stage;
    }
public:
    Observer(const std::filesystem::path& path, bool funding, bool create, bool fees, bool flow, bool batch, bool review, bool edit,bool sign)
        : output(path), create(create), fees(fees), flow(flow), batch(batch), review(review), edit(edit),sign(sign)
    {
        display = XOpenDisplay(nullptr);
        xtst = dlopen("libXtst.so.6", RTLD_NOW);
        Check(display && xtst, "Owned X11 input");
        motion = reinterpret_cast<decltype(motion)>(dlsym(xtst, "XTestFakeMotionEvent"));
        button = reinterpret_cast<decltype(button)>(dlsym(xtst, "XTestFakeButtonEvent"));
        key = reinterpret_cast<decltype(key)>(dlsym(xtst, "XTestFakeKeyEvent"));
        Check(motion && button, "XTest entry points");
        const auto binding = app::WalletBinding{*BackendSessionId::Make("send-fixture"), *WalletId::Make("Personal"),
            {*Hash256::Parse("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f")}, {}};
        state.documents.OpenWallet("Personal", binding, true);
        state.documents.Active()->history.enabled = true;
        requests.funding_enabled = funding;
        requests.fees_enabled = fees;
        requests.local_enabled = batch;
        requests.signer_enabled=sign;
    }
    void Connect(ServerCandidate profile, const std::filesystem::path& metadata, std::string address)
    {
        requests.funding_enabled = requests.live = true;
        metadata_path = metadata;
        requests.recovery = std::make_unique<app::RecoverySession>(metadata);
        Check(requests.recovery->Start(), "Private fixture metadata");
        Check(requests.connection.SetDraft(std::move(profile)) && requests.connection.Test(), "Explicit owned connection");
        destination = std::move(address);
        if (flow) {
            std::string_view csv=destination;
            while (!csv.empty()) {
                const auto end=csv.find(',');
                flow_destinations.emplace_back(csv.substr(0,end));
                if (end==std::string_view::npos) break;
                csv.remove_prefix(end+1);
            }
            Check(flow_destinations.size()==8,"Owned large destination fixture");
            destination=flow_destinations.front();
        }
        state.documents = {};
    }
    ~Observer() override
    {
        if (display) XCloseDisplay(display);
        if (xtst) dlclose(xtst);
    }
    Requests requests;
    shell::Facts facts;
    shell::State state;

    bool CaptureFramebuffer() const override { return true; }
    void Frame(platform::NativeHost& host, const platform::NativeFrame& frame) override
    {
        if (frame.rendered_frames < 3) return;
        if (key_release) {
            Check(key(display,XKeysymToKeycode(display,XK_Down),False,0)
                && key(display,XKeysymToKeycode(display,XK_Control_L),False,0),"native keyboard release");
            XSync(display,False);
            key_release=false;
            return;
        }
        if (release) {
            Check(button(display, 1, False, 0), "Native mouse release");
            XSync(display, False);
            release = false;
            return;
        }
        const auto* document = state.documents.Active();
        // A staged edit or mouse transaction can change the owner scope after
        // controls are submitted. Only act on the next coherent rendered frame.
        const auto packet = state.presentation.exchange.Published();
        if (stage > 0 && (!packet || !packet->valid)) return;
        if (fees) { FeeFrame(host,frame); return; }
        if (batch) { BatchFrame(host,frame); return; }
        if (edit && stage>=11) { EditFrame(host,frame); return; }
        if (sign && stage>=11) { SignFrame(host,frame); return; }
        if (review && stage>=7) { ReviewFrame(host,frame); return; }
        if (flow && stage>=6) { FlowFrame(host,frame); return; }
        if (requests.live) {
            if (stage == 0) {
                if (!document || !requests.refresh.View().chain_current) return;
                if (create) Check(requests.recovery->Enable(state.documents), "Explicit durable public recovery");
                Click(host, facts.navigation[size_t(app::Section::Send)]);
            } else if (stage == 1) {
                if (!EnterField("bitcoin:" + destination + "?amount=0.12345678&label=Local%20invoice")) return;
            } else if (stage == 2) Click(host, facts.send.at("apply"));
            else if (stage == 3) Click(host, facts.funding.at("toggle"));
            else if (stage == 4) {
                if (!EnterField("2.125", "Fee rate (sat/vB)")) return;
            } else if (stage == 5) Click(host, facts.funding.at("preview"));
            else if (stage == 6) {
                if (!state.funding.preview) {
                    if (!requests.funding.Busy())
                        throw std::runtime_error("Connected funding preview did not arrive: " + state.funding.status);
                    return;
                }
                const auto& preview = *state.funding.preview;
                const auto& outputs = preview.psbt->transaction->outputs;
                Check(outputs.front().value == Money::Satoshis(12345678) && preview.fee.sats() > 0 && preview.change,
                    "Exact connected preview results");
                Check(preview.total_inputs.sats() == outputs.front().value.sats() + outputs.back().value.sats()
                    + preview.fee.sats(), "Connected results reconcile");
                Check(state.presentation.Snapshot().find("Unsigned preview verified") != std::string::npos,
                    "Verified total is visible in the current semantics");
                if (create) {
                    accepted = preview.psbt->source;
                    source_id = document->id;
                    source_revision = document->draft_revision;
                    Press("Create Transaction");
                } else {
                    Capture(frame, "funding-preview");
                    host.RequestClose();
                }
            } else if (stage == 7) {
                if (!document || !document->imported) {
                    if (!requests.funding.Busy()) throw std::runtime_error("Create did not finish: " + state.funding.status);
                    return;
                }
                created_id = document->id;
                const auto& imported = *document->imported;
                Check(created_id != source_id && imported.parsed->source == accepted
                    && imported.funding && app::ValidCreatedFunding(imported), "Inspectable unchanged source and independent proof");
                Check(state.documents.Find(source_id)->draft_revision == source_revision
                    && imported.funding->draft_revision == source_revision, "Draft revision retained through Create");
                Check(state.presentation.Snapshot().find("Created unsigned transaction") != std::string::npos,
                    "Native inspection identifies unsigned origin and proofs");
                const auto encoded = app::EncodePublicSession({state.documents,{}});
                const auto decoded = encoded ? app::DecodePublicSession(*encoded) : std::nullopt;
                Check(decoded && decoded->documents.Active()->imported->parsed->source == accepted
                    && app::ValidCreatedFunding(*decoded->documents.Active()->imported)
                    && decoded->documents.Active()->observation == app::ObservationState::Offline,
                    "Exact unsigned source and proofs recover offline");
                AdmissionRegressions(imported);
                Capture(frame, "send-created");
            } else if (stage == 8) Click(host, facts.tabs.at(source_id));
            else if (stage == 9) Click(host, facts.funding.at("clear"));
            else if (stage == 10) {
                Check(facts.document_dialog_visible && state.presentation.Snapshot().find("Clear this Send draft?") != std::string::npos,
                    "Native confirmation explains the recoverable change");
                Capture(frame, "send-clear-confirm");
                Click(host, facts.document_actions.at("cancel"));
            } else if (stage == 11) {
                Check(!facts.document_dialog_visible && document->draft_revision == source_revision
                    && document->recipients.Find(1)->label == "Local invoice"
                    && !requests.recovery->HasClearedSend(), "Cancel preserves draft and recovery");
                Click(host, facts.funding.at("clear"));
            } else if (stage == 12) Click(host, facts.document_actions.at("confirm-clear-send"));
            else if (stage == 13) {
                Check(!document->recipients.Dirty() && document->funding_rate.empty() && document->selected_coins.empty()
                    && document->excluded_coins.empty() && requests.recovery->HasClearedSend(), "Confirmed Clear retires Send state");
                Check(state.documents.Find(created_id)->imported->parsed->source == accepted,
                    "Clear preserves the separate transaction document");
                ClickMenu(host, "File");
            } else if (stage == 14) {
                if (!facts.menu_rows.contains(app::Command::Recovery)) return;
                Click(host, facts.menu_rows.at(app::Command::Recovery));
            } else if (stage == 15) {
                if (!facts.document_actions.contains("recover-send")) return;
                Click(host, facts.document_actions.at("recover-send"));
            }
            else if (stage == 16) {
                if (state.recovery_dialog.open || requests.recovery->HasClearedSend()) return;
                Check(document->id == source_id && document->recipients.Find(1)->amount == "0.12345678"
                    && document->recipients.Find(1)->label == "Local invoice" && document->funding_rate == "2.125"
                    && document->draft_revision.value > source_revision.value, "Explicit recovery restores exact Send state at a new revision");
                Check(!state.funding.preview && !requests.funding.Busy() && !requests.recovery->HasClearedSend(),
                    "Recovery does not replay a funding or Create action");
                host.RequestClose();
            }
            if (create) std::cerr << "Completed native Create stage " << stage << '\n';
            ++stage;
            return;
        }
        if (requests.funding_enabled) {
            if (stage == 0) Click(host, facts.navigation[size_t(app::Section::Send)]);
            else if (stage == 1) Click(host, facts.funding.at("toggle"));
            else if (stage == 2) {
                Check(facts.funding.at("preview").disabled && !requests.funding.Busy(), "Offline funding remains disabled");
                Check(state.presentation.Snapshot().find("Funding is Unknown") != std::string::npos, "Honest funding availability");
                Capture(frame, "funding-unavailable");
                host.RequestClose();
            }
            ++stage;
            return;
        }
        switch (stage) {
        case 0:
            Click(host, facts.navigation[size_t(app::Section::Send)]);
            break;
        case 1:
            Check(document->section == app::Section::Send, "Native Send route");
            if (!EnterField("bitcoin:" + std::string(ADDRESS) + "?amount=0.00001234&label=Invoice%20%2342")) return;
            break;
        case 2:
            Check(document->recipients.Find(1)->address.empty(), "Staged URI has no payment authority");
            Click(host, facts.send.at("apply"));
            break;
        case 3: {
            const auto* row = document->recipients.Find(1);
            Check(row->address == ADDRESS && row->amount == "0.00001234" && row->label == "Invoice #42",
                "Atomic URI fields on a real activated control");
            Check(facts.send.at("qr").disabled && state.presentation.Snapshot().find("Eligible funds and fee are Unknown.") != std::string::npos,
                "Truthful QR and offline fundability");
            Capture(frame, "send-recipient");
            Click(host, facts.send.at("add"));
            break;
        }
        case 4:
            Check(document->recipients.selected == 2, "Native Add chooses a new recipient");
            if (!EnterField("bitcoin:" + std::string(ADDRESS) + "?req-unsupported=1&amount=2")) return;
            break;
        case 5:
            Click(host, facts.send.at("apply"));
            break;
        case 6: {
            const auto* row = document->recipients.Find(2);
            Check(row->address.empty() && row->amount.empty() && row->label.empty()
                && document->recipients.Find(1)->label == "Invoice #42", "Rejected URI leaves both recipients intact");
            Check(state.presentation.Snapshot().find("unsupported parameter") != std::string::npos,
                "Current recipient validation alert");
            Capture(frame, "send-errors");
            host.RequestClose();
            break;
        }
        }
        ++stage;
    }
    void Teardown(const platform::NativeTeardown& result) override
    {
        cleaned = result.renderer_cleared && result.platform_cleared && result.textures_released
            && result.imgui_cleared && result.context_cleared && result.accessibility_cleared && result.input_cleared;
        Check(cleaned, "Settled native renderer and input");
    }
    void Report()
    {
        const int expected_stage = sign ? 18 : edit ? 20 : review ? 12 : batch ? 15 : flow ? 21 : fees ? 12 : create ? 17 : requests.funding_enabled && !requests.live ? 3 : 7;
        const int expected_frames = sign || edit ? 1 : flow || fees ? 3 : batch || create || !requests.funding_enabled ? 2 : 1;
        Check(stage == expected_stage && frames == expected_frames && cleaned, "All intended native checks ran");
        std::ofstream report(output / "result.json");
        report << "{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":"
            << checks << ",\"frames\":" << frames << ",\"teardown\":true,\"connected\":"
            << (requests.live ? "true" : "false") << ",\"externalHostCommands\":0}\n";
        if (!report) throw std::runtime_error("Cannot retain native result");
    }
};
}

int main(int argc, char** argv)
{
    const bool edit = argc == 10 && std::string_view(argv[3]) == "--edit-live";
    const bool sign = argc == 10 && std::string_view(argv[3]) == "--sign-live";
    const bool review = sign || edit || (argc == 10 && std::string_view(argv[3]) == "--review-live");
    const bool create = review || (argc == 10 && std::string_view(argv[3]) == "--create-live");
    const bool fees = argc == 10 && std::string_view(argv[3]) == "--fees-live";
    const bool flow = argc == 10 && std::string_view(argv[3]) == "--flow-live";
    const bool batch = argc == 10 && std::string_view(argv[3]) == "--batch-live";
    const bool live = create || fees || flow || batch || (argc == 10 && std::string_view(argv[3]) == "--funding-live");
    if (argc != 3 && (argc != 4 || std::string_view(argv[3]) != "--funding") && !live) return 2;
    try {
        const auto* owned = std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        const auto* display = std::getenv("DISPLAY");
        if (!owned || !display || std::string_view(owned) != display) throw std::runtime_error("Owned display required");
        if (!live) for (const int family : {AF_INET, AF_INET6}) {
            const int fd = socket(family, SOCK_STREAM, 0);
            const bool denied = fd < 0 && errno == EPERM;
            if (fd >= 0) close(fd);
            if (!denied) throw std::runtime_error("Offline network fence required");
        }
        std::filesystem::create_directories(argv[1]);
        Observer observer(argv[1], argc != 3, create, fees, flow, batch, review, edit,sign);
        if (live) {
            ServerCandidate profile;
            const std::string_view port(argv[4]);
            const auto result = std::from_chars(port.data(), port.data()+port.size(), profile.connection.port);
            if (result.ec != std::errc{} || result.ptr != port.data()+port.size() || !profile.connection.port)
                throw std::runtime_error("Owned fixture port required");
            profile.connection.credential.reference = argv[5];
            profile.expected_genesis = argv[6];
            profile.wallet = argv[7];
            observer.Connect(std::move(profile), argv[8], argv[9]);
        }
        platform::NativeOptions options{960, 640, false, review ? 1.F : 1.5F, argv[2]};
        options.fixed_delta_seconds = 1.F / 60;
        options.accessibility = &observer.state.presentation.exchange;
        platform::NativeHost* active{};
        {
            platform::NativeHost host(options, &observer, [&](const auto& theme, const auto& appearance) {
                if (live) {
                    observer.requests.connection.Poll();
                    if (observer.requests.connection.CanApply()) observer.requests.connection.Apply();
                }
                observer.facts = platform::DrawShellFrame(*active, observer.state, observer.requests, theme, appearance);
            });
            active = &host;
            host.Run();
        }
        observer.Report();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
