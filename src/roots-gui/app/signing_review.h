// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "app/document_registry.h"
#include "app/child_package.h"

namespace roots::gui::app {
struct CancellationIntent {
    std::shared_ptr<const protocol::TransactionDocument> original;
    Money original_fee;
    protocol::OfflineChange owned_destination;
    bool operator==(const CancellationIntent&) const=default;
};

// Derived from the live registry and independent public evidence. Request data
// and recovery records cannot supply approvals or decide the active context.
struct SigningBasis {
    uint64_t document{},origin{},wallet_document{},session{},profile{};
    DraftRevision document_revision,origin_revision;
    LockEpoch epoch;
    bool offline{};
    WalletBinding wallet;
    std::shared_ptr<const protocol::TransactionDocument> content;
    protocol::OfflineReviewEvidence proof;
    std::optional<size_t> change_output;
    Hash256 proof_digest;
    std::optional<Hash256> observed_policy, coin_policy;
    std::vector<uint32_t> sighash;
    bool requires_ack{};
    std::optional<CancellationIntent> cancellation;
    std::optional<ChildPackageIntent> child_package;
    bool operator==(const SigningBasis&) const=default;
};
struct SigningApproval {
    SigningBasis reviewed;
    Hash256 commitment;
    Hash256 original_source;
    uint64_t generation{};
    bool operator==(const SigningApproval&) const=default;
};
struct ApprovedFinalTransaction {
    SigningApproval approval;
    std::shared_ptr<const protocol::TransactionDocument> collected;
    Hash256 collected_source;
    Hash256 final_source;
    protocol::FinalTransaction final;
};
struct ApprovedFinalization {
    std::optional<ApprovedFinalTransaction> artifact;
    std::string error;
};
std::optional<Hash256> SigningDigest(std::string_view);
std::string ReviewDifference(const SigningBasis&,const SigningBasis&);
std::optional<protocol::OfflineReviewEvidence> FundingReviewEvidence(const Document&,const ImportedDocument&);

// Owner-thread session state. This type is absent from public recovery and all
// import codecs. The current context must be supplied again at every boundary.
class Replacements;
class SigningReviews {
    friend class Replacements;
    bool ApproveCurrent(const SigningBasis&);
    struct Entry {
        std::optional<SigningBasis> acknowledged;
        std::optional<SigningApproval> reviewed;
        bool approved{};
        std::string reason;
    };
    std::map<uint64_t,Entry> entries;
    uint64_t last_generation{};
public:
    bool Acknowledged(const SigningBasis&) const;
    bool Acknowledge(SigningBasis);
    bool Record(SigningBasis);
    bool Approve(const SigningBasis&);
    const SigningApproval* Recorded(const SigningBasis&) const;
    const SigningApproval* Authorize(const SigningBasis&) const;
    ApprovedFinalization Finalize(const SigningBasis&,std::shared_ptr<const protocol::TransactionDocument>) const;
    bool HasReview(uint64_t document) const;
    bool Revalidate(const SigningBasis&);
    bool Revoke(uint64_t,std::string);
    void RetireAll(std::string_view);
    void RemoveClosed(const DocumentRegistry&);
    std::vector<uint64_t> PendingDocuments() const;
    std::string_view Reason(uint64_t) const;
};
}
