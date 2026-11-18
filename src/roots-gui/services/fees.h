// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "services/refresh.h"
#include <mutex>
#include <span>
#include <thread>

namespace roots::gui {
inline constexpr uint16_t MAX_FEE_TARGET = 1008;
inline constexpr uint32_t MAX_FUNDING_RATE = 10000000; // 10,000 sat/vB
inline constexpr int64_t FEE_SAMPLE_INTERVAL = 300000; // 5 minutes
inline constexpr int64_t FEE_SAMPLE_GAP = 450000;
inline constexpr size_t MAX_MEMPOOL_FEE_ENTRIES = 2048;
inline constexpr std::array<uint32_t,5> FEE_BAND_ENDS{1, 2, 5, 10, 25}; // sat/vB, exclusive

enum class FeeRead { Target, Mempool, Recent };
struct FeeContext {
    RefreshIdentity wallet;
    uint64_t profile_revision, document;
    bool operator==(const FeeContext&) const = default;
};
struct FeeScope {
    FeeContext owner;
    DraftRevision draft;
    std::string tip;
    uint16_t target{};
    std::vector<std::string> unconfirmed;
    bool operator==(const FeeScope&) const = default;
};
struct FeeSupport {
    bool estimate{}, policy{}, mempool{}, recent{};
    bool Allows(FeeRead) const;
};
FeeSupport SupportedFees(const BackendCapabilities&);

struct FeeQuote {
    std::optional<uint32_t> rate; // sat/kvB, converted exactly from BTC/kvB
    std::optional<uint16_t> blocks;
    std::string estimator, error;
};
struct FeePolicy {
    std::optional<uint32_t> relay, mempool, incremental; // sat/kvB; zero is distinct from Unknown
};
struct FeeBandSnapshot {
    std::array<uint64_t, FEE_BAND_ENDS.size()+1> vbytes{};
};
struct AncestorFees {
    uint64_t sats{};
    uint32_t vbytes{};
    size_t transactions{};
};
struct MempoolFees {
    FeeBandSnapshot bands;
    std::optional<AncestorFees> ancestors;
};
struct RecentBlockFees {
    uint32_t height{};
    std::string hash;
    int64_t time{};
    uint64_t transactions{}, fees{}; // txs including coinbase, and total non-coinbase fee in sats
    std::optional<uint32_t> median_rate; // daemon's integer sat/vB percentile
};
struct FeeObservation {
    FeeRead read;
    int64_t observed_ms{}, monotonic_ms{};
    std::string tip;
    FeeQuote quote;
    std::optional<FeePolicy> policy;
    std::optional<MempoolFees> mempool;
    std::vector<RecentBlockFees> recent;
    std::string error;
};
struct FeeRequest { FeeScope scope; FeeRead read; FeeSupport support; };

struct FeeSample {
    int64_t observed_ms, monotonic_ms;
    std::optional<FeeBandSnapshot> bands; // failed scheduled observations are gaps
};
struct FeeCoverage {size_t samples{}, gaps{}; bool sufficient{};};
class FeeHistory {
    std::vector<FeeSample> samples;
public:
    void Append(FeeSample);
    std::span<const FeeSample> Samples() const { return samples; }
    FeeCoverage Coverage(int64_t now, int64_t hours) const;
};

FeeQuote ReadFeeQuote(const rpc::Json&);
std::optional<FeePolicy> ReadFeePolicy(const rpc::Json&);
std::optional<MempoolFees> ReadMempoolFees(const rpc::Json&, std::span<const std::string> selected);
bool FeeObservationLive(const FeeObservation&, int64_t now);
std::optional<uint32_t> EffectiveFundingRate(std::string_view manual, uint16_t target, const FeeQuote* live_quote);
bool MeetsFeeMinimum(uint32_t rate, const FeePolicy&);
bool HighFundingFee(Money fee, Money recipients); // >= 100,000 sats or >= 10% of recipients
std::string FeeRateText(uint32_t sat_per_kvb);
FeeObservation FetchFees(rpc::Transport&, const FeeRequest&, std::stop_token);

class FeeService {
public:
    using Fetch = std::function<FeeObservation(const FeeRequest&, std::stop_token)>;
    explicit FeeService(Fetch fetch) : fetch(std::move(fetch)) {}
    ~FeeService();
    bool Busy() const { return worker.joinable(); }
    bool Start(FeeRequest);
    void Cancel();
    std::optional<FeeObservation> Poll(const std::optional<FeeScope>&);
private:
    const std::thread::id owner{std::this_thread::get_id()};
    Fetch fetch;
    std::optional<FeeScope> running;
    std::optional<FeeObservation> result;
    std::mutex mutex;
    std::jthread worker;
};
}
