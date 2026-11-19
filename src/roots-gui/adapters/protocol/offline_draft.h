// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "adapters/protocol/transaction.h"

namespace roots::gui::protocol {
inline constexpr size_t MAX_OFFLINE_INPUTS = 16;
inline constexpr size_t MAX_OFFLINE_PREVIOUS = 32768;
struct OfflineCoinProof {
    std::string previous_hex;
    std::string descriptor; // exact concrete public policy from scoped eligible inventory
    std::string redeem_script,witness_script;
    int64_t observed_ms{};
};
struct OfflineInput {
    Outpoint point;
    std::optional<Money> value;
    std::string script;
    std::optional<OfflineCoinProof> proof;
};
struct OfflineChange {
    std::string descriptor;
    uint32_t index{};
    int64_t observed_ms{};
    bool operator==(const OfflineChange&) const=default;
};
struct OfflineRecipient {
    std::string address;
    Money amount;
};
struct OfflineDraft {
    DisplayNetwork network;
    std::vector<OfflineInput> inputs;
    std::vector<OfflineRecipient> recipients;
    Money fee;
    std::optional<OfflineChange> change;
};
struct OfflineReviewInput {
    Outpoint point;
    Money value;
    std::string descriptor;
    bool operator==(const OfflineReviewInput&) const=default;
};
struct OfflineReviewEvidence {
    DisplayNetwork network;
    std::vector<OfflineReviewInput> inputs;
    std::vector<TransactionOutput> recipients;
    Money fee;
    std::optional<OfflineChange> change;
    bool operator==(const OfflineReviewEvidence&) const=default;
};
struct OfflineReview {
    std::optional<Hash256> commitment;
    std::optional<size_t> change_output;
    std::string error;
};
struct OfflineResult {
    std::shared_ptr<const TransactionDocument> document;
    std::string error;
    size_t satisfied{}, required{};
    std::optional<OfflineReviewEvidence> review_evidence;
    std::string review_conflict;
};
struct PublicSignature {
    std::string key;
    bool verified{};
};
struct InputSignatureProgress {
    size_t threshold{};
    std::vector<PublicSignature> signers;
    std::string unknown;
    size_t Verified() const;
    bool Satisfied() const;
};
struct SignatureProgress {
    std::vector<InputSignatureProgress> inputs;
    std::string error;
    bool Complete() const;
};
SignatureProgress InspectSignatures(const TransactionDocument&);
bool ValidOfflineCoin(const OfflineInput&, DisplayNetwork);
OfflineResult ConstructOfflineDraft(const OfflineDraft&);
OfflineResult MergeOfflineSignatures(const TransactionDocument& current, const TransactionDocument& external);
OfflineResult CombineVerifiedCollections(const TransactionDocument& current, const TransactionDocument& signed_reply);
OfflineResult ApplyDaemonPsbt(const TransactionDocument& current,const TransactionDocument& reply,bool signing);
OfflineResult FinalizeOfflineDraft(const TransactionDocument& current);
struct FinalTransaction {
    std::shared_ptr<const TransactionDocument> document;
    size_t size{}, stripped_size{}, weight{}, vsize{};
    Money fee;
};
struct FinalizationResult {
    std::optional<FinalTransaction> final;
    std::string error;
};
FinalizationResult FinalizeReviewedDraft(const TransactionDocument& original,const TransactionDocument& collected,
    const OfflineReviewEvidence&,std::optional<size_t> change_position);
// Requires evidence from the scoped wallet and intended payment, independent
// of every origin or change hint contained in the PSBT.
OfflineReview InspectOfflineReview(const TransactionDocument&,const OfflineReviewEvidence&,
    std::optional<size_t> change_position={});
}
