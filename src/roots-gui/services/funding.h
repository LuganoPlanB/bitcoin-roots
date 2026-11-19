// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "adapters/protocol/offline_draft.h"
#include "services/funding_change.h"
#include "services/refresh.h"
#include "services/transaction_flow.h"

namespace roots::gui {
inline constexpr size_t MAX_FUNDING_INPUTS = 64;
inline constexpr size_t MAX_FUNDING_OUTPUTS = 64;

struct FundingScope {
    RefreshIdentity wallet;
    uint64_t document{}, profile_revision{}, generation{}, history_sequence{}, policy_revision{};
    DraftRevision draft;
    std::string tip;
    uint32_t fee_rate{};
    int64_t fee_observation{};
    bool operator==(const FundingScope&) const = default;
};
struct FundingCoin {
    Outpoint point;
    Money amount;
    std::string script;
    protocol::OfflineCoinProof proof;
    int64_t confirmations{};
};
struct FundingRecipient {
    std::string address, script;
    std::optional<Money> amount; // empty is the sole Max recipient
};
enum class FundingSelection { Automatic, Restricted, Exact };
struct FundingPreview {
    std::shared_ptr<const protocol::TransactionDocument> psbt;
    std::vector<protocol::OfflineReviewInput> inputs;
    std::optional<FundingChange> change;
    Money total_inputs, fee;
    std::optional<Money> maximum;
    std::vector<FundingCoin> verified_coins;
    std::vector<FundingChange> change_choices;
    std::optional<TransactionFlow> flow;
};
struct FundingRequest {
    FundingScope scope;
    FundingSelection selection;
    std::vector<FundingCoin> coins; // current eligible allowlist or exact inputs
    std::vector<FundingRecipient> recipients;
    uint32_t rate_per_kvb{}; // exact satoshis per 1000 vB
    std::optional<FundingPreview> recheck; // Verify existing bytes without another construction call.
    std::optional<FundingChange> requested_change;
};
struct FundingReply {
    std::optional<FundingPreview> preview;
    std::string error;
};

// The supported daemon fee_rate argument is sat/vB at 0.001 precision.
std::optional<uint32_t> ParseFundingRate(std::string_view);
bool CanFund(const BackendCapabilities&);
// Conservative size ceiling for an independently proved supported input.
uint32_t FundingInputVsizeBound(std::string_view script,std::string_view redeem);
FundingReply FetchFunding(rpc::Transport&, const FundingRequest&, std::stop_token);

class FundingService {
public:
    using Fetch = std::function<FundingReply(const FundingRequest&, std::stop_token)>;
    explicit FundingService(Fetch fetch) : fetch(std::move(fetch)) {}
    ~FundingService();
    bool Busy() const { return worker.joinable(); }
    bool Start(FundingRequest);
    void Cancel();
    std::optional<FundingReply> Poll(const std::optional<FundingScope>&);
private:
    const std::thread::id owner{std::this_thread::get_id()};
    Fetch fetch;
    std::optional<FundingScope> running;
    std::optional<FundingReply> completed;
    std::mutex mutex;
    std::jthread worker;
};
}
