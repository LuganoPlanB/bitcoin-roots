// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "app/document_registry.h"
namespace roots::gui { class ConnectionService; class RefreshService; class AddressService; class WalletLifecycle; class ReceiveService; class RescanService; class TransactionDocuments; }
namespace roots::gui::app {
class RecoverySession;
enum class Menu { File,View,Tools,Help };
enum class Command {
 NewWallet,OpenWallet,ImportWallet,ExportWallet,TransactionFile,TransactionId,TransactionText,TransactionQR,SaveTransaction,ShowQR,RenameWallet,DeleteWallet,Preferences,CloseTab,Quit,
 Units,Decimal,Grouping,HideEmpty,AppearanceSystem,AppearanceLight,AppearanceDark,Camera,SeparateWindows,HideAmounts,LoadingLog,TransactionHex,Tray,LockCurrent,LockAll,SearchCurrent,SearchAll,Summaries,
 Transactions,Send,Receive,Addresses,UTXOs,Settings,Refresh,
 SignMessage,SendMany,Sweep,Contacts,Server,VerifyDownload,Hardware,PreventSleep,Restart,
 Introduction,Documentation,LogFile,Support,ReportIssue,About,Component,Primary,DocumentMetadata,SaveDraft,ImportSource,Recovery
};
struct CommandSpec { Command id;Menu menu;const char* label;const char* shortcut;const char* dependency; };
std::span<const CommandSpec> CommandInventory();
const CommandSpec* Spec(Command);
enum class ImportFormat { PublicWallet,Descriptor,PSBT,RawTransaction };
struct ImportCandidate { std::string source;std::vector<ImportFormat> formats; };
ImportCandidate ClassifyImportPath(std::string_view); // Hints only; service must validate contents.
const char* FormatName(ImportFormat);
struct Request { Command command;uint64_t document{};Section section{Section::Transactions};std::optional<ui::components::Command> component;std::string value;int option{}; };
struct Receipt { bool accepted{};std::string reason; };
struct Availability {bool available{};std::string reason;};
enum class ServiceState { Unavailable,Idle,Loading,Ready,Offline,Stale,Error,Locked };
const char* StateName(ServiceState);
struct ServiceObservation {ServiceState state{ServiceState::Unavailable};std::string detail;};
struct ActivityItem {uint64_t id{};std::string title,detail;ServiceState state{ServiceState::Loading};std::optional<unsigned> progress_per_mille;};
// Public operational observations only: no raw RPC payload, amounts, addresses,
// wallet secrets or financial log data. UI also conceals free text when hidden.
struct HostObservations {
    ServiceObservation devices,proxy,updates,network,connection,activity;
    std::optional<int64_t> backend_height;
    std::string last_success,error;
    std::vector<ActivityItem> tasks;
    std::vector<std::string> log;
    std::array<ServiceObservation,3> screen_observations;
};
std::string MissingDependency(Command);
enum class FeeDraftKind { ReplaceByFee, ChildPaysForParent };
struct FeeDraftRequest { RequestContext context;Hash256 txid;FeeDraftKind kind;uint64_t generation{},sequence{}; };
struct ExplorerRequest { RequestContext context;Hash256 txid;bool permission{};uint64_t generation{},sequence{}; };
struct Requests {
    virtual ~Requests()=default;
    virtual Receipt Submit(const Request&)=0;
    virtual Availability CanSubmit(const Request& request) const {return {false,MissingDependency(request.command)};}
    virtual HostObservations Observe() const {return {};}
    // These typed boundaries confer no RPC/transaction execution authority.
    virtual Receipt FeeDraft(const FeeDraftRequest&) {return {false,"Fee drafts are unavailable until the transaction draft service is connected. No transaction action was performed."};}
    virtual Receipt OpenExplorer(const ExplorerRequest&) {return {false,"External explorer is unavailable for this network/host. No external request was made."};}
    virtual ConnectionService* Connection() {return nullptr;}
    virtual WalletLifecycle* Wallets() {return nullptr;}
    virtual TransactionDocuments* LocalDocuments() const {return nullptr;}
    virtual RescanService* Rescanning() {return nullptr;}
    virtual ReceiveService* Receiving() {return nullptr;}
    virtual AddressService* Addresses() {return nullptr;}
    virtual RefreshService* Refresh() {return nullptr;}
    virtual RecoverySession* Recovery() {return nullptr;}
};
}
