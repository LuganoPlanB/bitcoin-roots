// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#ifndef ROOTS_GUI_DOMAIN_MODEL_H
#define ROOTS_GUI_DOMAIN_MODEL_H

#include <compare>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace roots::gui {

// Signed deltas and balances share an exact representation. Transaction outputs
// and prevouts have the additional nonnegative constraint enforced below.
class Money {
public:
    static constexpr Money Satoshis(int64_t value) { return Money(value); }
    template <std::floating_point T> static Money Satoshis(T) = delete;
    template <std::unsigned_integral T> requires (sizeof(T) >= sizeof(int64_t))
    static Money Satoshis(T) = delete;
    static std::optional<Money> ParseBtc(std::string_view text, char decimal = '.');
    static std::optional<Money> ParseJsonBtc(std::string_view token);
    constexpr int64_t sats() const { return m_sats; }
    std::string btc() const;
    std::optional<Money> Add(Money other) const;
    std::optional<Money> Subtract(Money other) const;
    auto operator<=>(const Money&) const = default;
private:
    explicit constexpr Money(int64_t value) : m_sats(value) {}
    int64_t m_sats;
};

class FeeRate {
public:
    // Decimal sat/vB, at most eight fractional digits; never a floating value.
    static std::optional<FeeRate> Parse(std::string_view text);
    static std::optional<FeeRate> Ratio(uint64_t satoshis, uint32_t virtual_bytes);
    std::optional<Money> Fee(uint32_t virtual_bytes) const; // rounds up once
    uint64_t numerator() const { return m_numerator; }
    uint32_t denominator() const { return m_denominator; }
    bool operator==(const FeeRate&) const = default;
private:
    FeeRate(uint64_t numerator, uint32_t denominator);
    uint64_t m_numerator;
    uint32_t m_denominator;
};

template <typename Tag> class Id {
public:
    static std::optional<Id> Make(std::string_view value)
    {
        if (value.empty() || value.size() > 256) return std::nullopt;
        for (unsigned char c : value) if (c < 32 || c == 127) return std::nullopt;
        return Id(std::string(value));
    }
    const std::string& value() const { return m_value; }
    bool operator==(const Id&) const = default;
private:
    explicit Id(std::string value) : m_value(std::move(value)) {}
    std::string m_value;
};
using BackendSessionId = Id<struct BackendSessionTag>;
using WalletId = Id<struct WalletTag>;
using DocumentId = Id<struct DocumentTag>;
// Reserved at the future account-service boundary; unused by this RPC profile.
using AccountId = Id<struct AccountTag>;

class Hash256 {
public:
    static std::optional<Hash256> Parse(std::string_view hex);
    const std::string& hex() const { return m_hex; }
    bool operator==(const Hash256&) const = default;
private:
    explicit Hash256(std::string hex) : m_hex(std::move(hex)) {}
    std::string m_hex;
};
struct NetworkIdentity {
    Hash256 genesis;
    bool operator==(const NetworkIdentity&) const = default;
};
struct Outpoint {
    Hash256 txid;
    uint32_t index;
    bool operator==(const Outpoint&) const = default;
};
struct DraftRevision {
    uint64_t value{0};
    bool operator==(const DraftRevision&) const = default;
};
struct LockEpoch {
    uint64_t value{0};
    bool operator==(const LockEpoch&) const = default;
};
struct RequestContext {
    BackendSessionId backend;
    NetworkIdentity network;
    WalletId wallet;
    DocumentId document;
    DraftRevision revision;
    LockEpoch lock_epoch;
    bool operator==(const RequestContext&) const = default;
};

enum class CapabilityReason {
    UnsupportedMethod, UnsupportedArgument, WalletDisabled, PrunedData,
    Offline, PrivacyLocked, MissingProtocolProvider, MissingAccountService,
    NoDirectDeviceTransport, Unknown,
};
class Capability {
public:
    static Capability Available() { return Capability(std::nullopt); }
    static Capability Unavailable(CapabilityReason reason) { return Capability(reason); }
    bool available() const { return !m_reason; }
    std::optional<CapabilityReason> reason() const { return m_reason; }
private:
    explicit Capability(std::optional<CapabilityReason> reason) : m_reason(reason) {}
    std::optional<CapabilityReason> m_reason;
};
enum class Freshness { Live, Cached, Stale, Offline, Unknown };
struct Provenance {
    RequestContext context;
    Freshness freshness;
    std::optional<int64_t> observed_unix_ms;
    std::optional<uint32_t> height;
    bool IsLiveFor(const RequestContext& current) const;
};

