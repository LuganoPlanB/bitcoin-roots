// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/commands.h"
#include <algorithm>
#include <cctype>
namespace roots::gui::app {
namespace {
constexpr CommandSpec inventory[]{
#define ROUTE(id,menu,label,key,reason) {Command::id,Menu::menu,label,key,reason}
 ROUTE(NewWallet,File,"New Wallet","N","Wallet creation requires a daemon-wallet adapter."),
 ROUTE(OpenWallet,File,"Open Wallet","O","Wallet discovery requires a daemon-wallet adapter."),
 ROUTE(ImportWallet,File,"Import Wallet","","Wallet import service is unavailable."),
 ROUTE(ExportWallet,File,"Export Wallet","","Wallet export service is unavailable."),
 ROUTE(TransactionFile,File,"Open Transaction from File","F","Transaction file adapter is unavailable."),
 ROUTE(TransactionId,File,"Open Transaction from ID","I","Transaction lookup adapter is unavailable."),
 ROUTE(TransactionText,File,"Open Transaction from Text","T","Transaction parsing adapter is unavailable."),
 ROUTE(TransactionQR,File,"Open Transaction from QR","U","Local QR image and decoded-text import requires the transaction document service."),
 ROUTE(SaveTransaction,File,"Save Transaction / PSBT","S","Transaction persistence adapter is unavailable."),
 ROUTE(ShowQR,File,"Show Transaction / PSBT QR","","Select a visible transaction or PSBT for QR export."),
 ROUTE(RenameWallet,File,"Rename Wallet","","Wallet metadata service is unavailable."),
 ROUTE(DeleteWallet,File,"Delete Wallet","","Separate wallet deletion service is unavailable."),
 ROUTE(Preferences,File,"Preferences",",","Preferences service is unavailable in this shell."),
 ROUTE(Recovery,File,"Session Recovery","","Local public recovery storage is unavailable."),
 ROUTE(SubmissionAttempts,File,"Submission Attempts","","Local submission records are unavailable."),
 ROUTE(CloseTab,File,"Close Tab","W",""),ROUTE(Quit,File,"Quit","",""),
 ROUTE(Units,View,"BTC / sats / automatic","B",""),ROUTE(Decimal,View,"Decimal Format","","Decimal format preference service is unavailable."),
 ROUTE(Grouping,View,"Group Outputs in New Wallets","","Address grouping preference storage is unavailable."),ROUTE(HideEmpty,View,"Hide Empty Used Addresses","","Address view adapter is unavailable."),
 ROUTE(AppearanceSystem,View,"System Appearance","",""),ROUTE(AppearanceLight,View,"Light Appearance","",""),ROUTE(AppearanceDark,View,"Dark Appearance","",""),
 ROUTE(Camera,View,"Camera","","Camera adapter is unavailable."),ROUTE(SeparateWindows,View,"Separate Wallet Windows","","Separate-window document ownership service is unavailable."),
 ROUTE(HideAmounts,View,"Hide Amounts","Shift+H",""),ROUTE(LoadingLog,View,"Loading Log","",""),
 ROUTE(TransactionHex,View,"Transaction Hex","","Transaction representation adapter is unavailable."),ROUTE(Tray,View,"Tray","","Native tray adapter is unavailable."),
 ROUTE(LockCurrent,View,"Lock Current Wallet","L","Wallet privacy-lock adapter is unavailable."),ROUTE(LockAll,View,"Lock All Wallets","Shift+L","Wallet privacy-lock adapter is unavailable."),
 ROUTE(SearchCurrent,View,"Search Current Wallet","Shift+S","Wallet search service is unavailable."),ROUTE(SearchAll,View,"Search All Wallets","Shift+Alt+S","Wallet search service is unavailable."),
 ROUTE(Summaries,View,"Summaries","","Wallet summary adapter is unavailable."),
 ROUTE(Transactions,View,"Go To Transactions","1",""),ROUTE(Send,View,"Go To Send","2",""),ROUTE(Receive,View,"Go To Receive","3",""),
 ROUTE(Addresses,View,"Go To Addresses","4",""),ROUTE(UTXOs,View,"Go To UTXOs","5",""),ROUTE(Settings,View,"Go To Settings","6",""),
 ROUTE(Refresh,View,"Refresh","R","Refresh requires a daemon-wallet adapter."),
 ROUTE(SignMessage,Tools,"Sign / Verify Message","M","Message signing and verification adapter is unavailable."),
 ROUTE(SendMany,Tools,"Send to Many","","Batch recipient editor is unavailable."),ROUTE(Sweep,Tools,"Sweep Private Key","","Sweeping needs a verified unsigned draft provider and a separate compatible signer."),
 ROUTE(Contacts,Tools,"Payment-code Contacts","","Payment-code contacts require a qualified PaymentCodeContacts provider."),ROUTE(Server,Tools,"Switch Server","","Connection profile adapter is unavailable."),
 ROUTE(VerifyDownload,Tools,"Verify Download","","Local verification needs the build-pinned GnuPG 2.4.7 verifier."),ROUTE(Hardware,Tools,"Hardware Access Setup","","Hardware permission guidance adapter is unavailable."),
 ROUTE(PreventSleep,Tools,"Prevent Sleep","","Native sleep-inhibition adapter is unavailable."),ROUTE(Restart,Tools,"Restart with Network / Data-directory Profile","","GUI profile restart adapter is unavailable; no daemon is started or reconfigured."),
 ROUTE(Introduction,Help,"Introduction","","Introduction document adapter is unavailable."),ROUTE(Documentation,Help,"Documentation","","Documentation route is not connected; no external page is opened."),
 ROUTE(LogFile,Help,"Log File","","Native log-file adapter is unavailable."),ROUTE(Support,Help,"Support","","Support route is not connected; no external page is opened."),
 ROUTE(ReportIssue,Help,"Report Issue","","Issue-report adapter is unavailable."),ROUTE(About,Help,"About","","About document adapter is unavailable.")
#undef ROUTE
};
}
std::span<const CommandSpec> CommandInventory() { return inventory; }
const CommandSpec* Spec(Command id) { for(const auto& item:inventory)if(item.id==id)return &item;return nullptr; }
ImportCandidate ClassifyImportPath(std::string_view path)
{
    ImportCandidate result;if(path.empty() || path.size()>4096 || path.find('\0')!=std::string_view::npos)return result;
    result.source=path;const auto dot=path.find_last_of('.');if(dot==path.npos)return result;
    std::string ext(path.substr(dot));std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char ch){return char(ch>='A' && ch<='Z' ? ch+('a'-'A') : ch);});
    if(ext==".psbt")result.formats={ImportFormat::PSBT};
    else if(ext==".txn" || ext==".tx" || ext==".hex")result.formats={ImportFormat::RawTransaction};
    else if(ext==".desc")result.formats={ImportFormat::Descriptor};
    else if(ext==".json")result.formats={ImportFormat::PublicWallet,ImportFormat::RawTransaction};
    else if(ext==".txt")result.formats={ImportFormat::Descriptor,ImportFormat::PSBT,ImportFormat::RawTransaction};
    return result;
}
const char* FormatName(ImportFormat f)
{
    switch(f){case ImportFormat::PublicWallet:return "Public wallet JSON";case ImportFormat::Descriptor:return "Public descriptor";case ImportFormat::PSBT:return "PSBT";case ImportFormat::RawTransaction:return "Raw transaction";}return "Unsupported";
}
const char* StateName(ServiceState state)
{
    switch(state){case ServiceState::Unavailable:return "Unavailable";case ServiceState::Idle:return "Idle";case ServiceState::Loading:return "Loading";case ServiceState::Ready:return "Ready";case ServiceState::Offline:return "Offline";case ServiceState::Stale:return "Stale";case ServiceState::Error:return "Error";case ServiceState::Locked:return "Locked";}return "Unavailable";
}
std::string MissingDependency(Command command)
{
    if(const auto* spec=Spec(command);spec && *spec->dependency)return spec->dependency;
    switch(command){case Command::DocumentMetadata:return "Wallet metadata adapter is unavailable.";case Command::SaveDraft:return "Draft persistence adapter is unavailable.";case Command::ImportSource:return "Public file import adapter is unavailable.";
    case Command::SignTransaction:return "A transaction signer is unavailable. Approval authorizes no action by itself.";
    default:return "Command handler is unavailable.";}
}
}
