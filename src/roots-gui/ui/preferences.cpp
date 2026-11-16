// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/preferences.h"
#include "ui/theme/theme.h"
#include <imkit/imkit.h>
#include <algorithm>
namespace roots::gui::ui {
PreferenceFacts DrawPreferences(PreferencePanel& state, PreferenceRequests& service, const imkit::Theme& theme)
{
    PreferenceFacts facts;
    ImGui::Begin("Preferences###preferences", nullptr, ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("Refresh interval (seconds)");
    ImGui::SetNextItemWidth(std::min(320.F * ImGui::GetFontSize()/14, ImGui::GetContentRegionAvail().x));
    ImGui::InputInt("###refresh", &state.draft.refresh_seconds);
    ImGui::Checkbox("Hide balances###hide-balances", &state.draft.hide_balances);
    ImGui::BeginDisabled(!ValidPreferences(state.draft));
    if (theme::ActionButton("Save###save", imkit::ActionVariant::Primary, {}, theme)) {
        state.save_rejected = !SavePreferences(state.draft, service);
        if (!state.save_rejected) state.saved = state.draft;
    }
    auto a=ImGui::GetItemRectMin(), b=ImGui::GetItemRectMax();
    facts.save={a.x,a.y,b.x,b.y};facts.save_visible=ImGui::IsItemVisible();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (theme::ActionButton("Cancel###cancel", imkit::ActionVariant::Secondary, {}, theme)) {
        CancelPreferences(state.draft, state.saved, service);
        state.save_rejected = false;
    }
    a=ImGui::GetItemRectMin();b=ImGui::GetItemRectMax();
    facts.cancel={a.x,a.y,b.x,b.y};facts.cancel_visible=ImGui::IsItemVisible();facts.font_size=ImGui::GetFontSize();
    if (state.save_rejected) ImGui::TextUnformatted("Preferences could not be saved.");
    ImGui::End();
    return facts;
}
}
