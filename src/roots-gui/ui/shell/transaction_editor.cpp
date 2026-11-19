// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"

#include <algorithm>
#include <charconv>
#include <sodium.h>

namespace roots::gui::ui::shell {
namespace {
namespace a=app;
namespace c=components;
using c::ax::SemanticAction;
using c::ax::SemanticRole;
using protocol::InspectionNode;
constexpr uint64_t MAX_MONEY=21000000ULL*100000000;

bool Visible(const State& state)
{
    return !state.privacy.hidden && !state.semantic_hide_toggle && !state.lock_panel.setup
        && (!state.recovery || (!state.recovery->Locked() && !state.recovery->Pending()));
}
bool Unobstructed(const State& state)
{
    return Visible(state) && state.actions.kind==DocumentDialog::None && !state.local_document.dialog.open
        && !state.local_document.close_pending && !state.server.dialog.open && !state.onboarding.dialog.open
        && !state.recovery_dialog.open && !state.close_tab_popup && !state.close_document_popup
        && (!c::native_input || !c::native_input->NativeDialogActive());
}
bool OwnerExists(const State& state,a::Requests& host,const InspectionOwner& owner)
{
    const auto* document=state.documents.Find(owner.id);
    const auto* connection=host.Connection();
    if (!Visible(state) || !document || !document->imported || document->observation==a::ObservationState::Locked
        || document->draft_revision!=owner.draft || document->imported->network!=owner.network
        || document->imported->parsed!=owner.content) return false;
    return (!connection ? owner.session==0 && owner.profile==0 && !owner.offline
        : owner.session==connection->Session() && owner.profile==connection->Revision()
            && owner.offline==connection->Offline() && !connection->PrivacyLocked())
        && owner.epoch==(state.recovery ? state.recovery->Gate().Epoch() : LockEpoch{});
}

void Clear(TransactionEditor& editor)
{
    for (auto* field:{&editor.version,&editor.locktime,&editor.outpoint,&editor.sequence,&editor.amount}) {
        sodium_memzero(field->text.data(),field->text.size());
        if (!field->saved.empty()) sodium_memzero(field->saved.data(),field->saved.size());
        if (field->external_buffer && !field->external_buffer->empty())
            sodium_memzero(field->external_buffer->data(),field->external_buffer->size());
    }
    if (!editor.destination.empty()) sodium_memzero(editor.destination.data(),editor.destination.size());
    if (!editor.saved_destination.empty()) sodium_memzero(editor.saved_destination.data(),editor.saved_destination.size());
    if (editor.external_destination && !editor.external_destination->empty())
        sodium_memzero(editor.external_destination->data(),editor.external_destination->size());
}

void Load(TransactionEditor& editor,InspectionNode node,size_t index)
{
    const auto& tx=*editor.owner.content->transaction;
    editor.node=node;
    editor.index=index;
    editor.version=c::Edit(std::to_string(tx.version));
    editor.locktime=c::Edit(std::to_string(*tx.locktime));
    if (node==InspectionNode::Input) {
        editor.outpoint=c::Edit(a::CoinKey(tx.inputs.at(index).outpoint));
        editor.sequence=c::Edit(std::to_string(tx.inputs.at(index).sequence));
    } else if (node==InspectionNode::Output) {
        const auto& output=tx.outputs.at(index);
        editor.amount=c::Edit(std::to_string(output.value.sats()));
        editor.saved_destination=protocol::OutputAddress(output.script,editor.owner.network).value_or(output.script);
        editor.destination.assign(20001,'\0');
        std::ranges::copy(editor.saved_destination,editor.destination.begin());
        editor.external_destination.reset();
    }
}
bool Dirty(const TransactionEditor& editor)
{
    if (editor.node==InspectionNode::Input) return editor.outpoint.dirty() || editor.sequence.dirty();
    if (editor.node==InspectionNode::Output) return editor.amount.dirty()
        || std::string_view(editor.destination.data())!=editor.saved_destination;
    return editor.version.dirty() || editor.locktime.dirty();
}

std::optional<uint64_t> Decimal(std::string_view text,uint64_t max)
{
    uint64_t n{};
    const auto scan=std::from_chars(text.data(),text.data()+text.size(),n);
    if (text.empty() || scan.ec!=std::errc{} || scan.ptr!=text.data()+text.size() || n>max) return {};
    return n;
}
std::optional<protocol::TransactionEdit> ReadForm(const TransactionEditor& editor)
{
    protocol::TransactionEdit edit;
    if (editor.node==InspectionNode::Overview) {
        const auto version=Decimal(editor.version.draft(),UINT32_MAX),locktime=Decimal(editor.locktime.draft(),UINT32_MAX);
        if (!version || !locktime) return {};
        edit.version=uint32_t(*version);
        edit.locktime=uint32_t(*locktime);
    } else if (editor.node==InspectionNode::Input) {
        const auto point=a::ParseCoinKey(editor.outpoint.draft());
        const auto sequence=Decimal(editor.sequence.draft(),UINT32_MAX);
        if (!point || !sequence) return {};
        edit.input=protocol::EditInput{editor.index,*point,uint32_t(*sequence)};
    } else {
        const auto amount=Decimal(editor.amount.draft(),MAX_MONEY);
        if (!amount) return {};
        const auto destination=std::string_view(editor.destination.data());
        auto script=protocol::AddressScript(destination,editor.owner.network);
        if (!script) script=std::string(destination);
        edit.output=protocol::EditOutput{editor.index,{Money::Satoshis(int64_t(*amount)),std::move(*script)}};
    }
    return edit;
}

a::Request Request(a::Command command,uint64_t id,const TransactionEditor* editor=nullptr,protocol::TransactionEdit fields={})
{
    a::Request request{command,id};
    if (editor) request.edit=a::TransactionEditRequest{editor->generation,editor->owner.draft,std::move(fields)};
    return request;
}

a::Receipt Adopt(State& state,a::Requests& host,const TransactionEditor& editor,protocol::EditedTransaction changed,
    const protocol::TransactionEdit& edit)
{
    const auto id=editor.owner.id;
    const auto generation=editor.generation;
    const auto* original=state.documents.Find(id);
    if (!original || CurrentInspectionOwner(state,host)!=editor.owner || !Unobstructed(state))
        return {false,"The transaction edit no longer matches the active revision."};
    const auto existing=state.edited_origins.find(id);
    const bool working=existing!=state.edited_origins.end();
    auto lineage=working ? existing->second : EditedOrigin{id,original->draft_revision,original->imported->parsed};
    if (const auto source=state.imported_views.find(id);source!=state.imported_views.end() && source->second.replacement)
        lineage.fee_replacement=true;
    lineage.affected=changed.affected;
    auto proposed=state.documents;
    auto target=id;
    if (!working) {
        target=proposed.OpenImportedDocument({changed.document,editor.owner.network});
        if (!target) return {false,"Close a document before creating a working copy. The original and fields are retained."};
        auto* document=proposed.Find(target);
        document->name=changed.document->psbt_version ? "Edited PSBT v"+std::to_string(*changed.document->psbt_version) : "Edited transaction";
        document->original_name=document->name;
    } else {
        auto* document=proposed.Find(target);
        if (document->draft_revision.value==UINT64_MAX) return {false,"The working document revision limit was reached."};
        document->imported->parsed=changed.document;
        document->imported->representation=a::DocumentRepresentation::Source;
        ++document->draft_revision.value;
    }
    if (state.recovery && (!state.recovery->CanAdmitPublicDocuments(proposed)
        || (state.recovery->Enabled() && !state.recovery->Checkpoint(proposed))))
        return {false,"The new working revision could not be saved. The original and pending fields are retained."};

    auto node=editor.node;
    auto index=editor.index;
    if (edit.reorder) {
        node=edit.reorder->group;
        index=edit.reorder->destination;
    }
    RevokeSigningReview(state,id,"Changed "+changed.affected+". Review and approve the working revision again.");
    Clear(state.transaction_edits.at(id));
    state.transaction_edits.erase(id);
    state.documents=std::move(proposed);
    state.edited_origins[target]=std::move(lineage);
    state.signing_views[target]={};
    auto& view=state.imported_views[target];
    const auto notice="Changed "+changed.affected+". Review and approve this revision separately.";
    view.review_status=notice;
    view.reviewed.reset();
    view.review_commitment.reset();
    view.proved_change_output.reset();
    view.inspection_owner.reset();
    view.selected_document=changed.document;
    view.node=node;
    if (node!=InspectionNode::Overview) view.tree_group=node;
    view.index=index;
    view.tree_page=index/8;
    ++view.revision;
    if (const auto owner=CurrentInspectionOwner(state,host)) {
        auto& next=state.transaction_edits.emplace(target,TransactionEditor{*owner,generation}).first->second;
        Load(next,node,index);
        next.status=notice;
    }
    ++state.transaction_edit_revision;
    if (target!=id) RestoreDocumentFocus(state,target);
    return {true,notice};
}

void Text(State& state,const char* key,const char* label,std::string_view value,SemanticRole role=SemanticRole::Text)
{
    ImGui::TextWrapped("%.*s",int(value.size()),value.data());
    state.presentation.Annotate(key,label,value,role);
}
bool Button(State& state,Facts& facts,const imkit::Theme& theme,const char* key,const char* label,
    bool enabled,std::function<bool()> action,std::string_view detail={})
{
    ImGui::PushID(key);
    ImGui::BeginDisabled(!enabled);
    const bool pressed=theme::ActionButton(label,imkit::ActionVariant::Secondary,{},theme);
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    facts.transaction_document[key]={{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),!enabled,
        pressed,pressed,ImGui::IsItemVisible()};
    const auto external=state.presentation.Interact(label,SemanticRole::Button,SemanticAction::Press|SemanticAction::Focus,
        !enabled,false,{},detail);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted=pressed && enabled && action();
    if (external) state.presentation.Defer(*external,[enabled,pressed,accepted,action=std::move(action)](const auto&){
        return enabled && (pressed ? accepted : action());
    });
    return accepted;
}
void Field(State& state,Facts& facts,const char* key,const char* label,c::Edit& edit,const std::function<bool()>& current)
{
    ImGui::PushID(key);
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1);
    if (c::EditInput("##workbench-edit",edit,0,true)) ++state.transaction_edit_revision;
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    facts.transaction_document[key]={{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),false,
        false,false,ImGui::IsItemVisible()};
    const auto action=state.presentation.Interact(label,SemanticRole::TextField,SemanticAction::SetValue|SemanticAction::Focus,
        false,false,edit.draft());
    state.presentation.BindTextPosition(ImGui::GetItemID(),edit.draft(),edit.cursor);
    if (action) state.presentation.Defer(*action,[&state,&edit,current](const auto& request){
        if (!current() || !edit.SetExternalDraft(request.value)) return false;
        ++state.transaction_edit_revision;
        return true;
    });
    ImGui::PopID();
}
void Destination(State& state,TransactionEditor& editor,const std::function<bool()>& current)
{
    ImGui::TextUnformatted("Output address or script hex");
    ImGui::SetNextItemWidth(-1);
    c::TextPosition cursor;
    if (c::TextInput("##workbench-destination","Output address or script hex",editor.destination.data(),
        editor.destination.size(),editor.external_destination,0,&cursor,true)) ++state.transaction_edit_revision;
    const auto request=state.presentation.Interact("Output address or script hex",SemanticRole::TextField,
        SemanticAction::SetValue|SemanticAction::Focus,false,false,editor.destination.data());
    state.presentation.BindTextPosition(ImGui::GetItemID(),editor.destination.data(),cursor);
    if (request) state.presentation.Defer(*request,[&state,&editor,current](const auto& action){
        if (!current() || !c::SetExternalBuffer(editor.destination,editor.external_destination,action.value)) return false;
        ++state.transaction_edit_revision;
        return true;
    });
}
} // namespace

