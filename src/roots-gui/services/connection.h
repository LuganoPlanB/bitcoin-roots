// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace roots::gui {
enum class Authentication { Cookie, SecureStore };
struct CredentialReference {
    Authentication kind{Authentication::Cookie};
    // Explicit cookie path or opaque OS secure-store item ID. Never a password.
    std::string reference;
    bool operator==(const CredentialReference&) const = default;
};
enum class ProxyKind { Direct, Socks5RemoteDns, HttpTunnel };
struct ConnectionProfile {
    std::string host{"127.0.0.1"};
    uint16_t port{8332};
    bool tls{false};
    std::string trusted_ca_file;
    CredentialReference credential;
    ProxyKind proxy{ProxyKind::Direct};
    std::string proxy_host;
    uint16_t proxy_port{9050};
    bool operator==(const ConnectionProfile&) const = default;
};
// Shared Server form/connection boundary. No URL userinfo, arbitrary path,
// query, embedded credentials or inherited proxy environment is accepted.
bool ValidConnectionProfile(const ConnectionProfile&);
}
