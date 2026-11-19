// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/refresh.h"
#include <algorithm>
#include <charconv>
#include <ctime>
#include <limits>

namespace roots::gui {
namespace {
using J = rpc::Json;
std::optional<int64_t> Integer(const J* value)
{
    if (!value || value->kind != J::Kind::Number) return {};
    int64_t n{}; const auto p = std::from_chars(value->text.data(), value->text.data() + value->text.size(), n);
    return p.ec == std::errc{} && p.ptr == value->text.data() + value->text.size() ? std::optional<int64_t>(n) : std::nullopt;
}
std::optional<bool> Boolean(const J* value)
{
    if (!value || value->kind != J::Kind::Boolean) return {};
    return value->text == "true";
}
std::optional<Money> Amount(const J* value,bool nonnegative=true)
{
    if (!value || (value->kind != J::Kind::Number && value->kind != J::Kind::String)) return {};
    auto n = Money::ParseJsonBtc(value->text);
    return n && n->sats() >= (nonnegative?0:-2100000000000000LL) && n->sats() <= 2100000000000000LL ? n : std::nullopt;
}
int64_t Due(int64_t now, int64_t delay)
{
    return now <= std::numeric_limits<int64_t>::max() - delay ? now + delay : std::numeric_limits<int64_t>::max();
}
}
int64_t RefreshMonotonicMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::string ObservationDate(std::optional<int64_t> value)
{
    if (!value || *value <= 0) return "No successful observation";
    const auto seconds = *value / 1000;
    const std::time_t time = static_cast<std::time_t>(seconds);
    if (static_cast<int64_t>(time) != seconds) return "Observation date unavailable";
    const auto* utc = std::gmtime(&time); char text[32]{};
    if (!utc || !std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S UTC", utc)) return "Observation date unavailable";
    return text;
}
RefreshReply FetchRefresh(rpc::Transport& transport, const RefreshRequest& request, std::stop_token stop)
{
    RefreshReply result{request.identity, request.generation, request.sequence};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    uint64_t id{};
    const auto call = [&](std::string method, std::string params = "[]", bool wallet = false) {
        rpc::Reply reply;
        if (stop.stop_requested()) { reply.error = rpc::Error::Cancelled; return reply; }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) { reply.error = rpc::Error::Timeout; return reply; }
        rpc::Limits limits; limits.total_timeout = left; limits.connect_timeout = std::min(left, std::chrono::milliseconds(3000));
        return transport.Call(request.identity.profile.connection, {++id, std::move(method), std::move(params), wallet ? request.identity.profile.wallet : std::nullopt, rpc::Effect::ReadOnly}, stop, limits);
    };
    const auto chain = call("getblockchaininfo");
    if (chain.error != rpc::Error::None || !chain.result) { result.errors.fill(chain.error); return result; }
    const auto height = Integer(chain.result->Find("blocks")); const auto ibd = Boolean(chain.result->Find("initialblockdownload")); const auto pruned = Boolean(chain.result->Find("pruned"));
    const auto* tip = chain.result->Find("bestblockhash");
    if (!height || *height < 0 || *height>INT32_MAX || !ibd || !pruned || !tip || tip->kind != J::Kind::String || !Hash256::Parse(tip->text)) { result.errors.fill(rpc::Error::Json); return result; }
    const auto genesis = call("getblockhash", "[0]");
    if (genesis.error != rpc::Error::None || !genesis.result) { result.errors.fill(genesis.error); return result; }
    if (genesis.result->kind != J::Kind::String || genesis.result->text != request.identity.context.network.genesis.hex() || genesis.result->text != request.identity.profile.expected_genesis) { result.errors.fill(rpc::Error::Identity); return result; }
    result.node = NodeObservation{*height, tip->text, *ibd, *pruned};
    if (request.identity.profile.wallet) {
        if (!request.wallet_supported || !request.balances_supported) result.errors[1] = rpc::Error::Rpc;
        else {
            const auto info = call("getwalletinfo", "[]", true);
            const auto* name = info.result ? info.result->Find("walletname") : nullptr;
            const auto* scan = info.result ? info.result->Find("scanning") : nullptr;
            if (info.error != rpc::Error::None) result.errors[1] = info.error;
            else if (!name || name->kind != J::Kind::String || name->text != *request.identity.profile.wallet || !scan || (scan->kind != J::Kind::Boolean && scan->kind != J::Kind::Object)) result.errors[1] = rpc::Error::Identity;
            else {
                const auto balances = call("getbalances", "[]", true);
                const auto* mine = balances.result ? balances.result->Find("mine") : nullptr;
                const auto trusted = mine ? Amount(mine->Find("trusted")) : std::nullopt;
                const auto pending = mine ? Amount(mine->Find("untrusted_pending")) : std::nullopt;
                const auto immature = mine ? Amount(mine->Find("immature")) : std::nullopt;
                if (balances.error != rpc::Error::None) result.errors[1] = balances.error;
                else if (!trusted || !pending || !immature) result.errors[1] = rpc::Error::Json;
                else {
                    WalletObservation value{*trusted, *pending, *immature}; value.scanning = scan->kind == J::Kind::Object || scan->text == "true";
                    if (const auto* other = mine->Find("nonmempool")) { value.nonmempool = Amount(other,false); if (!value.nonmempool) result.errors[1] = rpc::Error::Json; }
                    if (const auto* used = mine->Find("used")) { value.used = Amount(used); if (!value.used) result.errors[1] = rpc::Error::Json; }
                    if (result.errors[1] == rpc::Error::None) result.wallet = std::move(value);
                }
            }
        }
        if (request.fetch_history) {
            if (result.errors[1] != rpc::Error::None) result.errors[2] = result.errors[1];
            else if (!request.history_supported) result.errors[2] = rpc::Error::Rpc;
            else {
                const std::string params = "{\"include_removed\":true" + (request.cursor ? ",\"blockhash\":" + rpc::Quote(*request.cursor) : "") + '}';
                const auto history = call("listsinceblock", params, true);
                const auto* rows = history.result ? history.result->Find("transactions") : nullptr;
                const auto* removed = history.result ? history.result->Find("removed") : nullptr;
                const auto* last = history.result ? history.result->Find("lastblock") : nullptr;
                if (history.error != rpc::Error::None) result.errors[2] = history.error;
                else if (!rows || rows->kind != J::Kind::Array || !removed || removed->kind != J::Kind::Array || !last || last->kind != J::Kind::String || !Hash256::Parse(last->text)) result.errors[2] = rpc::Error::Json;
                else {
                    HistoryDelta delta{*rows,*removed,last->text};
                    if(request.normalize_history){
                        const auto& w=*result.wallet;const HistoryBalances balances{w.trusted,w.pending,w.immature,w.nonmempool.value_or(Money::Satoshis(0)),w.used};
                        auto normalized=FetchHistory(transport,request.identity.profile,request.identity.context,*history.result,balances,result.node->tip,!request.cursor,w.scanning,result.node->pruned,request.inventory_supported,request.revisit,stop,deadline,request.history_limits,request.decoded_supported,request.locks_supported);
                        if(normalized.batch)delta.normalized=std::move(normalized.batch);else result.errors[2]=normalized.error;
                    }
                    if(result.errors[2]==rpc::Error::None)result.history=std::move(delta);
                }
            }
        }
    }
    result.observed_unix_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return result;
}
RefreshService::RefreshService(Fetch value) : fetch(std::move(value)) {}
RefreshService::~RefreshService() { if (worker.joinable()) worker.request_stop(); }
void RefreshService::Fence()
{
    if (worker.joinable()) worker.request_stop();
    if (generation == std::numeric_limits<uint64_t>::max()) exhausted = true; else ++generation;
    if (selected) {slots[*selected].current.fill(false);slots[*selected].scan_attempt=false;}
}
bool RefreshService::Bind(RefreshIdentity identity, const BackendCapabilities& caps, bool is_locked, int64_t now)
{
    if (!Owner()) return false;
    if (exhausted || !caps.CanApply() || caps.candidate != identity.profile || caps.genesis != identity.context.network.genesis.hex() || identity.profile.expected_genesis != identity.context.network.genesis.hex() || !identity.connection_session) { Unbind(); return false; }
    if (selected && slots[*selected].identity == identity && locked == is_locked) return true;
    Fence(); locked = is_locked;
    auto found = std::find_if(slots.begin(), slots.end(), [&](const auto& slot) { return slot.identity == identity; });
    if (found == slots.end()) {
        selected.reset();
        if (slots.size() == 8) { slots.erase(slots.begin()); }
        Slot slot{std::move(identity)};
        slot.wallet_supported = caps.Supports("getwalletinfo") == SupportReason::Available;
        slot.balances_supported = caps.Supports("getbalances") == SupportReason::Available;
        slot.history_supported = caps.Supports("listsinceblock", "include_removed") == SupportReason::Available && caps.Supports("listsinceblock", "blockhash") == SupportReason::Available;
        slot.locks_supported=caps.Supports("listlockunspent")==SupportReason::Available;
        slot.decoded_supported=caps.Supports("gettransaction","verbose")==SupportReason::Available;
        slot.normalize_history=caps.Supports("gettransaction","txid")==SupportReason::Available;
        slot.inventory_supported=caps.Supports("listunspent","minconf")==SupportReason::Available && caps.Supports("listunspent","include_unsafe")==SupportReason::Available && caps.Supports("listunspent","query_options.include_immature_coinbase")==SupportReason::Available && caps.Supports("listunspent","query_options.maximumCount")==SupportReason::Available;
        if(slot.normalize_history && slot.identity.profile.wallet)slot.ledger.emplace(slot.identity.context,*slot.identity.profile.wallet);
        slots.push_back(std::move(slot)); selected = slots.size() - 1;
    } else selected = size_t(found - slots.begin());
    auto& slot = slots[*selected]; slot.current.fill(false); slot.next_due = now;
    return true;
}
void RefreshService::Unbind() { if (!Owner()) return; if (!selected) { if (worker.joinable()) worker.request_stop(); return; } Fence(); selected.reset(); }
void RefreshService::SetOffline(bool value, int64_t now)
{
    if (!Owner() || offline == value) return;
    Fence(); offline = value; if (selected) slots[*selected].next_due = now;
}
void RefreshService::Cancel(int64_t now) { if (Owner()) { Fence(); if (selected) slots[*selected].next_due = Due(now,5000); } }
void RefreshService::PrivacyLock(){if(!Owner() || locked)return;Fence();locked=true;slots.clear();selected.reset();}
void RefreshService::Poll(int64_t now)
{
    std::optional<RefreshReply> result;
    { std::lock_guard lock(mutex); if (!completion) return; result = std::move(completion); completion.reset(); }
    worker.join();
    if (!selected || offline || locked || exhausted || result->generation != generation) return;
    auto& slot = slots[*selected];
    if (result->identity != slot.identity || result->sequence != sequence) return;
    if (result->observed_unix_ms <= 0 && (result->node || result->wallet || result->history)) { result->node.reset(); result->wallet.reset(); result->history.reset(); result->errors.fill(rpc::Error::Identity); }
    slot.errors = result->errors; slot.current.fill(false);
    if (result->node) { slot.node = std::move(result->node); slot.observed[0] = result->observed_unix_ms; slot.current[0] = true; }
    if (result->wallet) { slot.wallet = std::move(result->wallet); slot.observed[1] = result->observed_unix_ms; slot.current[1] = true; }
    const bool scan_attempt=slot.scan_attempt;
    if (result->history && (!slot.history || scan_attempt)) {
        if(result->history->normalized && (!slot.ledger || !slot.ledger->Apply(*result->history->normalized,result->sequence,result->observed_unix_ms))){slot.errors[2]=rpc::Error::Identity;if(scan_attempt)slot.scan_blocked=true;}
        else {if(scan_attempt && result->history->normalized){slot.scan_blocked=result->history->normalized->limited && result->history->normalized->transactions.size()<=slot.scan_previous_rows;slot.scan_attempt=false;}slot.history = std::move(result->history); slot.history_sequence = result->sequence; slot.observed[2] = result->observed_unix_ms; slot.current[2] = true;
            // Advance only after transactional normalization, never for a
            // bounded partial scan. Legacy raw consumers retain explicit ACK.
            if(slot.history->normalized && !slot.history->normalized->limited){slot.cursor=slot.history->lastblock;slot.history.reset();}
        }
    }
    slot.scan_attempt=false; // failed/cancelled continuation never becomes an automatic retry
    const bool failure = std::any_of(slot.errors.begin(), slot.errors.end(), [](auto error) { return error != rpc::Error::None; });
    slot.failures = failure ? std::min(slot.failures + 1, 5U) : 0;
    const int64_t delay = failure ? std::min(int64_t(60000), int64_t(5000) << (slot.failures - 1)) : 5000;
    slot.next_due = Due(now, delay);
}
void RefreshService::Tick(int64_t now)
{
    if (!Owner()) return;
    Poll(now);
    if (!selected || offline || locked || exhausted || Busy() || !fetch || now < slots[*selected].next_due) return;
    if (sequence == std::numeric_limits<uint64_t>::max()) { exhausted = true; return; }
    const auto& slot = slots[*selected];
    RefreshRequest request{slot.identity, generation, ++sequence, slot.wallet_supported, slot.balances_supported, slot.history_supported, (!slot.history.has_value() || slot.scan_attempt), slot.cursor};
    request.history_limits=slot.history_limits;request.decoded_supported=slot.decoded_supported;request.locks_supported=slot.locks_supported;request.normalize_history=slot.normalize_history;request.inventory_supported=slot.inventory_supported;if(slot.ledger)request.revisit=slot.ledger->Revisit();
    try {
        worker = std::jthread([this, request = std::move(request)](std::stop_token stop) {
            RefreshReply reply{request.identity, request.generation, request.sequence};
            try { reply = fetch(request, stop); } catch (...) { reply.errors.fill(rpc::Error::Configuration); }
            if (reply.identity != request.identity || reply.generation != request.generation || reply.sequence != request.sequence) {
                reply = RefreshReply{request.identity, request.generation, request.sequence}; reply.errors.fill(rpc::Error::Identity);
            }
            if (stop.stop_requested()) { reply.node.reset(); reply.wallet.reset(); reply.history.reset(); reply.errors.fill(rpc::Error::Cancelled); }
            std::lock_guard lock(mutex); completion = std::move(reply);
        });
    } catch (...) { auto& current = slots[*selected]; current.errors.fill(rpc::Error::Configuration); current.next_due = Due(now, 5000); current.current.fill(false); }
}
bool RefreshService::RequestNow(int64_t now)
{
    if (!Owner() || !selected || offline || locked || exhausted) return false;
    Fence(); slots[*selected].next_due = now; return true;
}
bool RefreshService::ScanEarlier(const RefreshIdentity& identity,int64_t now)
{
    if(!Owner() || !selected || offline || locked || exhausted || Busy())return false;
    auto& slot=slots[*selected];
    if(slot.identity!=identity || slot.scan_blocked || !slot.normalize_history || !slot.ledger || slot.ledger->Scanning() || slot.ledger->Pruned())return false;
    if(slot.history){
        if(!slot.history->normalized || !slot.history->normalized->limited || slot.history_limits.transactions>=4096)return false;
        // Retain the pending limited observation until a successful re-read
        // replaces it transactionally. Failure/cancellation leaves it intact.
        slot.history_limits.transactions=std::min(size_t(4096),slot.history_limits.transactions*2);
        slot.scan_previous_rows=slot.history->normalized->transactions.size();
    }else{
        if(slot.ledger->Complete())return false;
        slot.cursor.reset(); // explicit full wallet query, never daemon rescan
    }
    const bool retry=slot.history.has_value();Fence();slot.scan_attempt=retry;slot.next_due=now;return true;
}
bool RefreshService::AcknowledgeHistory(const RefreshIdentity& identity, uint64_t seq, std::string_view hash)
{
    if (!Owner() || !selected || offline || locked) return false;
    auto& slot = slots[*selected];
    if (slot.identity != identity || !slot.history || slot.history_sequence != seq || slot.history->lastblock != hash) return false;
    if(slot.history->normalized && slot.history->normalized->limited)return false;
    slot.cursor = slot.history->lastblock; slot.history.reset(); return true;
}
uint64_t RefreshService::PendingHistorySequence() const { return Owner() && selected ? slots[*selected].history_sequence : 0; }
bool RefreshService::SetHistoryPolicy(const RefreshIdentity& identity,HistoryPolicy policy){if(!Owner() || !selected || locked || slots[*selected].identity!=identity || !slots[*selected].ledger)return false;return slots[*selected].ledger->SetPolicy(std::move(policy));}
RefreshView RefreshService::View() const
{
    RefreshView value; if (!Owner()) return value;
    value.generation=generation;value.offline = offline; value.locked = locked; value.busy = Busy();
    if (!selected) return value;
    const auto& slot = slots[*selected]; value.errors = slot.errors; value.next_due_ms = slot.next_due;
    if (locked) return value;
    value.identity = &slot.identity;
    value.ledger=slot.ledger?&*slot.ledger:nullptr;value.generation=generation;value.history_sequence=slot.history_sequence;value.scan_budget=slot.history_limits.transactions;value.scan_blocked=slot.scan_blocked;value.chain_current=slot.ledger && slot.node && slot.current[0] && slot.current[2] && slot.errors[0]==rpc::Error::None && slot.errors[2]==rpc::Error::None && slot.ledger->Stable() && !slot.ledger->Limited() && slot.node->tip==slot.ledger->Tip() && !offline;
    value.node = slot.node ? &*slot.node : nullptr; value.wallet = slot.wallet ? &*slot.wallet : nullptr; value.history = slot.history ? &*slot.history : nullptr; value.observed = slot.observed;
    for (size_t i = 0; i < 3; ++i) value.freshness[i] = offline ? Freshness::Offline : !slot.observed[i] ? Freshness::Unknown : slot.errors[i] != rpc::Error::None ? Freshness::Stale : !slot.current[i] ? Freshness::Cached : Freshness::Live;
    return value;
}
std::vector<RefreshView> RefreshService::ObservationViews() const
{
    std::vector<RefreshView> values;
    if(!Owner() || locked)return values;
    for(size_t index=0;index<slots.size();++index){
        const auto& slot=slots[index];
        if(!slot.ledger && !slot.wallet)continue;
        if(selected==index){values.push_back(View());continue;}
        RefreshView value;
        value.identity=&slot.identity;value.node=slot.node?&*slot.node:nullptr;
        value.wallet=slot.wallet?&*slot.wallet:nullptr;value.ledger=slot.ledger?&*slot.ledger:nullptr;
        value.generation=generation;value.history_sequence=slot.history_sequence;value.scan_budget=slot.history_limits.transactions;
        value.scan_blocked=slot.scan_blocked;value.observed=slot.observed;value.errors=slot.errors;value.offline=offline;
        for(size_t i=0;i<3;++i)value.freshness[i]=offline?Freshness::Offline:!slot.observed[i]?Freshness::Unknown:
            slot.errors[i]!=rpc::Error::None?Freshness::Stale:Freshness::Cached;
        values.push_back(value);
    }
    return values;
}
std::vector<RefreshView> RefreshService::HistoryViews() const
{
    auto values=ObservationViews();
    std::erase_if(values,[](const RefreshView& view){return !view.ledger;});
    return values;
}
}
