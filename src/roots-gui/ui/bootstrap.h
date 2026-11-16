// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <string>
namespace imkit { struct Theme; }
namespace roots::gui::ui {
struct BootstrapAppearance { bool dark{false}; float font_scale{1}, dpi_scale{1}; };
struct BootstrapFacts {
    float close_x0{}, close_y0{}, close_x1{}, close_y1{};
    float font_size{}, frame_padding_y{};
    bool close_visible{}, close_focused{}, close_requested{};
};
struct BootstrapState { bool initial_focus{true}; };
imkit::Theme MakeBootstrapTheme(const BootstrapAppearance&);
BootstrapFacts DrawBootstrap(const imkit::Theme&, const BootstrapAppearance&, BootstrapState&);
std::string BuildInfo();
}
