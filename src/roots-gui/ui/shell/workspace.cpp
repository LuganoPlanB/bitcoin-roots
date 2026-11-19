// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/localization.h"
#include "ui/shell/workspace.h"
#include "app/history_workspace.h"
#include "ui/theme/theme.h"
#include <algorithm>
#include <chrono>
#include <cfloat>
#include <cmath>
namespace roots::gui::ui::shell {
namespace {
namespace a=app;namespace c=components;
c::ItemFacts Last(bool requested=false) {
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    return {{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),bool(ImGui::GetItemFlags()&ImGuiItemFlags_Disabled),false,requested,ImGui::IsItemVisible()};
}
#if defined(__APPLE__)
constexpr bool apple_primary=true;
#else
constexpr bool apple_primary=false;
#endif
void Execute(State& s,a::Command id,a::Requests& host,Facts& f,bool semantic=false)
{
    const auto availability=CommandAvailability(s,id,host);const auto* active=s.documents.Active();
    s.invocation=Invocation{id,active?active->id:0,availability.available,false,false,availability.reason};
    if(!availability.available){s.notice=availability.reason;return;}
    s.invocation->accepted=true;s.notice.clear();
    if((id==a::Command::LockCurrent || id==a::Command::LockAll) && s.recovery){s.invocation->accepted=RequestPrivacyLock(s,host);return;}
    if(id==a::Command::Recovery && s.recovery){s.recovery_dialog.open=true;s.recovery_dialog.initialFocus=true;return;}
    if (host.Wallets()) {
        OnboardingPage page = OnboardingPage::None;
        switch (id) {
        case a::Command::Introduction: page = OnboardingPage::Introduction; break;
        case a::Command::OpenWallet: page = OnboardingPage::Open; break;
        case a::Command::NewWallet: page = OnboardingPage::Create; break;
        case a::Command::ImportWallet: page = OnboardingPage::Import; break;
        case a::Command::ExportWallet: page = OnboardingPage::Backup; break;
        default: break;
        }
        if (page != OnboardingPage::None) { BeginOnboarding(s,page,host); return; }
    }
    if(id==a::Command::Preferences && s.general.owner) {OpenGeneralPreferences(s);s.invocation->dispatched=true;return;}
    if(id==a::Command::Server && host.Connection()) {OpenServer(s.server,*host.Connection());s.invocation->dispatched=true;return;}
    if(id==a::Command::Sweep){
        // This route only opens the local capability notice. It has no host request.
        s.invocation->accepted=OpenSweepNotice(s,host);return;
    }
    if(id==a::Command::Contacts){s.invocation->accepted=OpenPaymentProtocolNotice(s,host);return;}
    if(id==a::Command::VerifyDownload){s.invocation->accepted=OpenDownload(s,host);return;}
    if(id==a::Command::SignMessage && host.Messages()){
        s.invocation->accepted=OpenMessage(s,host);s.invocation->dispatched=s.invocation->accepted;return;
    }
    if(host.Desktop() && id==a::Command::LogFile){s.drawer=Drawer::Activity;s.drawer_focus=true;s.invocation->dispatched=true;return;}
    if(const auto page=DesktopPageFor(id);host.Desktop() && page!=DesktopPage::None){
        s.invocation->accepted=OpenDesktop(s,host,page);s.invocation->dispatched=s.invocation->accepted;return;
    }
    if(id==a::Command::LoadingLog){s.drawer=Drawer::Activity;s.drawer_focus=true;return;}
    if(host.Refresh() || host.Connection()) {
        const auto tool=id==a::Command::SearchCurrent?WalletTool::CurrentSearch:
            id==a::Command::SearchAll?WalletTool::AllSearch:id==a::Command::Summaries?WalletTool::Summary:WalletTool::None;
        if(tool!=WalletTool::None){OpenWalletTool(s,host,tool);s.invocation->dispatched=true;return;}
    }
    if(id>=a::Command::Transactions && id<=a::Command::Settings) {
        if(!s.documents.Navigate(a::Section(int(id)-int(a::Command::Transactions))))s.notice="Select a wallet document to navigate its sections.";
        return;
    }
    if(id==a::Command::HideAmounts){
        if(semantic && !s.general.owner)s.semantic_hide_toggle=!s.semantic_hide_toggle;
        else s.invocation->accepted=SetAmountVisibility(s,!s.privacy.hidden);
        return;
    }
    if(id==a::Command::Units){s.invocation->accepted=SetAmountUnit(s,(s.unit+1)%3);return;}
    if(id==a::Command::Decimal){s.invocation->accepted=SetDecimalPresentation(s,!s.privacy.trim_decimals);return;}
    if(id==a::Command::Grouping && s.general.saved){
        s.invocation->accepted=SetAddressGroupingDefault(s,!s.general.saved->preferences.group_address_outputs);
        return;
    }
    if(id==a::Command::AppearanceSystem || id==a::Command::AppearanceLight || id==a::Command::AppearanceDark) {
        f.appearance_requested=id==a::Command::AppearanceSystem?2:id==a::Command::AppearanceDark?1:0;return;
    }
    if(id==a::Command::CloseTab){s.invocation->accepted=BeginClose(s,s.documents.ActiveId());return;}
    if(id==a::Command::Quit){
        const auto ids=s.documents.CloseTargets(s.documents.ActiveId(),false,true);
        if(!AllowPolicyClose(s,ids) || !SaveReceiveLabelsBeforeClose(s,ids)){
            s.invocation->accepted=false;
            return;
        }
    }
    if(id==a::Command::RenameWallet){BeginRename(s,s.documents.ActiveId());return;}
    if(id==a::Command::DeleteWallet){BeginDelete(s,s.documents.ActiveId());return;}
    if(host.LocalDocuments()) {
        if (id==a::Command::SubmissionAttempts) {
            s.invocation->accepted=BeginLocalDocument(s,host,LocalDocumentMode::BroadcastAttempts);
            return;
        }
        if (id==a::Command::TransactionFile || id==a::Command::TransactionText || id==a::Command::SaveTransaction) {
            const auto mode=id==a::Command::TransactionFile ? LocalDocumentMode::File
                : id==a::Command::TransactionText ? LocalDocumentMode::Text : LocalDocumentMode::Save;
            s.invocation->accepted=BeginLocalDocument(s,host,mode);
            return;
        }
        if (id==a::Command::TransactionQR || id==a::Command::ShowQR) {
            s.invocation->accepted=BeginLocalDocument(s,host,id==a::Command::ShowQR ? LocalDocumentMode::ShowQr : LocalDocumentMode::Qr);
            return;
        }
        if (id==a::Command::TransactionHex) {
            s.invocation->accepted=ToggleTransactionHex(s,host);
            return;
        }
    }
    if(id==a::Command::ImportWallet){BeginImport(s);return;}
    if(id==a::Command::SeparateWindows){s.notice="Separate wallet windows are unavailable: this native backend has one context/thread owner. The tab, drafts and selection remain here.";return;}
    const auto* d=s.documents.Active();const auto receipt=host.Submit({id,d?d->id:0,d?d->section:a::Section::Transactions,{}});
    s.invocation->dispatched=true;s.invocation->accepted=receipt.accepted;
    if(receipt.accepted && s.documents.ActiveId() && s.documents.ActiveId()!=s.invocation->document){s.focus_tab=s.documents.ActiveId();s.reveal_tab_focus=s.focus_tab;}
    s.notice=s.privacy.hidden?"Command result details hidden.":receipt.reason;
    if(s.notice.empty())s.notice=receipt.accepted?"Request acknowledged.":a::MissingDependency(id);
    s.invocation->reason=s.notice;
    if(s.notice.empty()) {const auto* spec=a::Spec(id);s.notice=spec?spec->dependency:"Section service is unavailable.";}
}
void Menus(State& s,a::Requests& host,Facts& f,const AppearanceStatus& appearance)
{
    for(const auto& command:a::CommandInventory()) {
        const auto chord=CommandChord(command.shortcut,apple_primary);
        f.availability[command.id]=CommandAvailability(s,command.id,host);
        f.shortcut_labels[command.id]=CommandShortcutLabel(command.shortcut,apple_primary);
        if(chord && ImGui::Shortcut(chord,ImGuiInputFlags_RouteGlobal))Execute(s,command.id,host,f);
    }
    bool any_open{};
    if(ImGui::BeginMenuBar()) {
        const char* names[]{"File","View","Tools","Help"};
        for(int menu=0;menu<4;++menu){
            f.menus[menu]=ImGui::GetID(names[menu]);
            const float scale=ImGui::GetFontSize()/14.F;const auto size=ImGui::GetMainViewport()->WorkSize;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{12*scale,12*scale});
            ImGui::SetNextWindowSizeConstraints({0,0},{std::max(1.F,size.x-16*scale),std::max(1.F,size.y-16*scale)});
            if(s.semantic_menu_request==menu){ImGui::OpenPopup(names[menu]);s.semantic_menu_request=-1;}
            const bool menu_open=ImGui::BeginMenu(names[menu]);any_open|=menu_open;
            // BeginMenu uses the public popup identity in this menu-bar scope.
            // Its header remains a real focus target even while closed.
            if(!menu_open){const auto header=s.presentation.Interact(names[menu],c::ax::SemanticRole::Menu,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus);
                if(header)s.presentation.Defer(*header,[&s,menu](const c::SemanticRequest&){s.semantic_menu_request=menu;return true;});}
            if(menu_open) {
            // A queued semantic command runs after the menu was drawn. Dismiss
            // its popup before opening the dialog at the workspace layer.
            if((s.wallet_tools.dialog.open && s.wallet_tools.dialog.initialFocus) || (s.desktop.dialog.open && s.desktop.dialog.initialFocus)
                || (s.message.dialog.open && s.message.dialog.initialFocus) || (s.sweep.dialog.open && s.sweep.dialog.initialFocus)
                || (s.payment_protocol.dialog.open && s.payment_protocol.dialog.initialFocus)
                || (s.download.dialog.open && s.download.dialog.initialFocus)){
                ImGui::CloseCurrentPopup();ImGui::EndMenu();ImGui::PopStyleVar();continue;
            }
            for(const auto& command:a::CommandInventory())if(int(command.menu)==menu) {
                const auto& shortcut=f.shortcut_labels.at(command.id);
                const bool selected=(command.id==a::Command::HideAmounts && s.privacy.hidden) ||
                    (command.id==a::Command::Decimal && s.privacy.trim_decimals) ||
                    (command.id==a::Command::Grouping && s.general.saved && s.general.saved->preferences.group_address_outputs) ||
                    (command.id==a::Command::AppearanceLight && appearance.choice==0) ||
                    (command.id==a::Command::AppearanceDark && appearance.choice==1) ||
                    (command.id==a::Command::AppearanceSystem && appearance.choice==2);
                // Routes remain keyboard inspectable. Unavailable services
                // return a persistent explanation, never an invented effect.
                f.menu_items[command.id]=ImGui::GetID(command.label);
                const bool pressed=ImGui::MenuItem(command.label,shortcut.c_str(),selected);
                const auto& available=f.availability.at(command.id);
                const auto external=s.presentation.Interact(command.label,c::ax::SemanticRole::MenuItem,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus,!available.available,selected,{},available.reason);
                s.presentation.BindShortcut(ImGui::GetItemID(),shortcut);
                if(pressed)Execute(s,command.id,host,f);
                if(external)s.presentation.Defer(*external,[&s,&host,&f,id=command.id,pressed,native_ok=s.invocation && s.invocation->accepted](const c::SemanticRequest&){if(pressed)return native_ok;Execute(s,id,host,f,true);return s.invocation && s.invocation->accepted;});
                f.menu_rows[command.id]=Last();
                const auto& availability=f.availability.at(command.id);
                if(!availability.available && (ImGui::IsItemHovered() || (ImGui::IsItemFocused() && ImGui::GetIO().NavVisible))) {
                    ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+std::min(440.F,ImGui::GetMainViewport()->WorkSize.x-48.F));ImGui::TextUnformatted(availability.reason.c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();
                }
            }
            ImGui::EndMenu();
        }ImGui::PopStyleVar();}
        ImGui::EndMenuBar();
    }
    s.semantic_menu_open=any_open;if(any_open)s.presentation.OnlyCurrentLayer();
}
void At(const Rect& r) { ImGui::SetCursorScreenPos({r.x,r.y}); }
void Tabs(State& s,a::Requests& host,Facts& f,const imkit::Theme& theme,float scale,const Rect& region)
{
    At(region);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{8*scale,0});ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{12*scale,0});
    ImGui::PushStyleColor(ImGuiCol_ChildBg,theme.semantic.canvas);
    ImGui::BeginChild("documents",{region.w,region.h},ImGuiChildFlags_NavFlattened|ImGuiChildFlags_AlwaysUseWindowPadding,ImGuiWindowFlags_HorizontalScrollbar|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoSavedSettings);
    s.presentation.Group("semantic-documents","Documents",c::ax::SemanticRole::TabList,{region.x,region.y},{region.x+region.w,region.y+region.h});
    if(s.documents.Documents().empty())ImGui::TextUnformatted("No documents");
    if(s.close_tab_popup){ImGui::PushID(std::to_string(s.close_tab_popup).c_str());if(ImGui::BeginPopup("tab-actions")){ImGui::CloseCurrentPopup();ImGui::EndPopup();}ImGui::PopID();s.close_tab_popup=0;}
    // Context actions can reorder/erase the registry during this traversal.
    // Visit each displayed identity once; resolve it again before borrowing.
    std::vector<uint64_t> displayed;
    for(const auto& document:s.documents.Documents())displayed.push_back(document.id);
    bool context_visible{};
    for(size_t i=0;i<displayed.size();++i) {
        const auto* document=s.documents.Find(displayed[i]);if(!document)continue;
        const auto& d=*document;if(i)ImGui::SameLine();ImGui::PushID(std::to_string(d.id).c_str());
        if(s.focus_tab==d.id && s.actions.kind==DocumentDialog::None && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup)){
            // An Escape-dismissed popup can restore an active input in another
            // child. Clear that public window focus before the one-shot return
            // request; otherwise the old input steals navigation back.
            ImGui::SetWindowFocus(nullptr);ImGui::SetWindowFocus();ImGui::SetKeyboardFocusHere();s.focus_tab=0;
        }
        const bool concealed=s.privacy.hidden || d.observation==a::ObservationState::Locked;
        const auto name=concealed?std::string(d.kind==a::DocumentKind::Wallet?"Wallet ":"Transaction ")+std::to_string(d.id):d.name;
        const std::string display=(concealed?"":d.icon==1?"o ":d.icon==2?"<> ":"")+name+(d.Dirty()?" *":"");
        const float width=std::clamp(ImGui::CalcTextSize(display.c_str(),nullptr,false).x+24*scale,140*scale,260*scale);
        ImGui::SetCursorPosY(scale);
        const bool activate=theme::ActionButton("###tab",imkit::ActionVariant::Secondary,{width,34*scale},theme,ImGui::GetForegroundDrawList());
        const auto item=Last();f.tabs[d.id]=item;f.tab_hovered[d.id]=ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride);f.tab_held[d.id]=ImGui::IsItemActive() && ImGui::GetIO().MouseDown[0];const auto& r=item.rect;auto* draw=ImGui::GetWindowDrawList();
        const auto external=s.presentation.Interact(name.c_str(),c::ax::SemanticRole::Tab,c::ax::SemanticAction::Select|c::ax::SemanticAction::Focus,false,d.id==s.documents.ActiveId(),name);
        f.annotated_tabs.push_back(d.id);
        if(s.reveal_tab_focus==d.id && item.focused){ImGui::SetNavCursorVisible(true);s.reveal_tab_focus=0;}
        f.tab_cursor_visible[d.id]=ImGui::IsAnyItemFocused();
        const ImVec4 clip{r[0]+8*scale,r[1],r[2]-8*scale,r[3]};
        draw->AddText(ImGui::GetFont(),ImGui::GetFontSize(),{r[0]+8*scale,r[1]+(r[3]-r[1]-ImGui::GetFontSize())/2},ImGui::GetColorU32(theme.semantic.text),display.data(),display.data()+display.size(),0,&clip);
        if(d.id==s.documents.ActiveId())draw->AddLine({r[0]+1,r[3]-2*scale},{r[2]-1,r[3]-2*scale},ImGui::GetColorU32(theme.semantic.text),2*scale);
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride) || (item.focused && ImGui::GetIO().NavVisible)) {
            ImGui::BeginTooltip();ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+std::min(480*scale,ImGui::GetMainViewport()->WorkSize.x-32*scale));
            ImGui::TextUnformatted(name.c_str());ImGui::PopTextWrapPos();ImGui::EndTooltip();
        }
        if(activate)s.documents.Activate(d.id);
        if(external)s.presentation.Defer(*external,[&s,key=d.id,activate](const c::SemanticRequest&){if(activate)return true;const bool accepted=s.documents.Activate(key);if(accepted){s.focus_tab=key;s.reveal_tab_focus=key;}return accepted;});
        if(item.focused) {
            ImGui::SetScrollHereX(.5F);
            const int offset=ImGui::IsKeyPressed(ImGuiKey_RightArrow)?1:ImGui::IsKeyPressed(ImGuiKey_LeftArrow)?-1:0;
            if(offset) { const auto next=(i+displayed.size()+offset)%displayed.size();s.documents.Activate(displayed[next]);s.focus_tab=s.documents.ActiveId(); }
        }
        const auto tab_id=d.id;
        if(item.focused && ImGui::IsKeyPressed(ImGuiKey_F10) && ImGui::GetIO().KeyShift)ImGui::OpenPopup("tab-actions");
        if(ImGui::BeginPopupContextItem("tab-actions")) {context_visible=true;s.semantic_context=tab_id;s.presentation.DialogOnly();DrawTabActions(s,host,f,tab_id,theme);s.presentation.OnlyCurrentLayer();ImGui::EndPopup();}
        ImGui::PopID();
    }
    if(!context_visible)s.semantic_context=0;
    s.presentation.EndGroup();ImGui::EndChild();ImGui::PopStyleColor();ImGui::PopStyleVar(2);
}
void Icon(int section,ImVec2 p,float size,ImU32 color)
{
    auto* d=ImGui::GetWindowDrawList();const float u=size/20,stroke=std::max(1.F,1.5F*u);
    const auto line=[&](float x,float y,float xx,float yy){d->AddLine({p.x+x*u,p.y+y*u},{p.x+xx*u,p.y+yy*u},color,stroke);};
    if(section<=2) {
        if(section!=2){line(2,5,18,5);line(14,1,18,5);line(18,5,14,9);}
        if(section!=1){line(18,15,2,15);line(6,11,2,15);line(2,15,6,19);}
    } else if(section==3) {
        d->AddRect({p.x+2*u,p.y+u},{p.x+18*u,p.y+19*u},color,2*u,stroke);line(6,6,14,6);line(6,11,14,11);line(6,16,12,16);
    } else if(section==4) {
        d->AddRect({p.x+2*u,p.y+2*u},{p.x+14*u,p.y+14*u},color,2*u,stroke);line(7,18,18,18);line(18,18,18,7);
    } else {
        for(int i=0;i<3;++i){const float y=4+6*i;line(1,y,19,y);d->AddRectFilled({p.x+(i==1?6:12)*u,p.y+(y-2)*u},{p.x+(i==1?9:15)*u,p.y+(y+2)*u},color);}
    }
}
void Rail(State& s,Facts& f,const imkit::Theme& theme,float scale)
{
    At(f.layout.rail);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{(f.layout.compact?6:8)*scale,6*scale});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{ImGui::GetStyle().ItemSpacing.x,4*scale});
    ImGui::BeginChild("navigation",{f.layout.rail.w,f.layout.rail.h},ImGuiChildFlags_AlwaysUseWindowPadding|ImGuiChildFlags_NavFlattened);
    {const auto p=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();f.layout.rail={p.x,p.y,size.x,size.y};}
    s.presentation.Group("semantic-navigation","Wallet sections",c::ax::SemanticRole::Toolbar,{f.layout.rail.x,f.layout.rail.y},{f.layout.rail.x+f.layout.rail.w,f.layout.rail.y+f.layout.rail.h});
    if(components::native_input)components::native_input->Privacy(s.privacy.hidden);
    const auto* active=s.documents.Active();const bool wallet=active && active->kind==a::DocumentKind::Wallet;
    const float extent=std::max(31*scale,std::floor((f.layout.rail.h-12*scale-5*ImGui::GetStyle().ItemSpacing.y)/6));
    for(int i=0;i<6;++i) {
        ImGui::PushID(i);ImGui::BeginDisabled(!wallet);
        const float width=std::max(1.F,ImGui::GetContentRegionAvail().x);
        const float wrap=std::max(1.F,width-(f.layout.compact?4:12)*scale);
        const auto* label=locale::Text(a::SectionNames[i]);
        auto* font=ImGui::GetFont();
        const float label_size=(f.layout.compact?12.5F:14.F)*scale;
        const auto text=font->CalcTextSizeA(label_size,FLT_MAX,wrap,label);
        const float gap=3*scale;
        const float height=std::max(extent,text.y+2*gap);
        const bool pressed=theme::ActionButton("###section",imkit::ActionVariant::Secondary,{width,height},theme);
        const auto external=s.presentation.Interact(label,c::ax::SemanticRole::Radio,c::ax::SemanticAction::Select|c::ax::SemanticAction::Focus,!wallet,wallet && int(active->section)==i);
        auto item=Last(pressed);f.navigation[i]=item;const auto& r=item.rect;
        auto* draw=ImGui::GetWindowDrawList();const bool selected=wallet && int(active->section)==i;
        if(selected)draw->AddRectFilled({r[0]+1,r[1]+1},{r[2]-1,r[3]-1},ImGui::GetColorU32(theme.semantic.control.selected),theme.radius.control);
        const float icon=std::min(20*scale,height-text.y-3*gap);
        const float leading=icon>=10*scale ? icon+gap : 0.F;
        const float top=r[1]+(height-leading-text.y)/2;
        if (leading>0) Icon(i,{(r[0]+r[2]-icon)/2,top},icon,ImGui::GetColorU32(theme.semantic.text));
        const ImVec4 clip{r[0],r[1],r[2],r[3]};
        draw->AddText(font,label_size,{r[0]+(width-text.x)/2,top+leading},ImGui::GetColorU32(theme.semantic.text),label,nullptr,wrap,&clip);
        if(selected)draw->AddLine({r[0]+4*scale,r[3]-2*scale},{r[2]-4*scale,r[3]-2*scale},ImGui::GetColorU32(theme.semantic.text),2*scale);
        if(pressed)s.documents.Navigate(a::Section(i));
        if(external)s.presentation.Defer(*external,[&s,i,pressed](const c::SemanticRequest&){return pressed || s.documents.Navigate(a::Section(i));});
        if(s.reveal_navigation==i){ImGui::SetScrollHereY(.5F);s.reveal_navigation=-1;}
        ImGui::EndDisabled();ImGui::PopID();
    }
    s.presentation.EndGroup();ImGui::EndChild();ImGui::PopStyleVar(2);
}
struct ContextCommands final:c::Commands {
    a::Requests& host;uint64_t document;a::Section section;std::string& notice;
    ContextCommands(a::Requests& h,uint64_t d,a::Section s,std::string& n):host(h),document(d),section(s),notice(n){}
    bool Available(const c::Command& value) const override {return host.CanSubmit({a::Command::Component,document,section,value}).available;}
    c::CommandEndpoint CaptureAvailability() const override {return [h=&host,d=document,section=section](const c::Command& value){return h->CanSubmit({a::Command::Component,d,section,value}).available;};}
    c::CommandEndpoint Capture() override {return [h=&host,d=document,section=section,n=&notice](const c::Command& value){const a::Request request{a::Command::Component,d,section,value};const auto available=h->CanSubmit(request);if(!available.available){*n=available.reason;return false;}const auto receipt=h->Submit(request);if(!receipt.accepted)*n=receipt.reason;return receipt.accepted;};}
    bool Submit(const c::Command& value) override { const auto receipt=host.Submit({a::Command::Component,document,section,value});if(!receipt.accepted)notice=receipt.reason;return receipt.accepted; }
};
void Content(State& s,a::Requests& host,Facts& f,const imkit::Theme& theme,float scale,const AppearanceStatus& appearance)
{
    auto* d=s.documents.Active();
    const bool public_document=d && (d->history.enabled || d->observed_transaction || d->coin_draft || d->imported);
    At(f.layout.content);
    if (!d || d->Current().restore_scroll) {
        if (public_document) {
            auto& position=d->Current();
            ImGui::SetNextWindowScroll({position.scroll_x,position.scroll_y});
            position.restore_scroll=false;
        } else {
            ImGui::SetNextWindowScroll({0,0});
        }
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{24*scale,d && (d->history.enabled || d->observed_transaction)?8*scale:16*scale});
    ImGui::BeginChild("content",{f.layout.content.w,f.layout.content.h},ImGuiChildFlags_AlwaysUseWindowPadding|ImGuiChildFlags_NavFlattened);
    {const auto p=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();f.layout.content={p.x,p.y,size.x,size.y};}
    const auto appearance_notice=[&](const char* id,const char* text) {
        ImGui::TextWrapped("%s",text);
        const auto lo=ImGui::GetWindowDrawList()->GetClipRectMin(),hi=ImGui::GetWindowDrawList()->GetClipRectMax();
        const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
        const bool visible=ImGui::IsItemVisible() && a.x>=lo.x && a.y>=lo.y && b.x<=hi.x && b.y<=hi.y;
        f.appearance_notice_visible=(!f.appearance_notice_count || f.appearance_notice_visible) && visible;++f.appearance_notice_count;
        s.presentation.Annotate(id,"Appearance status",text,imkit::accessibility::SemanticRole::Alert);
    };
    if(appearance.invalid_saved)appearance_notice("appearance-invalid","Saved appearance is invalid; using Light. Choose an appearance to replace it.");
    if(appearance.save_failed)appearance_notice("appearance-save","Appearance could not be saved. Choose again to retry.");
    if(appearance.system_unavailable)appearance_notice("appearance-system","System appearance is unavailable; using Light. Saved choice remains System.");
    if(!s.general.error.empty()){
        ImGui::TextWrapped("%s",s.general.error.c_str());
        s.presentation.Annotate("preferences-error","Preferences",s.general.error,c::ax::SemanticRole::Alert);
    }
    if(!d) {
        if(!s.notice.empty()){const auto text=s.privacy.hidden?std::string("Command result details hidden."):s.notice;ImGui::TextWrapped("%s",text.c_str());s.presentation.Annotate("command-notice","Command status",text,c::ax::SemanticRole::Alert);}
        const auto available=ImGui::GetContentRegionAvail();
        const float width=std::max(1.F,std::min(450*scale,available.x)),height=std::max(1.F,std::min(230*scale,available.y));
        const auto start=ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos({start.x+std::max(0.F,(available.x-width)/2),start.y+std::max(0.F,(available.y-height)/2)});
        const auto pos=ImGui::GetCursorScreenPos();f.drop_target={pos.x,pos.y,width,height};s.native_drop_target=f.drop_target;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{16*scale,16*scale});
        ImGui::BeginChild("empty-drop",{width,height},ImGuiChildFlags_AlwaysUseWindowPadding|ImGuiChildFlags_NavFlattened);s.presentation.Group("semantic-empty-drop","Open public document",c::ax::SemanticRole::Group,pos,{pos.x+width,pos.y+height});
        auto* draw=ImGui::GetWindowDrawList();const auto color=ImGui::GetColorU32(theme.semantic.border);
        const float dash=8*scale,gap=5*scale;
        for(float x=0;x<width;x+=dash+gap){const float end=std::min(width,x+dash);draw->AddLine({pos.x+x,pos.y},{pos.x+end,pos.y},color,scale);draw->AddLine({pos.x+x,pos.y+height-1},{pos.x+end,pos.y+height-1},color,scale);}
        for(float y=0;y<height;y+=dash+gap){const float end=std::min(height,y+dash);draw->AddLine({pos.x,pos.y+y},{pos.x,pos.y+end},color,scale);draw->AddLine({pos.x+width-1,pos.y+y},{pos.x+width-1,pos.y+end},color,scale);}
        ImGui::TextUnformatted("Bitcoin Roots");
        ImGui::TextWrapped("Drop a public wallet or transaction file");s.presentation.Annotate("drop-hint","Open public documents","Drop a public wallet or transaction file");
        const char* services=host.Connection() && host.Connection()->Active() && !host.Connection()->Offline()?"Open a loaded daemon wallet, or create a wallet on the connected node.":host.Wallets()?"Offline public workspace. Use Help → Introduction to configure a node when needed.":"New, Open or Import a public document. Daemon and import services are currently unavailable.";
        ImGui::TextWrapped("%s",services);s.presentation.Annotate("drop-services","Document services",services);
        const float action_width=std::max(1.F,(ImGui::GetContentRegionAvail().x-2*ImGui::GetStyle().ItemSpacing.x)/3);
        const bool create=theme::ActionButton("New",imkit::ActionVariant::Secondary,{action_width,32*scale},theme);f.document_actions["new"]=Last(create);const auto create_ax=s.presentation.Interact("New wallet",c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus,!CommandAvailability(s,a::Command::NewWallet,host).available);
        ImGui::SameLine();const bool open=theme::ActionButton("Open",imkit::ActionVariant::Primary,{action_width,32*scale},theme);f.primary=Last(open);const auto open_ax=s.presentation.Interact("Open wallet",c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus,!CommandAvailability(s,a::Command::OpenWallet,host).available);
        ImGui::SameLine();if(s.focus_empty){ImGui::SetKeyboardFocusHere();s.focus_empty=false;}const bool import=theme::ActionButton("Import",imkit::ActionVariant::Secondary,{action_width,32*scale},theme);f.document_actions["open-import"]=Last(import);const auto import_ax=s.presentation.Interact(host.Wallets()?"Import wallet":"Import public file",c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus);
        if(s.reveal_empty_focus && f.document_actions.at("open-import").focused){ImGui::SetNavCursorVisible(true);s.reveal_empty_focus=false;}
        if(create)Execute(s,a::Command::NewWallet,host,f);if(open)Execute(s,a::Command::OpenWallet,host,f);if(import)Execute(s,a::Command::ImportWallet,host,f);
        if(create_ax)s.presentation.Defer(*create_ax,[&s,&host,&f,create](const c::SemanticRequest&){if(create)return s.invocation && s.invocation->accepted;Execute(s,a::Command::NewWallet,host,f);return s.invocation && s.invocation->accepted;});
        if(open_ax)s.presentation.Defer(*open_ax,[&s,&host,&f,open](const c::SemanticRequest&){if(open)return s.invocation && s.invocation->accepted;Execute(s,a::Command::OpenWallet,host,f);return s.invocation && s.invocation->accepted;});
        if(import_ax)s.presentation.Defer(*import_ax,[&s,&host,&f,import](const c::SemanticRequest&){if(!import)Execute(s,a::Command::ImportWallet,host,f,true);return s.invocation && s.invocation->accepted;});
        s.presentation.EndGroup();ImGui::EndChild();ImGui::PopStyleVar();
        if(ImGui::BeginDragDropTarget()) {
            if(const auto* payload=ImGui::AcceptDragDropPayload("ROOTS_GUI_PUBLIC_IMPORT_PATH")) {
                if(payload->DataSize>0 && payload->DataSize<=4097){const auto* data=static_cast<const char*>(payload->Data);const std::string_view path(data,size_t(payload->DataSize));if(path.back()=='\0')BeginImport(s,path.substr(0,path.size()-1));}
            }ImGui::EndDragDropTarget();
        }
        ImGui::EndChild();ImGui::PopStyleVar();return;
    }
    if(HasReceiveLabelError(s,d->section==a::Section::Receive && host.Receiving()?d->id:0)){
        const char* note="An unsaved Receive label is retained. Return to its Receive tab to review it.";
        ImGui::TextWrapped("%s",note);
        s.presentation.Annotate("receive-retained","Receive label status",note,c::ax::SemanticRole::Alert);
    }
    if(public_document){
        // Drawing can change the active document. Remember this window's
        // scroll before passing control to a section or workbench action.
        auto& position=d->Current();
        position.scroll_x=ImGui::GetScrollX();
        position.scroll_y=ImGui::GetScrollY();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{8*scale,4*scale});
        ImGui::PushID(std::to_string(d->id).c_str());
        if(d->imported) DrawImportedDocument(s,host,f,theme,scale);
        else if(d->coin_draft) {
            const auto id = d->id;
            DrawCoinDraft(s,host,f,theme,scale);
            const auto* draft = s.documents.Active();
            const auto builder = s.offline_builders.find(id);
            if (draft && draft->id == id && draft->coin_draft
                && (builder == s.offline_builders.end() || !builder->second.open)
                && !s.privacy.hidden && ImGui::CollapsingHeader("Payment recipients")) {
                DrawSendRecipients(s, host, f, theme, scale);
            } else RetireSendView(s);
        }
        else if(d->observed_transaction) DrawObservedTransaction(s,f,theme,scale);
        else if(d->section==a::Section::Transactions) DrawHistory(s,host,f,theme,scale);
        else if(d->section==a::Section::Send && d->complete && d->observation!=a::ObservationState::Locked)
            DrawSendRecipients(s,host,f,theme,scale);
        else if(d->section==a::Section::Addresses) DrawAddresses(s,host,f,theme,scale);
        else if(d->section==a::Section::Receive && host.Receiving()) DrawReceive(s,host,f,theme,scale);
        else if(d->section==a::Section::UTXOs) DrawCoins(s,host,f,theme,scale);
        else if(d->section==a::Section::Settings && host.Wallets()) DrawPolicySettings(s,host,f,theme,scale);
        else {
            ImGui::TextUnformatted(locale::Text(a::SectionNames[size_t(d->section)]));
            ImGui::TextWrapped("This wallet section is not available yet. Use Transactions for dated read-only observations.");
            s.presentation.Annotate("wallet-section-unavailable","Wallet section","Unavailable; use Transactions for read-only observations");
        }
        ImGui::PopID();
        ImGui::PopStyleVar();
        ImGui::EndChild();ImGui::PopStyleVar();return;
    }
    const bool wallet=d->kind==a::DocumentKind::Wallet;const auto section=d->section;auto& state=d->Current();
    const auto reason=wallet?a::Dependency(*d,section):std::string{};
    imkit::PushTextRole(theme,imkit::TextRole::Heading);ImGui::TextUnformatted(wallet?locale::Text(a::SectionNames[size_t(section)]):"Transaction document");s.presentation.Annotate("section-heading","Current section",wallet?locale::Text(a::SectionNames[size_t(section)]):"Transaction document");imkit::PopTextRole();
    const char* summary=!d->complete?"Incomplete wallet policy":d->observation==a::ObservationState::Loading?"Loading public fixture":d->observation==a::ObservationState::Error?"Fixture request failed":d->observation==a::ObservationState::Stale?"Stale public observations":d->observation==a::ObservationState::Locked?"Wallet locked":d->observation==a::ObservationState::Empty?"No public observations":"Offline / public fixture";
    imkit::PushTextRole(theme,imkit::TextRole::Caption);ImGui::TextUnformatted(summary);
    if(s.privacy.hidden){ImGui::SameLine();ImGui::TextUnformatted("Amounts hidden");}imkit::PopTextRole();
    if(!s.notice.empty()){const auto text=s.privacy.hidden?std::string("Command result details hidden."):s.notice;ImGui::TextWrapped("%s",text.c_str());s.presentation.Annotate("command-notice","Command status",text,c::ax::SemanticRole::Alert);}
    const float footer=48*scale;
    ImGui::PushID(std::to_string(d->id).c_str());ImGui::PushID(wallet?int(section):6);
    s.presentation.Group("semantic-section",wallet?locale::Text(a::SectionNames[size_t(section)]):"Transaction document public observations",c::ax::SemanticRole::Group,{f.layout.content.x,f.layout.content.y},{f.layout.content.x+f.layout.content.w,f.layout.content.y+f.layout.content.h});
    ImGui::BeginChild("section-body",{0,std::max(1.F,ImGui::GetContentRegionAvail().y-footer)},ImGuiChildFlags_NavFlattened,ImGuiWindowFlags_HorizontalScrollbar);
    const bool restoring=state.restore_scroll;
    if(state.restore_scroll){ImGui::SetScrollX(state.scroll_x);ImGui::SetScrollY(state.scroll_y);state.restore_scroll=false;}
    c::FreshnessStatus status{d->observation==a::ObservationState::Stale?Freshness::Stale:Freshness::Offline,d->observation==a::ObservationState::Error,false,"Public fixture data only; no daemon or wallet service.","2026-11-15 12:00 UTC"};
    if(d->observation==a::ObservationState::Loading){status.detail="Loading public fixture observations; prior view state retained.";status.busy=true;}
    if(!d->complete)status.detail="Wallet policy is incomplete. A daemon-wallet adapter is required; no balance data is available.";
    if(!reason.empty())ImGui::TextWrapped("%s",reason.c_str());
    c::StatusBanner("fixture-status",status,theme,&s.presentation);
    if(reason.empty()) {
        ContextCommands commands(host,d->id,section,s.notice);
        f.note=c::LabelEditor("section-note",state.note,commands,theme,reason.empty(),&s.presentation,[&s,key=d->id,section]{const auto* current=s.documents.Find(key);return current && s.documents.ActiveId()==key && a::Dependency(*current,section).empty();});
        if(!d->complete)ImGui::TextWrapped("Wallet policy is incomplete. A daemon-wallet adapter is required to configure public policy; no balance data is available.");
        else if(d->observation==a::ObservationState::Empty)ImGui::TextWrapped("No public observations are available. Your draft and position remain retained.");
        else {
            if(ImGui::BeginTable("observations",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_Borders|ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY|ImGuiTableFlags_SizingFixedFit,{0,240*scale},1200*scale)) {
                const auto lo=ImGui::GetWindowDrawList()->GetClipRectMin(),hi=ImGui::GetWindowDrawList()->GetClipRectMax();f.table_hover={lo.x,lo.y,hi.x,hi.y};s.presentation.Group("semantic-grid","Public observations",c::ax::SemanticRole::Grid,lo,hi);
                const bool restoring_table=state.restore_table;
                if(state.restore_table){ImGui::SetScrollX(state.table_x);ImGui::SetScrollY(state.table_y);state.restore_table=false;}
                ImGui::TableSetupColumn("Public label",ImGuiTableColumnFlags_WidthFixed,800*scale);
                ImGui::TableSetupColumn(s.unit==1?"Amount (sats)":s.unit==2?"Amount (automatic)":"Amount (BTC)",ImGuiTableColumnFlags_WidthFixed,160*scale);
                ImGui::TableSetupColumn("Availability",ImGuiTableColumnFlags_WidthFixed,220*scale);ImGui::TableHeadersRow();
                for(int i=0;i<40;++i) {
                    const uint64_t key=i+1;const bool enabled=i!=3;ImGui::PushID(std::to_string(key).c_str());ImGui::TableNextRow(ImGuiTableRowFlags_None,28*scale);ImGui::TableNextColumn();
                    ImGui::BeginDisabled(!enabled);const auto at=ImGui::GetCursorScreenPos();
                    const bool choose=ImGui::Selectable("###row",state.selection.selected.contains(key),ImGuiSelectableFlags_SpanAllColumns);f.rows.push_back(Last(choose));
                    const std::string semantic_label=enabled?"Public observation "+std::to_string(key)+" / full literal Invoice##2026 / horizontal inspection":"Frozen public observation (host fixture)";
                    const auto external=s.presentation.Interact(semantic_label.c_str(),c::ax::SemanticRole::Row,c::ax::SemanticAction::Select|c::ax::SemanticAction::Focus,!enabled,state.selection.selected.contains(key),{},enabled?"":"Frozen public fixture");
                    s.presentation.Parent(f.rows.back().id);
                    if(ImGui::IsItemFocused())state.selection.focused=key;
                    const std::string label=enabled?"Public observation "+std::to_string(key)+" / full literal Invoice##2026 / horizontal inspection":"Frozen public observation (host fixture)";
                    ImGui::SetCursorScreenPos(at);ImGui::TextUnformatted(label.c_str());s.presentation.Annotate("literal-label","Public label",label,c::ax::SemanticRole::Cell,!enabled);ImGui::EndDisabled();
                    bool native_accepted{};
                    if(choose && enabled) {
                        auto next=state.selection.selected;if(ImGui::GetIO().KeyCtrl){if(!next.erase(key))next.insert(key);}else next={key};
                        native_accepted=commands.Request(c::SelectRows{"observations",{next.begin(),next.end()}});if(native_accepted)state.selection.selected=std::move(next);
                    }
                    if(external)s.presentation.Defer(*external,[&state,submit=commands.Capture(),key,ctrl=ImGui::GetIO().KeyCtrl,choose,native_accepted](const c::SemanticRequest&){
                        if(choose)return native_accepted;auto next=state.selection.selected;if(ctrl){if(!next.erase(key))next.insert(key);}else next={key};const bool accepted=submit(c::SelectRows{"observations",{next.begin(),next.end()}});if(accepted)state.selection.selected=std::move(next);return accepted;
                    });
                    ImGui::TableNextColumn();const auto value=Money::Satoshis(1000000+i);const auto amount=s.privacy.Present(value);
                    const auto text=!s.privacy.hidden && s.unit!=0?std::to_string(value.sats())+(s.unit==2?" sats":""):amount.text;
                    ImGui::TextUnformatted(text.c_str());if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",text.c_str());
                    s.presentation.Annotate("amount","Observation amount",text,imkit::accessibility::SemanticRole::Cell,!enabled);
                    ImGui::TableNextColumn();ImGui::TextUnformatted(enabled?"Public fixture":"Frozen public fixture");s.presentation.Annotate("availability","Observation availability",enabled?"Public fixture":"Frozen public fixture",c::ax::SemanticRole::Cell,!enabled);s.presentation.EndGroup();ImGui::PopID();
                }
                if(!restoring_table){state.table_x=ImGui::GetScrollX();state.table_y=ImGui::GetScrollY();}f.horizontal_scroll=ImGui::GetScrollMaxX()>0;f.table_scroll_x=ImGui::GetScrollX();f.table_scroll_max_x=ImGui::GetScrollMaxX();
                if(ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_Tab,ImGuiInputFlags_RouteFocused)) {
                    s.focus_tab=d->id;s.reveal_tab_focus=d->id;++s.grid_exits;f.grid_escape=true;
                }
                s.presentation.EndGroup();ImGui::EndTable();
            }
            for(int i=0;i<24;++i)ImGui::Text("Public observation detail %d: watch-only fixture provenance and notes.",i+1);
        }
    }
    if(!restoring){state.scroll_x=ImGui::GetScrollX();state.scroll_y=ImGui::GetScrollY();}f.observed_scroll_x=ImGui::GetScrollX();f.observed_scroll_y=ImGui::GetScrollY();

    ImGui::EndChild();
    const char* action=wallet?(section==a::Section::Send?"Inspect draft":section==a::Section::Settings?"Inspect policy":"Inspect section"):"Inspect transaction";
    ContextCommands commands(host,d->id,section,s.notice);
    f.primary=c::ActionButton("primary",action,c::ActionKind::Primary,commands,theme,reason.empty(),reason.c_str(),&s.presentation,[&s,key=d->id,section]{const auto* current=s.documents.Find(key);return current && s.documents.ActiveId()==key && (current->kind!=a::DocumentKind::Wallet || a::Dependency(*current,section).empty());});
    s.presentation.EndGroup();ImGui::PopID();ImGui::PopID();ImGui::EndChild();ImGui::PopStyleVar();
}
}
void RestoreDocumentFocus(State& s,uint64_t desired) {
    const auto target=s.documents.Find(desired)?desired:s.documents.ActiveId();s.focus_tab=target;s.reveal_tab_focus=target;s.focus_empty=!target;s.reveal_empty_focus=!target;
}
Layout Measure(ImVec2 origin,ImVec2 size,float scale,float menu_height)
{
    const bool compact=size.x<1100;const float tabs=36*scale,status=36*scale,rail=(compact?96:180)*scale;
    const float y=origin.y+menu_height+tabs,h=std::max(1.F,size.y-menu_height-tabs-status);
    return {{origin.x,origin.y,size.x,menu_height},{origin.x,origin.y+menu_height,size.x,tabs},{origin.x,y,rail,h},{origin.x+rail,y,std::max(1.F,size.x-rail),h},{origin.x,origin.y+size.y-status,size.x,status},{},compact};
}
std::string SemanticScope(const State& s) {
    std::string scope=std::to_string(s.documents.ActiveId())+":"+std::to_string(s.semantic_hide_toggle)+":"+std::to_string(s.privacy.hidden)+":"+std::to_string(int(s.actions.kind))+":"+std::to_string(s.actions.target)+":"+std::to_string(int(s.drawer))+":"+std::to_string(s.close_tab_popup)+":"+std::to_string(s.close_document_popup)+":"+std::to_string(s.semantic_menu_open)+":"+std::to_string(s.semantic_context)+":"+std::to_string(s.semantic_context_revision);
    scope+=":"+std::to_string(s.general.dialog.open)+":"+std::to_string(s.general.saved?s.general.saved->revision:0)+":"+std::to_string(int(s.server.source));
    scope+=":"+std::to_string(int(s.wallet_tools.tool))+":"+std::to_string(s.wallet_tools.revision)+":"+std::to_string(s.activity_log.Revision());
    scope+=":"+std::to_string(int(s.desktop.page))+":"+std::to_string(s.desktop.revision)+":"+std::to_string(s.desktop.dialog.open);
    scope+=":"+std::to_string(s.message.revision)+":"+std::to_string(s.message.dialog.open)+":"+std::to_string(s.message.pending);
    scope+=":"+std::to_string(s.payment_protocol.dialog.open)+":"+std::to_string(s.payment_protocol.profile_revision);
    scope+=":"+std::to_string(s.recovery_dialog.open)+":"+std::to_string(s.metadata_backup.revision);
    scope+=":"+std::to_string(int(s.local_document.mode))+":"+std::to_string(s.local_document.context.generation)
        +":"+std::to_string(s.local_document.revision);
    if(const auto view=s.imported_views.find(s.documents.ActiveId());view!=s.imported_views.end())
        scope+=":"+std::to_string(view->second.revision);
    scope+=":"+std::to_string(s.signing_revision);
    scope+=":"+std::to_string(s.transaction_edit_revision);
    if(const auto draft=s.offline_builders.find(s.documents.ActiveId());draft!=s.offline_builders.end())
        scope+=":"+std::to_string(draft->second.revision);
    if (const auto* d = s.documents.Active(); d && d->SendWallet())
        scope += ":" + std::to_string(d->recipients.selected) + "," + std::to_string(s.send_view.wallet_destinations)
            + "," + std::to_string(s.funding.revision) + "," + std::to_string(s.fees.revision);
    scope+=":"+std::to_string(s.batch_generation)+","+std::to_string(s.batch?s.batch->revision:0)+","+std::to_string(s.batch.has_value())+","+std::to_string(s.send_analysis.has_value());
    for(const auto& d:s.documents.Documents())scope+=":"+std::to_string(d.id)+","+std::to_string(int(d.section))+","+std::to_string(int(d.observation))+","+std::to_string(d.complete)+","+std::to_string(d.name.size())+","+std::to_string(d.draft_revision.value);
    for(const auto& d:s.documents.Documents())if(d.history.enabled)scope+=":"+std::to_string(d.history.query_revision)+","+std::to_string(d.history.metadata_revision)+","+d.history.selected+","+std::to_string(d.history.sort)+","+std::to_string(d.history.descending)+","+d.history.filter;
    if(const auto* d=s.documents.Active();d && d->history.enabled)for(const auto& [key,draft]:d->history.drafts)scope+=":"+key+":"+std::to_string(draft.draft.size())+":"+draft.draft;
    for(const auto& [id,h]:s.histories)scope+=":"+std::to_string(id)+","+std::to_string(h.observed)+","+std::to_string(h.generation)+","+std::to_string(h.sequence);
    for(const auto& d:s.documents.Documents())if(d.history.enabled)scope+=":"+d.addresses.selected+","+std::to_string(d.addresses.group)+","+std::to_string(d.addresses.hide_empty_used)+","+std::to_string(d.addresses.transactions)+","+std::to_string(d.addresses.split);
    if(const auto r=s.receives.find(s.documents.ActiveId());r!=s.receives.end())scope+=":"+std::to_string(r->second.revision);
    if(const auto settings=s.settings.find(s.documents.ActiveId());settings!=s.settings.end())scope+=":"+std::to_string(settings->second.revision);
    if(const auto advanced=s.advanced.find(s.documents.ActiveId());advanced!=s.advanced.end())scope+=":"+std::to_string(advanced->second.revision);
    for(const auto& [id,a]:s.addresses)scope+=":"+std::to_string(id)+","+std::to_string(a.generation)+","+std::to_string(a.sequence)+","+std::to_string(a.history_sequence);
    for(const auto& d:s.documents.Documents())if(d.history.enabled){scope+=":"+d.coins.focused+","+std::to_string(d.coins.by_value)+","+std::to_string(d.coins.descending)+","+std::to_string(d.coins.metadata_revision);scope+=":"+std::to_string(d.coins.SelectionRevision());for(const auto& [key,draft]:d.coins.drafts)scope+=":"+key+":"+draft.draft;}
    for(const auto& [id,c]:s.coins)scope+=":"+std::to_string(id)+","+std::to_string(c.generation)+","+std::to_string(c.sequence)+","+std::to_string(c.observed)+","+std::to_string(c.chart_group)+","+std::to_string(c.chain_current);
    for(const auto& [id,error]:s.coin_draft_errors)scope+=":"+std::to_string(id)+":"+error;scope+=":"+std::to_string(s.coin_controls.Revision());for(const auto& d:s.documents.Documents())if(d.coin_draft){scope+=":"+std::to_string(d.id)+","+std::to_string(d.draft_revision.value);for(const auto& p:d.coin_draft->inputs)scope+=":"+app::CoinKey(p.outpoint)+":"+p.current_reason;}
    const auto& o=s.observations;size_t bytes=o.last_success.size()+o.error.size();for(const auto* observation:{&o.devices,&o.proxy,&o.updates,&o.network,&o.connection,&o.activity})bytes+=observation->detail.size();
    for(const auto& task:o.tasks)bytes+=task.title.size()+task.detail.size();for(const auto& line:o.log)bytes+=line.size();
    return scope+":"+std::to_string(o.tasks.size())+":"+std::to_string(o.log.size())+":"+std::to_string(bytes)+":"+std::to_string(components::native_input && components::native_input->NativeDialogActive())+":"+std::to_string(s.server.dialog.open)+":"+std::to_string(s.server.scope_revision)+":"+std::to_string(s.server.scope_session)+":"+std::to_string(s.recovery_dialog.open)+":"+std::to_string(s.recovery && s.recovery->Pending())+":"+std::to_string(s.recovery && s.recovery->Locked())+":"+std::to_string(s.recovery?s.recovery->Gate().Epoch().value:0)+":"+std::to_string(s.lock_panel.setup)+":"+std::to_string(s.onboarding.revision);
}
Facts DrawWorkspace(State& s,app::Requests& host,const imkit::Theme& theme,const BootstrapAppearance& appearance,ImVec2 work_size)
{
    s.recovery=host.Recovery();SyncGeneralPreferences(s,s.recovery);s.coin_controls.Sync(s.documents,s.recovery,host.Refresh(),RefreshMonotonicMs());BeginHistoryLabelFrame(s);BeginCoinFrame(s);
    if(s.recovery && host.Refresh()){auto view=host.Refresh()->View();if(view.identity && view.ledger){const app::Document* wallet=nullptr;for(const auto& d:s.documents.Documents())if(d.kind==app::DocumentKind::Wallet && view.identity->context.document.value()=="wallet-document-"+std::to_string(d.id)){wallet=&d;break;}if(wallet){const auto scope=std::to_string(wallet->id)+":"+std::to_string(view.identity->context.lock_epoch.value);if(scope!=s.history_policy_scope || s.history_policy_revision!=s.coin_controls.Revision() || s.history_policy_sequence!=view.history_sequence || s.history_policy_generation!=view.generation){auto local=s.coin_controls.Policy(*wallet,view,*s.recovery);HistoryPolicy policy;policy.known=local && local->rules.size()==view.ledger->Coins().size();if(local)for(const auto& [key,rule]:local->rules)if(auto p=app::ParseCoinKey(key)){if(rule==app::CoinRule::Frozen)policy.frozen.push_back(*p);else if(rule==app::CoinRule::Reserved)policy.reserved.push_back(*p);}host.Refresh()->SetHistoryPolicy(*view.identity,std::move(policy));s.history_policy_scope=scope;s.history_policy_revision=s.coin_controls.Revision();s.history_policy_sequence=view.history_sequence;s.history_policy_generation=view.generation;}}}}
    if(s.recovery && s.recovery->Locked())s.privacy.hidden=true;
    if(s.recovery)s.recovery->SetPresentation(s.privacy.hidden,s.unit);
    if(s.recovery && s.recovery->Pending() && !s.recovery_prompted){s.recovery_dialog.open=true;s.recovery_dialog.initialFocus=true;s.recovery_prompted=true;}
    if(s.semantic_hide_toggle){s.privacy.hidden=!s.privacy.hidden;s.semantic_hide_toggle=false;}
    if(components::native_input)components::native_input->Privacy(s.privacy.hidden);
    PollReplacement(s,host);
    const auto* active=s.documents.Active();const auto active_id=active?active->id:0;
    if (s.send_view.document && (!active || active_id != s.send_view.document || s.privacy.hidden
        || (active->kind == app::DocumentKind::Wallet && active->section != a::Section::Send)))
        RetireSendView(s);
    const bool locked=active && active->observation==app::ObservationState::Locked;
    if (auto* devices=host.Devices()) {
        const auto section=active ? std::optional{active->section} : std::nullopt;
        if (s.previous_document!=active_id || s.previous_device_section!=section || locked || s.privacy.hidden
            || (s.recovery && s.recovery->Locked())) devices->Cancel();
        s.previous_device_section=section;
        devices->Poll(host.Connection());
    }
    // A retired body must leave a real focus target for keyboard/native readers.
    // Observe the model so external lock observations take the same path as UI
    // commands; this one-shot transition never steals focus on later frames.
    if(locked && (s.previous_document!=active_id || !s.previous_locked))RestoreDocumentFocus(s,active_id);
    if (s.previous_document!=active_id || s.privacy.hidden || locked) {
        const auto previous=s.imported_views.find(s.previous_document);
        if (previous!=s.imported_views.end() && previous->second.inspection_owner) {
            previous->second.inspection_owner.reset();
            previous->second.selection.reset();
            previous->second.selection_label.clear();
            previous->second.inspection_status.clear();
            ++previous->second.revision;
        }
    }
    s.previous_document=active_id;s.previous_locked=locked;
    BeginReceiveLabelFrame(s);PollReceive(s,host);PollPolicySettings(s,host);PollAdvancedSettings(s,host);
    std::erase_if(s.histories,[&](const auto& item){return !s.documents.Find(item.first) && !item.second.editor;});
    std::erase_if(s.coins,[&](const auto& item){return !s.documents.Find(item.first) && !item.second.editor;});
    std::erase_if(s.imported_views,[&](const auto& item){return !s.documents.Find(item.first);});
    std::erase_if(s.offline_builders,[&](const auto& item){const auto* doc=s.documents.Find(item.first);return !doc || !doc->coin_draft;});
    PollBatch(s,host);
    PollFees(s,host);
    PollFunding(s,host);
    PollTransactionEditors(s,host);
    PollSigningReviews(s,host);
    s.broadcasts=host.Broadcasts();
    PollBroadcast(s,host);
    Facts f;s.native_drop_target={};s.observations=host.Observe();
    const auto tools_scope=CurrentToolScope(s,host);
    if(s.activity_session!=tools_scope.connection || s.activity_epoch!=tools_scope.epoch || tools_scope.locked){
        s.activity_log.Clear();s.activity_export={};s.activity_notice.clear();
        s.activity_session=tools_scope.connection;s.activity_epoch=tools_scope.epoch;
    }
    if(!tools_scope.locked)s.activity_log.Observe(s.observations,std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    s.observations.log.clear();
    s.observations.activity.detail.clear();
    for(auto& task:s.observations.tasks){task.title.clear();task.detail.clear();}
    s.presentation.Begin(SemanticScope(s),[&s]{return SemanticScope(s);});s.presentation.Suppress(s.actions.kind!=DocumentDialog::None || s.recovery_dialog.open || s.onboarding.dialog.open || s.local_document.dialog.open || s.wallet_tools.dialog.open || s.desktop.dialog.open || s.message.dialog.open || s.sweep.dialog.open || s.payment_protocol.dialog.open || s.download.dialog.open);const float scale=appearance.font_scale*appearance.dpi_scale;const auto* vp=ImGui::GetMainViewport();
    if(work_size.x<=0 || work_size.y<=0)work_size=vp->WorkSize;
    ImGui::SetNextWindowPos(vp->WorkPos);ImGui::SetNextWindowSize(work_size);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
    ImGui::Begin("Bitcoin Roots###workspace",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_MenuBar|ImGuiWindowFlags_NoDocking);
    ImGui::BeginDisabled(components::native_input && components::native_input->NativeDialogActive());
    ClosePendingDocumentPopup(s);
    f.layout=Measure(vp->WorkPos,work_size,scale,ImGui::GetFrameHeight());Menus(s,host,f,appearance.status);
    const bool privacy_locked=s.recovery && s.recovery->Locked();
    if(!privacy_locked && !s.general.dialog.open){Tabs(s,host,f,theme,scale,f.layout.tabs);Rail(s,f,theme,scale);
    if(const auto* d=s.documents.Active()){f.active_document=d->id;if(d->kind==app::DocumentKind::Wallet)f.section=int(d->section);}
    Content(s,host,f,theme,scale,appearance.status);
    DrawStatus(s,f,theme,scale);DrawActivityDrawer(s,f,theme,scale,&host);
    DrawDocumentDialog(s,host,f,theme,scale);
    DrawRecoveryDialog(s,host,f,theme,scale);}
    DrawOnboarding(s,host,f,theme,scale);
    DrawLocalDocumentDialog(s,host,f,theme,scale);
    DrawGeneralPreferences(s,host,f,theme,appearance.status,scale);
    if(auto* connection=host.Connection())f.server=DrawServer(s.server,*connection,s.presentation,theme,scale,s.privacy.hidden);
    DrawWalletTools(s,host,f,theme,scale);
    DrawDesktop(s,host,f,theme,scale);
    DrawMessage(s,host,f,theme,scale);
    DrawSweepNotice(s,host,f,theme,scale);
    DrawPaymentProtocolNotice(s,host,f,theme,scale);
    DrawDownload(s,host,f,theme,scale);
    DrawPrivacyPanel(s,host,f,theme,scale);
    if(s.recovery && s.recovery->Locked() && s.onboarding.page!=OnboardingPage::None)RetireOnboarding(s,host);
    ImGui::EndDisabled();
    ImGui::End();ImGui::PopStyleVar();FinishHistoryLabelFrame(s,host);FinishCoinFrame(s,host);s.presentation.Seal();FinishReceiveLabelFrame(s);
    if(s.recovery)s.recovery->SetPresentation(s.semantic_hide_toggle?!s.privacy.hidden:s.privacy.hidden,s.unit);
    if(s.recovery && s.recovery->Enabled() && !s.recovery->Locked() && !s.recovery->Pending() && !s.recovery->Checkpoint(s.documents))s.notice="Public recovery was not saved: "+s.recovery->Error()+". Previous snapshot retained.";
    if(components::native_input)components::native_input->Privacy(s.privacy.hidden || s.semantic_hide_toggle);
    // A deferred close or lock can retire the projections during Seal().
    // Do not return a borrowed view of those retired keys.
    const auto history=s.histories.find(f.active_document);
    if (history==s.histories.end() || history->second.projection.keys.data()!=f.history_order.data()) f.history_order={};
    const auto coins=s.coins.find(f.active_document);
    if (coins==s.coins.end() || !coins->second.projection || coins->second.projection->keys.data()!=f.coin_order.data()) f.coin_order={};
    return f;
}
}
