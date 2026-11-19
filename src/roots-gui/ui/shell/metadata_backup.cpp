// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"

#include "app/history_workspace.h"
#include "ui/theme/theme.h"
#include <sodium.h>

namespace roots::gui::ui::shell {
namespace {
namespace c = components;
using E = storage::Error;

uint64_t ProfileRevision(app::Requests& host)
{
    const auto* connection = host.Connection();
    return connection ? connection->Revision() : 0;
}

bool Current(const State& s, app::Requests& host)
{
    return s.recovery_dialog.open && s.metadata_backup.mode != MetadataBackupMode::None && CanOpenMetadataBackup(s)
        && s.metadata_backup.scope == CurrentToolScope(s, host)
        && s.metadata_backup.profile_revision == ProfileRevision(host)
        && s.metadata_backup.binding == s.documents.Active()->binding;
}

void Wipe(std::optional<std::string>& value)
{
    if (value) sodium_memzero(value->data(), value->size());
    value.reset();
}

void ClearPasswords(MetadataBackupPanel& panel)
{
    sodium_memzero(panel.password.data(), panel.password.size());
    sodium_memzero(panel.confirmation.data(), panel.confirmation.size());
    Wipe(panel.external_password);
    Wipe(panel.external_confirmation);
    // Replace any active ImGui buffer on the next draw as well.
    panel.external_password.emplace();
    panel.external_confirmation.emplace();
}

void Text(State& s, const char* id, const char* label, std::string_view value, c::ax::SemanticRole role = c::ax::SemanticRole::Text)
{
    ImGui::TextWrapped("%.*s", int(value.size()), value.data());
    s.presentation.Annotate(id, label, value, role);
}

std::string Counts(size_t labels, size_t freezes)
{
    return "Labels " + std::to_string(labels) + " · Freezes " + std::to_string(freezes);
}

void Button(State& s, Facts& f, const imkit::Theme& theme, const char* id, const char* label,
            bool available, std::function<bool()> activate)
{
    ImGui::PushID(id);
    ImGui::BeginDisabled(!available);
    const bool pressed = theme::ActionButton(label, imkit::ActionVariant::Secondary, {}, theme);
    const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    f.document_actions[id] = {{lo.x, lo.y, hi.x, hi.y}, ImGui::GetItemID(), ImGui::IsItemFocused(),
                              !available, false, pressed, ImGui::IsItemVisible()};
    const auto outside = s.presentation.Interact(label, c::ax::SemanticRole::Button,
        c::ax::SemanticAction::Press | c::ax::SemanticAction::Focus, !available);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted = pressed && available && activate();
    if (outside) s.presentation.Defer(*outside, [pressed, accepted, activate = std::move(activate)](const auto&) {
        return pressed ? accepted : activate();
    });
}

template<size_t N>
void Field(State& s, app::Requests& host, Facts& f, const char* id, const char* title, std::array<char,N>& field,
           std::optional<std::string>& external, float label_width, float width, bool password = false, bool focus = false)
{
    auto& panel = s.metadata_backup;
    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title);
    ImGui::SameLine(label_width);
    ImGui::SetNextItemWidth(width);
    if (focus) ImGui::SetKeyboardFocusHere();
    const bool injected = external.has_value();
    if(c::TextInput("##archive-input", nullptr, field.data(), field.size(), external,
                    password ? ImGuiInputTextFlags_Password : 0, nullptr, true) && !injected) {
        ++panel.revision;
        panel.inspected.reset();
        panel.notice.clear();
        panel.restored_name.clear();
    }
    const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    f.document_actions[id] = {{lo.x, lo.y, hi.x, hi.y}, ImGui::GetItemID(), ImGui::IsItemFocused(), false, false, false, ImGui::IsItemVisible()};
    const auto action = s.presentation.Interact(title, c::ax::SemanticRole::TextField,
        c::ax::SemanticAction::SetValue | c::ax::SemanticAction::Focus);
    ImGui::PopID();
    if (action) s.presentation.Defer(*action, [&s, &host, &field, &external](const auto& request) {
        if (!Current(s, host) || s.metadata_backup.chooser || !rpc::ValidUtf8(request.value)
            || !c::SetExternalBuffer(field, external, request.value)) return false;
        ++s.metadata_backup.revision;
        s.metadata_backup.inspected.reset();
        s.metadata_backup.notice.clear();
        s.metadata_backup.restored_name.clear();
        return true;
    });
}

const char* Failure(E error)
{
    switch (error) {
    case E::Authentication: return "Archive password is incorrect or the encrypted file is damaged.";
    case E::Corrupt: return "This file is not a supported wallet metadata archive.";
    case E::Resource: return "The archive exceeds the local resource limit.";
    case E::NotFound: return "The selected archive file could not be found.";
    case E::Unsupported: return "Authenticated metadata archives need the local Linux private-file implementation.";
    default: return "The archive or metadata store could not be opened or written. Existing data is retained.";
    }
}

bool InspectOrExport(State& s, app::Requests& host)
{
    auto& panel = s.metadata_backup;
    if (!Current(s, host) || panel.chooser || panel.inspected) return false;
    const auto scope = app::HistoryMetadataScope(*panel.binding);
    const std::string_view password(panel.password.data());
    const bool exporting = panel.mode == MetadataBackupMode::Export;
    if (!ValidLocalDocumentPath(panel.path.data()) || password.size() < 12
        || (exporting && password != panel.confirmation.data())) {
        panel.notice = "Choose an absolute path and enter an archive password of at least 12 bytes. Confirm it when exporting.";
        return false;
    }

    panel.restored_name.clear();
    ++panel.revision;
    const std::filesystem::path filename(panel.path.data());
    if (exporting) {
        const auto result = s.recovery->ExportWalletMetadata(scope, s.documents.Active()->name, filename, password);
        ClearPasswords(panel);
        if (!result) {
            panel.notice = result.error == E::Conflict ? "Choose a new backup path. Existing files are never replaced." : Failure(result.error);
            return false;
        }
        panel.notice = "Encrypted metadata backup saved. Keep the archive password separately.";
        return true;
    }

    auto inspected = s.recovery->InspectWalletMetadata(scope, filename, password);
    ClearPasswords(panel);
    if (!inspected) {
        panel.notice = inspected.error == E::Conflict ? "The archive belongs to a different backend, network or wallet." : Failure(inspected.error);
        return false;
    }
    panel.notice.clear();
    panel.inspected = std::move(*inspected.value);
    return true;
}

bool Apply(State& s, app::Requests& host)
{
    auto& panel = s.metadata_backup;
    if (!Current(s, host) || !panel.inspected) return false;
    const auto scope = app::HistoryMetadataScope(*panel.binding);
    auto restored_name = panel.inspected->archive.display_name;
    auto display_name = restored_name;
    const auto labels = panel.inspected->archive.labels.size();
    const auto freezes = panel.inspected->archive.freezes.size();
    ++panel.revision;
    const auto committed = s.recovery->ApplyWalletMetadata(scope, *panel.inspected);
    panel.inspected.reset();
    if (!committed) {
        panel.notice = committed.error == E::Conflict ? "Wallet metadata changed. Inspect the archive again before applying it." : Failure(committed.error);
        return false;
    }

    auto* wallet = s.documents.Active();
    panel.restored_name = std::move(display_name);
    panel.restored_labels = labels;
    panel.restored_freezes = freezes;
    s.documents.Rename(wallet->id, std::move(restored_name));
    wallet->history.metadata_loaded = false;
    wallet->coins.metadata_loaded = false;
    if (auto receive = s.receives.find(wallet->id); receive != s.receives.end() && !receive->second.label.dirty())
        receive->second.label_loaded = false;
    s.coin_controls.InvalidateMetadata(scope);
    panel.notice = "Local wallet metadata restored.";
    return true;
}
}

