// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "domain/rescan.h"
#include "services/refresh.h"
#include "services/wallet_lifecycle.h"

namespace roots::gui {
namespace storage {class FileGuard;}
struct ScanObservation {
    int64_t chain_height{}, wallet_height{}, first_available{};
    bool pruned{}, initial_download{}, scanning{}, wallet_locked{};
    std::optional<int64_t> birthtime, receive_keys, change_keys;
    std::optional<std::vector<PolicyRange>> active_ranges;
};
struct ScanReview {
    ScanRange range;
    Hash256 last_block;
};
enum class ScanAction { Check, Review, Start, Acknowledge };
struct ScanRequest {
    ScanAction action;
    RefreshIdentity identity;
    uint64_t profile_revision{}, ticket{};
    ScanRange range;
    std::optional<Hash256> reviewed_block;
    bool read_descriptors{};
};
struct ScanReply {
    std::optional<ScanObservation> observation;
    std::optional<ScanReview> review;
    std::optional<int64_t> last_scanned;
    bool dispatched{}, complete{};
    std::string error;
};
bool CanObserveScan(const BackendCapabilities&);
bool CanStartScan(const BackendCapabilities&);
std::string ValidateScanRange(const ScanObservation&, ScanRange);
std::optional<int64_t> ReportedScanEnd(const rpc::Json&, ScanRange);
ScanReply FetchScan(rpc::Transport&, const ScanRequest&, std::stop_token);

class RescanService {
public:
    using Fetch = std::function<ScanReply(const ScanRequest&, std::stop_token)>;
    using Lease = std::shared_ptr<storage::FileGuard>;
    struct Completion {ScanRequest request; ScanReply reply; Lease lease;};
    explicit RescanService(Fetch fetch) : fetch(std::move(fetch)) {}
    ~RescanService();
    bool Start(ScanRequest, const ConnectionService&, Lease = {});
    void Cancel();
    bool Busy() const {return worker.joinable();}
    std::optional<Completion> Poll(const ConnectionService*, const std::optional<RefreshIdentity>&);
private:
    const std::thread::id owner{std::this_thread::get_id()};
    Fetch fetch;
    std::optional<ScanRequest> running;
    Lease lease;
    bool retired{};
    std::optional<ScanReply> completed;
    std::mutex mutex;
    std::jthread worker;
};
}
