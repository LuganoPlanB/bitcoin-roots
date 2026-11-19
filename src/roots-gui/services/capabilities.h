// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "adapters/rpc/transport.h"
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace roots::gui {
struct ServerCandidate {
    ConnectionProfile connection;
    std::string expected_genesis;
    std::optional<std::string> wallet;
    bool operator==(const ServerCandidate&) const = default;
};
enum class WalletAvailability { Unselected, Unchecked, Available, Unavailable, Unsupported };
enum class SupportReason { Available, UnsupportedMethod, UnprovenArgument, WalletUnavailable, ProbeRejected };
struct MethodSupport {
    bool registered{};
    bool schema_proven{};
    bool wallet_method{};
    bool probe_rejected{};
    std::set<std::string> arguments;
    std::set<std::string, std::less<>> string_arguments{};
};
struct BackendCapabilities {
    ServerCandidate candidate;
    bool authenticated{}, identity_verified{}, initial_download{}, pruned{}, wallet_scanning{};
    int64_t version{}, height{};
    std::string chain, genesis;
    WalletAvailability wallet{WalletAvailability::Unselected};
    std::optional<bool> descriptors, private_keys, external_signer;
    std::optional<int64_t> unlocked_until;
    std::map<std::string, MethodSupport> methods;
    rpc::Error error{rpc::Error::None};
    bool network_mismatch{};
    SupportReason Supports(std::string_view method, std::string_view argument = {}) const;
    bool SupportsString(std::string_view method, std::string_view argument) const;
    bool CanApply() const { return authenticated && identity_verified && error == rpc::Error::None && !network_mismatch; }
};
BackendCapabilities ProbeCapabilities(rpc::Transport&, const ServerCandidate&, std::stop_token);
bool ValidServerCandidate(const ServerCandidate&);
const char* SupportName(SupportReason);
const char* WalletAvailabilityName(WalletAvailability);

// Owner-thread service. Exactly one test worker and one completion slot. Test
// never applies a profile; Apply never issues RPC. Draft edits invalidate proof.
class ConnectionService {
public:
    using Probe = std::function<BackendCapabilities(const ServerCandidate&, std::stop_token)>;
    explicit ConnectionService(Probe, std::function<void(bool)> offline_changed = {});
    ~ConnectionService();
    bool SetDraft(ServerCandidate);
    bool Test();
    void Poll();
    void Cancel();
    bool Apply();
    bool AdoptWallet(BackendCapabilities, uint64_t expected_session, uint64_t expected_revision);
    bool SetOffline(bool);
    void SetPrivacyLocked(bool);
    bool PrivacyLocked() const{return privacy_locked;}
    bool Offline() const { return offline; }
    bool Busy() const { return worker.joinable(); }
    bool CanApply() const;
    const ServerCandidate& Draft() const { return draft; }
    const std::optional<BackendCapabilities>& Tested() const { return tested; }
    const std::optional<BackendCapabilities>& Active() const { return active; }
    uint64_t Revision() const { return revision; }
    uint64_t Session() const { return session; }
private:
    const std::thread::id owner{std::this_thread::get_id()};
    Probe probe;
    std::function<void(bool)> offline_changed;
    bool offline{},privacy_locked{};
    ServerCandidate draft;
    uint64_t revision{1}, tested_revision{}, session{};
    std::optional<BackendCapabilities> tested, active;
    struct Completion { uint64_t revision; BackendCapabilities result; };
    std::mutex mutex;
    std::optional<Completion> completed;
    std::jthread worker;
};
}
