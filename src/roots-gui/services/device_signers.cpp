// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/device_signers.h"
#include "adapters/protocol/public_wallet.h"

#include <algorithm>
#include <charconv>
#include <set>
#include <tuple>

namespace roots::gui {
namespace {
using J = rpc::Json;
constexpr size_t MAX_DEVICES = 16;
constexpr size_t MAX_BRANCHES = 32;

bool Is(const J* field, J::Kind kind, std::string_view text)
{
    return field && field->kind == kind && field->text == text;
}

std::optional<DeviceScope> Scope(const ConnectionService* connection)
{
    if (!connection || connection->Offline() || connection->PrivacyLocked() || !connection->Active()
        || !connection->Active()->CanApply() || !connection->Active()->candidate.wallet
        || connection->Active()->wallet != WalletAvailability::Available) return {};
    const auto& caps = *connection->Active();
    if (caps.genesis != caps.candidate.expected_genesis || protocol::ReceiveChain(caps.genesis) != caps.chain
        || caps.chain.empty()) return {};
    return DeviceScope{caps.candidate, caps.chain, connection->Session(), connection->Revision()};
}

std::optional<uint32_t> Index(std::string_view text)
{
    uint32_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value >= 0x80000000) return {};
    return value;
}

std::optional<uint32_t> Index(const J& value)
{
    return value.kind == J::Kind::Number ? Index(value.text) : std::nullopt;
}

std::string Fingerprint(std::string fingerprint)
{
    if (fingerprint.size() != 8) return {};
    for (char& c : fingerprint) {
        if (c >= 'A' && c <= 'F') c += 'a' - 'A';
        if (!(c >= 'a' && c <= 'f') && !(c >= '0' && c <= '9')) return {};
    }
    return fingerprint;
}

std::optional<protocol::PublicSigner> SingleKey(std::string_view descriptor, std::string_view chain, bool ranged)
{
    const auto parsed = protocol::DerivePublicDescriptor(descriptor, chain, 0);
    if (!parsed || parsed->signers.size() != 1 || parsed->choice.multisig || parsed->ranged != ranged) return {};
    auto signer = protocol::InspectPublicSigner(parsed->signers.front(), chain,
        parsed->choice.script == protocol::ScriptTemplate::Taproot).signer;
    if (!signer || Fingerprint(signer->fingerprint).empty() || signer->origin_path.empty()) return {};
    if (ranged && (!signer->extended || !signer->derivation_suffix.ends_with("/*"))) return {};
    return signer;
}

DeviceEvidence Observe(rpc::Transport& transport, const DeviceScope& scope, std::stop_token stop)
{
    const auto failure = [](DeviceState state) {return DeviceEvidence{state,{}, {},{}};};
    const auto& profile = scope.profile;
    uint64_t id{};
    const auto call = [&](const char* method, std::string params, bool wallet, size_t bytes) {
        rpc::Limits limits;
        limits.connect_timeout = std::chrono::seconds(2);
        limits.total_timeout = std::chrono::seconds(4);
        limits.response_bytes = bytes;
        limits.json_depth = 16;
        limits.json_nodes = 4096;
        return transport.Call(profile.connection, {++id, method, std::move(params),
            wallet ? profile.wallet : std::nullopt, rpc::Effect::ReadOnly}, stop, limits);
    };
    const auto genesis = call("getblockhash", "[0]", false, 4096);
    if (genesis.error != rpc::Error::None) return failure(SignerFailure(genesis));
    if (!genesis.result || !Is(&*genesis.result, J::Kind::String, profile.expected_genesis))
        return failure(DeviceState::Failure);
    const auto wallet = call("getwalletinfo", "[]", true, 16384);
    if (wallet.error != rpc::Error::None) return failure(SignerFailure(wallet));
    if (!wallet.result || !Is(wallet.result->Find("walletname"), J::Kind::String, *profile.wallet)
        || !Is(wallet.result->Find("descriptors"), J::Kind::Boolean, "true")
        || !Is(wallet.result->Find("external_signer"), J::Kind::Boolean, "true")
        || !Is(wallet.result->Find("private_keys_enabled"), J::Kind::Boolean, "false"))
        return failure(DeviceState::Unsupported);
    const auto list = call("listdescriptors", "{\"private\":false}", true, 65536);
    if (list.error != rpc::Error::None) return failure(SignerFailure(list));
    if (!list.result || !Is(list.result->Find("wallet_name"), J::Kind::String, *profile.wallet))
        return failure(DeviceState::Failure);
    const auto* descriptors = list.result->Find("descriptors");
    if (!descriptors || descriptors->kind != J::Kind::Array || descriptors->items.size() > MAX_BRANCHES)
        return failure(DeviceState::Failure);
    DeviceEvidence evidence{DeviceState::Select,{}, {},{}};
    std::set<std::string> identifiers;
    bool receiving{};
    for (const auto& item : descriptors->items) {
        const auto* active = item.Find("active");
        if (Is(active, J::Kind::Boolean, "false")) continue;
        const auto* desc = item.Find("desc");
        const auto* internal = item.Find("internal");
        const auto* range = item.Find("range");
        if (!Is(active, J::Kind::Boolean, "true") || !desc || desc->kind != J::Kind::String || desc->text.size() > 1024
            || !internal || internal->kind != J::Kind::Boolean || !range || range->kind != J::Kind::Array
            || range->items.size() != 2 || !identifiers.insert(desc->text).second) return failure(DeviceState::Failure);
        const auto first = Index(range->items[0]);
        const auto last = Index(range->items[1]);
        const auto signer = SingleKey(desc->text, scope.chain, true);
        if (!first || !last || *first > *last || !signer) return failure(DeviceState::Unsupported);
        if (evidence.fingerprint.empty()) evidence.fingerprint = signer->fingerprint;
        if (evidence.fingerprint != signer->fingerprint) return failure(DeviceState::Unsupported);
        const bool change = internal->text == "true";
        receiving |= !change;
        evidence.branches.push_back({desc->text, change, *first, *last});
    }
    if (!receiving || evidence.fingerprint.empty()) return failure(DeviceState::Unsupported);
    std::ranges::sort(evidence.branches, {}, &DevicePolicyBranch::descriptor);

    if (stop.stop_requested()) return failure(DeviceState::Idle);
    const auto reply = call("enumeratesigners", "[]", false, 16384);
    if (reply.error != rpc::Error::None) return failure(SignerFailure(reply));
    const auto* devices = reply.result ? reply.result->Find("signers") : nullptr;
    if (!devices || devices->kind != J::Kind::Array || devices->items.size() > MAX_DEVICES)
        return failure(DeviceState::Failure);
    std::set<std::string> fingerprints;
    for (const auto& item : devices->items) {
        const auto* id_field = item.Find("fingerprint");
        const auto* name = item.Find("name");
        if (!id_field || id_field->kind != J::Kind::String || !name || name->kind != J::Kind::String
            || name->text.size() > 96 || !std::ranges::all_of(name->text, [](unsigned char c) {return c >= 32 && c <= 126;}))
            return failure(DeviceState::Failure);
        auto id = Fingerprint(id_field->text);
        if (id.empty() || !fingerprints.insert(id).second) return failure(DeviceState::Failure);
        if (id == evidence.fingerprint) evidence.label = name->text;
    }
    if (fingerprints.empty()) return failure(DeviceState::NoDevice);
    if (fingerprints.size() != 1) return failure(DeviceState::Multiple);
    if (!fingerprints.contains(evidence.fingerprint)) return failure(DeviceState::NoMatch);
    return evidence;
}
} // namespace

