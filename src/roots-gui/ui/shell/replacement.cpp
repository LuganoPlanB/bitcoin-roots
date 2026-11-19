// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "app/broadcast.h"

#include <algorithm>

namespace roots::gui::ui::shell {
namespace {
namespace a=app;
bool ReplacementVisible(const State& state)
{
    return !state.privacy.hidden && !state.semantic_hide_toggle && !state.lock_panel.setup
        && state.actions.kind==DocumentDialog::None && !state.server.dialog.open && !state.onboarding.dialog.open
        && !state.recovery_dialog.open && !state.local_document.dialog.open && !state.close_tab_popup
        && !state.close_document_popup && (!components::native_input || !components::native_input->NativeDialogActive());
}
}
a::Availability ReplacementAvailability(State& state,a::Requests& host,const a::FeeDraftRequest& request)
{
    if (!ReplacementVisible(state)) return {false,"Return to the visible current wallet history to increase the fee."};
    const auto* service=host.ReplacementDrafts();
    if (!service || !host.Refresh() || !state.recovery) return {false,"The replacement draft service is unavailable."};
    if (const auto* broadcast=host.Broadcasts();broadcast && broadcast->Busy())
        return {false,"Wait for the current submission or status check to finish."};
    if (std::ranges::any_of(state.imported_views,[&](const auto& item){
        const auto& replacement=item.second.replacement;
        return replacement && replacement->scope.txid==request.txid && replacement->scope.identity.context.network==request.context.network;
    })) return {false,"A draft for this original is already open."};
    const auto reason=service->Unavailable(state.documents,*host.Refresh(),*state.recovery,request);
    return {reason.empty(),reason};
}

a::Receipt BeginReplacement(State& state,a::Requests& host,const a::FeeDraftRequest& request)
{
    if (request.kind==a::FeeDraftKind::ChildPaysForParent) {
        const auto history=state.histories.find(state.documents.ActiveId());
        if (history==state.histories.end() || history->second.child_parent!=request.txid
            || !request.parent_output || history->second.child_output!=request.parent_output)
            return {false,"Select the specific parent outpoint in the current fee action."};
    }
    if (request.kind==a::FeeDraftKind::CancelPayment) {
        const auto history=state.histories.find(state.documents.ActiveId());
        if (history==state.histories.end() || history->second.cancellation_prompt!=request)
            return {false,"Open the current cancellation warning and choose Create competing self-payment."};
    }
    const auto available=ReplacementAvailability(state,host,request);
    if (!available.available) return {false,available.reason};
    return host.ReplacementDrafts()->Start(state.documents,*host.Refresh(),*state.recovery,state.coin_controls,request);
}

void PollReplacement(State& state,a::Requests& host)
{
    auto* service=host.ReplacementDrafts();
    if (!service || !service->Busy() || !host.Refresh() || !state.recovery) return;
    auto result=service->Poll(state.documents,*host.Refresh(),*state.recovery,state.coin_controls,ReplacementVisible(state));
    if (!result) return;
    ++state.signing_revision;
    if (!result->draft) {
        if (ReplacementVisible(state) && state.documents.Active()) {
            const auto id=state.documents.ActiveId();
            if (auto found=state.histories.find(id);found!=state.histories.end()) found->second.error=std::move(result->reason);
            else state.notice=std::move(result->reason);
        }
        return;
    }
    auto& draft=*result->draft;
    auto candidate=state.documents;
    const auto id=candidate.OpenImportedDocument({draft.working,draft.proof.network});
    if (!id || !state.recovery->CanAdmitPublicDocuments(candidate)) {
        state.histories[draft.scope.document].error="The workspace could not admit the replacement draft. The original was retained.";
        return;
    }
    auto* document=candidate.Find(id);
    document->name=document->original_name=draft.child_package ? "Child pays for parent" : draft.cancellation ? "Cancellation attempt" : "Fee replacement";
    state.documents=std::move(candidate);
    state.histories[draft.scope.document].error.clear();
    state.imported_views[id].replacement=std::move(draft);
    RestoreDocumentFocus(state,id);
}
} // namespace roots::gui::ui::shell
