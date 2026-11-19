// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/receive.h"
#include "adapters/protocol/public_wallet.h"
#include <algorithm>
#include <chrono>

namespace roots::gui {
namespace {
using J = rpc::Json;
bool StringIs(const J* field, std::string_view value)
{
    return field && field->kind == J::Kind::String && field->text == value;
}
bool True(const J* field)
{
    return field && field->kind == J::Kind::Boolean && field->text == "true";
}
bool Current(const ReceiveRequest& operation, const ConnectionService* connection)
{
    return connection && !connection->Offline() && !connection->PrivacyLocked() && connection->Active()
        && connection->Active()->wallet == WalletAvailability::Available && connection->Active()->descriptors == true
        && operation.identity.profile == connection->Active()->candidate
        && operation.identity.context.network.genesis.hex() == connection->Active()->genesis
        && protocol::ReceiveChain(connection->Active()->genesis) == connection->Active()->chain
        && operation.identity.connection_session == connection->Session()
        && operation.profile_revision == connection->Revision();
}
}
bool ReceiveAllocation(ReceiveAction action)
{
    return action == ReceiveAction::Next || action == ReceiveAction::Reserve;
}
bool CanAllocateReceive(const BackendCapabilities& backend)
{
    return backend.descriptors == true && backend.candidate.wallet.has_value()
        && !protocol::ReceiveChain(backend.genesis).empty() && protocol::ReceiveChain(backend.genesis) == backend.chain
        && backend.Supports("getnewaddress") == SupportReason::Available
        && backend.Supports("getaddressinfo", "address") == SupportReason::Available
        && backend.Supports("getwalletinfo") == SupportReason::Available;
}
bool CanVerifyReceiveOnDevice(const BackendCapabilities& backend)
{
    return CanDiscoverDevice(backend) && backend.Supports("walletdisplayaddress", "address") == SupportReason::Available
        && backend.Supports("getaddressinfo", "address") == SupportReason::Available
        && backend.Supports("getwalletinfo") == SupportReason::Available;
}

ReceiveReply FetchReceive(rpc::Transport& transport, const ReceiveRequest& operation, std::stop_token stop)
{
    ReceiveReply result;
    const auto fail = [&result](const char* error) {
        result.error = error;
        return std::move(result);
    };
    const auto& identity = operation.identity;
    const auto& profile = identity.profile;
    auto network = identity.context.network.genesis.hex();
    const auto chain = protocol::ReceiveChain(network);
    if (!profile.wallet || profile.expected_genesis != network || chain.empty()) return fail("Receive is unavailable for this wallet network.");
    const bool displaying = operation.action == ReceiveAction::VerifyDevice;
    if (!displaying && !ReceiveAllocation(operation.action) && operation.action != ReceiveAction::Inspect)
        return fail("Unsupported receive operation.");
    const auto& device = operation.device;
    if (displaying && (!device || !device->Matches(profile, identity.connection_session, operation.profile_revision)))
        return fail("Select the current wallet's matching daemon signer first.");
    std::optional<DeviceSelection::Operation> device_operation;
    if (displaying) {
        device_operation.emplace(*device);
        if (!*device_operation) return fail("Wait for the current signer operation to settle.");
    }
    const auto device_failure = [&](DeviceState state) {
        device->Retire(state);
        return fail(DeviceStateText(state));
    };
    uint64_t request_id{};
    auto call = [&](const char* method, std::string params, bool wallet, rpc::Effect effect = rpc::Effect::ReadOnly) {
        rpc::Limits limits;
        limits.response_bytes = 1024 * 1024;
        if (effect == rpc::Effect::Mutation && displaying) {
            limits.response_bytes = 4096;
            limits.total_timeout = std::chrono::seconds(30);
        }
        return transport.Call(profile.connection, {++request_id, method, std::move(params), wallet ? profile.wallet : std::nullopt, effect}, stop, limits);
    };
    const auto verify_wallet = [&] {
        if (stop.stop_requested()) return false;
        const auto genesis = call("getblockhash", "[0]", false);
        if (genesis.error != rpc::Error::None || !genesis.result || !StringIs(&*genesis.result, network) || stop.stop_requested()) return false;
        const auto wallet = call("getwalletinfo", "[]", true);
        return wallet.error == rpc::Error::None && wallet.result && StringIs(wallet.result->Find("walletname"), *profile.wallet)
            && True(wallet.result->Find("descriptors")) && !stop.stop_requested();
    };
    if (!verify_wallet()) {
        if (displaying) device->Retire(DeviceState::Disconnected);
        return fail("Authenticated wallet and network could not be reverified.");
    }
    if (operation.action == ReceiveAction::Inspect) {
        result.inspected = true;
        return result;
    }
    std::string address;
    if (ReceiveAllocation(operation.action)) {
        result.allocation_dispatched = true;
        const auto allocated = call("getnewaddress", "[]", true, rpc::Effect::Mutation);
        if (allocated.error != rpc::Error::None || !allocated.result || allocated.result->kind != J::Kind::String
            || allocated.result->text.empty() || allocated.result->text.size() > 128) {
            if (allocated.error != rpc::Error::None && !allocated.mutation_uncertain) result.allocation_dispatched = false;
            return fail("The daemon allocation could not be confirmed.");
        }
        address = allocated.result->text;
    } else {
        if (!operation.selected || operation.selected->chain != chain || !protocol::VerifyReceiveTarget(*operation.selected))
            return fail("Select a verified receive address for the signer.");
        address = operation.selected->address;
    }
    const auto params = "{\"address\":" + rpc::Quote(address) + '}';
    const auto detail = call("getaddressinfo", params, true);
    if (detail.error != rpc::Error::None || !detail.result || !StringIs(detail.result->Find("address"), address)
        || !True(detail.result->Find("ismine"))) {
        if (displaying) device->Retire(DeviceState::Failure);
        return fail("The daemon did not prove ownership of this exact address.");
    }
    const auto* script = detail.result->Find("scriptPubKey");
    const auto* descriptor = detail.result->Find("desc");
    const auto* path = detail.result->Find("hdkeypath");
    if (!script || script->kind != J::Kind::String || !descriptor || descriptor->kind != J::Kind::String
        || (path && path->kind != J::Kind::String)) return fail("The daemon did not return a complete public address proof.");
    ReceiveTarget proven{address, script->text, descriptor->text, path ? path->text : "", std::string(chain), {},
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()};
    if (!protocol::VerifyReceiveTarget(proven)) return fail("The address, script and public descriptor do not agree.");
    if (ReceiveAllocation(operation.action)) {
        if (!verify_wallet()) return fail("The allocation was accepted but the same wallet and network could not be confirmed.");
        result.address = std::move(proven);
        return result;
    }
    if (proven.script != operation.selected->script || !device->Owns(operation.selected->descriptor, proven.script, true,operation.selected->index)
        || !device->Owns(proven.descriptor, proven.script, true))
        return device_failure(DeviceState::NoMatch);
    const auto before = device->Recheck(transport, stop);
    if (before != DeviceState::Ready) return device_failure(before);
    if (!device->Live() || stop.stop_requested()) return device_failure(DeviceState::Idle);
    const auto shown = call("walletdisplayaddress", params, true, rpc::Effect::Mutation);
    if (shown.error != rpc::Error::None) return device_failure(SignerFailure(shown));
    if (!shown.result || !StringIs(shown.result->Find("address"), address))
        return device_failure(DeviceState::Failure);
    const auto after = device->Recheck(transport, stop);
    if (after != DeviceState::Ready) return device_failure(after);
    result.signer_echo = true;
    return result;
}

std::optional<std::string> ReceiveUri(std::string_view address, std::string_view label, std::optional<Money> amount)
{
    const auto alphanumeric = [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    };
    if (address.empty() || address.size() > 128 || !std::ranges::all_of(address, alphanumeric)
        || label.size() > 255 || !rpc::ValidUtf8(label) || std::ranges::any_of(label, [](unsigned char c) {return c < 32 || c == 127;})
        || (amount && (amount->sats() < 0 || amount->sats() > 2100000000000000LL))) return {};
    std::string uri = "bitcoin:" + std::string(address);
    if (amount) uri += "?amount=" + amount->btc();
    if (!label.empty()) {
        uri += amount ? "&label=" : "?label=";
        constexpr std::string_view hex = "0123456789ABCDEF";
        for (const unsigned char c : label) {
            if (alphanumeric(c) || c == '-' || c == '_' || c == '.' || c == '~') uri += char(c);
            else {
                uri += '%';
                uri += hex[c >> 4];
                uri += hex[c & 15];
            }
        }
    }
    return uri;
}

std::optional<int64_t> ReceiveLastUsed(const HistoryLedger& ledger, const ReceiveTarget& address)
{
    std::optional<int64_t> last;
    for (const auto& [id, row] : ledger.Rows()) {
        if (row.time <= 0 || (row.state != HistoryState::Confirmed && row.state != HistoryState::Pending && row.state != HistoryState::Immature)) continue;
        if (std::ranges::any_of(row.io, [&](const auto& io) {
            return io.kind == "Output" && io.script == address.script;
        })) last = std::max(last.value_or(0), row.time);
    }
    return last;
}

ReceiveService::~ReceiveService() {Cancel();}
bool ReceiveService::Start(ReceiveRequest operation, const ConnectionService& connection)
{
    if (std::this_thread::get_id() != owner || Busy() || !fetch || !Current(operation, &connection)) return false;
    const auto& caps = *connection.Active();
    if (operation.action != ReceiveAction::VerifyDevice && operation.device) return false;
    if (ReceiveAllocation(operation.action) && (!operation.ticket || !CanAllocateReceive(caps))) return false;
    if (operation.action == ReceiveAction::Inspect && (!operation.ticket || caps.Supports("getwalletinfo") != SupportReason::Available)) return false;
    if (operation.action == ReceiveAction::VerifyDevice && (!operation.selected || !operation.device
        || !operation.device->Current(connection) || operation.device->Busy() || !CanVerifyReceiveOnDevice(caps))) return false;
    try {
        running = operation;
        retired = false;
        worker = std::jthread([this, operation = std::move(operation)](std::stop_token stop) {
            ReceiveReply reply;
            try {
                reply = fetch(operation, stop);
            } catch (...) {
                reply.allocation_dispatched = ReceiveAllocation(operation.action);
                reply.error = "The receive operation could not be verified.";
            }
            std::lock_guard guard(mutex);
            completed = std::move(reply);
        });
    } catch (...) {
        running.reset();
        return false;
    }
    return true;
}
void ReceiveService::Cancel()
{
    if (std::this_thread::get_id() != owner || !Busy()) return;
    retired = true;
    if (running && running->device) running->device->Retire();
    worker.request_stop();
}
bool ReceiveService::CancelDevice()
{
    if (std::this_thread::get_id()!=owner || !running || running->action!=ReceiveAction::VerifyDevice) return false;
    Cancel();
    return true;
}
std::optional<std::pair<ReceiveRequest, ReceiveReply>> ReceiveService::Poll(const ConnectionService* connection,
    const std::optional<RefreshIdentity>& identity, const std::optional<ReceiveTarget>& selected)
{
    if (std::this_thread::get_id() != owner || !running) return {};
    if (!Current(*running, connection) || running->identity != identity
        || (running->action == ReceiveAction::VerifyDevice && (running->selected != selected
            || (running->device && !running->device->Live() && running->device->RetiredState() == DeviceState::Idle)))) Cancel();
    std::optional<ReceiveReply> result;
    {
        std::lock_guard guard(mutex);
        if (!completed) return {};
        result = std::move(completed);
        completed.reset();
    }
    worker.join();
    auto operation = std::move(*running);
    running.reset();
    if (retired) return {};
    if (result->signer_echo && (!operation.device || !operation.device->Current(*connection))) return {};
    return std::pair{std::move(operation), std::move(*result)};
}
}
