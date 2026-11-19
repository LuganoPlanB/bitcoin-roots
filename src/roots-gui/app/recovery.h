// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "app/document_registry.h"
#include "adapters/storage/metadata.h"
#include "adapters/storage/metadata_archive.h"
#include "services/session_gate.h"
namespace roots::gui::app {
bool MatchesSendDraft(const Document&,const ImportedDocument&); // exact ordered draft, independently saved inputs and change
struct PublicSession {
    DocumentRegistry documents;
    std::vector<Document> recoverable;
    bool hide_amounts{};
    int unit{};
    std::optional<Document> cleared_send;
};
std::optional<std::string> EncodePublicSession(const PublicSession&);
std::optional<PublicSession> DecodePublicSession(std::string_view);
struct MetadataRestore {
    storage::WalletMetadataArchive archive;
    uint64_t destination_revision{};
    LockEpoch epoch;
};
// Owner-thread local persistence. There is deliberately no RPC/command port.
class RecoverySession {
    const std::thread::id owner{std::this_thread::get_id()};
    std::filesystem::path path;
    std::unique_ptr<storage::MetadataStore> store;
    storage::RecoveryRecord record;
    PublicSession saved;
    bool pending{},started{},suspended{};
    bool hidden{};int unit{};
    bool protected_store{};
    bool protection_retry{};
    bool unlock_conflict{},conversion_conflict{};
    uint64_t locked_revision{};
    SessionGate gate;
    std::vector<unsigned char> locked_documents;
    std::string error;
    bool Open();
    bool OpenBroadcastJournalForRead();
    bool LoadOpen();
    bool Commit(PublicSession&,bool enabled);
    PublicSession Snapshot(const DocumentRegistry&) const;
    bool SaveDocuments(DocumentRegistry&, const Document* unchanged_draft);
public:
    explicit RecoverySession(std::filesystem::path p):path(std::move(p)){}
    bool Start(); // reads only an existing store, never restores documents
    bool Enabled() const{return record.enabled && !suspended;}
    bool Pending() const{return pending;}
    bool Locked() const{return gate.Locked();}
    bool Protected() const{return protected_store;}
    bool UnlockConflict() const{return unlock_conflict;}
    bool ConversionConflict() const{return conversion_conflict;}
    SessionGate& Gate(){return gate;}
    const SessionGate& Gate() const{return gate;}
    bool HasRecovery() const{return !saved.recoverable.empty();}
    bool RetainedWorkspace() const{return suspended && record.enabled;}
    bool Hidden() const{return saved.hide_amounts;}
    int Unit() const{return saved.unit;}
    void SetPresentation(bool hide,int value){if(!pending){hidden=hide;unit=value;}}
    const std::string& Error() const{return error;}
    storage::Result<storage::Workspace> LoadWorkspacePreferences();
    storage::Result<uint64_t> SaveWorkspacePreferences(const storage::Workspace&);
    bool Enable(DocumentRegistry&);
    bool Disable(); // atomically forgets public snapshots
    bool Restore(DocumentRegistry&); // deliberate offline public-state restoration
    bool StartEmpty(); // deliberate choice; leaves the stored candidate intact
    bool Retry(); // reread after a reported failure; never silently overwrite a conflict
    bool Protect(DocumentRegistry&,std::string_view password); // explicit protection + opt-in recovery
    bool Lock(DocumentRegistry&);
    bool Unlock(std::string_view password,DocumentRegistry&,bool choose_latest=false,bool preserve_other_copy=false);
    bool CanAdmitPublicDocuments(const DocumentRegistry&) const;
    bool Checkpoint(DocumentRegistry&);
    bool BroadcastJournalAvailable() const;
    storage::Result<bool> HasBroadcastAttempt(const NetworkIdentity&,const Hash256&);
    storage::Result<storage::Done> RecordBroadcastAttempt(const storage::BroadcastAttempt&);
    storage::Result<std::optional<storage::BroadcastRecord>> LoadBroadcastAttempt(const NetworkIdentity&,const Hash256&);
    storage::Result<std::vector<storage::BroadcastRecord>> RecentBroadcastAttempts();
    storage::Result<std::shared_ptr<storage::FileGuard>> BroadcastLease();
    storage::Result<uint64_t> BeginBroadcastAttempt(const storage::BroadcastRecord&,std::optional<uint64_t> expected_serial);
    storage::Result<storage::Done> FinishBroadcastAttempt(const storage::BroadcastAttempt&,uint64_t serial,
        storage::BroadcastOutcome,int64_t observed_ms);
    uint64_t CreateFunded(DocumentRegistry&, ImportedDocument, std::string& error);
    bool Close(DocumentRegistry&,std::span<const uint64_t>);
    bool Clear(DocumentRegistry&,uint64_t);
    bool ClearSend(DocumentRegistry&, uint64_t, DraftRevision);
    bool HasClearedSend() const { return saved.cleared_send.has_value(); }
    bool RecoverSend(DocumentRegistry&);
    bool RecoverLast(DocumentRegistry&);
    storage::Result<storage::Metadata> HistoryMetadata(const storage::Scope&);
    void RestoreWalletPresentation(Document&);
    storage::Result<uint64_t> SaveWalletPresentation(const WalletBinding&,std::string name,int icon);
    storage::Result<uint64_t> DeleteWalletMetadata(const WalletBinding&);
    storage::Result<storage::Done> ExportWalletMetadata(const storage::Scope&,std::string display_name,
        const std::filesystem::path&,std::string_view password);
    storage::Result<MetadataRestore> InspectWalletMetadata(const storage::Scope&,
        const std::filesystem::path&,std::string_view password);
    storage::Result<uint64_t> ApplyWalletMetadata(const storage::Scope&,const MetadataRestore&);
    storage::Result<std::vector<storage::Scope>> RecentWallets();
    storage::Result<storage::Done> RememberWallet(const storage::Scope&);
    storage::Result<std::shared_ptr<storage::FileGuard>> RescanLease();
    storage::Result<std::optional<PendingRescan>> PendingScan(const storage::Scope&);
    storage::Result<uint64_t> BeginScan(const storage::Scope&, ScanRange);
    storage::Result<storage::Done> FinishScan(const storage::Scope&, uint64_t ticket);
    storage::Result<storage::ReceivePool> ReceivePool(const storage::Scope&);
    storage::Result<uint64_t> BeginReceive(const storage::Scope&, bool reserve);
    storage::Result<storage::Done> FinishReceive(const storage::Scope&, uint64_t ticket, const std::optional<ReceiveTarget>& = {});
    storage::Result<ReceiveTarget> ConsumeReceive(const storage::Scope&);
    storage::Result<uint64_t> SaveReceiveLabel(const storage::Scope&, std::string_view address, std::string label, uint64_t expected_revision);
    storage::Result<uint64_t> SaveCoinFreezes(const storage::Scope&,const std::vector<storage::Target>&,uint64_t expected_revision);
    storage::Result<uint64_t> SaveOutputLabel(const storage::Scope&,const Outpoint&,std::string,uint64_t expected_revision);
    storage::Result<uint64_t> SaveHistoryLabel(const storage::Scope&,const Hash256&,std::string,uint64_t expected_revision);
};
}
