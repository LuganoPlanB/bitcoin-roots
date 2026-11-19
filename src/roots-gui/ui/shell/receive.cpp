// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/localization.h"
#include "ui/shell/workspace.h"
#include "adapters/protocol/public_wallet.h"
#include "app/history_workspace.h"
#include "services/custody_extensions.h"
#include "ui/theme/theme.h"
#include <algorithm>

namespace roots::gui::ui::shell {
namespace {
namespace c = components;
using c::ax::SemanticAction;
using c::ax::SemanticRole;

const app::Document* ActiveWallet(const State& state, app::Section section)
{
    const auto* document = state.documents.Active();
    if (!document || !document->binding || document->kind != app::DocumentKind::Wallet || document->section != section
        || document->observation == app::ObservationState::Locked || (state.recovery && state.recovery->Locked())) return nullptr;
    return document;
}

std::optional<RefreshIdentity> Identity(const State& state, app::Requests& host, const app::Document& document)
{
    const auto* connection = host.Connection();
    if (!connection || connection->Offline() || connection->PrivacyLocked() || !connection->Active()) return {};
    return app::WalletRefreshIdentity(*connection->Active(), connection->Session(), document,
        state.recovery ? state.recovery->Gate().Epoch() : LockEpoch{});
}

void Status(ReceiveRuntime& runtime, std::string message)
{
    runtime.status = std::move(message);
    ++runtime.revision;
}

ReceiveRuntime& Runtime(State& state, const app::Document& document)
{
    auto& runtime = state.receives[document.id];
    const auto scope = app::HistoryMetadataScope(*document.binding);
    if (runtime.scope != scope) {
        runtime = ReceiveRuntime();
        runtime.scope = scope;
        runtime.label_epoch = state.recovery ? state.recovery->Gate().Epoch() : LockEpoch{};
    }
    return runtime;
}

void Select(State& state,app::Requests& host,ReceiveRuntime& runtime,ReceiveTarget target,ReceiveSource source)
{
    runtime.selection = ReceiveSelection{std::move(target), source};
    runtime.label = c::Edit{};
    runtime.label_loaded = false;
    runtime.label_active = runtime.label_seen_active = false;
    runtime.label_revision = 0;
    runtime.qr_payload.clear();
    runtime.qr_matrix.reset();
    runtime.qr_uri=false;
    const auto* connection=host.Connection();
    runtime.qr_session=connection ? connection->Session() : 0;
    runtime.qr_profile=connection ? connection->Revision() : 0;
    runtime.qr_epoch=state.recovery ? state.recovery->Gate().Epoch() : LockEpoch{};
    Status(runtime, {});
}

bool CurrentSelection(const State& state, uint64_t id, const ReceiveSelection& selection)
{
    const auto* document = ActiveWallet(state, app::Section::Receive);
    const auto runtime = state.receives.find(id);
    return document && document->id == id && runtime != state.receives.end()
        && runtime->second.scope == app::HistoryMetadataScope(*document->binding)
        && runtime->second.selection == selection;
}

template <typename Action>
void Button(State& state, std::map<std::string,c::ItemFacts>& facts, const imkit::Theme& theme,
    const char* key, const char* label, bool enabled, Action action, bool primary = false)
{
    ImGui::PushID(key);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = theme::ActionButton(label, primary ? imkit::ActionVariant::Primary : imkit::ActionVariant::Secondary, {}, theme);
    const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    facts[key] = {{lo.x,lo.y,hi.x,hi.y}, ImGui::GetItemID(), ImGui::IsItemFocused(), !enabled, pressed, pressed, ImGui::IsItemVisible()};
    const auto external = state.presentation.Interact(label, SemanticRole::Button, SemanticAction::Press | SemanticAction::Focus, !enabled);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted = pressed && action();
    if (external) state.presentation.Defer(*external, [action,enabled,pressed,accepted](const auto&) {
        return enabled && (pressed ? accepted : action());
    });
}

void Text(State& state, const char* key, const char* name, const std::string& value, SemanticRole role = SemanticRole::Text)
{
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
    state.presentation.Annotate(key, name, value, role);
}

bool LoadLabel(State& state, ReceiveRuntime& runtime)
{
    if (!state.recovery || !runtime.scope || !runtime.selection) return false;
    const auto metadata = state.recovery->HistoryMetadata(*runtime.scope);
    if (!metadata) {
        Status(runtime, std::string("Label unavailable: ") + storage::ErrorName(metadata.error));
        return false;
    }
    std::string saved;
    for (const auto& entry : metadata.value->labels) {
        if (entry.target.kind == storage::TargetKind::Address && entry.target.key == runtime.selection->target.address) {
            if (!ReceiveUri(runtime.selection->target.address, entry.text)) {
                Status(runtime, "This stored label cannot be edited in Receive.");
                return false;
            }
            saved = entry.text;
            break;
        }
    }
    if (!runtime.label.dirty() || !runtime.label_loaded) runtime.label.SetExternalDraft(saved);
    runtime.label.saved = std::move(saved);
    runtime.label.error.clear();
    ++runtime.revision;
    runtime.label_revision = metadata.value->revision;
    runtime.label_loaded = true;
    return true;
}

bool SaveLabel(State& state, uint64_t id, bool explicit_save = false)
{
    const auto found = state.receives.find(id);
    if (found == state.receives.end() || !found->second.label.dirty()) return true;
    auto& runtime = found->second;
    auto& label = runtime.label;
    if (!explicit_save && label.attempted && label.last_attempt == label.draft()) {
        if (label.error.empty()) {
            label.error = "Label draft retained. Press Enter to save it.";
            ++runtime.revision;
        }
        return false;
    }
    const auto* document = state.documents.Find(id);
    label.attempted = true;
    label.last_attempt = label.draft();
    if (!document || !document->binding || document->observation == app::ObservationState::Locked
        || !state.recovery || state.recovery->Locked() || state.recovery->Pending()
        || runtime.label_epoch != state.recovery->Gate().Epoch() || state.privacy.hidden || state.semantic_hide_toggle
        || runtime.scope != app::HistoryMetadataScope(*document->binding) || !runtime.label_loaded || !runtime.selection) {
        label.error = "Label draft retained. Return to the unlocked Receive editor and press Enter to save.";
        ++runtime.revision;
        return false;
    }
    const auto result = state.recovery->SaveReceiveLabel(*runtime.scope, runtime.selection->target.address, std::string(label.draft()), runtime.label_revision);
    if (!result) {
        label.error = std::string("Label draft retained: ") + storage::ErrorName(result.error) + ". Reload the saved label to retry.";
        ++runtime.revision;
        return false;
    }
    runtime.label_revision = *result.value;
    label.saved = label.draft();
    label.error.clear();
    ++runtime.revision;
    return true;
}

void DrawLabel(State& state, Facts& facts, const imkit::Theme& theme, uint64_t id, ReceiveRuntime& runtime, bool busy)
{
    if (state.privacy.hidden) {
        Text(state, "label-hidden", "Receive label", "Activity and label hidden");
        if (!runtime.label.error.empty() && runtime.label.dirty())
            Text(state, "label-hidden-error", "Receive label status", "An unsaved Receive label is retained.", SemanticRole::Alert);
        return;
    }
    if (!runtime.label_loaded) {
        Text(state, "label-unavailable", "Receive label", "Local label unavailable");
        return;
    }
    auto& label = runtime.label;
    const auto selected = *runtime.selection;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Label");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::BeginDisabled(busy);
    const bool entered = c::EditInput("##receive-label", label, ImGuiInputTextFlags_EnterReturnsTrue, true);
    const bool left = ImGui::IsItemDeactivatedAfterEdit();
    runtime.label_seen_active = !busy && ImGui::IsItemActive();
    runtime.label_active |= runtime.label_seen_active;
    const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    facts.receive["label"] = {{lo.x,lo.y,hi.x,hi.y}, ImGui::GetItemID(), ImGui::IsItemFocused(), busy, entered, entered, ImGui::IsItemVisible()};
    const auto edit = state.presentation.Interact("Receive label; Enter or focus loss saves", SemanticRole::TextField,
        SemanticAction::SetValue | SemanticAction::Focus, busy, false, label.draft(), label.error, !label.error.empty());
    ImGui::EndDisabled();
    if (entered || left) SaveLabel(state, id, entered);
    if (edit) state.presentation.Defer(*edit, [&state,id,selected,busy](const c::SemanticRequest& request) {
        return !busy && CurrentSelection(state, id, selected) && !state.privacy.hidden && !state.semantic_hide_toggle
            && ReceiveUri(selected.target.address, request.value)
            && state.receives.at(id).label.SetExternalDraft(request.value) && SaveLabel(state, id, true);
    });
    if (!label.error.empty()) {
        Text(state, "receive-label-error", "Label save", label.error, SemanticRole::Alert);
        Button(state, facts.receive, theme, "reload-label", "Reload saved label", true, [&state,id,selected] {
            return CurrentSelection(state, id, selected) && !state.privacy.hidden && !state.semantic_hide_toggle
                && LoadLabel(state, state.receives.at(id));
        });
    }
}

bool Copy(State& state, app::Requests& host, uint64_t id, const ReceiveSelection& selected,
    std::string label, bool uri, bool hidden)
{
    if (!CurrentSelection(state, id, selected) || hidden != (state.privacy.hidden || state.semantic_hide_toggle)
        || (!hidden && label != state.receives.at(id).label.saved)) return false;
    const auto value = uri ? ReceiveUri(selected.target.address, label) : std::optional{selected.target.address};
    if (!value) return false;
    const app::Request request{app::Command::Component, id, app::Section::Receive,
        c::CopyIdentifier{uri ? "receive-uri" : "receive-address", *value, uri && !label.empty()}};
    return host.CanSubmit(request).available && host.Submit(request).accepted;
}

std::string LastUsed(State& state, app::Requests& host, const app::Document& document, const ReceiveTarget& target)
{
    if (state.privacy.hidden) return "Hidden";
    const auto identity = Identity(state, host, document);
    if (!identity || !host.Refresh()) return "Unknown";
    const auto history = app::HistoryForDocument(document, *host.Refresh(), identity->context.lock_epoch);
    if (!history || !history->identity || *history->identity != *identity
        || !history->chain_current || history->offline || !history->ledger) return "Unknown";
    if (const auto used = ReceiveLastUsed(*history->ledger, target)) return locale::LocalDate(*used, true);
    return "Unknown";
}

bool Start(State& state, app::Requests& host, uint64_t id, ReceiveRequest request)
{
    const auto* document = ActiveWallet(state, app::Section::Receive);
    auto* service = host.Receiving();
    auto* connection = host.Connection();
    if (!document || document->id != id || !service || service->Busy() || !connection
        || !state.recovery || Identity(state, host, *document) != request.identity
        || connection->Revision() != request.profile_revision) return false;
    auto& runtime = Runtime(state, *document);
    if (request.action == ReceiveAction::VerifyDevice && (!runtime.selection || runtime.selection->target != request.selected)) return false;
    if (request.action == ReceiveAction::VerifyDevice) {
        if (state.privacy.hidden || !host.Devices()) return false;
        request.device = host.Devices()->Current(connection);
        if (!request.device) return false;
    }
    if (request.action == ReceiveAction::Next && !SaveLabel(state, id)) return false;
    if (ReceiveAllocation(request.action)) {
        const auto ticket = state.recovery->BeginReceive(*runtime.scope, request.action == ReceiveAction::Reserve);
        if (!ticket) {
            Status(runtime, std::string("Allocation was not started: ") + storage::ErrorName(ticket.error));
            return false;
        }
        request.ticket = *ticket.value;
        runtime.inspection.reset();
    }
    if (service->Start(request, *connection)) {
        Status(runtime, request.action == ReceiveAction::VerifyDevice
            ? "Waiting for the daemon. Check this exact address on the device."
            : "Checking this exact wallet with the daemon…");
        return true;
    }
    if (ReceiveAllocation(request.action)) state.recovery->FinishReceive(*runtime.scope, request.ticket);
    Status(runtime, "Receive operation did not start.");
    return false;
}

bool TakeReservation(State& state, app::Requests& host, uint64_t id)
{
    const auto* document = ActiveWallet(state, app::Section::Receive);
    if (!document || document->id != id || !state.recovery || Identity(state, host, *document)) return false;
    auto& runtime = Runtime(state, *document);
    if (!SaveLabel(state, id)) return false;
    auto target = state.recovery->ConsumeReceive(*runtime.scope);
    if (!target) {
        Status(runtime, std::string("Offline reservation unavailable: ") + storage::ErrorName(target.error));
        return false;
    }
    Select(state,host,runtime,std::move(*target.value),ReceiveSource::Reservation);
    LoadLabel(state, runtime);
    return true;
}

bool Abandon(State& state, app::Requests& host, uint64_t id, uint64_t ticket)
{
    const auto* document = ActiveWallet(state, app::Section::Receive);
    if (!document || document->id != id || !state.recovery || !host.Connection() || !host.Receiving() || host.Receiving()->Busy()) return false;
    auto& runtime = Runtime(state, *document);
    if (!runtime.inspection || runtime.inspection->ticket != ticket
        || runtime.inspection->identity != Identity(state, host, *document)
        || runtime.inspection->profile_revision != host.Connection()->Revision()) return false;
    const auto cleared = state.recovery->FinishReceive(*runtime.scope, ticket);
    runtime.inspection.reset();
    Status(runtime, cleared ? "Local attempt cleared. Any completed daemon allocation remains in the wallet."
        : std::string("Attempt remains unresolved: ") + storage::ErrorName(cleared.error));
    return bool(cleared);
}

std::optional<ReceiveTarget> InventoryTarget(const AddressView& view, std::string_view key)
{
    if (!view.identity || !view.current || !view.snapshot || !view.snapshot->stable || !view.observed) return {};
    const auto row = std::ranges::find(view.snapshot->rows, key, &DerivedAddress::key);
    if (row == view.snapshot->rows.end() || row->internal || row->lookahead) return {};
    const auto descriptor = std::ranges::find(view.snapshot->descriptors, row->descriptor_id, &AddressDescriptor::id);
    if (descriptor == view.snapshot->descriptors.end() || !descriptor->classified || descriptor->internal
        || !descriptor->ranged || !descriptor->reason.empty()) return {};
    ReceiveTarget target{row->address, row->script, descriptor->descriptor, row->path,
        std::string(protocol::ReceiveChain(view.identity->context.network.genesis.hex())), row->index, *view.observed};
    return protocol::VerifyReceiveTarget(target) ? std::optional{std::move(target)} : std::nullopt;
}

void DrawTarget(State& state, app::Requests& host, Facts& facts, const imkit::Theme& theme, float scale,
    const app::Document& document, ReceiveRuntime& runtime, bool busy)
{
    const auto selected = *runtime.selection;
    const auto& target = selected.target;
    const auto id = document.id;
    const bool hidden = state.privacy.hidden;
    const auto qr = 190 * scale;
    const bool wide = ImGui::GetContentRegionAvail().x >= qr + 185 * scale
        && ImGui::BeginTable("receive-layout", 2, ImGuiTableFlags_SizingStretchSame);
    if (wide) {
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthStretch, 1.F);
        ImGui::TableSetupColumn("QR", ImGuiTableColumnFlags_WidthFixed, qr);
        ImGui::TableNextColumn();
    }
    Text(state, "receive-address", "Receive address", target.address);
    const char* source = selected.source == ReceiveSource::Daemon ? "Daemon allocation"
        : selected.source == ReceiveSource::Reservation ? "Daemon reservation" : "Verified receive inventory";
    Text(state, "receive-provenance", "Address provenance", std::string(source) + " / " + ObservationDate(target.observed_ms));
    const auto path = target.path.empty() ? (target.index ? "Descriptor index " + std::to_string(*target.index) : "Unavailable") : target.path;
    Text(state, "receive-path", "Derivation", "Derivation: " + path);
    Text(state, "receive-last-used", "Last used", "Last used: " + LastUsed(state, host, document, target));
    ImGui::PushID(target.address.c_str());
    DrawLabel(state, facts, theme, id, runtime, busy);
    ImGui::PopID();
    const auto label = hidden ? std::string{} : runtime.label.saved;
    Button(state, facts.receive, theme, "copy-address", "Copy address", true, [&state,&host,id,selected,label,hidden] {
        return Copy(state, host, id, selected, label, false, hidden);
    });
    ImGui::SameLine();
    Button(state, facts.receive, theme, "copy-uri", "Copy URI", true, [&state,&host,id,selected,label,hidden] {
        return Copy(state, host, id, selected, label, true, hidden);
    });
    if (wide) ImGui::TableNextColumn();
    const auto qr_current=[&state,&host,&runtime,id,selected] {
        const auto* connection=host.Connection();
        return !state.privacy.hidden && !state.semantic_hide_toggle && CurrentSelection(state,id,selected)
            && runtime.qr_session==(connection ? connection->Session() : 0)
            && runtime.qr_profile==(connection ? connection->Revision() : 0)
            && runtime.qr_epoch==(state.recovery ? state.recovery->Gate().Epoch() : LockEpoch{});
    };
    if (!hidden && qr_current()) {
        const auto payload=runtime.qr_uri ? ReceiveUri(target.address,label) : std::optional{target.address};
        const auto validated=payload ? ParsePaymentRequest(*payload,protocol::DocumentNetwork(document.binding->network))
            : PaymentRequest{};
        if (!payload || validated.address!=target.address || !validated.error.empty()) {
            runtime.qr_payload.clear();
            runtime.qr_matrix.reset();
        } else if (runtime.qr_payload!=*payload) {
            runtime.qr_payload=*payload;
            runtime.qr_matrix=protocol::EncodeQr(*payload);
        }
        Text(state,"receive-qr-type","Receive QR format",runtime.qr_uri ? "Static QR · payment URI" : "Static QR · Bitcoin address");
        if (!runtime.qr_matrix || !DrawQrMatrix(*runtime.qr_matrix,qr-4))
            Text(state,"receive-qr-size","Receive QR size","Enlarge the window or copy the address or URI to share it.");
        Button(state,facts.receive,theme,"receive-qr-mode",runtime.qr_uri ? "Show address QR" : "Show payment URI QR",true,
            [&runtime,qr_current] {
                if (!qr_current()) return false;
                runtime.qr_uri=!runtime.qr_uri;
                return true;
            });
    }
    if (!hidden && !qr_current()) {
        runtime.qr_payload.clear();
        runtime.qr_matrix.reset();
        Text(state,"receive-qr-current","Receive QR availability","Select an address in the current wallet session to display its QR.");
    }
    if (wide) ImGui::EndTable();
    ImGui::Separator();
    ImGui::TextUnformatted("scriptPubKey");
    Text(state, "receive-script", "scriptPubKey", target.script);
    ImGui::TextUnformatted("Public descriptor");
    Text(state, "receive-descriptor", "Public descriptor", target.descriptor);
}
}

