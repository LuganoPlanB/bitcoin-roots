// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include "services/extensions.h"

namespace roots::gui {

enum class SecretPurpose { MnemonicPassphrase, WorkspaceProtection, MetadataArchive, DaemonUnlock, DevicePin, DevicePassphrase };
enum class SecretOwner { LocalSigner, Workspace, Archive, DaemonWallet, DeviceTransport };
SecretOwner OwnerOf(SecretPurpose);

struct CustodyScope {
    RequestContext owner;
    uint64_t session, profile, generation;
    Hash256 policy; // Independently expected public policy identity for result adoption.
    bool operator==(const CustodyScope&) const = default;
};

enum class SeedFormat { Bip39 };
enum class WordList { Bip39English };
struct WordGrid {
    SeedFormat format;
    WordList language;
    uint8_t words, columns; // The isolated provider renders and orders the actual words.
    bool operator==(const WordGrid&) const = default;
};
enum class MnemonicPassphrase { None, IsolatedEntry };
enum class LocalSignerAction { Create, Restore, VerifyBackup };
enum class CustodyOutcome { PublicPolicy, BackupVerified, Acknowledged, Refused, Cancelled, Disconnected, TimedOut };

struct LocalSignerRequest {
    CustodyScope scope;
    uint64_t ticket; // One live workflow, including separately explicit backup verification.
    LocalSignerAction action;
    WordGrid grid;
    MnemonicPassphrase passphrase;
    bool confirmed; // Deliberate entry into the isolated workflow, never a preference or checkbox.
    bool operator==(const LocalSignerRequest&) const = default;
};

struct PublicSignerFingerprint {
    std::string master_key_fingerprint;
    Hash256 public_descriptor_commitment;
    bool operator==(const PublicSignerFingerprint&) const = default;
};
struct LocalSignerReply {
    LocalSignerRequest request;
    std::string provider;
    CustodyOutcome outcome;
    uint64_t signer_handle{}; // Provider-scoped transient handle. Never a key or a recovery field.
    std::optional<PublicSignerFingerprint> fingerprint;
    std::string public_descriptor;
};

// Only public settings and scope cross this port. The isolated signer owns
// word entry and display, mnemonic passphrase, seed persistence and verification.
// It must be independently qualified and have its own protected secret store.
class LocalSignerService {
public:
    virtual ~LocalSignerService() = default;
    virtual std::string_view Identity() const = 0;
    virtual bool ProtectedSecretStore() const = 0;
    virtual bool QualifiedVectors() const = 0;
    virtual bool Supports(const LocalSignerRequest&) const = 0;
    virtual std::optional<LocalSignerReply> BeginTrustedWorkflow(const LocalSignerRequest&, std::stop_token) const = 0;
};

enum class DeviceAction { Pin, Passphrase, CardStatus, ConfirmCardPolicy };
enum class TrustedDeviceEntry { OnDevice, IsolatedTransport };
struct DeviceTransportRequest {
    CustodyScope scope;
    Hash256 selected_device; // Bound by a verified transport selection, distinct from a short fingerprint.
    uint64_t ticket;
    DeviceAction action;
    TrustedDeviceEntry entry;
    bool confirmed;
    bool operator==(const DeviceTransportRequest&) const = default;
};
enum class CardState { Absent, Ready, Locked };
struct DeviceTransportReply {
    DeviceTransportRequest request;
    std::string provider;
    CustodyOutcome outcome;
    std::optional<CardState> card;
};

// PIN/passphrase prompts and card confirmation belong to the selected device
// or isolated transport. No GUI credential buffer, clipboard or fallback port.
// Cancellation retires the owner even if the device already acted.
class DeviceTransport {
public:
    virtual ~DeviceTransport() = default;
    virtual std::string_view Identity() const = 0;
    virtual bool QualifiedTransport() const = 0;
    virtual bool Supports(const DeviceTransportRequest&) const = 0;
    virtual std::optional<DeviceTransportReply> BeginTrustedWorkflow(const DeviceTransportRequest&, std::stop_token) const = 0;
};

ExtensionStatus LocalSignerStatus(const ExtensionRegistry&, const LocalSignerRequest* current = nullptr);
ExtensionStatus DeviceTransportStatus(const ExtensionRegistry&, const DeviceTransportRequest* current = nullptr);
std::optional<Hash256> CustodyPublicPolicyIdentity(std::string_view canonical_public_descriptor);
std::optional<LocalSignerReply> BeginLocalSigner(const ExtensionRegistry&, const LocalSignerRequest&,
    const LocalSignerRequest& current, std::stop_token = {});
std::optional<DeviceTransportReply> BeginDeviceTransport(const ExtensionRegistry&, const DeviceTransportRequest&,
    const DeviceTransportRequest& current, std::stop_token = {});
bool CurrentLocalSignerReply(const ExtensionRegistry&, const LocalSignerRequest& current,
    const LocalSignerReply&, std::stop_token = {});
bool CurrentDeviceTransportReply(const ExtensionRegistry&, const DeviceTransportRequest& current,
    const DeviceTransportReply&, std::stop_token = {});

enum class ReusableFamily { FixedDescriptor, SilentPayments, PaymentCode };
struct FixedReceivePolicy { std::string descriptor, address; };
struct SilentReceivePolicy { std::string encoded_address; uint8_t version; };
struct PaymentCodeReceivePolicy { std::string code; uint8_t version; };
struct ReusableReceiveRecord {
    CustodyScope scope;
    std::string provider;
    // A reusable identifier has no allocator index or Next Address operation.
    std::variant<FixedReceivePolicy, SilentReceivePolicy, PaymentCodeReceivePolicy> fields;
};

class ReusableReceiveService {
public:
    virtual ~ReusableReceiveService() = default;
    virtual std::string_view Identity() const = 0;
    virtual bool Supports(ReusableFamily, NetworkIdentity) const = 0;
    // Qualified policy-specific decoding and independent ownership proof. Any
    // provider work must be read-only and stop-aware.
    virtual bool Verify(const ReusableReceiveRecord&, std::stop_token) const = 0;
};

struct ReusablePolicyStatus {
    const char* name;
    const char* public_fields;
    ExtensionStatus capability;
};
ReusablePolicyStatus ReusableStatus(const ExtensionRegistry&, ReusableFamily, NetworkIdentity);
bool CurrentReusableReceive(const ExtensionRegistry&, const CustodyScope&, const ReusableReceiveRecord&, std::stop_token = {});

} // namespace roots::gui
