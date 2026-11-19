// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "services/capabilities.h"
#include "domain/model.h"
#include <map>

namespace roots::gui {
struct HistoryAmount {
    Money value{Money::Satoshis(0)};
    std::string lexeme;
    bool quoted{};
};
enum class HistoryState { Confirmed, Pending, Immature, Orphan, Replaced, Conflicted, Abandoned };
struct HistoryDetail {
    uint32_t vout{};
    std::string category,address,label;
    HistoryAmount amount;
    std::optional<HistoryAmount> repeated_fee; // never added per output
};
struct HistoryIO {std::string kind,reference,destination;std::optional<Money> value;std::string script;};
struct HistoryTransaction {
    Hash256 txid;
    HistoryAmount reported_amount;
    std::optional<HistoryAmount> fee;
    Money wallet_delta{Money::Satoshis(0)}; // includes fee once and immature reward
    int64_t time{},confirmations{};
    std::optional<int64_t> block_height;
    std::optional<int64_t> conflict_height;
    std::optional<Hash256> block_hash,replaced_by,replaces;
    std::vector<Hash256> conflicts;
    std::vector<Hash256> mempool_conflicts;
    std::vector<HistoryDetail> details;
    std::vector<HistoryIO> io;bool decoded_available{}; // public references/values only; no executable transaction bytes
    HistoryState state{HistoryState::Pending};
    bool generated{},removed_event{};
    std::string previous_hex; // bounded public bytes already returned with gettransaction
    std::optional<Money> running_balance; // total incl. immature; Unknown unless reconciled
    std::optional<Hash256> wire_witness; // observed wtxid; original bytes can be fetched separately
};
struct HistoryCoin {
    Outpoint outpoint;
    Money amount;
    int64_t confirmations{};
    bool safe{},solvable{},reused{};
    std::string script;
    std::string address,label;std::optional<bool> daemon_spendable; // observed, never inferred custody
    std::string descriptor,redeem_script,witness_script;
};
struct HistoryBalances {
    Money trusted{Money::Satoshis(0)},pending{Money::Satoshis(0)},immature{Money::Satoshis(0)},nonmempool{Money::Satoshis(0)};
    std::optional<Money> used;
    bool operator==(const HistoryBalances&) const = default;
};
// Local freeze/reservation evidence is a separate axis from daemon safety or
// key custody. An absent policy never silently means no excluded coins.
struct HistoryPolicy { bool known{};std::vector<Outpoint> frozen,reserved; };
struct HistoryTotals {
    HistoryBalances rpc;
    std::optional<Money> total,confirmed,mempool,rpc_fundable,spendable,frozen,reserved;
    bool has_rpc{},inventory_reconciled{};
};
struct HistoryLimits { size_t transactions{256},entries{8192},coins{8192},ledger{4096}; };
struct HistoryBatch {
    RequestContext context;
    std::string daemon_wallet,lastblock;
    std::vector<HistoryTransaction> transactions;
    std::vector<HistoryCoin> coins;
    HistoryBalances balances;
    int64_t height{};
    bool full_query{},limited{},scanning{},pruned{},stable{},inventory_available{};
    std::vector<Outpoint> daemon_locked;bool locks_available{};
};
struct HistoryFetch {std::optional<HistoryBatch> batch;rpc::Error error{rpc::Error::None};};
// Called only by the existing read-only refresh worker, after identity checks.
// It owns no daemon process. Bounded previous-transaction bytes are observations, never executable requests.
HistoryFetch FetchHistory(rpc::Transport&,const ServerCandidate&,const RequestContext&,
    const rpc::Json& since,const HistoryBalances& before,std::string_view tip,
    bool full_query,bool scanning,bool pruned,bool inventory_supported,
    const std::vector<std::string>& revisit,std::stop_token,
    std::chrono::steady_clock::time_point deadline,HistoryLimits = {},bool decoded_supported=false,bool locks_supported=false);
std::optional<HistoryTransaction> NormalizeHistoryTransaction(const rpc::Json&,std::string_view txid,size_t max_details=4096);

// Owner-thread transactional adoption. RefreshService supplies the full bound
// profile/session identity and rejects cancelled/late replies before Apply.
class HistoryLedger {
    RequestContext context;
    std::string daemon_wallet;
    std::map<std::string,HistoryTransaction> rows;
    HistoryTotals totals;
    HistoryPolicy policy;
    uint64_t sequence{},revision{};
    int64_t observed{};
    std::string tip;
    bool full_seen{},complete{},limited{},scanning{},stable{},pruned{};
    HistoryLimits limits;
public:
    HistoryLedger(RequestContext,std::string,HistoryLimits = {});
    bool Apply(const HistoryBatch&,uint64_t sequence,int64_t observed_unix_ms);
    bool SetPolicy(HistoryPolicy);
    std::vector<std::string> Revisit() const;
    const std::map<std::string,HistoryTransaction>& Rows() const{return rows;}
    const RequestContext& Context() const{return context;}
    const std::string& DaemonWallet() const{return daemon_wallet;}
    const std::vector<HistoryCoin>& Coins() const{return coins;}
    const std::vector<Outpoint>& DaemonLocked() const{return daemon_locked;}
    bool LocksAvailable() const{return locks_available && stable && !limited && !scanning;}
    bool InventoryAvailable() const{return inventory_available && stable && !limited && !scanning;}
    const HistoryTotals& Totals() const{return totals;}
    bool Complete() const{return complete;}
    bool Limited() const{return limited;}
    bool Scanning() const{return scanning;}
    bool Pruned() const{return pruned;}
    bool Stable() const{return stable;}
    const std::string& Tip() const{return tip;}
    int64_t Observed() const{return observed;}
    uint64_t Revision() const{return revision;}
private:
    std::vector<HistoryCoin> coins;
    bool inventory_available{},locks_available{};std::vector<Outpoint> daemon_locked;
    bool Recalculate();
};
}