void BeginReceiveLabelFrame(State& state)
{
    for (auto& [id, runtime] : state.receives) runtime.label_seen_active = false;
}

void FinishReceiveLabelFrame(State& state)
{
    for (auto& [id, runtime] : state.receives) {
        if (!runtime.label_active) continue;
        if (runtime.label_seen_active && runtime.selection && CurrentSelection(state, id, *runtime.selection)
            && !state.privacy.hidden && !state.semantic_hide_toggle) continue;
        runtime.label_active = false;
        SaveLabel(state, id);
    }
}

bool SaveReceiveLabelsBeforeClose(State& state, std::span<const uint64_t> documents)
{
    return std::ranges::all_of(documents, [&](uint64_t id) {return SaveLabel(state, id);});
}

bool HasReceiveLabelError(const State& state, uint64_t except)
{
    return std::ranges::any_of(state.receives, [except](const auto& item) {
        const auto& label = item.second.label;
        return item.first != except && label.dirty() && !label.error.empty();
    });
}

void PollReceive(State& state, app::Requests& host)
{
    std::erase_if(state.receives, [&](const auto& entry) {return !state.documents.Find(entry.first);});
    if (state.recovery && state.recovery->Locked()) state.receives.clear();
    auto* service = host.Receiving();
    if (!service) return;
    const auto* active = ActiveWallet(state, app::Section::Receive);
    auto* runtime = active ? &Runtime(state, *active) : nullptr;
    const auto identity = active ? Identity(state, host, *active) : std::nullopt;
    const auto selected = runtime && runtime->selection ? std::optional{runtime->selection->target} : std::nullopt;
    if (runtime && runtime->inspection && (runtime->inspection->identity != identity
        || !host.Connection() || runtime->inspection->profile_revision != host.Connection()->Revision())) runtime->inspection.reset();
    auto result = service->Poll(host.Connection(), identity, selected);
    if (!result || !runtime || !state.recovery) return;
    auto& [operation, reply] = *result;
    if (ReceiveAllocation(operation.action) && (reply.address || !reply.allocation_dispatched)) {
        const auto stored = state.recovery->FinishReceive(*runtime->scope, operation.ticket, reply.address);
        if (!stored) {
            Status(*runtime, std::string("Allocation recording failed: ") + storage::ErrorName(stored.error) + ". Check the pending attempt.");
            return;
        }
    }
    if (reply.address && operation.action == ReceiveAction::Next) {
        if (!SaveLabel(state, active->id)) return;
        Select(state,host,*runtime,std::move(*reply.address),ReceiveSource::Daemon);
        LoadLabel(state, *runtime);
    } else if (reply.address && operation.action == ReceiveAction::Reserve) {
        Status(*runtime, "One verified address saved for offline use.");
    } else if (reply.inspected) {
        runtime->inspection = operation;
        Status(*runtime, "Wallet and network reverified. Check the daemon wallet's address history before clearing the local pending attempt.");
    } else if (reply.signer_echo) {
        Status(*runtime, "The daemon signer echoed this exact address. Compare it on the device before relying on it.");
    } else {
        Status(*runtime, std::move(reply.error));
    }
}

