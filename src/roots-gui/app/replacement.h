// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "app/coin_controls.h"
#include "app/signing_review.h"

namespace roots::gui::app {
struct SelectedParentOutput {
    uint32_t index{};
    Money amount;
    std::string script;
    bool operator==(const SelectedParentOutput&) const=default;
};
struct ReplacementScope {
    RefreshIdentity identity;
    uint64_t document{}, profile{}, generation{}, sequence{}, query{}, policy_revision{};
    DraftRevision revision;
    Hash256 txid;
    std::optional<Hash256> observed_witness;
    std::string tip, original_hex;
    uint32_t rate_per_kvb{};
    FeeDraftKind kind;
    std::optional<SelectedParentOutput> parent_output;
    bool operator==(const ReplacementScope&) const = default;
};
struct ReplacementDraft {
    ReplacementScope scope;
    std::shared_ptr<const protocol::TransactionDocument> original, returned, working;
    CoinDraft coins;
    protocol::OfflineReviewEvidence proof;
    std::optional<size_t> change_output;
    Money original_fee, original_change, proposed_change;
    std::optional<CancellationIntent> cancellation;
    std::optional<ChildPackageIntent> child_package;
};
struct ReplacementResult {
    std::optional<ReplacementDraft> draft;
    std::string reason;
};

// Applies existing freeze/reservation/daemon-lock policy to both the original
// spent inputs and independently eligible additional confirmed inputs.
// Empty means the current exact original remains eligible. Read-only and
// bounded; use at cancellation approval, signing and submission boundaries.
std::string CheckFeeRelationship(rpc::Transport&,const ServerCandidate&,
    const std::optional<CancellationIntent>&,const std::optional<ChildPackageIntent>&,std::stop_token);

bool ReplacementInputsAllowed(const ReplacementDraft&,const Document&,const RefreshView&,RecoverySession&,CoinControls&);

// No RPC authority is retained by a draft or written to public recovery.
class Replacements {
public:
    Replacements(rpc::Transport& rpc, ConnectionService& node) : transport(rpc), connection(node) {}
    ~Replacements();
    bool Busy() const { return worker.joinable(); }
    const std::optional<ReplacementScope>& Running() const { return running; }
    std::string Unavailable(const DocumentRegistry&, const RefreshService&, const RecoverySession&,
        const FeeDraftRequest&) const;
    Receipt Start(const DocumentRegistry&, const RefreshService&, RecoverySession&, CoinControls&, const FeeDraftRequest&);
    bool Cancel();
    const SigningBasis* ApprovalBasis() const {return approval ? &approval->basis : nullptr;}
    Receipt StartApproval(const SigningReviews&,const SigningBasis&);
    std::optional<Receipt> PollApproval(SigningReviews&,const SigningBasis* current);
    std::optional<ReplacementResult> Poll(const DocumentRegistry&, const RefreshService&,
        RecoverySession&, CoinControls&, bool visible);
private:
    const std::thread::id owner{std::this_thread::get_id()};
    rpc::Transport& transport;
    ConnectionService& connection;
    std::optional<ReplacementScope> running;
    FeeDraftKind local_job{FeeDraftKind::ReplaceByFee};
    struct ApprovalCheck {SigningBasis basis;uint64_t generation;bool retired{};};
    std::optional<ApprovalCheck> approval;
    std::optional<ReplacementResult> completed;
    std::mutex mutex;
    std::jthread worker;
    bool ApprovalCurrent(const SigningBasis&) const;
    std::optional<ReplacementResult> TakeCompletion();
    std::optional<ReplacementScope> Scope(const DocumentRegistry&, const RefreshService&,
        const RecoverySession&, const FeeDraftRequest&, uint64_t policy_revision) const;
};
}
