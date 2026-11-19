// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "adapters/rpc/json.h"
#include "services/connection.h"
#include <chrono>
#include <optional>
#include <stop_token>

namespace roots::gui::rpc {
struct Credentials {
    std::string username;
    std::string password;
    Credentials(std::string user, std::string pass);
    Credentials(Credentials&&) noexcept;
    Credentials& operator=(Credentials&&) = delete;
    Credentials(const Credentials&) = delete;
    ~Credentials();
};
class CredentialSource {
public:
    virtual ~CredentialSource() = default;
    // Resolve afresh for each call. Secure-store implementations must bind the
    // item to this origin. No value is retained in a profile, log or URL.
    virtual std::optional<Credentials> Resolve(const CredentialReference&, std::string_view origin) = 0;
};
class CookieCredentials final : public CredentialSource {
public:
    std::optional<Credentials> Resolve(const CredentialReference&, std::string_view origin) override;
};
enum class Error {
    None, InvalidProfile, InvalidRequest, CredentialsUnavailable, Configuration,
    Cancelled, Timeout, Resolve, Proxy, Tls, Connection, Http, Authentication,
    Redirect, Limit, Json, Identity, Rpc,
};
const char* ErrorName(Error);
enum class Effect { ReadOnly, Mutation };
enum class SignerNotice { None, Unconfigured, Absent, Multiple, Locked, Passphrase, Disconnected };
struct Limits {
    std::chrono::milliseconds connect_timeout{3000};
    std::chrono::milliseconds total_timeout{15000};
    size_t request_bytes{1024 * 1024};
    size_t response_bytes{8 * 1024 * 1024};
    size_t header_bytes{64 * 1024};
    size_t json_depth{64};
    size_t json_nodes{100000};
};
struct Request {
    uint64_t id{1};
    std::string method;
    std::string params{"[]"}; // exact JSON array or named-argument object
    std::optional<std::string> wallet;
    Effect effect{Effect::Mutation};
};
struct Reply {
    Error error{Error::None};
    long http_status{};
    std::optional<int32_t> rpc_code;
    std::optional<Json> result;
    // Cancellation/timeout cannot retract an already accepted remote command.
    bool mutation_uncertain{false};
    SignerNotice signer_notice{SignerNotice::None};
};
class Transport {
public:
    explicit Transport(CredentialSource& credentials) : m_credentials(credentials) {}
    // Worker operation: owns no UI state. Caller returns this value to the
    // owner thread with its immutable request context. No retry, even on 401.
    Reply Call(const ConnectionProfile&, const Request&, std::stop_token = {}, Limits = {}) const;
    // The passphrase is borrowed from a transient protected buffer and never
    // copied into the general-purpose request or its JSON parameter tree.
    Reply UnlockWallet(const ConnectionProfile&, std::string_view wallet,
        std::string_view passphrase, unsigned seconds, std::stop_token = {}) const;
private:
    Reply Call(const ConnectionProfile&, const Request&, std::stop_token, Limits,
        std::string_view passphrase, unsigned seconds) const;
    CredentialSource& m_credentials;
};
}
