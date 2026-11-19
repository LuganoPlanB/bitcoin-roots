// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/daemon_signer.h"
#include "app/history_workspace.h"
#include "services/receive.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
using namespace std::chrono_literals;
namespace a=app;
namespace p=protocol;
namespace {
constexpr auto GENESIS="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
constexpr auto WALLET="usb fixture & test";

void Check(bool value, std::string_view reason)
{
    if (!value) throw std::runtime_error(std::string(reason));
}
template <typename Done>
void Wait(Done done)
{
    const auto end=std::chrono::steady_clock::now()+12s;
    while (!done()) {
        Check(std::chrono::steady_clock::now()<end,"owner worker deadline");
        std::this_thread::sleep_for(2ms);
    }
}
p::PublicDescriptor Normalize(const char* arg)
{
    auto preview=p::PreviewPublicWallet(p::PublicImportKind::Descriptor,arg,"regtest");
    Check(preview.preview && preview.preview->descriptors.size()==1,"fixture public descriptor parses");
    return std::move(preview.preview->descriptors.front());
}
void Retire()
{
    std::string input;
    Check(bool(std::getline(std::cin,input)) && input=="retire","owned controller retires the operation");
}
}

int main(int argc,char** argv)
{
    try {
        if (argc==4 && std::string_view(argv[1])=="target") {
            auto target=Normalize(argv[2]);
            auto branch=Normalize(argv[3]);
            Check(branch.script==target.script && target.address.starts_with("bcrt1"),"independent BIP32 child matches branch");
            std::cout<<"{\"descriptor\":"<<rpc::Quote(target.descriptor)<<",\"branch\":"<<rpc::Quote(branch.descriptor)
                <<",\"address\":"<<rpc::Quote(target.address)<<"}\n";
            return 0;
        }
        if (argc!=6) return 2;
        const std::string_view mode=argv[1];
        const auto target=Normalize(argv[5]);
        std::ifstream file(argv[4],std::ios::binary);
        const std::string bytes(std::istreambuf_iterator<char>(file),{});
        const auto psbt=p::ParseTransactionDocument(bytes,p::DisplayNetwork::Regtest).document;
        Check(psbt && psbt->transaction && psbt->transaction->inputs.size()==1 && psbt->transaction->fee,"fixture PSBT");
        const auto& tx=*psbt->transaction;
        Check(tx.inputs[0].previous_value && tx.inputs[0].previous_script==target.script,"fixture previous output");
        rpc::CookieCredentials credentials;
        rpc::Transport transport(credentials);
        ServerCandidate profile;
        profile.connection.port=uint16_t(std::stoul(argv[2]));
        profile.connection.credential.reference=argv[3];
        profile.expected_genesis=GENESIS;
        profile.wallet=WALLET;
        ConnectionService connection([&](const auto& candidate,std::stop_token) {
            BackendCapabilities caps;
            caps.candidate=candidate;caps.genesis=GENESIS;caps.chain="regtest";
            caps.authenticated=caps.identity_verified=true;
            caps.wallet=WalletAvailability::Available;
            caps.descriptors=true;caps.private_keys=false;caps.external_signer=true;
            caps.methods["getblockhash"]={true,true,false,false,{}};
            caps.methods["getwalletinfo"]={true,true,true,false,{}};
            if (mode!="scan-absent") caps.methods["enumeratesigners"]={true,true,false,false,{}};
            caps.methods["listdescriptors"]={true,true,true,false,{"private"}};
            caps.methods["getaddressinfo"]={true,true,true,false,{"address"}};
            caps.methods["walletdisplayaddress"]={true,true,true,false,{"address"}};
            caps.methods["walletprocesspsbt"]={true,true,true,false,{"psbt","sign","finalize","bip32derivs","sighashtype"}};
            return caps;
        });
        Check(connection.SetDraft(profile) && connection.Test(),"stage exact fixture capability observations");
        Wait([&]{connection.Poll();return !connection.Busy();});
        Check(connection.Apply(),"apply exact fixture wallet");
        DeviceSigners devices(transport);
        Check(!devices.Current(&connection) && !devices.Select(connection),"connection grants no device authority");
        if (mode=="scan-absent") {
            Check(!devices.Rescan(connection) && !CanVerifyReceiveOnDevice(*connection.Active()),"absent method refuses discovery/display");
            std::cout<<"{\"admitted\":false,\"settled\":true}\n";
            return 0;
        }
        Check(devices.Rescan(connection) && devices.Status()==DeviceState::Scanning
            && !devices.Rescan(connection),"one explicit scan");
        if (mode=="scan-cancel") {
            Retire();
            devices.Cancel();
        }
        Wait([&]{
            devices.Poll(&connection);
            if (mode=="scan-cancel") {
                // A newly admitted scan would issue another RPC; wait by
                // leaving the original owner to unwind on scope destruction.
                return devices.Status()==DeviceState::Idle;
            }
            return devices.Status()!=DeviceState::Scanning;
        });
        if (mode.starts_with("scan-")) {
            const std::map<std::string_view,DeviceState> states{
                {"scan-rejected",DeviceState::Unsupported},{"scan-unconfigured",DeviceState::Unconfigured},
                {"scan-empty",DeviceState::NoDevice},{"scan-no-match",DeviceState::NoMatch},{"scan-multiple",DeviceState::Multiple},
                {"scan-duplicate",DeviceState::Failure},{"scan-malformed",DeviceState::Failure},{"scan-too-many",DeviceState::Failure},
                {"scan-limit",DeviceState::Failure},{"scan-cancel",DeviceState::Idle}};
            Check(devices.Status()==states.at(mode) && !devices.Select(connection) && !devices.Current(&connection),
                DeviceStateText(devices.Status()));
            std::cout<<"{\"status\":"<<rpc::Quote(DeviceStateText(devices.Status()))<<",\"settled\":true}\n";
            return 0;
        }
        Check(devices.Status()==DeviceState::Select && !devices.Current(&connection),"discovery grants no automatic selection");
        Check(devices.Select(connection) && !devices.Select(connection) && devices.Status()==DeviceState::Ready,"explicit matching selection");
        auto device=devices.Current(&connection);
        Check(device && device->Owns(target.descriptor,target.script,true),"exact derived public policy match");
        Check(!device->Owns(target.descriptor,"00140000000000000000000000000000000000000000",true),"same origin cannot fake the script");
        const auto wallet=a::VerifiedWalletBinding(*connection.Active());
        Check(wallet.has_value(),"verified public wallet binding");
        if (mode.starts_with("display")) {
            ReceiveService receive([&](const auto& operation,std::stop_token stop){return FetchReceive(transport,operation,stop);});
            auto registry=a::DocumentRegistry{};
            const auto id=registry.OpenWallet(WALLET,*wallet,true);
            registry.Find(id)->history.enabled=true;
            const auto identity=a::WalletRefreshIdentity(*connection.Active(),connection.Session(),*registry.Find(id),LockEpoch{});
            Check(identity.has_value(),"verified Receive context");
            auto selected=std::optional{ReceiveTarget{target.address,target.script,target.descriptor,"m/0/0","regtest",{},1}};
            ReceiveRequest operation{ReceiveAction::VerifyDevice,*identity,connection.Revision(),0,selected,device};
            auto injected=operation;
            injected.device.reset();
            Check(!receive.Start(injected,connection) && !FetchReceive(transport,injected,{}).signer_echo,"missing selection cannot dispatch");
            if (mode=="display-injected") {
                std::cout<<"{\"admitted\":false,\"settled\":true}\n";return 0;
            }
            Check(receive.Start(operation,connection) && !receive.Start(operation,connection),"one explicit display");
            const bool retiring=mode=="display-cancel" || mode=="display-privacy" || mode=="display-target-change";
            if (retiring) {
                Retire();
                if (mode=="display-cancel") receive.Cancel();
                if (mode=="display-privacy") connection.SetPrivacyLocked(true);
                if (mode=="display-target-change") selected.reset();
            }
            std::optional<ReceiveReply> reply;
            Wait([&]{
                if (auto result=receive.Poll(&connection,identity,selected)) reply=std::move(result->second);
                return !receive.Busy();
            });
            Check(retiring ? !reply : reply && reply->signer_echo==(mode=="display"),"display adoption classification");
            if (mode=="display-locked") Check(devices.Status()==DeviceState::Locked,"explicit locked response");
            if (mode=="display-passphrase") Check(devices.Status()==DeviceState::Passphrase,"explicit passphrase response");
            if (mode=="display-changed-signer") Check(devices.Status()==DeviceState::Disconnected,"post-display signer gone");
            Check(!reply || (!reply->allocation_dispatched && !reply->address),"display is never address allocation");
            std::cout<<"{\"echo\":"<<(reply && reply->signer_echo?"true":"false")<<",\"settled\":true,\"status\":"
                <<rpc::Quote(reply ? reply->error : "Retired")<<"}\n";
            return 0;
        }
        a::DaemonSigner signer(transport,connection);
        const auto proof=p::OfflineReviewEvidence{p::DisplayNetwork::Regtest,
            {{tx.inputs[0].outpoint,*tx.inputs[0].previous_value,target.descriptor}},tx.outputs,*tx.fee,{}};
        a::SigningBasis basis{1,2,3,connection.Session(),connection.Revision(),{1},{2},{},false,
            *wallet,psbt,proof,{},*a::SigningDigest("fixture public proof"),{},{},{1U},false};
        a::SigningReviews reviews;
        Check(reviews.Record(basis),"review exact PSBT");
        if (mode!="sign-unapproved") Check(reviews.Approve(basis),"separate explicit approval");
        auto offered=basis;
        if (mode=="sign-stale") ++offered.document_revision.value;
        Check(!signer.Start(reviews,basis,a::DaemonSignAction::ExternalSign)
            && !signer.Start(reviews,basis,a::DaemonSignAction::Unlock,"never-supplied-device-passphrase"),
            "missing device or generic external wallet unlock refused");
        const auto action=mode=="sign-ordinary" ? a::DaemonSignAction::Sign : a::DaemonSignAction::ExternalSign;
        const bool started=signer.Start(reviews,offered,action,{},mode=="sign-ordinary" ? SelectedDevice{} : device);
        if (mode=="sign-ordinary" || mode=="sign-stale" || mode=="sign-unapproved") {
            Check(!started && !signer.Busy(),"unapproved/stale/ordinary authority could not start");
            std::cout<<"{\"admitted\":false,\"settled\":true}\n";return 0;
        }
        Check(started && !signer.Start(reviews,offered,action,{},device),"one explicit device sign");
        const bool retiring=mode=="sign-cancel" || mode=="sign-privacy" || mode=="sign-profile";
        if (retiring) {
            Retire();
            if (mode=="sign-cancel") signer.Cancel();
            if (mode=="sign-privacy") connection.SetPrivacyLocked(true);
            if (mode=="sign-profile") {
                auto other=profile;other.wallet="different wallet";
                Check(connection.SetDraft(other),"retire exact profile revision");
            }
        }
        std::optional<a::DaemonSignResult> reply;
        Wait([&]{reply=signer.Poll(reviews.Authorize(offered));return !signer.Busy();});
        Check(reply && reply->success==(mode=="sign") && reply->original_retired==(mode!="sign"),"sign reply classified and fenced");
        Check(psbt->source==bytes,"exact approved original retained");
        if (reply->success) {
            Check(reply->psbt && p::InspectSignatures(*reply->psbt).Complete()
                && reply->psbt->unsigned_wire==psbt->unsigned_wire,"independently verified signature and unchanged intent");
        } else Check(!reply->psbt && !device->Live(),"failed signer cannot contribute material or retain authority");
        std::cout<<"{\"success\":"<<(reply->success?"true":"false")<<",\"settled\":true,\"status\":"
            <<rpc::Quote(reply->status)<<"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