bool IsTransactionEditCommand(a::Command action)
{
    return action==a::Command::BeginTransactionEdit || action==a::Command::ApplyTransactionEdit || action==a::Command::CloseTransactionEdit;
}

a::Availability TransactionEditAvailability(State& state,a::Requests& host,const a::Request& request)
{
    if (!IsTransactionEditCommand(request.command) || request.component || request.option || !request.value.empty()
        || request.section!=a::Section::Transactions || request.document!=state.documents.ActiveId() || !Unobstructed(state))
        return {false,"The current transaction edit context is unavailable."};
    const auto owner=CurrentInspectionOwner(state,host);
    if (!owner) return {false,"Open a current unsigned transaction first."};
    if (BroadcastFreezes(state,owner->id)) return {false,"Wait for the pending submission before editing this transaction."};
    if (const auto blocked=protocol::TransactionEditBlock(*owner->content);!blocked.empty()) return {false,blocked};
    const auto found=state.transaction_edits.find(owner->id);
    if (request.command==a::Command::BeginTransactionEdit)
        return !request.edit && found==state.transaction_edits.end() && state.transaction_edit_generation<UINT64_MAX
            ? a::Availability{true,{}} : a::Availability{false,"An editable session is already open or the owner changed."};
    if (!request.edit || found==state.transaction_edits.end() || found->second.owner!=owner
        || request.edit->generation!=found->second.generation || request.edit->revision!=owner->draft)
        return {false,"Return to editing explicitly and use the currently displayed revision."};
    if (request.command==a::Command::ApplyTransactionEdit && state.signing_reviews.HasReview(owner->id))
        return {false,"Return to editing must first revoke the signing review."};
    return {true,{}};
}

