// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "app/commands.h"
#include "app/history_workspace.h"
#include "services/addresses.h"
#include <deque>

namespace roots::gui::app {
enum class SearchType { Any, Transaction, Address, Output };
struct WalletSearchQuery {
    std::string text;
    SearchType type{SearchType::Any};
    uint64_t document{}; // zero searches all open wallet tabs
    bool operator==(const WalletSearchQuery&) const=default;
};
struct WalletSearchHit {
    uint64_t document{},session{};
    NetworkIdentity network;
    SearchType type;
    std::string key,identifier,label,wallet_name;
    int64_t observed{};
    Freshness freshness{Freshness::Unknown};
    bool operator==(const WalletSearchHit&) const=default;
};
struct WalletSearchSource {
    uint64_t document{};
    RefreshIdentity identity;
    uint64_t history_sequence{},address_sequence{};
    std::optional<int64_t> history_observed,address_observed;
    Freshness history_freshness{},address_freshness{};
    bool chain_current{};
    bool operator==(const WalletSearchSource&) const=default;
};
struct WalletSearchResult {
    std::vector<WalletSearchHit> hits;
    std::vector<WalletSearchSource> sources;
    size_t wallets{},observed_wallets{};
    bool limited{},valid{};
    bool operator==(const WalletSearchResult&) const=default;
};
bool ToolWallet(const Document&);
bool SearchSourcesRetained(const WalletSearchResult&,const DocumentRegistry&,const RefreshService&,const AddressService*,LockEpoch);
using WalletLabelReader=std::function<std::optional<storage::Metadata>(const WalletBinding&)>;
WalletSearchResult SearchWallets(const DocumentRegistry&,const RefreshService&,const AddressService*,
    LockEpoch,const WalletSearchQuery&,const WalletLabelReader& = {});
const char* SearchTypeName(SearchType);
const char* ObservationSource(Freshness);
std::string WalletSearchCsv(const WalletSearchResult&,bool show_wallet);

struct WalletBalanceSummary {
    uint64_t document{};
    std::string wallet_name;
    NetworkIdentity network;
    std::array<std::optional<Money>,4> categories; // trusted, pending, immature, nonmempool
    std::optional<int64_t> observed;
    Freshness freshness{Freshness::Unknown};
    bool scanning{};
};
std::vector<WalletBalanceSummary> WalletBalances(const DocumentRegistry&,const RefreshService&,LockEpoch);
struct NodeDetails {
    std::string chain,genesis,tip;
    int64_t version{},height{};
    bool initial_download{},pruned{};
    Freshness freshness{Freshness::Unknown};
    std::optional<int64_t> observed;
};
std::optional<NodeDetails> AppliedNodeDetails(const ConnectionService&,const RefreshService&);

enum class ActivityKind { Connection, Chain, Wallet, History, Background };
struct ActivityRecord {
    ActivityKind kind;
    ServiceState state;
    std::optional<unsigned> progress;
    int64_t observed{};
};
class ActivityJournal {
    struct Status {
        ServiceState state;
        std::optional<unsigned> progress;
        bool operator==(const Status&) const=default;
    };
    std::array<std::optional<Status>,5> latest;
    std::deque<ActivityRecord> records;
    uint64_t revision{};
public:
    void Observe(const HostObservations&,int64_t now);
    const auto& Records() const{return records;}
    uint64_t Revision() const{return revision;}
    void Clear(){if(!records.empty())++revision;latest={};records.clear();}
};
const char* ActivityKindName(ActivityKind);
std::string ActivityCsv(const ActivityJournal&);
}