bool CanOpenMetadataBackup(const State& s)
{
    const auto* wallet = s.documents.Active();
    return storage::WalletMetadataBackupAvailable() && s.recovery && !s.recovery->Pending() && !s.recovery->Locked()
        && s.recovery->Error().empty() && !s.privacy.hidden && !s.semantic_hide_toggle && wallet
        && wallet->kind == app::DocumentKind::Wallet && wallet->binding && wallet->observation != app::ObservationState::Locked;
}

bool OpenMetadataBackup(State& s, app::Requests& host, MetadataBackupMode mode)
{
    if (!s.recovery_dialog.open || !CanOpenMetadataBackup(s) || mode == MetadataBackupMode::None) return false;
    RetireMetadataBackup(s);
    auto& panel = s.metadata_backup;
    panel.mode = mode;
    ++panel.revision;
    panel.scope = CurrentToolScope(s, host);
    panel.profile_revision = ProfileRevision(host);
    panel.binding = s.documents.Active()->binding;
    panel.initial_focus = true;
    return true;
}

void RetireMetadataBackup(State& s)
{
    auto& panel = s.metadata_backup;
    if (panel.mode == MetadataBackupMode::None) return;
    const auto revision=panel.revision;
    ClearPasswords(panel);
    panel = MetadataBackupPanel{};
    panel.revision=revision+1;
}

