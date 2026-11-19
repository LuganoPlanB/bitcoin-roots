// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <imkit/components.h>
namespace roots::gui::ui::theme {
imkit::Theme Make(bool dark, float scale = 1);
void Apply(const imkit::Theme&);
// Retains ImKit input/IDs/results; replaces its fixed-offset focus decoration
// using public draw APIs so the scaled 2px ring has a scaled 2px canvas gap.
// Optional borrowed current-frame layer keeps the ring outside child clips.
bool ActionButton(const char*, imkit::ActionVariant, ImVec2, const imkit::Theme&, ImDrawList* focus_draw=nullptr);
float ActionWidth(const char* label);
void SameLineIfFits(float width);
void SameLineForAction(const char* label);
// ImKit owns restoration of every style/font/metric, including our projection.
class Scope {
    imkit::ThemeScope scope;
public:
    explicit Scope(const imkit::Theme&);
};
}