const char* DeviceStateText(DeviceState state)
{
    switch (state) {
    case DeviceState::Idle: return "Scan for a device through the daemon, or use PSBT file or QR exchange.";
    case DeviceState::Scanning: return "Scanning through the daemon…";
    case DeviceState::Unconfigured: return "The daemon reports that its external signer command is unconfigured.";
    case DeviceState::Unsupported: return "This daemon or external wallet policy does not support the device route. Use PSBT file or QR exchange.";
    case DeviceState::NoDevice: return "The daemon reported no device.";
    case DeviceState::NoMatch: return "The device reported by the daemon does not match the wallet fingerprint.";
    case DeviceState::Multiple: return "The daemon requires a single connected signer. Rescan after disconnecting the others.";
    case DeviceState::Select: return "The daemon reported one signer matching this wallet. Select it explicitly.";
    case DeviceState::Ready: return "Matching signer selected. Follow prompts on the device or its external signer.";
    case DeviceState::Confirm: return "Waiting for the daemon. Check the request on the device.";
    case DeviceState::Locked: return "The external signer reports that the device is locked. Unlock on the device.";
    case DeviceState::Passphrase: return "The external signer requests a passphrase. Use the device or its external signer.";
    case DeviceState::Disconnected: return "The connection or selected signer could no longer be verified. Rescan explicitly.";
    case DeviceState::Failure: return "The daemon signer request failed or its reply could not be verified.";
    }
    return "Device status unavailable.";
}

