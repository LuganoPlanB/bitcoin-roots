// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/theme/tokens.h"
#include <algorithm>
#include <cmath>
namespace roots::gui::ui::theme {
std::array<GradientStop, 4> IdentityGradient(bool dark)
{
    return {{{0, Rgb(dark ? 0xF7931A : 0xFFB604)}, {dark ? .34F : .28F, Rgb(0xFF5DB1)},
             {dark ? .68F : .58F, Rgb(0x7468FF)}, {1, Rgb(0x23F5FF)}}};
}
WalletTokens Wallet(bool d)
{
    return {
        Rgb(d ? 0x171717 : 0xFFFEFA), Rgb(d ? 0x1A1717 : 0xF3F9FF),
        Rgb(d ? 0x211D1D : 0xFCFCFC), Rgb(d ? 0x292424 : 0xF3F8FD),
        Rgb(d ? 0xFFFFFF : 0x030B20), Rgb(d ? 0xFFFFFF : 0x030B20, .72F),
        Rgb(d ? 0xE15364 : 0x4F97E9), d ? Rgb(0xFFFFFF, .12F) : Rgb(0xB0D3F1),
        Rgb(d ? 0xFFFFFF : 0x082952), Rgb(d ? 0x171717 : 0xFFFEFA),
        Rgb(d ? 0xF2EEEE : 0x103B6A), Rgb(d ? 0xE0DADA : 0x061F3F),
        Rgb(d ? 0x8A7F7F : 0x74849A), Rgb(0x7468FF), Rgb(d ? 0xB0D3F1 : 0x082952),
        {Rgb(d ? 0x75D6A3 : 0x1B6E45), Rgb(d ? 0x17362A : 0xE8F5ED)},
        {Rgb(d ? 0xF5CD70 : 0x855C00), Rgb(d ? 0x3A2F16 : 0xFFF4DA)},
        {Rgb(d ? 0xFFB4BC : 0xB4233A), Rgb(d ? 0x3A2028 : 0xFCECEE)},
        {Rgb(d ? 0xB0D3F1 : 0x082952), Rgb(d ? 0x1D2C3C : 0xF3F9FF)},
        Rgb(d ? 0xA99EA2 : 0x66768B), Rgb(d ? 0xB0D3F1 : 0x315F9C),
        Rgb(d ? 0xBEA9FF : 0x5A46B8), Rgb(d ? 0x75D6A3 : 0x1B6E45),
        Rgb(d ? 0xF7931A : 0xFFB604)};
}
ImVec4 Composite(ImVec4 f, ImVec4 b)
{
    const float fa = std::clamp(f.w, 0.F, 1.F), ba = std::clamp(b.w, 0.F, 1.F);
    const float a = fa + ba * (1 - fa);
    if (a == 0) return {};
    return {(f.x * fa + b.x * ba * (1 - fa)) / a, (f.y * fa + b.y * ba * (1 - fa)) / a,
            (f.z * fa + b.z * ba * (1 - fa)) / a, a};
}
float Contrast(ImVec4 f, ImVec4 b, ImVec4 backdrop)
{
    b = Composite(b, backdrop); f = Composite(f, b);
    const auto luminance = [](ImVec4 c) {
        const auto linear = [](float v) { return v <= .04045F ? v / 12.92F : std::pow((v + .055F) / 1.055F, 2.4F); };
        return .2126F * linear(c.x) + .7152F * linear(c.y) + .0722F * linear(c.z);
    };
    const float a = luminance(f), z = luminance(b);
    return (std::max(a, z) + .05F) / (std::min(a, z) + .05F);
}
}
