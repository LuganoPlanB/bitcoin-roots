// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include "services/funding.h"
#include <variant>

namespace roots::gui {
class LocalSignerService;
class DeviceTransport;
class ReusableReceiveService;
class PayjoinModule;
class PaymentCodeContacts;
class FiatRateProvider;
class ReleaseUpdateProvider;

enum class IndexAccess { Private, Public };
enum class IndexCoverage { Partial, ProviderCompleteAtTip };
enum class ConstructionStrategy { Efficiency, AvoidPartialSpends };

struct ExtensionObservation {
    std::string provider;
    NetworkIdentity network;
    Hash256 tip;
    int64_t observed_ms;
    bool operator==(const ExtensionObservation&) const = default;
};

struct HistoryIndexQuery {
    RequestContext owner;
    uint64_t session, profile, revision;
    IndexAccess access;
    // An exact outpoint or an exact public script in canonical hexadecimal.
    std::variant<Outpoint, std::string> subject;
    Hash256 tip;
    bool permission;
    bool operator==(const HistoryIndexQuery&) const = default;
};

struct IndexedOutput {
    Outpoint point;
    std::string creation_wire;
    std::optional<std::string> spending_wire;
};

struct HistoryIndexReply {
    HistoryIndexQuery query;
    ExtensionObservation observation;
    IndexCoverage coverage;
    uint32_t first_height;
    bool has_more;
    std::vector<IndexedOutput> outputs;
};

// The linked client owns endpoint authentication, exact backend/genesis
// verification, bounded replies and stop-aware I/O. Public queries can reveal
// script/outpoint links to the operator. The caller must grant permission for
// this exact query. Partial coverage never implies absence or unspent status.
class HistoryIndexAdapter {
public:
    virtual ~HistoryIndexAdapter() = default;
    virtual std::string_view Identity() const = 0;
    virtual bool Supports(IndexAccess, NetworkIdentity) const = 0;
    virtual std::optional<HistoryIndexReply> Read(const HistoryIndexQuery&, std::stop_token) const = 0;
};

struct ConstructionIntent {
    FundingScope scope;
    ConstructionStrategy strategy;
    FundingSelection selection;
    // Independent confirmed and eligible wallet observations. Exact selection
    // contains all and only the pinned inputs. Recipients have fixed amounts.
    std::vector<FundingCoin> coins;
    std::vector<protocol::TransactionOutput> recipients;
    Money maximum_fee;
    std::optional<protocol::OfflineChange> change;
};

struct ConstructionReply {
    FundingScope scope;
    ConstructionStrategy strategy;
    ExtensionObservation observation;
    std::string psbt;
};

// This port prepares unsigned data only. It receives neither keys nor signing
// authority. AvoidPartialSpends groups eligible confirmed outputs by exact
// script. It may consume more inputs and raise fees, and promises no anonymity.
class TransactionConstructionAdapter {
public:
    virtual ~TransactionConstructionAdapter() = default;
    virtual std::string_view Identity() const = 0;
    virtual bool Supports(ConstructionStrategy, NetworkIdentity) const = 0;
    virtual std::optional<ConstructionReply> Prepare(const ConstructionIntent&, std::stop_token) const = 0;
};

struct ExtensionRegistry {
    const HistoryIndexAdapter* history{};
    const TransactionConstructionAdapter* construction{};
    const LocalSignerService* local_signer{};
    const DeviceTransport* device_transport{};
    const ReusableReceiveService* reusable_receive{};
    const PayjoinModule* payjoin{};
    const PaymentCodeContacts* payment_code_contacts{};
    const FiatRateProvider* fiat_rates{};
    const ReleaseUpdateProvider* release_updates{};
};

const ExtensionRegistry& ShippedExtensions();

struct ExtensionStatus {
    bool available;
    const char* detail;
};
ExtensionStatus HistoryIndexStatus(const ExtensionRegistry&, IndexAccess, std::optional<NetworkIdentity>);
ExtensionStatus ConstructionStatus(const ExtensionRegistry&, ConstructionStrategy, std::optional<NetworkIdentity>);

// The owner supplies the currently verified tip, scope and wall-clock time.
// Returned wire proofs corroborate exact transaction relations, while coverage
// and tip provenance remain the authenticated provider's observations.
std::optional<HistoryIndexReply> ReadIndexedHistory(const ExtensionRegistry&, const HistoryIndexQuery&,
    const HistoryIndexQuery& current, int64_t now_ms, std::stop_token = {});
// A worker's result crosses this gate again with the current owner's scope
// before a view adopts it. This operation performs no provider I/O.
std::optional<HistoryIndexReply> ValidateIndexedHistory(const ExtensionRegistry&, const HistoryIndexQuery& current,
    HistoryIndexReply, int64_t now_ms, std::stop_token = {});

class PreparedConstruction {
public:
    const ConstructionReply& Provenance() const { return reply; }
    const std::shared_ptr<const protocol::TransactionDocument>& Document() const { return document; }
    const protocol::OfflineReviewEvidence& Proof() const { return proof; }
    bool Current(const FundingScope&, int64_t now_ms) const;
private:
    friend std::optional<PreparedConstruction> PrepareUnsigned(const ExtensionRegistry&, const ConstructionIntent&,
        const FundingScope&, int64_t, std::stop_token);
    PreparedConstruction(ConstructionReply reply, std::shared_ptr<const protocol::TransactionDocument> document,
        protocol::OfflineReviewEvidence proof)
        : reply(std::move(reply)), document(std::move(document)), proof(std::move(proof)) {}
    ConstructionReply reply;
    std::shared_ptr<const protocol::TransactionDocument> document;
    protocol::OfflineReviewEvidence proof;
};

// Validate against independent intent and retain exact source bytes and maps.
// Success is an inspectable proposal, with no review, signing or send rights.
std::optional<PreparedConstruction> PrepareUnsigned(const ExtensionRegistry&, const ConstructionIntent&,
    const FundingScope& current, int64_t now_ms, std::stop_token = {});

} // namespace roots::gui