DeviceState SignerFailure(const rpc::Reply& reply)
{
    using N = rpc::SignerNotice;
    switch (reply.signer_notice) {
    case N::Unconfigured: return DeviceState::Unconfigured;
    case N::Absent: return DeviceState::NoDevice;
    case N::Multiple: return DeviceState::Multiple;
    case N::Locked: return DeviceState::Locked;
    case N::Passphrase: return DeviceState::Passphrase;
    case N::Disconnected: return DeviceState::Disconnected;
    case N::None: break;
    }
    if (reply.rpc_code == -32601) return DeviceState::Unsupported;
    if (reply.error == rpc::Error::Connection || reply.error == rpc::Error::Timeout
        || reply.error == rpc::Error::Cancelled) return DeviceState::Disconnected;
    return DeviceState::Failure;
}

bool CanDiscoverDevice(const BackendCapabilities& caps)
{
    return caps.descriptors == true && caps.external_signer == true && caps.private_keys == false
        && caps.Supports("enumeratesigners") == SupportReason::Available
        && caps.Supports("getblockhash") == SupportReason::Available
        && caps.Supports("getwalletinfo") == SupportReason::Available
        && caps.Supports("listdescriptors", "private") == SupportReason::Available;
}

DeviceSelection::Operation::Operation(const DeviceSelection& value)
    : selection(value), acquired(value.Live() && !value.in_use.test_and_set())
{
}
DeviceSelection::Operation::~Operation()
{
    if (acquired) selection.in_use.clear();
}
void DeviceSelection::Retire(DeviceState reason) const
{
    auto expected=DeviceState::Ready;
    state.compare_exchange_strong(expected,reason);
}
bool DeviceSelection::Current(const ConnectionService& connection) const
{
    return Live() && Scope(&connection) == scope && CanDiscoverDevice(*connection.Active());
}
bool DeviceSelection::Matches(const ServerCandidate& profile, uint64_t session, uint64_t revision) const
{
    return Live() && scope.profile == profile && scope.session == session && scope.revision == revision;
}
DeviceState DeviceSelection::Recheck(rpc::Transport& transport, std::stop_token stop) const
{
    if (!Live() || stop.stop_requested()) return DeviceState::Idle;
    const auto latest = Observe(transport, scope, stop);
    if (!Live() || stop.stop_requested()) return DeviceState::Idle;
    if (latest.state != DeviceState::Select)
        return latest.state == DeviceState::NoDevice ? DeviceState::Disconnected : latest.state;
    if (evidence.fingerprint != latest.fingerprint || evidence.branches.size() != latest.branches.size())
        return DeviceState::NoMatch;
    // Ranges may advance as addresses are issued. Never accept decreased
    // coverage or changed public keys/derivation policy for this selection.
    for (size_t i = 0; i < evidence.branches.size(); ++i) {
        const auto& old = evidence.branches[i];
        const auto& current = latest.branches[i];
        if (old.descriptor != current.descriptor || old.internal != current.internal
            || current.first > old.first || current.last < old.last) return DeviceState::NoMatch;
    }
    return DeviceState::Ready;
}
bool DeviceSelection::Owns(std::string_view descriptor, std::string_view exact_script, bool receive_only,
    std::optional<uint32_t> requested_index) const
{
    if (requested_index) {
        const auto saved = std::ranges::find(evidence.branches, descriptor, &DevicePolicyBranch::descriptor);
        if (saved == evidence.branches.end() || (receive_only && saved->internal)
            || *requested_index < saved->first || *requested_index > saved->last) return false;
        const auto derived = protocol::DerivePublicDescriptor(descriptor, scope.chain, *requested_index);
        return derived && derived->script == exact_script;
    }
    const auto fixed = SingleKey(descriptor, scope.chain, false);
    if (!fixed || fixed->fingerprint != evidence.fingerprint) return false;
    const auto last_slash = fixed->origin_path.rfind('/');
    if (last_slash == std::string::npos) return false;
    const auto index = Index(std::string_view(fixed->origin_path).substr(last_slash + 1));
    if (!index) return false;
    const auto concrete = protocol::DerivePublicDescriptor(descriptor, scope.chain, 0);
    if (!concrete || concrete->script != exact_script) return false;
    for (const auto& branch : evidence.branches) {
        if ((receive_only && branch.internal) || *index < branch.first || *index > branch.last) continue;
        const auto key = SingleKey(branch.descriptor, scope.chain, true);
        if (!key) continue;
        const auto expected_path = key->origin_path + key->derivation_suffix.substr(0, key->derivation_suffix.size() - 1)
            + std::to_string(*index);
        if (fixed->origin_path != expected_path) continue;
        const auto derived = protocol::DerivePublicDescriptor(branch.descriptor, scope.chain, *index);
        if (derived && derived->script == exact_script) return true;
    }
    return false;
}

