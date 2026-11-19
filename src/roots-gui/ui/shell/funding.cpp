// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "services/extensions.h"
#include "ui/theme/theme.h"

#include <algorithm>
#include <sodium.h>

namespace roots::gui::ui::shell {
const app::Document* ActiveSendDraft(const State& state)
{
    auto* draft = state.documents.Active();
    if (!draft || !draft->SendWallet() || !draft->complete || state.privacy.hidden || state.semantic_hide_toggle
        || draft->observation == app::ObservationState::Locked
        || (state.recovery && (state.recovery->Locked() || state.recovery->Pending()))) return nullptr;
    if (draft->coin_draft) {
        const auto offline = state.offline_builders.find(draft->id);
        return offline == state.offline_builders.end() || !offline->second.open ? draft : nullptr;
    }
    return draft->section == app::Section::Send && draft->history.enabled ? draft : nullptr;
}

namespace {
namespace c = components;
using c::ax::SemanticAction;
using c::ax::SemanticRole;

struct Access {
    FundingScope scope;
    const app::Document* wallet;
    RefreshView view;
    DraftFeeTerms fee;
};
std::optional<Access> Current(State& state, app::Requests& host)
{
    const auto* draft = ActiveSendDraft(state);
    const auto* connection = host.Connection();
    const auto* refresh = host.Refresh();
    if (!draft || !state.recovery || !connection || connection->Offline() || connection->PrivacyLocked()
        || BroadcastFreezes(state,draft->id) || !connection->Active() || !refresh || !CanFund(*connection->Active())) return {};
    const auto* wallet = app::RegisteredWallet(state.documents, *draft->SendWallet());
    if (!wallet) return {};
    const auto identity = app::WalletRefreshIdentity(*connection->Active(), connection->Session(), *wallet,
        state.recovery->Gate().Epoch());
    const auto view = refresh->View();
    if (!identity || !view.identity || *view.identity != *identity || !view.ledger || !view.chain_current
        || view.offline || view.locked || view.freshness[2] != Freshness::Live
        || !view.ledger->InventoryAvailable() || !view.ledger->LocksAvailable()) return {};
    const auto fee = FeeTerms(state,*draft,{*identity,connection->Revision(),draft->id},view.ledger->Tip());
    return Access{{*identity, draft->id, connection->Revision(), view.generation, view.history_sequence,
        state.coin_controls.Revision(), draft->draft_revision, view.ledger->Tip(),
        fee.below_minimum ? 0 : fee.rate.value_or(0), fee.quote_observation}, wallet, view, fee};
}

void Status(State& state, std::string message)
{
    state.funding.status = std::move(message);
    ++state.funding.revision;
}

std::optional<app::CoinInventory> ReloadInventory(State& state, Access& access, uint64_t owner)
{
    if (!state.coin_controls.Reload(*access.wallet, access.view, *state.recovery)) return {};
    const auto policy = state.coin_controls.Policy(*access.wallet, access.view, *state.recovery, owner);
    if (!policy) return {};
    access.scope.policy_revision = state.coin_controls.Revision();
    app::CoinInventory inventory(access.scope.wallet.context, access.wallet->binding->DaemonWallet());
    if (!inventory.Apply(*access.view.ledger, access.view.history_sequence, true, &*policy)) return {};
    return inventory;
}

bool RequestFunding(State& state, app::Requests& host, uint64_t id, bool create)
{
    auto access = Current(state, host);
    auto* service = host.Funding();
    const auto* draft = ActiveSendDraft(state);
    if (!access || !service || service->Busy() || !draft || draft->id != id
        || state.actions.kind != DocumentDialog::None || state.recovery_dialog.open) return false;
    const auto reject = [&](const char* reason) { Status(state, reason); return false; };
    auto& funding = state.funding;
    if (create) {
        if (!funding.preview || funding.scope != access->scope) return false;
    } else {
        funding.preview.reset();
        funding.scope.reset();
        funding.flow={};
    }
    const auto owner = state.coin_controls.Owner(id);
    const auto rate = access->fee.rate;
    if (!rate) return reject(draft->funding_rate.empty() && draft->funding_target
        ? "Target rate is Unknown. Read current fee data or enter a manual override."
        : "Enter a positive exact fee rate in sat/vB, with up to three decimal places.");
    if (access->fee.below_minimum) return reject("The draft fee rate is below the observed relay or mempool minimum.");
    if (draft->coin_draft && !owner)
        return reject("Validate the current exact pins before funding. View changes never substitute an input.");
    const auto network = protocol::DocumentNetwork(draft->SendWallet()->network);
    FundingRequest request{access->scope, FundingSelection::Automatic, {}, {}, *rate};
    request.requested_change=draft->funding_change;
    if (draft->recipients.rows.size() > MAX_FUNDING_OUTPUTS) return reject("This preview supports up to 64 recipients.");
    for (const auto& row : draft->recipients.rows) {
        const auto check = CheckRecipient(row, network);
        const auto script = protocol::AddressScript(row.address, network);
        if (!check.destination_error.empty() || !script || (!row.maximum && !check.amount))
            return reject("Apply and validate each recipient before requesting funding.");
        request.recipients.push_back({row.address, *script, row.maximum ? std::nullopt : check.amount});
    }
    const auto inventory = ReloadInventory(state, *access, draft->coin_draft ? id : 0);
    if (!inventory) return reject("Current local freeze, reservation or daemon lock policy is Unknown.");
    request.scope = access->scope;
    std::set<std::string> exclusions;
    for (const auto& excluded : draft->excluded_coins) exclusions.insert(app::CoinKey(excluded));
    const auto& ledger = *access->view.ledger;
    std::map<std::string, const HistoryCoin*> observed;
    for (const auto& coin : ledger.Coins()) observed.emplace(app::CoinKey(coin.outpoint), &coin);
    const auto append = [&](const Outpoint& point) {
        const auto key = app::CoinKey(point);
        const auto row = inventory->Rows().find(key);
        const auto source = observed.find(key);
        const auto parent = ledger.Rows().find(point.txid.hex());
        if (row == inventory->Rows().end() || !row->second.eligible.value_or(false) || exclusions.contains(key)
            || source == observed.end() || source->second->confirmations <= 0 || parent == ledger.Rows().end()
            || parent->second.previous_hex.empty() || source->second->descriptor.empty()) return false;
        const auto& coin = *source->second;
        request.coins.push_back({point, coin.amount, coin.script,
            {parent->second.previous_hex, coin.descriptor, coin.redeem_script, coin.witness_script, ledger.Observed()},
            coin.confirmations});
        return true;
    };
    if (draft->coin_draft || !draft->selected_coins.empty()) {
        request.selection = FundingSelection::Exact;
        if (draft->coin_draft) {
            if (draft->coin_draft->inputs.size() != draft->selected_coins.size())
                return reject("The exact pin set changed. Validate the current pins.");
            for (const auto& pin : draft->coin_draft->inputs) {
                if (std::ranges::find(draft->selected_coins, pin.outpoint) == draft->selected_coins.end()
                    || !append(pin.outpoint) || request.coins.back().amount != pin.value || request.coins.back().script != pin.script)
                    return reject("An exact pin is unavailable, changed or unconfirmed. Ancestor fees are Unknown.");
                if (request.coins.size() > MAX_FUNDING_INPUTS) return reject("This preview supports up to 64 confirmed inputs.");
            }
        } else for (const auto& selected : draft->selected_coins) {
            if (!append(selected)) return reject("An exact selected input is unavailable or unconfirmed. Ancestor fees are Unknown.");
            if (request.coins.size() > MAX_FUNDING_INPUTS) return reject("This preview supports up to 64 confirmed inputs.");
        }
    } else if (create) {
        request.selection = FundingSelection::Exact;
        for (const auto& input : funding.preview->inputs) {
            if (!append(input.point) || request.coins.back().amount != input.value
                || request.coins.back().proof.descriptor != input.descriptor)
                return reject("An accepted input is no longer eligible. Request another funding preview.");
        }
    } else {
        for (const auto& coin : ledger.Coins()) {
            if (coin.confirmations > 0 && !append(coin.outpoint)) request.selection = FundingSelection::Restricted;
            if (request.coins.size() > MAX_FUNDING_INPUTS) return reject("Pin up to 64 confirmed inputs to preview this wallet.");
        }
    }
    if (request.coins.empty()) return reject("No eligible confirmed input proof. Unconfirmed ancestor fees are Unknown.");
    if (create) {
        request.recheck = *funding.preview;
        funding.scope = access->scope;
    }
    if (!service->Start(std::move(request))) return reject("Funding worker is unavailable.");
    funding.scope = access->scope;
    funding.creating = create;
    funding.create_owner = owner;
    Status(state, create ? "Rechecking accepted inputs and outputs before opening the unsigned transaction."
                         : "Checking an unsigned funding preview.");
    return true;
}

bool PublishCreated(State& state, const app::Document& draft, const FundingPreview& verified, int64_t observed)
{
    auto& funding = state.funding;
    const auto& accepted = funding.preview;
    if (!accepted || !accepted->psbt || !verified.psbt || accepted->psbt->source != verified.psbt->source
        || accepted->total_inputs != verified.total_inputs || accepted->fee != verified.fee
        || accepted->maximum != verified.maximum || accepted->change != verified.change) {
        Status(state, "Accepted funding changed. Create did not open a transaction.");
        return false;
    }
    app::CoinDraft evidence{*draft.SendWallet(), {}};
    for (const auto& coin : verified.verified_coins)
        evidence.inputs.push_back({coin.point, coin.amount, coin.script, "Verified funded input", "", coin.proof});
    if (const auto& change = verified.change)
        evidence.offline_change = protocol::OfflineChange{change->descriptor, change->index, observed};
    app::ImportedDocument document{accepted->psbt, protocol::DocumentNetwork(evidence.wallet.network),
        app::DocumentRepresentation::Source, app::CreatedFunding{draft.id, draft.draft_revision, verified.fee, std::move(evidence)}};
    std::string error;
    const auto id = state.recovery->CreateFunded(state.documents, std::move(document), error);
    if (!id) {
        Status(state, std::move(error));
        return false;
    }
    state.reveal_tab_focus = state.focus_tab = id;
    return true;
}

bool SetRate(State& state, uint64_t id, std::string_view text)
{
    auto* draft = ActiveSendDraft(state) ? state.documents.Active() : nullptr;
    if (!draft || draft->id != id || draft->draft_revision.value == UINT64_MAX || text.size() > 32
        || !rpc::ValidUtf8(text) || std::ranges::any_of(text, [](unsigned char c) {return c < 32 || c == 127;})
        || draft->funding_rate == text) return false;
    draft->funding_rate = text;
    ++draft->draft_revision.value;
    return true;
}
void Text(State& state, const char* key, const char* name, const std::string& text)
{
    ImGui::TextWrapped("%s", text.c_str());
    state.presentation.Annotate(key, name, text);
}

template<typename Action>
void Button(State& state, Facts& facts, const imkit::Theme& theme, const char* key, const char* name, bool enabled, Action action)
{
    ImGui::PushID(key);
    ImGui::BeginDisabled(!enabled);
    const auto label=std::string(name)+"###funding-action";
    const bool pressed = theme::ActionButton(label.c_str(), imkit::ActionVariant::Secondary, {}, theme);
    const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    facts.funding[key] = {{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),!enabled,
        pressed,pressed,ImGui::IsItemVisible()};
    const auto semantic = state.presentation.Interact(name, SemanticRole::Button, SemanticAction::Press | SemanticAction::Focus, !enabled);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted = pressed && enabled && action();
    if (semantic) state.presentation.Defer(*semantic, [action, enabled, pressed, accepted](const auto&) {
        return enabled && (pressed ? accepted : action());
    });
}

void Totals(State& state,const FundingPreview& preview)
{
    const auto& flow=*preview.flow;
    Text(state,"funding-totals","Verified totals","Inputs "+std::to_string(flow.total_inputs.sats())
        +" sats · Recipients "+std::to_string(flow.total_recipients.sats())+" sats · Fee "+std::to_string(flow.fee.sats())
        +" sats · Change "+std::to_string(flow.total_change.sats())+" sats");
    if (HighFundingFee(flow.fee,flow.total_recipients))
        Text(state,"funding-high-fee","High fee warning","High fee. At least 100,000 sats or 10% of the recipient total.");
    if (preview.maximum)
        Text(state,"funding-max","Verified Max amount","Max "+std::to_string(preview.maximum->sats())+" sats after fixed outputs and fee.");
}
}

std::shared_ptr<const FundingPreview> CurrentFundingPreview(State& state,app::Requests& host)
{
    if (!state.funding.preview || !state.funding.preview->flow || !state.funding.scope) return {};
    const auto access=Current(state,host);
    return access && state.funding.scope==access->scope ? state.funding.preview : nullptr;
}

void RetireFunding(State& state)
{
    if (!state.funding.rate_shown.empty())
        sodium_memzero(state.funding.rate_shown.data(), state.funding.rate_shown.size());
    state.funding = {};
}

bool CanClearSend(const State& state, uint64_t id)
{
    const auto* draft = state.documents.Active();
    const auto offline = state.offline_builders.find(id);
    return state.recovery && !state.recovery->Locked() && !state.recovery->Pending()
        && !state.privacy.hidden && !state.semantic_hide_toggle && draft && draft->id == id
        && !BroadcastFreezes(state,id)
        && draft->SendWallet() && draft->observation != app::ObservationState::Locked
        && (offline == state.offline_builders.end() || !offline->second.open) && draft->draft_revision.value != UINT64_MAX
        && (draft->recipients.Dirty() || !draft->funding_rate.empty() || draft->funding_target || draft->funding_change || draft->coin_draft
            || !draft->selected_coins.empty() || !draft->excluded_coins.empty());
}

bool ConfirmClearSend(State& state, app::Requests& host)
{
    auto& action = state.actions;
    if (action.kind != DocumentDialog::ClearSend || !CanClearSend(state,action.target)
        || state.documents.Active()->draft_revision != action.send_revision
        || state.recovery->Gate().Epoch() != action.send_epoch
        || state.coin_controls.Owner(action.target) != action.send_owner) return false;
    auto& recovery = *state.recovery;
    if (!recovery.ClearSend(state.documents, action.target, action.send_revision)) {
        action.error = recovery.Error().empty() ? "Send changed. Cancel and try again." : recovery.Error();
        return false;
    }
    state.coin_controls.Release(action.target, action.send_owner, host.Refresh());
    if (state.funding.scope && state.funding.scope->document == action.target) {
        if (auto* service = host.Funding()) service->Cancel();
    }
    state.offline_builders.erase(action.target);
    state.coin_draft_errors.erase(action.target);
    RetireSendView(state);
    RetireFunding(state);
    if (auto* fees=host.Fees()) fees->Cancel();
    RetireFees(state);
    return true;
}

void PollFunding(State& state, app::Requests& host)
{
    auto* service = host.Funding();
    if (!service) return;
    auto& session = state.funding;
    const auto* draft = ActiveSendDraft(state);
    const auto id = draft ? draft->id : 0;
    if (session.document != id) {
        if (!session.rate_shown.empty()) sodium_memzero(session.rate_shown.data(), session.rate_shown.size());
        session.rate_shown.clear();
        session.status.clear();
        session.expanded = false;
        session.document = id;
    }
    auto access = (service->Busy() || session.scope) ? Current(state, host) : std::nullopt;
    const auto scope = access ? std::optional(access->scope) : std::nullopt;
    if (session.scope && session.scope != scope) {
        session.scope.reset();
        session.preview.reset();
        session.flow={};
        session.creating = false;
        session.create_owner.reset();
        Status(state, draft ? "Funding preview retired because its draft, wallet or coin evidence changed." : "");
    }
    if (session.creating && (state.actions.kind != DocumentDialog::None || state.recovery_dialog.open)) return;
    auto reply = service->Poll(scope);
    if (!reply || !scope || !session.scope || !draft) return;
    if (reply->preview) {
        const bool creating = std::exchange(session.creating, false);
        const auto owner = std::exchange(session.create_owner, std::nullopt);
        const auto inventory = ReloadInventory(state, *access, draft->coin_draft ? draft->id : 0);
        const auto current = [&](const auto& input) {
            const auto row = inventory->Rows().find(app::CoinKey(input.point));
            return row != inventory->Rows().end() && row->second.eligible.value_or(false)
                && row->second.value == input.value;
        };
        if (!inventory || !reply->preview->flow || !std::ranges::all_of(reply->preview->inputs, current)
            || (draft->coin_draft && !state.coin_controls.Owner(draft->id))
            || (creating && owner != state.coin_controls.Owner(draft->id))) {
            session.scope.reset();
            session.preview.reset();
            session.flow={};
            Status(state, "A coin's current local eligibility changed. Request another funding preview.");
            return;
        }
        session.scope = access->scope;
        if (creating) {
            PublishCreated(state, *draft, *reply->preview, access->view.ledger->Observed());
            return;
        }
        session.preview = std::make_shared<const FundingPreview>(std::move(*reply->preview));
        session.flow={};
        Status(state, "Unsigned preview verified against current coin evidence.");
    } else {
        if (std::exchange(session.creating, false)) session.preview.reset();
        session.flow={};
        session.scope.reset();
        session.create_owner.reset();
        Status(state, reply->error.empty() ? "Funding preview is unavailable." : std::move(reply->error));
    }
}

void DrawFunding(State& state, app::Requests& host, Facts& facts, const imkit::Theme& theme)
{
    auto* service = host.Funding();
    const auto* draft = ActiveSendDraft(state);
    if (!draft) return;
    const auto id = draft->id;
    if (state.funding.flow.open) {
        const auto preview=CurrentFundingPreview(state,host);
        if (!preview) return;
        Totals(state,*preview);
        const auto scope=*state.funding.scope;
        DrawTransactionFlow(state,host,facts,theme,[&state,&host,scope]{
            const auto access=Current(state,host);
            return access && access->scope==scope;
        });
        return;
    }
    ImGui::PushID("funding");
    if (!draft->coin_draft) {
        Button(state, facts, theme, "clear", "Clear Send", CanClearSend(state,id),
            [&state,id] { return BeginClearSend(state,id); });
        if (service) ImGui::SameLine();
    }
    if (!service) {
        DrawFeePanel(state,host,facts,theme);
        ImGui::PopID();
        return;
    }
    Button(state, facts, theme, "toggle", state.funding.expanded ? "Hide funding" : "Funding preview", true, [&state, id] {
        const auto* current = ActiveSendDraft(state);
        if (!current || current->id != id) return false;
        state.funding.expanded = !state.funding.expanded;
        ++state.funding.revision;
        return true;
    });
    if (host.Fees()) {
        ImGui::SameLine();
        DrawFeePanel(state,host,facts,theme);
    }
    if (!state.funding.expanded) { ImGui::PopID(); return; }
    if (draft->funding_change) {
        Text(state,"funding-change-preference","Change preference","A specific internal change preference will be checked at the next preview.");
        Button(state,facts,theme,"default-change","Use default change",!service->Busy() && draft->draft_revision.value!=UINT64_MAX,
            [&state,&host,id,revision=draft->draft_revision]{
                const auto* current=ActiveSendDraft(state);
                if (!current || current->id!=id || current->draft_revision!=revision || revision.value==UINT64_MAX
                    || !current->funding_change || !host.Funding() || host.Funding()->Busy()
                    || state.actions.kind!=DocumentDialog::None || state.recovery_dialog.open) return false;
                auto* updated=state.documents.Active();
                updated->funding_change.reset();
                ++updated->draft_revision.value;
                state.funding.scope.reset();
                state.funding.preview.reset();
                state.funding.flow={};
                Status(state,"Default change selected. Request a new preview.");
                return true;
            });
    }
    Text(state, "funding-efficiency", "Efficiency strategy",
        "Efficiency uses the wallet's confirmed coin selection when policy permits. Max and local restrictions use exact inputs.");
    const auto strategy=ConstructionStatus(ShippedExtensions(),ConstructionStrategy::AvoidPartialSpends,draft->SendWallet()->network);
    Text(state, "funding-privacy", strategy.available ? "Privacy strategy available" : "Privacy strategy unavailable",strategy.detail);
    Text(state, "funding-grouping", "Avoid partial spends",
        "Avoid partial spends groups eligible coins from the same script. It may use more inputs and raise fees. It provides no anonymity guarantee.");
    ImGui::TextUnformatted("Fee rate (sat/vB)");
    std::array<char, 33> edit{};
    const auto& amount = draft->funding_rate;
    std::copy_n(amount.begin(), std::min<size_t>(32, amount.size()), edit.begin());
    std::optional<std::string> pending;
    if (state.funding.rate_shown != amount) pending = amount;
    c::TextPosition cursor;
    ImGui::SetNextItemWidth(-1);
    c::TextInput("##funding-rate", "Enter an exact rate", edit.data(), edit.size(), pending, 0, &cursor, true);
    state.funding.rate_shown = amount;
    if (SetRate(state, id, edit.data())) state.funding.rate_shown = edit.data();
    const auto semantic = state.presentation.Interact("Fee rate (sat/vB)", SemanticRole::TextField,
        SemanticAction::SetValue | SemanticAction::Focus, false, false, state.funding.rate_shown);
    state.presentation.BindTextPosition(ImGui::GetItemID(), state.funding.rate_shown, cursor);
    if (semantic) state.presentation.Defer(*semantic, [&state, id](const auto& input) {return SetRate(state, id, input.value);});
    const auto access = Current(state, host);
    Button(state, facts, theme, "preview", "Preview funding", access.has_value() && !service->Busy(),
        [&state, &host, id] {return RequestFunding(state, host, id, false);});
    if (service->Busy()) {
        ImGui::SameLine();
        Button(state, facts, theme, "cancel", "Cancel", true, [&state, &host, id] {
            const auto* active = ActiveSendDraft(state);
            if (!active || active->id != id || !host.Funding()->Busy()) return false;
            host.Funding()->Cancel();
            const bool creating = std::exchange(state.funding.creating, false);
            state.funding.create_owner.reset();
            if (!creating) {
                state.funding.scope.reset();
                state.funding.preview.reset();
                state.funding.flow={};
            }
            Status(state, creating ? "Create cancelled. The unsigned preview is retained." : "Preview cancelled.");
            return true;
        });
    }
    if (!access) Text(state, "funding-unavailable", "Funding availability",
        "Funding is Unknown. Connect this exact wallet and refresh confirmed coins. Required RPC options and public internal descriptor access must be available.");
    const bool current = access && state.funding.scope == access->scope;
    if (!state.funding.status.empty() && (!state.funding.scope || current))
        Text(state, "funding-status", "Funding status", state.funding.status);
    if (const auto snapshot=CurrentFundingPreview(state,host); snapshot && current) {
        Button(state, facts, theme, "create", "Create Transaction", !service->Busy(),
            [&state, &host, id] {return RequestFunding(state, host, id, true);});
        ImGui::SameLine();
        Button(state,facts,theme,"flow","Inspect transaction flow",!service->Busy(),
            [&state,&host,scope=*state.funding.scope,snapshot]{
                if (state.funding.scope!=scope || CurrentFundingPreview(state,host)!=snapshot
                    || !host.Funding() || host.Funding()->Busy() || state.actions.kind!=DocumentDialog::None
                    || state.recovery_dialog.open) return false;
                SuspendFeeRecording(state,host);
                state.funding.flow.open=true;
                ++state.funding.revision;
                return true;
            });
        Totals(state,*snapshot);
        if (snapshot->change) Text(state, "funding-change", "Verified change preview",
            "An internal change address was verified for this preview. Allocation is unconfirmed.");
    }
    ImGui::PopID();
}
}
