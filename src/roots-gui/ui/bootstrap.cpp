// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/bootstrap.h"
#include <imkit/imkit.h>
#include <algorithm>
namespace roots::gui::ui {
namespace {
ImVec4 Color(unsigned int rgb, float alpha = 1)
{
    return {float((rgb >> 16) & 255) / 255, float((rgb >> 8) & 255) / 255, float(rgb & 255) / 255, alpha};
}
}
imkit::Theme MakeBootstrapTheme(const BootstrapAppearance& appearance)
{
    auto theme = imkit::MakePrecisionTheme(appearance.dark ? imkit::ColorScheme::Dark : imkit::ColorScheme::Light);
    const float scale = appearance.font_scale * appearance.dpi_scale;
    auto& s = theme.semantic;
    s.canvas = Color(appearance.dark ? 0x171717 : 0xFFFEFA);
    s.surface = Color(appearance.dark ? 0x211D1D : 0xFCFCFC);
    s.surfaceRaised = Color(appearance.dark ? 0x292424 : 0xF3F8FD);
    s.overlay = s.surface;
    s.text = Color(appearance.dark ? 0xFFFFFF : 0x030B20);
    s.textSecondary = Color(appearance.dark ? 0xFFFFFF : 0x030B20, .72F);
    s.border = Color(appearance.dark ? 0x8A7F7F : 0x74849A);
    s.accent = Color(appearance.dark ? 0xFFFFFF : 0x082952); // Primary action role.
    s.onAccent = Color(appearance.dark ? 0x171717 : 0xFFFEFA);
    theme.typography = {12 * scale, 14 * scale, 14 * scale, 20 * scale, 20 * scale, 13 * scale};
    theme.layout.controlHeight = 32 * scale;
    theme.layout.windowPaddingX = theme.layout.windowPaddingY = 24 * scale;
    theme.layout.horizontalPadding = 12 * scale;
    theme.layout.spacingX = theme.layout.spacingY = 8 * scale;
    theme.radius = {4 * scale, 6 * scale, 8 * scale};
    theme.stroke = {1, 2 * scale};
    theme.motion.enabled = false;
    theme.motion.reducedMotion = true;
    // All dimensions derive once from immutable base tokens. ApplyTheme uses
    // scale 1; framebuffer density is independently supplied by the backend.
    imkit::ResolveTheme(theme);
    return theme;
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
        facts.close_requested = imkit::ActionButton("Close###bootstrap-close", imkit::ActionVariant::Primary,
                                                   {std::min(140 * scale, content.x), button_height}, {&theme});
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
