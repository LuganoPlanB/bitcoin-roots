// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "domain/receive.h"
#include "services/device_signers.h"
#include "services/refresh.h"

namespace roots::gui {
enum class ReceiveAction { Next, Reserve, Inspect, VerifyDevice };
struct ReceiveRequest {
    ReceiveAction action;
    RefreshIdentity identity;
    uint64_t profile_revision{}, ticket{};
    std::optional<ReceiveTarget> selected;
    SelectedDevice device{};
};
struct ReceiveReply {
    std::optional<ReceiveTarget> address;
    bool allocation_dispatched{}, inspected{}, signer_echo{};
    std::string error;
};
bool CanAllocateReceive(const BackendCapabilities&);
bool CanVerifyReceiveOnDevice(const BackendCapabilities&);
bool ReceiveAllocation(ReceiveAction);
ReceiveReply FetchReceive(rpc::Transport&, const ReceiveRequest&, std::stop_token);
std::optional<std::string> ReceiveUri(std::string_view address, std::string_view label, std::optional<Money> amount = {});
std::optional<int64_t> ReceiveLastUsed(const HistoryLedger&, const ReceiveTarget&);

// Only the current owner may adopt results. Durable allocation admission and
// consumption belong to the local metadata store, never this transport worker.
class ReceiveService {
public:
    using Fetch = std::function<ReceiveReply(const ReceiveRequest&, std::stop_token)>;
    explicit ReceiveService(Fetch fetch) : fetch(std::move(fetch)) {}
    ~ReceiveService();
    bool Start(ReceiveRequest, const ConnectionService&);
    void Cancel();
    bool CancelDevice();
    bool Busy() const {return worker.joinable();}
    std::optional<std::pair<ReceiveRequest, ReceiveReply>> Poll(
        const ConnectionService*, const std::optional<RefreshIdentity>&, const std::optional<ReceiveTarget>&);
private:
    const std::thread::id owner{std::this_thread::get_id()};
    Fetch fetch;
    std::optional<ReceiveRequest> running;
    bool retired{};
    std::optional<ReceiveReply> completed;
    std::mutex mutex;
    std::jthread worker;
};
}
