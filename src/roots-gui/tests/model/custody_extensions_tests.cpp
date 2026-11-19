// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/custody_extensions.h"
#include "services/receive.h"

#include <iostream>
#include <stdexcept>
#include <type_traits>

using namespace roots::gui;
namespace p=protocol;
namespace {
constexpr auto GENESIS="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
constexpr auto MAIN="000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";
constexpr auto PUBLIC="wpkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)";
int checks{};

template<typename T> void Check(const T& condition,const char* what)
{
    if (!static_cast<bool>(condition)) throw std::runtime_error(what);
    ++checks;
}
Hash256 Hash(char value) {return *Hash256::Parse(std::string(64,value));}
NetworkIdentity Regtest() {return {*Hash256::Parse(GENESIS)};}
CustodyScope Scope(Hash256 policy)
{
    return {{*BackendSessionId::Make("synthetic-backend"),Regtest(),*WalletId::Make("public-fixture"),
        *DocumentId::Make("policy-document"),{8},{2}},17,6,10,policy};
}

struct Signer final:LocalSignerService {
    bool secret_store{}, vectors{};
    std::optional<LocalSignerReply> reply;
    mutable unsigned calls{};
    std::string_view Identity() const override {return "isolated-fixture";}
    bool ProtectedSecretStore() const override {return secret_store;}
    bool QualifiedVectors() const override {return vectors;}
    bool Supports(const LocalSignerRequest& request) const override {
        return request.scope.owner.network==Regtest() && request.grid.words==12;
    }
    std::optional<LocalSignerReply> BeginTrustedWorkflow(const LocalSignerRequest&,std::stop_token) const override {
        ++calls;
        return reply;
    }
};

struct Device final:DeviceTransport {
    bool qualified{};
    mutable unsigned calls{};
    std::optional<DeviceTransportReply> reply;
    std::stop_source* retire{};
    std::string_view Identity() const override {return "device-fixture";}
    bool QualifiedTransport() const override {return qualified;}
    bool Supports(const DeviceTransportRequest& request) const override {
        return request.scope.owner.network==Regtest() && request.selected_device==Hash('d')
            && request.entry==TrustedDeviceEntry::OnDevice;
    }
    std::optional<DeviceTransportReply> BeginTrustedWorkflow(const DeviceTransportRequest&,std::stop_token) const override {
        ++calls;
        if (retire) retire->request_stop();
        return reply;
    }
};

struct FixedService final:ReusableReceiveService {
    mutable unsigned proofs{};
    std::string_view Identity() const override {return "fixed-fixture";}
    bool Supports(ReusableFamily family,NetworkIdentity network) const override {
        return family==ReusableFamily::FixedDescriptor && network==Regtest();
    }
    bool Verify(const ReusableReceiveRecord& record,std::stop_token stop) const override {
        ++proofs;
        return std::holds_alternative<FixedReceivePolicy>(record.fields) && !stop.stop_requested();
    }
};

void SignerCases(const p::PublicDescriptor& descriptor,Hash256 identity)
{
    const LocalSignerRequest request{Scope(identity),19,LocalSignerAction::Restore,
        {SeedFormat::Bip39,WordList::Bip39English,12,3},MnemonicPassphrase::IsolatedEntry,true};
    const auto& shipped=ShippedExtensions();
    Check(!LocalSignerStatus(shipped,&request).available && !BeginLocalSigner(shipped,request,request),
        "RPC inventory cannot start a local trust workflow");
    Check(OwnerOf(SecretPurpose::MnemonicPassphrase)==SecretOwner::LocalSigner
        && OwnerOf(SecretPurpose::WorkspaceProtection)==SecretOwner::Workspace
        && OwnerOf(SecretPurpose::MetadataArchive)==SecretOwner::Archive
        && OwnerOf(SecretPurpose::DaemonUnlock)==SecretOwner::DaemonWallet
        && OwnerOf(SecretPurpose::DevicePin)==SecretOwner::DeviceTransport
        && OwnerOf(SecretPurpose::DevicePassphrase)==SecretOwner::DeviceTransport,"all credential purposes have distinct owning boundaries");
    Signer signer;
    ExtensionRegistry ports;
    ports.local_signer=&signer;
    signer.reply=LocalSignerReply{request,"isolated-fixture",CustodyOutcome::PublicPolicy,19,
        PublicSignerFingerprint{"a1234567",identity},descriptor.descriptor};
    Check(!BeginLocalSigner(ports,request,request) && signer.calls==0,"missing protected store blocks workflow before dispatch");
    signer.secret_store=true;
    Check(!BeginLocalSigner(ports,request,request) && signer.calls==0,"missing independent vectors blocks workflow");
    signer.vectors=true;
    auto wrong_grid=request;
    wrong_grid.grid.words=13;
    Check(!BeginLocalSigner(ports,wrong_grid,wrong_grid) && signer.calls==0,"unqualified word count and grid fenced");
    auto tentative=request;
    tentative.confirmed=false;
    Check(LocalSignerStatus(ports,&tentative).available && !BeginLocalSigner(ports,tentative,tentative)
        && signer.calls==0,"capability observation conveys no consent");
    const auto admitted=BeginLocalSigner(ports,request,request);
    Check(admitted && admitted->fingerprint->master_key_fingerprint=="a1234567" && signer.calls==1,
          "current isolated public result carries fingerprint, descriptor commitment and provenance");
    auto retired=request;
    ++retired.scope.owner.lock_epoch.value;
    Check(!CurrentLocalSignerReply(ports,retired,*admitted),"workspace epoch retires public signer result");
    signer.reply->fingerprint->public_descriptor_commitment=Hash('b');
    Check(!CurrentLocalSignerReply(ports,request,*signer.reply),"short fingerprint cannot stand in for descriptor identity");
    const auto substituted=p::PreviewPublicWallet(p::PublicImportKind::Descriptor,
        "pkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)","regtest");
    Check(substituted.preview && substituted.preview->descriptors.size()==1,"independent alternative public descriptor");
    signer.reply->public_descriptor=substituted.preview->descriptors[0].descriptor;
    signer.reply->fingerprint->public_descriptor_commitment=*CustodyPublicPolicyIdentity(signer.reply->public_descriptor);
    Check(!CurrentLocalSignerReply(ports,request,*signer.reply),"matching reply commitment cannot substitute a different policy");
    auto backup=request;
    backup.action=LocalSignerAction::VerifyBackup;
    ++backup.ticket;
    signer.reply=LocalSignerReply{backup,"isolated-fixture",CustodyOutcome::BackupVerified};
    Check(!CurrentLocalSignerReply(ports,request,*signer.reply) && BeginLocalSigner(ports,backup,backup),
          "backup is a separate authorized, format-bound attestation");
    signer.reply->signer_handle=71;
    Check(!CurrentLocalSignerReply(ports,backup,*signer.reply),"backup attestation cannot confer a signer handle");
}

void DeviceCases(Hash256 identity)
{
    const DeviceTransportRequest request{Scope(identity),Hash('d'),22,DeviceAction::Pin,TrustedDeviceEntry::OnDevice,true};
    const auto& shipped=ShippedExtensions();
    BackendCapabilities flags;
    flags.authenticated=flags.identity_verified=true;
    flags.external_signer=true;
    flags.methods["enumeratesigners"]={true};
    Check(flags.Supports("enumeratesigners")==SupportReason::Available
        && !DeviceTransportStatus(shipped,&request).available && !BeginDeviceTransport(shipped,request,request),
        "daemon discovery flags cannot qualify device PIN/card entry");
    Device device;
    ExtensionRegistry ports;
    ports.device_transport=&device;
    device.reply=DeviceTransportReply{request,"device-fixture",CustodyOutcome::Acknowledged,{}};
    Check(!BeginDeviceTransport(ports,request,request) && device.calls==0,"unqualified transport cannot begin PIN");
    device.qualified=true;
    auto other=request;
    other.selected_device=Hash('e');
    Check(!BeginDeviceTransport(ports,request,other) && device.calls==0,"changed selected device fences a pending request");
    Check(BeginDeviceTransport(ports,request,request) && device.calls==1,"trusted device receives only public workflow metadata");
    auto card=request;
    card.action=DeviceAction::CardStatus;
    ++card.ticket;
    device.reply=DeviceTransportReply{card,"device-fixture",CustodyOutcome::Acknowledged,CardState::Ready};
    Check(BeginDeviceTransport(ports,card,card) && !CurrentDeviceTransportReply(ports,request,*device.reply),
          "public card status is distinct from PIN acknowledgment");
    std::stop_source cancelled;
    device.retire=&cancelled;
    Check(!BeginDeviceTransport(ports,card,card,cancelled.get_token()),"disconnect/retirement cannot publish a late device completion");
    device.reply=DeviceTransportReply{card,"device-fixture",CustodyOutcome::Disconnected,{}};
    Check(CurrentDeviceTransportReply(ports,card,*device.reply),"current credential-free failure is distinguishable");
}

void ReusableCases(const p::PublicDescriptor& descriptor,Hash256 identity)
{
    static_assert(!std::is_convertible_v<ReusableReceiveRecord,ReceiveTarget>);
    const auto scope=Scope(identity);
    const auto& shipped=ShippedExtensions();
    for (const auto family:{ReusableFamily::FixedDescriptor,ReusableFamily::SilentPayments,ReusableFamily::PaymentCode}) {
        const auto status=ReusableStatus(shipped,family,Regtest());
        Check(!status.capability.available && *status.name && *status.public_fields,"policy-specific unavailable public fields in RPC profile");
    }
    const ReusableReceiveRecord current{scope,"fixed-fixture",FixedReceivePolicy{descriptor.descriptor,descriptor.address}};
    Check(!CurrentReusableReceive(shipped,scope,current),"shipped profile cannot adopt a reusable record");
    FixedService service;
    ExtensionRegistry ports;
    ports.reusable_receive=&service;
    Check(ReusableStatus(ports,ReusableFamily::FixedDescriptor,Regtest()).capability.available
        && CurrentReusableReceive(ports,scope,current) && service.proofs==1,"fixed public identifier independently proven");
    auto invalid=current;
    std::get<FixedReceivePolicy>(invalid.fields).address="foreign-address";
    Check(!CurrentReusableReceive(ports,scope,invalid) && service.proofs==1,"incorrect address fails before provider");
    auto wrong=scope;
    wrong.owner.network={*Hash256::Parse(MAIN)};
    Check(!CurrentReusableReceive(ports,wrong,current),"foreign genesis denied");
    wrong=scope;
    ++wrong.owner.lock_epoch.value;
    Check(!CurrentReusableReceive(ports,wrong,current),"locked epoch retires reusable proof");
    wrong=scope;
    wrong.policy=Hash('e');
    invalid.scope=wrong;
    invalid.fields=current.fields;
    Check(!CurrentReusableReceive(ports,wrong,invalid) && service.proofs==1,"wrong expected descriptor identity cannot adopt");
    Check(!CanAllocateReceive(BackendCapabilities{}),"reusable availability adds no RPC allocation capability");
}
}

int main()
{
    try {
        const auto preview=p::PreviewPublicWallet(p::PublicImportKind::Descriptor,PUBLIC,"regtest");
        Check(preview.preview && preview.preview->descriptors.size()==1,"independent fixed public descriptor fixture");
        const auto& descriptor=preview.preview->descriptors[0];
        const auto identity=CustodyPublicPolicyIdentity(descriptor.descriptor);
        Check(identity,"exact public policy identity");
        SignerCases(descriptor,*identity);
        DeviceCases(*identity);
        ReusableCases(descriptor,*identity);
        std::cout<<"{\"selected\":3,\"executed\":3,\"passed\":3,\"failed\":0,\"skipped\":0,\"assertions\":"<<checks<<"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"Custody contracts failed after "<<checks<<" assertions: "<<error.what()<<'\n';
        return 1;
    }
}
