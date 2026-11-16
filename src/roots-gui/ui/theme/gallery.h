// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "ui/bootstrap.h"
#include <array>
namespace roots::gui::ui {
struct ThemeGalleryState { char label[128]{"Public wallet label"}; bool initial_focus{true}; int selected{0}; };
struct ThemeGalleryFacts {
    std::array<float,4> primary{};
    std::array<float,4> input{};
    bool visible{}, focused{}, hovered{}, active{}, requested{};
};
ThemeGalleryFacts DrawThemeGallery(const imkit::Theme&, const BootstrapAppearance&, ThemeGalleryState&);
}
