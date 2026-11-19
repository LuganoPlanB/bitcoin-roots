// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/localization.h"
#include "ui/shell/workspace.h"
#include "app/history_workspace.h"
#include "ui/theme/theme.h"
#include <algorithm>

namespace roots::gui::ui::shell {
namespace {
namespace c=components;
using c::ax::SemanticAction;
using c::ax::SemanticRole;
const app::Document* Open(const State& state,uint64_t id)
{
    const auto* doc=state.documents.Active();
    return doc && doc->id==id && doc->coin_draft && !state.privacy.hidden && !state.semantic_hide_toggle
        && state.recovery && !state.recovery->Locked() && !state.recovery->Pending() ? doc : nullptr;
}
void Text(State& state,const char* key,const char* label,const std::string& value)
{
    ImGui::TextWrapped("%s",value.c_str());
    state.presentation.Annotate(key,label,value);
}
template<class Action>
bool ActionButton(State& state,Facts& facts,const imkit::Theme& theme,const char* key,const char* label,bool enabled,Action action)
{
    ImGui::PushID(key);
    ImGui::BeginDisabled(!enabled);
    const bool pressed=theme::ActionButton(label,imkit::ActionVariant::Secondary,{},theme);
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    facts.transaction_document[key]={{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),!enabled,pressed,pressed,ImGui::IsItemVisible()};
    const auto accessible=state.presentation.Interact(label,SemanticRole::Button,SemanticAction::Press|SemanticAction::Focus,!enabled);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted=pressed && enabled && action();
    if (accessible) state.presentation.Defer(*accessible,[action,enabled,pressed,accepted](const auto&) {
        return enabled && (pressed ? accepted : action());
    });
    return accepted;
}
void Field(State& state,uint64_t id,OfflineBuilder& editor,c::Edit& field,const char* label)
{
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1);
    if (c::EditInput("##offline-field",field,0,true)) {++editor.revision;editor.error.clear();}
    const auto external=state.presentation.Interact(label,SemanticRole::TextField,SemanticAction::SetValue|SemanticAction::Focus,
        false,false,field.draft());
    state.presentation.BindTextPosition(ImGui::GetItemID(),field.draft(),field.cursor);
    if (external) state.presentation.Defer(*external,[&state,id,&field,&editor](const auto& request) {
        if (!Open(state,id) || !field.SetExternalDraft(request.value)) return false;
        ++editor.revision;
        editor.error.clear();
        return true;
    });
    ImGui::PopID();
}
bool Construct(State& state,app::Requests& host,uint64_t id)
{
    const auto* doc=Open(state,id);
    if (!doc) return false;
    auto& editor=state.offline_builders.at(id);
    const auto rejected=[&](std::string message) {editor.error=std::move(message);++editor.revision;return false;};
    const auto& cached=*doc->coin_draft;
    const auto* wallet=app::RegisteredWallet(state.documents,cached.wallet);
    const auto view=wallet && host.Refresh() ? app::HistoryForDocument(*wallet,*host.Refresh(),state.recovery->Gate().Epoch()) : std::nullopt;
    if (!state.coin_controls.AllowsOfflineDraft(*doc,state.documents,view?&*view:nullptr,state.recovery))
        return rejected("An exact pin is unavailable, frozen, reserved, or lacks complete previous-output proof. No inputs substituted.");
    const auto amount=Money::ParseBtc(editor.amount.draft()), fee=Money::ParseBtc(editor.fee.draft());
    if (!amount || !fee) return rejected("Enter exact BTC amounts for recipient and fee.");
    protocol::OfflineDraft request{protocol::DocumentNetwork(cached.wallet.network),{},
        {{std::string(editor.address.draft()),*amount}},*fee,editor.use_change?cached.offline_change:std::nullopt};
    for (const auto& pin:cached.inputs) request.inputs.push_back({pin.outpoint,pin.value,pin.script,pin.offline});
    auto result=protocol::ConstructOfflineDraft(request);
    if (!result.document) return rejected(std::move(result.error));
    auto binding=cached.wallet;
    const auto origin=std::pair{id,doc->draft_revision};
    if (!OpenLocalData(state,std::move(result.document),request.network,editor.error)) {
        ++editor.revision;
        return false;
    }
    auto& review=state.imported_views[state.documents.ActiveId()];
    review.wallet=std::move(binding);
    review.review_evidence=std::move(result.review_evidence);
    review.offline_review_origin=origin;
    state.notice="Unsigned PSBT from cached public data. Verify current spendability before any later transaction action.";
    return true;
}
}
void DrawOfflineBuilder(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme)
{
    const auto id=state.documents.ActiveId();
    const auto* doc=Open(state,id);
    if (!doc) return;
    auto& editor=state.offline_builders[id];
    ActionButton(state,facts,theme,"offline-builder",editor.open?"Back to exact pins":"Construct offline",true,[&state,id,&editor] {
        if (!Open(state,id)) return false;
        editor.open=!editor.open;
        ++editor.revision;
        return true;
    });
    if (!editor.open) return;
    const auto& cached=*doc->coin_draft;
    int64_t earliest=INT64_MAX;
    bool complete=!cached.inputs.empty() && cached.inputs.size()<=protocol::MAX_OFFLINE_INPUTS;
    for (const auto& input:cached.inputs) {
        if (!input.offline) complete=false;
        else earliest=std::min(earliest,input.offline->observed_ms);
    }
    Text(state,"offline-source","Cached input provenance",complete ? "Exact wallet snapshot: "+locale::LocalDate(earliest/1000,true)
        +" · "+std::to_string(cached.inputs.size())+" pinned. Live spendability unknown."
        : "Previous output or policy proof is Unknown for at least one exact pin.");
    if (cached.observed_unavailable)
        Text(state,"offline-unavailable","Previously checked input status","The last exact online check denied this cached set.");
    const auto network=protocol::DocumentNetwork(cached.wallet.network);
    Text(state,"offline-network","Cached wallet network","Wallet network: "+std::string(protocol::DocumentNetworkName(network)));
    Field(state,id,editor,editor.address,"Recipient address");
    Field(state,id,editor,editor.amount,"Recipient amount (BTC)");
    Field(state,id,editor,editor.fee,"Fee (BTC)");
    if (cached.offline_change) {
        const auto& proof=*cached.offline_change;
        const auto change=protocol::DerivePublicDescriptor(proof.descriptor,protocol::DocumentChain(network),proof.index);
        if (change) {
            if (ImGui::Checkbox("Use cached change output",&editor.use_change)) ++editor.revision;
            const auto request=state.presentation.Interact("Use cached change output",SemanticRole::Toggle,
                SemanticAction::Press|SemanticAction::Focus,false,editor.use_change);
            if (request) state.presentation.Defer(*request,[&state,id,&editor](const auto&) {
                if (!Open(state,id)) return false;
                editor.use_change=!editor.use_change;
                ++editor.revision;
                return true;
            });
            Text(state,"offline-change","Proved cached change address",change->address+" · derivation "+std::to_string(proof.index)
                +". Unused status unknown.");
        }
    } else Text(state,"offline-change-missing","Change proof","Change is Unknown. Read the wallet's saved descriptors before pinning.");
    if (!editor.error.empty()) Text(state,"offline-builder-error","Offline draft validation",editor.error);
    ActionButton(state,facts,theme,"make-offline-draft","Build unsigned PSBT",complete && !cached.observed_unavailable
        && !editor.address.draft().empty() && !editor.amount.draft().empty() && !editor.fee.draft().empty(),
        [&state,&host,id]{return Construct(state,host,id);});
}
}