a::Receipt DispatchTransactionEdit(State& state,a::Requests& host,const a::Request& request)
{
    const auto availability=TransactionEditAvailability(state,host,request);
    if (!availability.available) return {false,availability.reason};
    const auto id=request.document;
    if (request.command==a::Command::CloseTransactionEdit) {
        Clear(state.transaction_edits.at(id));
        state.transaction_edits.erase(id);
        ++state.transaction_edit_revision;
        return {true,"Editing closed. The last applied public revision is retained."};
    }
    if (request.command==a::Command::BeginTransactionEdit) {
        const auto owner=CurrentInspectionOwner(state,host);
        auto& view=state.imported_views[id];
        const auto node=view.selected_document.lock()==owner->content ? view.node : InspectionNode::Overview;
        const auto index=node==InspectionNode::Overview ? 0 : view.index;
        auto& editor=state.transaction_edits.emplace(id,TransactionEditor{*owner,++state.transaction_edit_generation}).first->second;
        Load(editor,node,index);
        const bool reviewed=state.signing_reviews.HasReview(id);
        RevokeSigningReview(state,id,"Returned to editing by explicit request.");
        if (reviewed) {
            editor.status="Prior signing approval or review was retired. New changes require a fresh review.";
        }
        state.signing_views[id]={};
        if (view.reviewed) view.review_status="Local review revoked. Returned to editing.";
        view.reviewed.reset();
        view.review_commitment.reset();
        view.proved_change_output.reset();
        ++state.transaction_edit_revision;
        return {true,"Editing the selected unsigned transaction."};
    }
    const auto& editor=state.transaction_edits.at(id);
    auto changed=protocol::EditTransaction(*editor.owner.content,editor.owner.network,request.edit->fields);
    if (!changed.document) return {false,std::move(changed.error)};
    return Adopt(state,host,editor,std::move(changed),request.edit->fields);
}

