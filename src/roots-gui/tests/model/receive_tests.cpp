// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/public_wallet.h"
#include "services/receive.h"
#include <atomic>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
namespace {
constexpr char GENESIS[] = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
unsigned checks{};
void Check(bool condition, const char* label)
{
    if (!condition) throw std::runtime_error(label);
    ++checks;
}
void Wait(auto predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    Check(predicate(), "owner-thread completion deadline");
}
ReceiveTarget ExactTarget()
{
    auto preview = protocol::PreviewPublicWallet(protocol::PublicImportKind::Descriptor,
        "wpkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)", "regtest");
    Check(preview.preview.has_value(), "fixture derives one public target");
    const auto& descriptor = preview.preview->descriptors.front();
    return {descriptor.address,descriptor.script,descriptor.descriptor,"m/84h/1h/0h/0/7","regtest",{},1700000000000};
}
BackendCapabilities Capabilities(const ServerCandidate& profile)
{
    BackendCapabilities caps;
    caps.candidate = profile;
    caps.authenticated = caps.identity_verified = true;
    caps.chain = "regtest";
    caps.genesis = profile.expected_genesis;
    caps.wallet = WalletAvailability::Available;
    caps.descriptors = true;
    caps.methods["getnewaddress"] = {true,true,true,false,{}};
    caps.methods["getwalletinfo"] = {true,true,true,false,{}};
    caps.methods["getaddressinfo"] = {true,true,true,false,{"address"}};
    return caps;
}
void ProofAndUri(const ReceiveTarget& target)
{
    Check(protocol::VerifyReceiveTarget(target), "public address, script and descriptor agree");
    auto altered = target;
    altered.script.back() = altered.script.back() == '0' ? '1' : '0';
    Check(!protocol::VerifyReceiveTarget(altered),"mismatched script rejected");
    altered = target; altered.chain = "test";
    Check(!protocol::VerifyReceiveTarget(altered),"wrong network address rejected");
    altered = target; altered.path = "m/0/01";
    Check(!protocol::VerifyReceiveTarget(altered),"malformed derivation rejected");
    altered = target; altered.index = 1;
    Check(!protocol::VerifyReceiveTarget(altered),"fixed descriptor with relative index rejected");
    altered = target; altered.descriptor.pop_back();
    Check(!protocol::VerifyReceiveTarget(altered),"bad descriptor checksum rejected");
    const auto prefix = "bitcoin:" + target.address;
    Check(ReceiveUri(target.address,"") == prefix,"bare address URI");
    Check(ReceiveUri(target.address,"café / A+B~_-") == prefix + "?label=caf%C3%A9%20%2F%20A%2BB~_-","exact UTF-8 URI vector");
    Check(ReceiveUri(target.address,"A # B",Money::Satoshis(123456789)) == prefix + "?amount=1.23456789&label=A%20%23%20B",
        "exact BTC amount and escaped query");
    Check(ReceiveUri(target.address,"",Money::Satoshis(1)) == prefix + "?amount=0.00000001", "exact one satoshi");
    Check(!ReceiveUri(target.address,"x",Money::Satoshis(-1)) && !ReceiveUri(target.address,"x\ny")
        && !ReceiveUri(target.address + "?","x"), "reject negative amounts, controls, and URI injection");
}
void OwnerFence(const ReceiveTarget& target)
{
    ServerCandidate profile;
    profile.connection.credential.reference = "/explicit/disposable-cookie";
    profile.expected_genesis = GENESIS;
    profile.wallet = "";
    ConnectionService connection([](const ServerCandidate& candidate, std::stop_token) {return Capabilities(candidate);});
    const auto apply = [&](const ServerCandidate& candidate) {
        Check(connection.SetDraft(candidate) && connection.Test(), "test exact fixture profile");
        Wait([&] {connection.Poll(); return !connection.Busy();});
        Check(connection.Apply(), "apply exact fixture profile");
    };
    apply(profile);
    auto context = RequestContext{*BackendSessionId::Make("fixture-session"),{*Hash256::Parse(GENESIS)},
        *WalletId::Make("default-wallet"),*DocumentId::Make("wallet-one"),{7},{1}};
    RefreshIdentity identity{context, profile, connection.Session()};
    std::atomic<unsigned> calls{}, stopped{};
    std::atomic<bool> release{true};
    ReceiveService service([&](const ReceiveRequest&, std::stop_token stop) {
        ++calls;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!release && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        stopped += stop.stop_requested() ? 1 : 0;
        return ReceiveReply{target,true,false,false,{}};
    });
    const auto operation = [&](uint64_t ticket) {
        return ReceiveRequest{ReceiveAction::Next,identity,connection.Revision(),ticket,{}};
    };
    const auto settle = [&](const std::optional<RefreshIdentity>& current, bool expected) {
        bool observed = false;
        Wait([&] {
            if (auto result = service.Poll(&connection,current,{})) observed = result->second.address == target;
            return !service.Busy();
        });
        Check(observed == expected, "completion adopted only by its current owner");
    };
    Check(CanAllocateReceive(*connection.Active()), "verified empty-named daemon wallet supported");
    Check(!CanVerifyReceiveOnDevice(*connection.Active()), "unproven signer unavailable");
    auto device = *connection.Active();
    device.external_signer = true;
    device.private_keys = false;
    device.methods["getblockhash"] = {true,true,false,false,{}};
    device.methods["enumeratesigners"] = {true,true,false,false,{}};
    device.methods["listdescriptors"] = {true,true,true,false,{"private"}};
    device.methods["walletdisplayaddress"] = {true,true,true,false,{"address"}};
    Check(CanVerifyReceiveOnDevice(device), "only a proved signer operation is available");
    device.chain = "main";
    Check(!CanAllocateReceive(device), "chain/genesis contradiction");
    Check(!service.Start(operation(0),connection), "no mutation without durable ticket");
    Check(service.Start(operation(1),connection) && !service.Start(operation(2),connection), "one explicit owned allocation");
    settle(identity,true);

    release = false;
    Check(service.Start(operation(2),connection), "explicit cancellable allocation");
    Wait([&] {return calls == 2;});
    service.Cancel();
    release = true;
    settle(identity,false);
    Check(stopped == 1, "worker received cancellation");

    release = false;
    Check(service.Start(operation(3),connection), "explicit request before document switch");
    Wait([&] {return calls == 3;});
    auto other = identity;
    other.context.document = *DocumentId::Make("wallet-two");
    service.Poll(&connection,other,{});
    release = true;
    settle(identity,false);
    Check(stopped == 2, "late document completion fenced even after returning");

    release = false;
    Check(service.Start(operation(4),connection), "explicit request before profile and wallet replacement");
    Wait([&] {return calls == 4;});
    auto replacement = profile;
    replacement.wallet = "other-wallet";
    apply(replacement);
    service.Poll(&connection,identity,{});
    release = true;
    settle(identity,false);
    Check(stopped == 3 && calls == 4, "late wallet/session results discarded without automatic replay");
    Check(connection.SetOffline(true) && !service.Start(operation(5),connection), "offline no transport");
}
}

int main()
{
    try {
        const auto target = ExactTarget();
        ProofAndUri(target);
        OwnerFence(target);
        std::cout << "{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":" << checks << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
