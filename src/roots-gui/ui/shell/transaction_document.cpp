// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "app/broadcast.h"
#include "ui/theme/theme.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <sodium.h>

namespace roots::gui::ui::shell {
namespace {
namespace a=app;
namespace c=components;
using c::ax::SemanticAction;
using c::ax::SemanticRole;
using protocol::DisplayNetwork;
constexpr const char* DIALOG="Local transaction document###local-transaction-document";

bool Merging(LocalDocumentMode mode)
{
    return mode==LocalDocumentMode::Merge || mode==LocalDocumentMode::MergeText || mode==LocalDocumentMode::MergeQr;
}
bool ReadingQr(LocalDocumentMode mode)
{
    return mode==LocalDocumentMode::Qr || mode==LocalDocumentMode::MergeQr || mode==LocalDocumentMode::PaymentQr;
}
bool ShowingQr(LocalDocumentMode mode)
{
    return mode==LocalDocumentMode::ShowQr || mode==LocalDocumentMode::ShowFinalQr;
}
std::pair<uint64_t,uint64_t> QrContext(app::Requests& host)
{
    const auto* connection=host.Connection();
    return connection ? std::pair{connection->Session(),connection->Revision()} : std::pair<uint64_t,uint64_t>{};
}
bool FeeReplacement(const State& state,uint64_t id)
{
    const auto view=state.imported_views.find(id);
    const auto edit=state.edited_origins.find(id);
    return (view!=state.imported_views.end() && view->second.replacement)
        || (edit!=state.edited_origins.end() && edit->second.fee_replacement);
}
bool Permitted(const State& state)
{
    return !state.privacy.hidden && !state.semantic_hide_toggle && !state.lock_panel.setup
        && (!state.recovery || (!state.recovery->Locked() && !state.recovery->Pending()));
}
LocalDocumentContext Context(const State& state)
{
    const auto* active=state.documents.Active();
    return {state.local_document.context.generation,active ? active->id : 0,
        active ? active->draft_revision : DraftRevision{},state.recovery ? state.recovery->Gate().Epoch() : LockEpoch{}};
}
bool ExchangeReady(const State& state)
{
    const auto& panel=state.local_document;
    if (!panel.signature_approval) return true;
    const auto* approval=state.signing_reviews.Authorize(panel.signature_approval->reviewed);
    return !panel.signature_terminal && approval && *approval==*panel.signature_approval
        && SigningMaterial(state,panel.context.document)==panel.merge_base;
}
bool Current(State& state,app::Requests& host)
{
    const auto& panel=state.local_document;
    if (!Permitted(state) || !panel.dialog.open || panel.context!=Context(state)) return false;
    if (Merging(panel.mode) && BroadcastFreezes(state,panel.context.document)) return false;
    if ((ReadingQr(panel.mode) || ShowingQr(panel.mode)) && QrContext(host)!=std::pair{panel.qr_session,panel.qr_profile})
        return false;
    if (panel.mode==LocalDocumentMode::PaymentQr) {
        const auto* active=state.documents.Active();
        if (!active || !active->SendWallet() || !panel.qr_wallet || *active->SendWallet()!=*panel.qr_wallet
            || (active->kind==app::DocumentKind::Wallet && active->section!=app::Section::Send)
            || active->recipients.selected!=panel.qr_recipient
            || BroadcastFreezes(state,active->id)) return false;
    }
    if (panel.mode==LocalDocumentMode::ShowQr && SigningMaterial(state,panel.context.document)!=panel.merge_base)
        return false;
    if (panel.owner && CurrentInspectionOwner(state,host)!=panel.owner) return false;
    if (!panel.signature_terminal && !ExchangeReady(state)) return false;
    if ((Merging(panel.mode) || panel.mode==LocalDocumentMode::SaveSigningMaterial)
        && SigningMaterial(state,panel.context.document)!=panel.merge_base) return false;
    if (panel.mode==LocalDocumentMode::SaveFinalized || panel.mode==LocalDocumentMode::ShowFinalQr) {
        const auto* artifact=FinalizedMaterial(state,panel.context.document);
        if (!artifact || artifact->final.document!=panel.merge_base) return false;
    }
    return panel.mode!=LocalDocumentMode::SaveInspection || (panel.inspection && ValidInspectionExport(state,host,*panel.inspection));
}
void Status(LocalDocumentPanel& panel,std::string message)
{
    panel.error=std::move(message);
    ++panel.revision;
}
void Retire(State& state,app::Requests& host)
{
    if (auto* documents=host.LocalDocuments()) documents->Cancel();
    auto& panel=state.local_document;
    if (panel.camera_generation) if (auto* camera=host.Camera()) camera->Stop();
    sodium_memzero(panel.path.data(),panel.path.size());
    if (panel.edited_path && !panel.edited_path->empty()) sodium_memzero(panel.edited_path->data(),panel.edited_path->size());
    sodium_memzero(panel.qr_text.data(),panel.qr_text.size());
    if (panel.qr_text_edit && !panel.qr_text_edit->empty()) sodium_memzero(panel.qr_text_edit->data(),panel.qr_text_edit->size());
    state.local_document=LocalDocumentPanel{};
    state.local_document.close_pending=true;
}
void Text(State& state, const char* id, const char* name, std::string_view value, SemanticRole role=SemanticRole::Text)
{
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(value.data(),value.data()+value.size());
    ImGui::PopTextWrapPos();
    state.presentation.Annotate(id,name,value,role);
}
template <typename Action>
bool Button(State& state,std::map<std::string,c::ItemFacts>& facts,const imkit::Theme& theme,
    const char* id,const char* label,bool enabled,Action action,bool primary=false)
{
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    const bool pressed=theme::ActionButton(label,primary ? imkit::ActionVariant::Primary : imkit::ActionVariant::Secondary,{},theme);
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    facts[id]={{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),!enabled,pressed,pressed,ImGui::IsItemVisible()};
    const auto request=state.presentation.Interact(label,SemanticRole::Button,SemanticAction::Press|SemanticAction::Focus,!enabled);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted=pressed && action();
    if (request) state.presentation.Defer(*request,[action,enabled,pressed,accepted](const auto&){
        return enabled && (pressed ? accepted : action());
    });
    return accepted;
}
bool ConsumeQr(State& state,app::Requests& host,std::string_view text,bool from_camera=false)
{
    auto& panel=state.local_document;
    auto* documents=host.LocalDocuments();
    if (!documents || documents->Busy() || !Current(state,host) || !ReadingQr(panel.mode)) return false;
    panel.qr_payment.clear();
    panel.camera_result_from_stream=false;
    if (panel.mode==LocalDocumentMode::PaymentQr) {
        const auto request=ParsePaymentRequest(text,panel.network);
        if (!request.error.empty()) {Status(panel,request.error); return false;}
        panel.qr_payment=text;
        panel.camera_result_from_stream=from_camera;
        Status(panel,{});
        return true;
    }
    auto qr=panel.qr_assembly.Read(text);
    if (qr.kind==protocol::QrRead::Kind::Error) {Status(panel,std::move(qr.error));return false;}
    if (qr.kind==protocol::QrRead::Kind::Partial) {Status(panel,{});return true;}
    const bool binary=qr.kind==protocol::QrRead::Kind::Psbt;
    if (!binary && !qr.payload.starts_with("cHNidP")) {
        Status(panel,"Choose a Base64 PSBT or sequential UR v2 psbt. Raw transactions and payment requests are separate types.");
        return false;
    }
    const bool ready=binary ? documents->ReadBinary(panel.context,std::move(qr.payload),panel.network)
        : documents->ReadText(panel.context,std::move(qr.payload),panel.network);
    Status(panel,ready ? "" : "The exact QR payload could not be validated.");
    if (ready) {
        panel.camera_validation=from_camera;
        panel.qr_assembly.Reset();
    }
    return ready;
}

bool CaptureEngaged(CameraStage stage)
{
    return stage==CameraStage::Opening || stage==CameraStage::ChooseMode
        || stage==CameraStage::Starting || stage==CameraStage::Streaming;
}
bool CurrentCamera(State& state,app::Requests& host,uint64_t generation)
{
    if (!generation || !Current(state,host) || !ReadingQr(state.local_document.mode)
        || state.local_document.camera_generation!=generation || !host.Camera()) return false;
    const auto view=host.Camera()->View();
    return view.generation==generation && view.stage==CameraStage::Streaming;
}
void ClearCameraResult(LocalDocumentPanel& panel)
{
    panel.camera_sample=0;
    panel.camera_last_code.clear();
    panel.camera_validation=false;
    panel.camera_result_from_stream=false;
    panel.camera_result.reset();
    panel.qr_assembly.Reset();
    panel.qr_payment.clear();
}
void StopCamera(State& state,app::Requests& host)
{
    auto& panel=state.local_document;
    if (panel.camera_validation) if (auto* documents=host.LocalDocuments()) documents->Cancel();
    if (auto* camera=host.Camera()) {
        camera->Stop();
        panel.camera_generation=camera->View().generation;
    }
    panel.camera_mode.reset();
    ClearCameraResult(panel);
}
void PollCamera(State& state,app::Requests& host)
{
    auto& panel=state.local_document;
    auto* camera=host.Camera();
    if (!camera || !panel.camera_generation || !ReadingQr(panel.mode) || !Current(state,host)) return;
    const auto view=camera->View();
    if (view.generation!=panel.camera_generation || view.stage==CameraStage::Error) {
        if (panel.camera_validation) if (auto* documents=host.LocalDocuments()) documents->Cancel();
        if (panel.camera_sample || panel.camera_validation || panel.camera_result_from_stream) {
            ClearCameraResult(panel);
            Status(panel,{});
        }
        return;
    }
    if (view.stage!=CameraStage::Streaming || !view.frame || view.sample<=panel.camera_sample
        || panel.camera_validation || panel.camera_result || !panel.qr_payment.empty()) return;
    panel.camera_sample=view.sample;
    const auto& qr=view.frame->qr;
    if (qr && panel.camera_last_code!=*qr) {
        panel.camera_last_code=*qr;
        ConsumeQr(state,host,*qr,true);
    }
}
void PrepareQr(LocalDocumentPanel& panel)
{
    panel.qr_omitted=0;
    panel.qr_position=0;
    panel.qr_paused=false;
    panel.qr_started=panel.qr_changed=protocol::QrAssembler::Clock::now();
    panel.qr_export={};
    const auto& material=panel.merge_base;
    if (material && material->psbt_version) {
        if (material->wire.size()>protocol::MAX_QR_PSBT) {
            panel.qr_export.error="The PSBT exceeds the 16 KiB QR limit. Save the PSBT to a file.";
            return;
        }
        auto outbound=protocol::PsbtForQr(*material,panel.network,panel.qr_omit_xpubs);
        if (!outbound) {
            panel.qr_export.error="The outbound PSBT copy could not be verified.";
            return;
        }
        panel.qr_omitted=outbound->omitted_global_xpubs;
        panel.qr_export=protocol::EncodePsbtQr(outbound->wire,panel.qr_force_animation,panel.qr_density);
    } else if (material && material->transaction) {
        if (material->wire.size()<=protocol::MAX_QR_TEXT/2) {
            if (auto matrix=protocol::EncodeQr(protocol::TransactionHex(material->wire))) {
                panel.qr_export={"Static QR · raw transaction hex",{}, {std::move(*matrix)},material->wire.size()};
                return;
            }
        }
        panel.qr_export.error="The raw transaction does not fit a supported static QR. Save the exact transaction instead.";
    }
}
void NetworkPicker(State& state,app::Requests& host,LocalDocumentPanel& panel,bool enabled)
{
    const auto display=protocol::DocumentNetworkName(panel.network);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Display network");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150*ImGui::GetFontSize()/14.F);
    ImGui::BeginDisabled(!enabled);
    const bool open=ImGui::BeginCombo("##display-network",display.data());
    const auto choices=state.presentation.Interact("Display network",SemanticRole::TextField,
        SemanticAction::SetValue|SemanticAction::Focus,!enabled,false,display,"Network for address encoding. Transaction bytes carry no network.");
    const auto pick=[&panel](DisplayNetwork network) {
        panel.network=network;
        Status(panel,{});
        return true;
    };
    if (open) {
        for (unsigned i=0;i<=unsigned(DisplayNetwork::Regtest);++i) {
            const auto network=DisplayNetwork(i);
            const auto name=protocol::DocumentNetworkName(network);
            if (ImGui::Selectable(name.data(),panel.network==network)) pick(network);
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (choices) state.presentation.Defer(*choices,[&state,&host,pick,enabled](const auto& request) {
        if (!enabled || !Current(state,host)) return false;
        if (request.action==SemanticAction::Focus) return true;
        for (unsigned i=0;i<=unsigned(DisplayNetwork::Regtest);++i)
            if (protocol::DocumentNetworkName(DisplayNetwork(i))==request.value) return pick(DisplayNetwork(i));
        return false;
    });
}
ImportedView PolicyFor(const State& state,DisplayNetwork selected)
{
    ImportedView result;
    const auto* wallet=state.documents.Active();
    if (!wallet || !wallet->binding || protocol::DocumentNetwork(wallet->binding->network)!=selected) return result;
    const auto settings=state.settings.find(wallet->id);
    if (settings==state.settings.end() || settings->second.binding!=wallet->binding || !settings->second.saved) return result;
    for (const auto& branch:settings->second.saved->branches) {
        if (!branch.classified) continue;
        auto descriptor=branch.descriptor;
        descriptor.internal=false;
        protocol::PublicWalletPreview proof{settings->second.saved->chain,{std::move(descriptor)}};
        if (protocol::MatchesPublicPolicy(branch.descriptor.script,selected,proof))
            result.policy_scripts.insert(branch.descriptor.script);
    }
    if (!result.policy_scripts.empty()) result.wallet=wallet->binding;
    return result;
}
void RevokeReview(ImportedView& view,std::string_view reason)
{
    if (!view.reviewed) return;
    view.reviewed.reset();
    view.review_commitment.reset();
    view.proved_change_output.reset();
    view.review_status="Review revoked. "+std::string(reason);
    ++view.revision;
}
bool RecordReview(State& state,uint64_t id)
{
    const auto* doc=state.documents.Active();
    if (!Permitted(state) || !doc || doc->id!=id || !doc->imported) return false;
    auto& view=state.imported_views[id];
    if (!view.review_evidence || !view.wallet || view.review_evidence->network!=doc->imported->network
        || protocol::DocumentNetwork(view.wallet->network)!=doc->imported->network) return false;
    const auto checked=protocol::InspectOfflineReview(*doc->imported->parsed,*view.review_evidence);
    ++view.revision;
    if (!checked.commitment) {
        RevokeReview(view,checked.error);
        if (!view.review_status.starts_with("Review revoked.")) view.review_status=checked.error;
        return false;
    }
    view.reviewed=doc->imported->parsed;
    view.review_commitment=checked.commitment;
    view.proved_change_output=checked.change_output;
    view.review_status="Local review recorded for these exact transaction and policy facts.";
    return true;
}
void RetainCollectedSignatures(State& state,const app::Document& owner,
    std::shared_ptr<const protocol::TransactionDocument> material)
{
    auto& signing=state.signing_views[owner.id];
    signing.external_source=owner.imported->parsed;
    signing.external_signed=std::move(material);
}
void Adopt(State& state,app::Requests& host,LocalDocumentReply reply)
{
    if (!Current(state,host)) return;
    auto& panel=state.local_document;
    if (panel.camera_validation) {
        panel.camera_validation=false;
        if (!CurrentCamera(state,host,panel.camera_generation)) return;
        if (!reply.parsed.document || !reply.parsed.document->psbt_version) {
            Status(panel,reply.file_error.empty() ? protocol::DocumentErrorMessage(reply.parsed) : std::move(reply.file_error));
            return;
        }
        panel.camera_result=std::move(reply.parsed.document);
        panel.camera_result_from_stream=true;
        Status(panel,{});
        return;
    }
    if (reply.qr_text) {
        ConsumeQr(state,host,*reply.qr_text);
        return;
    }
    if (ReadingQr(panel.mode) && (!reply.file_error.empty() || (reply.parsed.document && !reply.parsed.document->psbt_version))) {
        Status(panel,reply.file_error.empty() ? "The decoded QR does not contain a supported PSBT." : std::move(reply.file_error));
        return;
    }
    if (!reply.parsed.document) {
        if (Merging(panel.mode) && !panel.signature_approval && (reply.parsed.error==protocol::DocumentError::Malformed
            || reply.parsed.error==protocol::DocumentError::Duplicate)) {
            RevokeReview(state.imported_views[panel.context.document],"The supplied PSBT contains malformed or contradictory fields.");
            RevokeSigningReview(state,panel.context.document,"The supplied merge contains malformed or contradictory fields.");
        }
        Status(panel,reply.file_error.empty() ? protocol::DocumentErrorMessage(reply.parsed) : std::move(reply.file_error));
        return;
    }
    if (Merging(panel.mode)) {
        auto* active=state.documents.Active();
        if (!active || !active->imported || !panel.merge_base) return;
        auto& previous=state.imported_views[active->id];
        auto merged=protocol::MergeOfflineSignatures(*panel.merge_base,*reply.parsed.document);
        if (!merged.document) {
            if (!merged.review_conflict.empty()) {
                if (panel.signature_approval && panel.merge_base!=active->imported->parsed)
                    RetainCollectedSignatures(state,*active,panel.merge_base);
                RevokeReview(previous,merged.review_conflict);
                RevokeSigningReview(state,active->id,merged.review_conflict);
                panel.signature_terminal=panel.signature_approval.has_value();
            }
            Status(panel,std::move(merged.error)+(panel.signature_terminal
                ? " Review and approve the intended transaction again before collecting signatures." : ""));
            return;
        }
        if (panel.signature_approval) {
            const auto* approval=CurrentSigningApproval(state,host,active->id,true);
            if (!approval || *approval!=*panel.signature_approval
                || SigningMaterial(state,active->id)!=panel.merge_base) {
                Status(panel,"The signing context changed. Reopen signature exchange from the current review.");
                return;
            }
            const auto& basis=approval->reviewed;
            const auto proof=protocol::InspectOfflineReview(*merged.document,basis.proof,basis.change_output);
            if (proof.commitment!=approval->commitment || proof.change_output!=basis.change_output) {
                Status(panel,"The signed material does not preserve the exact approved payment, inputs and change proof.");
                RevokeSigningReview(state,active->id,panel.error);
                panel.signature_terminal=true;
                return;
            }
            RetainCollectedSignatures(state,*active,std::move(merged.document));
            previous.signatures.open=true;
            ++state.signing_revision;
            state.notice=std::to_string(merged.satisfied)+" / "+std::to_string(merged.required)
                +" input policies verified. The approved original is retained.";
            Retire(state,host);
            return;
        }
        if (previous.reviewed) {
            auto conflict=protocol::CompareTransactionReview(*previous.reviewed,*merged.document);
            if (!conflict.empty()) {
                RevokeReview(previous,conflict);
                RevokeSigningReview(state,active->id,conflict);
                Status(panel,std::move(conflict));
                return;
            }
        }
        if (!OpenLocalData(state,std::move(merged.document),active->imported->network,panel.error)) {
            ++panel.revision;
            return;
        }
        auto copied=previous;
        copied.signatures.open=true;
        state.imported_views.emplace(state.documents.ActiveId(),std::move(copied));
        state.notice=std::to_string(merged.satisfied)+" / "+std::to_string(merged.required)
            +" input policies verified. The previous PSBT is retained.";
        Retire(state,host);
        return;
    }
    auto view=PolicyFor(state,panel.network);
    if (!OpenLocalData(state,std::move(reply.parsed.document),panel.network,panel.error)) {
        ++panel.revision;
        return;
    }
    state.imported_views.emplace(state.documents.ActiveId(),std::move(view));
    Retire(state,host);
}

void DrawCameraPreview(State& state,const CameraFrame& frame,float scale)
{
    if (!frame.width || !frame.height || frame.width>CAMERA_MAX_WIDTH || frame.height>CAMERA_MAX_HEIGHT
        || frame.luma.size()!=size_t(frame.width)*frame.height) return;
    const auto shrink=std::min({1.F,320.F/frame.width,240.F/frame.height});
    const auto width=std::max(1U,unsigned(frame.width*shrink));
    const auto height=std::max(1U,unsigned(frame.height*shrink));
    const float scale_to_fit=std::min(scale,ImGui::GetContentRegionAvail().x/width);
    if (scale_to_fit<=0) return;
    const auto origin=ImGui::GetCursorScreenPos();
    auto* draw=ImGui::GetWindowDrawList();
    for (unsigned y=0;y<height;++y) {
        const auto row=size_t(y*frame.height/height)*frame.width;
        unsigned x=0;
        while (x<width) {
            const auto value=frame.luma[row+x*frame.width/width];
            unsigned end=x+1;
            while (end<width && frame.luma[row+end*frame.width/width]==value) ++end;
            draw->AddRectFilled({origin.x+x*scale_to_fit,origin.y+y*scale_to_fit},
                {origin.x+end*scale_to_fit,origin.y+(y+1)*scale_to_fit},IM_COL32(value,value,value,255));
            x=end;
        }
    }
    ImGui::Dummy({width*scale_to_fit,height*scale_to_fit});
    state.presentation.Annotate("camera-preview","Live camera preview",
        std::to_string(frame.width)+" × "+std::to_string(frame.height)+" luminance frame");
}
void DrawPaymentImport(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme,bool busy)
{
    auto& panel=state.local_document;
    if (panel.qr_payment.empty()) return;
    const auto request=ParsePaymentRequest(panel.qr_payment,panel.network);
    Text(state,"qr-payment","Validated payment request","Destination "+request.address
        +(request.amount ? " · "+std::to_string(request.amount->sats())+" sat" : ""));
    const auto generation=panel.camera_result_from_stream ? panel.camera_generation : 0;
    Button(state,facts.local_document,theme,"qr-use-payment","Use this payment request",!busy,[&state,&host,&panel,generation] {
        if (!Current(state,host) || panel.qr_payment.empty() || (generation && !CurrentCamera(state,host,generation))) return false;
        auto* doc=state.documents.Find(panel.context.document);
        if (!doc || !doc->recipients.ApplyDestination(panel.qr_recipient,panel.qr_payment,panel.network,doc->draft_revision))
            return false;
        Retire(state,host);
        return true;
    },true);
}

bool DrawCameraImport(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme,float scale)
{
    auto* camera=host.Camera();
    if (!camera || !camera->Available()) return false;
    auto& panel=state.local_document;
    const auto view=camera->View();
    const bool owns=panel.camera_generation && panel.camera_generation==view.generation;
    const bool engaged=owns && CaptureEngaged(view.stage);
    const bool busy=host.LocalDocuments()->Busy() || panel.file_dialog || panel.clipboard_pending;
    if(engaged && host.Desktop())if(const auto status=host.Desktop()->Status();!status.empty())
        Text(state,"camera-desktop","Camera desktop operation",status);
    const auto current=[&state,&host]{return Current(state,host) && ReadingQr(state.local_document.mode);};
    if (view.retiring) Text(state,"camera-closing","Camera status","Releasing the camera…");
    if (!engaged) {
        Button(state,facts.local_document,theme,"camera-find","Find cameras",!busy && !view.retiring && (!owns || view.stage!=CameraStage::Finding),
            [&panel,camera,current] {
                if (!current() || !camera->Find()) return false;
                ClearCameraResult(panel);
                panel.camera_device.reset();
                panel.camera_mode.reset();
                panel.camera_generation=camera->View().generation;
                return true;
            });
        if (owns) for (const auto& choice:view.devices) {
            const auto key="camera-"+std::to_string(choice.slot);
            const auto label=(panel.camera_device==choice ? "Selected: " : "Select ")+choice.name;
            Button(state,facts.local_document,theme,key.c_str(),label.c_str(),!busy,
                [&panel,current,choice,camera,generation=view.generation] {
                    if (!current() || camera->View().generation!=generation) return false;
                    ClearCameraResult(panel);
                    panel.camera_device=choice;
                    return true;
                });
        }
        if (owns && panel.camera_device) Button(state,facts.local_document,theme,"camera-open","Open selected camera",!busy && !view.retiring,
            [&panel,camera,current,generation=view.generation,selected=panel.camera_device] {
                if (!current() || !selected || panel.camera_device!=selected || !camera->Open(generation,*selected)) return false;
                ClearCameraResult(panel);
                panel.camera_mode.reset();
                panel.camera_generation=camera->View().generation;
                return true;
            });
    }
    if (owns && !view.status.empty()) Text(state,"camera-status","Camera status",view.status,
        view.stage==CameraStage::Error ? SemanticRole::Alert : SemanticRole::Text);
    if (!engaged) return false;
    if (view.stage==CameraStage::ChooseMode) {
        for (size_t index=0;index<view.modes.size();++index) {
            const auto mode=view.modes[index];
            const auto key="camera-mode-"+std::to_string(index);
            const auto label=(panel.camera_mode==mode ? "Selected: " : "Select ")+CameraModeLabel(mode);
            Button(state,facts.local_document,theme,key.c_str(),label.c_str(),true,[&panel,current,camera,generation=view.generation,mode] {
                if (!current()) return false;
                const auto live=camera->View();
                if (live.generation!=generation || live.stage!=CameraStage::ChooseMode) return false;
                panel.camera_mode=mode;
                return true;
            });
        }
        Button(state,facts.local_document,theme,"camera-start","Start capture",panel.camera_mode.has_value(),
            [&panel,camera,current,generation=view.generation,selected=panel.camera_mode] {
                return current() && selected && panel.camera_mode==selected && camera->Start(generation,*selected);
            },true);
    }
    if (view.stage==CameraStage::Streaming && view.frame) {
        DrawCameraPreview(state,*view.frame,scale);
        if (panel.camera_mode) Text(state,"camera-mode-active","Current camera resolution",CameraModeLabel(*panel.camera_mode));
        if (!panel.camera_last_code.empty() && !panel.camera_result && panel.qr_payment.empty())
            Text(state,"camera-detected","QR detection","QR detected. Complete data must be validated before use.");
    }
    if (panel.qr_assembly.Expected()) Text(state,"camera-assembly","QR assembly progress",
        "Received "+std::to_string(panel.qr_assembly.Collected())+" / "+std::to_string(panel.qr_assembly.Expected())+" frames.");
    if (panel.camera_validation) Text(state,"camera-validating","QR validation","Validating the complete PSBT…");
    if (panel.camera_result) {
        Text(state,"camera-validated","QR validation",Merging(panel.mode)
            ? "PSBT structure validated. Import checks the current signing context and signatures."
            : "PSBT structure validated. Import opens it for inspection.");
        Button(state,facts.local_document,theme,"camera-import","Import validated PSBT",!busy,
            [&state,&host,&panel,generation=view.generation] {
                if (!CurrentCamera(state,host,generation) || !panel.camera_result || host.LocalDocuments()->Busy()) return false;
                auto data=std::exchange(panel.camera_result,{});
                Adopt(state,host,{protocol::DocumentResult{std::move(data)}});
                return true;
            },true);
    }
    if (panel.mode==LocalDocumentMode::PaymentQr) DrawPaymentImport(state,host,facts,theme,busy);
    if (!panel.error.empty()) Text(state,"camera-qr-error","QR validation error",panel.error,SemanticRole::Alert);
    Button(state,facts.local_document,theme,"camera-stop","Stop camera",true,[&state,&host,current] {
        if (!current()) return false;
        StopCamera(state,host);
        Status(state.local_document,{});
        return true;
    });
    ImGui::SameLine();
    Button(state,facts.local_document,theme,"camera-close","Cancel",true,[&state,&host]{Retire(state,host);return true;});
    return true;
}

}

void RetireLocalDocument(State& state,app::Requests& host)
{
    Retire(state,host);
}
bool DrawQrMatrix(const protocol::QrMatrix& matrix,float max_side)
{
    constexpr int quiet=4;
    const int width=matrix.size+2*quiet;
    const auto scale=ImGui::GetIO().DisplayFramebufferScale;
    if (matrix.size<21 || matrix.modules.size()!=size_t(matrix.size)*matrix.size || scale.x<=0 || scale.y<=0)
        return false;
    const float side=std::min(ImGui::GetContentRegionAvail().x,max_side)-2.F;
    const float cell=std::floor(side*std::min(scale.x,scale.y)/width);
    if (cell<3.F) return false;
    const ImVec2 pitch{cell/scale.x,cell/scale.y};
    const auto cursor=ImGui::GetCursorScreenPos();
    const ImVec2 origin{std::ceil(cursor.x*scale.x)/scale.x,std::ceil(cursor.y*scale.y)/scale.y};
    auto* list=ImGui::GetWindowDrawList();
    list->AddRectFilled(origin,{origin.x+width*pitch.x,origin.y+width*pitch.y},IM_COL32_WHITE);
    for (int y=0;y<matrix.size;++y) for (int x=0;x<matrix.size;++x) {
        if (!matrix.modules[size_t(y)*matrix.size+x]) continue;
        list->AddRectFilled({origin.x+(x+quiet)*pitch.x,origin.y+(y+quiet)*pitch.y},
            {origin.x+(x+quiet+1)*pitch.x,origin.y+(y+quiet+1)*pitch.y},IM_COL32_BLACK);
    }
    ImGui::Dummy({width*pitch.x+1.F,width*pitch.y+1.F});
    return true;
}

bool OpenLocalData(State& state,std::shared_ptr<const protocol::TransactionDocument> parsed,
    DisplayNetwork network,std::string& error)
{
    if (!Permitted(state) || !parsed) return false;
    auto requested=state.documents;
    const auto id=requested.OpenImportedDocument({std::move(parsed),network});
    if (!id || (state.recovery && !state.recovery->CanAdmitPublicDocuments(requested))) {
        error=id ? "This document could not fit in the saved workspace. Previous documents retained."
                 : "Close a document before opening another.";
        return false;
    }
    if (state.recovery && state.recovery->Enabled() && !state.recovery->Checkpoint(requested)) {
        error="The saved workspace could not be updated. Previous documents retained.";
        return false;
    }
    state.documents=std::move(requested);
    state.reveal_tab_focus=state.focus_tab=id;
    return true;
}

bool BeginLocalDocument(State& state,app::Requests& host,LocalDocumentMode mode,std::string_view path,bool collect)
{
    auto* documents=host.LocalDocuments();
    auto& panel=state.local_document;
    if (!documents || documents->Busy() || !Permitted(state) || panel.dialog.open || panel.close_pending
        || state.server.dialog.open || state.onboarding.dialog.open || state.actions.kind!=DocumentDialog::None
        || state.recovery_dialog.open || state.local_document_generation==UINT64_MAX || path.size()>4096)
        return false;
    const auto* active=state.documents.Active();
    const bool save=mode==LocalDocumentMode::Save || mode==LocalDocumentMode::SaveInspection
        || mode==LocalDocumentMode::SaveSigningMaterial || mode==LocalDocumentMode::SaveFinalized;
    const bool merge=Merging(mode);
    const bool attempts=mode==LocalDocumentMode::BroadcastAttempts;
    const bool qr_show=ShowingQr(mode);
    const bool qr_read=ReadingQr(mode);
    const bool payment=mode==LocalDocumentMode::PaymentQr;
    if (payment && (!active || !active->SendWallet()
        || (active->kind==app::DocumentKind::Wallet && active->section!=app::Section::Send)
        || !active->recipients.Find(active->recipients.selected) || BroadcastFreezes(state,active->id))) return false;
    if (merge && active && (BroadcastFreezes(state,active->id) || (FeeReplacement(state,active->id) && !collect))) return false;
    if (((save || merge || qr_show) && (!active || !active->imported))
        || (merge && !active->imported->parsed->psbt_version)
        || (!save && !collect && !attempts && !qr_show && !payment && state.documents.Documents().size()>=32)) return false;
    const auto owner=save || merge || qr_show ? CurrentInspectionOwner(state,host) : std::nullopt;
    if ((save || merge || qr_show) && !owner) return false;
    const auto material=merge || mode==LocalDocumentMode::SaveSigningMaterial || mode==LocalDocumentMode::ShowQr ? SigningMaterial(state,active->id) : nullptr;
    const auto* finalized=mode==LocalDocumentMode::SaveFinalized || mode==LocalDocumentMode::ShowFinalQr ? FinalizedMaterial(state,active->id) : nullptr;
    if ((mode==LocalDocumentMode::SaveFinalized || mode==LocalDocumentMode::ShowFinalQr) && !finalized) return false;
    if (mode==LocalDocumentMode::SaveSigningMaterial && (!material || !material->psbt_version || material==owner->content))
        return false;
    const auto* approval=collect && merge ? CurrentSigningApproval(state,host,active->id) : nullptr;
    if (collect && (!approval || state.daemon_operation || state.daemon_unlock.owner)) return false;
    const auto entries=attempts && state.recovery ? state.recovery->RecentBroadcastAttempts()
        : storage::Result<std::vector<storage::BroadcastRecord>>{};
    panel={};
    panel.mode=mode;
    panel.owner=owner;
    if (merge) {
        panel.network=active->imported->network;
        panel.merge_base=material;
        if (approval) panel.signature_approval=*approval;
    }
    if (mode==LocalDocumentMode::SaveSigningMaterial || mode==LocalDocumentMode::ShowQr) panel.merge_base=material;
    if (qr_read || qr_show) {
        const auto [session,profile]=QrContext(host);
        panel.qr_session=session;
        panel.qr_profile=profile;
        if (active && active->imported) panel.network=active->imported->network;
        if (active && active->binding) panel.network=protocol::DocumentNetwork(active->binding->network);
    }
    if (payment) {
        panel.qr_wallet=*active->SendWallet();
        panel.network=protocol::DocumentNetwork(panel.qr_wallet->network);
        panel.qr_recipient=active->recipients.selected;
    }
    if (finalized) panel.merge_base=finalized->final.document;
    if (qr_show) PrepareQr(panel);
    if (attempts) {
        if (entries) panel.broadcast_attempts=*entries.value;
        else panel.error="The local submission records could not be read.";
    }
    panel.context.generation=++state.local_document_generation;
    panel.context=Context(state);
    panel.dialog.open=true;
    panel.dialog.initialFocus=true;
    std::copy(path.begin(),path.end(),panel.path.begin());
    return true;
}
void DrawLocalDocumentDialog(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme,float scale)
{
    auto* documents=host.LocalDocuments();
    if (!documents) return;
    auto& panel=state.local_document;
    if (panel.dialog.open && !Current(state,host)) {
        if (panel.camera_generation && Permitted(state)) state.notice="Camera closed because the current import context changed.";
        Retire(state,host);
    }
    PollCamera(state,host);
    auto reply=documents->Poll(Current(state,host) ? std::optional{panel.context} : std::nullopt);
    if (reply) Adopt(state,host,std::move(*reply));
    if (panel.close_pending) {
        if (ImGui::BeginPopupModal(DIALOG)) {ImGui::CloseCurrentPopup();ImGui::EndPopup();}
        panel.close_pending=false;
        return;
    }
    if (!panel.dialog.open) return;
    const auto* viewport=ImGui::GetMainViewport();
    const auto width=std::min(490*scale,viewport->WorkSize.x-32*scale);
    ImGui::SetNextWindowSizeConstraints({width,0},{width,viewport->WorkSize.y-20*scale});
    imkit::ComponentOptions options;
    options.theme=&theme;
    if (!imkit::BeginDialog(DIALOG,panel.dialog,options)) {
        if (!panel.dialog.open) {
            if (panel.camera_generation && Permitted(state)) state.notice="Camera closed with the import dialog.";
            Retire(state,host);
        }
        return;
    }
    state.presentation.DialogOnly();
    const auto minimum=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
    state.presentation.Group("local-document-dialog","Local transaction document",SemanticRole::Dialog,minimum,
        {minimum.x+size.x,minimum.y+size.y});
    if (ShowingQr(panel.mode)) {
        const auto current=[&state,&host]{return Current(state,host);};
        if (panel.merge_base && panel.merge_base->psbt_version) {
            Button(state,facts.local_document,theme,"qr-mode",panel.qr_force_animation ? "Prefer static when it fits" : "Always use UR",
                true,[&panel,current] {
                    if (!current()) return false;
                    panel.qr_force_animation=!panel.qr_force_animation;
                    PrepareQr(panel);
                    return true;
                });
            const std::array densities{"Compact","Standard","Dense"};
            const auto density="QR density: "+std::string(densities[size_t(panel.qr_density)]);
            ImGui::SameLine();
            Button(state,facts.local_document,theme,"qr-density",density.c_str(),true,[&panel,current] {
                if (!current()) return false;
                panel.qr_density=protocol::QrDensity((size_t(panel.qr_density)+1)%3);
                PrepareQr(panel);
                return true;
            });
            Button(state,facts.local_document,theme,"qr-xpubs",panel.qr_omit_xpubs ? "Include global xpubs" : "Omit global xpubs",
                true,[&panel,current] {
                    if (!current()) return false;
                    panel.qr_omit_xpubs=!panel.qr_omit_xpubs;
                    PrepareQr(panel);
                    return true;
                });
            Text(state,"qr-metadata","Outbound metadata",panel.qr_omit_xpubs
                ? "Outbound copy: omitted "+std::to_string(panel.qr_omitted)+" global xpub records. Other metadata and the original are retained."
                : "Outbound copy: includes all global xpub records from the current PSBT.");
        }
        const auto now=protocol::QrAssembler::Clock::now();
        const auto frames=panel.qr_export.frames.size();
        const bool expired=now-panel.qr_started>=protocol::QR_LIFETIME;
        if (!expired && frames>1 && !panel.qr_paused && now-panel.qr_changed>=std::chrono::milliseconds(400)) {
            panel.qr_position=(panel.qr_position+1)%frames;
            panel.qr_changed=now;
        }
        if (!panel.qr_export.error.empty()) Text(state,"qr-export-error","QR export",panel.qr_export.error,SemanticRole::Alert);
        else if (frames>0) {
            const auto& matrix=panel.qr_export.frames[panel.qr_position];
            Text(state,"qr-format","QR encoding and size",panel.qr_export.encoding+" · "
                +std::to_string(panel.qr_export.bytes)+" bytes · "+std::to_string(matrix.size)+" modules");
            Text(state,"qr-frame","QR frame","Frame "+std::to_string(panel.qr_position+1)+" / "+std::to_string(frames));
            if (expired) Text(state,"qr-expired","QR display expired","QR display expired. Restart to reveal the current copy.");
            else if (!DrawQrMatrix(matrix,std::min(320*scale,viewport->WorkSize.y-190*scale)))
                Text(state,"qr-room","QR size","Enlarge the window so each QR module can use at least three pixels.");
            if (frames>1) {
                Button(state,facts.local_document,theme,"qr-pause",panel.qr_paused ? "Resume animation" : "Pause animation",
                    !expired,[&panel,current] {
                        if (!current()) return false;
                        panel.qr_paused=!panel.qr_paused;
                        panel.qr_changed=protocol::QrAssembler::Clock::now();
                        return true;
                    });
                ImGui::SameLine();
                Button(state,facts.local_document,theme,"qr-next","Next frame",panel.qr_paused && !expired,[&panel,current] {
                    if (!current() || panel.qr_export.frames.size()<2 || !panel.qr_paused) return false;
                    panel.qr_position=(panel.qr_position+1)%panel.qr_export.frames.size();
                    return true;
                });
                ImGui::SameLine();
            }
            Button(state,facts.local_document,theme,"qr-restart","Restart display",true,[&panel,current] {
                if (!current()) return false;
                panel.qr_position=0;
                panel.qr_paused=false;
                panel.qr_started=panel.qr_changed=protocol::QrAssembler::Clock::now();
                return true;
            });
        }
        Button(state,facts.local_document,theme,"qr-close","Close QR",true,[&state,&host] {Retire(state,host);return true;});
        state.presentation.EndGroup();
        imkit::EndDialog();
        return;
    }
    if (panel.mode==LocalDocumentMode::BroadcastAttempts) {
        Text(state,"recent-submissions","Recent submission attempts","Recent submission attempts");
        if (!panel.error.empty()) Text(state,"submission-record-error","Submission records",panel.error,SemanticRole::Alert);
        else if (panel.broadcast_attempts.empty()) Text(state,"submission-empty","Submission records","No submission attempts are recorded.");
        else {
            ImGui::BeginChild("recent-submission-list",{0,250*scale},ImGuiChildFlags_Borders);
            for (const auto& entry:panel.broadcast_attempts) {
                const auto& key=entry.identity;
                const auto network=protocol::DocumentNetwork(key.network);
                ImGui::PushID(key.txid.hex().c_str());
                ImGui::PushID(key.network.genesis.hex().c_str());
                Text(state,"submission-id","Recorded transaction ID",key.txid.hex());
                Text(state,"submission-attempt","Recorded outcome",a::BroadcastRecordLabel(entry)+" · "+
                    std::string(protocol::DocumentNetworkName(network)));
                const bool opened=Button(state,facts.local_document,theme,"open-recorded-transaction","Open recorded transaction",
                    !entry.wire_hex.empty() && network!=DisplayNetwork::Unknown && state.documents.Documents().size()<32,
                    [&state,&host,network,key,serial=entry.serial,context=panel.context] {
                        if (!Current(state,host) || !state.recovery || state.local_document.context!=context) return false;
                        const auto stored=state.recovery->LoadBroadcastAttempt(key.network,key.txid);
                        if (!stored || !*stored.value || (**stored.value).serial!=serial) {
                            Status(state.local_document,"The attempt changed. Reopen this list to see the latest record.");
                            return false;
                        }
                        const auto parsed=protocol::ParseTransactionDocument((**stored.value).wire_hex,network);
                        if (!parsed.document || !OpenLocalData(state,parsed.document,network,state.local_document.error)) return false;
                        Retire(state,host);
                        return true;
                    });
                ImGui::PopID();
                ImGui::PopID();
                if (opened) break;
                ImGui::Separator();
            }
            ImGui::EndChild();
        }
        Button(state,facts.local_document,theme,"close-records","Close",!panel.close_pending,[&state,&host]{
            Retire(state,host);
            return true;
        });
        state.presentation.EndGroup();
        imkit::EndDialog();
        return;
    }
    const bool byte_save=panel.mode==LocalDocumentMode::SaveInspection;
    const bool signed_save=panel.mode==LocalDocumentMode::SaveSigningMaterial;
    const bool final_save=panel.mode==LocalDocumentMode::SaveFinalized;
    const bool save=panel.mode==LocalDocumentMode::Save || byte_save || signed_save || final_save;
    const bool clipboard=panel.mode==LocalDocumentMode::Text || panel.mode==LocalDocumentMode::MergeText;
    const bool merge=Merging(panel.mode);
    const bool busy=documents->Busy();
    const bool qr=ReadingQr(panel.mode);
    const bool payment=panel.mode==LocalDocumentMode::PaymentQr;
    if (qr && DrawCameraImport(state,host,facts,theme,scale)) {
        state.presentation.EndGroup();
        imkit::EndDialog();
        return;
    }
    if (qr) {
        Text(state,"qr-read-purpose","QR import formats",payment
            ? "Read a Bitcoin address or payment URI from a PNG or decoded QR text."
            : "Read a Base64 PSBT or sequential UR v2 psbt from a PNG or decoded QR text.");
        if (panel.qr_assembly.Expected()) {
            const auto total=std::to_string(panel.qr_assembly.Collected())+" / "+std::to_string(panel.qr_assembly.Expected());
            Text(state,"qr-received","QR assembly progress",panel.qr_assembly.Expired()
                ? "Assembly expired. Restart to discard "+total+" retained frames." : "Received "+total+" frames.");
        }
        ImGui::TextUnformatted("Decoded QR text");
        ImGui::SetNextItemWidth(-1);
        ImGui::BeginDisabled(busy);
        c::TextInput("##qr-text",nullptr,panel.qr_text.data(),panel.qr_text.size(),panel.qr_text_edit,0,&panel.qr_cursor,true);
        const auto input=state.presentation.Interact("Decoded QR text",SemanticRole::TextField,
            SemanticAction::SetValue|SemanticAction::Focus,busy,false,{},"Paste or type a single decoded QR frame.");
        ImGui::EndDisabled();
        if (input) state.presentation.Defer(*input,[&state,&host,&panel,documents](const auto& request) {
            if (!Current(state,host) || documents->Busy()) return false;
            if (request.action==SemanticAction::Focus) return true;
            return c::SetExternalBuffer(panel.qr_text,panel.qr_text_edit,request.value);
        });
        Button(state,facts.local_document,theme,"qr-read-text","Read QR text",!busy && panel.qr_text[0],[&state,&host,&panel] {
            return ConsumeQr(state,host,panel.qr_text.data());
        });
        ImGui::SameLine();
        Button(state,facts.local_document,theme,"qr-reset","Restart QR import",!busy,[&state,&host,&panel] {
            if (!Current(state,host)) return false;
            panel.qr_assembly.Reset();
            panel.qr_payment.clear();
            sodium_memzero(panel.qr_text.data(),panel.qr_text.size());
            panel.qr_text_edit=std::string{};
            Status(panel,{});
            return true;
        });
        if (payment) DrawPaymentImport(state,host,facts,theme,busy);
        if (panel.mode==LocalDocumentMode::Qr) {
            NetworkPicker(state,host,panel,!busy && !panel.qr_assembly.Expected());
            Text(state,"qr-network","QR network information","A PSBT does not identify its chain. Review its outputs before approval.");
        }
    }
    auto* input=c::native_input;
    const auto clipboard_id=ImGui::GetID("inspect-transaction-clipboard");
    if (panel.clipboard_pending && input) {
        auto text=input->Paste(clipboard_id,"",0,0,0,protocol::MAX_TRANSACTION_SOURCE+1);
        if (text) {
            panel.clipboard_pending=false;
            if (!Current(state,host) || !ExchangeReady(state) || !documents->ReadText(panel.context,std::move(*text),panel.network)) {
                if (!text->empty()) sodium_memzero(text->data(),text->size());
                Status(panel,"Clipboard inspection was cancelled.");
            }
        } else if (!input->InspectionPending(clipboard_id)) {
            panel.clipboard_pending=false;
            Status(panel,"Clipboard inspection was cancelled or no bounded text was available.");
        }
    }
    if (!qr && merge) Text(state,"merge-purpose","External PSBT merge",clipboard
        ? "Copy a Base64 PSBT, then merge from the clipboard. The working PSBT and confirmation must refer to the same version."
        : "Choose a signed PSBT of the same version. The transaction and every added signature are checked before combining.");
    else if (byte_save && panel.inspection) {
        const auto& request=*panel.inspection;
        const auto description=std::string(protocol::InspectionBufferName(*request.owner.content,request.representation));
        Text(state,"save-representation","Export representation",description);
        if (request.selection) Text(state,"save-byte-range","Export byte range",
            "Offset "+std::to_string(request.selection->offset)+" · "+std::to_string(request.selection->length)+" bytes");
    } else if (!save && !qr) {
        Text(state,"import-purpose","Local document types",clipboard
            ? "Inspect clipboard text as transaction hex, Base64 PSBT, or a public descriptor."
            : "Open a local raw transaction, PSBT, or public descriptor. Format is checked from the contents.");
        NetworkPicker(state,host,panel,!busy && !panel.clipboard_pending && !panel.file_dialog);
    }
    if (!clipboard) {
        ImGui::TextUnformatted("Local file path");
        ImGui::SetNextItemWidth(-1);
        ImGui::BeginDisabled(busy || panel.file_dialog!=0);
        if (c::TextInput("##local-transaction-path",nullptr,panel.path.data(),panel.path.size(),panel.edited_path,0,&panel.cursor,true))
            Status(panel,{});
        const auto path=state.presentation.Interact("Local file path",SemanticRole::TextField,SemanticAction::SetValue|SemanticAction::Focus,
            busy || panel.file_dialog,false,panel.path.data(),"Choose an absolute local file path.");
        state.presentation.BindTextPosition(ImGui::GetItemID(),panel.path.data(),panel.cursor);
        ImGui::EndDisabled();
        if (path) state.presentation.Defer(*path,[&state,&host,&panel](const auto& request){
            if (!Current(state,host)) return false;
            if (request.action==SemanticAction::Focus) return true;
            if (!c::SetExternalBuffer(panel.path,panel.edited_path,request.value)) return false;
            Status(panel,{});
            return true;
        });
        if (panel.file_dialog && input) {
            const auto chosen=input->PollFile(panel.file_dialog);
            if (chosen.outcome!=c::NativeOutcome::Pending) {
                panel.file_dialog=0;
                if (Current(state,host) && chosen.outcome==c::NativeOutcome::Chosen
                    && c::SetExternalBuffer(panel.path,panel.edited_path,chosen.path)) Status(panel,{});
                else if (chosen.outcome!=c::NativeOutcome::Cancelled && !chosen.detail.empty()) Status(panel,chosen.detail);
            }
        }
        Button(state,facts.local_document,theme,"browse","Browse local files",!busy && !panel.file_dialog && input && input->FileChooserAvailable(),
            [&state,&host,&panel,input,save] {
                if (!Current(state,host) || !input) return false;
                panel.file_dialog=input->ChooseFile(panel.path.data(),save);
                return panel.file_dialog!=0;
            });
    }
    if (!panel.error.empty()) Text(state,"local-document-error","Local document status",panel.error,SemanticRole::Alert);
    if (busy) Text(state,"local-document-reading","Local document progress","Reading and validating the document…");
    Button(state,facts.local_document,theme,"cancel","Cancel",true,[&state,&host]{Retire(state,host);return true;});
    ImGui::SameLine();
    if (clipboard) Button(state,facts.local_document,theme,"inspect-clipboard",merge ? "Validate clipboard signatures" : "Inspect clipboard",
        input && !busy && !panel.clipboard_pending && ExchangeReady(state),
        [&state,&host,&panel,input,clipboard_id] {
            if (!Current(state,host) || !ExchangeReady(state) || !input || !input->RequestInspection(clipboard_id)) return false;
            // Keep the native selection request in its admitted view until
            // it is consumed or that view is retired.
            panel.clipboard_pending=true;
            panel.error.clear();
            return true;
        },true);
    else Button(state,facts.local_document,theme,save ? (byte_save ? "save-inspection" : "save-original") : "inspect-file",
        save ? (byte_save ? "Save bytes to file" : final_save ? "Save final transaction" : signed_save ? "Save collected PSBT" : "Save original bytes")
             : qr ? "Read QR image" : merge ? "Validate and merge" : "Inspect file",
        !busy && !panel.file_dialog && ValidLocalDocumentPath(panel.path.data()) && ExchangeReady(state),
        [&state,&host,&panel,documents,save,byte_save,signed_save,final_save,qr] {
            if (!Current(state,host) || !ExchangeReady(state)) return false;
            if (!save) {
                const bool accepted=qr ? documents->ReadQrImage(panel.context,panel.path.data())
                    : documents->ReadFile(panel.context,panel.path.data(),panel.network);
                if (!accepted) return false;
                Status(panel,{});
                return true;
            }
            const auto* doc=state.documents.Active();
            if (!doc || !doc->imported) return false;
            std::string error;
            if (byte_save) {
                const auto& request=*panel.inspection;
                auto bytes=protocol::InspectionBuffer(*request.owner.content,request.representation);
                if (request.selection) bytes=bytes.substr(request.selection->offset,request.selection->length);
                error=SavePublicFile(panel.path.data(),bytes,protocol::MAX_TRANSACTION_SOURCE,true);
            } else error=SaveOriginalDocument(panel.path.data(),signed_save || final_save ? *panel.merge_base : *doc->imported->parsed);
            if (!error.empty()) {Status(panel,error);return false;}
            state.notice=byte_save ? "Selected representation bytes saved to a new local file." :
                final_save ? "Exact final transaction saved to a new local file." :
                signed_save ? "Collected PSBT saved to a new local file." : "Original bytes saved to a new local file.";
            Retire(state,host);
            return true;
        },true);
    state.presentation.EndGroup();
    imkit::EndDialog();
}

const protocol::SignatureProgress& SignatureProgressFor(State& state,uint64_t id,
    const std::shared_ptr<const protocol::TransactionDocument>& material)
{
    auto& signatures=state.imported_views[id].signatures;
    if (signatures.source.lock()!=material || !signatures.progress) {
        signatures.source=material;
        signatures.progress=protocol::InspectSignatures(*material);
        signatures.page=std::min(signatures.page,signatures.progress->inputs.empty() ? size_t{} : signatures.progress->inputs.size()-1);
    }
    return *signatures.progress;
}

void DrawSignatureExchange(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme,
    const InspectionOwner& owner,bool collect)
{
    const auto material=SigningMaterial(state,owner.id);
    if (!material || !material->psbt_version) return;
    auto& view=state.imported_views[owner.id];
    auto& signatures=view.signatures;
    auto& controls=facts.transaction_document;
    const auto current=[&state,&host,owner,material] {
        return !state.local_document.dialog.open && !state.local_document.close_pending
            && state.actions.kind==DocumentDialog::None
            && CurrentInspectionOwner(state,host)==owner && SigningMaterial(state,owner.id)==material;
    };
    const bool can_merge=host.LocalDocuments() && !BroadcastFreezes(state,owner.id)
        && !state.daemon_operation && !state.daemon_unlock.owner
        && (collect || state.documents.Documents().size()<32);
    const bool merge_controls=collect || !FeeReplacement(state,owner.id);
    if (merge_controls) {
        Button(state,controls,theme,"merge","Merge signed file",can_merge,[&state,&host,current,collect] {
            return current() && BeginLocalDocument(state,host,LocalDocumentMode::Merge,{},collect);
        });
        theme::SameLineForAction("Merge signed text");
        Button(state,controls,theme,"merge-text","Merge signed text",can_merge && c::native_input,[&state,&host,current,collect] {
            return current() && BeginLocalDocument(state,host,LocalDocumentMode::MergeText,{},collect);
        });
        theme::SameLineForAction("Copy PSBT text");
    }
    Button(state,controls,theme,"copy-psbt-text","Copy PSBT text",c::native_input,[&state,&host,current,owner,material] {
        if (!current()) return false;
        auto data=protocol::TransactionBase64(material->wire);
        if (data.empty()) return false;
        const a::Request request{a::Command::Component,owner.id,a::Section::Transactions,
            c::CopyIdentifier{"PSBT",std::move(data),true}};
        if (!host.CanSubmit(request).available) return false;
        const auto receipt=host.Submit(request);
        if (!receipt.accepted) state.notice="Clipboard export is unavailable. Save the PSBT to a file.";
        return receipt.accepted;
    });
    if (material!=owner.content) Button(state,controls,theme,"save-collected-psbt","Save collected PSBT",host.LocalDocuments(),
        [&state,&host,current] {
            return current() && BeginLocalDocument(state,host,LocalDocumentMode::SaveSigningMaterial);
        });
    Button(state,controls,theme,"psbt-qr-export","Show PSBT QR",host.LocalDocuments(),[&state,&host,current]{
        return current() && BeginLocalDocument(state,host,LocalDocumentMode::ShowQr);
    });
    if (merge_controls) {
        theme::SameLineForAction("Import signature QR");
        Button(state,controls,theme,"psbt-qr-import","Import signature QR",can_merge,[&state,&host,current,collect]{
            return current() && BeginLocalDocument(state,host,LocalDocumentMode::MergeQr,{},collect);
        });
    }
    Button(state,controls,theme,"signature-progress",signatures.open ? "Hide signature progress" : "Signature progress",true,
        [&signatures,&view,current] {
            if (!current()) return false;
            signatures.open=!signatures.open;
            ++view.revision;
            return true;
        });
    if (!signatures.open) return;
    const auto& progress=SignatureProgressFor(state,owner.id,material);
    if (!progress.error.empty()) {
        Text(state,"signature-progress-error","Signature progress",progress.error,SemanticRole::Alert);
        return;
    }
    const auto satisfied=std::ranges::count_if(progress.inputs,&protocol::InputSignatureProgress::Satisfied);
    const bool unknown=std::ranges::any_of(progress.inputs,[](const auto& input){return !input.unknown.empty();});
    Text(state,"signature-completion","Signature progress",std::string(progress.Complete() ? "Complete" : unknown ? "Unknown" : "Partial")
        +" · "+std::to_string(satisfied)+" of "+std::to_string(progress.inputs.size())+" input policies satisfied");
    if (progress.inputs.empty()) return;
    const auto page=signatures.page;
    const auto& input=progress.inputs.at(page);
    const auto title="Input "+std::to_string(page+1)+" of "+std::to_string(progress.inputs.size());
    Text(state,"signature-input","Input signature progress",input.unknown.empty()
        ? title+" · "+std::to_string(input.Verified())+" verified · "+std::to_string(input.threshold)+" required"
        : title+" · Unknown · "+input.unknown);
    for (size_t i=0;i<input.signers.size();++i) {
        ImGui::PushID(int(i));
        const auto& signer=input.signers[i];
        Text(state,"signature-public-key","Signer public key",signer.key+(signer.verified ? " · Verified" : " · No signature"));
        ImGui::PopID();
    }
    if (progress.inputs.size()>1) {
        const auto turn=[&view,&signatures,current,page](size_t next) {
            if (!current() || !signatures.open || signatures.page!=page) return false;
            signatures.page=next;
            ++view.revision;
            return true;
        };
        Button(state,controls,theme,"signature-previous","Previous signature input",page>0,[turn,page]{return turn(page-1);});
        ImGui::SameLine();
        Button(state,controls,theme,"signature-next","Next signature input",page+1<progress.inputs.size(),[turn,page]{return turn(page+1);});
    }
}

void DrawImportedDocument(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme,float scale)
{
    const auto* doc=state.documents.Active();
    if (!doc || !doc->imported) return;
    const auto owner=CurrentInspectionOwner(state,host);
    if (!owner) {
        Text(state,"document-hidden","Transaction privacy","Document details hidden.");
        return;
    }
    const auto& imported=*doc->imported;
    const auto& decoded=*imported.parsed;
    const auto& funding=imported.funding;
    const bool fee_replacement=FeeReplacement(state,doc->id);
    auto& view=state.imported_views[doc->id];
    auto& controls=facts.transaction_document;
    imkit::PushTextRole(theme,imkit::TextRole::Heading);
    const auto name=decoded.psbt_version ? "PSBT v"+std::to_string(*decoded.psbt_version) : std::string(protocol::DocumentFormatName(decoded.format));
    Text(state,"document-kind","Local document",name);
    imkit::PopTextRole();
    Text(state,"document-network","Display network","Display network: "+std::string(protocol::DocumentNetworkName(imported.network)));
    if (const auto& replacement=view.replacement) {
        const auto& original=*replacement->original->transaction;
        if (const auto& child=replacement->child_package) {
            const auto& package=child->package;
            const auto& output=replacement->proof.recipients.front();
            const auto address=protocol::OutputAddress(output.script,imported.network);
            const auto* finalized=FinalizedMaterial(state,doc->id);
            const size_t child_vsize=finalized ? finalized->final.vsize : child->estimated_vsize;
            const auto fee=replacement->proof.fee.sats();
            const auto combined=package.base_fee.sats()+fee;
            const auto combined_vsize=package.vsize+child_vsize;
            const std::string quality=finalized ? "actual BIP141" : "estimated";
            Text(state,"child-outpoint","Exact parent outpoint","Parent outpoint: "+original.txid->hex()+":"+std::to_string(child->output));
            Text(state,"child-parent-witness","Exact parent witness transaction identifier","Parent wtxid: "+original.wtxid->hex());
            Text(state,"child-destination","Owned child payment destination",
                "Wallet-owned destination: "+address.value_or("Unknown")+" · "+std::to_string(output.value.sats())+" sat");
            Text(state,"child-ancestors","Verified ancestors including parent",
                "Parent and ancestors: "+std::to_string(package.ancestors.size())+" unique transactions · "
                +std::to_string(package.base_fee.sats())+" sat base fees · "+std::to_string(package.vsize)+" BIP141 vB");
            Text(state,"child-own-fee","Child fee and size",
                "Child: "+std::to_string(fee)+" sat · "+std::to_string(child_vsize)+" vB ("+quality+") · "
                +std::to_string(fee)+"/"+std::to_string(child_vsize)+" sat/vB");
            Text(state,"child-package-fee","Effective package rate",
                "Package: "+std::to_string(combined)+" sat · "+std::to_string(combined_vsize)+" vB ("+quality+") · "
                +std::to_string(combined)+"/"+std::to_string(combined_vsize)+" sat/vB");
            Text(state,"child-cluster-policy","Observed node policy limits",
                "Node cluster: "+std::to_string(package.cluster.size())+"/"+std::to_string(package.cluster_count_limit)
                +" transactions · "+std::to_string((package.cluster_weight+3)/4)+"/"+std::to_string(package.cluster_vsize_limit)
                +" adjusted vB · mempool floor "+std::to_string(package.mempool_floor.sats())+" sat/kvB");
            Text(state,"child-snapshot","Package accounting source",
                "Last verified node package. Base fees exclude operator adjustments. Live eligibility and node policy are checked separately.");
        } else {
            Text(state,"replacement-original","Original payment","Original txid: "+original.txid->hex());
            Text(state,"replacement-original-witness","Original witness transaction identifier","Original wtxid: "+original.wtxid->hex());
            Text(state,"replacement-fee","Original and proposed fee","Fee: "+std::to_string(replacement->original_fee.sats())
                +" → "+std::to_string(replacement->proof.fee.sats())+" sat");
        }
        if (replacement->cancellation) {
            const auto& output=replacement->proof.recipients.front();
            const auto address=protocol::OutputAddress(output.script,imported.network);
            Text(state,"cancellation-owned-destination","Verified wallet-owned destination",
                "Wallet-owned destination: "+address.value_or("Unknown"));
            Text(state,"cancellation-self-amount","Self-payment amount",
                "Self-payment: "+std::to_string(output.value.sats())+" sat. Separate change: 0 sat.");
            Text(state,"cancellation-conflict","Proved conflicting inputs",
                "Spends all "+std::to_string(original.inputs.size())+" original inputs.");
            Text(state,"cancellation-warning","Cancellation race",
                "The original may confirm first and node policy may refuse the competing transaction. Cancellation is never guaranteed.");
        } else if (!replacement->child_package) {
            Text(state,"replacement-change","Original and proposed change","Change: "+std::to_string(replacement->original_change.sats())
                +" → "+std::to_string(replacement->proposed_change.sats())+" sat");
            Text(state,"replacement-status","Unsigned replacement status",
                "Unsigned replacement. Signed size and realized fee rate are unknown. Review and approve this draft separately.");
        }
        if (Button(state,controls,theme,"replacement-view-original",replacement->child_package ? "View parent transaction" : "View original payment",state.documents.Documents().size()<32,
            [&state,&host,owner,id=doc->id] {
                if (CurrentInspectionOwner(state,host)!=owner) return false;
                const auto view=state.imported_views.find(id);
                if (view==state.imported_views.end() || !view->second.replacement) return false;
                const auto& source=*view->second.replacement;
                auto candidate=state.documents;
                const auto opened=candidate.OpenImportedDocument({source.original,source.proof.network});
                if (!opened || (state.recovery && !state.recovery->CanAdmitPublicDocuments(candidate))) return false;
                state.documents=std::move(candidate);
                RestoreDocumentFocus(state,opened);
                return true;
            })) return;
    }
    if (!decoded.psbt_version && decoded.transaction) DrawBroadcastStatus(state,host,facts,theme);
    if (DrawTransactionEditor(state,host,facts,theme)) return;
    if (DrawSigningReview(state,host,facts,theme)) return;
    if (funding) {
        Text(state,"created-funding","Created unsigned transaction","Unsigned transaction created from Send revision "
            + std::to_string(funding->draft_revision.value) + ". Matched previous-output proofs: "
            + std::to_string(funding->evidence.inputs.size()) + ".");
        if (funding->evidence.offline_change) Text(state,"created-change","Created change evidence",
            "The last output matches the saved internal change descriptor. Allocation is unconfirmed.");
    }
    Button(state,controls,theme,"save",fee_replacement ? "Save unsigned PSBT" : "Save original",host.LocalDocuments()!=nullptr,[&state,&host,owner] {
        return CurrentInspectionOwner(state,host)==owner && BeginLocalDocument(state,host,LocalDocumentMode::Save);
    });
    if (decoded.psbt_version && host.LocalDocuments())
        DrawSignatureExchange(state,host,facts,theme,*owner,false);
    if (decoded.psbt_version && host.LocalDocuments() && !fee_replacement) {
        if (Button(state,controls,theme,"finalize","Finalize locally",state.documents.Documents().size()<32
            && !BroadcastFreezes(state,doc->id),[&state,&host,owner,id=doc->id] {
            const auto* active=state.documents.Active();
            if (CurrentInspectionOwner(state,host)!=owner || !active || !active->imported
                || BroadcastFreezes(state,id) || FeeReplacement(state,id) || state.local_document.dialog.open
                || (c::native_input && c::native_input->NativeDialogActive())) return false;
            auto result=protocol::FinalizeOfflineDraft(*active->imported->parsed);
            if (!result.document) {
                state.notice=std::move(result.error);
                ++state.imported_views[id].revision;
                return false;
            }
            if (!OpenLocalData(state,std::move(result.document),active->imported->network,state.notice)) return false;
            state.notice="Raw transaction finalized locally. Save original to export. Live relay status is unknown.";
            return true;
        })) return;
        if (!state.notice.empty()) Text(state,"offline-signature-status","Local PSBT operation",state.notice);
        if (view.review_evidence) {
            Button(state,controls,theme,"record-review","Record local review",!view.reviewed,[&state,&host,owner,id=doc->id] {
                return CurrentInspectionOwner(state,host)==owner && !state.local_document.dialog.open && RecordReview(state,id);
            });
            if (!view.review_status.empty()) Text(state,"offline-review-status","Local review status",view.review_status);
        } else if (!funding) Text(state,"offline-review-status","Local review status",
            "Local review unavailable. Independent payment and wallet proofs are Unknown.");
    }
    if (decoded.descriptor) {
        for (size_t index=0;index<decoded.descriptor->descriptors.size();++index) {
            ImGui::PushID(int(index));
            const auto& branch=decoded.descriptor->descriptors[index];
            Text(state,"descriptor-address","Public descriptor sample address",branch.address);
            Text(state,"descriptor-script","Public descriptor sample script",branch.script);
            ImGui::PopID();
        }
        Text(state,"descriptor-confidence","Descriptor evidence","Public descriptor inspected. Wallet ownership and history are unknown.");
    }
    if (!decoded.psbt_version && !decoded.descriptor) ImGui::SameLine();
    DrawTransactionWorkbench(state,host,facts,theme,scale);
}
}
