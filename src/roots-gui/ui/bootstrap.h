// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <string>
namespace imkit { struct Theme; }
struct ImFont;
namespace roots::gui::ui {
struct AppearanceStatus {
    int choice{-1}; // Light=0, Dark=1, System=2; -1 means no preference path.
    bool invalid_saved{},save_failed{},system_unavailable{};
};
struct BootstrapAppearance { bool dark{false}; float font_scale{1}, dpi_scale{1}; ImFont* monospace{}; AppearanceStatus status; bool reduced_motion{}; };
struct BootstrapFacts {
    float close_x0{}, close_y0{}, close_x1{}, close_y1{};
    float font_size{}, frame_padding_y{};
    bool close_visible{}, close_focused{}, close_requested{};
    int appearance_requested{-1};
};
struct BootstrapState {
    bool initial_focus{true};
    bool show_appearance{}, appearance_error{}, system_unknown{};
    int appearance_choice{};
};
imkit::Theme MakeBootstrapTheme(const BootstrapAppearance&);
BootstrapFacts DrawBootstrap(const imkit::Theme&, const BootstrapAppearance&, BootstrapState&);
std::string BuildInfo();
}
