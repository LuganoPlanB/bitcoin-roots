// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/rpc/transport.h"
#include "domain/model.h"
#include <cstdlib>
#include <condition_variable>
#include <iostream>
#include <thread>

using namespace roots::gui;
namespace {
class FixtureStore final : public rpc::CredentialSource {
    rpc::CookieCredentials cookie;
public:
    std::optional<rpc::Credentials> Resolve(const CredentialReference& ref, std::string_view origin) override
    {
        if (ref.kind == Authentication::Cookie) return cookie.Resolve(ref, origin);
        // Test-only secure-store double; secrets never appear in argv/output.
        const char* expected = std::getenv("ROOTS_RPC_TEST_ORIGIN");
        const char* password = std::getenv("ROOTS_RPC_TEST_PASSWORD");
        if (!expected || origin != expected || ref.reference != "fixture-item" || !password) return {};
        return rpc::Credentials("fixture", password);
    }
};
}
int main(int argc, char** argv)
{
    if (argc != 11) return 2;
    ConnectionProfile p;
    p.host = argv[1]; p.port = uint16_t(std::stoi(argv[2])); p.credential.reference = argv[3];
    if (p.credential.reference == "fixture-item") p.credential.kind = Authentication::SecureStore;
    p.tls = std::string_view(argv[4]) != "plain";
    if (p.tls && std::string_view(argv[4]) != "tls") p.trusted_ca_file = argv[4];
    rpc::Request request; request.method = argv[5]; request.params = "[0.00000001,9007199254740993]";
    if (std::string_view(argv[6]) != "node") request.wallet = argv[6];
    rpc::Limits limits; limits.total_timeout = std::chrono::milliseconds(std::stoi(argv[7]));
    limits.connect_timeout = std::min(limits.total_timeout, std::chrono::milliseconds(500));
    limits.response_bytes = 1024; limits.header_bytes = 4096;
    const std::string mode = argv[8];
    if (mode == "read") request.effect = rpc::Effect::ReadOnly;
    if (mode == "depth") limits.json_depth = 4;
    if (mode == "nodes") limits.json_nodes = 8;
    if (mode == "request-limit") limits.request_bytes = 16;
    if (std::string_view(argv[9]) != "direct") {
        p.proxy = std::string_view(argv[9]) == "socks" ? ProxyKind::Socks5RemoteDns : ProxyKind::HttpTunnel;
        p.proxy_host = "127.0.0.1"; p.proxy_port = uint16_t(std::stoi(argv[10]));
    }
    std::stop_source stop;
    if (mode == "pre-cancel") stop.request_stop();
    std::jthread cancel;
    if (mode == "cancel") cancel = std::jthread([&](std::stop_token token) {
        std::mutex mutex; std::condition_variable_any cv; std::unique_lock lock(mutex);
        cv.wait_for(lock, token, std::chrono::milliseconds(120), [] { return false; });
        if (!token.stop_requested()) stop.request_stop();
    });
    FixtureStore credentials;
    const auto result = rpc::Transport(credentials).Call(p, request, stop.get_token(), limits);
    if (cancel.joinable()) cancel.request_stop();
    std::cout << "{\"error\":" << rpc::Quote(rpc::ErrorName(result.error)) << ",\"status\":" << result.http_status
        << ",\"uncertain\":" << (result.mutation_uncertain ? "true" : "false");
    if (result.rpc_code) std::cout << ",\"code\":" << *result.rpc_code;
    if (result.result) {
        if (const auto* amount = result.result->Find("amount"); amount && amount->kind == rpc::Json::Kind::Number) {
            const auto exact = Money::ParseJsonBtc(amount->text);
            std::cout << ",\"lexeme\":" << rpc::Quote(amount->text);
            if (exact) std::cout << ",\"sats\":" << exact->sats();
        }
    }
    std::cout << "}\n";
}
