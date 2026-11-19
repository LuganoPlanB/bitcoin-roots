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
app::Availability CommandAvailability(const State& s,app::Command command,app::Requests& host)
{
    namespace a=app;const auto* d=s.documents.Active();const bool wallet=d && d->kind==a::DocumentKind::Wallet;
    const bool transaction=d && d->kind==a::DocumentKind::Transaction;
    if(s.recovery && s.recovery->Locked())return command==a::Command::Quit?host.CanSubmit({command}):app::Availability{false,"Workspace is locked."};
    if(s.recovery && (command==a::Command::LockCurrent || command==a::Command::LockAll))return {true,{}};
    if(s.actions.kind!=DocumentDialog::None || s.local_document.dialog.open || s.local_document.close_pending || s.server.dialog.open || s.general.dialog.open || s.onboarding.dialog.open || s.recovery_dialog.open || s.lock_panel.setup || s.wallet_tools.dialog.open || s.desktop.dialog.open || s.message.dialog.open || s.sweep.dialog.open || s.payment_protocol.dialog.open || s.download.dialog.open)return {false,"Finish or cancel the current dialog first."};
    if(command==a::Command::VerifyDownload){
        if(s.recovery && s.recovery->Pending())return {false,"Finish workspace recovery first."};
        if(s.privacy.hidden || s.semantic_hide_toggle)return {false,"Reveal the workspace before selecting local verification files."};
        return {true,{}};
    }
    if(command==a::Command::Contacts){
        if(s.recovery && s.recovery->Pending())return {false,"Finish workspace recovery first."};
        return {true,{}}; // Opens local capability information only.
    }
    if(command==a::Command::Sweep){
        if(s.recovery && s.recovery->Pending())return {false,"Finish workspace recovery first."};
        if(s.privacy.hidden || s.semantic_hide_toggle)return {false,"Reveal the workspace to view sweep availability."};
        return {true,{}};
    }
    if(command==a::Command::SignMessage && host.Messages()){
        if(s.recovery && s.recovery->Pending())return {false,"Finish workspace recovery first."};
        if(s.privacy.hidden || s.semantic_hide_toggle)return {false,"Reveal the workspace before entering a message."};
        return {true,{}};
    }
    if(host.Desktop() && (DesktopPageFor(command)!=DesktopPage::None || command==a::Command::LogFile))
        return {!(s.recovery && s.recovery->Pending()),s.recovery && s.recovery->Pending()?"Finish workspace recovery first.":""};
    if(command==a::Command::Preferences && s.general.owner)return {true,{}};
    if(command==a::Command::Decimal || command==a::Command::Grouping)
        return s.general.saved?app::Availability{true,{}}:app::Availability{false,"Local preferences storage is unavailable."};
    if(command==a::Command::Recovery)return {s.recovery!=nullptr,"Local public recovery storage is unavailable."};
    if(command==a::Command::SubmissionAttempts)return {host.LocalDocuments() && s.recovery && s.recovery->BroadcastJournalAvailable()
        && !s.privacy.hidden && !s.semantic_hide_toggle,"Reveal and unlock the workspace to see local submission records."};
    if(command>=a::Command::Transactions && command<=a::Command::Settings)return {wallet,wallet?"":"Select a wallet document to navigate its sections."};
    if((host.Refresh() || host.Connection()) && (command==a::Command::SearchCurrent || command==a::Command::SearchAll || command==a::Command::Summaries)){
        if(command==a::Command::Summaries)return {true,{}};
        if(s.privacy.hidden || s.semantic_hide_toggle)return {false,"Reveal wallet details before searching."};
        if(command==a::Command::SearchCurrent)return {d && a::ToolWallet(*d),"Select an unlocked wallet with a verified RPC binding."};
        return {std::ranges::any_of(s.documents.Documents(),a::ToolWallet),"Open an unlocked wallet with a verified RPC binding."};
    }
    switch(command) {
    case a::Command::ExportWallet:
        if (!wallet) return {false,"Select a wallet tab before backing it up."};
        break;
    case a::Command::CloseTab:return {d!=nullptr,d?"":"No document is open to close."};
    case a::Command::RenameWallet:case a::Command::DeleteWallet:
        if(!wallet)return {false,"Select a wallet tab for this wallet command."};
        return !s.privacy.hidden && d->observation!=a::ObservationState::Locked
            ? a::Availability{true,{}}:a::Availability{false,"Reveal and unlock this wallet before changing its metadata."};
    case a::Command::ImportWallet:case a::Command::Units:case a::Command::HideAmounts:
    case a::Command::AppearanceSystem:case a::Command::AppearanceLight:case a::Command::AppearanceDark:case a::Command::LoadingLog:return {true,{}};
    case a::Command::SeparateWindows:return {false,"Separate wallet windows are unavailable: this native backend has one context/thread owner. The tab, drafts and selection remain here."};
    case a::Command::TransactionFile:case a::Command::TransactionText:case a::Command::TransactionQR:
        if (host.LocalDocuments()) return {!s.privacy.hidden && !s.semantic_hide_toggle && !(s.recovery && s.recovery->Pending())
            && s.documents.Documents().size()<32 && !host.LocalDocuments()->Busy(),"Unlock and reveal the workspace, or close an existing document."};
        break;
    case a::Command::SaveTransaction:case a::Command::TransactionHex:
        if (d && d->imported && host.LocalDocuments())
            return {!s.privacy.hidden && !s.semantic_hide_toggle && (command!=a::Command::TransactionHex || !d->imported->parsed->wire.empty()),
                "This representation is unavailable while workspace details are hidden or there are no transaction bytes."};
        [[fallthrough]];
    case a::Command::ShowQR:
        return {transaction && d->imported && d->imported->parsed->transaction && host.LocalDocuments()
            && !s.privacy.hidden && !s.semantic_hide_toggle && !(s.recovery && s.recovery->Pending()),
            "Select a visible PSBT or transaction document."};
    case a::Command::LockCurrent:case a::Command::SearchCurrent:case a::Command::Summaries:
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
