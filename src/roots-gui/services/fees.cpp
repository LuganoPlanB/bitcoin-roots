// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/fees.h"
#include "services/funding.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <set>

namespace roots::gui {
namespace {
using Json = rpc::Json;
using Clock = std::chrono::steady_clock;
constexpr uint64_t MAX_MONEY = 21000000ULL * 100000000;
constexpr int64_t HOUR = 3600000;
constexpr size_t MAX_SAMPLES = 24 * HOUR / FEE_SAMPLE_INTERVAL + 3;

std::optional<uint64_t> Number(const Json* value, uint64_t limit = UINT64_MAX)
{
    if (!value || value->kind != Json::Kind::Number) return {};
    uint64_t n{};
    const auto end = value->text.data() + value->text.size();
    const auto read = std::from_chars(value->text.data(), end, n);
    return read.ec == std::errc{} && read.ptr == end && n <= limit ? std::optional(n) : std::nullopt;
}
std::optional<uint64_t> Sats(const Json* item)
{
    if (!item || (item->kind != Json::Kind::Number && item->kind != Json::Kind::String)) return {};
    const auto amount = Money::ParseJsonBtc(item->text);
    if (!amount || amount->sats() < 0 || uint64_t(amount->sats()) > MAX_MONEY) return {};
    return uint64_t(amount->sats());
}
std::optional<uint32_t> Rate(const Json* item)
{
    const auto sat = Sats(item);
    return sat && *sat <= UINT32_MAX ? std::optional(uint32_t(*sat)) : std::nullopt;
}
bool Txid(std::string_view text)
{
    const auto hash = Hash256::Parse(text);
    return hash && hash->hex() == text;
}
bool Is(const Json* field, Json::Kind kind, std::string_view text)
{
    return field && field->kind == kind && field->text == text;
}
std::string Text(const Json* field, size_t max)
{
    if (!field || field->kind != Json::Kind::String || field->text.size() > max || !rpc::ValidUtf8(field->text)
        || std::ranges::any_of(field->text, [](unsigned char c) { return c < 32 || c == 127; })) return {};
    return field->text;
}

struct MempoolEntry {
    uint64_t fees;
    uint32_t size;
    std::optional<std::vector<std::string>> parents;
    std::optional<uint64_t> ancestor_count, ancestor_size;
};
std::optional<std::vector<std::string>> Parents(const Json* depends)
{
    if (!depends || depends->kind != Json::Kind::Array || depends->items.size() > 128) return {};
    std::set<std::string> distinct;
    for (const auto& id : depends->items) {
        if (id.kind != Json::Kind::String || !Txid(id.text) || !distinct.insert(id.text).second) return {};
    }
    return std::vector<std::string>(distinct.begin(), distinct.end());
}
std::optional<AncestorFees> ReadAncestors(const std::map<std::string, MempoolEntry>& entries, std::span<const std::string> selected)
{
    if (selected.empty() || selected.size() > 64) return {};
    const std::set<std::string> roots(selected.begin(),selected.end());
    std::set<std::string> combined;
    for (const auto& root : roots) {
        std::set<std::string> group, visiting;
        uint64_t group_size{};
        const auto add = [&](auto&& self, const std::string& id, size_t depth) -> bool {
            if (group.contains(id)) return true;
            const auto found = entries.find(id);
            if (depth > 512 || found == entries.end() || !found->second.parents || !visiting.insert(id).second) return false;
            const auto& entry = found->second;
            for (const auto& parent : *entry.parents) if (!self(self,parent,depth+1)) return false;
            visiting.erase(id);
            group.insert(id);
            group_size+=entry.size;
            return true;
        };
        if (!add(add,root,0)) return {};
        const auto& reported=entries.at(root);
        if (reported.ancestor_count.value_or(group.size()) != group.size()
            || reported.ancestor_size.value_or(group_size) != group_size) return {};
        combined.merge(group);
    }
    AncestorFees total;
    for (const auto& id : combined) {
        const auto& entry=entries.at(id);
        if (entry.fees > MAX_MONEY - total.sats || entry.size > UINT32_MAX - total.vbytes) return {};
        total.sats+=entry.fees;
        total.vbytes+=entry.size;
    }
    total.transactions=combined.size();
    return total;
}

std::optional<RecentBlockFees> ReadBlock(const Json& stats, std::string_view hash, uint32_t height)
{
    const auto time = Number(stats.Find("time"), INT64_MAX / 1000);
    const auto txs = Number(stats.Find("txs"));
    const auto fees = Number(stats.Find("totalfee"), MAX_MONEY);
    const auto* rates = stats.Find("feerate_percentiles");
    if (!Is(stats.Find("blockhash"), Json::Kind::String, hash) || Number(stats.Find("height")) != height
        || !time || !*time || !txs || !*txs || !fees || !rates || rates->kind != Json::Kind::Array
        || rates->items.size() != 5) return {};
    const auto middle = Number(&rates->items[2], UINT32_MAX);
    if (!middle) return {};
    return RecentBlockFees{height, std::string(hash), int64_t(*time), *txs, *fees,
        *txs > 1 ? std::optional(uint32_t(*middle)) : std::nullopt};
}
}

bool FeeSupport::Allows(FeeRead read) const
{
    switch (read) {
    case FeeRead::Target: return estimate || policy;
    case FeeRead::Mempool: return mempool;
    case FeeRead::Recent: return recent;
    }
    return false;
}
FeeSupport SupportedFees(const BackendCapabilities& caps)
{
    const auto has = [&](const char* name) { return caps.Supports(name) == SupportReason::Available; };
    if (!has("getblockhash") || !has("getbestblockhash") || !has("getblockchaininfo") || !has("getwalletinfo")) return {};
    return {has("estimatesmartfee"), has("getmempoolinfo"), has("getmempoolinfo") && has("getrawmempool"),
        has("getblockstats") && has("getblockheader")};
}

FeeQuote ReadFeeQuote(const Json& value)
{
    FeeQuote out;
    if (const auto target = Number(value.Find("blocks"), MAX_FEE_TARGET)) out.blocks = uint16_t(*target);
    if (const auto* errors = value.Find("errors")) {
        if (errors->kind != Json::Kind::Array) out.error = "The estimate response was malformed.";
        else if (!errors->items.empty()) {
            out.error = Text(&errors->items.front(), 200);
            if (out.error.empty()) out.error = "The node reported that an estimate is unavailable.";
        }
    }
    if (!out.error.empty()) return out;
    if (const auto rate = Rate(value.Find("feerate")); rate && *rate && out.blocks.value_or(0)) {
        out.rate = rate;
        out.estimator = Text(value.Find("estimator"), 64);
    } else out.error = "The node did not return a usable fee rate and block target.";
    return out;
}

std::optional<FeePolicy> ReadFeePolicy(const Json& value)
{
    if (!Is(value.Find("loaded"), Json::Kind::Boolean, "true")) return {};
    FeePolicy policy{Rate(value.Find("minrelaytxfee")), Rate(value.Find("mempoolminfee")),
        Rate(value.Find("incrementalrelayfee"))};
    if (policy.relay && policy.mempool && *policy.mempool < *policy.relay) return {};
    return policy;
}

std::optional<MempoolFees> ReadMempoolFees(const Json& value, std::span<const std::string> selected)
{
    if (value.kind != Json::Kind::Object || value.members.size() > MAX_MEMPOOL_FEE_ENTRIES) return {};
    MempoolFees result;
    std::map<std::string, MempoolEntry> entries;
    for (const auto& [id, item] : value.members) {
        const auto vbytes = Number(item.Find("vsize"), 4000000);
        const auto* fees = item.Find("fees");
        const auto base = fees ? Sats(fees->Find("base")) : std::nullopt;
        if (!Txid(id) || !vbytes || !*vbytes || !base) return {};
        const auto count=Number(item.Find("ancestorcount"));
        const auto size=Number(item.Find("ancestorsize"));
        auto parents=Parents(item.Find("depends"));
        if ((item.Find("ancestorcount") && !count) || (item.Find("ancestorsize") && !size)) parents.reset();
        if (!entries.emplace(id,MempoolEntry{*base,uint32_t(*vbytes),std::move(parents),count,size}).second) return {};
        const auto band = std::ranges::find_if(FEE_BAND_ENDS, [&](auto end) { return *base < *vbytes * end; });
        result.bands.vbytes[size_t(band - FEE_BAND_ENDS.begin())] += *vbytes;
    }
    result.ancestors = ReadAncestors(entries, selected);
    return result;
}

void FeeHistory::Append(FeeSample sample)
{
    if (sample.monotonic_ms <= 0 || sample.observed_ms <= 0
        || (!samples.empty() && (sample.monotonic_ms <= samples.back().monotonic_ms
            || sample.monotonic_ms - samples.back().monotonic_ms < FEE_SAMPLE_INTERVAL))) return;
    if (samples.size() == MAX_SAMPLES) samples.erase(samples.begin());
    samples.push_back(std::move(sample));
}
FeeCoverage FeeHistory::Coverage(int64_t now, int64_t hours) const
{
    if ((hours != 2 && hours != 24) || now < hours * HOUR || samples.empty()) return {};
    const auto start = now - hours * HOUR;
    int64_t prior = start;
    FeeCoverage out;
    bool beginning{};
    for (const auto& sample : samples) {
        if (sample.monotonic_ms > now) break;
        if (sample.monotonic_ms <= start) {
            beginning = sample.bands && start - sample.monotonic_ms <= FEE_SAMPLE_GAP;
            if (beginning) prior = sample.monotonic_ms;
            continue;
        }
        if (sample.monotonic_ms - prior > FEE_SAMPLE_GAP) ++out.gaps;
        prior = sample.monotonic_ms;
        if (sample.bands) ++out.samples;
        else ++out.gaps;
    }
    if (now - prior > FEE_SAMPLE_GAP) ++out.gaps;
    out.sufficient = beginning && out.gaps == 0 && out.samples >= size_t(hours*HOUR/FEE_SAMPLE_INTERVAL - 1);
    return out;
}

bool FeeObservationLive(const FeeObservation& value, int64_t now)
{
    return value.monotonic_ms > 0 && value.observed_ms > 0 && value.monotonic_ms <= now
        && now - value.monotonic_ms <= FEE_SAMPLE_INTERVAL;
}
std::optional<uint32_t> EffectiveFundingRate(std::string_view manual, uint16_t target, const FeeQuote* live_quote)
{
    if (!manual.empty()) return ParseFundingRate(manual);
    if (!target || !live_quote || !live_quote->rate || !live_quote->blocks.value_or(0) || !live_quote->error.empty()
        || *live_quote->rate > MAX_FUNDING_RATE) return {};
    return live_quote->rate;
}
bool MeetsFeeMinimum(uint32_t rate, const FeePolicy& policy)
{
    return (!policy.relay || rate >= *policy.relay) && (!policy.mempool || rate >= *policy.mempool);
}
bool HighFundingFee(Money fee, Money recipients)
{
    return fee.sats() >= 100000 || (fee.sats() > 0 && recipients.sats() > 0
        && fee.sats() >= recipients.sats()/10 + (recipients.sats()%10 != 0));
}
std::string FeeRateText(uint32_t rate)
{
    const auto fraction = std::to_string(1000 + rate%1000).substr(1);
    return std::to_string(rate/1000) + "." + fraction;
}

FeeObservation FetchFees(rpc::Transport& transport, const FeeRequest& request, std::stop_token stop)
{
    const auto& scope = request.scope;
    const auto& profile = scope.owner.wallet.profile;
    const auto& genesis = scope.owner.wallet.context.network.genesis.hex();
    FeeObservation result{request.read};
    const auto failure = [&](std::string error) {
        FeeObservation unavailable{request.read};
        unavailable.error = std::move(error);
        return unavailable;
    };
    if (!ValidServerCandidate(profile) || !profile.wallet || !scope.owner.document || profile.expected_genesis != genesis
        || !Txid(scope.tip) || !request.support.Allows(request.read) || scope.target > MAX_FEE_TARGET)
        return failure("Fee observation source or scope is unavailable.");
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    uint64_t counter{};
    const auto call = [&](std::string method, std::string params = "[]", bool wallet = false) -> std::optional<Json> {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (stop.stop_requested() || left.count() <= 0) return {};
        rpc::Limits limits;
        limits.total_timeout = left;
        limits.connect_timeout = std::min(left, std::chrono::milliseconds(3000));
        limits.response_bytes = 4 * 1024 * 1024;
        auto reply = transport.Call(profile.connection, {++counter, std::move(method), std::move(params),
            wallet ? profile.wallet : std::nullopt, rpc::Effect::ReadOnly}, stop, limits);
        if (reply.error != rpc::Error::None || stop.stop_requested()) return {};
        return std::move(reply.result);
    };
    const auto verify_identity = [&] {
        const auto first = call("getblockhash", "[0]");
        const auto wallet = call("getwalletinfo", "[]", true);
        return first && Is(&*first, Json::Kind::String, genesis)
            && wallet && Is(wallet->Find("walletname"), Json::Kind::String, *profile.wallet);
    };
    const auto chain = call("getblockchaininfo");
    const auto height = chain ? Number(chain->Find("blocks"), UINT32_MAX) : std::nullopt;
    if (!height || !chain || !Is(chain->Find("bestblockhash"), Json::Kind::String, scope.tip)
        || !Is(chain->Find("initialblockdownload"), Json::Kind::Boolean, "false") || !verify_identity())
        return failure("Authenticated current chain or wallet evidence changed. Read again after refresh.");

    if (request.read == FeeRead::Recent) {
        auto hash=scope.tip;
        for (auto n = *height; n > 0 && *height - n < 6; --n) {
            const auto stats = call("getblockstats", "[" + rpc::Quote(hash)
                + ",[\"blockhash\",\"height\",\"time\",\"txs\",\"totalfee\",\"feerate_percentiles\"]]");
            const auto row = stats ? ReadBlock(*stats, hash, uint32_t(n)) : std::nullopt;
            if (!row) {
                result.error = "A recent block summary is unavailable, including on pruned data.";
                break;
            }
            result.recent.push_back(*row);
            if (n==1 || *height-n==5) break;
            const auto header = call("getblockheader", "[" + rpc::Quote(hash) + ",true]");
            const auto* previous = header ? header->Find("previousblockhash") : nullptr;
            if (!header || !Is(header->Find("hash"),Json::Kind::String,hash) || Number(header->Find("height")) != n
                || !previous || previous->kind != Json::Kind::String || !Txid(previous->text)) {
                result.error = "A recent height could not be bound to the active chain.";
                break;
            }
            hash=previous->text;
        }
    } else {
        const auto info = request.support.policy ? call("getmempoolinfo") : std::nullopt;
        if (info) result.policy = ReadFeePolicy(*info);
        if (request.read == FeeRead::Target) {
            if (request.support.estimate && scope.target) {
                const auto quote = call("estimatesmartfee", "[" + std::to_string(scope.target) + "]");
                result.quote = quote ? ReadFeeQuote(*quote) : FeeQuote{.error="The node returned no estimate."};
            } else if (scope.target) result.quote.error = "estimatesmartfee is unavailable on this node.";
        } else {
            const auto count = info ? Number(info->Find("size")) : std::nullopt;
            if (!count || !Is(info->Find("loaded"), Json::Kind::Boolean, "true")) result.error = "The mempool is unavailable or still loading.";
            else if (*count > MAX_MEMPOOL_FEE_ENTRIES) result.error = "Mempool exceeds the 2,048-entry observation limit.";
            else {
                const auto mempool = call("getrawmempool", "[true]");
                if (mempool) result.mempool = ReadMempoolFees(*mempool, scope.unconfirmed);
                if (!result.mempool) result.error = "A complete bounded mempool snapshot is unavailable.";
            }
        }
    }
    const bool same_wallet=verify_identity();
    const auto tip = same_wallet ? call("getbestblockhash") : std::nullopt;
    if (!tip || !Is(&*tip, Json::Kind::String, scope.tip) || stop.stop_requested())
        return failure("Fee observation retired because chain or wallet evidence changed.");
    result.tip = scope.tip;
    result.monotonic_ms = RefreshMonotonicMs();
    result.observed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return result;
}

FeeService::~FeeService() { Cancel(); }
bool FeeService::Start(FeeRequest request)
{
    if (std::this_thread::get_id() != owner || Busy() || !fetch) return false;
    running = request.scope;
    worker = std::jthread([this, request = std::move(request)](std::stop_token stop) {
        FeeObservation value{request.read};
        try { value = fetch(request, stop); }
        catch (...) { value.error = "Fee observation unavailable."; }
        std::lock_guard lock(mutex);
        result = std::move(value);
    });
    return true;
}
void FeeService::Cancel()
{
    if (std::this_thread::get_id() != owner) return;
    running.reset();
    if (Busy()) worker.request_stop();
}
std::optional<FeeObservation> FeeService::Poll(const std::optional<FeeScope>& scope)
{
    if (std::this_thread::get_id() != owner || !Busy()) return {};
    if (running != scope) Cancel();
    std::optional<FeeObservation> done;
    {
        std::lock_guard lock(mutex);
        if (!result) return {};
        done = std::exchange(result, std::nullopt);
    }
    worker.join();
    const bool current = running && running == scope;
    running.reset();
    return current ? std::move(done) : std::nullopt;
}
}
