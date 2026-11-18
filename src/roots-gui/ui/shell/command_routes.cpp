// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include <algorithm>
namespace roots::gui::ui::shell {
ImGuiKeyChord CommandChord(std::string_view key,bool macos)
{
    if(key.empty())return 0;ImGuiKeyChord chord=macos?ImGuiMod_Super:ImGuiMod_Ctrl;
    if(key.starts_with("Shift+")){chord|=ImGuiMod_Shift;key.remove_prefix(6);}
    if(key.starts_with("Alt+")){chord|=ImGuiMod_Alt;key.remove_prefix(4);}
    if(key.size()!=1)return 0;const char ch=key.front();
    if(ch>='A' && ch<='Z')return chord|ImGuiKey(ImGuiKey_A+ch-'A');
    if(ch>='1' && ch<='6')return chord|ImGuiKey(ImGuiKey_1+ch-'1');
    return ch==','?chord|ImGuiKey_Comma:0;
}
std::string CommandShortcutLabel(std::string_view key,bool macos) {return key.empty()?"":std::string(macos?"Cmd+":"Ctrl+")+std::string(key);}
app::Availability CommandAvailability(const State& s,app::Command command,const app::Requests& host)
{
    namespace a=app;const auto* d=s.documents.Active();const bool wallet=d && d->kind==a::DocumentKind::Wallet;
    const bool transaction=d && d->kind==a::DocumentKind::Transaction;
    if(s.recovery && s.recovery->Locked())return command==a::Command::Quit?host.CanSubmit({command}):app::Availability{false,"Workspace is locked."};
    if(s.recovery && (command==a::Command::LockCurrent || command==a::Command::LockAll))return {true,{}};
    if(s.actions.kind!=DocumentDialog::None || s.server.dialog.open || s.recovery_dialog.open || s.lock_panel.setup)return {false,"Finish or cancel the current dialog first."};
    if(command==a::Command::Recovery)return {s.recovery!=nullptr,"Local public recovery storage is unavailable."};
    if(command>=a::Command::Transactions && command<=a::Command::Settings)return {wallet,wallet?"":"Select a wallet document to navigate its sections."};
    switch(command) {
    case a::Command::CloseTab:return {d!=nullptr,d?"":"No document is open to close."};
    case a::Command::RenameWallet:case a::Command::DeleteWallet:return {wallet,wallet?"":"Select a wallet tab for this wallet command."};
    case a::Command::ImportWallet:case a::Command::Units:case a::Command::HideAmounts:
    case a::Command::AppearanceSystem:case a::Command::AppearanceLight:case a::Command::AppearanceDark:case a::Command::LoadingLog:return {true,{}};
    case a::Command::SeparateWindows:return {false,"Separate wallet windows are unavailable: this native backend has one context/thread owner. The tab, drafts and selection remain here."};
    case a::Command::SaveTransaction:case a::Command::ShowQR:case a::Command::TransactionHex:
        if(!transaction)return {false,"Select a transaction document for this command."};break;
    case a::Command::ExportWallet:case a::Command::LockCurrent:case a::Command::SearchCurrent:case a::Command::Summaries:
        if(!wallet)return {false,"Select a wallet document for this command."};break;
    case a::Command::LockAll:case a::Command::SearchAll:
        if(std::none_of(s.documents.Documents().begin(),s.documents.Documents().end(),[](const auto& item){return item.kind==a::DocumentKind::Wallet;}))return {false,"Open a wallet document for this command."};break;
    default:break;
    }
    auto result=host.CanSubmit({command,d?d->id:0,d?d->section:a::Section::Transactions,{}});
    if(s.privacy.hidden)result.reason=result.available?"":a::MissingDependency(command)+" Details hidden.";
    if(!result.available && result.reason.empty())result.reason=a::MissingDependency(command);return result;
}
}
