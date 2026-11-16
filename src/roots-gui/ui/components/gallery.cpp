// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/components/gallery.h"
#include "ui/theme/tokens.h"
#include <algorithm>
namespace roots::gui::ui::components {
namespace {
bool PrimaryAvailable(const Gallery& s) {
    return s.enabled && ParseAmount(s.amount.draft()).has_value() && FeeRate::Parse(s.fee.draft()).has_value() && s.amount.error.empty() && s.fee.error.empty() && s.label.error.empty();
}
bool RowAvailable(const Gallery& s,uint64_t key) {
    const auto it=std::find_if(s.rows.begin(),s.rows.end(),[&](const Row& row){return row.id==key;});if(it==s.rows.end() || !it->enabled)return false;
    const auto& label=key==202?s.label.saved:it->label;return label.find(s.filter.data())!=std::string::npos;
}
std::string SemanticScope(const Gallery& s) {
    size_t size=s.rows.size()+s.label.saved.size()+s.status.detail.size()+s.status.observed.size();for(const auto& row:s.rows)size+=row.label.size()+row.disabled_reason.size();
    return std::to_string(s.semantic_hide_toggle)+":"+std::to_string(s.privacy.hidden)+":"+std::to_string(s.enabled)+":"+std::to_string(s.dialog.state.open)+":"+std::to_string(s.show_summary)+":"+std::to_string(s.review_fixture)+":"+std::to_string(s.rows.size())+":"+std::to_string(size);
}
}
GalleryFacts DrawGallery(Gallery& s,Commands& host,const imkit::Theme& theme,const BootstrapAppearance& appearance)
{
    if(s.semantic_hide_toggle){s.privacy.hidden=!s.privacy.hidden;s.semantic_hide_toggle=false;}
    GalleryFacts f;const float scale=appearance.font_scale*appearance.dpi_scale;
    if(native_input)native_input->Privacy(s.privacy.hidden);s.presentation.Begin(SemanticScope(s),[&s]{return SemanticScope(s);});s.presentation.Suppress(s.dialog.state.open);s.polish.Begin();
    const auto* vp=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("Shared controls###component-gallery",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove);
    imkit::PushTextRole(theme,imkit::TextRole::Heading);ImGui::TextUnformatted("Wallet controls");imkit::PopTextRole();
    if (s.polish.ClaimIdentityAccent()) {
        ImGui::SameLine();const auto at=ImGui::GetCursorScreenPos();ImGui::Dummy({24*scale,ImGui::GetTextLineHeight()});
        const auto color=ui::theme::Wallet(theme.scheme==imkit::ColorScheme::Dark).identity;
        ImGui::GetWindowDrawList()->AddRectFilled({at.x,at.y+8*scale},{at.x+24*scale,at.y+10*scale},ImGui::GetColorU32(color));
    }
    ImGui::Checkbox("Hide amounts###hide-amounts",&s.privacy.hidden);
    const auto privacy_action=s.presentation.Interact("Hide amounts",ax::SemanticRole::Toggle,ax::SemanticAction::Toggle|ax::SemanticAction::Focus,false,s.privacy.hidden);if(privacy_action)s.presentation.Defer(*privacy_action,[&s](const SemanticRequest&){s.semantic_hide_toggle=!s.semantic_hide_toggle;return true;});
    auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();f.privacy_toggle={a.x,a.y,b.x,b.y};ImGui::SameLine();
    ImGui::Checkbox("Reduced motion###reduced-motion",&s.reduced_motion);
    const auto motion_action=s.presentation.Interact("Reduced motion",ax::SemanticRole::Toggle,ax::SemanticAction::Toggle|ax::SemanticAction::Focus,false,s.reduced_motion);if(motion_action)s.presentation.Defer(*motion_action,[&s](const SemanticRequest&){s.reduced_motion=!s.reduced_motion;return true;});
    a=ImGui::GetItemRectMin();b=ImGui::GetItemRectMax();f.motion_toggle={a.x,a.y,b.x,b.y};
    // Only this neutral, fixed-space state indicator transitions. Values and
    // privacy redaction change immediately; no financial data is interpolated.
    s.privacy_feedback=s.polish.PrivacyFeedback(ImGui::GetID("privacy-feedback"),s.privacy.hidden,s.reduced_motion || appearance.reduced_motion,ImGui::GetIO().DeltaTime,ImGui::GetFrameCount());
    const auto checkbox_end=ImGui::GetItemRectMax();const ImVec2 feedback_at{ImGui::GetWindowPos().x+24*scale,checkbox_end.y+scale};
    ImGui::GetWindowDrawList()->AddRectFilled(feedback_at,{feedback_at.x+(8+12*s.privacy_feedback)*scale,feedback_at.y+2*scale},ImGui::GetColorU32(theme.semantic.textSecondary));
    ImGui::BeginChild("controls",{0,std::max(1.F,ImGui::GetContentRegionAvail().y-56*scale)},ImGuiChildFlags_NavFlattened);
    StatusBanner("freshness",s.status,theme,&s.presentation);
    if (!s.enabled) ImGui::TextWrapped("Host busy: editing and primary action are disabled. Public values remain readable.");
    f.copy=CopyableIdentifier("public-id","bcrt1qpublic-fixture-identifier-000000000000000000000000000000000001",host,theme,s.enabled,appearance.monospace,&s.presentation,[&s]{return s.enabled;});
    f.amount=AmountField("amount",s.amount,host,theme,s.enabled,s.privacy,&s.presentation,[&s]{return s.enabled && !s.privacy.hidden && !s.semantic_hide_toggle;});
    f.fee=FeeField("fee",s.fee,host,theme,s.enabled,s.privacy,&s.presentation,[&s]{return s.enabled && !s.privacy.hidden && !s.semantic_hide_toggle;});
    f.label=LabelEditor("label",s.label,host,theme,s.enabled,&s.presentation,[&s]{return s.enabled;});
    if (s.show_summary) {
        AmountSummary("summary",s.rows,theme,s.privacy,&s.presentation);
        if (!s.rows.empty()) f.amount_copy=CopyableAmount("amount-copy",s.rows[0].amount,host,theme,s.privacy,&s.presentation,s.enabled,[&s,key=s.rows[0].id,value=s.rows[0].amount]{return s.enabled && !s.privacy.hidden && !s.semantic_hide_toggle && !s.rows.empty() && s.rows[0].id==key && s.rows[0].amount==value;});
    }
    ImGui::Separator();ImGui::TextUnformatted("Public observations / selection is independent of keyboard focus");
    ImGui::SetNextItemWidth(-1);TextInput("###filter","Filter labels",s.filter.data(),s.filter.size(),s.external_filter,0,&s.filter_cursor);
    const auto filter_action=s.presentation.Interact("Filter public labels",ax::SemanticRole::TextField,ax::SemanticAction::SetValue|ax::SemanticAction::Focus,false,false,s.filter.data());if(filter_action)s.presentation.Defer(*filter_action,[&s](const SemanticRequest& r){return SetExternalBuffer(s.filter,s.external_filter,r.value);});
    s.presentation.BindTextPosition(ImGui::GetItemID(),s.filter.data(),s.filter_cursor);
    std::vector<Row> visible;
    for (auto row:s.rows) {
        if (row.id==202) row.label=s.label.saved;
        if (row.label.find(s.filter.data())!=std::string::npos) visible.push_back(std::move(row));
    }
    f.rows=SelectionTable("observations",visible,s.selection,host,theme,120*scale,s.privacy,&s.presentation,[&s](uint64_t key){return RowAvailable(s,key);});
    if (visible.empty()) ImGui::TextWrapped("No public observations match this filter.");
    if (s.review_fixture) {
        ImGui::BeginGroup();
        ImGui::TextWrapped("Public review fixture only; no approval action is provided.");
        ReviewDetails("review",Money::Satoshis(1000000),"bcrt1qfull-destination###literal-%s-000000000000000000000001",s.privacy,&s.presentation);
        ImGui::EndGroup();const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
        const auto lo=ImGui::GetWindowDrawList()->GetClipRectMin(),hi=ImGui::GetWindowDrawList()->GetClipRectMax();
        f.review_visible=ImGui::IsItemVisible() && a.x>=lo.x && a.y>=lo.y && b.x<=hi.x && b.y<=hi.y;
    }
    if (s.reveal_rows) { ImGui::SetScrollHereY(1);s.reveal_rows=false; }
    ImGui::EndChild();
    f.primary=ActionButton("primary","Apply fixture",ActionKind::Primary,host,theme,PrimaryAvailable(s),"Correct invalid fields or wait for the host before applying.",&s.presentation,[&s]{return PrimaryAvailable(s);});
    ImGui::SameLine();f.secondary=DialogLauncher("details","Review label",s.dialog,host,theme,&s.presentation);
    f.dialog=DialogShell("Review public label###component-dialog","This fixture reviews a public label only. It does not sign, approve or broadcast a transaction.",s.dialog,host,theme,s.label.error.empty(),&s.presentation,[&s]{return s.label.error.empty();});
    ImGui::End();s.presentation.Seal();if(native_input)native_input->Privacy(s.privacy.hidden || s.semantic_hide_toggle);return f;
}
}
