// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"

#include <algorithm>

namespace roots::gui::ui::shell {
namespace {
namespace c = components;
constexpr char POPUP[] = "Verify Download###verify-download";
constexpr std::array LABELS{"Release file", "SHA-256 manifest", "Detached signature", "Trusted public key"};
constexpr std::array IDS{"artifact", "manifest", "signature", "public-key"};

uint64_t ProfileRevision(app::Requests& host)
{
    const auto* connection = host.Connection();
    return connection ? connection->Revision() : 0;
}

bool Current(const State& s, app::Requests& host)
{
    const auto scope = CurrentToolScope(s, host);
    return s.download.dialog.open && !scope.hidden && !scope.locked && s.download.scope == scope
        && s.download.profile_revision == ProfileRevision(host);
}

void Invalidate(State& s)
{
    s.download_verifier.Cancel();
    s.download.request.reset();
    s.download.result.reset();
    s.download.notice = nullptr;
    ++s.download.revision;
}

c::ItemFacts Last(bool pressed = false, bool disabled = false)
{
    const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    return {{lo.x, lo.y, hi.x, hi.y}, ImGui::GetItemID(), ImGui::IsItemFocused(),
            disabled, false, pressed, ImGui::IsItemVisible()};
}

void Text(State& s, const char* id, const char* name, std::string_view text, c::ax::SemanticRole role = c::ax::SemanticRole::Text)
{
    ImGui::TextWrapped("%.*s", int(text.size()), text.data());
    s.presentation.Annotate(id, name, text, role);
}

void ResultLine(State& s, const char* id, const char* label, const char* value)
{
    ImGui::TextWrapped("%s  %s", label, value);
    s.presentation.Annotate(id, label, value, c::ax::SemanticRole::Status);
}

void Button(State& s, Facts& f, const imkit::Theme& theme, const char* id, const char* label,
            bool enabled, std::function<bool()> action, const char* semantic = nullptr)
{
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = theme::ActionButton(label, imkit::ActionVariant::Secondary, {}, theme);
    f.download[id] = Last(pressed, !enabled);
    using enum c::ax::SemanticAction;
    const auto outside = s.presentation.Interact(semantic ? semantic : label, c::ax::SemanticRole::Button, Press | Focus, !enabled);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted = enabled && pressed && action();
    if (outside) s.presentation.Defer(*outside, [action = std::move(action), enabled, pressed, accepted](const auto&) {
        return enabled && (pressed ? accepted : action());
    });
}

template<size_t Size>
void Field(State& s, app::Requests& host, Facts& f, const char* id, const char* label, std::array<char, Size>& field,
           std::optional<std::string>& external, float label_width, float field_width, bool focus = false)
{
    ImGui::PushID(id);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(label_width);
    ImGui::SetNextItemWidth(field_width);
    if (focus) ImGui::SetKeyboardFocusHere();
    const bool injected = external.has_value();
    if (c::TextInput("##download-input", nullptr, field.data(), field.size(), external, 0, nullptr, true) && !injected)
        Invalidate(s);
    f.download[id] = Last();
    const auto outside = s.presentation.Interact(label, c::ax::SemanticRole::TextField,
        c::ax::SemanticAction::SetValue | c::ax::SemanticAction::Focus);
    ImGui::PopID();
    if (outside) s.presentation.Defer(*outside, [&s, &host, &field, &external](const auto& value) {
        if (!Current(s, host) || s.download.chooser || !rpc::ValidUtf8(value.value)
            || !c::SetExternalBuffer(field, external, value.value)) return false;
        Invalidate(s);
        return true;
    });
}

void Poll(State& s, app::Requests& host)
{
    auto& panel = s.download;
    if (panel.chooser && c::native_input) {
        const auto choice = c::native_input->PollFile(panel.chooser);
        if (choice.outcome != c::NativeOutcome::Pending) {
            panel.chooser = 0;
            if (choice.outcome == c::NativeOutcome::Chosen && ValidLocalDocumentPath(choice.path)
                && c::SetExternalBuffer(panel.paths[panel.choosing], panel.external_paths[panel.choosing], choice.path)) Invalidate(s);
            else if (choice.outcome != c::NativeOutcome::Cancelled) panel.notice = "The file choice is unavailable. Existing selection retained.";
        }
    }
    auto reply = s.download_verifier.Poll(Current(s, host) && panel.request ? &*panel.request : nullptr);
    if (reply) {
        panel.request.reset();
        panel.result = std::move(*reply);
    }
    if (panel.result && panel.result->Changed()) {
        panel.result.reset();
        panel.notice = DownloadText(DownloadIssue::Changed);
    }
}

bool Start(State& s, app::Requests& host)
{
    auto& panel = s.download;
    if (!Current(s, host) || panel.chooser || s.download_verifier.Busy()) return false;
    DownloadRequest request{panel.revision, {}};
    for (size_t i = 0; i < LABELS.size(); ++i) {
        request.selections.files[i] = panel.paths[i].data();
        if (!ValidLocalDocumentPath(request.selections.files[i])) {
            panel.notice = "Select four readable absolute file paths before verifying.";
            return false;
        }
    }
    if (panel.fingerprint.front()) {
        auto fingerprint = DownloadFingerprint(panel.fingerprint.data());
        if (!fingerprint) {
            panel.notice = "Enter the full 40-digit primary fingerprint, or leave it blank.";
            return false;
        }
        request.selections.expected_fingerprint = std::move(*fingerprint);
    }
    Invalidate(s);
    request.revision = panel.revision;
    const bool started = s.download_verifier.Start(request);
    if (started) panel.request = std::move(request);
    else panel.notice = "The verifier could not start.";
    return started;
}

void DrawResult(State& s, const DownloadResult& result)
{
    if (result.issue != DownloadIssue::None) Text(s, "verify-issue", "Verification issue", DownloadText(result.issue), c::ax::SemanticRole::Alert);
    ResultLine(s, "integrity", "File integrity", DownloadText(result.integrity));
    ResultLine(s, "manifest-signature", "Manifest signature", DownloadText(result.signature));
    ResultLine(s, "signer-trust", "Signer trust", DownloadText(result.trust));
    if (!result.primary_fingerprint.empty()) {
        ResultLine(s, "primary-fingerprint", "Primary fingerprint", result.primary_fingerprint.c_str());
        if (result.signing_fingerprint != result.primary_fingerprint)
            ResultLine(s, "signing-subkey", "Signing subkey", result.signing_fingerprint.c_str());
    }
    if (result.files) Text(s, "snapshot-scope", "Snapshot scope", "These checks apply to the captured file bytes.");
}

} // namespace

bool OpenDownload(State& s, app::Requests& host)
{
    const auto scope = CurrentToolScope(s, host);
    if (scope.hidden || scope.locked || s.download.dialog.open) return false;
    auto& panel = s.download;
    panel.dialog = {.open = true, .initialFocus = true};
    panel.scope = scope;
    panel.profile_revision = ProfileRevision(host);
    return true;
}

void RetireDownload(State& s)
{
    const auto revision = s.download.revision;
    s.download_verifier.Cancel();
    s.download = DownloadPanel{};
    s.download.close_pending = true;
    s.download.revision = revision + 1;
}

void DrawDownload(State& s, app::Requests& host, Facts& f, const imkit::Theme& theme, float scale)
{
    auto& panel = s.download;
    if (panel.dialog.open && !Current(s, host)) RetireDownload(s);
    Poll(s, host);
    if (panel.close_pending) {
        if (ImGui::BeginPopupModal(POPUP)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        panel.close_pending = false;
    }
    if (!panel.dialog.open) return;

    const auto* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize({std::min(650 * scale, vp->WorkSize.x - 24 * scale),
                             std::min(432 * scale, vp->WorkSize.y - 24 * scale)}, ImGuiCond_Always);
    const bool focus = panel.dialog.initialFocus;
    if (!imkit::BeginDialog(POPUP, panel.dialog, {&theme})) {
        if (!panel.dialog.open) RetireDownload(s);
        return;
    }
    f.download_visible = true;
    s.presentation.DialogOnly();
    const auto pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    s.presentation.Group("download-verification", "Verify Download", c::ax::SemanticRole::Dialog, pos,
                         {pos.x + size.x, pos.y + size.y});
    const bool configured = DownloadVerifierConfigured();
    const bool native = c::native_input && c::native_input->FileChooserAvailable();
    const bool has_footer_status = panel.request || panel.notice;
    const float footer = ImGui::GetFrameHeightWithSpacing()
        + (has_footer_status ? ImGui::GetTextLineHeightWithSpacing() : 0.F);
    ImGui::BeginChild("download-inputs", {0, -footer}, ImGuiChildFlags_NavFlattened);
    if (!configured) Text(s, "verifier-missing", "Verifier dependency", DownloadText(DownloadIssue::Unavailable), c::ax::SemanticRole::Alert);
    else {
        const float offset = 134 * scale;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {ImGui::GetStyle().FramePadding.x, 4 * scale});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {ImGui::GetStyle().ItemSpacing.x, 5 * scale});
        for (size_t i = 0; i < LABELS.size(); ++i) {
            Field(s, host, f, IDS[i], LABELS[i], panel.paths[i], panel.external_paths[i], offset, -103 * scale, focus && i == 0);
            if (panel.paths[i].front() && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 360 * scale);
                ImGui::TextUnformatted(panel.paths[i].data());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            ImGui::SameLine();
            ImGui::PushID(IDS[i]);
            const auto button_id = std::string(IDS[i]) + "-choose";
            const auto semantic = std::string("Browse for ") + LABELS[i];
            Button(s, f, theme, button_id.c_str(), "Browse", native && !panel.chooser && !s.download_verifier.Busy(),
                [&s, &host, i] {
                    auto* input = c::native_input;
                    if (!Current(s, host) || !input || !input->FileChooserAvailable() || s.download.chooser || s.download_verifier.Busy()) return false;
                    const auto chooser = input->ChooseFile(s.download.paths[i].data());
                    if (!chooser) return false;
                    Invalidate(s);
                    s.download.choosing = i;
                    s.download.chooser = chooser;
                    return true;
                }, semantic.c_str());
            ImGui::PopID();
        }
        Field(s, host, f, "fingerprint", "Expected fingerprint", panel.fingerprint, panel.external_fingerprint, offset, -1);
        ImGui::PopStyleVar(2);
        Text(s, "trust-source", "Trust source", "Enter the primary fingerprint from your independent source. Leave it blank to inspect without trust.");
        if (panel.result) DrawResult(s, *panel.result);
        else Text(s, "supported-format", "Supported formats", "Flat SHA-256 manifest, one detached OpenPGP binary-document signature, and one exported public key.");
    }
    ImGui::EndChild();

    if (panel.request) Text(s, "progress", "Verification progress", DownloadText(s.download_verifier.Stage()), c::ax::SemanticRole::Status);
    else if (panel.notice) Text(s, "notice", "Verification result", panel.notice, c::ax::SemanticRole::Status);
    Button(s, f, theme, "verify", "Verify", configured && !panel.chooser && !s.download_verifier.Busy(),
           [&s, &host] { return Start(s, host); });
    if (panel.request) {
        ImGui::SameLine();
        Button(s, f, theme, "cancel", "Cancel verification", true, [&s, &host] {
            if (!Current(s, host) || !s.download.request) return false;
            Invalidate(s);
            s.download.notice = DownloadText(DownloadIssue::Cancelled);
            return true;
        });
    }
    ImGui::SameLine();
    Button(s, f, theme, "done", "Done", true, [&s, &host] {
        if (!Current(s, host)) return false;
        RetireDownload(s);
        return true;
    });
    if (!panel.dialog.open) {
        ImGui::CloseCurrentPopup();
        panel.close_pending = false;
    }
    s.presentation.EndGroup();
    imkit::EndDialog();
}

} // namespace roots::gui::ui::shell
