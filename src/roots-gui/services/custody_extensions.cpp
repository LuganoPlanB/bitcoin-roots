// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/custody_extensions.h"

#include <algorithm>
#include <sodium.h>

namespace roots::gui {
namespace {
namespace p=protocol;

bool Key(std::string_view value)
{
    return value.size()==8 && std::ranges::all_of(value,[](char c) {
        return (c>='0' && c<='9') || (c>='a' && c<='f');
    });
}
bool PublicField(std::string_view value,size_t limit)
{
    return !value.empty() && value.size()<=limit
        && std::ranges::all_of(value,[](unsigned char c){return c>=0x21 && c<=0x7e;});
}
bool CurrentScope(const CustodyScope& scope)
{
    return scope.session && scope.profile && scope.generation
        && p::DocumentNetwork(scope.owner.network)!=p::DisplayNetwork::Unknown;
}
bool Valid(const LocalSignerRequest& request)
{
    const auto& grid=request.grid;
    return CurrentScope(request.scope) && request.ticket && grid.format==SeedFormat::Bip39
        && grid.language==WordList::Bip39English && grid.words>=12 && grid.words<=24 && grid.words%3==0
        && grid.columns>=2 && grid.columns<=8 && grid.columns<=grid.words;
}
bool Terminal(CustodyOutcome outcome)
{
    return outcome==CustodyOutcome::Refused || outcome==CustodyOutcome::Cancelled
        || outcome==CustodyOutcome::Disconnected || outcome==CustodyOutcome::TimedOut;
}
bool PublicDescriptor(std::string_view value,NetworkIdentity network)
{
    if (!PublicField(value,4096)) return false;
    const auto parsed=p::PreviewPublicWallet(p::PublicImportKind::Descriptor,value,p::DocumentChain(p::DocumentNetwork(network)));
    return parsed.preview && parsed.preview->descriptors.size()==1 && parsed.preview->descriptors[0].descriptor==value;
}
}

SecretOwner OwnerOf(SecretPurpose purpose)
{
    switch(purpose) {
    case SecretPurpose::MnemonicPassphrase: return SecretOwner::LocalSigner;
    case SecretPurpose::WorkspaceProtection: return SecretOwner::Workspace;
    case SecretPurpose::MetadataArchive: return SecretOwner::Archive;
    case SecretPurpose::DaemonUnlock: return SecretOwner::DaemonWallet;
    case SecretPurpose::DevicePin:
    case SecretPurpose::DevicePassphrase: return SecretOwner::DeviceTransport;
    }
    std::terminate();
}

ExtensionStatus LocalSignerStatus(const ExtensionRegistry& registry,const LocalSignerRequest* current)
{
    const auto* signer=registry.local_signer;
    if (!signer || !PublicField(signer->Identity(),64))
        return {false,"Local software wallet, recovery words and private keys require an isolated LocalSignerService."};
    if (!signer->ProtectedSecretStore())
        return {false,"The LocalSignerService needs its own protected secret store."};
    if (!signer->QualifiedVectors() || !current || !Valid(*current) || !signer->Supports(*current))
        return {false,"The LocalSignerService has no qualified workflow for this format, policy and network."};
    return {true,"Use the isolated signer for trusted entry and confirmation."};
}

ExtensionStatus DeviceTransportStatus(const ExtensionRegistry& registry,const DeviceTransportRequest* current)
{
    const auto* device=registry.device_transport;
    if (!device || !PublicField(device->Identity(),64))
        return {false,"Device PIN, passphrase and card actions require a separate trusted DeviceTransport."};
    if (!device->QualifiedTransport() || !current || !CurrentScope(current->scope) || !current->ticket
        || !device->Supports(*current))
        return {false,"No qualified transport operation for the selected device and policy."};
    return {true,"Entry and confirmation belong to the selected device or isolated transport."};
}

std::optional<Hash256> CustodyPublicPolicyIdentity(std::string_view descriptor)
{
    if (!PublicField(descriptor,4096)) return {};
    std::array<unsigned char,crypto_hash_sha256_BYTES> bytes{};
    if (crypto_hash_sha256(bytes.data(),reinterpret_cast<const unsigned char*>(descriptor.data()),descriptor.size())!=0)
        return {};
    return Hash256::Parse(p::TransactionHex({reinterpret_cast<const char*>(bytes.data()),bytes.size()}));
}

std::optional<LocalSignerReply> BeginLocalSigner(const ExtensionRegistry& registry,const LocalSignerRequest& request,
    const LocalSignerRequest& current,std::stop_token stop)
{
    if (stop.stop_requested() || request!=current || !request.confirmed || !LocalSignerStatus(registry,&current).available) return {};
    try {
        auto reply=registry.local_signer->BeginTrustedWorkflow(request,stop);
        return reply && CurrentLocalSignerReply(registry,current,*reply,stop) ? std::move(reply) : std::nullopt;
    } catch (...) {return {};}
}

std::optional<DeviceTransportReply> BeginDeviceTransport(const ExtensionRegistry& registry,const DeviceTransportRequest& request,
    const DeviceTransportRequest& current,std::stop_token stop)
{
    if (stop.stop_requested() || request!=current || !request.confirmed || !DeviceTransportStatus(registry,&current).available) return {};
    try {
        auto reply=registry.device_transport->BeginTrustedWorkflow(request,stop);
        return reply && CurrentDeviceTransportReply(registry,current,*reply,stop) ? std::move(reply) : std::nullopt;
    } catch (...) {return {};}
}

bool CurrentLocalSignerReply(const ExtensionRegistry& registry,const LocalSignerRequest& current,
    const LocalSignerReply& reply,std::stop_token stop)
{
    if (stop.stop_requested() || current!=reply.request || !current.confirmed || !LocalSignerStatus(registry,&current).available
        || reply.provider!=registry.local_signer->Identity()) return false;
    if (Terminal(reply.outcome))
        return !reply.signer_handle && !reply.fingerprint && reply.public_descriptor.empty();
    if (current.action==LocalSignerAction::VerifyBackup)
        return reply.outcome==CustodyOutcome::BackupVerified && !reply.signer_handle && !reply.fingerprint && reply.public_descriptor.empty();
    return reply.outcome==CustodyOutcome::PublicPolicy && reply.signer_handle && reply.fingerprint
        && Key(reply.fingerprint->master_key_fingerprint) && PublicDescriptor(reply.public_descriptor,current.scope.owner.network)
        && reply.fingerprint->public_descriptor_commitment==current.scope.policy
        && CustodyPublicPolicyIdentity(reply.public_descriptor)==current.scope.policy;
}

bool CurrentDeviceTransportReply(const ExtensionRegistry& registry,const DeviceTransportRequest& current,
    const DeviceTransportReply& reply,std::stop_token stop)
{
    if (stop.stop_requested() || current!=reply.request || !current.confirmed || !DeviceTransportStatus(registry,&current).available
        || reply.provider!=registry.device_transport->Identity()) return false;
    if (Terminal(reply.outcome)) return !reply.card;
    return reply.outcome==CustodyOutcome::Acknowledged && reply.card.has_value()==(current.action==DeviceAction::CardStatus);
}

ReusablePolicyStatus ReusableStatus(const ExtensionRegistry& registry,ReusableFamily family,NetworkIdentity network)
{
    const auto* service=registry.reusable_receive;
    const bool available=service && PublicField(service->Identity(),64) && p::DocumentNetwork(network)!=p::DisplayNetwork::Unknown
        && service->Supports(family,network);
    const ExtensionStatus status{available,available ? "A qualified provider can verify this reusable public identifier."
        : "Reusable policy unavailable. A qualified ReusableReceiveService is required."};
    switch(family) {
    case ReusableFamily::FixedDescriptor: return {"Fixed descriptor","Public descriptor and its fixed address",status};
    case ReusableFamily::SilentPayments: return {"Silent payments","Encoded receive identifier and scheme version",status};
    case ReusableFamily::PaymentCode: return {"Payment code","Encoded payment code and format version",status};
    }
    std::terminate();
}

bool CurrentReusableReceive(const ExtensionRegistry& registry,const CustodyScope& current,const ReusableReceiveRecord& record,
    std::stop_token stop)
{
    if (stop.stop_requested() || current!=record.scope || !CurrentScope(current)) return false;
    const auto network=current.owner.network;
    const auto* fixed=std::get_if<FixedReceivePolicy>(&record.fields);
    const auto* silent=std::get_if<SilentReceivePolicy>(&record.fields);
    const auto family=fixed ? ReusableFamily::FixedDescriptor : silent ? ReusableFamily::SilentPayments : ReusableFamily::PaymentCode;
    if (!ReusableStatus(registry,family,network).capability.available || record.provider!=registry.reusable_receive->Identity())
        return false;
    if (fixed) {
        if (!PublicDescriptor(fixed->descriptor,network) || CustodyPublicPolicyIdentity(fixed->descriptor)!=current.policy) return false;
        const auto derived=p::DerivePublicDescriptor(fixed->descriptor,p::DocumentChain(p::DocumentNetwork(network)),0);
        if (!derived || derived->ranged || derived->address!=fixed->address) return false;
    } else if (!PublicField(silent ? silent->encoded_address : std::get<PaymentCodeReceivePolicy>(record.fields).code,256)) return false;
    try {return registry.reusable_receive->Verify(record,stop) && !stop.stop_requested();}
    catch (...) {return false;}
}
} // namespace roots::gui