bool DrawMetadataBackup(State& s, app::Requests& host, Facts& f, const imkit::Theme& theme, float scale)
{
    auto& panel = s.metadata_backup;
    if (!Current(s, host)) {
        RetireMetadataBackup(s);
        return false;
    }
    const bool exporting = panel.mode == MetadataBackupMode::Export;
    const bool inspecting = panel.inspected.has_value();
    if (panel.chooser && c::native_input) {
        const auto choice = c::native_input->PollFile(panel.chooser);
        if (choice.outcome != c::NativeOutcome::Pending) {
            panel.chooser = 0;
            if (choice.outcome == c::NativeOutcome::Chosen && ValidLocalDocumentPath(choice.path)
                && c::SetExternalBuffer(panel.path, panel.external_path, choice.path)) {
                ++panel.revision;
                panel.notice.clear();
                panel.restored_name.clear();
            } else if (choice.outcome != c::NativeOutcome::Cancelled) {
                panel.notice = "The file choice was unavailable. Existing selection retained.";
            }
        }
    }

    const auto* title = exporting ? "Wallet metadata backup" : "Wallet metadata restore";
    imkit::PushTextRole(theme, imkit::TextRole::Heading);
    ImGui::TextUnformatted(title);
    imkit::PopTextRole();
    s.presentation.Annotate("metadata-backup-title", title, {});
    Text(s, "metadata-backup-selected", "Selected wallet", "Selected wallet  " + s.documents.Active()->name);
    Text(s, "metadata-backup-content", "Archive contents",
         "Includes the local display name, labels and freezes for this wallet. Use the daemon wallet backup or signer recovery material to recover funds.");
    if (!inspecting && panel.restored_name.empty()) {
        Text(s, "metadata-backup-path-scope", "File and password",
             exporting ? "Choose a new file in a private folder. Set a separate archive password."
                       : "Choose an encrypted metadata archive and enter its archive password.");
        const float offset = 130 * scale;
        Field(s, host, f, "metadata-file", "Archive file", panel.path, panel.external_path, offset, -70 * scale,
              false, std::exchange(panel.initial_focus, false));
        ImGui::SameLine();
        const bool browse = c::native_input && c::native_input->FileChooserAvailable() && !panel.chooser;
        Button(s, f, theme, "metadata-browse", "Browse", browse, [&s, &host, exporting] {
            auto* input = c::native_input;
            if (!Current(s, host) || !input || !input->FileChooserAvailable() || s.metadata_backup.chooser) return false;
            s.metadata_backup.chooser = input->ChooseFile(s.metadata_backup.path.data(), exporting);
            if(s.metadata_backup.chooser)++s.metadata_backup.revision;
            return s.metadata_backup.chooser != 0;
        });
        Field(s, host, f, "metadata-password", "Archive password", panel.password, panel.external_password, offset, -1, true);
        if (exporting) Field(s, host, f, "metadata-confirmation", "Confirm password", panel.confirmation,
                             panel.external_confirmation, offset, -1, true);
        Button(s, f, theme, "metadata-inspect-export", exporting ? "Save encrypted backup" : "Inspect archive", !panel.chooser,
               [&s, &host] { return InspectOrExport(s, host); });
        ImGui::SameLine();
    }
    if (panel.inspected) {
        const auto& candidate = panel.inspected->archive;
        Text(s, "metadata-reviewed-name", "Archive display name", candidate.display_name);
        Text(s, "metadata-reviewed-counts", "Archive counts", Counts(candidate.labels.size(), candidate.freezes.size()));
        Text(s, "metadata-replace-instruction", "Apply scope",
             "Applying replaces the selected wallet's saved display name, labels and freezes. Drafts and submission records stay separate.");
        const auto revision=panel.revision;
        Button(s, f, theme, "metadata-apply", "Apply inspected metadata", true, [&s, &host, revision] {
            return s.metadata_backup.revision==revision && Apply(s, host);
        });
        ImGui::SameLine();
    }
    if (!panel.restored_name.empty()) {
        Text(s, "metadata-restored-name", "Restored local display name", "Display name  " + panel.restored_name);
        Text(s, "metadata-restored-counts", "Restored local records", Counts(panel.restored_labels, panel.restored_freezes));
    }
    if (!panel.notice.empty()) Text(s, "metadata-backup-status", "Metadata archive status", panel.notice, c::ax::SemanticRole::Status);
    Button(s, f, theme, "metadata-back", "Back to Recovery", !panel.chooser, [&s, &host] {
        if (!Current(s, host) || s.metadata_backup.chooser) return false;
        RetireMetadataBackup(s);
        return true;
    });
    return true;
}

}
