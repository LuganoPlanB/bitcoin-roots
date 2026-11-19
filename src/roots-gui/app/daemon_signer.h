// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "app/signing_review.h"
#include "services/capabilities.h"
#include "services/device_signers.h"

namespace roots::gui::app {
enum class DaemonSignAction { Enrich, Unlock, Sign, ExternalSign };
inline constexpr unsigned DAEMON_UNLOCK_SECONDS=30;

struct DaemonSignResult {
    DaemonSignAction action;
    std::shared_ptr<const protocol::TransactionDocument> psbt;
    std::string status;
    bool success{};
    bool uncertain{};
    bool original_retired{};
};

class DaemonSigner {
public:
    DaemonSigner(rpc::Transport& transport,ConnectionService& connection)
        : transport(transport),connection(connection) {}
    ~DaemonSigner();
    bool Busy() const {return worker.joinable();}
    std::string Unavailable(const SigningBasis&,DaemonSignAction,const SelectedDevice& = {}) const;
    bool Start(const SigningReviews&,const SigningBasis&,DaemonSignAction,std::string_view passphrase={},SelectedDevice = {});
    std::optional<DaemonSignResult> Poll(const SigningApproval* current);
    void Cancel();
private:
    bool Current(const SigningBasis&) const;
    const std::thread::id owner{std::this_thread::get_id()};
    rpc::Transport& transport;
    ConnectionService& connection;
    std::optional<SigningApproval> running;
    SelectedDevice running_device;
    std::mutex mutex;
    std::optional<DaemonSignResult> completed;
    std::jthread worker;
};
}
