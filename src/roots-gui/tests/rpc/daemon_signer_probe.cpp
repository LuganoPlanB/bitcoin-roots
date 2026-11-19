// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/daemon_signer.h"
#include "app/history_workspace.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
using namespace std::chrono_literals;
namespace {
constexpr auto GENESIS="1111111111111111111111111111111111111111111111111111111111111111";
constexpr auto WALLET="signing & test";
constexpr auto FIXTURE_PASSPHRASE="signrpcfixture\"\\";
void Check(bool ok,const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

template <typename Ready>
void Wait(Ready ready)
{
    const auto deadline=std::chrono::steady_clock::now()+20s;
    while (!ready()) {
        Check(std::chrono::steady_clock::now()<deadline,"Test operation did not settle.");
        std::this_thread::sleep_for(2ms);
    }
}
}

int main(int argc,char** argv)
{
    if (argc!=6) return 2;
    try {
        const std::string_view mode=argv[1];
        std::ifstream file(argv[4],std::ios::binary);
        const std::string wire(std::istreambuf_iterator<char>(file),{});
        auto parsed=protocol::ParseTransactionDocument(wire,protocol::DisplayNetwork::Regtest);
        Check(parsed.document && parsed.document->transaction,"Cannot parse fixture PSBT.");
        const auto& tx=*parsed.document->transaction;
        Check(tx.inputs.size()==1 && tx.inputs[0].previous_value && tx.fee,"Fixture must supply independent single-input facts.");
        const auto descriptor=protocol::PreviewPublicWallet(protocol::PublicImportKind::Descriptor,argv[5],"regtest");
        Check(descriptor.preview && descriptor.preview->descriptors.size()==1,"Normalize the fixture's independent public descriptor.");
        const auto proof=protocol::OfflineReviewEvidence{protocol::DisplayNetwork::Regtest,
            {{tx.inputs[0].outpoint,*tx.inputs[0].previous_value,descriptor.preview->descriptors.front().descriptor}},tx.outputs,*tx.fee,{}};
        const auto inspected=protocol::InspectOfflineReview(*parsed.document,proof);
        if (!inspected.commitment) throw std::runtime_error("Fixture policy check failed: "+inspected.error);
        rpc::CookieCredentials credentials;
        rpc::Transport transport(credentials);
        ServerCandidate profile;
        profile.connection.port=uint16_t(std::stoi(argv[2]));
        profile.connection.credential.reference=argv[3];
        profile.expected_genesis=GENESIS;
        profile.wallet=WALLET;
        ConnectionService connection([&](const auto& candidate,std::stop_token){
            BackendCapabilities caps;
            caps.candidate=candidate;
            caps.genesis=GENESIS;
            caps.authenticated=caps.identity_verified=true;
            caps.wallet=WalletAvailability::Available;
            caps.descriptors=true;
            caps.private_keys=mode!="watch-only";
            caps.external_signer=false;
            caps.unlocked_until=0;
            caps.methods["walletprocesspsbt"]={true,true,true,false,{"psbt","sign","finalize","bip32derivs","sighashtype"}};
            caps.methods["walletpassphrase"]={true,true,true,false,{"passphrase","timeout"}};
            return caps;
        });
        Check(connection.SetDraft(profile) && connection.Test(),"Stage test capabilities.");
        Wait([&]{connection.Poll();return !connection.Busy();});
        Check(connection.Apply(),"Apply exact synthetic wallet capability observation.");
        app::DaemonSigner signer(transport,connection);
        const auto wallet=app::VerifiedWalletBinding(*connection.Active());
        Check(wallet.has_value(),"Verified fixture wallet identity.");
        app::SigningBasis basis{1,2,3,connection.Session(),connection.Revision(),{1},{2},{},false,
            *wallet,parsed.document,proof,{},*app::SigningDigest("fixture public proof"),{},{},{mode=="bad-sighash" ? 3U : 1U},false};
        app::SigningReviews reviews;
        if (mode!="unreviewed") Check(reviews.Record(basis),"Record exact review.");
        if (mode!="unreviewed" && mode!="review-only") Check(reviews.Approve(basis),"Record separate exact approval.");
        auto offered=basis;
        if (mode=="stale-source") ++offered.document_revision.value;
        if (mode=="stale-epoch") ++offered.epoch.value;
        const auto action=mode=="enrich" || mode=="enrich-signature" ? app::DaemonSignAction::Enrich
            : mode=="unlock" || mode=="unlock-rejected" ? app::DaemonSignAction::Unlock : app::DaemonSignAction::Sign;
        const auto secret=action==app::DaemonSignAction::Unlock ? FIXTURE_PASSPHRASE : "";
        const bool started=signer.Start(reviews,offered,action,secret);
        if (mode=="unreviewed" || mode=="review-only" || mode=="stale-source" || mode=="stale-epoch" || mode=="watch-only") {
            Check(!started && !signer.Busy(),"Injected, stale or incompatible authority reached the worker.");
            std::cout<<"{\"admitted\":false,\"settled\":true}\n";
            return 0;
        }
        Check(started,"Expected prepared daemon operation.");
        Check(!signer.Start(reviews,offered,action,secret),"Duplicate pending request accepted.");
        if (mode=="late-epoch" || mode=="switch-node" || mode=="cancel") {
            // Test controller holds the response until the real request arrived.
            std::string retired;
            Check(bool(std::getline(std::cin,retired)) && retired=="retire","Missing controller retirement event.");
            if (mode=="late-epoch") reviews.RetireAll("Lock epoch changed.");
            if (mode=="switch-node") {
                auto next=profile;
                next.connection.port=uint16_t(profile.connection.port==65535 ? 65534 : profile.connection.port+1);
                Check(connection.SetDraft(next),"Change node profile revision.");
            }
            if (mode=="cancel") signer.Cancel();
        }
        std::optional<app::DaemonSignResult> result;
        Wait([&]{result=signer.Poll(reviews.Authorize(offered));return !signer.Busy();});
        Check(parsed.document->source==wire,"Approved original changed.");
        if (mode=="late-epoch" || mode=="switch-node" || mode=="cancel") {
            Check(!result,"Retired reply was adopted.");
            std::cout<<"{\"admitted\":true,\"retired\":true,\"settled\":true}\n";
            return 0;
        }
        Check(result.has_value(),"No result for current operation.");
        std::cout<<"{\"admitted\":true,\"settled\":true,\"success\":"<<(result->success?"true":"false")
            <<",\"psbt\":"<<(result->psbt?"true":"false")<<",\"status\":"<<rpc::Quote(result->status)<<"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
