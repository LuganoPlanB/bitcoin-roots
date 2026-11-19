// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include <sodium.h>
#include <algorithm>
#include <type_traits>
namespace roots::gui::ui::shell {
namespace {
void PasswordInput(const char* id, std::array<char,256>& value, std::optional<std::string>& pending, bool reveal)
{
    // ImGui enables native Copy and Cut when a field is visually revealed.
    // Cut can erase a selection without publishing it. Paste remains enabled.
    auto& clipboard = ImGui::GetPlatformIO().Platform_SetClipboardTextFn;
    struct RestoreClipboard {
        decltype(clipboard) entry;
        std::remove_reference_t<decltype(clipboard)> previous;
        ~RestoreClipboard() { entry = previous; }
    } restore{clipboard, clipboard};
    clipboard = nullptr;
    const auto flags = ImGuiInputTextFlags_NoUndoRedo | (reveal ? 0 : ImGuiInputTextFlags_Password);
    components::TextInput(id,nullptr,value.data(),value.size(),pending,flags,nullptr,true,{},true);
}
void ClearPassword(PrivacyPanel& panel){sodium_memzero(panel.password.data(),panel.password.size());sodium_memzero(panel.confirm.data(),panel.confirm.size());for(auto* pending:{&panel.external_password,&panel.external_confirm}){if(*pending && !(*pending)->empty())sodium_memzero((*pending)->data(),(*pending)->size());pending->reset();}panel.reveal=false;}
void Retire(State& s,app::Requests& host){s.histories.clear();s.addresses.clear();s.receives.clear();s.settings.clear();s.advanced.clear();s.imported_views.clear();s.offline_builders.clear();RetireSendView(s);RetireBatch(s);RetireFunding(s);RetireFees(s);RetireLocalDocument(s,host);RetireWalletTool(s);s.activity_export={};s.activity_log.Clear();s.activity_notice.clear();s.coins.clear();s.coin_controls.PrivacyLock();s.coin_draft_errors.clear();s.registered_session=0;s.privacy.hidden=true;s.notice.clear();s.drawer=Drawer::None;s.recovery_dialog.open=false;s.close_recovery_popup=true;s.actions=DocumentActions();s.close_document_popup=true;s.server=ServerPanel();s.server.close_pending=true;if(components::native_input)components::native_input->PrivacyLock();s.lock_panel.focus=true;}
}
bool RequestPrivacyLock(State& s,app::Requests& host)
{
    if(!s.recovery)return false;
    if(s.desktop.dialog.open)RetireDesktop(s);
    if(s.message.dialog.open)RetireMessage(s,host);
    if(s.sweep.dialog.open)RetireSweepNotice(s);
    if(s.payment_protocol.dialog.open)RetirePaymentProtocolNotice(s);
    if(s.download.dialog.open)RetireDownload(s);
    RetireMetadataBackup(s);
    if(!s.recovery->Protected()){RetireWalletTool(s);s.activity_export={};s.lock_panel.setup=true;s.lock_panel.focus=true;return true;}
    if(!s.recovery->Lock(s.documents)){s.notice=s.recovery->Error();return false;}
    Retire(s,host);ClearPassword(s.lock_panel);return true;
}
void DrawPrivacyPanel(State& s,app::Requests& host,Facts& facts,const imkit::Theme& theme,float scale)
{
    if(!s.recovery || (!s.recovery->Locked() && !s.lock_panel.setup))return;
    auto& panel=s.lock_panel;const bool locked=s.recovery->Locked();const auto* vp=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);ImGui::SetNextWindowSize(vp->WorkSize);ImGui::SetNextWindowBgAlpha(1);
    if(panel.focus)ImGui::SetNextWindowFocus();
    ImGui::PushStyleColor(ImGuiCol_WindowBg,theme.semantic.canvas);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{24*scale,20*scale});
    ImGui::Begin("Workspace privacy###privacy-cover",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoDocking);
    s.presentation.DialogOnly();s.presentation.Group("privacy-dialog",locked?"Workspace locked":"Protect workspace",components::ax::SemanticRole::Dialog,vp->WorkPos,{vp->WorkPos.x+vp->WorkSize.x,vp->WorkPos.y+vp->WorkSize.y});
    const float width=std::min(560*scale,ImGui::GetContentRegionAvail().x);ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+width);
    imkit::PushTextRole(theme,imkit::TextRole::Heading);ImGui::TextUnformatted(locked?"Workspace locked":"Protect this workspace");imkit::PopTextRole();
    const char* detail=locked?"Workspace protection is enabled. Unlock to authenticate stored data and resolve any interrupted conversion.":"Set a local password to encrypt metadata and enable public recovery. This protection applies to the entire workspace. Use12–255 bytes.";
    ImGui::TextUnformatted(detail);s.presentation.Annotate("privacy-status","Workspace privacy",detail);
    const char* boundary="GUI privacy locking does not change daemon encryption or its unlock timeout. Work already accepted by a daemon cannot be remotely cancelled. Unlock never signs or broadcasts.";
    ImGui::TextUnformatted(boundary);s.presentation.Annotate("daemon-lock-boundary","Daemon lock boundary",boundary);
    const auto field=[&](const char* id,const char* label,std::array<char,256>& value,std::optional<std::string>& external){ImGui::TextUnformatted(label);ImGui::SetNextItemWidth(width);if(panel.focus){ImGui::SetKeyboardFocusHere();panel.focus=false;}PasswordInput(id,value,external,panel.reveal);
        const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();facts.document_actions[id]={{a.x,a.y,b.x,b.y},ImGui::GetItemID(),ImGui::IsItemFocused(),false,false,false,ImGui::IsItemVisible()};
        // Accessibility may supply input, but no password value/caret contents
        // are published. Native Password input also disables ordinary copying.
        const auto request=s.presentation.Interact(label,components::ax::SemanticRole::TextField,components::ax::SemanticAction::SetValue|components::ax::SemanticAction::Focus,false,false,{},"Protected input; value is never published.");if(request)s.presentation.Defer(*request,[&value,&external](const components::SemanticRequest& r){return components::SetExternalBuffer(value,external,r.value);});};
    field("###local-password","Local protection password",panel.password,panel.external_password);if(!locked)field("###confirm-password","Confirm local password",panel.confirm,panel.external_confirm);
    const auto action=[&](const char* id,const char* label,auto operation){ImGui::PushID(id);const auto display=std::string(label)+"###privacy-action";const bool pressed=theme::ActionButton(display.c_str(),imkit::ActionVariant::Secondary,{0,30*scale},theme);const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();facts.document_actions[id]={{a.x,a.y,b.x,b.y},ImGui::GetItemID(),ImGui::IsItemFocused(),false,false,pressed,ImGui::IsItemVisible()};const auto external=s.presentation.Interact(label,components::ax::SemanticRole::Button,components::ax::SemanticAction::Press|components::ax::SemanticAction::Focus);ImGui::PopID();const bool accepted=pressed && operation();if(external)s.presentation.Defer(*external,[operation,pressed,accepted](const components::SemanticRequest&){return pressed?accepted:operation();},[&s]{return s.recovery && (s.recovery->Locked() || s.lock_panel.setup);});};
    action("reveal-password",panel.reveal?"Hide password":"Reveal password",[&panel]{panel.reveal=!panel.reveal;return true;});
    action("unlock-workspace",locked?"Unlock workspace":"Protect and lock",[&s,&host,&panel,locked]{
        if(!locked && std::string_view(panel.password.data())!=panel.confirm.data()){panel.error="Passwords do not match. Existing work is retained.";ClearPassword(panel);panel.focus=true;return false;}
        bool ok=locked?s.recovery->Unlock(panel.password.data(),s.documents):s.recovery->Protect(s.documents,panel.password.data());
        if(ok && !locked)ok=s.recovery->Lock(s.documents);
        ClearPassword(panel);panel.focus=true;
        if(!ok){panel.error=s.recovery->Error().empty()?"Unlock failed. Work is retained.":s.recovery->Error();return false;}
        panel.error.clear();panel.setup=false;
        if(s.recovery->Locked())Retire(s,host);else{s.privacy.hidden=s.recovery->Hidden();s.unit=s.recovery->Unit();RestoreDocumentFocus(s,s.documents.ActiveId());s.notice="Workspace unlocked. Re-test the backend connection before reconnecting.";}
        return true;
    });
    if(!locked)action("cancel-protection","Cancel",[&panel]{ClearPassword(panel);panel.setup=false;panel.error.clear();return true;});
    if(locked && s.recovery->UnlockConflict())action("unlock-latest","Unlock latest saved workspace",[&s,&panel]{const bool ok=s.recovery->Unlock(panel.password.data(),s.documents,true);ClearPassword(panel);panel.focus=true;if(!ok){panel.error=s.recovery->Error();return false;}panel.error.clear();s.privacy.hidden=s.recovery->Hidden();s.unit=s.recovery->Unit();return true;});
    if(locked && s.recovery->ConversionConflict())action("resolve-conversion","Keep protected workspace; encrypt other copy",[&s,&panel]{const bool ok=s.recovery->Unlock(panel.password.data(),s.documents,false,true);ClearPassword(panel);panel.focus=true;if(!ok){panel.error=s.recovery->Error();return false;}panel.error.clear();s.privacy.hidden=s.recovery->Hidden();s.unit=s.recovery->Unit();s.notice="Other copy retained as metadata.sqlite.conversion-backup.protected. Re-test the backend before reconnecting.";return true;});
    if(locked)action("quit-locked","Quit",[&host]{return host.Submit({app::Command::Quit}).accepted;});
    if(!panel.error.empty()){ImGui::TextWrapped("%s",panel.error.c_str());s.presentation.Annotate("unlock-error","Unlock error",panel.error,components::ax::SemanticRole::Alert);}
    ImGui::PopTextWrapPos();s.presentation.EndGroup();ImGui::End();ImGui::PopStyleVar();ImGui::PopStyleColor();
}
}
