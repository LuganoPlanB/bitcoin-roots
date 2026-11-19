// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include "ui/theme/tokens.h"
#include <algorithm>
#include <cmath>
namespace roots::gui::ui::shell {
namespace {
namespace a=app;namespace c=components;
c::ItemFacts Last(){const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();return {{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),false,false,false,ImGui::IsItemVisible()};}
bool StatusAction(const char* id,std::string_view text,float width,const imkit::Theme& theme,float scale,std::array<float,4>& visual)
{
    ImGui::PushStyleColor(ImGuiCol_NavCursor,ImVec4{});
    const bool pressed=ImGui::InvisibleButton(id,{width,32*scale},ImGuiButtonFlags_EnableNav);
    ImGui::PopStyleColor();
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    // Keep the32px hit target while the compact visible frame and its full
    // focus cue fit inside the fixed36px status strip.
    const ImVec2 a{lo.x+6*scale,lo.y+6*scale},b{hi.x-6*scale,hi.y-6*scale};visual={a.x,a.y,b.x,b.y};
    const auto t=theme::Wallet(theme.scheme==imkit::ColorScheme::Dark);auto* draw=ImGui::GetWindowDrawList();
    const auto bg=ImGui::IsItemActive()?t.strong_panel:ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride)?t.alternate:t.panel;
    draw->AddRectFilled(a,b,ImGui::GetColorU32(bg),theme.radius.control);
    draw->AddRect(a,b,ImGui::GetColorU32(t.boundary),theme.radius.control,1.F,ImDrawFlags_None);
    const std::string label(text);const auto size=ImGui::CalcTextSize(label.c_str(),nullptr,false);const ImVec4 clip{a.x,a.y,b.x,b.y};
    draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{(a.x+b.x-size.x)/2,(a.y+b.y-size.y)/2},ImGui::GetColorU32(t.text),label.data(),label.data()+label.size(),0,&clip);
    if(ImGui::IsItemFocused() && ImGui::IsAnyItemFocused()) {
        const float stroke=theme.stroke.focus,offset=std::ceil(2*scale+stroke/2+2);
        draw->AddRect({a.x-offset,a.y-offset},{b.x+offset,b.y+offset},ImGui::GetColorU32(t.focus),theme.radius.control+offset,stroke,ImDrawFlags_None);
    }
    return pressed;
}
std::string Detail(const State& s,std::string_view text){return s.privacy.hidden && !text.empty()?"Details hidden":std::string(text);}
ImVec4 Color(a::ServiceState state,const imkit::Theme& theme) {
    const auto tokens=theme::Wallet(theme.scheme==imkit::ColorScheme::Dark);
    return state==a::ServiceState::Error?tokens.error.foreground:state==a::ServiceState::Stale?tokens.warning.foreground:state==a::ServiceState::Ready?tokens.success.foreground:tokens.text;
}
void Signal(State& s,const char* id,const char* label,const a::ServiceObservation& observation,const imkit::Theme& theme) {
    const std::string text=std::string(label)+": "+a::StateName(observation.state);
    const std::string detail=Detail(s,observation.detail.empty() && observation.state==a::ServiceState::Unavailable?"Observation adapter is unavailable.":observation.detail);
    ImGui::PushID(id);ImGui::BeginGroup();ImGui::PushStyleColor(ImGuiCol_Text,Color(observation.state,theme));ImGui::TextUnformatted(text.c_str());ImGui::PopStyleColor();
    if(!detail.empty())ImGui::TextWrapped("%s",detail.c_str());ImGui::EndGroup();
    s.presentation.Annotate(id,label,text+" / "+detail,observation.state==a::ServiceState::Error?imkit::accessibility::SemanticRole::Alert:imkit::accessibility::SemanticRole::Status,false,true,observation.state==a::ServiceState::Loading);ImGui::PopID();
}
void DrawActivityExport(State& s,Facts& f,const imkit::Theme& theme,a::Requests& host)
{
    const auto scope=CurrentToolScope(s,host);
    const bool enabled=!s.activity_export.chooser && !s.activity_log.Records().empty() && !scope.locked
        && c::native_input && c::native_input->FileChooserAvailable();
    ImGui::BeginDisabled(!enabled);
    const bool pressed=theme::ActionButton("Export activity CSV",imkit::ActionVariant::Secondary,{},theme);
    f.tools["activity-export"]=Last();f.tools["activity-export"].disabled=!enabled;
    const auto external=s.presentation.Interact("Export activity CSV",c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus,!enabled);
    ImGui::EndDisabled();
    const auto export_activity=[&s,&host,scope]{return s.drawer==Drawer::Activity && scope==CurrentToolScope(s,host)
        && BeginToolExport(s.activity_export,scope,a::ActivityCsv(s.activity_log));};
    const bool accepted=pressed && enabled && export_activity();
    if(external)s.presentation.Defer(*external,[pressed,accepted,export_activity](const auto&){return pressed?accepted:export_activity();});
    PollToolExport(s.activity_export,scope,[&s]{return a::ActivityCsv(s.activity_log);},s.activity_notice);
}
}
void DrawStatus(State& s,Facts& f,const imkit::Theme& theme,float scale)
{
    const auto& r=f.layout.status;ImGui::SetCursorScreenPos({r.x,r.y});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12*scale,2*scale});
    ImGui::PushStyleColor(ImGuiCol_ChildBg,theme.semantic.canvas);
    ImGui::BeginChild("status",{r.w,r.h},ImGuiChildFlags_AlwaysUseWindowPadding|ImGuiChildFlags_NavFlattened,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
    std::string activity_label="Activity";
    if(s.observations.tasks.size()==1 && s.observations.tasks[0].progress_per_mille && *s.observations.tasks[0].progress_per_mille<=1000)activity_label+=' '+std::to_string(*s.observations.tasks[0].progress_per_mille/10)+"%";
    else if(!s.observations.tasks.empty())activity_label+=" ("+std::to_string(s.observations.tasks.size())+")";
    std::array<float,4> activity_visual;
    const bool activity=StatusAction("###activity",activity_label,128*scale,theme,scale,activity_visual);f.activity_button=Last();f.activity_hovered=ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride);f.activity_held=ImGui::IsItemActive() && ImGui::GetIO().MouseDown[0];
    const auto activity_ax=s.presentation.Interact("Background activity",c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus,false,false,a::StateName(s.observations.activity.state));
    if(activity){s.drawer=Drawer::Activity;s.drawer_focus=true;}if(activity_ax)s.presentation.Defer(*activity_ax,[&s,activity](const c::SemanticRequest&){if(!activity){s.drawer=Drawer::Activity;s.drawer_focus=true;}return true;});
    ImGui::SameLine();
    const float right=r.w-170*scale;
    if(ImGui::GetCursorPosX()+250*scale<right) {ImGui::Text("Connection: %s",a::StateName(s.observations.connection.state));ImGui::SameLine();}
    ImGui::SetCursorPosX(right);
    if(s.restore_status){ImGui::SetKeyboardFocusHere();s.restore_status=false;}
    const auto label=std::string("Status: ")+a::StateName(s.observations.connection.state);
    const bool status=StatusAction("###status-details",label,158*scale,theme,scale,f.status_visual);f.status_button=Last();
    const auto& box=f.status_visual;ImGui::GetWindowDrawList()->AddRectFilled({box[0]+3*scale,box[1]+4*scale},{box[0]+5*scale,box[3]-4*scale},ImGui::GetColorU32(Color(s.observations.connection.state,theme)));
    const auto status_ax=s.presentation.Interact("Connection status",c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus,false,false,a::StateName(s.observations.connection.state));
    if(status){s.drawer=Drawer::Services;s.drawer_focus=true;}if(status_ax)s.presentation.Defer(*status_ax,[&s,status](const c::SemanticRequest&){if(!status){s.drawer=Drawer::Services;s.drawer_focus=true;}return true;});
    ImGui::EndChild();ImGui::PopStyleColor();ImGui::PopStyleVar();
}
void DrawActivityDrawer(State& s,Facts& f,const imkit::Theme& theme,float scale,app::Requests* host)
{
    if(s.drawer!=Drawer::Activity)s.activity_export={};
    if(s.drawer==Drawer::None)return;
    const float height=std::min(340*scale,f.layout.content.h*.7F);
    ImGui::SetNextWindowPos({f.layout.content.x,f.layout.status.y-height});ImGui::SetNextWindowSize({f.layout.content.w,height});
    const bool focus=s.drawer_focus;if(focus){ImGui::SetNextWindowFocus();s.drawer_focus=false;}
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12*scale,8*scale});
    ImGui::Begin(s.drawer==Drawer::Activity?"Background activity###shell-drawer":"Service status###shell-drawer",nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoScrollbar);
    {const auto p=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();f.drawer_rect={p.x,p.y,size.x,size.y};}f.drawer_visible=true;
    if(focus)ImGui::SetKeyboardFocusHere();
    const bool close=theme::ActionButton("Close drawer",imkit::ActionVariant::Secondary,{0,32*scale},theme);f.drawer_close=Last();
    const auto close_ax=s.presentation.Interact("Close drawer",c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus);
    const bool escape=s.actions.kind==DocumentDialog::None && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape);
    if(close || escape){s.drawer=Drawer::None;s.restore_status=true;}if(close_ax)s.presentation.Defer(*close_ax,[&s,close,escape](const c::SemanticRequest&){if(!close && !escape){s.drawer=Drawer::None;s.restore_status=true;}return true;});
    if(s.drawer==Drawer::Activity && host){ImGui::SameLine();DrawActivityExport(s,f,theme,*host);}
    ImGui::BeginChild("drawer-body",{0,0},ImGuiChildFlags_NavFlattened);
    if(focus)ImGui::SetScrollY(0);
    {const auto lo=ImGui::GetWindowDrawList()->GetClipRectMin(),hi=ImGui::GetWindowDrawList()->GetClipRectMax();f.drawer_body={lo.x,lo.y,hi.x-lo.x,hi.y-lo.y};}
    if(s.drawer==Drawer::Services) {
        if(auto* connection=host?host->Connection():nullptr) {
            const bool offline=connection->Offline();const char* label=offline?"Go online":"Go offline";
            const bool pressed=theme::ActionButton(offline?"Go online###connection-offline":"Go offline###connection-offline",imkit::ActionVariant::Secondary,{},theme);f.document_actions["offline"]=Last();
            const auto apply=[&s,connection,offline,session=connection->Session(),revision=connection->Revision()]{if(connection->Session()!=session || connection->Revision()!=revision || connection->Offline()!=offline)return false;const bool ok=connection->SetOffline(!offline);s.server.scope_revision=connection->Revision();return ok;};
            const bool applied=pressed && apply();
            const auto external=s.presentation.Interact(label,components::ax::SemanticRole::Button,components::ax::SemanticAction::Press|components::ax::SemanticAction::Focus);
            if(external)s.presentation.Defer(*external,[pressed,applied,apply](const components::SemanticRequest&){return pressed?applied:apply();});
            ImGui::SameLine();
        }
        if(host){
            const bool enabled=!(s.recovery && (s.recovery->Pending() || s.recovery->Locked()));
            ImGui::BeginDisabled(!enabled);
            const bool pressed=theme::ActionButton("Node information",imkit::ActionVariant::Secondary,{},theme);f.tools["node"]=Last();
            const auto external=s.presentation.Interact("Node information",c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus,!enabled);
            ImGui::EndDisabled();
            const auto open=[&s,host]{if(s.drawer!=Drawer::Services || s.wallet_tools.dialog.open)return false;
                s.drawer=Drawer::None;OpenWalletTool(s,*host,WalletTool::Node);return true;};
            const bool opened=pressed && enabled && open();
            if(external)s.presentation.Defer(*external,[pressed,opened,open](const auto&){return pressed?opened:open();});
        }
        Signal(s,"connection","Connection",s.observations.connection,theme);
        const std::string height_text=s.observations.backend_height && *s.observations.backend_height>=0?std::to_string(*s.observations.backend_height):"Unavailable";
        ImGui::Text("Backend height: %s",height_text.c_str());s.presentation.Annotate("backend-height","Backend height",height_text,imkit::accessibility::SemanticRole::Status);
        const auto last=s.observations.last_success.empty()?"Unavailable":Detail(s,s.observations.last_success);ImGui::TextWrapped("Last success: %s",last.c_str());
        s.presentation.Annotate("last-success","Last success",last,imkit::accessibility::SemanticRole::Status);
        const auto error=s.observations.error.empty()?"No error observation":Detail(s,s.observations.error);ImGui::TextWrapped("Error: %s",error.c_str());
        s.presentation.Annotate("connection-error","Connection error",error,imkit::accessibility::SemanticRole::Status);
        const char* names[]{"Chain observations","Wallet observations","History observations"};
        for(size_t i=0;i<s.observations.screen_observations.size();++i)if(!s.observations.screen_observations[i].detail.empty())Signal(s,names[i],names[i],s.observations.screen_observations[i],theme);
        Signal(s,"network","Network",s.observations.network,theme);
        Signal(s,"devices","Devices",s.observations.devices,theme);Signal(s,"proxy","Proxy",s.observations.proxy,theme);Signal(s,"updates","Updates",s.observations.updates,theme);
    } else if(s.drawer==Drawer::Activity) {
        Signal(s,"activity-feed","Activity feed",{s.observations.activity.state,s.privacy.hidden?"Details hidden":""},theme);
        if(!s.activity_notice.empty()){
            ImGui::TextWrapped("%s",s.activity_notice.c_str());s.presentation.Annotate("activity-export-result","Activity export",s.activity_notice,c::ax::SemanticRole::Status);
        }
        ImGui::TextUnformatted("Operational log");
        if(s.activity_log.Records().empty())ImGui::TextUnformatted("No service changes observed in this session.");
        size_t line{};
        for(auto entry=s.activity_log.Records().rbegin();entry!=s.activity_log.Records().rend();++entry){
            const auto text=ObservationDate(entry->observed)+" · "+a::ActivityKindName(entry->kind)+" · "+a::StateName(entry->state)
                +(entry->progress?" · "+std::to_string(*entry->progress/10)+"%":"");
            ImGui::PushID(int(++line));ImGui::TextWrapped("%s",text.c_str());
            s.presentation.Annotate("log-line","Operational log",text);ImGui::PopID();
        }
        if(s.observations.tasks.empty())ImGui::TextUnformatted(s.observations.activity.state==a::ServiceState::Ready?"No active background tasks.":"No active task progress reported.");
        size_t count{};std::set<uint64_t> ids;
        for(const auto& task:s.observations.tasks) {
            if(count++==32){ImGui::TextUnformatted("More activity entries omitted.");break;}
            if(!task.id || !ids.insert(task.id).second){ImGui::TextUnformatted("Invalid activity identity; entry unavailable.");continue;}
            const bool progress=task.progress_per_mille && *task.progress_per_mille<=1000;
            const auto stage=std::string(a::StateName(task.state));
            imkit::TaskProgressCardView view;view.id=task.id;view.title="Background task";view.stage=stage.c_str();
            view.fraction=progress?float(*task.progress_per_mille)/1000.F:-1.F;view.supplemental=task.progress_per_mille && !progress?"Progress unavailable":"";
            view.status=task.state==a::ServiceState::Error?imkit::ProgressStatus::Failed:task.state==a::ServiceState::Ready?imkit::ProgressStatus::Succeeded:task.state==a::ServiceState::Stale || task.state==a::ServiceState::Locked?imkit::ProgressStatus::Paused:imkit::ProgressStatus::Running;
            imkit::ComponentOptions options;options.theme=&theme;options.animation=nullptr;
            ImGui::PushID(std::to_string(task.id).c_str());imkit::TaskProgressCard("task",view,{true},options);
            s.presentation.Progress("task","Background activity",stage,progress?task.progress_per_mille:std::nullopt,task.state==a::ServiceState::Loading);ImGui::PopID();
        }
    }
    if(s.drawer!=Drawer::Activity)s.activity_export={};
    f.drawer_scroll=ImGui::GetScrollY();f.drawer_scroll_max=ImGui::GetScrollMaxY();ImGui::EndChild();ImGui::End();ImGui::PopStyleVar();
}
}
