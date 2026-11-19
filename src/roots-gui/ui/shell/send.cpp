// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/localization.h"
#include "ui/shell/workspace.h"
#include "adapters/protocol/public_wallet.h"
#include "ui/theme/theme.h"

#include <algorithm>
#include <sodium.h>

namespace roots::gui::ui::shell {
namespace {
namespace c = components;
using c::ax::SemanticAction;
using c::ax::SemanticRole;

app::Document* Editable(State& state, uint64_t document, uint64_t recipient)
{
    auto* active = state.documents.Active();
    if (!active || active->id != document || active->recipients.selected != recipient
        || !active->SendWallet() || !active->complete
        || (active->kind == app::DocumentKind::Wallet && active->section != app::Section::Send)
        || active->observation == app::ObservationState::Locked
        || BroadcastFreezes(state,document)
        || state.privacy.hidden || state.semantic_hide_toggle
        || (state.recovery && (state.recovery->Locked() || state.recovery->Pending()))) return nullptr;
    return active;
}

void Text(State& state, const char* id, const char* label, const std::string& value,
    SemanticRole role = SemanticRole::Text)
{
    const auto shown = locale::Translate(value, locale::Messages());
    ImGui::TextWrapped("%.*s", int(shown.size()), shown.data());
    state.presentation.Annotate(id, locale::Text(label), shown, role);
}

c::ItemFacts Item(bool pressed, bool disabled)
{
    const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    return {{lo.x, lo.y, hi.x, hi.y}, ImGui::GetItemID(), ImGui::IsItemFocused(), disabled,
        pressed, pressed, ImGui::IsItemVisible()};
}

template<typename Action>
void Button(State& state, Facts& facts, const imkit::Theme& theme, const std::string& key,
    const char* visible, const char* accessible, bool enabled, Action action, bool selected = false,
    float width = 0)
{
    ImGui::PushID(key.c_str());
    ImGui::BeginDisabled(!enabled);
    const bool pressed = theme::ActionButton(locale::Text(visible), selected ? imkit::ActionVariant::Primary
        : imkit::ActionVariant::Secondary, {width, 0}, theme);
    facts.send[key] = Item(pressed, !enabled);
    const auto semantic = state.presentation.Interact(locale::Text(accessible), SemanticRole::Button,
        SemanticAction::Press | SemanticAction::Focus, !enabled, selected);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted = pressed && enabled && action();
    if (semantic) state.presentation.Defer(*semantic, [action, enabled, pressed, accepted](const auto&) {
        return enabled && (pressed ? accepted : action());
    });
}

bool Apply(State& state, uint64_t document, uint64_t recipient, std::optional<std::string_view> destination = {})
{
    auto* active = Editable(state, document, recipient);
    if (!active) return false;
    const auto* row = active->recipients.Find(recipient);
    const auto network = protocol::DocumentNetwork(active->SendWallet()->network);
    return row && active->recipients.ApplyDestination(recipient, destination.value_or(row->pay_to),
        network, active->draft_revision);
}

template<size_t Limit>
void Field(State& state, Facts& facts, uint64_t document, const SendRecipient& row,
    RecipientField field, const char* id, const char* label, const std::string& value, bool disabled = false)
{
    std::array<char, Limit + 1> buffer{};
    std::copy_n(value.begin(), std::min(value.size(), Limit), buffer.begin());
    auto& last = state.send_view.edit_values.at(size_t(field));
    std::optional<std::string> external;
    if (last != value) external = value;
    last = value;
    c::TextPosition cursor;
    const char* translated = locale::Text(label);
    ImGui::TextWrapped("%s", translated);
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(-1);
    ImGui::BeginDisabled(disabled);
    const bool submitted = c::TextInput("##entry", nullptr, buffer.data(), buffer.size(), external,
        field == RecipientField::Destination ? ImGuiInputTextFlags_EnterReturnsTrue : 0, &cursor, true);
    facts.send[id] = Item(false, disabled);
    const std::string_view edited(buffer.data());
    if (!disabled && edited != value) {
        if (auto* active = Editable(state, document, row.id);
            active && active->recipients.Edit(row.id, field, edited, active->draft_revision)) last = edited;
    }
    const char* instructions = field == RecipientField::Amount && row.unit == RecipientUnit::BTC
        ? locale::Text("A comma or period marks decimals. Enter amounts without digit grouping.") : "";
    const auto semantic = state.presentation.Interact(translated, SemanticRole::TextField,
        SemanticAction::SetValue | SemanticAction::Focus, disabled, false, last, instructions);
    state.presentation.BindTextPosition(ImGui::GetItemID(), last, cursor);
    if (semantic) state.presentation.Defer(*semantic, [&state, document, recipient = row.id, field](const auto& action) {
        auto* active = Editable(state, document, recipient);
        return active && active->recipients.Edit(recipient, field, action.value, active->draft_revision);
    });
    ImGui::EndDisabled();
    ImGui::PopID();
    if (submitted && field == RecipientField::Destination) Apply(state, document, row.id);
}

std::optional<ReceiveTarget> WalletDestination(const State& state, app::Requests& host,
    const app::Document& draft, uint64_t destination)
{
    const auto* target = state.documents.Find(destination);
    const auto receive = state.receives.find(destination);
    const auto* connection = host.Connection();
    if (!target || !target->binding || !target->history.enabled || !target->complete
        || target->observation == app::ObservationState::Locked || !draft.SendWallet()
        || !connection || connection->Offline() || connection->PrivacyLocked() || !connection->Active()
        || receive == state.receives.end() || !receive->second.selection) return {};
    const auto backend = app::MetadataBackend(connection->Active()->candidate);
    const auto& binding = *target->binding;
    if (!backend || *backend != binding.backend || connection->Active()->genesis != binding.network.genesis.hex()
        || binding.network != draft.SendWallet()->network
        || receive->second.scope != app::HistoryMetadataScope(binding)
        || receive->second.label_epoch != (state.recovery ? state.recovery->Gate().Epoch() : LockEpoch{})) return {};
    const auto& observed = receive->second.selection->target;
    const auto network = protocol::DocumentNetwork(binding.network);
    if (observed.chain != protocol::DocumentChain(network)
        || protocol::AddressScript(observed.address, network) != observed.script
        || !protocol::VerifyReceiveTarget(observed)) return {};
    return observed;
}

void WalletDestinations(State& state, app::Requests& host, Facts& facts, const imkit::Theme& theme,
    const app::Document& draft, uint64_t recipient, bool enabled)
{
    bool found = false;
    for (const auto& wallet : state.documents.Documents()) {
        const auto verified = WalletDestination(state, host, draft, wallet.id);
        if (!verified) continue;
        found = true;
        const auto name = wallet.binding->DaemonWallet();
        ImGui::PushID(std::to_string(wallet.id).c_str());
        Text(state, "wallet-destination", "Verified receive address",
            (name.empty() ? "Default daemon wallet" : name) + " · " + verified->address);
        const std::string action = "Use verified receive address for " + (name.empty() ? "default daemon wallet" : name);
        Button(state, facts, theme, "use-wallet-" + std::to_string(wallet.id),
            "Use address", action.c_str(), enabled,
            [&state, &host, id = draft.id, recipient, destination = wallet.id, shown = *verified] {
                const auto* doc = Editable(state, id, recipient);
                if (!doc || WalletDestination(state, host, *doc, destination) != shown
                    || !Apply(state, id, recipient, shown.address)) return false;
                state.send_view.wallet_destinations = false;
                return true;
            });
        ImGui::PopID();
    }
    if (!found) Text(state, "wallet-destinations-unavailable", "Wallet destination availability",
        "Open a wallet on this node and select a verified address in its Receive tab.");
}

std::optional<Money> EligibleFunds(State& state, app::Requests& host, const app::Document& document)
{
    auto& cache = state.send_view.funds;
    const auto* connection = host.Connection();
    const auto* refresh = host.Refresh();
    // A pinned set needs its own current policy and preflight result. The
    // wallet-wide eligibility total can never stand in for an exact pin set.
    if (!state.recovery || !connection || connection->Offline() || connection->PrivacyLocked()
        || !connection->Active() || !refresh || !document.binding
        || !document.selected_coins.empty() || !document.excluded_coins.empty()) {
        cache.reset();
        return {};
    }
    const auto identity = app::WalletRefreshIdentity(*connection->Active(), connection->Session(), document,
        state.recovery->Gate().Epoch());
    const auto view = refresh->View();
    if (!identity || !view.identity || *identity != *view.identity || !view.ledger || !view.chain_current
        || view.offline || view.locked || view.freshness[2] != Freshness::Live
        || !view.ledger->InventoryAvailable() || !view.ledger->LocksAvailable()) {
        cache.reset();
        return {};
    }
    if (cache && cache->context == identity->context && cache->generation == view.generation
        && cache->sequence == view.history_sequence && cache->policy_revision == state.coin_controls.Revision()
        && cache->draft == document.draft_revision) return cache->value;
    const auto policy = state.coin_controls.Policy(document, view, *state.recovery);
    app::CoinInventory inventory(identity->context, document.binding->DaemonWallet());
    std::optional<Money> value;
    if (policy && inventory.Apply(*view.ledger, view.history_sequence, view.chain_current, &*policy))
        value = inventory.Summary({}).eligible;
    cache.emplace(SendView::Funds{identity->context, view.generation, view.history_sequence,
        state.coin_controls.Revision(), document.draft_revision, value});
    return value;
}
}

void RetireSendView(State& state)
{
    for (auto& value : state.send_view.edit_values)
        if (!value.empty()) sodium_memzero(value.data(), value.size());
    state.send_view = {};
}

void DrawSendRecipients(State& state, app::Requests& host, Facts& facts, const imkit::Theme& theme, float scale)
{
    auto* active = state.documents.Active();
    if (!active || !active->SendWallet()) return;
    const auto document = active->id, recipient = active->recipients.selected;
    const auto* record = active->recipients.Find(recipient);
    if (!Editable(state, document, recipient) || !record) {
        RetireSendView(state);
        if (state.privacy.hidden) {
            Text(state, "send-hidden", "Payment recipients", "Payment details hidden.");
            Text(state, "flow-hidden", "Transaction flow", "Transaction flow hidden.");
        }
        return;
    }
    const auto network = protocol::DocumentNetwork(active->SendWallet()->network);
    const auto row = *record;
    auto& view = state.send_view;
    const bool reveal_recipient = view.document != document || view.recipient != recipient;
    if (reveal_recipient) {
        RetireSendView(state);
        view.document = document;
        view.recipient = recipient;
    }
    const bool edit = active->draft_revision.value != UINT64_MAX;
    const std::string scope = "recipient-" + std::to_string(recipient);
    ImGui::PushID("send-recipients");
    imkit::PushTextRole(theme, imkit::TextRole::Heading);
    Text(state, "send-heading", "Send", "Send");
    imkit::PopTextRole();
    Text(state, "send-network", "Wallet network", "Network: " + std::string(protocol::DocumentNetworkName(network)));
    if (BroadcastFreezes(state,document)) {
        Text(state,"send-submission","Pending submission","Wait for the pending submission before editing this draft.",SemanticRole::Alert);
        ImGui::PopID();
        return;
    }
    if (DrawBatchOrAnalysis(state,host,facts,theme)) {
        ImGui::PopID();
        return;
    }
    DrawFunding(state, host, facts, theme);
    if (state.funding.flow.open && CurrentFundingPreview(state,host)) {
        ImGui::PopID();
        return;
    }
    const float recipient_width = std::max(92*scale, theme::ActionWidth(locale::Text("Recipients")));
    const float destination_width = row.address.empty() ? 0.F : theme::ActionWidth(row.address.c_str());
    const float payment_width = std::max({
        theme::ActionWidth(locale::Text("Apply destination")), theme::ActionWidth(locale::Text("Wallet address")),
        theme::ActionWidth(locale::Text("Import QR")), theme::ActionWidth(locale::Text("Unset Max")), destination_width
    });
    const float cell_insets = 4 * ImGui::GetStyle().CellPadding.x + ImGui::GetStyle().ScrollbarSize;
    const bool split = ImGui::GetContentRegionAvail().x >= payment_width + recipient_width + cell_insets;
    if (ImGui::BeginTable("recipients", split ? 2 : 1, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Payment", ImGuiTableColumnFlags_WidthStretch, 1);
        if (split) ImGui::TableSetupColumn("Recipients", ImGuiTableColumnFlags_WidthFixed, recipient_width);
        ImGui::TableNextColumn();
        const auto* draw = ImGui::GetWindowDrawList();
        state.presentation.Group("send-payment", locale::Text("Payment recipient"), SemanticRole::Group,
            draw->GetClipRectMin(), draw->GetClipRectMax());
        ImGui::PushID(scope.c_str());
        Field<MAX_PAYMENT_TEXT>(state, facts, document, row, RecipientField::Destination, "pay-to",
            "Pay to", row.pay_to, !edit);
        Button(state, facts, theme, "apply", "Apply destination", "Apply destination", edit && row.pay_to != row.address,
            [&state, document, recipient] {return Apply(state, document, recipient);});
        theme::SameLineForAction(locale::Text("Wallet address"));
        Button(state, facts, theme, "wallet", "Wallet address", "Choose an open wallet address", edit,
            [&state, document, recipient] {
                if (!Editable(state, document, recipient)) return false;
                state.send_view.wallet_destinations = !state.send_view.wallet_destinations;
                return true;
            }, view.wallet_destinations);
        theme::SameLineForAction(locale::Text("Import QR"));
        Button(state, facts, theme, "qr", "Import QR", "Import payment QR", edit && host.LocalDocuments(),
            [&state,&host,document,recipient] {
                return Editable(state,document,recipient) && BeginLocalDocument(state,host,LocalDocumentMode::PaymentQr);
            });
        if (view.wallet_destinations) WalletDestinations(state, host, facts, theme, *active, recipient, edit);
        Field<MAX_RECIPIENT_LABEL>(state, facts, document, row, RecipientField::Label, "label",
            "Label (local)", row.label, !edit);
        const bool sats = row.unit == RecipientUnit::Sats;
        Field<MAX_AMOUNT_TEXT>(state, facts, document, row, RecipientField::Amount, "amount",
            sats ? "Amount (sats)" : "Amount (BTC)", row.maximum ? std::string{} : row.amount, !edit || row.maximum);
        Button(state, facts, theme, "unit", sats ? "Use BTC" : "Use sats",
            sats ? "Use BTC" : "Use satoshis", edit, [&state, document, recipient, sats] {
                auto* doc = Editable(state, document, recipient);
                return doc && doc->recipients.SetUnit(recipient, sats ? RecipientUnit::BTC : RecipientUnit::Sats,
                    doc->draft_revision);
            });
        theme::SameLineForAction(locale::Text(row.maximum ? "Unset Max" : "Max"));
        Button(state, facts, theme, "max", row.maximum ? "Unset Max" : "Max", "Use remaining funds for this recipient",
            edit, [&state, document, recipient] {
                auto* doc = Editable(state, document, recipient);
                return doc && doc->recipients.ToggleMaximum(recipient, doc->draft_revision);
            }, row.maximum);
        const auto* current = active->recipients.Find(recipient);
        const auto checks = CheckRecipient(*current, network);
        if (!checks.destination_error.empty() && !current->pay_to.empty())
            Text(state, "send-destination-error", "Destination validation", checks.destination_error, SemanticRole::Alert);
        if (!checks.amount_error.empty() && (!current->amount.empty() || !current->address.empty()))
            Text(state, "send-amount-error", "Amount validation", checks.amount_error, SemanticRole::Alert);
        if (checks.below_reference_dust)
            Text(state, "send-dust-reference", "Reference dust warning", "Below the reference dust threshold. Current relay policy is Unknown.", SemanticRole::Alert);
        const auto funded=CurrentFundingPreview(state,host);
        const bool current_max=funded && funded->maximum;
        if (current->maximum && !current_max)
            Text(state, "send-max-unknown", "Max amount", "Max amount is Unknown until funding is calculated.");
        ImGui::PopID();
        state.presentation.EndGroup();

        ImGui::TableNextColumn();
        ImGui::TextWrapped("%s", locale::Text("Recipients"));
        // Selection itself does not change the draft, and IDs survive deletion
        // and renumbering. The visible number follows the actual recipient order.
        const auto& rows = active->recipients.rows;
        const auto count = rows.size();
        ImGui::BeginChild("recipient-tabs", {0, float(std::min<size_t>(count, 3)) * ImGui::GetFrameHeightWithSpacing()},
            ImGuiChildFlags_NavFlattened);
        ImGuiListClipper clipper;
        clipper.Begin(int(count));
        if (reveal_recipient && count > 3)
            clipper.IncludeItemByIndex(int(std::ranges::find(rows, recipient, &SendRecipient::id) - rows.begin()));
        while (clipper.Step()) for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const auto id = rows.at(size_t(index)).id;
            const auto number = std::to_string(index + 1);
            const auto label = "Recipient " + number;
            Button(state, facts, theme, "recipient-" + std::to_string(id), number.c_str(), label.c_str(),
                true, [&state, document, recipient, id] {
                    auto* doc = Editable(state, document, recipient);
                    return doc && doc->recipients.Select(id);
                }, id == recipient, -1);
            if (reveal_recipient && count > 3 && id == recipient) ImGui::SetScrollHereY();
        }
        ImGui::EndChild();
        Button(state, facts, theme, "add", "+ Add", "Add recipient", edit && count < MAX_SEND_RECIPIENTS,
            [&state, document, recipient] {
                auto* doc = Editable(state, document, recipient);
                return doc && doc->recipients.Add(doc->draft_revision);
            }, false, -1);
        if (count > 1) Button(state, facts, theme, "remove", "Remove", "Remove selected recipient", edit,
            [&state, document, recipient] {
                auto* doc = Editable(state, document, recipient);
                return doc && doc->recipients.Remove(recipient, doc->draft_revision);
            }, false, -1);
        ImGui::EndTable();
    }
    const auto eligible = EligibleFunds(state, host, *active);
    auto& cache=view.totals;
    if (!cache || cache->revision!=active->draft_revision || cache->eligible!=eligible) {
        facts.work.send_counted=active->recipients.rows.size();
        cache=SendView::Totals{active->draft_revision,eligible,CheckSendFunds(active->recipients,eligible)};
    }
    const auto& totals=cache->amounts;
    if (totals.above_money_limit)
        Text(state, "send-bounded-amounts", "Payment validation", "Fixed amounts exceed the Bitcoin supply.", SemanticRole::Alert);
    if (totals.insufficient)
        Text(state, "send-funds-error", "Available funds", "Insufficient eligible funds for the entered recipients.", SemanticRole::Alert);
    else if (!CurrentFundingPreview(state,host))
        Text(state, "send-funds", "Fundability", eligible
            ? "Eligible inputs observed. Fundability and fee are Unknown."
            : "Eligible funds and fee are Unknown.");
    if (!edit) Text(state, "send-revision", "Draft editing", "Draft revision exhausted; editing is unavailable.", SemanticRole::Alert);
    ImGui::PopID();
}
}
