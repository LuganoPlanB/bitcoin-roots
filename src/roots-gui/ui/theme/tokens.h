// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <imgui.h>
#include <array>
#include <string_view>
namespace roots::gui::ui::theme {
constexpr ImVec4 Rgb(unsigned rgb, float alpha = 1)
{
    return {float((rgb >> 16) & 255) / 255, float((rgb >> 8) & 255) / 255, float(rgb & 255) / 255, alpha};
}
// Published identity primitives are immutable. Wallet meaning belongs below.
struct BrandColor { std::string_view name; ImVec4 value; };
inline constexpr std::array BrandColors{
    BrandColor{"Commons Gold", Rgb(0xFFB604)}, BrandColor{"Bitcoin Orange", Rgb(0xF7931A)},
    BrandColor{"Roots artwork orange", Rgb(0xF57C00)}, BrandColor{"Network Cyan", Rgb(0x23F5FF)},
    BrandColor{"Violet", Rgb(0x7468FF)}, BrandColor{"Signal Pink", Rgb(0xFF5DB1)}};
inline constexpr std::string_view TokensSha256 = "2ade02257affd47398579383bdcb816f370c2848b18b4d135777d7b96dd328ef";
inline constexpr std::string_view BrandSha256 = "2053ae7a0e8105ee796c2546eebbef5743e77e221020877fd00b21125adc044a";
struct GradientStop { float position; ImVec4 color; };
std::array<GradientStop, 4> IdentityGradient(bool dark);
struct StatusColors { ImVec4 foreground, background; };
struct WalletTokens {
    ImVec4 canvas, alternate, panel, strong_panel, text, secondary;
    ImVec4 interaction, soft_accent, primary, on_primary, primary_hover, primary_pressed;
    ImVec4 boundary, focus, link;
    StatusColors success, warning, error, info;
    ImVec4 utxo, script_structure, script_key, script_signature, identity;
};
WalletTokens Wallet(bool dark);
// Source-over alpha in sRGB, then WCAG relative luminance of the visible colors.
ImVec4 Composite(ImVec4 foreground, ImVec4 background);
float Contrast(ImVec4 foreground, ImVec4 background, ImVec4 backdrop);
}
