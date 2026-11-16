// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/theme/gallery.h"
#include "ui/theme/tokens.h"
#include "ui/theme/theme.h"
#include <imkit/imkit.h>
#include <algorithm>
namespace roots::gui::ui {
ThemeGalleryFacts DrawThemeGallery(const imkit::Theme& theme, const BootstrapAppearance& appearance, ThemeGalleryState& state)
{
    ThemeGalleryFacts facts;
    const auto t=theme::Wallet(appearance.dark);
    const float scale=appearance.font_scale*appearance.dpi_scale;
    const auto* vp=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Theme gallery###theme-gallery",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove);
    imkit::PushTextRole(theme,imkit::TextRole::Heading); ImGui::TextUnformatted("Civic Digital Commons"); imkit::PopTextRole();
    ImGui::TextColored(t.secondary,"Wallet visual system / %s",appearance.dark?"Dark":"Light");
    // Keep the primary action outside the scrolling inspection area at 200%.
    const float footer=48*scale;
    ImGui::BeginChild("gallery-content",{0,std::max(1.F,ImGui::GetContentRegionAvail().y-footer)},ImGuiChildFlags_NavFlattened);
    ImGui::TextWrapped("Public fixture data. Incoming +1.00000000 BTC / outgoing -0.12500000 BTC use ordinary text and explicit signs.");
    ImGui::Separator();
    ImGui::TextUnformatted("Wallet label"); ImGui::SetNextItemWidth(-1);
    ImGui::InputText("###gallery-label",state.label,sizeof(state.label));
    const auto input_min=ImGui::GetItemRectMin(),input_max=ImGui::GetItemRectMax();
    facts.input={input_min.x,input_min.y,input_max.x,input_max.y};
    ImGui::TextColored(t.secondary,"Secondary text and placeholders retain readable contrast.");
    for (const auto& row : {std::pair{"Success: label saved",t.success},std::pair{"Warning: observations are stale",t.warning},
                           std::pair{"Error: request failed; retry available",t.error},std::pair{"Info: offline public data",t.info}}) {
        const auto p=ImGui::GetCursorScreenPos(); const auto size=ImVec2(ImGui::GetContentRegionAvail().x,ImGui::GetTextLineHeight()+12*scale);
        ImGui::GetWindowDrawList()->AddRectFilled(p,{p.x+size.x,p.y+size.y},ImGui::GetColorU32(row.second.background),4*scale);
        ImGui::SetCursorScreenPos({p.x+8*scale,p.y+6*scale});
        ImGui::TextColored(row.second.foreground,"%s",row.first);
        ImGui::SetCursorScreenPos({p.x,p.y+size.y+4*scale});
    }
    if (ImGui::BeginTable("public-observations",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_Borders|ImGuiTableFlags_ScrollX,{0,110*scale})) {
        ImGui::TableSetupColumn("State"); ImGui::TableSetupColumn("Amount (BTC)"); ImGui::TableHeadersRow();
        for (int i=0;i<2;++i) {
            ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::PushID(i);
            if (ImGui::Selectable(i?"Frozen / unavailable":"Selected / confirmed",state.selected==i,ImGuiSelectableFlags_SpanAllColumns)) state.selected=i;
            ImGui::TableNextColumn(); ImGui::TextUnformatted(i?"+0.25000000":"+1.00000000");ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (appearance.monospace) ImGui::PushFont(appearance.monospace,13*scale);
    ImGui::TextColored(t.script_structure,"wsh(sortedmulti(2,...))  /  public descriptor");
    if (appearance.monospace) ImGui::PopFont();
    ImGui::TextColored(t.secondary,"Loading observations... / no node connection");
    ImGui::EndChild();
    if (state.initial_focus) ImGui::SetKeyboardFocusHere();
    facts.requested=theme::ActionButton("Continue###gallery-primary",imkit::ActionVariant::Primary,{140*scale,0},theme);
    facts.visible=ImGui::IsItemVisible();facts.focused=ImGui::IsItemFocused();
    facts.hovered=ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride);facts.active=ImGui::IsItemActive();
    auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();facts.primary={a.x,a.y,b.x,b.y};state.initial_focus=false;
    ImGui::SameLine();
    ImGui::BeginDisabled();theme::ActionButton("Unavailable###gallery-disabled",imkit::ActionVariant::Secondary,{},theme);ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Requires a node connection");
    ImGui::End(); return facts;
}
}