void DrawDeviceSigner(State& state, app::Requests& host, std::map<std::string,c::ItemFacts>& facts,
    const imkit::Theme& theme, bool blocked)
{
    if (state.privacy.hidden || (state.recovery && state.recovery->Locked())) return;
    auto* devices = host.Devices();
    auto* connection = host.Connection();
    const auto* document = state.documents.Active();
    if (!devices || !connection || !document || connection->Offline() || !connection->Active()) return;
    if (!CanDiscoverDevice(*connection->Active())) {
        Text(state, "device-status", "Daemon signer status", DeviceStateText(DeviceState::Unsupported));
        return;
    }
    const auto status = devices->Status();
    std::string detail = DeviceStateText(status);
    if (!devices->Fingerprint().empty()) {
        detail += " Fingerprint " + std::string(devices->Fingerprint()) + ".";
        if (!devices->Label().empty()) detail += " Daemon label " + std::string(devices->Label()) + ".";
    }
    Text(state, "device-status", "Daemon signer status", detail);
    const auto current = [&state,connection,id=document->id,section=document->section,session=connection->Session(),
        revision=connection->Revision(),epoch=state.recovery ? state.recovery->Gate().Epoch() : LockEpoch{}] {
        const auto* active = state.documents.Active();
        return active && active->id == id && active->section == section && active->observation != app::ObservationState::Locked
            && !state.privacy.hidden && (!state.recovery || (!state.recovery->Locked() && state.recovery->Gate().Epoch() == epoch))
            && connection->Session() == session && connection->Revision() == revision && !connection->Offline();
    };
    Button(state, facts, theme, "device-rescan", "Rescan devices", !blocked && status != DeviceState::Scanning
        && status != DeviceState::Confirm, [current,devices,connection] {return current() && devices->Rescan(*connection);});
    if (status == DeviceState::Select) {
        ImGui::SameLine();
        Button(state, facts, theme, "device-select", "Select matching signer", !blocked,
            [current,devices,connection] {return current() && devices->Select(*connection);});
    }
    if (status == DeviceState::Scanning || status == DeviceState::Select || status == DeviceState::Ready || status == DeviceState::Confirm) {
        ImGui::SameLine();
        Button(state, facts, theme, "device-cancel", "Cancel device selection", true, [current,devices,&state,&host,id=document->id] {
            if (!current()) return false;
            if (auto* receiving=host.Receiving();receiving && receiving->CancelDevice())
                if (auto runtime=state.receives.find(id);runtime!=state.receives.end())
                    Status(runtime->second,"Waiting cancelled. The selected address is retained.");
            devices->Cancel();
            return true;
        });
    }
}

