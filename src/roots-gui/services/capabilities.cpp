// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/capabilities.h"
#include "domain/model.h"
#include <algorithm>
#include <charconv>
#include <limits>

namespace roots::gui {
namespace {
using J = rpc::Json;
std::optional<int64_t> Integer(const J* value)
{
    if (!value || value->kind != J::Kind::Number) return {};
    int64_t n{}; const auto result = std::from_chars(value->text.data(), value->text.data() + value->text.size(), n);
    if (result.ec != std::errc{} || result.ptr != value->text.data() + value->text.size()) return {};
    return n;
}
std::optional<bool> Boolean(const J* value)
{
    if (!value || value->kind != J::Kind::Boolean) return {};
    return value->text == "true";
}
bool Name(std::string_view s)
{
    return !s.empty() && s.size() <= 128 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.';
    });
}
bool Properties(const J& schema, std::string prefix, std::set<std::string>& names, unsigned depth = 0)
{
    if (depth > 16 || names.size() > 1024) return false;
    if (const auto* props = schema.Find("properties")) {
        if (props->kind != J::Kind::Object) return false;
        for (const auto& [key, value] : props->members) {
            if (!Name(key)) return false;
            const auto path = prefix + '.' + key;
            names.insert(path);
            if (!Properties(value, path, names, depth + 1)) return false;
        }
    }
    for (const auto* name : {"oneOf", "anyOf", "allOf"}) if (const auto* variants = schema.Find(name)) {
        if (variants->kind != J::Kind::Array) return false;
        // Union variants do not prove that a constraint is universally valid.
        // Nested argument support is retained only in direct properties.
    }
    const auto* type = schema.Find("type");
    if (type && type->kind == J::Kind::String && type->text == "array") {
        if (const auto* items = schema.Find("items"); items && items->kind == J::Kind::Object) {
            if (!Properties(*items, prefix, names, depth + 1)) return false;
        }
    }
    return names.size() <= 1024;
}
bool IsWalletMethod(std::string_view name)
{
    for (const auto* method : {"getwalletinfo", "getbalances", "listsinceblock", "gettransaction", "listunspent", "listlockunspent", "listdescriptors", "getaddressinfo", "getnewaddress", "walletcreatefundedpsbt", "walletprocesspsbt", "psbtbumpfee", "lockunspent", "walletdisplayaddress", "setlabel", "walletlock", "walletpassphrase", "importdescriptors", "backupwallet", "rescanblockchain", "signmessage"}) if (name == method) return true;
    return false;
}
bool ReadSchema(const J& value, BackendCapabilities& result)
{
    const auto* version = value.Find("openrpc"); const auto* methods = value.Find("methods");
    if (!version || version->kind != J::Kind::String || !methods || methods->kind != J::Kind::Array || methods->items.size() > 2048) return false;
    for (const auto& method : methods->items) {
        const auto* name = method.Find("name"); const auto* params = method.Find("params");
        if (!name || name->kind != J::Kind::String || !Name(name->text) || !params || params->kind != J::Kind::Array || params->items.size() > 128 || result.methods.contains(name->text)) return false;
        MethodSupport support; support.registered = true; support.schema_proven = true; support.wallet_method = IsWalletMethod(name->text);
        for (const auto& param : params->items) {
            const auto* key = param.Find("name"); const auto* schema = param.Find("schema");
            if (!key || key->kind != J::Kind::String || !Name(key->text) || !schema || schema->kind != J::Kind::Object || !support.arguments.insert(key->text).second) return false;
            const auto* type = schema->Find("type");
            if (type && type->kind == J::Kind::String && type->text == "string") support.string_arguments.insert(key->text);
            if (!Properties(*schema, key->text, support.arguments)) return false;
            if (const auto* aliases = param.Find("x-bitcoin-aliases")) {
                if (aliases->kind != J::Kind::Array || aliases->items.size() > 16) return false;
                for (const auto& alias : aliases->items) { if (alias.kind != J::Kind::String || !Name(alias.text)) return false; support.arguments.insert(alias.text); }
            }
        }
        result.methods.emplace(name->text, std::move(support));
    }
    return true;
}
}
bool ValidServerCandidate(const ServerCandidate& candidate)
{
    return ValidConnectionProfile(candidate.connection) && Hash256::Parse(candidate.expected_genesis).has_value() &&
        (!candidate.wallet || (candidate.wallet->size() <= 1024 && rpc::ValidUtf8(*candidate.wallet) && candidate.wallet->find('\0') == std::string::npos));
}
SupportReason BackendCapabilities::Supports(std::string_view method, std::string_view argument) const
{
    const auto found = methods.find(std::string(method));
    if (!CanApply() || found == methods.end() || !found->second.registered) return SupportReason::UnsupportedMethod;
    const auto& support = found->second;
    if (support.wallet_method && wallet != WalletAvailability::Available) return SupportReason::WalletUnavailable;
    if (support.probe_rejected) return SupportReason::ProbeRejected;
    if (!argument.empty() && (!support.schema_proven || !support.arguments.contains(std::string(argument)))) return SupportReason::UnprovenArgument;
    return SupportReason::Available;
}
bool BackendCapabilities::SupportsString(std::string_view method, std::string_view argument) const
{
    return Supports(method, argument) == SupportReason::Available
        && methods.at(std::string(method)).string_arguments.contains(argument);
}
const char* SupportName(SupportReason reason)
{
    switch (reason) {
    case SupportReason::Available: return "Available";
    case SupportReason::UnsupportedMethod: return "Method unavailable";
    case SupportReason::UnprovenArgument: return "Required argument unproven";
    case SupportReason::WalletUnavailable: return "Wallet unavailable";
    case SupportReason::ProbeRejected: return "Daemon rejected public probe";
    }
    return "Unavailable";
}
const char* WalletAvailabilityName(WalletAvailability state)
{
    switch (state) {
    case WalletAvailability::Unselected: return "No wallet selected";
    case WalletAvailability::Unchecked: return "Wallet not checked";
    case WalletAvailability::Available: return "Wallet available";
    case WalletAvailability::Unavailable: return "Wallet not loaded";
    case WalletAvailability::Unsupported: return "Wallet RPC unavailable";
    }
    return "Wallet unavailable";
}
BackendCapabilities ProbeCapabilities(rpc::Transport& transport, const ServerCandidate& candidate, std::stop_token stop)
{
    BackendCapabilities result; result.candidate = candidate;
    if (candidate.wallet) result.wallet = WalletAvailability::Unchecked;
    if (!ValidServerCandidate(candidate)) { result.error = rpc::Error::InvalidProfile; return result; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    uint64_t id{};
    auto call = [&](std::string method, std::string params = "[]", bool wallet = false) {
        rpc::Limits limits;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) { rpc::Reply reply; reply.error = rpc::Error::Timeout; return reply; }
        limits.total_timeout = std::min(left, std::chrono::milliseconds(10000));
        limits.connect_timeout = std::min(limits.total_timeout, std::chrono::milliseconds(3000));
        return transport.Call(candidate.connection, {++id, std::move(method), std::move(params), wallet ? candidate.wallet : std::nullopt, rpc::Effect::ReadOnly}, stop, limits);
    };
    auto network = call("getnetworkinfo");
    if (network.error != rpc::Error::None || !network.result) { result.error = network.error; return result; }
    result.authenticated = true;
    auto version = Integer(network.result->Find("version"));
    if (!version || *version < 0) { result.error = rpc::Error::Json; return result; }
    result.version = *version;
    auto chain = call("getblockchaininfo");
    if (chain.error != rpc::Error::None || !chain.result) { result.error = chain.error; return result; }
    auto height = Integer(chain.result->Find("blocks")); auto ibd = Boolean(chain.result->Find("initialblockdownload")); auto pruned = Boolean(chain.result->Find("pruned"));
    const auto* chain_name = chain.result->Find("chain");
    if (!height || *height < 0 || !ibd || !pruned || !chain_name || chain_name->kind != J::Kind::String || !Name(chain_name->text)) { result.error = rpc::Error::Json; return result; }
    result.height = *height; result.initial_download = *ibd; result.pruned = *pruned; result.chain = chain_name->text;
    auto genesis = call("getblockhash", "[0]");
    if (genesis.error != rpc::Error::None || !genesis.result) { result.error = genesis.error; return result; }
    if (genesis.result->kind != J::Kind::String || !Hash256::Parse(genesis.result->text)) { result.error = rpc::Error::Json; return result; }
    result.genesis = genesis.result->text;
    result.network_mismatch = result.genesis != candidate.expected_genesis;
    if (result.network_mismatch) return result;
    result.identity_verified = true;
    auto help = call("help");
    if (help.error != rpc::Error::None || !help.result || help.result->kind != J::Kind::String) { result.error = help.error == rpc::Error::None ? rpc::Error::Json : help.error; return result; }
    auto schema = call("getopenrpcinfo");
    if (schema.error == rpc::Error::None && schema.result) {
        if (!ReadSchema(*schema.result, result)) { result.error = rpc::Error::Json; return result; }
    } else if (schema.error == rpc::Error::Rpc && schema.rpc_code == -32601) {
        // Older/feature-reduced servers remain connectable; arguments stay
        // unproven. Only full help lines beginning with a method are accepted.
        std::string_view text = help.result->text;
        while (!text.empty()) {
            const auto end = text.find('\n'); const auto line = text.substr(0, end);
            const auto name = line.substr(0, line.find_first_of(" (\t\r"));
            if (Name(name) && !line.empty() && line.front() >= 'a' && line.front() <= 'z') {
                if (result.methods.size() == 2048) { result.error = rpc::Error::Limit; return result; }
                result.methods.emplace(std::string(name), MethodSupport{true, false, IsWalletMethod(name), false, {}});
            }
            if (end == std::string_view::npos) break; text.remove_prefix(end + 1);
        }
    } else { result.error = schema.error; return result; }
    if (!candidate.wallet) return result;
    auto wallet = call("getwalletinfo", "[]", true);
    if (wallet.error == rpc::Error::Rpc && (wallet.rpc_code == -18 || wallet.rpc_code == -19 || wallet.rpc_code == -32601)) {
        result.wallet = wallet.rpc_code == -32601 ? WalletAvailability::Unsupported : WalletAvailability::Unavailable; return result;
    }
    if (wallet.error != rpc::Error::None || !wallet.result) { result.error = wallet.error; return result; }
    const auto* name = wallet.result->Find("walletname");
    result.descriptors = Boolean(wallet.result->Find("descriptors")); result.private_keys = Boolean(wallet.result->Find("private_keys_enabled")); result.external_signer = Boolean(wallet.result->Find("external_signer"));
    const auto* scanning = wallet.result->Find("scanning");
    if (!name || name->kind != J::Kind::String || name->text != *candidate.wallet || !result.descriptors || !result.private_keys || !result.external_signer || !scanning ||
        (scanning->kind != J::Kind::Boolean && scanning->kind != J::Kind::Object)) { result.error = rpc::Error::Identity; return result; }
    result.wallet_scanning = scanning->kind == J::Kind::Object || scanning->text == "true";
    if (const auto* until = wallet.result->Find("unlocked_until")) {
        result.unlocked_until = Integer(until); if (!result.unlocked_until || *result.unlocked_until < 0) { result.error = rpc::Error::Json; return result; }
    }
    result.wallet = WalletAvailability::Available;
    if (result.Supports("listdescriptors", "private") == SupportReason::Available) {
        const auto descriptors = call("listdescriptors", "{\"private\":false}", true);
        if (descriptors.error == rpc::Error::Rpc && (descriptors.rpc_code == -4 || descriptors.rpc_code == -1)) result.methods.at("listdescriptors").probe_rejected = true;
        else if (descriptors.error != rpc::Error::None) { result.error = descriptors.error; return result; }
    }
    return result;
}
ConnectionService::ConnectionService(Probe fn, std::function<void(bool)> changed) : probe(std::move(fn)), offline_changed(std::move(changed)) {}
ConnectionService::~ConnectionService() { if (worker.joinable()) worker.request_stop(); }
bool ConnectionService::SetDraft(ServerCandidate value)
{
    if (std::this_thread::get_id() != owner || privacy_locked || revision == std::numeric_limits<uint64_t>::max()) return false;
    if (value == draft) return true;
    draft = std::move(value); ++revision; tested.reset(); tested_revision = 0;
    if (worker.joinable()) worker.request_stop();
    return true;
}
bool ConnectionService::Test()
{
    if (std::this_thread::get_id() != owner || privacy_locked || offline || Busy() || !probe || !ValidServerCandidate(draft)) return false;
    tested.reset(); tested_revision = 0;
    worker = std::jthread([this, value = draft, token = revision](std::stop_token stop) {
        BackendCapabilities result;
        try { result = probe(value, stop); } catch (...) { result.candidate = value; result.error = rpc::Error::Configuration; }
        if (stop.stop_requested()) result.error = rpc::Error::Cancelled;
        std::lock_guard lock(mutex); completed = Completion{token, std::move(result)};
    });
    return true;
}
void ConnectionService::Poll()
{
    if (std::this_thread::get_id() != owner) return;
    std::optional<Completion> item;
    { std::lock_guard lock(mutex); if (!completed) return; item = std::move(completed); completed.reset(); }
    worker.join();
    if (!privacy_locked && item->revision == revision && item->result.candidate == draft) { tested_revision = revision; tested = std::move(item->result); }
}
void ConnectionService::Cancel()
{
    if (std::this_thread::get_id() != owner) return;
    if (worker.joinable()) worker.request_stop();
    if (revision != std::numeric_limits<uint64_t>::max()) ++revision;
    tested.reset(); tested_revision = 0;
}
bool ConnectionService::CanApply() const
{
    return std::this_thread::get_id() == owner && !privacy_locked && !offline && !Busy() && tested && tested_revision == revision && tested->candidate == draft && tested->CanApply() && session != std::numeric_limits<uint64_t>::max();
}
bool ConnectionService::SetOffline(bool value)
{
    if (std::this_thread::get_id() != owner || (privacy_locked && !value)) return false;
    if (offline == value) return true;
    offline = value; Cancel(); if (offline_changed) offline_changed(value); return true;
}
bool ConnectionService::Apply()
{
    if (!CanApply()) return false;
    try {
        auto replacement = tested; // prepare fully before replacing working state
        active.swap(replacement); ++session; tested.reset(); tested_revision = 0; return true;
    } catch (...) { return false; } // working profile survives allocation failure
}
bool ConnectionService::AdoptWallet(BackendCapabilities wallet, uint64_t expected_session, uint64_t expected_revision)
{
    if (std::this_thread::get_id() != owner || privacy_locked || offline || Busy() || !active
        || expected_session != session || expected_revision != revision || session == std::numeric_limits<uint64_t>::max()
        || !wallet.CanApply() || wallet.wallet != WalletAvailability::Available || wallet.descriptors != true
        || !wallet.candidate.wallet || wallet.candidate.connection != active->candidate.connection
        || wallet.candidate.expected_genesis != active->candidate.expected_genesis || wallet.genesis != active->genesis) return false;
    active = std::move(wallet);
    ++session;
    tested.reset();
    tested_revision = 0;
    return true;
}
void ConnectionService::SetPrivacyLocked(bool value)
{
    if(std::this_thread::get_id()!=owner || privacy_locked==value)return;
    privacy_locked=value;Cancel();if(value){offline=true;active.reset();if(offline_changed)offline_changed(true);}
}
}
