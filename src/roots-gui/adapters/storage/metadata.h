// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "application/preferences.h"
#include "services/capabilities.h"
#include "domain/model.h"
#include "domain/receive.h"
#include "domain/rescan.h"
#include <filesystem>
#include <memory>
#include <thread>
#include <type_traits>
#include <span>

struct sqlite3;
namespace roots::gui::storage {
enum class Error { None, Invalid, Conflict, Busy, Full, Corrupt, Schema, Io, Resource, Timeout, WrongThread, Unsupported, NotFound, Authentication, ConversionConflict, ConversionBackupConflict };
enum class ConversionResolution { RequireIdentical, PreserveOtherCopy };
const char* ErrorName(Error);
template<class T> struct Result {
    std::optional<T> value;
    Error error{Error::None};
    explicit operator bool() const { return value.has_value() && error==Error::None; }
};
struct Done {};
// A durable backend ID, not a transient RPC session or an invented account.
struct Scope { std::string backend; NetworkIdentity network; std::string daemon_wallet; bool operator==(const Scope&) const = default; };
enum class TargetKind { Address, Transaction, Output };
struct Target { TargetKind kind; std::string key; bool operator==(const Target&) const = default; };
struct Label { Target target; std::string text; bool operator==(const Label&) const = default; };
struct TableLayout { std::string table, sort_column; bool descending{}; bool operator==(const TableLayout&) const = default; };
struct ColumnWidth { std::string table, column; int width{}; bool operator==(const ColumnWidth&) const = default; };
// Public editor fields only. No opaque command, approval, key, PSBT or seed blob.
// Recovery, selected/excluded coins and document orchestration are later work.
struct PublicDraft {
    std::string document;
    DraftRevision revision;
    std::string note;
    std::optional<Money> amount;
    bool operator==(const PublicDraft&) const = default;
};
struct Metadata {
    uint64_t revision{};
    std::string display_name;
    std::vector<Label> labels;
    std::vector<TableLayout> layouts;
    std::vector<ColumnWidth> widths;
    std::vector<Target> freezes;
    std::vector<PublicDraft> drafts;
    std::optional<int> display_icon; // absent in pre-icon stores
    bool operator==(const Metadata&) const = default;
};
bool ValidMetadata(const Scope&, const Metadata&);
struct Workspace {
    uint64_t revision{};
    PublicPreferences preferences;
    bool restore_documents{}; // opt-in only
    int appearance{}, unit{};
    bool operator==(const Workspace&) const = default;
};
struct Profile { std::string id; uint64_t revision{}; ServerCandidate candidate; bool operator==(const Profile&) const = default; };
struct CachedObservation { Provenance provenance; Freshness observed_freshness; std::optional<Money> amount; };
struct OpenOptions { int busy_ms{100}; int max_pages{65536}; };
// A bounded public-document codec owns the contents. This record is never a
// command queue; restoring it grants no RPC capability or transaction approval.
struct RecoveryRecord { uint64_t revision{}; bool enabled{}; std::string public_documents; };
// An exclusion record, never a command or an authorization to replay.
struct BroadcastAttempt {
    NetworkIdentity network;
    Hash256 txid, wtxid, wire_digest;
    int64_t observed_ms{};
};
enum class BroadcastOutcome { Pending, Unsent, Rejected, Acknowledged, Uncertain };
struct BroadcastRecord {
    BroadcastAttempt identity;
    std::string wire_hex;
    uint64_t serial{};
    int64_t attempted_ms{};
    BroadcastOutcome outcome{BroadcastOutcome::Pending};
    std::optional<int64_t> outcome_ms;
    std::string backend, wallet;
};
struct ReceivePool {
    static constexpr size_t MAX_RESERVED = 8, MAX_RECORDS = 4096;
    uint64_t pending{};
    size_t available{}, retained{};
};
class ProtectedFile;class FileGuard;
// Synchronous owner-thread adapter for an explicitly supplied private local
// file. A worker must own its connection; no implicit files or RPC operations.
class MetadataStore {
public:
    static Result<std::unique_ptr<MetadataStore>> Open(const std::filesystem::path&, OpenOptions = {});
    static Result<std::unique_ptr<MetadataStore>> OpenProtected(const std::filesystem::path& plain_path,std::string_view password,OpenOptions = {},ConversionResolution = ConversionResolution::RequireIdentical);
    Result<std::unique_ptr<MetadataStore>> Protect(std::string_view password); // complete metadata migration, not just drafts
    bool Protected() const{return bool(protection);}
    Result<std::vector<unsigned char>> SealMemory(std::string_view);
    Result<std::string> UnsealMemory(std::span<const unsigned char>);
    ~MetadataStore();
    MetadataStore(const MetadataStore&)=delete;
    MetadataStore& operator=(const MetadataStore&)=delete;
    Result<Metadata> Load(const Scope&);
    Result<uint64_t> Save(const Scope&, const Metadata&); // CAS on Metadata.revision
    Result<Workspace> LoadWorkspace();
    Result<uint64_t> SaveWorkspace(const Workspace&);
    Result<RecoveryRecord> LoadRecovery();
    Result<uint64_t> SaveRecovery(const RecoveryRecord&); // atomic CAS, disabled => no documents
    Result<bool> HasBroadcastAttempt(const NetworkIdentity&,const Hash256& txid);
    Result<Done> RecordBroadcastAttempt(const BroadcastAttempt&);
    Result<std::optional<BroadcastRecord>> LoadBroadcastAttempt(const NetworkIdentity&,const Hash256&);
    Result<std::vector<BroadcastRecord>> RecentBroadcastAttempts();
    Result<uint64_t> BeginBroadcastAttempt(const BroadcastRecord&,std::optional<uint64_t> expected_serial);
    Result<Done> FinishBroadcastAttempt(const BroadcastAttempt&,uint64_t serial,BroadcastOutcome,int64_t observed_ms);
    Result<std::vector<Scope>> LoadRecentWallets();
    Result<Done> RememberWallet(const Scope&); // merge recency under the writer transaction
    Result<ReceivePool> LoadReceivePool(const Scope&);
    Result<uint64_t> BeginReceive(const Scope&, bool reserve);
    Result<Done> FinishReceive(const Scope&, uint64_t ticket, const std::optional<ReceiveTarget>&);
    Result<ReceiveTarget> ConsumeReceive(const Scope&);
    Result<std::optional<PendingRescan>> LoadRescan(const Scope&);
    Result<uint64_t> BeginRescan(const Scope&, ScanRange);
    Result<Done> FinishRescan(const Scope&, uint64_t ticket);
    Result<Profile> LoadProfile(std::string_view id);
    Result<uint64_t> SaveProfile(const Profile&); // secure-store references only
    Result<Done> SaveObservation(const Scope&, std::string_view key, const Provenance&, std::optional<Money>);
    Result<CachedObservation> LoadObservation(const Scope&, std::string_view key);
private:
    MetadataStore()=default;
    template<class F> auto Run(F&&) -> Result<std::invoke_result_t<F>>;
    template<class F> auto Write(F&&) -> Result<std::invoke_result_t<F>>;
    sqlite3* db{};
    std::filesystem::path file_path;
    std::unique_ptr<FileGuard> file_guard;
    std::unique_ptr<ProtectedFile> protection;
    bool migration_blocked{};
    const std::thread::id owner{std::this_thread::get_id()};
};
}
