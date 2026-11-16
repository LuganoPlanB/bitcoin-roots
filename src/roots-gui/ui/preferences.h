// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "application/preferences.h"
#include <array>
namespace imkit { struct Theme; }
namespace roots::gui::ui {
struct PreferencePanel { PublicPreferences saved, draft; bool save_rejected{}; };
struct PreferenceFacts {
    std::array<float,4> save{}, cancel{};
    bool save_visible{}, cancel_visible{};
    float font_size{};
};
PreferenceFacts DrawPreferences(PreferencePanel&, PreferenceRequests&, const imkit::Theme&);
}
