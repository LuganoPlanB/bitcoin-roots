// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/funding.h"
#include "services/fees.h"

#include <atomic>
#include <charconv>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
namespace {
void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
const rpc::Json& Get(const rpc::Json& object, const char* key)
{
    const auto* value = object.Find(key);
    Check(value, "missing test field");
    return *value;
}
uint32_t Number(const rpc::Json& v)
{
    uint32_t value{};
    const auto result = std::from_chars(v.text.data(), v.text.data() + v.text.size(), value);
    Check(result.ec == std::errc{} && result.ptr == v.text.data() + v.text.size(), "fixture integer");
    return value;
}
FundingRequest Request(const rpc::Json& fixture, const ServerCandidate& profile)
{
    const auto genesis = Hash256::Parse(profile.expected_genesis);
    Check(bool(genesis), "fixture genesis");
    const auto backend = *BackendSessionId::Make("connection-1");
    const auto wallet = *WalletId::Make(*profile.wallet);
    const auto document = *DocumentId::Make("wallet-document-1");
    FundingRequest request{{{{backend, {*genesis}, wallet, document, {1}, {1}}, profile, 1},
        1, 1, 1, 1, 1, {7}, Get(fixture, "tip").text}, FundingSelection(Number(Get(fixture, "mode"))),
        {}, {}, Number(Get(fixture, "rate"))};
    request.scope.fee_rate=request.rate_per_kvb;
    for (const auto& coin : Get(fixture, "coins").items) {
        const auto txid = Hash256::Parse(Get(coin, "txid").text);
        const auto value = Money::ParseJsonBtc(Get(coin, "amount").text);
        Check(txid && value, "fixture input");
        const auto field = [&](const char* name) {const auto* v = coin.Find(name); return v ? v->text : std::string{};};
        request.coins.push_back({{*txid, Number(Get(coin, "vout"))}, *value, Get(coin, "scriptPubKey").text,
            {Get(coin, "previous").text, Get(coin, "desc").text, field("redeemScript"), field("witnessScript"), 1000},
            Number(Get(coin, "confirmations"))});
    }
    for (const auto& row : Get(fixture, "recipients").items) {
        const auto& address = Get(row, "address").text;
        const auto script = protocol::AddressScript(address, protocol::DocumentNetwork({*genesis}));
        Check(bool(script), "fixture recipient script");
        const auto& amount = Get(row, "amount");
        request.recipients.push_back({address, *script, amount.kind == rpc::Json::Kind::Null
            ? std::nullopt : Money::ParseJsonBtc(amount.text)});
    }
    return request;
}

void Stale(FundingRequest request, FundingReply outcome)
{
    using namespace std::chrono_literals;
    std::atomic<bool> entered{}, release{};
    FundingService worker([&](const FundingRequest&, std::stop_token) {
        entered = true;
        const auto deadline = std::chrono::steady_clock::now() + 2000ms;
        while (!release && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
        return outcome;
    });
    const auto settled = [&](auto use) {
        const auto end = std::chrono::steady_clock::now() + 2300ms;
        while (!use() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(1ms);
        Check(use(), "funding worker settlement deadline");
    };
    auto revision = request.scope;
    Check(worker.Start(request), "start stale probe");
    settled([&] {return entered.load();});
    ++revision.draft.value;
    Check(!worker.Poll(revision), "stale revision cannot adopt an in-flight result");
    release = true;
    bool adopted = false;
    settled([&] {
        adopted |= worker.Poll(revision).has_value();
        return !worker.Busy();
    });
    Check(!adopted, "stale revision cannot adopt a late result");
    release = false;
    entered = false;
    auto different_fee=request.scope;
    ++different_fee.fee_rate;
    Check(worker.Start(request), "start fee scope probe");
    settled([&] {return entered.load();});
    Check(!worker.Poll(different_fee), "changed effective rate retires in-flight funding");
    release = true;
    settled([&] {
        adopted |= worker.Poll(different_fee).has_value();
        return !worker.Busy();
    });
    Check(!adopted, "changed effective rate cannot adopt a late result");
    Check(worker.Start(request), "start explicitly cancelled probe");
    worker.Cancel();
    settled([&] {
        adopted |= worker.Poll(request.scope).has_value();
        return !worker.Busy();
    });
    Check(!adopted, "explicit cancel retires result for the same revision");
}
}

int main(int argc, char** argv)
{
    try {
        Check(argc == 4, "usage: probe <fixture> <proxy port> <cookie>");
        std::ifstream file(argv[1]);
        const std::string bytes((std::istreambuf_iterator<char>(file)), {});
        const auto fixture = rpc::Json::Parse(bytes);
        Check(bool(fixture), "fixture JSON");
        uint16_t port{};
        const std::string_view text(argv[2]);
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), port);
        Check(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && port, "owned port");
        ServerCandidate profile;
        profile.connection.port = port;
        profile.connection.credential.reference = argv[3];
        profile.expected_genesis = Get(*fixture, "genesis").text;
        profile.wallet = Get(*fixture, "wallet").text;
        rpc::CookieCredentials credentials;
        rpc::Transport transport(credentials);
        const auto caps = ProbeCapabilities(transport, profile, {});
        if (!CanFund(caps)) {
            const auto found = caps.methods.find("walletcreatefundedpsbt");
            if (found != caps.methods.end()) for (const auto& argument : found->second.arguments) std::cerr << argument << '\n';
            throw std::runtime_error("pinned backend did not prove required funding options");
        }
        Check(ParseFundingRate("2.125") == 2125 && ParseFundingRate("2.125000") == 2125
            && !ParseFundingRate("2.1251") && !ParseFundingRate("-1") && !ParseFundingRate("0"), "exact rate domain");
        auto request = Request(*fixture, profile);
        if (fixture->Find("check_stale")) {
            auto expired=request;
            const auto now=RefreshMonotonicMs();
            expired.scope.fee_observation=now>FEE_SAMPLE_INTERVAL+1
                ? now-FEE_SAMPLE_INTERVAL-1 : now+FEE_SAMPLE_INTERVAL*2;
            Check(!FetchFunding(transport,expired,{}).preview,"expired target is rejected before RPC");
            auto inconsistent=request;
            ++inconsistent.scope.fee_rate;
            Check(!FetchFunding(transport,inconsistent,{}).preview,"inconsistent effective rate is rejected before RPC");
        }
        auto reply = FetchFunding(transport, request, {});
        if (reply.preview) {
            const auto& view = *reply.preview;
            if (fixture->Find("check_stale")) {
                Stale(request, reply);
                Check(view.change.has_value(),"exact current change proof for stale choice regression");
                auto former_change=request;
                former_change.requested_change=view.change;
                ++former_change.requested_change->index;
                const auto refused=FetchFunding(transport,former_change,{});
                Check(!refused.preview && refused.error.find("selected internal change proof changed")!=std::string::npos,
                    "changed index must refuse the exact preference without another construction");
            }
            std::cout << "{\"ok\":true,\"fee\":" << view.fee.sats() << ",\"total\":" << view.total_inputs.sats()
                << ",\"inputs\":" << view.inputs.size() << ",\"change\":" << (view.change ? "true" : "false")
                << ",\"max\":" << (view.maximum ? std::to_string(view.maximum->sats()) : "null")
                << ",\"psbt\":" << rpc::Quote(view.psbt->source) << "}\n";
        } else {
            std::cout << "{\"ok\":false,\"error\":" << rpc::Quote(reply.error) << "}\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
