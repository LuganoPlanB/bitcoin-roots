// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "app/history_workspace.h"
namespace roots::gui::app {
std::string CoinKey(const Outpoint&);
std::optional<Outpoint> ParseCoinKey(std::string_view);
enum class CoinRule { Allowed, Frozen, Reserved, Unavailable };
// Observation-only seam for later policy ownership. No freeze/reservation action.
struct CoinPolicy { RequestContext context;uint64_t sequence{};std::map<std::string,CoinRule> rules; };
struct CoinRow {
    Outpoint outpoint;Money value;std::string address,label,script,reason;
    std::optional<int64_t> date;int64_t observed{};
    bool present{},current{},immature{},reused{},small_value{};
    std::optional<bool> eligible;
};
struct CoinGroup {std::string name;Money value;std::vector<std::string> keys;};
struct CoinSummary {
    size_t present{},selected{},unavailable_selected{},missing_selected{};
    std::optional<Money> total{Money::Satoshis(0)},selected_value{Money::Satoshis(0)},eligible{Money::Satoshis(0)},selected_eligible{Money::Satoshis(0)};
};
class CoinInventory {
    RequestContext context;std::string daemon_wallet;std::map<std::string,CoinRow> rows;
    uint64_t sequence{};bool current{},complete_inventory{};
public:
    CoinInventory(RequestContext c,std::string name):context(std::move(c)),daemon_wallet(std::move(name)){}
    // Exact scoped history only. Dated/absent rows retain values, never eligibility.
    bool Apply(const HistoryLedger&,uint64_t,bool chain_current,const CoinPolicy* =nullptr);
    void MarkDated();
    const auto& Rows() const{return rows;}
    const RequestContext& Context() const{return context;}
    uint64_t Sequence() const{return sequence;}
    bool Current() const{return current;}
    CoinSummary Summary(const std::set<std::string>&) const;
    std::vector<const CoinRow*> Ordered(bool by_value,bool descending,bool hidden=false) const;
    std::optional<std::vector<CoinGroup>> Chart() const;
};
std::optional<std::string> CoinCsv(const CoinInventory&,std::span<const CoinRow* const>,const std::map<std::string,std::string>& labels,bool hidden);
bool WriteCoinCsv(const std::string& path,const std::string& data,std::string& error);
}
