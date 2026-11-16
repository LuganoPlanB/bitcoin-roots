// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/bootstrap.h"
#include "ui/theme/theme.h"
#include <imkit/imkit.h>
#include <algorithm>
namespace roots::gui::ui {
imkit::Theme MakeBootstrapTheme(const BootstrapAppearance& appearance)
{
    return theme::Make(appearance.dark, appearance.font_scale * appearance.dpi_scale);
}
BootstrapFacts DrawBootstrap(const imkit::Theme& theme, const BootstrapAppearance& appearance, BootstrapState& state)
{
    BootstrapFacts facts;
    const float scale = appearance.font_scale * appearance.dpi_scale;
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;
    ImGui::Begin("Workspace###roots-bootstrap", nullptr, flags);
    imkit::PushTextRole(theme, imkit::TextRole::Heading);
    ImGui::TextUnformatted("Bitcoin Roots");
    imkit::PopTextRole();
    ImGui::TextColored(theme.semantic.textSecondary, "RPC wallet");
    if (state.show_appearance) {
        ImGui::SetNextItemWidth(160 * scale);
        if (ImGui::Combo("Appearance###appearance", &state.appearance_choice, "Light\0Dark\0System\0"))
            facts.appearance_requested = state.appearance_choice;
        if (state.appearance_error) ImGui::TextUnformatted("Appearance could not be saved. Choose again to retry.");
        if (state.system_unknown) ImGui::TextUnformatted("System appearance is unavailable; using Light.");
    }
    const auto available = ImGui::GetContentRegionAvail();
    const float footer = 40 * scale;
    const float card_width = std::min(450 * scale, available.x);
    const float card_height = std::min(230 * scale, std::max(1.F, available.y - footer - 16 * scale));
    const auto origin = ImGui::GetCursorPos();
    ImGui::SetCursorPos({origin.x + (available.x - card_width) / 2, origin.y + std::max(8 * scale, (available.y - footer - card_height) / 2)});
    const bool card = imkit::BeginCard("empty-workspace", {card_width, card_height}, {&theme});
    if (card) {
        imkit::PushTextRole(theme, imkit::TextRole::Heading);
        ImGui::TextWrapped("Your workspace is empty");
        imkit::PopTextRole();
        ImGui::TextWrapped("No wallets are open.");
        const auto content = ImGui::GetContentRegionAvail();
        const float button_height = std::max(32 * scale, ImGui::GetFrameHeight());
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + std::max(0.F, content.y - button_height));
        if (state.initial_focus) ImGui::SetKeyboardFocusHere();
        facts.close_requested = theme::ActionButton("Close###bootstrap-close", imkit::ActionVariant::Primary,
                                                   {std::min(140 * scale, content.x), button_height}, theme);
        facts.close_visible = ImGui::IsItemVisible();
        facts.close_focused = ImGui::IsItemFocused();
        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        facts.close_x0 = a.x; facts.close_y0 = a.y; facts.close_x1 = b.x; facts.close_y1 = b.y;
        facts.font_size = ImGui::GetFontSize();
        facts.frame_padding_y = ImGui::GetStyle().FramePadding.y;
        state.initial_focus = false;
    }
    imkit::EndCard();
    ImGui::SetCursorPos({origin.x, ImGui::GetWindowSize().y - ImGui::GetStyle().WindowPadding.y - ImGui::GetTextLineHeight()});
    ImGui::TextColored(theme.semantic.textSecondary, "Offline  |  No node connection");
    ImGui::End();
    return facts;
}
std::string BuildInfo()
{
    const auto theme = imkit::MakePrecisionTheme(imkit::ColorScheme::Dark);
    return "\"imgui\":" + std::to_string(IMGUI_VERSION_NUM) + ",\"imkit\":\"" IMKIT_VERSION "\",\"body_size\":" + std::to_string(theme.metrics.bodySize);
}
}
