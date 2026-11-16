// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "model.h"

#include <algorithm>
#include <limits>
#include <set>

namespace roots::gui {
namespace {
constexpr int64_t MAX_MONEY = 21000000LL * 100000000;
bool Hex(std::string_view value, bool empty = false)
{
    if ((!empty && value.empty()) || value.size() % 2) return false;
    return std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}
bool Amount(Money value) { return value.sats() >= 0 && value.sats() <= MAX_MONEY; }
std::string Key(const Outpoint& outpoint) { return outpoint.txid.hex() + ':' + std::to_string(outpoint.index); }
bool Accumulate(std::optional<Money>& total, std::optional<Money> amount)
{
    if (!total || !amount) { total.reset(); return true; }
    auto result = total->Add(*amount);
    if (!result || !Amount(*result)) return false;
    total = result;
    return true;
}
bool Mutable(TransactionState state)
{
    return state == TransactionState::Editable || state == TransactionState::Reviewed ||
           state == TransactionState::AwaitingSignatures || state == TransactionState::PartiallySigned ||
           state == TransactionState::Complete || state == TransactionState::ReadyToBroadcast;
}
} // namespace

std::optional<Hash256> Hash256::Parse(std::string_view hex)
{
    if (hex.size() != 64 || !Hex(hex)) return std::nullopt;
    std::string normalized(hex);
    for (char& c : normalized) if (c >= 'A' && c <= 'F') c += 'a' - 'A';
    return Hash256(std::move(normalized));
}
bool Provenance::IsLiveFor(const RequestContext& current) const
{
    return context == current && freshness == Freshness::Live && observed_unix_ms.has_value();
}
std::optional<BalanceTotals> CalculateBalances(const std::vector<CoinBalance>& coins)
{
    BalanceTotals result;
    Money known_total = Money::Satoshis(0);
    std::set<std::string> seen;
    for (const auto& coin : coins) {
        if (!seen.insert(Key(coin.outpoint)).second || (coin.amount && !Amount(*coin.amount)) ||
            (coin.confirmation == Confirmation::Immature && coin.availability == Availability::Spendable)) return std::nullopt;
        if (coin.amount) {
            const auto sum = known_total.Add(*coin.amount);
            if (!sum || !Amount(*sum)) return std::nullopt;
            known_total = *sum;
        }
        if (!Accumulate(result.total, coin.amount)) return std::nullopt;
        auto& confirmed = coin.confirmation == Confirmation::Confirmed ? result.confirmed :
                          coin.confirmation == Confirmation::Unconfirmed ? result.unconfirmed : result.immature;
        if (!Accumulate(confirmed, coin.amount)) return std::nullopt;
        std::optional<Money>* available = nullptr;
        switch (coin.availability) {
        case Availability::Frozen: available = &result.frozen; break;
        case Availability::Reserved: available = &result.reserved; break;
        case Availability::Spendable: available = &result.spendable; break;
        case Availability::Unavailable: break;
        }
        if (available && !Accumulate(*available, coin.amount)) return std::nullopt;
    }
    return result;
}
bool ApprovalIdentity::ValidShape() const
{
    if (inputs.empty() || outputs.empty()) return false;
    std::set<std::string> seen;
    for (const auto& input : inputs) {
        if (!seen.insert(Key(input.outpoint)).second || !Hex(input.script_sig, true) ||
            (input.prevout_amount && !Amount(*input.prevout_amount)) ||
            (input.prevout_script && !Hex(*input.prevout_script, true))) return false;
        if (input.sighash && (*input.sighash > 0x83 || *input.sighash == 0x80 ||
            ((*input.sighash & 0x7f) > 3))) return false;
    }
    Money total = Money::Satoshis(0);
    for (const auto& output : outputs) {
        if (!Amount(output.amount) || !Hex(output.script, true)) return false;
        const auto next = total.Add(output.amount);
        if (!next || !Amount(*next)) return false;
        total = *next;
        if (!output.change && output.change_proof) return false;
    }
    return true;
}
std::optional<Money> ApprovalIdentity::Fee() const
{
    if (!ValidShape()) return std::nullopt;
    Money incoming = Money::Satoshis(0), outgoing = Money::Satoshis(0);
    for (const auto& input : inputs) {
        if (!input.prevout_amount) return std::nullopt;
        auto sum = incoming.Add(*input.prevout_amount);
        if (!sum || !Amount(*sum)) return std::nullopt;
        incoming = *sum;
    }
    for (const auto& output : outputs) outgoing = *outgoing.Add(output.amount);
    auto fee = incoming.Subtract(outgoing);
    return fee && fee->sats() >= 0 ? fee : std::nullopt;
}
bool ApprovalIdentity::CanApprove() const
{
    if (!ValidShape() || wallet_policy.empty() || !Fee()) return false;
    for (const auto& input : inputs) if (!input.prevout_amount || !input.prevout_script || !input.sighash) return false;
    for (const auto& output : outputs) {
        if (output.change && (!output.change_proof || output.change_proof->descriptor.empty() ||
            output.change_proof->derivation.empty() || output.change_proof->derived_script != output.script)) return false;
    }
    return true;
}
TransactionDocument::TransactionDocument(RequestContext initial) : m_context(std::move(initial)) {}
RequestContext TransactionDocument::context() const { return m_context; }
bool TransactionDocument::Accepts(const RequestContext& request) const { return !m_locked && request == m_context; }
const ApprovalIdentity* TransactionDocument::visible_draft() const { return !m_locked && m_draft ? &*m_draft : nullptr; }
const FinalizationEvidence* TransactionDocument::visible_finalization() const { return !m_locked && m_finalization ? &*m_finalization : nullptr; }
bool TransactionDocument::AdvanceRevision()
{
    if (m_context.revision.value == std::numeric_limits<uint64_t>::max()) return false;
    ++m_context.revision.value;
    return true;
}
void TransactionDocument::RevokeApproval()
{
    m_approval.reset();
    m_finalization.reset();
    m_state = TransactionState::Editable;
}
bool TransactionDocument::Edit(const RequestContext& request, ApprovalIdentity candidate)
{
    if (!Accepts(request) || !Mutable(m_state) || !candidate.ValidShape() ||
        candidate.network != m_context.network || candidate.wallet != m_context.wallet) return false;
    if (m_draft && candidate == *m_draft) return true;
    if (!AdvanceRevision()) return false;
    m_draft = std::move(candidate);
    RevokeApproval();
    return true;
}
bool TransactionDocument::Review(const RequestContext& request)
{
    if (!Accepts(request) || m_state != TransactionState::Editable || !m_draft) return false;
    m_state = TransactionState::Reviewed;
    return true;
}
bool TransactionDocument::Approve(const RequestContext& request)
{
    if (!Accepts(request) || m_state != TransactionState::Reviewed || !m_draft || !m_draft->CanApprove()) return false;
    m_approval = m_draft;
    m_state = TransactionState::AwaitingSignatures;
    return true;
}
bool TransactionDocument::RecordSatisfaction(const RequestContext& request, const std::vector<Satisfaction>& inputs)
{
    if (!Accepts(request) || !m_approval || m_approval != m_draft || inputs.size() != m_draft->inputs.size() ||
        (m_state != TransactionState::AwaitingSignatures && m_state != TransactionState::PartiallySigned &&
         m_state != TransactionState::Complete)) return false;
    const auto complete = std::count(inputs.begin(), inputs.end(), Satisfaction::VerifiedComplete);
    m_state = static_cast<size_t>(complete) == inputs.size() ? TransactionState::Complete :
              complete ? TransactionState::PartiallySigned : TransactionState::AwaitingSignatures;
    return true;
}
bool TransactionDocument::RecordFinalization(const RequestContext& request, FinalizationEvidence evidence)
{
    if (!Accepts(request) || m_state != TransactionState::Complete || !m_approval || m_approval != m_draft ||
        evidence.serialized.empty() || !evidence.virtual_bytes || evidence.virtual_bytes > evidence.serialized.size() ||
        m_draft->Fee() != evidence.fee) return false;
    m_finalization = std::move(evidence);
    m_state = TransactionState::ReadyToBroadcast;
    return true;
}
bool TransactionDocument::BeginSubmission(const RequestContext& request)
{
    if (!Accepts(request) || m_state != TransactionState::ReadyToBroadcast || !m_finalization) return false;
    m_state = TransactionState::Submitting;
    return true;
}
bool TransactionDocument::SubmissionTimedOut(const RequestContext& request)
{
    if (!Accepts(request) || m_state != TransactionState::Submitting) return false;
    m_state = TransactionState::SubmissionUncertain;
    return true;
}
bool TransactionDocument::Observe(const RequestContext& request, const Hash256& txid, TransactionState observed)
{
    if (!Accepts(request) || !m_finalization || txid != m_finalization->txid || Mutable(m_state)) return false;
    if (observed != TransactionState::InMempool && observed != TransactionState::Confirmed &&
        observed != TransactionState::Replaced && observed != TransactionState::Conflicted) return false;
    if (m_state == TransactionState::Replaced || m_state == TransactionState::Conflicted) return false;
    m_state = observed;
    return true;
}
bool TransactionDocument::Reconnect(BackendSessionId backend)
{
    if (m_locked || backend == m_context.backend || !AdvanceRevision()) return false;
    m_context.backend = std::move(backend);
    if (Mutable(m_state)) RevokeApproval();
    else if (m_state == TransactionState::Submitting) m_state = TransactionState::SubmissionUncertain;
    return true;
}
bool TransactionDocument::Lock()
{
    m_locked = true;
    if (Mutable(m_state)) RevokeApproval();
    else if (m_state == TransactionState::Submitting) m_state = TransactionState::SubmissionUncertain;
    if (m_context.lock_epoch.value == std::numeric_limits<uint64_t>::max()) { m_epoch_exhausted = true; return false; }
    ++m_context.lock_epoch.value;
    return true;
}
bool TransactionDocument::Unlock()
{
    if (m_epoch_exhausted) return false;
    m_locked = false;
    return true;
}
} // namespace roots::gui
