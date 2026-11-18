// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "services/refresh.h"
namespace roots::gui {
struct AddressDescriptor {
    std::string id,descriptor,reason;
    bool internal{},ranged{},classified{};
    uint32_t first{},last{},next{},window_first{},window_last{};
};
struct DerivedAddress {
    std::string key,descriptor_id,address,script,label,path;
    uint32_t index{};bool internal{},lookahead{};
};
struct AddressSnapshot {
    RequestContext context;std::string wallet,tip;
    std::vector<AddressDescriptor> descriptors;
    std::vector<DerivedAddress> rows;
    bool stable{};size_t unmapped{};
};
struct AddressFetch {std::optional<AddressSnapshot> snapshot;rpc::Error error{rpc::Error::None};};
struct AddressLimits {size_t descriptors{16},per_descriptor{8},addresses{128};};
AddressFetch FetchAddresses(rpc::Transport&,const RefreshIdentity&,std::stop_token,AddressLimits={},const std::vector<std::string>& observed={});
enum class AddressUse { Unknown,Observed,NoWalletKnownUse };
enum class OutputState { Unknown,Unspent,Spent,PendingSpend,Inactive };
struct AddressOutput {Outpoint outpoint;std::optional<Money> value;OutputState state{OutputState::Unknown};};
struct AddressRow {DerivedAddress derived;bool current{},history_complete{};std::string issue;AddressUse use{AddressUse::Unknown};std::optional<Money> value;std::optional<size_t> transactions;std::vector<AddressOutput> outputs;};
// Exact script/outpoint association; missing prevouts/coverage stay Unknown.
std::vector<AddressRow> ReconcileAddresses(const AddressSnapshot&,const HistoryLedger*,bool current);
const char* OutputStateName(OutputState);
struct AddressView {
    const RefreshIdentity* identity{};const AddressSnapshot* snapshot{};
    std::optional<int64_t> observed;rpc::Error error{rpc::Error::None};
    uint64_t generation{},sequence{};bool busy{},offline{},current{};
};
// Independent read-only owner-thread inventory worker; eight full-identity
// caches, one completion, bounded batches. No allocation/import/rescan port.
class AddressService {
public:
    using Fetch=std::function<AddressFetch(const RefreshIdentity&,const std::vector<std::string>&,std::stop_token)>;
    explicit AddressService(Fetch);~AddressService();
    bool Bind(RefreshIdentity,const BackendCapabilities&,int64_t now);
    bool SetKnownAddresses(const RefreshIdentity&,std::vector<std::string>,int64_t now);
    bool Available() const{return Owner() && selected.has_value() && !offline && !exhausted;}
    void Unbind();void SetOffline(bool);void PrivacyLock();
    void Tick(int64_t now);bool RequestNow(int64_t now);
    AddressView View(const RefreshIdentity&) const;
    bool Busy() const{return worker.joinable();}
private:
    struct Slot {RefreshIdentity identity;std::vector<std::string> known;std::optional<AddressSnapshot> snapshot;std::optional<int64_t> observed;rpc::Error error{rpc::Error::None};uint64_t sequence{};bool current{};int64_t due{};};
    struct Completion {RefreshIdentity identity;uint64_t generation{},sequence{};AddressFetch result;int64_t observed{};};
    bool Owner() const{return owner==std::this_thread::get_id();}void Fence();
    std::thread::id owner{std::this_thread::get_id()};Fetch fetch;
    std::vector<Slot> slots;std::optional<size_t> selected;
    uint64_t generation{1},sequence{};bool offline{},exhausted{};
    std::mutex mutex;std::optional<Completion> completion;std::jthread worker;
};
}
