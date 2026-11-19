// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "app/commands.h"
#include "app/signing_review.h"
#include "adapters/storage/metadata.h"
#include "services/capabilities.h"

namespace roots::gui::app {
inline constexpr int64_t BROADCAST_MAX_SAT_PER_KVB=100000;
enum class BroadcastAction { Check, Submit, Rebroadcast };
enum class BroadcastStage { Idle, Checking, Ready, Rejected, Rechecking, Sending, Acknowledged, Refused, Unsent, Uncertain, Cancelled, Recorded,
    CheckingStatus, StatusObserved, StatusUnknown };
enum class TransactionLocation { Unknown, Mempool, ActiveChain, CompetingSpend, NoLongerObserved };
struct TransactionObservation {
    TransactionLocation location{TransactionLocation::Unknown};
    std::string tip;
    std::optional<std::string> active_block;
    bool former_block_removed{};
};
struct BroadcastView {
    uint64_t document{};
    BroadcastStage stage{BroadcastStage::Idle};
    std::string message;
    std::optional<TransactionObservation> observation;
    bool original_retired{};
};
struct BroadcastStatusContext {
    uint64_t document{},session{},profile{};
    DraftRevision revision;
    LockEpoch epoch;
    NetworkIdentity network;
    std::shared_ptr<const protocol::TransactionDocument> raw;
    bool operator==(const BroadcastStatusContext&) const=default;
};
std::string BroadcastRecordLabel(const storage::BroadcastRecord&);

// All state changes and the durable write stay on the owner thread. Workers
// borrow immutable request values and never access documents or local storage.
class Broadcaster {
public:
    Broadcaster(rpc::Transport& transport,ConnectionService& connection):transport(transport),connection(connection){}
    ~Broadcaster();
    const BroadcastView& View() const {return view;}
    bool Busy() const {return worker.joinable();}
    bool Freezes(uint64_t document,const protocol::TransactionDocument* content=nullptr) const;
    Availability Can(BroadcastAction,const SigningApproval*,const ApprovedFinalTransaction*,const RecoverySession*) const;
    Receipt Start(BroadcastAction,const SigningReviews&,const SigningBasis&,const ApprovedFinalTransaction&,
        RecoverySession&);
    Availability CanCheckStatus(const BroadcastStatusContext&,const RecoverySession*) const;
    Receipt StartStatus(const BroadcastStatusContext&,RecoverySession&,bool monitor=false);
    void PollStatus(const std::optional<BroadcastStatusContext>&,RecoverySession*,const std::optional<std::string>& observed_tip={});
    bool StatusActive() const {return status.has_value();}
    bool Monitoring(uint64_t document) const {return status && status->continuous && status->scope.document==document;}
    bool StopMonitoring();
    void Poll(const SigningReviews&,const SigningApproval*,const ApprovedFinalTransaction*,RecoverySession*);
    bool Cancel();
private:
    using TimePoint=std::chrono::steady_clock::time_point;
    struct Outcome { BroadcastStage stage; std::string message; std::optional<TransactionObservation> observation; bool original_retired{}; };
    struct StatusSession {
        BroadcastStatusContext scope;
        ServerCandidate target;
        bool continuous{},seen_mempool{},paused{};
        std::optional<std::string> prior_block, refresh_tip;
        std::optional<Outcome> last;
        TimePoint observed,began,launched,next_due;
    };
    using Work=std::function<Outcome(std::stop_token)>;
    bool Launch(BroadcastStage,Work);
    bool Current(const SigningBasis&) const;
    bool Current(const BroadcastStatusContext&) const;
    bool Bound(const ApprovedFinalTransaction&) const;
    Receipt LaunchStatus(const BroadcastStatusContext&,RecoverySession&);
    void ShowCachedStatus(std::string_view why);
    void RetireWorker();
    void Finish(Outcome);
    void FinishSend(Outcome,RecoverySession*);
    std::optional<Outcome> TakeCompletion();
    const std::thread::id owner{std::this_thread::get_id()};
    rpc::Transport& transport;
    ConnectionService& connection;
    std::optional<ApprovedFinalTransaction> attempt;
    std::optional<uint64_t> previous_serial;
    std::optional<std::pair<storage::BroadcastAttempt,uint64_t>> sending;
    std::shared_ptr<storage::FileGuard> send_lease;
    std::optional<StatusSession> status;
    BroadcastView view;
    std::chrono::steady_clock::time_point checked;
    bool retired{};
    std::optional<std::pair<NetworkIdentity,Hash256>> recorded;
    std::mutex mutex;
    std::optional<Outcome> completed;
    std::jthread worker;
};
}