enum class Confirmation { Confirmed, Unconfirmed, Immature };
enum class Availability { Spendable, Frozen, Reserved, Unavailable };
struct CoinBalance {
    Outpoint outpoint;
    std::optional<Money> amount;
    Confirmation confirmation;
    Availability availability;
};
struct BalanceTotals {
    std::optional<Money> total{Money::Satoshis(0)};
    std::optional<Money> confirmed{Money::Satoshis(0)};
    std::optional<Money> unconfirmed{Money::Satoshis(0)};
    std::optional<Money> immature{Money::Satoshis(0)};
    std::optional<Money> frozen{Money::Satoshis(0)};
    std::optional<Money> reserved{Money::Satoshis(0)};
    std::optional<Money> spendable{Money::Satoshis(0)};
};
// Reject duplicates/negative coins/overflow and immature-spendable claims.
// Confirmation and availability are separate axes, never added to each other.
std::optional<BalanceTotals> CalculateBalances(const std::vector<CoinBalance>& coins);

struct InputCommitment {
    Outpoint outpoint;
    uint32_t sequence;
    std::string script_sig;
    std::optional<Money> prevout_amount;
    std::optional<std::string> prevout_script;
    std::optional<uint32_t> sighash;
    bool operator==(const InputCommitment&) const = default;
};
struct ChangeProof {
    std::string descriptor;
    std::string derivation;
    // An adapter-owned derivation result, not an imported PSBT change flag.
    std::string derived_script;
    bool operator==(const ChangeProof&) const = default;
};
struct OutputCommitment {
    Money amount;
    std::string script;
    bool change{false};
    std::optional<ChangeProof> change_proof;
    bool operator==(const OutputCommitment&) const = default;
};
// Compare this complete structured identity, not a PSBT container hash, label
// or key fingerprint. Cryptographic/descriptor validation belongs to the adapter.
struct ApprovalIdentity {
    NetworkIdentity network;
    WalletId wallet;
    int32_t version{2};
    uint32_t locktime{0};
    std::string wallet_policy;
    std::vector<InputCommitment> inputs;
    std::vector<OutputCommitment> outputs;
    bool operator==(const ApprovalIdentity&) const = default;
    bool ValidShape() const;
    bool CanApprove() const;
    std::optional<Money> Fee() const;
};

enum class TransactionState {
    Editable, Reviewed, AwaitingSignatures, PartiallySigned, Complete,
    ReadyToBroadcast, Submitting, SubmissionUncertain, InMempool, Confirmed,
    Replaced, Conflicted,
};
enum class Satisfaction { Unknown, Missing, VerifiedComplete };
struct FinalizationEvidence {
    Hash256 txid;
    Hash256 wtxid;
    std::vector<uint8_t> serialized;
    uint32_t virtual_bytes;
    Money fee;
};

// Pure document state: no HTTP, daemon calls, filesystem, signing or UI globals.
class TransactionDocument {
public:
    explicit TransactionDocument(RequestContext initial);
    RequestContext context() const;
    bool Accepts(const RequestContext& request) const;
    TransactionState state() const { return m_state; }
    bool locked() const { return m_locked; }
    const ApprovalIdentity* visible_draft() const;
    const FinalizationEvidence* visible_finalization() const;
    bool Edit(const RequestContext&, ApprovalIdentity);
    bool Review(const RequestContext&);
    bool Approve(const RequestContext&);
    bool RecordSatisfaction(const RequestContext&, const std::vector<Satisfaction>&);
    // Caller must recalculate hashes, size and fee from actual final bytes.
    bool RecordFinalization(const RequestContext&, FinalizationEvidence);
    bool BeginSubmission(const RequestContext&);
    bool SubmissionTimedOut(const RequestContext&);
    bool Observe(const RequestContext&, const Hash256& txid, TransactionState);
    bool Reconnect(BackendSessionId backend);
    bool Lock();
    bool Unlock();
private:
    bool AdvanceRevision();
    void RevokeApproval();
    RequestContext m_context;
    TransactionState m_state{TransactionState::Editable};
    bool m_locked{false};
    bool m_epoch_exhausted{false};
    std::optional<ApprovalIdentity> m_draft;
    std::optional<ApprovalIdentity> m_approval;
    std::optional<FinalizationEvidence> m_finalization;
};
} // namespace roots::gui
#endif // ROOTS_GUI_DOMAIN_MODEL_H
