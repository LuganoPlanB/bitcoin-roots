// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include "services/custody_extensions.h"

namespace roots::gui {

struct PayjoinTarget {
    std::string provider, endpoint, version, recipient_script;
    bool operator==(const PayjoinTarget&) const = default;
};
enum class PayjoinDisclosure { None, UnsignedPaymentAndPreviousTransactions };
struct NegotiationOwner {
    CustodyScope scope;
    uint64_t document, ticket;
    Hash256 source, tip;
    PayjoinTarget target;
    PayjoinDisclosure disclosure;
    protocol::OfflineReviewEvidence original_proof;
    std::optional<size_t> change_output;
    size_t receiver_output;
    Money maximum_fee;
    bool operator==(const NegotiationOwner&) const = default;
};
struct NegotiationPermission {
    NegotiationOwner owner;
    int64_t valid_until_ms;
    bool confirmed;
    bool operator==(const NegotiationPermission&) const = default;
};
struct NegotiationIntent {
    NegotiationOwner owner;
    std::string original_source;
};

// This is the only payload suitable for a future provider. The GUI binds it
// to a transient local owner separately. No wallet/profile IDs, key origins,
// private keys, labels, original metadata, or signing rights cross this port.
struct PayjoinOffer {
    PayjoinTarget target;
    std::string unsigned_psbt;
};
struct PayjoinProposal {
    PayjoinTarget target;
    Hash256 offer_digest;
    std::string unsigned_psbt;
};
class PayjoinModule {
public:
    virtual ~PayjoinModule() = default;
    virtual std::string_view Identity() const = 0;
    virtual bool Qualified(const PayjoinTarget&, NetworkIdentity) const = 0;
    // Only a future authorized one-shot operation may call this. No redirect,
    // DNS/route fallback, auto-retry, or implicit transport from navigation.
    virtual std::optional<PayjoinProposal> Exchange(const PayjoinOffer&, std::stop_token) const = 0;
};
struct NegotiationReply {
    NegotiationOwner owner; // Local operation receipt. Never taken from the wire.
    PayjoinProposal proposal;
};
struct CounterpartyEvidence {
    CustodyScope scope;
    Hash256 tip;
    protocol::OfflineInput input; // Independently obtained raw prevout and concrete public policy.
};

ExtensionStatus PayjoinStatus(const ExtensionRegistry&, const PayjoinTarget* = nullptr,
    std::optional<NetworkIdentity> = {});
std::optional<Hash256> PaymentSourceIdentity(std::string_view);
std::optional<PayjoinOffer> PreparePayjoinOffer(const ExtensionRegistry&, const NegotiationIntent&,
    const NegotiationPermission&, const NegotiationOwner& current, int64_t now_ms, std::stop_token = {});

class NegotiatedDraft {
public:
    const NegotiationOwner& Owner() const { return owner; }
    const protocol::TransactionDocument& Original() const { return *original; }
    const protocol::TransactionDocument& Proposal() const { return *proposal; }
    const std::shared_ptr<const protocol::TransactionDocument>& Working() const { return working; }
    const protocol::OfflineReviewEvidence& Proof() const { return proof; }
    bool Current(const ExtensionRegistry&, const NegotiationPermission&, const NegotiationOwner&, int64_t now_ms) const;
private:
    friend std::optional<NegotiatedDraft> ValidatePayjoinReply(const ExtensionRegistry&, const NegotiationIntent&,
        const NegotiationPermission&, const NegotiationOwner&, const NegotiationReply&, const CounterpartyEvidence&, int64_t, std::stop_token);
    NegotiatedDraft(NegotiationOwner, std::shared_ptr<const protocol::TransactionDocument> original,
        std::shared_ptr<const protocol::TransactionDocument> proposal, std::shared_ptr<const protocol::TransactionDocument> working,
        protocol::OfflineReviewEvidence, int64_t observed_ms);
    NegotiationOwner owner;
    std::shared_ptr<const protocol::TransactionDocument> original, proposal, working;
    protocol::OfflineReviewEvidence proof;
    int64_t observed_ms;
};

// No dispatch. Independently checks the current unsigned proposal and keeps
// original/proposal/working bytes separately. A caller has to create a new
// review. Neither the offer nor proposal can carry a collected signature.
std::optional<NegotiatedDraft> ValidatePayjoinReply(const ExtensionRegistry&, const NegotiationIntent&,
    const NegotiationPermission&, const NegotiationOwner& current, const NegotiationReply&,
    const CounterpartyEvidence&, int64_t now_ms, std::stop_token = {});

enum class ContactDisclosure { None, PublicCodeAndNotificationLink };
struct PaymentCodeContact {
    CustodyScope scope;
    std::string provider;
    PaymentCodeReceivePolicy code;
    Hash256 contact;
};
struct ContactPermission {
    CustodyScope scope;
    std::string provider, endpoint;
    Hash256 contact, code;
    uint64_t ticket;
    uint8_t version;
    ContactDisclosure disclosure;
    bool confirmed;
    bool operator==(const ContactPermission&) const = default;
};
class PaymentCodeContacts {
public:
    virtual ~PaymentCodeContacts() = default;
    virtual std::string_view Identity() const = 0;
    virtual bool Qualified(uint8_t version, NetworkIdentity) const = 0;
    // Locally checks the canonical public code, its network and version, and
    // authenticated contact proof. This operation may not contact a service.
    // Derivation and notification preparation require separately qualified
    // transitions and ordinary fresh transaction review.
    virtual bool Verify(const PaymentCodeContact&, std::stop_token) const = 0;
};
ExtensionStatus PaymentCodeContactStatus(const ExtensionRegistry&, const PaymentCodeContact* = nullptr);
bool CurrentPaymentCodeContact(const ExtensionRegistry&, const PaymentCodeContact&, const ContactPermission&,
    const ContactPermission& current, std::stop_token = {});

class FiatRateProvider {
public:
    virtual ~FiatRateProvider() = default;
    virtual std::string_view Identity() const = 0;
    virtual bool QualifiedCurrency(std::string_view) const = 0;
};
class ReleaseUpdateProvider {
public:
    virtual ~ReleaseUpdateProvider() = default;
    virtual std::string_view Identity() const = 0;
    virtual bool QualifiedChannel(std::string_view) const = 0;
};
ExtensionStatus FiatRateStatus(const ExtensionRegistry&, std::string_view currency = {});
ExtensionStatus ReleaseUpdateStatus(const ExtensionRegistry&, std::string_view channel = {});
struct OptionalPort { std::string_view id; bool linked; };
std::array<OptionalPort,9> OptionalPorts(const ExtensionRegistry&);

} // namespace roots::gui
