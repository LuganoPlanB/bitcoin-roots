// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include <algorithm>

namespace roots::gui::ui::shell {
namespace {
constexpr char POPUP[] = "Sweep Private Key###sweep-notice";
constexpr char SIGNED_PREVIEW[] =
    "The Roots sweepprivkeys RPC receives private keys and signs even its preview. "
    "Its broadcast flag controls whether the signed transaction is submitted.";
constexpr char MISSING_PROVIDER[] =
    "An unsigned sweep needs public-output discovery, a verified draft and a separate compatible signer.";
constexpr char NO_NETWORK[] =
    "This screen accepts no keys and sends nothing to the node.";

uint64_t ProfileRevision(app::Requests& host)
{
    const auto* connection = host.Connection();
    return connection ? connection->Revision() : 0;
}

bool Current(const State& s, app::Requests& host)
{
    const auto scope = CurrentToolScope(s, host);
    return s.sweep.dialog.open && !scope.hidden && !scope.locked
        && s.sweep.scope == scope && s.sweep.profile_revision == ProfileRevision(host);
}

void Text(State& s, const char* id, const char* label, const char* text)
{
    ImGui::TextWrapped("%s", text);
    s.presentation.Annotate(id, label, text);
}
}

bool OpenSweepNotice(State& s, app::Requests& host)
{
    const auto scope = CurrentToolScope(s, host);
    if (s.sweep.dialog.open || scope.hidden || scope.locked) return false;
    s.sweep = SweepNotice{.dialog = {.open = true, .initialFocus = true},
                         .scope = scope, .profile_revision = ProfileRevision(host)};
    return true;
}

void RetireSweepNotice(State& s)
{
    s.sweep = SweepNotice{.close_pending = true};
}

void DrawSweepNotice(State& s, app::Requests& host, Facts& f, const imkit::Theme& theme, float scale)
{
    auto& notice = s.sweep;
    if (notice.dialog.open && !Current(s, host)) RetireSweepNotice(s);
    if (notice.close_pending) {
        if (ImGui::BeginPopupModal(POPUP)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        notice.close_pending = false;
    }
    if (!notice.dialog.open) return;

    const auto* viewport = ImGui::GetMainViewport();
    const float width = std::max(1.F, std::min(490 * scale, viewport->WorkSize.x - 32 * scale));
    ImGui::SetNextWindowSizeConstraints({width, 0}, {width, std::max(1.F, viewport->WorkSize.y - 32 * scale)});
    const bool focus = notice.dialog.initialFocus;
    if (!imkit::BeginDialog(POPUP, notice.dialog, {&theme})) {
        if (!notice.dialog.open) RetireSweepNotice(s);
        return;
    }

    f.sweep_visible = true;
    s.presentation.DialogOnly();
    const auto position = ImGui::GetWindowPos();
    const auto size = ImGui::GetWindowSize();
    using components::ax::SemanticRole;
    using components::ax::SemanticAction;
    s.presentation.Group("sweep-private-key", "Sweep Private Key", SemanticRole::Dialog, position,
                         {position.x + size.x, position.y + size.y});
    const char* status = "Unsigned sweep unavailable";
    imkit::PushTextRole(theme, imkit::TextRole::Heading);
    ImGui::TextUnformatted(status);
    imkit::PopTextRole();
    s.presentation.Annotate("sweep-status", status, {}, SemanticRole::Status);
    Text(s, "sweep-rpc", "Daemon sweep contract", SIGNED_PREVIEW);
    Text(s, "sweep-dependency", "Required providers", MISSING_PROVIDER);
    Text(s, "sweep-network", "Key input and network", NO_NETWORK);

    if (focus) ImGui::SetKeyboardFocusHere();
    const bool pressed = theme::ActionButton("Close", imkit::ActionVariant::Secondary, {}, theme);
    const auto lo = ImGui::GetItemRectMin();
    const auto hi = ImGui::GetItemRectMax();
    f.sweep_close = {{lo.x, lo.y, hi.x, hi.y}, ImGui::GetItemID(), ImGui::IsItemFocused(),
                     false, false, pressed, ImGui::IsItemVisible()};
    const auto external = s.presentation.Interact("Close", SemanticRole::Button, SemanticAction::Press | SemanticAction::Focus);
    const auto close = [&s, &host] {
        if (!Current(s, host)) return false;
        RetireSweepNotice(s);
        return true;
    };
    if (pressed && close()) {
        ImGui::CloseCurrentPopup();
        notice.close_pending = false;
    }
    if (external) s.presentation.Defer(*external, [close](const auto&) { return close(); });
    s.presentation.EndGroup();
    imkit::EndDialog();
}
}
