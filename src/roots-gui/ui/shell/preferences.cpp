// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "services/payment_protocols.h"
#include "ui/theme/theme.h"
#include <algorithm>

namespace roots::gui::ui::shell {
namespace {
namespace c=components;
constexpr char TITLE[]="Preferences###general-preferences";

void ReadSettings(State& s)
{
    auto& panel=s.general;
    const auto loaded=panel.owner->LoadWorkspacePreferences();
    if(!loaded){
        panel.saved.reset();
        panel.error="Saved preferences could not be read. Amounts stay hidden. Retry after the workspace store is available.";
        s.privacy.hidden=true;
        return;
    }
    panel.saved=*loaded.value;
    if(!panel.hide_not_saved)panel.error.clear();
    ApplyGeneralPresentation(s);
}

bool Save(State& s,storage::Workspace updated)
{
    auto& panel=s.general;
    if(!panel.owner || !panel.saved || panel.owner->Locked() || panel.saved->revision!=updated.revision)return false;
    const auto result=panel.owner->SaveWorkspacePreferences(updated);
    if(!result){
        panel.error=result.error==storage::Error::Conflict
            ? "Preferences changed in another window. Close and reopen Preferences before retrying."
            : std::string("Preferences were not saved (")+storage::ErrorName(result.error)+"). Retry when the store is available.";
        return false;
    }
    updated.revision=*result.value;
    panel.saved=std::move(updated);
    panel.hide_not_saved=false;
    panel.error.clear();
    ApplyGeneralPresentation(s);
    return true;
}

template <typename Change>
bool ChangeSettings(State& s,Change change)
{
    if(!s.general.saved)return false;
    auto updated=*s.general.saved;
    // Preserve the current legacy presentation when creating the first row.
    if(!updated.revision || s.general.hide_not_saved)updated.preferences.hide_balances=s.privacy.hidden;
    if(!updated.revision)updated.unit=s.unit;
    change(updated);
    return Save(s,std::move(updated));
}

c::ItemFacts Last(bool pressed,bool disabled)
{
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    return {{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),disabled,false,pressed,ImGui::IsItemVisible()};
}
}

void ApplyGeneralPresentation(State& s)
{
    const auto& saved=s.general.saved;
    if(!saved){if(s.general.owner)s.privacy.hidden=true;return;}
    if(!saved->revision)return;
    s.privacy.hidden=s.general.hide_not_saved || saved->preferences.hide_balances;
    s.privacy.trim_decimals=saved->preferences.trim_decimals;
    s.unit=saved->unit;
    s.documents.SetAddressGroupingDefault(saved->preferences.group_address_outputs);
}

bool SetDesktopPreference(State& s,bool PublicPreferences::*member,bool value)
{
    return ChangeSettings(s,[member,value](auto& updated){updated.preferences.*member=value;});
}

void CloseGeneralPreferences(State& s)
{
    auto& panel=s.general;
    panel.dialog.open=false;
    panel.close_pending=true;
    panel.explorer_template=c::Edit{};
}

void SyncGeneralPreferences(State& s,app::RecoverySession* owner)
{
    auto& panel=s.general;
    const auto epoch=owner?owner->Gate().Epoch():LockEpoch{};
    if(panel.initialized && panel.owner==owner && panel.epoch==epoch)return;
    panel.initialized=true;
    panel.owner=owner;
    panel.epoch=epoch;
    panel.saved.reset();
    panel.error.clear();
    if(!owner)return;
    if(owner->Locked()){
        CloseGeneralPreferences(s);
        s.privacy.hidden=true;
        return;
    }
    ReadSettings(s);
}

void OpenGeneralPreferences(State& s)
{
    auto& panel=s.general;
    if(!panel.owner || panel.owner->Locked() || panel.dialog.open)return;
    ReadSettings(s);
    panel.explorer_template=c::Edit(panel.saved?panel.saved->preferences.explorer_template:std::string{});
    panel.close_pending=false;
    panel.dialog.open=true;
    panel.dialog.initialFocus=true;
}

bool SetAmountVisibility(State& s,bool hidden)
{
    if(!s.general.owner){s.privacy.hidden=hidden;return true;}
    if(s.general.owner->Locked())return false;
    const bool saved=ChangeSettings(s,[hidden](storage::Workspace& next){next.preferences.hide_balances=hidden;});
    if(!saved && hidden){s.privacy.hidden=true;s.general.hide_not_saved=true;}
    return saved;
}

bool SetAmountUnit(State& s,int unit)
{
    if(unit<0 || unit>2)return false;
    if(!s.general.owner){s.unit=unit;return true;}
    return ChangeSettings(s,[unit](storage::Workspace& next){next.unit=unit;});
}
bool SetDecimalPresentation(State& s,bool trim)
{
    return ChangeSettings(s,[trim](storage::Workspace& next){next.preferences.trim_decimals=trim;});
}
bool SetAddressGroupingDefault(State& s,bool group)
{
    return ChangeSettings(s,[group](storage::Workspace& next){next.preferences.group_address_outputs=group;});
}

void DrawGeneralPreferences(State& s,app::Requests& host,Facts& facts,const imkit::Theme& theme,const AppearanceStatus& appearance,float scale)
{
    auto& panel=s.general;
    if(panel.close_pending){
        if(ImGui::BeginPopupModal(TITLE)){ImGui::CloseCurrentPopup();ImGui::EndPopup();}
        panel.close_pending=false;
    }
    if(!panel.dialog.open || !panel.owner || panel.owner->Locked())return;
    const auto* vp=ImGui::GetMainViewport();
    ImGui::SetNextWindowSize({std::min(750*scale,std::max(1.F,vp->WorkSize.x-32*scale)),
        std::min(630*scale,std::max(1.F,vp->WorkSize.y-32*scale))},ImGuiCond_Always);
    if(!imkit::BeginDialog(TITLE,panel.dialog,{&theme})){
        if(!panel.dialog.open)CloseGeneralPreferences(s);
        return;
    }
    facts.preferences_visible=true;
    s.presentation.DialogOnly();
    const auto pos=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
    s.presentation.Group("general-preferences","General preferences",c::ax::SemanticRole::Dialog,pos,{pos.x+size.x,pos.y+size.y});
    const auto available=[&panel]{return panel.dialog.open && panel.owner && !panel.owner->Locked();};
    const auto button=[&](const char* id,const char* label,bool enabled,auto operation,bool selected=false){
        ImGui::PushID(id);
        ImGui::BeginDisabled(!enabled);
        const bool pressed=theme::ActionButton(label,selected?imkit::ActionVariant::Primary:imkit::ActionVariant::Secondary,{},theme);
        facts.preferences[id]=Last(pressed,!enabled);
        const auto external=s.presentation.Interact(label,c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus,!enabled,selected);
        ImGui::EndDisabled();
        ImGui::PopID();
        const bool accepted=pressed && enabled && operation();
        if(external)s.presentation.Defer(*external,[operation,pressed,accepted](const auto&){return pressed?accepted:operation();},[available,enabled]{return enabled && available();});
    };
    const auto toggle=[&](const char* id,const char* label,bool current,auto operation){
        const bool enabled=panel.saved.has_value();
        ImGui::PushID(id);
        ImGui::BeginDisabled(!enabled);
        bool next=current;
        const bool pressed=imkit::Toggle("##value",&next,{&theme});
        facts.preferences[id]=Last(pressed,!enabled);
        const auto external=s.presentation.Interact(label,c::ax::SemanticRole::Toggle,c::ax::SemanticAction::Toggle|c::ax::SemanticAction::Focus,!enabled,current);
        ImGui::EndDisabled();
        ImGui::PopID();
        ImGui::SameLine();ImGui::TextUnformatted(label);
        const bool accepted=pressed && enabled && operation(next);
        if(external)s.presentation.Defer(*external,[operation,pressed,accepted,current](const auto&){return pressed?accepted:operation(!current);},[available,enabled]{return enabled && available();});
    };
    const auto heading=[&](const char* name){
        imkit::PushTextRole(theme,imkit::TextRole::Heading);ImGui::TextUnformatted(name);imkit::PopTextRole();
        s.presentation.Annotate(name,name,"",c::ax::SemanticRole::Group);
    };
    const auto detail=[&](const char* id,const char* text){
        ImGui::TextWrapped("%s",text);s.presentation.Annotate(id,text,"");
    };
    const auto appearance_choice=[&](int choice){facts.appearance_requested=choice;return true;};
    const float body_height=std::max(1.F,ImGui::GetContentRegionAvail().y-(panel.error.empty()?46:107)*scale);
    ImGui::BeginChild("preferences-rail",{132*scale,body_height},ImGuiChildFlags_Borders);
    heading("General");
    button("server","Server",host.Connection()!=nullptr,[&s,&host]{
        if(!host.Connection())return false;
        CloseGeneralPreferences(s);OpenServer(s.server,*host.Connection());return true;
    });
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("general-body",{0,body_height},ImGuiChildFlags_Borders|ImGuiChildFlags_NavFlattened);
    heading("Appearance");
    constexpr const char* themes[]{"Light","Dark","System"};
    for(int choice=0;choice<3;++choice){
        if(choice)ImGui::SameLine();
        const auto label=themes[choice];
        button(label,label,appearance.choice!=choice,[appearance_choice,choice]{return appearance_choice(choice);},appearance.choice==choice);
    }
    constexpr const char* units[]{"BTC","Satoshis","Automatic"};
    for(int unit=0;unit<3;++unit){
        if(unit)ImGui::SameLine();
        const auto label=units[unit];
        button(label,label,panel.saved && s.unit!=unit,[&s,unit]{return SetAmountUnit(s,unit);},s.unit==unit);
    }
    toggle("decimals","Trim trailing BTC decimals",s.privacy.trim_decimals,[&s](bool trim){return SetDecimalPresentation(s,trim);});
    if(appearance.save_failed)detail("appearance-error","Theme could not be saved. The previous theme stays active.");
    if(appearance.invalid_saved)detail("invalid-theme","Saved theme is invalid. Choose Light, Dark, or System to replace it.");
    if(appearance.system_unavailable)detail("system-theme","System theme unavailable. Light is used until the desktop setting is available.");
    ImGui::Separator();
    heading("Privacy");
    toggle("hide-amounts","Hide amounts",s.privacy.hidden,[&s](bool hide){return SetAmountVisibility(s,hide);});
    ImGui::Separator();
    heading("Bitcoin");
    detail("fee-source","Fee estimates use the applied Bitcoin RPC node. Use an exact manual rate in Send when estimates are unavailable.");
    const auto mode=panel.saved?panel.saved->preferences.explorer:ExplorerMode::Disabled;
    toggle("explorer","Allow explicit explorer links",mode!=ExplorerMode::Disabled,[&s](bool permit){
        return ChangeSettings(s,[permit](storage::Workspace& next){
            next.preferences.explorer=permit?ExplorerMode::NetworkDefault:ExplorerMode::Disabled;
            next.preferences.explorer_template.clear();next.preferences.explorer_genesis.clear();
        });
    });
    detail("explorer-permission","The default is available for mainnet and testnet4. Each link requires confirmation. Explorer links are unavailable through proxied profiles.");
    if(mode!=ExplorerMode::Disabled){
        imkit::BeginFormField("explorer-url",{"Custom HTTPS template","For the applied network. End the path with /{txid}.","",false},{&theme});
        c::EditInput("###custom-explorer",panel.explorer_template);
        facts.preferences["explorer-template"]=Last(false,false);
        const auto external=s.presentation.Interact("Custom HTTPS template",c::ax::SemanticRole::TextField,c::ax::SemanticAction::SetValue|c::ax::SemanticAction::Focus,false,false,panel.explorer_template.draft());
        if(external)s.presentation.Defer(*external,[&panel](const auto& request){return panel.explorer_template.SetExternalDraft(request.value);},available);
        imkit::EndFormField();
        const auto* connection=host.Connection();
        const bool direct=connection && connection->Active() && !connection->Offline() && connection->Active()->candidate.connection.proxy==ProxyKind::Direct;
        button("save-explorer","Use custom explorer",direct && ValidExplorerTemplate(panel.explorer_template.draft()),[&s,&host]{
            const auto* connection=host.Connection();
            if(!connection || !connection->Active() || connection->Offline() || connection->Active()->candidate.connection.proxy!=ProxyKind::Direct)return false;
            return ChangeSettings(s,[&s,connection](storage::Workspace& next){
                next.preferences.explorer=ExplorerMode::Custom;
                next.preferences.explorer_template=s.general.explorer_template.draft();
                next.preferences.explorer_genesis=connection->Active()->genesis;
            });
        });
        if(mode==ExplorerMode::Custom){
            ImGui::SameLine();
            button("reset-explorer","Use network default",true,[&s]{return ChangeSettings(s,[](storage::Workspace& next){
                next.preferences.explorer=ExplorerMode::NetworkDefault;
                next.preferences.explorer_template.clear();next.preferences.explorer_genesis.clear();
            });});
        }
    }
    ImGui::Separator();
    heading("Fiat");
    detail("fiat-unavailable",FiatRateStatus(ShippedExtensions()).detail);
    ImGui::Separator();
    heading("Wallet");
    detail("wallet-recovery",panel.owner->Enabled()?"Public workspace recovery is enabled.":"Public workspace recovery is off.");
    button("recovery","Manage public recovery",true,[&s]{CloseGeneralPreferences(s);s.recovery_dialog.open=true;s.recovery_dialog.initialFocus=true;return true;});
    detail("derivation","Wallet derivation is checked against public descriptor evidence when available.");
    ImGui::Separator();
    heading("Coin selection");
    toggle("group","Group outputs by address in new wallets",!panel.saved || panel.saved->preferences.group_address_outputs,[&s](bool group){return SetAddressGroupingDefault(s,group);});
    detail("unconfirmed","Funding requires eligible confirmed coins. Grouping changes the address view.");
    ImGui::Separator();
    heading("Notifications");
    detail("notification-policy",panel.saved && panel.saved->preferences.desktop_notifications?
        "Generic native notifications are opted in.":"Generic native notifications are off.");
    if(host.Desktop())button("desktop","Desktop preferences",true,[&s,&host]{CloseGeneralPreferences(s);return OpenDesktop(s,host,DesktopPage::Preferences);});
    detail("update-unavailable",ReleaseUpdateStatus(ShippedExtensions()).detail);
    ImGui::EndChild();
    if(!panel.error.empty()){
        ImGui::PushStyleColor(ImGuiCol_Text,theme.semantic.error);ImGui::TextWrapped("%s",panel.error.c_str());ImGui::PopStyleColor();
        s.presentation.Annotate("preferences-error","Preferences save or read error",panel.error,c::ax::SemanticRole::Alert);
    }
    button("close","Close",true,[&s]{CloseGeneralPreferences(s);return true;});
    if(!panel.dialog.open){ImGui::CloseCurrentPopup();panel.close_pending=false;}
    s.presentation.EndGroup();
    imkit::EndDialog();
}
}
