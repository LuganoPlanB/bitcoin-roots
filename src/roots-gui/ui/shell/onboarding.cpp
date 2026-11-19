// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "app/history_workspace.h"
#include "services/custody_extensions.h"
#include "ui/theme/theme.h"
#include <algorithm>
#include <sodium.h>

namespace roots::gui::ui::shell {
namespace {
namespace c = components;
constexpr auto DIALOG = "Wallet workspace###wallet-onboarding";

LockEpoch Epoch(const State& state)
{
    return state.recovery ? state.recovery->Gate().Epoch() : LockEpoch{};
}

std::string Display(std::string_view name)
{
    return name.empty() ? "Default daemon wallet" : std::string(name);
}

bool Current(const State& state, const ConnectionService* connection)
{
    const auto& panel = state.onboarding;
    return connection && connection->Active() && !connection->Offline() && !connection->PrivacyLocked()
        && connection->Session() == panel.session && connection->Revision() == panel.profile_revision
        && state.documents.ActiveId() == panel.document && Epoch(state) == panel.epoch;
}

void Close(State& state)
{
    auto& panel = state.onboarding;
    panel.page = OnboardingPage::None;
    panel.dialog.open = false;
    panel.close_pending = true;
    ++panel.revision;
    panel.wallets.clear();
    panel.recent.clear();
    panel.selected.reset();
    panel.name = c::Edit("");
    panel.daemon_path = c::Edit("");
    panel.preview.reset();
    panel.clipboard_pending = false;
    panel.path_reviewed = false;
    panel.error.clear();
}

void FactsFor(Facts& facts, const std::string& id, bool pressed = false)
{
    const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    facts.onboarding[id] = {{lo.x,lo.y,hi.x,hi.y}, ImGui::GetItemID(), ImGui::IsItemFocused(),
        bool(ImGui::GetItemFlags() & ImGuiItemFlags_Disabled), false, pressed, ImGui::IsItemVisible()};
}

void ApplyWallet(State& state, ConnectionService& connection, WalletAction action, BackendCapabilities wallet)
{
    auto binding = app::VerifiedWalletBinding(wallet);
    if (!binding) {
        state.onboarding.error = "The daemon wallet's identity could not be opened.";
        return;
    }
    const bool exists = app::RegisteredWallet(state.documents, *binding);
    if (!exists && state.documents.Documents().size() >= 32) {
        state.onboarding.error = "Close a document before opening another wallet.";
        return;
    }
    auto documents = state.documents;
    const auto name = binding->DaemonWallet();
    const auto id = documents.OpenWallet(name.size() > 255 ? "Daemon wallet " + binding->wallet.value().substr(0, 24) : Display(name), *binding, true);
    auto* document = documents.Find(id);
    if (!exists && state.recovery) state.recovery->RestoreWalletPresentation(*document);
    if (!document->history.enabled) {
        document->history.enabled = true;
        for (auto& section : document->sections) section.note = c::Edit("");
    }
    if (action == WalletAction::Create || action == WalletAction::Import || action == WalletAction::Restore) documents.Navigate(app::Section::Settings);
    if (!connection.AdoptWallet(std::move(wallet), state.onboarding.session, state.onboarding.profile_revision)) {
        state.onboarding.error = "The connection changed before the wallet could be opened. Refresh its current wallet list.";
        return;
    }
    state.documents = std::move(documents);
    if (state.onboarding.policy_review && action == WalletAction::Import && state.onboarding.preview)
        PolicyWalletApplied(state,state.onboarding.document,id,*state.onboarding.preview,connection.Session());
    state.registered_session = connection.Session();
    switch (action) {
    case WalletAction::Create: state.notice = "Daemon wallet created. Settings is open."; break;
    case WalletAction::Import: state.notice = "Public descriptors verified in the new watch-only wallet."; break;
    case WalletAction::Restore: state.notice = "Daemon backup restored and wallet verified."; break;
    default: state.notice = "Daemon wallet verified and opened."; break;
    }
    if (state.recovery && !state.recovery->RememberWallet(app::HistoryMetadataScope(*binding))) {
        state.notice += " Recent workspace could not be saved locally.";
    }
    Close(state);
    RestoreDocumentFocus(state, id);
}
}

void BeginOnboarding(State& state, OnboardingPage page, app::Requests& host)
{
    auto& panel = state.onboarding;
    auto* connection = host.Connection();
    if (!connection || !host.Wallets() || panel.dialog.open || panel.close_pending) return;
    const auto revision = panel.revision + 1;
    panel = OnboardingPanel();
    panel.revision = revision;
    panel.page = page;
    panel.dialog.open = true;
    panel.dialog.initialFocus = true;
    panel.session = connection->Session();
    panel.profile_revision = connection->Revision();
    panel.epoch = Epoch(state);
    panel.document = state.documents.ActiveId();
    if (page == OnboardingPage::Open && state.recovery) {
        if (auto recent = state.recovery->RecentWallets()) panel.recent = std::move(*recent.value);
        else panel.error = "Recent workspaces could not be read.";
    }
}

void RetireOnboarding(State& state, app::Requests& host)
{
    if (auto* wallets = host.Wallets()) wallets->Cancel();
    Close(state);
}

void DrawOnboarding(State& state, app::Requests& host, Facts& facts, const imkit::Theme& theme, float scale)
{
    auto* wallets = host.Wallets();
    auto* connection = host.Connection();
    if (!wallets || !connection) return;
    auto& panel = state.onboarding;
    if (state.recovery && state.recovery->Locked() && panel.page != OnboardingPage::None) RetireOnboarding(state, host);
    if (!state.policy_read) if (auto completed = wallets->Poll(*connection, Epoch(state), state.documents.ActiveId()); completed && panel.dialog.open) {
        auto& [action, result] = *completed;
        ++panel.revision;
        panel.error = std::move(result.error);
        if (action == WalletAction::List) {
            panel.wallets = std::move(result.entries);
            panel.listed = panel.error.empty();
            panel.directory_verified = result.directory_verified;
            panel.selected.reset();
        } else if (action == WalletAction::Backup) {
            panel.path_reviewed = false;
            if (result.backup_written) panel.error = "Backup acknowledged by the daemon. Protect the file on that host.";
        } else if (result.wallet && Current(state, connection)) {
            try {
                ApplyWallet(state, *connection, action, std::move(*result.wallet));
            } catch (...) {
                panel.error = "The verified wallet could not be opened locally. Refresh the wallet list before retrying.";
            }
        } else {
            panel.listed = false;
            panel.selected.reset();
        }
    }
    if (panel.close_pending) {
        if (ImGui::BeginPopupModal(DIALOG)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        panel.close_pending = false;
    }
    if (!panel.dialog.open) return;
    const auto* viewport = ImGui::GetMainViewport();
    const float width = std::min(590 * scale, viewport->WorkSize.x - 32 * scale);
    const float height = std::min(444 * scale, viewport->WorkSize.y - 32 * scale);
    ImGui::SetNextWindowSize({width,height}, ImGuiCond_Always);
    if (!imkit::BeginDialog(DIALOG, panel.dialog, {&theme})) {
        if (!panel.dialog.open) {
            if (wallets->Busy()) state.notice = "Local waiting cancelled. Any action accepted by the daemon may still complete. Check the wallet list.";
            RetireOnboarding(state, host);
        }
        return;
    }
    state.presentation.DialogOnly();
    const auto p = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    const auto page = panel.page;
    const char* title = "Create a daemon wallet";
    switch (page) {
    case OnboardingPage::Introduction: title = "Choose how to use Bitcoin Roots"; break;
    case OnboardingPage::Open: title = "Open a daemon wallet"; break;
    case OnboardingPage::Import: title = panel.policy_review ? "Review a distinct wallet policy" : "Import a wallet"; break;
    case OnboardingPage::Backup: title = "Back up a daemon wallet"; break;
    default: break;
    }
    state.presentation.Group("wallet-onboarding", title, c::ax::SemanticRole::Dialog, p, {p.x + size.x, p.y + size.y});
    auto text = [&](const char* id, const char* label, const std::string& value) {
        ImGui::TextWrapped("%s", value.c_str());
        state.presentation.Annotate(id, label, value);
    };
    auto action = [&](const char* id, const char* label, bool enabled, auto invoke, bool primary = false, const std::string& accessible_name = {}) {
        ImGui::PushID(id);
        ImGui::BeginDisabled(!enabled);
        const auto display = std::string(label) + "###onboarding-action";
        const auto variant = !enabled ? imkit::ActionVariant::Ghost : primary ? imkit::ActionVariant::Primary : imkit::ActionVariant::Secondary;
        const bool pressed = theme::ActionButton(display.c_str(), variant, {0,30*scale}, theme);
        FactsFor(facts, id, pressed);
        const auto access = state.presentation.Interact(accessible_name.empty() ? label : accessible_name.c_str(), c::ax::SemanticRole::Button, c::ax::SemanticAction::Press | c::ax::SemanticAction::Focus, !enabled);
        ImGui::EndDisabled();
        ImGui::PopID();
        const auto revision = panel.revision;
        const bool accepted = enabled && pressed && invoke();
        if (access) state.presentation.Defer(*access, [invoke, pressed, accepted](const c::SemanticRequest&) { return pressed ? accepted : invoke(); },
            [&panel, revision, enabled] { return enabled && panel.dialog.open && panel.revision == revision; });
    };
    auto field = [&](const char* id, const char* label, const char* help, c::Edit& value, bool busy) {
        imkit::BeginFormField(id, {label, help, "", true}, {&theme});
        ImGui::BeginDisabled(busy);
        auto edited = [&panel] { ++panel.revision; panel.path_reviewed = false; panel.error.clear(); };
        if (c::EditInput("###wallet-field", value)) edited();
        FactsFor(facts, id);
        const auto access = state.presentation.Interact(label, c::ax::SemanticRole::TextField,
            c::ax::SemanticAction::SetValue | c::ax::SemanticAction::Focus, busy, false, value.draft());
        state.presentation.BindTextPosition(facts.onboarding.at(id).id, value.draft(), value.cursor);
        if (access) state.presentation.Defer(*access, [&value, edited](const c::SemanticRequest& request) {
            if (!value.SetExternalDraft(request.value)) return false;
            edited();
            return true;
        });
        ImGui::EndDisabled();
        imkit::EndFormField();
    };
    const auto configure = [&state, connection](bool remote) {
        Close(state);
        if (!connection->Active()) connection->SetOffline(false);
        OpenServer(state.server, *connection);
        if (!connection->Active() && remote) {
            state.server.tls = true;
            connection->SetDraft(state.server.Candidate());
            state.server.scope_revision = connection->Revision();
        }
        state.server.notice = remote ? "Use your remote node's trusted endpoint and credentials. Test the network identity before applying."
            : "Use the local daemon's explicit RPC settings and test the network identity before applying.";
        return true;
    };
    std::function<bool()> submit;
    const char* submit_id = nullptr;
    const char* submit_label = nullptr;
    bool can_submit = false;
    const auto choose_method = [&panel](WalletImportMethod method) {
        panel.import_method = method;
        panel.preview.reset();
        panel.clipboard_pending = false;
        panel.path_reviewed = false;
        panel.daemon_path = c::Edit("");
        panel.error.clear();
        ++panel.revision;
        return true;
    };
    ImGui::BeginChild("onboarding-body", {0, -48*scale}, ImGuiChildFlags_NavFlattened);
    if (page == OnboardingPage::Introduction) {
        text("intro", "Connection choices", title);
        action("local", "Local node", true, [configure] { return configure(false); });
        text("local-privacy", "Local privacy", "Work with a daemon you run. Your node receives wallet RPC requests; the GUI holds no private keys.");
        action("remote", "Remote node", true, [configure] { return configure(true); });
        text("remote-privacy", "Remote privacy", "The remote operator can see wallet activity and connection information. Verify the endpoint and protect RPC credentials.");
        action("indexing", "Indexing service", false, [] { return false; });
        text("index-privacy", "Indexing availability", "Indexed address and output history requires a HistoryIndexAdapter. An indexer could link your queries.");
        text("offline-privacy", "Offline privacy", "Work with local public documents. Network requests stay disabled until you go online.");
    } else if (page == OnboardingPage::Import && panel.import_method == WalletImportMethod::Choose) {
        text("wallet-title", "Wallet action", title);
        action("method-descriptor", "Public descriptor", true, [choose_method] { return choose_method(WalletImportMethod::Descriptor); });
        action("method-xpub", "Public extended key", true, [choose_method] { return choose_method(WalletImportMethod::ExtendedKey); });
        action("method-restore", "Daemon wallet backup", true, [choose_method] { return choose_method(WalletImportMethod::Restore); });
        action("method-words", "Recovery words", true, [choose_method] { return choose_method(WalletImportMethod::RecoveryWords); });
        action("method-hardware", "Hardware wallet", true, [choose_method] { return choose_method(WalletImportMethod::Hardware); });
        action("method-foreign", "Other wallet formats", true, [choose_method] { return choose_method(WalletImportMethod::Foreign); });
    } else if (page == OnboardingPage::Import && panel.import_method >= WalletImportMethod::RecoveryWords) {
        if (panel.import_method==WalletImportMethod::RecoveryWords) {
            const auto status=LocalSignerStatus(ShippedExtensions());
            text("import-unavailable","Local software wallet unavailable",status.detail);
            text("signer-secret-entry","Trusted signer entry",
                "The signer owns the word grid, mnemonic passphrase and backup verification. The workspace password only protects local GUI data.");
            text("public-import-help","Public import","Choose Public descriptor to import supported watch-only material.");
        } else {
            const char* unavailable = panel.import_method == WalletImportMethod::Hardware
                ? "Hardware onboarding requires a configured daemon external-signer service. Use a public descriptor exported by your device when available."
                : "This wallet format requires a compatible conversion service. Export a public Bitcoin descriptor or provide a compatible daemon wallet backup.";
            text("import-unavailable", "Import requirement", unavailable);
        }
        action("methods", "Choose another method", true, [choose_method] { return choose_method(WalletImportMethod::Choose); });
    } else {
        text("wallet-title", "Wallet action", title);
        if (page==OnboardingPage::Create)
            text("local-software-unavailable","Local software wallet unavailable",LocalSignerStatus(ShippedExtensions()).detail);
        if (!Current(state, connection)) {
            text("connection-needed", "Connection required", "Test and apply an online daemon connection before opening or creating a wallet.");
            action("configure", "Connection settings", true, [configure] { return configure(false); });
            for (size_t i = 0; i < panel.recent.size(); ++i) {
                text(("recent-" + std::to_string(i)).c_str(), "Recent workspace", Display(panel.recent[i].daemon_wallet) + " · Connect to verify");
            }
        } else {
            const auto& backend = *connection->Active();
            const bool busy = wallets->Busy();
            const bool create = page == OnboardingPage::Create;
            const bool available = create ? CanCreateWallet(backend) : CanListWallets(backend);
            const auto start = [&state, connection, wallets](WalletAction kind, std::string name, WalletTransfer transfer = {}) {
                if (!Current(state, connection)) return false;
                const auto* doc=state.documents.Active();
                if (state.onboarding.policy_review && (!doc || doc->section!=app::Section::Settings
                    || app::VerifiedWalletBinding(*connection->Active())!=doc->binding)) return false;
                if (wallets->Start(kind, std::move(name), *connection, Epoch(state), state.documents.ActiveId(), std::move(transfer))) {
                    state.onboarding.error.clear();
                    ++state.onboarding.revision;
                    return true;
                }
                state.onboarding.error = "Wallet request is unavailable. Check the connection and refresh before choosing the next action.";
                return false;
            };
            if ((page == OnboardingPage::Open || create) && !available) text("wallet-unavailable", "Wallet capability", "This daemon has not proven the RPC methods and parameters required for this operation.");
            if (busy) text("wallet-pending", "Wallet request", "Waiting for the daemon. You can cancel local waiting.");
            const auto inspect = [start] { return start(WalletAction::List, {}); };
            if (page == OnboardingPage::Backup || (page == OnboardingPage::Import && panel.import_method == WalletImportMethod::Restore)) {
                const bool backup = page == OnboardingPage::Backup;
                const auto* document = state.documents.Active();
                const bool bound = document && document->binding && app::VerifiedWalletBinding(backend) == document->binding;
                const bool capable = backup ? bound && CanBackupWallet(backend) : CanRestoreWallet(backend);
                if (backup) {
                    text("backup-file", "Daemon backup scope", "The daemon writes a compatible wallet file at the destination you supply. It may contain private recovery material or replace an existing file. Use a new, protected path.");
                    text("backup-public", "Public descriptor and metadata scope", "A public descriptor records viewing policy and derivation. Keep the signing recovery material separately. GUI labels, freezes and drafts belong to the local metadata store.");
                } else {
                    text("restore-scope", "Daemon restore scope", "Provide an existing compatible wallet backup on the daemon host. The daemon checks compatibility during Restore. Backup network, policy, script and signers remain unknown before then.");
                    field("name", "New wallet name", "A new name within the daemon wallet directory.", panel.name, busy);
                }
                text("host-path", "Path location", "Use an absolute file path on the connected daemon host. The GUI sends only the path text.");
                field("daemon-path", backup ? "Daemon destination file" : "Daemon backup file", "This path is resolved by the daemon, including for remote connections.", panel.daemon_path, busy);
                const std::string path(panel.daemon_path.draft());
                const std::string name = backup && bound ? document->binding->DaemonWallet() : std::string(panel.name.draft());
                auto error = backup ? std::string{} : NewWalletNameError(name);
                if (error.empty() && !ValidDaemonWalletPath(path)) error = "Enter an absolute daemon-host filename.";
                if (error.empty() && (backup ? wallets->BackupUncertain(backend, name, path) : wallets->Uncertain(backend, name))) {
                    error = backup ? "This destination may have changed. Inspect it on the daemon host before choosing a new destination."
                        : "The wallet may have been restored. Check the current daemon wallets before continuing.";
                }
                if (!error.empty()) text("path-error", "Wallet validation", error);
                if (!capable) text("transfer-unavailable", "Wallet capability", backup ? "Select the current verified daemon wallet and an available backup RPC." : "The daemon has not proven support for compatible wallet restore.");
                const bool room = backup || state.documents.Documents().size() < 32;
                if (!room) text("capacity", "Document capacity", "Close a document before restoring a wallet.");
                if (panel.path_reviewed) {
                    text("transfer-target", "Confirm exact daemon action", "Network " + backend.chain + " · Wallet " + Display(name) + " · Path " + path);
                    submit = [start, backup, name, path] { return start(backup ? WalletAction::Backup : WalletAction::Restore, name, {{}, path}); };
                    submit_id = "transfer";
                    submit_label = backup ? "Write backup on daemon" : "Restore on daemon";
                } else {
                    submit = [&panel] { panel.path_reviewed = true; ++panel.revision; return true; };
                    submit_id = "review";
                    submit_label = backup ? "Review backup" : "Review restore";
                }
                can_submit = capable && room && !busy && error.empty();
            } else if (page == OnboardingPage::Import) {
                const bool extended = panel.import_method == WalletImportMethod::ExtendedKey;
                if (panel.policy_review) {
                    text("policy-consequence", "Policy change consequences",
                        "This policy defines a separate watch-only wallet. The original wallet and its saved policy are kept.");
                    text("policy-history", "Daemon scan consequences",
                        "The daemon scans from genesis. A pruned node cannot perform this import. Cancellation can leave a partially configured new wallet.");
                    if (panel.preview && panel.preview->descriptors.size() == 1)
                        text("single-branch", "Descriptor coverage", "Only the supplied descriptor is imported. Supply a separate change descriptor if needed.");
                } else text("import-public", "Public watch-only import", extended
                    ? "Copy an xpub or tpub. The preview derives native SegWit receiving branch 0 and change branch 1. Confirm both branches against your source."
                    : "Copy a supported public descriptor. Its preview shows policy, signer expressions and a sample script without reserving an address.");
                field("name", "New watch-only wallet name", "This creates a separate descriptor wallet on the daemon.", panel.name, busy);
                if (!panel.policy_review) {
                    const auto clipboard = ImGui::GetID("wallet-public-inspection");
                    if (panel.clipboard_pending && c::native_input) {
                        if (auto input = c::native_input->Paste(clipboard, "", 0, 0, 0, 4097)) {
                            struct ClearText {std::string& value; ~ClearText() {sodium_memzero(value.data(),value.size());}} clear{*input};
                            auto result = protocol::PreviewPublicWallet(extended ? protocol::PublicImportKind::ExtendedKey : protocol::PublicImportKind::Descriptor,
                                *input, backend.chain);
                            panel.preview = std::move(result.preview);
                            panel.error = std::move(result.error);
                            panel.clipboard_pending = false;
                            ++panel.revision;
                        } else if (!c::native_input->InspectionPending(clipboard)) {
                            panel.clipboard_pending = false;
                            panel.error = "Clipboard inspection was cancelled or did not contain bounded text.";
                        }
                    }
                    action("paste-preview", "Paste and preview public material", !busy && !panel.clipboard_pending && c::native_input,
                        [&panel, clipboard] {
                            if (!c::native_input) return false;
                            panel.preview.reset();
                            panel.error.clear();
                            panel.clipboard_pending = c::native_input->RequestInspection(clipboard);
                            if (!panel.clipboard_pending) panel.error = "Public clipboard inspection is unavailable.";
                            return panel.clipboard_pending;
                        });
                    if (panel.clipboard_pending) text("clipboard-pending", "Clipboard status", "Reading clipboard for local validation.");
                }
                if (panel.preview) {
                    text("import-network", "Verified target network", "Target network: " + panel.preview->chain);
                    text("import-custody", "Import policy", "Watch-only. The daemon scans from genesis. Ranged import requests indices 0–99. The daemon may extend its active keypool.");
                    for (size_t i = 0; i < panel.preview->descriptors.size(); ++i) {
                        const auto& descriptor = panel.preview->descriptors[i];
                        ImGui::PushID(int(i));
                        text("import-policy", "Descriptor policy", (descriptor.internal ? "Change · " : "Receive · ") + descriptor.policy);
                        text("import-script", "Sample scriptPubKey", "Sample scriptPubKey: " + descriptor.script);
                        text("import-address", "Sample address", "Sample address: " + descriptor.address);
                        for (size_t k = 0; k < descriptor.signers.size(); ++k) {
                            ImGui::PushID(int(k));
                            text("import-signer", "Public signer expression", "Public signer: " + descriptor.signers[k]);
                            ImGui::PopID();
                        }
                        text("canonical-descriptor", "Checksummed public descriptor", descriptor.descriptor);
                        ImGui::PopID();
                    }
                }
                const std::string name(panel.name.draft());
                auto error = NewWalletNameError(name);
                if (error.empty() && wallets->Uncertain(backend, name)) error = "The wallet may have changed. Inspect the daemon's current wallet list before continuing.";
                if (!error.empty()) text("name-error", "Wallet name validation", error);
                if (backend.pruned) text("pruned", "History unavailable", "Full-history import requires an unpruned daemon.");
                if (!CanImportWallet(backend)) text("import-unavailable", "Wallet capability", "This daemon has not proven the required public descriptor import parameters.");
                const bool room = state.documents.Documents().size() < 32;
                if (!room) text("capacity", "Document capacity", "Close a document before importing a wallet.");
                submit = [start, name, &panel] { return panel.preview && start(WalletAction::Import, name, {panel.preview,{}}); };
                submit_id = "import-wallet";
                submit_label = panel.policy_review ? "Create distinct wallet" : "Import watch-only wallet";
                can_submit = CanImportWallet(backend) && !backend.pruned && room && !busy && panel.preview && error.empty();
            } else if (create) {
                text("create-custody", "Wallet custody", "Create a descriptor wallet on the connected daemon. It generates and keeps the private keys. Settings opens after verification.");
                field("name", "Wallet name", "A name within the daemon wallet directory.", panel.name, busy);
                const std::string name(panel.name.draft());
                auto error = NewWalletNameError(name);
                if (error.empty() && std::ranges::any_of(panel.wallets, [&](const auto& entry) { return entry.name == name; })) error = "That wallet name already exists on the daemon.";
                if (error.empty() && wallets->Uncertain(backend, name)) error = "An earlier request may still have changed this wallet. Check its actual status from Open Wallet before proceeding.";
                if (!error.empty()) text("name-error", "Wallet name validation", error);
                const bool room = state.documents.Documents().size() < 32;
                if (!room) text("capacity", "Document capacity", "Close a document before creating a wallet.");
                submit = [start, name] { return start(WalletAction::Create, name); };
                submit_id = "create";
                submit_label = "Create on daemon";
                can_submit = available && room && !busy && error.empty();
                if (!panel.error.empty()) action("refresh", "Check wallet list", available && !busy, inspect);
            } else {
                text("open-privacy", "Daemon wallet paths", "Names refer to wallets on the connected daemon. Loading affects that daemon and uses its existing files.");
                action("refresh", panel.listed ? "Refresh wallet list" : "List daemon wallets", available && !busy, inspect);
                if (panel.listed && !panel.directory_verified) text("directory", "Wallet directory", "The daemon wallet directory could not be verified. Loaded wallets remain available.");
                if (panel.listed && panel.wallets.empty()) text("empty", "Wallet list", "No wallets were reported by the daemon.");
                auto row = [&](const DaemonWalletEntry& entry, const std::string& id, bool recent) {
                    const auto label = Display(entry.name) + (entry.loaded ? " · Loaded" : " · Unloaded");
                    text((id + "-label").c_str(), recent ? "Recent workspace" : "Daemon wallet", label);
                    action(id.c_str(), panel.selected == entry ? "Selected" : "Select", !busy, [&panel, entry] {
                        panel.selected = entry;
                        ++panel.revision;
                        return true;
                    }, false, "Select " + label);
                };
                const auto origin = app::MetadataBackend(backend.candidate);
                const auto genesis = Hash256::Parse(backend.genesis);
                std::set<std::string> recent_names;
                for (size_t i = 0; origin && genesis && i < panel.recent.size(); ++i) {
                    const auto& recent = panel.recent[i];
                    if (recent.backend != origin->value() || recent.network.genesis != *genesis) continue;
                    if (recent_names.empty()) text("recent-workspaces", "Recent workspaces", "Recent workspaces");
                    recent_names.insert(recent.daemon_wallet);
                    const auto found = std::ranges::find(panel.wallets, recent.daemon_wallet, &DaemonWalletEntry::name);
                    if (found != panel.wallets.end()) row(*found, "recent-" + std::to_string(i), true);
                    else text(("recent-" + std::to_string(i)).c_str(), "Recent workspace", Display(recent.daemon_wallet) + " · Check wallet list to verify");
                }
                for (size_t i = 0; i < panel.wallets.size(); ++i) if (!recent_names.contains(panel.wallets[i].name)) {
                    row(panel.wallets[i], "wallet-" + std::to_string(i), false);
                }
                if (panel.selected) {
                    const auto& entry = *panel.selected;
                    const bool supported = entry.loaded || CanLoadWallet(backend);
                    const bool uncertain = !entry.loaded && wallets->Uncertain(backend, entry.name);
                    if (uncertain) text("uncertain", "Wallet load state", "A previous request might still complete. Refresh and open the wallet once it is loaded.");
                    if (!supported) text("load-unavailable", "Wallet load capability", "The daemon did not prove support for loading this wallet.");
                    const bool room = state.documents.Documents().size() < 32 || std::ranges::any_of(state.documents.Documents(), [&](const auto& document) {
                        return document.binding && origin && genesis && document.binding->backend == *origin
                            && document.binding->network.genesis == *genesis && document.binding->DaemonWallet() == entry.name;
                    });
                    if (!room) text("capacity", "Document capacity", "Close a document before opening another wallet.");
                    submit = [start, entry] { return start(entry.loaded ? WalletAction::Open : WalletAction::Load, entry.name); };
                    submit_id = "open";
                    submit_label = entry.loaded ? "Open selected wallet" : "Load and open on daemon";
                    can_submit = supported && room && !uncertain && !busy;
                }
            }
        }
        if (!panel.error.empty()) text("wallet-error", "Wallet request status", panel.error);
    }
    ImGui::EndChild();
    if (page == OnboardingPage::Introduction) {
        action("offline", "Continue offline", true, [&state, connection] {
            if (!connection->SetOffline(true)) return false;
            state.notice = "Offline public-document workspace. Network transport is disabled.";
            Close(state);
            RestoreDocumentFocus(state, state.documents.ActiveId());
            return true;
        }, true);
    } else {
        if (page == OnboardingPage::Import && panel.import_method > WalletImportMethod::Choose && panel.import_method < WalletImportMethod::RecoveryWords) {
            action("methods", "Import methods", !wallets->Busy(), [choose_method] { return choose_method(WalletImportMethod::Choose); });
            ImGui::SameLine();
        }
        if (submit) {
            action(submit_id, submit_label, can_submit, std::move(submit), true);
            ImGui::SameLine();
        }
        action("cancel", "Cancel", true, [&state, &host, wallets] {
            const bool busy = wallets->Busy();
            RetireOnboarding(state, host);
            if (busy) state.notice = "Local waiting cancelled. Any action accepted by the daemon may still complete. Check the wallet list.";
            RestoreDocumentFocus(state, state.documents.ActiveId());
            return true;
        });
    }
    state.presentation.EndGroup();
    imkit::EndDialog();
}
}
