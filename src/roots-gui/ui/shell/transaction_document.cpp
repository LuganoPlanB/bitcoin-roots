// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include <algorithm>
#include <bit>
#include <charconv>
#include <sodium.h>

namespace roots::gui::ui::shell {
namespace {
namespace a=app;
namespace c=components;
using c::ax::SemanticAction;
using c::ax::SemanticRole;
using protocol::DisplayNetwork;
constexpr const char* DIALOG="Local transaction document###local-transaction-document";
constexpr size_t HEX_PAGE=256;

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
bool Current(const State& state)
{
    return Permitted(state) && state.local_document.dialog.open && state.local_document.context==Context(state);
}
void Status(LocalDocumentPanel& panel,std::string message)
{
    panel.error=std::move(message);
    ++panel.revision;
}
void Retire(State& state,app::Requests& host)
{
    if (auto* documents=host.LocalDocuments()) documents->Cancel();
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
void NetworkPicker(State& state,LocalDocumentPanel& panel,bool enabled)
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
    if (choices) state.presentation.Defer(*choices,[&state,pick,enabled](const auto& request) {
        if (!enabled || !Current(state)) return false;
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
    if (!Current(state)) return;
    auto& panel=state.local_document;
    if (!reply.parsed.document) {
        if (panel.mode==LocalDocumentMode::Merge && (reply.parsed.error==protocol::DocumentError::Malformed
            || reply.parsed.error==protocol::DocumentError::Duplicate))
            RevokeReview(state.imported_views[panel.context.document],"The supplied PSBT contains malformed or contradictory fields.");
        Status(panel,reply.file_error.empty() ? protocol::DocumentErrorMessage(reply.parsed) : std::move(reply.file_error));
        return;
    }
    if (panel.mode==LocalDocumentMode::Merge) {
        auto* active=state.documents.Active();
        if (!active || !active->imported) return;
        auto& previous=state.imported_views[active->id];
        auto merged=protocol::MergeOfflineSignatures(*active->imported->parsed,*reply.parsed.document);
        if (!merged.document) {
            if (!merged.review_conflict.empty()) RevokeReview(previous,merged.review_conflict);
            Status(panel,std::move(merged.error));
            return;
        }
        if (previous.reviewed) {
            auto conflict=protocol::CompareTransactionReview(*previous.reviewed,*merged.document);
            if (!conflict.empty()) {
                RevokeReview(previous,conflict);
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

void DrawBytes(State& state,std::map<std::string,c::ItemFacts>& facts,const imkit::Theme& theme,
    ImportedView& view,const a::ImportedDocument& imported)
{
    const auto& parsed=*imported.parsed;
    const bool wire=imported.representation==a::DocumentRepresentation::WireHex;
    const bool binary=wire || parsed.format==protocol::DocumentFormat::RawBinary || parsed.format==protocol::DocumentFormat::PsbtBinary;
    const std::string_view data=wire ? parsed.wire : parsed.source;
    const size_t page_size=binary ? HEX_PAGE/2 : HEX_PAGE;
    const size_t pages=(data.size()+page_size-1)/page_size;
    view.bytes_page=pages ? std::min(view.bytes_page,pages-1) : 0;
    const auto segment=data.substr(view.bytes_page*page_size,page_size);
    Text(state,"shown-representation","Active representation",wire ? "Wire hex · original bytes" : protocol::DocumentFormatName(parsed.format));
    Button(state,facts,theme,"bytes-back","Previous bytes",view.bytes_page>0,[&view] {--view.bytes_page;++view.revision;return true;});
    ImGui::SameLine();
    Button(state,facts,theme,"bytes-next","Next bytes",view.bytes_page+1<pages,[&view] {++view.bytes_page;++view.revision;return true;});
    ImGui::SameLine();
    ImGui::Text("%zu / %zu",view.bytes_page+1,pages);
    auto shown=binary ? protocol::TransactionHex(segment) : std::string(segment);
    if (binary) for (size_t next=64; next<shown.size(); next+=65) shown.insert(next,1,'\n');
    Text(state,"document-bytes","Document bytes",shown);
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
    if (((mode==LocalDocumentMode::Save || mode==LocalDocumentMode::Merge) && (!active || !active->imported))
        || (mode==LocalDocumentMode::Merge && !active->imported->parsed->psbt_version)
        || (mode!=LocalDocumentMode::Save && state.documents.Documents().size()>=32)) return false;
    panel={};
    panel.mode=mode;
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
    if (panel.dialog.open && !Current(state)) Retire(state,host);
    auto reply=documents->Poll(Current(state) ? std::optional{panel.context} : std::nullopt);
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
    const bool save=panel.mode==LocalDocumentMode::Save, clipboard=panel.mode==LocalDocumentMode::Text;
    const bool merge=panel.mode==LocalDocumentMode::Merge;
    const bool busy=documents->Busy();
    auto* input=c::native_input;
    const auto clipboard_id=ImGui::GetID("inspect-transaction-clipboard");
    if (panel.clipboard_pending && input) {
        auto text=input->Paste(clipboard_id,"",0,0,0,protocol::MAX_TRANSACTION_SOURCE+1);
        if (text) {
            panel.clipboard_pending=false;
            if (!Current(state) || !documents->ReadText(panel.context,std::move(*text),panel.network)) {
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
    else if (!save) {
        Text(state,"import-purpose","Local document types",clipboard
            ? "Inspect clipboard text as transaction hex, Base64 PSBT, or a public descriptor."
            : "Open a local raw transaction, PSBT, or public descriptor. Format is checked from the contents.");
        NetworkPicker(state,panel,!busy && !panel.clipboard_pending && !panel.file_dialog);
    }
    if (!clipboard) {
        ImGui::TextUnformatted("Local file path");
        ImGui::SetNextItemWidth(-1);
        ImGui::BeginDisabled(busy || panel.file_dialog!=0);
        if (c::TextInput("##local-transaction-path",nullptr,panel.path.data(),panel.path.size(),panel.edited_path,0,&panel.cursor))
            Status(panel,{});
        const auto path=state.presentation.Interact("Local file path",SemanticRole::TextField,SemanticAction::SetValue|SemanticAction::Focus,
            busy || panel.file_dialog,false,panel.path.data(),"Choose an absolute local file path.");
        state.presentation.BindTextPosition(ImGui::GetItemID(),panel.path.data(),panel.cursor);
        ImGui::EndDisabled();
        if (path) state.presentation.Defer(*path,[&state,&panel](const auto& request){
            if (!Current(state)) return false;
            if (request.action==SemanticAction::Focus) return true;
            if (!c::SetExternalBuffer(panel.path,panel.edited_path,request.value)) return false;
            Status(panel,{});
            return true;
        });
        if (panel.file_dialog && input) {
            const auto chosen=input->PollFile(panel.file_dialog);
            if (chosen.outcome!=c::NativeOutcome::Pending) {
                panel.file_dialog=0;
                if (Current(state) && chosen.outcome==c::NativeOutcome::Chosen
                    && c::SetExternalBuffer(panel.path,panel.edited_path,chosen.path)) Status(panel,{});
                else if (chosen.outcome!=c::NativeOutcome::Cancelled && !chosen.detail.empty()) Status(panel,chosen.detail);
            }
        }
        Button(state,facts.local_document,theme,"browse","Browse local files",!busy && !panel.file_dialog && input && input->FileChooserAvailable(),
            [&state,&panel,input,save] {
                if (!Current(state) || !input) return false;
                panel.file_dialog=input->ChooseFile(panel.path.data(),save);
                return panel.file_dialog!=0;
            });
    }
    if (!panel.error.empty()) Text(state,"local-document-error","Local document status",panel.error,SemanticRole::Alert);
    if (busy) Text(state,"local-document-reading","Local document progress","Reading and validating the document…");
    Button(state,facts.local_document,theme,"cancel","Cancel",true,[&state,&host]{Retire(state,host);return true;});
    ImGui::SameLine();
    if (clipboard) Button(state,facts.local_document,theme,"inspect-clipboard","Inspect clipboard",input && !busy && !panel.clipboard_pending,
        [&state,&panel,input,clipboard_id] {
            if (!Current(state) || !input || !input->RequestInspection(clipboard_id)) return false;
            // Keep the native selection request in its admitted view until
            // it is consumed or that view is retired.
            panel.clipboard_pending=true;
            panel.error.clear();
            return true;
        },true);
    else Button(state,facts.local_document,theme,save ? "save-original" : "inspect-file",save ? "Save original bytes" : merge ? "Validate and merge" : "Inspect file",
        !busy && !panel.file_dialog && ValidLocalDocumentPath(panel.path.data()),[&state,&host,&panel,documents,save] {
            if (!Current(state)) return false;
            if (!save) {
                if (!documents->ReadFile(panel.context,panel.path.data(),panel.network)) return false;
                Status(panel,{});
                return true;
            }
            const auto* doc=state.documents.Active();
            if (!doc || !doc->imported) return false;
            const auto error=SaveOriginalDocument(panel.path.data(),*doc->imported->parsed);
            if (!error.empty()) {Status(panel,error);return false;}
            state.notice="Original bytes saved to a new local file.";
            Retire(state,host);
            return true;
        },true);
    state.presentation.EndGroup();
    imkit::EndDialog();
}

void DrawImportedDocument(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme,float)
{
    const auto* doc=state.documents.Active();
    if (!doc || !doc->imported) return;
    if (!Permitted(state)) {
        Text(state,"document-hidden","Transaction privacy","Document details hidden.");
        return;
    }
    const auto& imported=*doc->imported;
    const auto& decoded=*imported.parsed;
    auto& view=state.imported_views[doc->id];
    auto& controls=facts.transaction_document;
    imkit::PushTextRole(theme,imkit::TextRole::Heading);
    const auto name=decoded.psbt_version ? "PSBT v"+std::to_string(*decoded.psbt_version) : std::string(protocol::DocumentFormatName(decoded.format));
    Text(state,"document-kind","Local document",name);
    imkit::PopTextRole();
    Text(state,"document-network","Display network","Display network: "+std::string(protocol::DocumentNetworkName(imported.network)));
    Button(state,controls,theme,"save","Save original",host.LocalDocuments()!=nullptr,[&state,&host] {
        return BeginLocalDocument(state,host,LocalDocumentMode::Save);
    });
    if (!decoded.wire.empty()) {
        ImGui::SameLine();
        Button(state,controls,theme,"representation",imported.representation==a::DocumentRepresentation::WireHex?"Show source":"Show wire hex",
            true,[&state,id=doc->id] {
                auto* active=state.documents.Active();
                if (!Permitted(state) || !active || active->id!=id || !active->imported) return false;
                auto& repr=active->imported->representation;
                repr=repr==a::DocumentRepresentation::WireHex ? a::DocumentRepresentation::Source : a::DocumentRepresentation::WireHex;
                auto& view=state.imported_views[id];
                view.bytes_page=0;
                ++view.revision;
                return true;
            });
    }
    if (decoded.psbt_version && host.LocalDocuments()) {
        Button(state,controls,theme,"merge","Merge signed file",state.documents.Documents().size()<32,[&state,&host] {
            return BeginLocalDocument(state,host,LocalDocumentMode::Merge);
        });
        ImGui::SameLine();
        if (Button(state,controls,theme,"finalize","Finalize locally",state.documents.Documents().size()<32,[&state,id=doc->id] {
            const auto* active=state.documents.Active();
            if (!Permitted(state) || !active || active->id!=id || !active->imported) return false;
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
            Button(state,controls,theme,"record-review","Record local review",!view.reviewed,[&state,id=doc->id] {
                return RecordReview(state,id);
            });
            if (!view.review_status.empty()) Text(state,"offline-review-status","Local review status",view.review_status);
        } else Text(state,"offline-review-status","Local review status",
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
    } else {
        const auto& tx=*decoded.transaction;
        const auto height=tx.locktime ? std::to_string(*tx.locktime) : std::string("Unknown");
        Text(state,"transaction-header","Transaction header",std::to_string(tx.inputs.size())+" inputs · "+std::to_string(tx.outputs.size())
             +" outputs · Version "+std::to_string(std::bit_cast<int32_t>(tx.version))+" · Locktime "+height);
        if (tx.txid) Text(state,"raw-txid","Raw transaction ID",tx.txid->hex());
        if (tx.fee) Text(state,"transaction-fee","Offline calculated fee","Fee: "+tx.fee->btc()+" BTC · matched supplied prevouts");
        else Text(state,"transaction-fee","Offline calculated fee","Fee: Unknown");
        Text(state,"document-confidence","Proof and approval",view.proved_change_output
            ? "Offline inspection. Change matches the cached wallet policy. Live spendability and relay status are unknown."
            : "Offline inspection. Live spendability and change intent are unknown.");
        if (view.review_commitment)
            Text(state,"transaction-commitment","Unsigned transaction commitment",view.review_commitment->hex());
        Button(state,controls,theme,"switch-io",view.inspect_inputs?"Inspect outputs":"Inspect inputs",
            view.inspect_inputs ? !tx.outputs.empty() : !tx.inputs.empty(),[&view]{view.inspect_inputs=!view.inspect_inputs;++view.revision;return true;});
        const auto count=view.inspect_inputs ? tx.inputs.size() : tx.outputs.size();
        auto& selected=view.inspect_inputs ? view.input : view.output;
        if (count) {
            ImGui::SameLine();
            Button(state,controls,theme,"previous","Previous",selected>0,[&selected,&view]{--selected;++view.revision;return true;});
            ImGui::SameLine();
            Button(state,controls,theme,"next","Next",selected+1<count,[&selected,&view]{++selected;++view.revision;return true;});
            ImGui::SameLine();
            ImGui::Text("%zu / %zu",selected+1,count);
            if (view.inspect_inputs) {
                const auto& input=tx.inputs.at(selected);
                Text(state,"input-outpoint","Selected input outpoint",input.outpoint.txid.hex()+":"+std::to_string(input.outpoint.index));
                const char* evidence="Previous output: Unknown";
                if (input.evidence==protocol::PrevoutEvidence::Supplied) evidence="Previous output: supplied without matching transaction proof";
                if (input.evidence==protocol::PrevoutEvidence::TxidMatched) evidence="Previous output: matches the supplied previous transaction ID";
                if (input.evidence==protocol::PrevoutEvidence::Conflict) evidence="Previous output: conflicting metadata; value unknown";
                Text(state,"prevout-evidence","Previous output evidence",evidence);
            } else {
                const auto& output=tx.outputs.at(selected);
                Text(state,"output-amount","Selected output amount",output.value.btc()+" BTC");
                const auto address=protocol::OutputAddress(output.script,imported.network);
                Text(state,"output-address","Selected output address",address.value_or("Address unavailable for this script or display network"));
                const auto matched=view.wallet && protocol::DocumentNetwork(view.wallet->network)==imported.network
                    && view.policy_scripts.contains(output.script);
                Text(state,"output-ownership","Wallet script evidence",view.proved_change_output==selected
                    ? "Intended change matches the separately read internal wallet descriptor."
                    : matched ? "Exact match to previously read wallet script; intent unknown." : "Wallet ownership: Unknown");
                Text(state,"output-script","Selected output script",output.script.size()<=512 ? output.script : output.script.substr(0,512)+"…");
            }
        }
    }
    ImGui::Separator();
    DrawBytes(state,controls,theme,view,imported);
}
}
