// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/theme/theme.h"
#include "ui/theme/tokens.h"
#include <imkit/skin.h>
#include <cmath>
#include <stdexcept>
namespace roots::gui::ui::theme {
namespace {
bool Frame(const imkit::SkinFrame& f, void*)
{
    if (f.part != imkit::SkinPart::Button || !f.theme ||
        (f.variant != imkit::ActionVariant::Primary && f.variant != imkit::ActionVariant::Secondary)) return false;
    const auto t = Wallet(f.theme->scheme == imkit::ColorScheme::Dark);
    const bool primary = f.variant == imkit::ActionVariant::Primary;
    const bool pointer_hovered=ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride);
    auto bg = primary ? (f.active ? t.primary_pressed : pointer_hovered ? t.primary_hover : t.primary) :
        (f.active ? t.strong_panel : pointer_hovered ? t.alternate : t.panel);
    if (f.disabled && !primary) bg = t.strong_panel;
    f.draw->AddRectFilled(f.min, f.max, ImGui::GetColorU32(bg), f.rounding);
    if (!primary || f.disabled) f.draw->AddRect(f.min, f.max, ImGui::GetColorU32(t.boundary), f.rounding, 1.F, ImDrawFlags_None);
    return true;
}
const imkit::Skin skin{Frame, nullptr};
void Project(const imkit::Theme& theme)
{
    const auto t = Wallet(theme.scheme == imkit::ColorScheme::Dark);
    auto& s = ImGui::GetStyle(); auto* c = s.Colors;
    // ImKit standard hairlines and mixed action states do not meet this brief.
    c[ImGuiCol_Border] = t.boundary;
    c[ImGuiCol_InputTextCursor] = c[ImGuiCol_NavCursor] = t.focus;
    c[ImGuiCol_TextLink] = t.link;
    c[ImGuiCol_TextDisabled] = t.secondary;
    c[ImGuiCol_TableHeaderBg] = t.strong_panel;
    c[ImGuiCol_TableRowBgAlt] = t.alternate;
    c[ImGuiCol_TableBorderStrong] = t.boundary;
    s.DisabledAlpha = 1; // Disabled/frozen content remains readable; state has text/shape too.
    s.FrameBorderSize = 1;
}
}
imkit::Theme Make(bool dark, float scale)
{
    if (!std::isfinite(scale) || scale <= 0 || scale > 8) throw std::invalid_argument("invalid theme scale");
    auto theme = imkit::MakePrecisionTheme(dark ? imkit::ColorScheme::Dark : imkit::ColorScheme::Light);
    const auto t = Wallet(dark); auto& s = theme.semantic;
    s.canvas=t.canvas; s.surface=t.panel; s.surfaceRaised=t.strong_panel; s.overlay=t.panel;
    s.text=t.text; s.textSecondary=t.secondary; s.textDisabled=t.secondary; s.border=t.boundary;
    s.accent=t.primary; s.onAccent=t.on_primary;
    s.success=t.success.foreground; s.warning=t.warning.foreground; s.error=t.error.foreground;
    s.control={t.panel,t.alternate,t.strong_panel,t.focus,t.strong_panel,t.strong_panel};
    theme.typography={12*scale,14*scale,14*scale,20*scale,20*scale,13*scale};
    theme.layout.controlHeight=32*scale;
    theme.layout.windowPaddingX=theme.layout.windowPaddingY=24*scale;
    theme.layout.horizontalPadding=12*scale; theme.layout.spacingX=theme.layout.spacingY=8*scale;
    theme.layout.cellPaddingY=6*scale;
    theme.radius={4*scale,6*scale,8*scale}; theme.stroke={1,2*scale};
    theme.motion.enabled=false; theme.motion.reducedMotion=true; theme.opacity.disabled=1;
    theme.skin=&skin;
    imkit::ResolveTheme(theme);
    return theme;
}
void Apply(const imkit::Theme& theme) { imkit::ApplyTheme(theme, 1); Project(theme); }
Scope::Scope(const imkit::Theme& theme) : scope(theme, 1) { Project(theme); }
float ActionWidth(const char* label)
{
    return ImGui::CalcTextSize(label, nullptr, true).x + 2*ImGui::GetStyle().FramePadding.x;
}

void SameLineIfFits(float width)
{
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= right)
        ImGui::SameLine();
}

void SameLineForAction(const char* label) { SameLineIfFits(ActionWidth(label)); }

bool ActionButton(const char* label, imkit::ActionVariant variant, ImVec2 size, const imkit::Theme& theme, ImDrawList* focus_draw)
{
    // The pinned component has no focus-offset option. A transparent local
    // cursor color suppresses only its paint, leaving navigation untouched.
    // NavVisible is a cached output from the start of the frame. Preserve
    // current public cursor state so a keyboard focus handoff earlier in this
    // frame is not undone by unrelated buttons submitted later.
    bool navigation_visible=ImGui::IsAnyItemFocused();
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4{});
    const bool result=imkit::ActionButton(label,variant,size,{&theme});
    ImGui::PopStyleColor();
    // A pointer activation supersedes the previous field's keyboard cue.
    // ImKit's unconditional focused-button reveal must not change input mode.
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride) && ImGui::IsMouseClicked(ImGuiMouseButton_Left))navigation_visible=false;
    // This revision also forces the cursor visible for mouse-focused buttons.
    // Preserve the host's keyboard/gamepad visibility instead of changing mode.
    ImGui::SetNavCursorVisible(navigation_visible);
    if ((ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) && ImGui::CalcTextSize(label,nullptr,true).x > 0) {
        const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
        const float scale = theme.typography.body/14.F;
        // An interrupted lower edge distinguishes an unavailable action
        // even when neighboring panel and control colors are close.
        const auto color = ImGui::GetColorU32(theme.semantic.textDisabled);
        auto* draw = ImGui::GetWindowDrawList();
        for (float x = lo.x+6*scale; x+3*scale < hi.x-6*scale; x += 7*scale) {
            draw->AddLine({x,hi.y-2*scale},{x+3*scale,hi.y-2*scale},color);
        }
    }
    if (ImGui::IsItemFocused() && navigation_visible) {
        const float scale=theme.typography.body/14.F;
        const float width=theme.stroke.focus;
        // One framebuffer-pixel guard accommodates antialias coverage; the
        // minimum clear canvas gap remains two design pixels at every scale.
        const float offset=std::ceil(2*scale+width/2+1);
        const auto a=ImGui::GetItemRectMin(), b=ImGui::GetItemRectMax();
        (focus_draw?focus_draw:ImGui::GetWindowDrawList())->AddRect({a.x-offset,a.y-offset},{b.x+offset,b.y+offset},
            ImGui::GetColorU32(theme.semantic.control.focused),ImGui::GetStyle().FrameRounding+offset,width,ImDrawFlags_None);
    }
    return result;
}
}