void DrawReceive(State& state, app::Requests& host, Facts& facts, const imkit::Theme& theme, float scale)
{
    const auto* document = ActiveWallet(state, app::Section::Receive);
    if (!document) return;
    const auto id = document->id;
    auto& runtime = Runtime(state, *document);
    auto* service = host.Receiving();
    const bool busy = service->Busy();
    const auto change_view=[&state,id,revision=runtime.revision](bool reusable){
        const auto* wallet=ActiveWallet(state,app::Section::Receive);
        const auto found=state.receives.find(id);
        if (!wallet || wallet->id!=id || found==state.receives.end() || found->second.revision!=revision) return false;
        auto& current=found->second;
        current.reusable_view=reusable;
        current.reusable_focus=true;
        ++current.revision;
        return true;
    };
    if (runtime.reusable_view) {
        Text(state,"reusable-heading","Reusable policies","Reusable receive policies");
        for (const auto family:{ReusableFamily::FixedDescriptor,ReusableFamily::SilentPayments,ReusableFamily::PaymentCode}) {
            const auto policy=ReusableStatus(ShippedExtensions(),family,document->binding->network);
            const auto name="reusable-"+std::to_string(int(family));
            Text(state,name.c_str(),policy.name,std::string(policy.name)+" · "+policy.public_fields);
        }
        const auto info=ReusableStatus(ShippedExtensions(),ReusableFamily::FixedDescriptor,document->binding->network);
        Text(state,"reusable-service","Reusable policy unavailable",info.capability.detail);
        Text(state,"reusable-action","Address advancement","These policies use a fixed public identifier. Address allocation is available under standard Receive when supported.");
        if (std::exchange(runtime.reusable_focus,false)) ImGui::SetKeyboardFocusHere();
        Button(state,facts.receive,theme,"standard-policy","Back to standard Receive",true,[change_view]{return change_view(false);});
        return;
    }
    const auto identity = Identity(state, host, *document);
    const auto pool = state.recovery ? state.recovery->ReceivePool(*runtime.scope) : storage::Result<storage::ReceivePool>{};
    const uint64_t pending = pool ? pool.value->pending : 0;
    const auto* caps = identity ? &*host.Connection()->Active() : nullptr;
    const bool allocatable = caps && CanAllocateReceive(*caps) && pool && !pending
        && pool.value->retained < storage::ReceivePool::MAX_RECORDS && !busy;

    imkit::PushTextRole(theme, imkit::TextRole::Heading);
    ImGui::TextUnformatted("Receive");
    imkit::PopTextRole();

    if (pending) Text(state, "receive-pending", "Pending allocation",
        busy ? "One allocation is pending." : "Allocation outcome unknown. Verify the daemon wallet before clearing this attempt.", SemanticRole::Alert);

    const auto action = [&state,&host,id,identity,revision=caps ? host.Connection()->Revision() : 0](ReceiveAction operation, uint64_t ticket = 0,
        std::optional<ReceiveTarget> target = {}) {
        return identity && Start(state, host, id, {operation,*identity,revision,ticket,std::move(target)});
    };
    const float line = ImGui::GetFrameHeightWithSpacing();
    const auto& style = ImGui::GetStyle();
    const auto width = [&](const char* label) {return ImGui::CalcTextSize(label).x + style.FramePadding.x * 2;};
    const float next_width = width("Get Next Address");
    const bool verify = caps && CanVerifyReceiveOnDevice(*caps);
    const auto device = host.Devices() ? host.Devices()->Current(host.Connection()) : SelectedDevice{};
    const float signer_width = verify ? width("Verify on device") + style.ItemSpacing.x : 0;
    const float footer_width = ImGui::GetContentRegionAvail().x;
    const bool signer_alongside = next_width + signer_width <= footer_width;
    const bool reserve_alongside = width("Reserve one for offline") + style.ItemSpacing.x + next_width + signer_width <= footer_width;
    const auto footer_rows = busy || pending || !identity || reserve_alongside ? 1.F : signer_alongside ? 2.F : 3.F;
    const float details_height = std::max(line * 2, ImGui::GetContentRegionAvail().y - footer_rows * line);
    ImGui::BeginChild("receive-details", {0,details_height});
    if (runtime.selection) DrawTarget(state, host, facts, theme, scale, *document, runtime, busy);
    else Text(state, "receive-no-address", "Receive address", "Select an address from inventory, get the next address, or use a previously reserved address.");
    if (!pool) Text(state, "receive-pool-error", "Offline address pool", "Offline pool unavailable. Local metadata must be unlocked and ready.");
    else Text(state, "receive-pool", "Offline address pool", pool.value->available
        ? std::to_string(pool.value->available) + " verified offline reservations"
        : "Offline pool exhausted (0 reservations)");
    if (!runtime.status.empty()) Text(state, "receive-status", "Receive status", runtime.status);
    if (std::exchange(runtime.reusable_focus,false)) ImGui::SetKeyboardFocusHere();
    Button(state,facts.receive,theme,"reusable-policies","Reusable policies",!busy,[change_view]{return change_view(true);});
    if (caps && caps->external_signer == true) DrawDeviceSigner(state,host,facts.receive,theme,busy);
    if (!verify)
        Text(state, "receive-device-unavailable", "Device verification", "Device verification requires a supported external signer and display RPC.");
    ImGui::EndChild();

    if (busy) {
        Button(state, facts.receive, theme, "cancel", "Cancel receive operation", true, [service,&state,id] {
            service->Cancel();
            if (auto runtime=state.receives.find(id);runtime!=state.receives.end())
                Status(runtime->second,"Waiting cancelled. The selected address is retained.");
            return true;
        });
    } else if (pending && identity) {
        Button(state, facts.receive, theme, "inspect", "Check daemon wallet", caps != nullptr, [action,pending] {return action(ReceiveAction::Inspect,pending);});
        if (runtime.inspection && runtime.inspection->ticket == pending) {
            ImGui::SameLine();
            Button(state, facts.receive, theme, "abandon", "Clear pending attempt", true, [&state,&host,id,pending] {return Abandon(state,host,id,pending);});
        }
    } else if (!identity) {
        const bool available = pool && pool.value->available > 0;
        const char* label = available ? "Use reserved address offline" : pool ? "Offline pool exhausted" : "Offline pool unavailable";
        Button(state, facts.receive, theme, "offline-next", label, available,
            [&state,&host,id] {return TakeReservation(state,host,id);}, available);
    } else {
        Button(state, facts.receive, theme, "reserve", "Reserve one for offline", allocatable && pool.value->available < storage::ReceivePool::MAX_RESERVED,
            [action] {return action(ReceiveAction::Reserve);});
        const auto target = runtime.selection ? std::optional{runtime.selection->target} : std::nullopt;
        if (reserve_alongside) ImGui::SameLine();
        const float remaining = ImGui::GetContentRegionAvail().x;
        if (signer_alongside) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + remaining - next_width - signer_width);
        if (verify) {
            Button(state, facts.receive, theme, "verify-device", "Verify on device", target.has_value() && device && !device->Busy(),
                [action,target] {return action(ReceiveAction::VerifyDevice,0,target);});
            if (signer_alongside) ImGui::SameLine();
        }
        Button(state, facts.receive, theme, "next", "Get Next Address", allocatable, [action] {return action(ReceiveAction::Next);}, true);
    }
}

void DrawReceiveTo(State& state, app::Requests& host, Facts& facts, const imkit::Theme& theme, const AddressView& view, std::string_view key)
{
    if (!host.Receiving() || !host.Addresses() || !view.identity) return;
    const auto* document = ActiveWallet(state, app::Section::Addresses);
    if (!document) return;
    const auto valid = InventoryTarget(view, key);
    Button(state, facts.address, theme, "receive-to", "Receive To", valid.has_value(),
        [&state,&host,id=document->id,identity=*view.identity,sequence=view.sequence,generation=view.generation,key=std::string(key),valid] {
            const auto* active = ActiveWallet(state, app::Section::Addresses);
            if (!valid || !active || active->id != id || active->addresses.selected != key || Identity(state,host,*active) != identity) return false;
            const auto current = host.Addresses()->View(identity);
            if (current.sequence != sequence || current.generation != generation || InventoryTarget(current,key) != valid) return false;
            auto& runtime = Runtime(state, *active);
            if (!SaveLabel(state, id)) return false;
            Select(state,host,runtime,*valid,ReceiveSource::Inventory);
            LoadLabel(state, runtime);
            state.documents.Find(id)->section = app::Section::Receive;
            return true;
        });
}
}
