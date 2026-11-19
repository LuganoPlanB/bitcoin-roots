// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "services/capabilities.h"
#include "domain/model.h"
#include "services/history.h"
#include <array>

namespace roots::gui {
struct RefreshIdentity {
    RequestContext context;
    ServerCandidate profile;
    uint64_t connection_session{};
    bool operator==(const RefreshIdentity&) const = default;
};
struct NodeObservation { int64_t height{}; std::string tip; bool initial_download{}, pruned{}; };
struct WalletObservation {
    // Exact RPC categories, not a spendable/complete-history claim.
    Money trusted{Money::Satoshis(0)}, pending{Money::Satoshis(0)}, immature{Money::Satoshis(0)};
    std::optional<Money> nonmempool;
    bool scanning{};
    std::optional<Money> used;
};
struct HistoryDelta { rpc::Json transactions, removed; std::string lastblock;std::optional<HistoryBatch> normalized; };
struct RefreshRequest {
    RefreshIdentity identity;
    uint64_t generation{}, sequence{};
    bool wallet_supported{}, balances_supported{}, history_supported{}, fetch_history{};
    std::optional<std::string> cursor;
    bool normalize_history{},inventory_supported{};
    std::vector<std::string> revisit;
    bool decoded_supported{},locks_supported{};
    HistoryLimits history_limits;
};
struct RefreshReply {
    RefreshIdentity identity;
    uint64_t generation{}, sequence{};
    int64_t observed_unix_ms{};
    std::optional<NodeObservation> node;
    std::optional<WalletObservation> wallet;
    std::optional<HistoryDelta> history;
    std::array<rpc::Error, 3> errors{}; // node, wallet, history
};
RefreshReply FetchRefresh(rpc::Transport&, const RefreshRequest&, std::stop_token);
struct RefreshView {
    const RefreshIdentity* identity{};
    const NodeObservation* node{};
    const WalletObservation* wallet{};
    const HistoryDelta* history{};
    const HistoryLedger* ledger{};
    std::array<Freshness, 3> freshness{Freshness::Unknown, Freshness::Unknown, Freshness::Unknown};
    std::array<std::optional<int64_t>, 3> observed;
    std::array<rpc::Error, 3> errors{};
    bool busy{}, offline{}, locked{}, chain_current{};
    size_t scan_budget{256};
    bool scan_blocked{};
    uint64_t generation{},history_sequence{};
    int64_t next_due_ms{};
};
// One worker/completion, eight full-identity caches and one unacknowledged
// history delta per cache. All views and mutations belong to the owner thread.
class RefreshService {
public:
    using Fetch = std::function<RefreshReply(const RefreshRequest&, std::stop_token)>;
    explicit RefreshService(Fetch);
    ~RefreshService();
    bool Bind(RefreshIdentity, const BackendCapabilities&, bool locked, int64_t monotonic_ms);
    void Unbind();
    void SetOffline(bool, int64_t monotonic_ms);
    void Tick(int64_t monotonic_ms);
    bool RequestNow(int64_t monotonic_ms);
    // Explicitly re-read the SAME unacknowledged range with a larger budget.
    // Never advances its cursor; hard bounds remain actionable, not retries.
    bool ScanEarlier(const RefreshIdentity&, int64_t monotonic_ms);
    void Cancel(int64_t monotonic_ms);
    void PrivacyLock(); // cancellation + no retained sensitive observations
    bool AcknowledgeHistory(const RefreshIdentity&, uint64_t sequence, std::string_view lastblock);
    bool SetHistoryPolicy(const RefreshIdentity&,HistoryPolicy); // explicit local freeze/reservation snapshot
    RefreshView View() const;
    std::vector<RefreshView> HistoryViews() const; // borrowed owner-thread cache views, no transport
    std::vector<RefreshView> ObservationViews() const;
    bool Busy() const { return worker.joinable(); }
    bool Offline() const { return offline; }
    uint64_t PendingHistorySequence() const;
private:
    struct Slot {
        RefreshIdentity identity;
        bool wallet_supported{}, balances_supported{}, history_supported{};
        bool normalize_history{},inventory_supported{},decoded_supported{},locks_supported{};
        std::optional<NodeObservation> node;
        std::optional<WalletObservation> wallet;
        std::optional<HistoryDelta> history;
        std::optional<HistoryLedger> ledger;
        std::optional<std::string> cursor;
        uint64_t history_sequence{};
        HistoryLimits history_limits;
        size_t scan_previous_rows{};bool scan_attempt{},scan_blocked{};
        std::array<std::optional<int64_t>, 3> observed;
        std::array<rpc::Error, 3> errors{};
        std::array<bool, 3> current{};
        unsigned failures{};
        int64_t next_due{};
    };
    bool Owner() const { return owner == std::this_thread::get_id(); }
    void Fence();
    void Poll(int64_t);
    const std::thread::id owner{std::this_thread::get_id()};
    Fetch fetch;
    std::vector<Slot> slots;
    std::optional<size_t> selected;
    uint64_t generation{1}, sequence{};
    bool offline{}, locked{}, exhausted{};
    std::mutex mutex;
    std::optional<RefreshReply> completion;
    std::jthread worker; // destroyed/joined before captured state above
};
int64_t RefreshMonotonicMs();
std::string ObservationDate(std::optional<int64_t>);
}
