// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include <algorithm>
#include <sodium.h>

namespace roots::gui::ui::shell {
namespace {
namespace a=app;
namespace c=components;
using c::ax::SemanticAction;
using c::ax::SemanticRole;
using protocol::DisplayNetwork;
constexpr const char* DIALOG="Local transaction document###local-transaction-document";

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
bool Current(const State& state,app::Requests& host)
{
    const auto& panel=state.local_document;
    if (!Permitted(state) || !panel.dialog.open || panel.context!=Context(state)) return false;
    if (panel.owner && CurrentInspectionOwner(state,host)!=panel.owner) return false;
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
    sodium_memzero(panel.path.data(),panel.path.size());
    if (panel.edited_path && !panel.edited_path->empty()) sodium_memzero(panel.edited_path->data(),panel.edited_path->size());
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
void Adopt(State& state,app::Requests& host,LocalDocumentReply reply)
{
    if (!Current(state,host)) return;
    auto& panel=state.local_document;
    if (!reply.parsed.document) {
        if (panel.mode==LocalDocumentMode::Merge && (reply.parsed.error==protocol::DocumentError::Malformed
            || reply.parsed.error==protocol::DocumentError::Duplicate)) {
            RevokeReview(state.imported_views[panel.context.document],"The supplied PSBT contains malformed or contradictory fields.");
            RevokeSigningReview(state,panel.context.document,"The supplied merge contains malformed or contradictory fields.");
        }
        Status(panel,reply.file_error.empty() ? protocol::DocumentErrorMessage(reply.parsed) : std::move(reply.file_error));
        return;
    }
    if (panel.mode==LocalDocumentMode::Merge) {
        auto* active=state.documents.Active();
        if (!active || !active->imported) return;
        auto& previous=state.imported_views[active->id];
        auto merged=protocol::MergeOfflineSignatures(*active->imported->parsed,*reply.parsed.document);
        if (!merged.document) {
            if (!merged.review_conflict.empty()) {
                RevokeReview(previous,merged.review_conflict);
                RevokeSigningReview(state,active->id,merged.review_conflict);
            }
            Status(panel,std::move(merged.error));
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
        state.imported_views.emplace(state.documents.ActiveId(),previous);
        state.notice=std::to_string(merged.satisfied)+" / "+std::to_string(merged.required)
            +" input policies verified. Previous PSBT and external source retained.";
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

bool BeginLocalDocument(State& state,app::Requests& host,LocalDocumentMode mode,std::string_view path)
{
    auto* documents=host.LocalDocuments();
    auto& panel=state.local_document;
    if (!documents || documents->Busy() || !Permitted(state) || panel.dialog.open || panel.close_pending
        || state.server.dialog.open || state.onboarding.dialog.open || state.actions.kind!=DocumentDialog::None
        || state.recovery_dialog.open || state.local_document_generation==UINT64_MAX || path.size()>4096)
        return false;
    const auto* active=state.documents.Active();
    const bool save=mode==LocalDocumentMode::Save || mode==LocalDocumentMode::SaveInspection;
    if (((save || mode==LocalDocumentMode::Merge) && (!active || !active->imported))
        || (mode==LocalDocumentMode::Merge && !active->imported->parsed->psbt_version)
        || (!save && state.documents.Documents().size()>=32)) return false;
    const auto owner=save || mode==LocalDocumentMode::Merge ? CurrentInspectionOwner(state,host) : std::nullopt;
    if ((save || mode==LocalDocumentMode::Merge) && !owner) return false;
    panel={};
    panel.mode=mode;
    panel.owner=owner;
    if (mode==LocalDocumentMode::Merge) panel.network=active->imported->network;
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
    if (panel.dialog.open && !Current(state,host)) Retire(state,host);
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
        if (!panel.dialog.open) Retire(state,host);
        return;
    }
    state.presentation.DialogOnly();
    const auto minimum=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
    state.presentation.Group("local-document-dialog","Local transaction document",SemanticRole::Dialog,minimum,
        {minimum.x+size.x,minimum.y+size.y});
    const bool byte_save=panel.mode==LocalDocumentMode::SaveInspection;
    const bool save=panel.mode==LocalDocumentMode::Save || byte_save, clipboard=panel.mode==LocalDocumentMode::Text;
    const bool merge=panel.mode==LocalDocumentMode::Merge;
    const bool busy=documents->Busy();
    auto* input=c::native_input;
    const auto clipboard_id=ImGui::GetID("inspect-transaction-clipboard");
    if (panel.clipboard_pending && input) {
        auto text=input->Paste(clipboard_id,"",0,0,0,protocol::MAX_TRANSACTION_SOURCE+1);
        if (text) {
            panel.clipboard_pending=false;
            if (!Current(state,host) || !documents->ReadText(panel.context,std::move(*text),panel.network)) {
                if (!text->empty()) sodium_memzero(text->data(),text->size());
                Status(panel,"Clipboard inspection was cancelled.");
            }
        } else if (!input->InspectionPending(clipboard_id)) {
            panel.clipboard_pending=false;
            Status(panel,"Clipboard inspection was cancelled or no bounded text was available.");
        }
    }
    if (merge) Text(state,"merge-purpose","External PSBT merge",
        "Choose a signed PSBT file. The transaction and every added signature will be verified before combining. The current file and tab are retained.");
    else if (byte_save && panel.inspection) {
        const auto& request=*panel.inspection;
        const auto description=std::string(protocol::InspectionBufferName(*request.owner.content,request.representation));
        Text(state,"save-representation","Export representation",description);
        if (request.selection) Text(state,"save-byte-range","Export byte range",
            "Offset "+std::to_string(request.selection->offset)+" · "+std::to_string(request.selection->length)+" bytes");
    } else if (!save) {
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
    if (clipboard) Button(state,facts.local_document,theme,"inspect-clipboard","Inspect clipboard",input && !busy && !panel.clipboard_pending,
        [&state,&host,&panel,input,clipboard_id] {
            if (!Current(state,host) || !input || !input->RequestInspection(clipboard_id)) return false;
            // Keep the native selection request in its admitted view until
            // it is consumed or that view is retired.
            panel.clipboard_pending=true;
            panel.error.clear();
            return true;
        },true);
    else Button(state,facts.local_document,theme,save ? (byte_save ? "save-inspection" : "save-original") : "inspect-file",save ? (byte_save ? "Save bytes to file" : "Save original bytes") : merge ? "Validate and merge" : "Inspect file",
        !busy && !panel.file_dialog && ValidLocalDocumentPath(panel.path.data()),[&state,&host,&panel,documents,save,byte_save] {
            if (!Current(state,host)) return false;
            if (!save) {
                if (!documents->ReadFile(panel.context,panel.path.data(),panel.network)) return false;
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
            } else error=SaveOriginalDocument(panel.path.data(),*doc->imported->parsed);
            if (!error.empty()) {Status(panel,error);return false;}
            state.notice=byte_save ? "Selected representation bytes saved to a new local file." : "Original bytes saved to a new local file.";
            Retire(state,host);
            return true;
        },true);
    state.presentation.EndGroup();
    imkit::EndDialog();
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
    auto& view=state.imported_views[doc->id];
    auto& controls=facts.transaction_document;
    imkit::PushTextRole(theme,imkit::TextRole::Heading);
    const auto name=decoded.psbt_version ? "PSBT v"+std::to_string(*decoded.psbt_version) : std::string(protocol::DocumentFormatName(decoded.format));
    Text(state,"document-kind","Local document",name);
    imkit::PopTextRole();
    Text(state,"document-network","Display network","Display network: "+std::string(protocol::DocumentNetworkName(imported.network)));
    if (DrawTransactionEditor(state,host,facts,theme)) return;
    if (DrawSigningReview(state,host,facts,theme)) return;
    if (funding) {
        Text(state,"created-funding","Created unsigned transaction","Unsigned transaction created from Send revision "
            + std::to_string(funding->draft_revision.value) + ". Matched previous-output proofs: "
            + std::to_string(funding->evidence.inputs.size()) + ".");
        if (funding->evidence.offline_change) Text(state,"created-change","Created change evidence",
            "The last output matches the saved internal change descriptor. Allocation is unconfirmed.");
    }
    Button(state,controls,theme,"save","Save original",host.LocalDocuments()!=nullptr,[&state,&host,owner] {
        return CurrentInspectionOwner(state,host)==owner && BeginLocalDocument(state,host,LocalDocumentMode::Save);
    });
    if (decoded.psbt_version && host.LocalDocuments()) {
        Button(state,controls,theme,"merge","Merge signed file",state.documents.Documents().size()<32,[&state,&host,owner] {
            return CurrentInspectionOwner(state,host)==owner && BeginLocalDocument(state,host,LocalDocumentMode::Merge);
        });
        ImGui::SameLine();
        if (Button(state,controls,theme,"finalize","Finalize locally",state.documents.Documents().size()<32,[&state,&host,owner,id=doc->id] {
            const auto* active=state.documents.Active();
            if (CurrentInspectionOwner(state,host)!=owner || !active || !active->imported
                || state.local_document.dialog.open || (c::native_input && c::native_input->NativeDialogActive())) return false;
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
