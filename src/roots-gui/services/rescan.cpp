// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/rescan.h"
#include <charconv>

namespace roots::gui {
namespace {
using J = rpc::Json;
std::optional<int64_t> Number(const J* field)
{
    if (!field || field->kind != J::Kind::Number) return {};
    int64_t n{};
    const auto& text = field->text;
    const auto result = std::from_chars(text.data(), text.data()+text.size(), n);
    if (result.ec != std::errc{} || result.ptr != text.data()+text.size() || n < 0) return {};
    return n;
}
std::optional<bool> Boolean(const J* field)
{
    if (!field || field->kind != J::Kind::Boolean) return {};
    return field->text == "true";
}
bool Exact(const J* field, std::string_view text)
{
    return field && field->kind == J::Kind::String && field->text == text;
}
std::optional<Hash256> Hash(const J* value)
{
    if (!value || value->kind != J::Kind::String) return {};
    auto hash = Hash256::Parse(value->text);
    if (!hash || hash->hex() != value->text) return {};
    return hash;
}
std::optional<ScanObservation> Decode(const J& chain, const J& wallet)
{
    const auto blocks = Number(chain.Find("blocks"));
    const auto pruned = Boolean(chain.Find("pruned"));
    const auto syncing = Boolean(chain.Find("initialblockdownload"));
    const auto* cursor = wallet.Find("lastprocessedblock");
    const auto height = cursor ? Number(cursor->Find("height")) : std::nullopt;
    const auto* scanning = wallet.Find("scanning");
    if (!blocks || *blocks > std::numeric_limits<int>::max() || !pruned || !syncing
        || !height || *height > *blocks || !Hash(cursor->Find("hash")) || !scanning
        || (scanning->kind != J::Kind::Object && Boolean(scanning) != false)) return {};
    const auto earliest = *pruned ? Number(chain.Find("pruneheight")) : std::optional<int64_t>{0};
    if (!earliest || *earliest > *blocks) return {};
    const auto* until = wallet.Find("unlocked_until");
    const auto expiry = Number(until);
    if (until && !expiry) return {};
    return ScanObservation{*blocks, *height, *earliest, *pruned, *syncing,
        scanning->kind == J::Kind::Object, expiry == 0, Number(wallet.Find("birthtime")),
        Number(wallet.Find("keypoolsize")), Number(wallet.Find("keypoolsize_hd_internal")), {}};
}
bool Current(const ScanRequest& operation, const ConnectionService* connection)
{
    return connection && !connection->Offline() && !connection->PrivacyLocked() && connection->Active()
        && CanObserveScan(*connection->Active()) && operation.identity.profile == connection->Active()->candidate
        && operation.identity.context.network.genesis.hex() == connection->Active()->genesis
        && operation.identity.connection_session == connection->Session() && operation.profile_revision == connection->Revision();
}
}
bool CanObserveScan(const BackendCapabilities& caps)
{
    return caps.CanApply() && caps.wallet == WalletAvailability::Available && caps.candidate.wallet && caps.descriptors == true
        && !protocol::ReceiveChain(caps.genesis).empty() && protocol::ReceiveChain(caps.genesis) == caps.chain
        && caps.Supports("getwalletinfo") == SupportReason::Available
        && caps.Supports("getblockhash") == SupportReason::Available
        && caps.Supports("getblockchaininfo") == SupportReason::Available;
}
bool CanStartScan(const BackendCapabilities& caps)
{
    return CanObserveScan(caps) && caps.Supports("rescanblockchain", "start_height") == SupportReason::Available
        && caps.Supports("rescanblockchain", "stop_height") == SupportReason::Available;
}
std::string ValidateScanRange(const ScanObservation& status, ScanRange range)
{
    if (!range.Valid()) return "Enter ordered, nonnegative block heights.";
    if (status.scanning) return "This wallet already has a scan in progress.";
    if (status.wallet_locked) return "Unlock this wallet using its daemon owner before rescanning.";
    if (status.initial_download) return "Wait for the node to finish initial block download.";
    if (range.first < status.first_available) return "Earlier blocks were pruned. Use a backend that retains the required history.";
    if (range.last > status.chain_height || range.last > status.wallet_height)
        return "The end height is beyond the blocks this wallet has processed.";
    return {};
}

std::optional<int64_t> ReportedScanEnd(const rpc::Json& result, ScanRange requested)
{
    const auto end = Number(result.Find("stop_height"));
    if (!requested.Valid() || Number(result.Find("start_height")) != requested.first
        || !end || *end < requested.first || *end > requested.last) return {};
    return end;
}

ScanReply FetchScan(rpc::Transport& transport, const ScanRequest& request, std::stop_token stop)
{
    ScanReply reply;
    const auto fail = [&reply](std::string error) {
        reply.error = std::move(error);
        return std::move(reply);
    };
    const auto& profile = request.identity.profile;
    const auto genesis = request.identity.context.network.genesis.hex();
    const auto chain_name = protocol::ReceiveChain(genesis);
    if (!profile.wallet || profile.expected_genesis != genesis || chain_name.empty())
        return fail("This wallet's network has no verified rescan service.");
    uint64_t id{};
    const auto call = [&](const char* method, std::string params, bool wallet = false, rpc::Effect effect = rpc::Effect::ReadOnly) {
        rpc::Limits limits;
        limits.response_bytes = 2 * 1024 * 1024;
        if (effect == rpc::Effect::Mutation) limits.total_timeout = std::chrono::minutes(5);
        return transport.Call(profile.connection, {++id, method, std::move(params), wallet ? profile.wallet : std::nullopt, effect}, stop, limits);
    };
    const auto lookup = [&](int64_t height) -> std::optional<Hash256> {
        const auto observed = call("getblockhash", '[' + std::to_string(height) + ']');
        return observed.error == rpc::Error::None && observed.result ? Hash(&*observed.result) : std::nullopt;
    };
    const auto exact_wallet = [&](const rpc::Reply& observed) {
        return observed.error == rpc::Error::None && observed.result
            && Exact(observed.result->Find("walletname"), *profile.wallet)
            && Boolean(observed.result->Find("descriptors")) == true;
    };
    if (lookup(0) != request.identity.context.network.genesis) return fail("The authenticated chain no longer matches this wallet.");
    const auto chain = call("getblockchaininfo", "[]");
    const auto wallet = call("getwalletinfo", "[]", true);
    if (stop.stop_requested() || !exact_wallet(wallet) || chain.error != rpc::Error::None || !chain.result
        || !Exact(chain.result->Find("chain"), chain_name))
        return fail("The authenticated daemon wallet and chain could not be confirmed.");
    reply.observation = Decode(*chain.result, *wallet.result);
    if (!reply.observation) return fail("The daemon did not report verifiable scan bounds or wallet state.");
    if (request.action == ScanAction::Check) {
        if (request.read_descriptors) {
            const auto descriptors = call("listdescriptors", "{\"private\":false}", true);
            if (descriptors.error == rpc::Error::None && descriptors.result) {
                if (auto policy = ValidateWalletPolicy(*descriptors.result, *profile.wallet, chain_name);
                        policy && !policy->unsupported) {
                    reply.observation->active_ranges.emplace();
                    for (const auto& branch : policy->branches)
                        if (branch.active && branch.range) reply.observation->active_ranges->push_back(*branch.range);
                }
            }
        }
        return reply;
    }
    if (request.action == ScanAction::Acknowledge) {
        if (reply.observation->scanning) return fail("The wallet still reports a scan in progress.");
        return reply;
    }
    if (auto invalid = ValidateScanRange(*reply.observation, request.range); !invalid.empty()) return fail(std::move(invalid));
    const auto last = lookup(request.range.last);
    if (!last || stop.stop_requested()) return fail("The end block could not be verified.");
    if (request.action == ScanAction::Review) {
        reply.review = ScanReview{request.range, *last};
        return reply;
    }
    if (request.reviewed_block != last) return fail("The reviewed chain changed. Check the wallet and review the range again.");
    const auto params = "{\"start_height\":" + std::to_string(request.range.first)
        + ",\"stop_height\":" + std::to_string(request.range.last) + '}';
    reply.dispatched = true;
    const auto scan = call("rescanblockchain", params, true, rpc::Effect::Mutation);
    if (scan.error != rpc::Error::None || !scan.result) {
        // A daemon error can follow partial writes. Only a transport-proven
        // non-dispatch or rejection permits clearing the pending ticket.
        if (scan.error != rpc::Error::Rpc && !scan.mutation_uncertain) reply.dispatched = false;
        return fail(reply.dispatched ? "The daemon's scan outcome is unconfirmed. History may be partial."
                                     : "The daemon did not accept the scan request.");
    }
    if (lookup(0) != request.identity.context.network.genesis || lookup(request.range.last) != request.reviewed_block
        || !exact_wallet(call("getwalletinfo", "[]", true)) || stop.stop_requested())
        return fail("The daemon replied, but the same wallet and reviewed chain could not be confirmed.");
    reply.last_scanned = ReportedScanEnd(*scan.result,request.range);
    if (reply.last_scanned != request.range.last) return fail("The daemon did not confirm the full requested range. History may be partial.");
    reply.complete = true;
    return reply;
}

RescanService::~RescanService()
{
    Cancel();
    if (worker.joinable()) worker.join();
}
bool RescanService::Start(ScanRequest request, const ConnectionService& connection, Lease admission)
{
    if (std::this_thread::get_id() != owner || Busy() || !fetch || !Current(request, &connection)) return false;
    if ((request.action == ScanAction::Start || request.action == ScanAction::Acknowledge)
        && (!request.ticket || !admission)) return false;
    if ((request.action == ScanAction::Start || request.action == ScanAction::Review) && (!request.range.Valid()
        || !CanStartScan(*connection.Active()))) return false;
    if (request.action == ScanAction::Start && !request.reviewed_block) return false;
    try {
        running = request;
        retired = false;
        worker = std::jthread([this, request = std::move(request)](std::stop_token stop) {
            ScanReply reply;
            try {
                reply = fetch(request, stop);
            } catch (...) {
                reply.error = "The scan state could not be verified.";
                reply.dispatched = request.action == ScanAction::Start;
            }
            std::lock_guard guard(mutex);
            completed = std::move(reply);
        });
        lease = std::move(admission);
    } catch (...) {
        running.reset();
        return false;
    }
    return true;
}
void RescanService::Cancel()
{
    if (std::this_thread::get_id() != owner || !Busy()) return;
    retired = true;
    worker.request_stop();
}
std::optional<RescanService::Completion> RescanService::Poll(const ConnectionService* connection,
    const std::optional<RefreshIdentity>& identity)
{
    if (std::this_thread::get_id() != owner || !running) return {};
    if (!Current(*running, connection) || running->identity != identity) Cancel();
    std::optional<ScanReply> reply;
    {
        std::lock_guard guard(mutex);
        if (!completed) return {};
        reply = std::move(completed);
        completed.reset();
    }
    worker.join();
    auto request = std::move(*running);
    running.reset();
    if (retired) {
        lease.reset();
        return {};
    }
    return Completion{std::move(request), std::move(*reply), std::move(lease)};
}
}
