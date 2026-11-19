// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/signing_review.h"
#include "app/recovery.h"
#include <algorithm>
#include <limits>
#include <sodium.h>

namespace roots::gui::app {
std::optional<Hash256> SigningDigest(std::string_view data)
{
    std::array<unsigned char,crypto_hash_sha256_BYTES> hash{};
    if (crypto_hash_sha256(hash.data(),reinterpret_cast<const unsigned char*>(data.data()),data.size())!=0) return {};
    return Hash256::Parse(protocol::TransactionHex({reinterpret_cast<const char*>(hash.data()),hash.size()}));
}

std::optional<protocol::OfflineReviewEvidence> FundingReviewEvidence(const Document& draft,const ImportedDocument& imported)
{
    if (!imported.funding || !imported.parsed || !imported.parsed->transaction) return {};
    const auto& funding=*imported.funding;
    if (draft.id!=funding.draft_document || draft.draft_revision!=funding.draft_revision || !draft.complete
        || draft.observation==ObservationState::Locked || !draft.SendWallet()
        || *draft.SendWallet()!=funding.evidence.wallet || !MatchesSendDraft(draft,imported)) return {};
    const auto& tx=*imported.parsed->transaction;
    protocol::OfflineReviewEvidence evidence{imported.network,{}, {}, funding.fee,funding.evidence.offline_change};
    for (const auto& input:funding.evidence.inputs) {
        if (!input.value || !input.offline) return {};
        evidence.inputs.push_back({input.outpoint,*input.value,input.offline->descriptor});
    }
    // Exact scripts/order and fixed amounts were checked against the separate
    // Send draft. Its sole Max was resolved by the accepted funding result.
    evidence.recipients.assign(tx.outputs.begin(),tx.outputs.begin()+draft.recipients.rows.size());
    return evidence;
}

std::string ReviewDifference(const SigningBasis& before,const SigningBasis& after)
{
    if (before.wallet!=after.wallet || before.wallet_document!=after.wallet_document || before.proof.network!=after.proof.network)
        return "Signing wallet or network changed.";
    if (before.epoch!=after.epoch || before.session!=after.session || before.profile!=after.profile || before.offline!=after.offline)
        return "Signing session, connection, or lock context changed.";
    if (!before.content || !after.content) return "The current transaction is unavailable.";
    if (const auto change=protocol::CompareTransactionReview(*before.content,*after.content);!change.empty()) return change;
    if (before.proof.change!=after.proof.change || before.change_output!=after.change_output)
        return "Intended change, its position, or its independent proof changed.";
    if (before.proof.recipients!=after.proof.recipients || before.proof.fee!=after.proof.fee)
        return "The independent recipients or fee changed.";
    if (before.cancellation!=after.cancellation) return "The original payment or owned cancellation destination changed.";
    if (before.child_package!=after.child_package) return "The exact parent outpoint, ownership or package evidence changed.";
    if (before.sighash!=after.sighash) return "The reviewed sighash commitment changed.";
    if (before.observed_policy!=after.observed_policy) return "The observed wallet policy changed.";
    if (before.coin_policy!=after.coin_policy) return "Current input policy evidence changed.";
    if (before.proof.inputs!=after.proof.inputs || before.proof_digest!=after.proof_digest)
        return "An independent previous-output or wallet proof changed.";
    if (before.content!=after.content) return "The exact source changed. Review this document again.";
    if (before.document!=after.document || before.document_revision!=after.document_revision
        || before.origin!=after.origin || before.origin_revision!=after.origin_revision)
        return "The transaction or originating draft revision changed.";
    return "The current review provenance changed.";
}

bool SigningReviews::Acknowledged(const SigningBasis& basis) const
{
    if (!basis.requires_ack) return true;
    const auto entry=entries.find(basis.document);
    return entry!=entries.end() && entry->second.acknowledged==basis;
}
bool SigningReviews::Acknowledge(SigningBasis basis)
{
    if (!basis.requires_ack) return false;
    Revalidate(basis);
    entries[basis.document].acknowledged=std::move(basis);
    return true;
}
bool SigningReviews::Record(SigningBasis basis)
{
    if (!basis.content || !Acknowledged(basis) || last_generation==std::numeric_limits<uint64_t>::max()) return false;
    if (basis.child_package && basis.cancellation) return false;
    if (const auto& child=basis.child_package) {
        if (!basis.content->psbt_version || !basis.content->transaction || !child->parent
            || child->parent->psbt_version || !child->parent->transaction || !child->parent->transaction->txid
            || child->output>=child->parent->transaction->outputs.size() || basis.proof.inputs.size()!=1
            || basis.proof.recipients.size()!=1 || basis.proof.change || basis.change_output) return false;
        const auto& parent=*child->parent->transaction;
        const auto& selected=parent.outputs[child->output];
        const auto& input=basis.proof.inputs.front();
        const auto& output=basis.proof.recipients.front();
        if (input.point!=Outpoint{*parent.txid,child->output} || input.value!=selected.value || output.script!=selected.script
            || child->package.ancestors.empty() || !child->package.vsize || !child->estimated_vsize
            || child->owned_destination.observed_ms<=0) return false;
        const auto derived=protocol::DerivePublicDescriptor(child->owned_destination.descriptor,
            protocol::DocumentChain(basis.proof.network),child->owned_destination.index);
        if (!derived || derived->script!=output.script || !protocol::InspectOfflineReview(*basis.content,basis.proof).commitment)
            return false;
    }
    if (const auto& intent=basis.cancellation) {
        if (!basis.content->psbt_version || !basis.content->transaction || !intent->original
            || intent->original->psbt_version || !intent->original->transaction || basis.proof.recipients.size()!=1
            || basis.proof.change || basis.change_output || basis.proof.fee.sats()<=intent->original_fee.sats()) return false;
        const auto destination=protocol::DerivePublicDescriptor(intent->owned_destination.descriptor,
            protocol::DocumentChain(basis.proof.network),intent->owned_destination.index);
        if (!destination || destination->script!=basis.proof.recipients.front().script) return false;
        const auto& original=intent->original->transaction->inputs;
        const auto& candidate=basis.content->transaction->inputs;
        if (candidate.size()!=original.size() || !std::ranges::all_of(original,[&](const auto& input) {
            return std::ranges::find(candidate,input.outpoint,&protocol::TransactionInput::outpoint)!=candidate.end();
        })) return false;
        if (!protocol::InspectOfflineReview(*basis.content,basis.proof).commitment) return false;
    }
    const auto commitment=protocol::TransactionCommitment(*basis.content);
    const auto source=SigningDigest(basis.content->source);
    if (!commitment || !source) return false;
    auto& entry=entries[basis.document];
    entry.reviewed=SigningApproval{std::move(basis),*commitment,*source,++last_generation};
    entry.approved=false;
    entry.reason.clear();
    return true;
}
const SigningApproval* SigningReviews::Recorded(const SigningBasis& basis) const
{
    const auto entry=entries.find(basis.document);
    return entry!=entries.end() && entry->second.reviewed && entry->second.reviewed->reviewed==basis
        && Acknowledged(basis) ? &*entry->second.reviewed : nullptr;
}
bool SigningReviews::Approve(const SigningBasis& current)
{
    return !current.cancellation && !current.child_package && ApproveCurrent(current);
}
bool SigningReviews::ApproveCurrent(const SigningBasis& current)
{
    if (!Recorded(current)) return false;
    auto& entry=entries.at(current.document);
    if (entry.approved) return false;
    entry.approved=true;
    return true;
}
const SigningApproval* SigningReviews::Authorize(const SigningBasis& basis) const
{
    const auto* record=Recorded(basis);
    return record && entries.at(basis.document).approved ? record : nullptr;
}
ApprovedFinalization SigningReviews::Finalize(const SigningBasis& basis,
    std::shared_ptr<const protocol::TransactionDocument> collected) const
{
    const auto* approval=Authorize(basis);
    if (!approval || !basis.content || !collected)
        return {{},"Finalization requires the exact current signing approval and its collected PSBT."};
    const auto source=SigningDigest(collected->source);
    if (!source || SigningDigest(basis.content->source)!=approval->original_source
        || protocol::TransactionCommitment(*collected)!=approval->commitment)
        return {{},"The original or collected PSBT differs from the exact signing approval."};
    auto result=protocol::FinalizeReviewedDraft(*basis.content,*collected,basis.proof,basis.change_output);
    if (!result.final) return {{},std::move(result.error)};
    const auto final_source=SigningDigest(result.final->document->wire);
    if (!final_source) return {{},"A digest of the exact finalized bytes is unavailable."};
    return {ApprovedFinalTransaction{*approval,std::move(collected),*source,*final_source,std::move(*result.final)},{}};
}
bool SigningReviews::HasReview(uint64_t id) const
{
    const auto entry=entries.find(id);
    return entry!=entries.end() && entry->second.reviewed.has_value();
}
bool SigningReviews::Revalidate(const SigningBasis& current)
{
    const auto entry=entries.find(current.document);
    if (entry==entries.end()) return false;
    auto& data=entry->second;
    if (data.reviewed && data.reviewed->reviewed!=current) return Revoke(current.document,ReviewDifference(data.reviewed->reviewed,current));
    if (data.acknowledged && data.acknowledged!=current) return Revoke(current.document,"The acknowledged sighash context changed.");
    return false;
}
bool SigningReviews::Revoke(uint64_t id,std::string reason)
{
    const auto entry=entries.find(id);
    if (entry==entries.end()) return false;
    auto& data=entry->second;
    const bool changed=data.reviewed || data.acknowledged;
    if (changed) data.reason="Signing review revoked. "+std::move(reason);
    data.acknowledged.reset();
    data.reviewed.reset();
    data.approved=false;
    return changed;
}
void SigningReviews::RetireAll(std::string_view reason)
{
    for (auto& [id,entry]:entries) Revoke(id,std::string(reason));
}
void SigningReviews::RemoveClosed(const DocumentRegistry& documents)
{
    std::erase_if(entries,[&](const auto& item){return !documents.Find(item.first);});
}
std::vector<uint64_t> SigningReviews::PendingDocuments() const
{
    std::vector<uint64_t> ids;
    for (const auto& [id,entry]:entries) if (entry.reviewed || entry.acknowledged) ids.push_back(id);
    return ids;
}
std::string_view SigningReviews::Reason(uint64_t id) const
{
    const auto entry=entries.find(id);
    return entry==entries.end() ? std::string_view{} : std::string_view(entry->second.reason);
}
}
