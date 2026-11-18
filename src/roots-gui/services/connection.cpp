// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/connection.h"
#include <algorithm>

namespace roots::gui {
namespace {
bool Host(const std::string& s)
{
    if (s.empty() || s.size() > 253 || s.front() == '.' || s.back() == '.') return false;
    const bool ipv6 = s.find(':') != std::string::npos;
    return std::all_of(s.begin(), s.end(), [ipv6](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '-' || (ipv6 && c == ':');
    });
}
bool SafeReference(const std::string& s)
{
    return !s.empty() && s.size() <= 4096 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return c >= 32 && c != 127; });
}
}
bool ValidConnectionProfile(const ConnectionProfile& p)
{
    return Host(p.host) && p.port && SafeReference(p.credential.reference) &&
        (p.credential.kind == Authentication::Cookie || p.credential.kind == Authentication::SecureStore) &&
        (p.trusted_ca_file.empty() || (p.tls && SafeReference(p.trusted_ca_file))) &&
        ((p.proxy == ProxyKind::Direct && p.proxy_host.empty()) ||
         ((p.proxy == ProxyKind::Socks5RemoteDns || p.proxy == ProxyKind::HttpTunnel) && Host(p.proxy_host) && p.proxy_port));
}
}
