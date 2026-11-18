// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "domain/model.h"
#include "adapters/protocol/public_wallet.h"
#include "services/capabilities.h"

namespace roots::gui {

bool ValidDaemonWalletName(std::string_view);
std::string NewWalletNameError(std::string_view);
bool CanListWallets(const BackendCapabilities&);
bool CanLoadWallet(const BackendCapabilities&);
bool CanCreateWallet(const BackendCapabilities&);
bool CanImportWallet(const BackendCapabilities&);
bool CanRestoreWallet(const BackendCapabilities&);
bool CanBackupWallet(const BackendCapabilities&);
bool CanReadWalletPolicy(const BackendCapabilities&);
bool ValidDaemonWalletPath(std::string_view);

struct DaemonWalletEntry {
    std::string name;
    bool loaded{};
    bool operator==(const DaemonWalletEntry&) const = default;
};

struct PolicyRange {
    uint32_t first{}, last{}, next{};
    bool operator==(const PolicyRange&) const = default;
};
struct SavedPolicyBranch {
    protocol::PublicDescriptor descriptor;
    bool active{}, classified{};
    std::optional<PolicyRange> range;
};
struct SavedWalletPolicy {
    std::string chain;
    std::vector<SavedPolicyBranch> branches;
    size_t unsupported{};
};
std::optional<SavedWalletPolicy> ValidateWalletPolicy(const rpc::Json&, std::string_view exact_wallet, std::string_view chain);

enum class WalletAction { List, Open, Load, Create, Import, Restore, Backup, ReadPolicy };
struct WalletTransfer {
    std::optional<protocol::PublicWalletPreview> preview;
    std::string daemon_path;
};
struct WalletOperation {
    WalletAction action{WalletAction::List};
    std::string name;
    BackendCapabilities backend;
    uint64_t session{}, revision{}, document{};
    LockEpoch epoch;
    WalletTransfer transfer;
};
struct WalletResult {
    std::vector<DaemonWalletEntry> entries;
    bool directory_verified{};
    std::optional<BackendCapabilities> wallet;
    bool mutation_dispatched{}, mutation_uncertain{}, backup_written{};
    std::string error;
    std::optional<SavedWalletPolicy> policy;
};

WalletResult RunWalletOperation(rpc::Transport&, const WalletOperation&, std::stop_token);

// One bounded worker. Keep acceptance and application on the owning thread.
// Cancel retires the completion even when the daemon has already acted.
class WalletLifecycle {
public:
    using Run = std::function<WalletResult(const WalletOperation&, std::stop_token)>;
    explicit WalletLifecycle(Run run) : run(std::move(run)) {}
    ~WalletLifecycle();
    bool Busy() const { return worker.joinable(); }
    bool Start(WalletAction, std::string name, const ConnectionService&, LockEpoch, uint64_t document, WalletTransfer = {});
    void Cancel();
    bool Uncertain(const BackendCapabilities&, std::string_view name) const;
    bool BackupUncertain(const BackendCapabilities&, std::string_view name, std::string_view path) const;
    std::optional<std::pair<WalletAction, WalletResult>> Poll(const ConnectionService&, LockEpoch, uint64_t document);
private:
    struct Pending {
        WalletAction action;
        uint64_t session, revision, document;
        LockEpoch epoch;
        std::string mutation_key;
        std::string name;
    };
    const std::thread::id owner{std::this_thread::get_id()};
    Run run;
    std::set<std::string> uncertain;
    std::optional<Pending> pending;
    bool retired{};
    std::mutex mutex;
    std::optional<WalletResult> completed;
    std::jthread worker;
};

}