DeviceSigners::~DeviceSigners() {Cancel();}
bool DeviceSigners::Rescan(const ConnectionService& connection)
{
    if (std::this_thread::get_id() != owner || worker.joinable() || (selection && selection->Busy())
        || (retiring && retiring->Busy())) return false;
    auto current = Scope(&connection);
    if (!current || !CanDiscoverDevice(*connection.Active())) return false;
    Cancel();
    scope = *current;
    report.state = DeviceState::Scanning;
    try {
        worker = std::jthread([this, current = std::move(*current)](std::stop_token stop) {
            DeviceEvidence result;
            try {result = Observe(transport, current, stop);} catch (...) {}
            std::lock_guard lock(mutex);
            completion = std::move(result);
        });
    } catch (...) {
        report.state = DeviceState::Failure;
        return false;
    }
    return true;
}
bool DeviceSigners::Select(const ConnectionService& connection)
{
    if (std::this_thread::get_id() != owner || !scope || Scope(&connection) != scope
        || !CanDiscoverDevice(*connection.Active()) || Status() != DeviceState::Select) return false;
    selection = SelectedDevice(new DeviceSelection(*scope, report));
    report.state = DeviceState::Ready;
    return true;
}
void DeviceSigners::Poll(const ConnectionService* connection)
{
    if (std::this_thread::get_id() != owner) return;
    if (retiring && !retiring->Busy()) retiring.reset();
    if (scope && Scope(connection) != scope) Cancel();
    if (!worker.joinable()) return;
    std::optional<DeviceEvidence> result;
    {
        std::lock_guard lock(mutex);
        if (!completion) return;
        result = std::move(completion);
        completion.reset();
    }
    worker.join();
    if (scope) {
        report = std::move(*result);
        select_by = std::chrono::steady_clock::now() + std::chrono::minutes(2);
    }
}
void DeviceSigners::Cancel()
{
    if (std::this_thread::get_id() != owner) return;
    if (selection) {
        selection->Retire();
        if (selection->Busy()) retiring = selection;
    }
    selection.reset();
    scope.reset();
    report = {DeviceState::Idle,{}, {},{}};
    if (worker.joinable()) worker.request_stop();
}
DeviceState DeviceSigners::Status() const
{
    if (selection && !selection->Live()) return selection->RetiredState();
    if (selection && selection->Busy()) return DeviceState::Confirm;
    if (report.state == DeviceState::Select && std::chrono::steady_clock::now() >= select_by) return DeviceState::Idle;
    return report.state;
}
std::string_view DeviceSigners::Fingerprint() const
{
    return Status() == DeviceState::Select || Status() == DeviceState::Ready || Status() == DeviceState::Confirm
        ? std::string_view(report.fingerprint) : std::string_view{};
}
std::string_view DeviceSigners::Label() const
{
    return Fingerprint().empty() ? std::string_view{} : std::string_view(report.label);
}
SelectedDevice DeviceSigners::Current(const ConnectionService* connection) const
{
    return std::this_thread::get_id() == owner && selection && connection && selection->Current(*connection)
        ? selection : SelectedDevice{};
}
}