void PollTransactionEditors(State& state,a::Requests& host)
{
    std::erase_if(state.edited_origins,[&](const auto& item){return !state.documents.Find(item.first);});
    for (auto at=state.transaction_edits.begin();at!=state.transaction_edits.end();) {
        if (OwnerExists(state,host,at->second.owner) && (at->first!=state.documents.ActiveId()
            || CurrentInspectionOwner(state,host)==at->second.owner)) {++at;continue;}
        Clear(at->second);
        at=state.transaction_edits.erase(at);
        ++state.transaction_edit_revision;
    }
}

bool DrawTransactionEditor(State& state,a::Requests& host,Facts& facts,const imkit::Theme& theme)
{
    const auto owner=CurrentInspectionOwner(state,host);
    if (!owner || !owner->content->transaction || !Unobstructed(state)) return false;
    const auto id=owner->id,revision=state.transaction_edit_revision;
    const auto scoped=[&state,&host,owner,revision]{
        return Unobstructed(state) && CurrentInspectionOwner(state,host)==owner && state.transaction_edit_revision==revision;
    };
    auto found=state.transaction_edits.find(id);
    if (found==state.transaction_edits.end()) {
        const auto request=Request(a::Command::BeginTransactionEdit,id);
        const auto allowed=TransactionEditAvailability(state,host,request);
        const auto label=state.signing_reviews.HasReview(id) ? "Return to editing" : "Edit transaction";
        const bool opened=Button(state,facts,theme,"edit-open",label,allowed.available,[&state,&host,request,scoped] {
            return scoped() && DispatchTransactionEdit(state,host,request).accepted;
        },allowed.reason);
        if (!allowed.available) Text(state,"edit-block","Transaction is read-only",allowed.reason);
        return opened;
    }
    auto& editor=found->second;
    const auto apply=[&state,&host,id,&editor,scoped](protocol::TransactionEdit fields) {
        if (!scoped()) return false;
        const auto receipt=DispatchTransactionEdit(state,host,Request(a::Command::ApplyTransactionEdit,id,&editor,std::move(fields)));
        if (!receipt.accepted) {
            editor.status=receipt.reason;
            ++state.transaction_edit_revision;
        }
        return receipt.accepted;
    };
    Text(state,"edit-title","Transaction editor","Transaction editor");
    if (!state.edited_origins.contains(id))
        Text(state,"edit-source","Original source preservation","Applying a change opens a separate working document. The original source is retained.");
    if (!editor.status.empty()) Text(state,"edit-status","Transaction edit status",editor.status,SemanticRole::Alert);
    if (Button(state,facts,theme,"edit-close","Back to inspection",true,[&state,&host,id,&editor,scoped]{
        if (!scoped()) return false;
        auto& view=state.imported_views[id];
        view.selected_document=editor.owner.content;
        view.node=editor.node;
        if (editor.node!=InspectionNode::Overview) view.tree_group=editor.node;
        view.index=editor.index;
        view.tree_page=editor.index/8;
        const auto pending=Request(a::Command::CloseTransactionEdit,id,&editor);
        return DispatchTransactionEdit(state,host,pending).accepted;
    })) return true;
    const bool dirty=Dirty(editor);
    ImGui::SameLine();
    Button(state,facts,theme,"edit-reset","Discard pending fields",dirty,[&state,&editor,scoped]{
        if (!scoped()) return false;
        Load(editor,editor.node,editor.index);
        editor.status="Unapplied fields discarded.";
        ++state.transaction_edit_revision;
        return true;
    });
    const auto select=[&state,&editor,scoped](InspectionNode node,size_t index) {
        if (!scoped() || Dirty(editor)) return false;
        Load(editor,node,index);
        ++state.transaction_edit_revision;
        return true;
    };
    Button(state,facts,theme,"edit-header","Version and locktime",editor.node!=InspectionNode::Overview && !dirty,
        [select]{return select(InspectionNode::Overview,0);});
    ImGui::SameLine();
    Button(state,facts,theme,"edit-inputs","Inputs to edit",editor.node!=InspectionNode::Input && !dirty,
        [select]{return select(InspectionNode::Input,0);});
    ImGui::SameLine();
    Button(state,facts,theme,"edit-outputs","Outputs to edit",editor.node!=InspectionNode::Output && !dirty,
        [select]{return select(InspectionNode::Output,0);});
    const auto& tx=*owner->content->transaction;
    const auto node=editor.node;
    const auto index=editor.index;
    if (node==InspectionNode::Overview) {
        Field(state,facts,"edit-version","Transaction version",editor.version,scoped);
        Field(state,facts,"edit-locktime","Transaction locktime",editor.locktime,scoped);
        const auto value=Decimal(editor.locktime.draft(),UINT32_MAX);
        const bool all_final=std::ranges::all_of(tx.inputs,[](const auto& input){return input.sequence==UINT32_MAX;});
        if (value && *value) Text(state,"edit-time","Locktime interpretation",
            std::string(*value<500000000 ? "Block height " : "Unix timestamp ")+std::to_string(*value)
            +(all_final ? " · all input sequences are final, so this absolute constraint is inactive."
                          : " · chain maturity is Unknown."));
    } else {
        const auto count=node==InspectionNode::Input ? tx.inputs.size() : tx.outputs.size();
        const auto item=std::string(node==InspectionNode::Input ? "Input " : "Output ")+
            std::to_string(index+1)+" of "+std::to_string(count);
        Text(state,"edit-item","Selected editable item",item);
        if (count>1) {
            Button(state,facts,theme,"edit-prev","Previous item",index>0 && !dirty,[select,node,index]{
                return index && select(node,index-1);
            });
            ImGui::SameLine();
            Button(state,facts,theme,"edit-next","Next item",index+1<count && !dirty,[select,node,index]{
                return select(node,index+1);
            });
            const auto move=[apply,node,index](size_t destination){
                protocol::TransactionEdit edit;
                edit.reorder=protocol::ReorderTransactionItem{node,index,destination};
                return apply(std::move(edit));
            };
            const bool earlier=index>0 && !dirty,later=index+1<count && !dirty;
            const bool key_earlier=ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_UpArrow,ImGuiInputFlags_RouteFocused);
            const bool key_later=ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_DownArrow,ImGuiInputFlags_RouteFocused);
            if ((earlier && key_earlier && move(index-1)) || (later && key_later && move(index+1))) return true;
            if (Button(state,facts,theme,"edit-reorder-up","Move earlier (Ctrl+Up)",earlier,[move,index]{
                return index && move(index-1);
            })) return true;
            ImGui::SameLine();
            if (Button(state,facts,theme,"edit-reorder-down","Move later (Ctrl+Down)",later,[move,index]{
                return move(index+1);
            })) return true;
        }
        if (node==InspectionNode::Input) {
            Field(state,facts,"edit-outpoint","Previous outpoint",editor.outpoint,scoped);
            Field(state,facts,"edit-sequence","Input sequence",editor.sequence,scoped);
        } else {
            Field(state,facts,"edit-amount","Output amount (sats)",editor.amount,scoped);
            Destination(state,editor,scoped);
        }
    }
    const auto form=ReadForm(editor);
    const auto explanation=dirty && !form ? "Enter whole decimal values and a canonical outpoint." : "";
    Button(state,facts,theme,"edit-apply","Apply fields",dirty && form.has_value(),[apply,form]{
        return form && apply(*form);
    },explanation);
    return true;
}
} // namespace roots::gui::ui::shell
