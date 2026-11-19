// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "services/payment_protocols.h"
#include "application/build_info.h"
#include "ui/theme/theme.h"
#include <algorithm>

namespace roots::gui::ui::shell {
namespace {
namespace a=app;
namespace c=components;
constexpr char POPUP[]="Desktop and Help###desktop-tools";
const char* Title(DesktopPage page)
{
    switch(page){
    case DesktopPage::Preferences:return "Desktop preferences";
    case DesktopPage::Camera:return "Camera settings";
    case DesktopPage::Documentation:return "Documentation";
    case DesktopPage::Support:return "Support";
    case DesktopPage::Report:return "Report an issue";
    case DesktopPage::About:return "About Bitcoin Roots";
    case DesktopPage::Restart:return "Restart GUI";
    default:return "Desktop and Help";
    }
}
uint64_t ProfileRevision(a::Requests& host)
{
    const auto* service=host.Connection();
    return service?service->Revision():0;
}
bool Current(const State& s,a::Requests& host)
{
    return s.desktop.dialog.open && !s.desktop.scope.locked && s.desktop.scope==CurrentToolScope(s,host)
        && s.desktop.profile_revision==ProfileRevision(host);
}
void Text(State& s,const char* id,const char* name,std::string_view line,c::ax::SemanticRole role=c::ax::SemanticRole::Text)
{
    ImGui::TextWrapped("%.*s",int(line.size()),line.data());
    s.presentation.Annotate(id,name,line,role);
}
c::ItemFacts Last(bool pressed,bool disabled)
{
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    return {{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),disabled,false,pressed,ImGui::IsItemVisible()};
}
void Button(State& s,Facts& facts,const imkit::Theme& theme,const char* id,const char* label,bool enabled,std::function<bool()> action)
{
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    const bool pressed=theme::ActionButton(label,imkit::ActionVariant::Secondary,{},theme);
    facts.desktop[id]=Last(pressed,!enabled);
    const auto request=s.presentation.Interact(label,c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus,!enabled);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted=pressed && enabled && action();
    if(request)s.presentation.Defer(*request,[action=std::move(action),pressed,accepted](const auto&){return pressed?accepted:action();});
}
void Toggle(State& s,a::Requests& host,Facts& facts,const char* id,const char* label,bool PublicPreferences::*member,bool supported)
{
    const bool enabled=s.general.saved && s.general.owner && !s.general.owner->Locked() && supported;
    const bool old=s.general.saved && s.general.saved->preferences.*member;
    bool checked=old;
    ImGui::PushID(id);ImGui::BeginDisabled(!enabled);
    const bool pressed=ImGui::Checkbox(label,&checked);
    facts.desktop[id]=Last(pressed,!enabled);
    const auto request=s.presentation.Interact(label,c::ax::SemanticRole::Toggle,c::ax::SemanticAction::Toggle|c::ax::SemanticAction::Focus,!enabled,old);
    ImGui::EndDisabled();ImGui::PopID();
    const auto apply=[&s,&host,member,old]{return Current(s,host) && s.general.saved
        && s.general.saved->preferences.*member==old && SetDesktopPreference(s,member,!old);};
    const bool acknowledged=pressed && enabled && apply();
    if(request)s.presentation.Defer(*request,[apply,pressed,acknowledged](const auto&){return pressed?acknowledged:apply();});
}
bool ExternalAllowed(const State& s,a::Requests& host)
{
    if(!Current(s,host) || s.desktop.scope.hidden || s.desktop.scope.offline || s.privacy.hidden || s.semantic_hide_toggle)return false;
    const auto* service=host.Connection();
    return !service || (service->Active()?service->Active()->candidate.connection.proxy==ProxyKind::Direct:
        service->Draft().connection.proxy==ProxyKind::Direct);
}
bool PickLink(State& s,a::Requests& host,a::HelpTarget target)
{
    auto* port=host.Desktop();
    if(!ExternalAllowed(s,host) || !port || !port->Available(a::DesktopCapability::Browser).accepted)return false;
    const auto url=a::HelpUrl(target);
    if(url.empty())return false;
    s.desktop.target=target;s.desktop.confirmed_url=url;s.desktop.notice.clear();++s.desktop.revision;
    return true;
}
void Link(State& s,a::Requests& host,Facts& facts,const imkit::Theme& theme,const char* id,const char* label,a::HelpTarget target)
{
    auto* port=host.Desktop();
    Button(s,facts,theme,id,label,ExternalAllowed(s,host) && port && port->Available(a::DesktopCapability::Browser).accepted,
        [&s,&host,target]{return PickLink(s,host,target);});
}
void DrawLinkConfirmation(State& s,a::Requests& host,Facts& facts,const imkit::Theme& theme)
{
    const auto target=*s.desktop.target;
    const auto url=s.desktop.confirmed_url;
    Text(s,"external-link","External address",url);
    Text(s,"external-consent","Browser permission","The system browser receives this address and uses its own network settings.");
    Button(s,facts,theme,"open-link","Open in browser",ExternalAllowed(s,host),[&s,&host,target,url]{
        auto* port=host.Desktop();
        if(!port || !s.desktop.target || *s.desktop.target!=target || s.desktop.confirmed_url!=url || !ExternalAllowed(s,host)
            || !a::ConfirmedHelpUrl(target,url))return false;
        const auto result=port->OpenHelp(target,url);
        s.desktop.target.reset();s.desktop.confirmed_url.clear();s.desktop.notice=result.detail;++s.desktop.revision;
        return result.accepted;
    });
    ImGui::SameLine();
    Button(s,facts,theme,"cancel-link","Cancel",true,[&s]{s.desktop.target.reset();s.desktop.confirmed_url.clear();++s.desktop.revision;return true;});
}
void DrawPreferences(State& s,a::Requests& host,Facts& facts,const imkit::Theme& theme)
{
    auto& port=*host.Desktop();
    const auto minimize=port.Available(a::DesktopCapability::Minimize);
    Button(s,facts,theme,"minimize","Minimize window",minimize.accepted,[&s,&host]{
        if(!Current(s,host))return false;
        const auto result=host.Desktop()->Minimize();
        if(result.accepted)RetireDesktop(s);else s.desktop.notice=result.detail;
        return result.accepted;
    });
    if(!minimize.accepted)Text(s,"minimize-unavailable","Minimize",minimize.detail);
    Text(s,"tray","System tray","System tray needs a tray backend. Minimize keeps this window in the desktop task switcher.");
    Text(s,"separate","Separate windows","Separate wallet windows require a document-owner integration for multiple native windows.");
    ImGui::Separator();
    const auto sleep=port.Available(a::DesktopCapability::Sleep);
    Toggle(s,host,facts,"sleep","Keep awake during camera QR capture",&PublicPreferences::camera_keep_awake,sleep.accepted);
    Text(s,"sleep-status","Sleep-inhibition dependency",sleep.detail);
    const auto notifications=port.Available(a::DesktopCapability::Notifications);
    Toggle(s,host,facts,"notifications","Allow generic desktop notifications",&PublicPreferences::desktop_notifications,notifications.accepted);
    Text(s,"notify-status","Notification dependency",notifications.detail);
    const auto notify_allowed=[&s,&host]{return Current(s,host) && !s.privacy.hidden && !s.semantic_hide_toggle && s.general.saved
        && s.general.saved->preferences.desktop_notifications && host.Desktop()->Available(a::DesktopCapability::Notifications).accepted;};
    Button(s,facts,theme,"notification-test","Send generic test notification",notify_allowed(),[&s,&host,notify_allowed]{
        if(!notify_allowed())return false;
        const auto result=host.Desktop()->TestNotification();s.desktop.notice=result.detail;return result.accepted;
    });
    ImGui::Separator();
    Text(s,"updates","Updates and downloads",ReleaseUpdateStatus(ShippedExtensions()).detail);
    Link(s,host,facts,theme,"releases","View project releases",a::HelpTarget::Releases);
}
void DrawPage(State& s,a::Requests& host,Facts& f,const imkit::Theme& theme)
{
    auto& panel=s.desktop;
    switch(panel.page){
    case DesktopPage::Preferences:DrawPreferences(s,host,f,theme);break;
    case DesktopPage::Camera:{
        const auto* camera=host.Camera();
        if(!camera || !camera->Available()){
            Text(s,"camera-unavailable","Camera dependency","This build needs a supported native V4L2 camera adapter. QR image and text import remain available.");
            break;
        }
        Text(s,"camera-guide","Camera settings","Open QR import, then choose Find cameras, Open, a mode and Start capture.");
        Text(s,"camera-formats","Camera support","Use a supported YUYV or grayscale mode. QR images and text work without a camera.");
        Button(s,f,theme,"camera-import","Open QR import",!panel.scope.hidden && host.LocalDocuments(),[&s,&host]{
            if(!Current(s,host) || s.privacy.hidden || !BeginLocalDocument(s,host,LocalDocumentMode::Qr))return false;
            RetireDesktop(s);return true;
        });
        break;
    }
    case DesktopPage::Documentation:
        Text(s,"docs-summary","Documentation","Open a wallet through an explicit RPC profile. Review and approve transactions before signing.");
        Link(s,host,f,theme,"docs","Review documentation link",a::HelpTarget::Documentation);break;
    case DesktopPage::Support:
        Text(s,"support-summary","Support","The Bitcoin Roots project site has project information and support resources.");
        Link(s,host,f,theme,"support","Review project support link",a::HelpTarget::Support);break;
    case DesktopPage::Report:
        Text(s,"report-summary","Issue reporting","Review the local Activity log before reporting. Describe the behavior using the GUI version and public error category.");
        Text(s,"report-privacy","Diagnostics privacy","Exclude wallet and server identifiers, credentials and transaction data from your report.");
        Link(s,host,f,theme,"issues","Review project issue tracker link",a::HelpTarget::Issues);break;
    case DesktopPage::About:
        Text(s,"version","GUI version",std::string("GUI version ")+GUI_VERSION);
        Text(s,"license","Source and license","Bitcoin Roots by Plan-B Foundation · MIT license");
        Text(s,"lineage","Project lineage","Based on Bitcoin Core v32.1, with selected Bitcoin Knots policy features.");
        Link(s,host,f,theme,"source","Review source repository link",a::HelpTarget::Source);
        break;
    case DesktopPage::Restart:{
        Text(s,"restart-scope","Restart scope","Restart this GUI with its current RPC profile details ready for Test and Apply.");
        Text(s,"restart-recovery","Public workspace recovery","Restore saved public documents through the recovery prompt. Pending operations and approvals are retired.");
        const auto available=host.Desktop()->Available(a::DesktopCapability::Restart);
        if(!available.accepted)Text(s,"restart-blocked","Restart unavailable",available.detail,c::ax::SemanticRole::Status);
        Button(s,f,theme,"confirm-restart","Restart GUI",available.accepted,[&s,&host]{
            if(!Current(s,host) || s.privacy.hidden || s.semantic_hide_toggle)return false;
            const auto reply=host.Desktop()->Restart();s.desktop.notice=reply.detail;return reply.accepted;
        });
        break;
    }
    default:break;
    }
}
}

DesktopPage DesktopPageFor(app::Command command)
{
    switch(command){
    case app::Command::Tray:case app::Command::PreventSleep:return DesktopPage::Preferences;
    case app::Command::Camera:return DesktopPage::Camera;
    case app::Command::Documentation:return DesktopPage::Documentation;
    case app::Command::Support:return DesktopPage::Support;
    case app::Command::ReportIssue:return DesktopPage::Report;
    case app::Command::About:return DesktopPage::About;
    case app::Command::Restart:return DesktopPage::Restart;
    default:return DesktopPage::None;
    }
}
bool OpenDesktop(State& s,app::Requests& host,DesktopPage page)
{
    if(!host.Desktop() || page==DesktopPage::None || CurrentToolScope(s,host).locked)return false;
    if(page==DesktopPage::Preferences && s.general.owner){
        s.general.initialized=false;
        SyncGeneralPreferences(s,s.general.owner);
    }
    s.desktop.page=page;s.desktop.scope=CurrentToolScope(s,host);s.desktop.profile_revision=ProfileRevision(host);
    s.desktop.target.reset();s.desktop.confirmed_url.clear();s.desktop.notice.clear();
    s.desktop.dialog.open=true;s.desktop.dialog.initialFocus=true;++s.desktop.revision;
    return true;
}
void RetireDesktop(State& s)
{
    auto& panel=s.desktop;
    panel.dialog.open=false;panel.close_pending=true;panel.target.reset();panel.confirmed_url.clear();panel.notice.clear();++panel.revision;
}
void DrawDesktop(State& s,app::Requests& host,Facts& facts,const imkit::Theme& theme,float scale)
{
    auto& panel=s.desktop;
    if(panel.dialog.open && (!host.Desktop() || !Current(s,host)))RetireDesktop(s);
    if(panel.close_pending){
        if(ImGui::BeginPopupModal(POPUP)){ImGui::CloseCurrentPopup();ImGui::EndPopup();}
        panel.close_pending=false;
    }
    if(!panel.dialog.open)return;
    const auto* vp=ImGui::GetMainViewport();
    ImGui::SetNextWindowSize({std::min(680*scale,vp->WorkSize.x-32*scale),std::min(480*scale,vp->WorkSize.y-32*scale)},ImGuiCond_Always);
    if(!imkit::BeginDialog(POPUP,panel.dialog,{&theme})){if(!panel.dialog.open)RetireDesktop(s);return;}
    s.presentation.DialogOnly();facts.desktop_visible=true;
    const auto position=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
    const auto* title=Title(panel.page);
    s.presentation.Group("desktop-modal",title,c::ax::SemanticRole::Dialog,position,{position.x+size.x,position.y+size.y});
    ImGui::BeginChild("desktop-body",{0,-ImGui::GetFrameHeightWithSpacing()},ImGuiChildFlags_NavFlattened);
    Text(s,"desktop-title",title,title);
    if(panel.target)DrawLinkConfirmation(s,host,facts,theme);else DrawPage(s,host,facts,theme);
    if(!panel.notice.empty())Text(s,"desktop-notice","Desktop result",panel.notice,c::ax::SemanticRole::Status);
    if(panel.page==DesktopPage::Preferences)if(const auto status=host.Desktop()->Status();!status.empty())
        Text(s,"desktop-native-status","Desktop operation status",status,c::ax::SemanticRole::Status);
    if(panel.page==DesktopPage::Preferences && !s.general.error.empty())Text(s,"desktop-save-error","Preference not saved",s.general.error,c::ax::SemanticRole::Alert);
    ImGui::EndChild();
    Button(s,facts,theme,"close","Close",true,[&s]{RetireDesktop(s);return true;});
    if(!panel.dialog.open){ImGui::CloseCurrentPopup();panel.close_pending=false;}
    s.presentation.EndGroup();imkit::EndDialog();
}
}
