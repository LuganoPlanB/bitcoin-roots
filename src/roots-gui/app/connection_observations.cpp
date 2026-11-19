// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/connection_observations.h"
namespace roots::gui::app {
std::optional<RefreshIdentity> ConnectionRefreshIdentity(const BackendCapabilities& caps, uint64_t session)
{
    if (!caps.CanApply() || !session) return {};
    const auto backend = BackendSessionId::Make("connection-" + std::to_string(session));
    const auto network = Hash256::Parse(caps.genesis);
    if (!backend || !network) return {};
    // Connection-level observer before wallet/document consumers bind their
    // own full RequestContext. Actual wallet name remains in the exact profile.
    RequestContext context{*backend, {*network}, *WalletId::Make(caps.candidate.wallet ? "selected-wallet" : "node"), *DocumentId::Make("connection"), {session}, {0}};
    return RefreshIdentity{std::move(context), caps.candidate, session};
}
HostObservations ConnectionObservations(const ConnectionService& connection, const RefreshService& refresh)
{
    HostObservations result; const auto view = refresh.View();
    if (!connection.Active()) { if (connection.Offline()) result.connection = {ServiceState::Offline, "Offline mode: network requests are disabled."}; return result; }
    const auto state = [&](size_t i) {
        return view.offline ? ServiceState::Offline : view.errors[i] != rpc::Error::None ? (view.observed[i] ? ServiceState::Stale : ServiceState::Error) : view.freshness[i] == Freshness::Live ? ServiceState::Ready : view.observed[i] ? ServiceState::Stale : view.busy ? ServiceState::Loading : ServiceState::Unavailable;
    };
    result.connection = {state(0), view.offline ? "Offline mode; cached observations only." : view.errors[0] != rpc::Error::None ? "Refresh failed; last known observations retained." : view.busy ? "Read-only refresh in progress." : "Read-only backend observation."};
    if (view.node) { result.backend_height = view.node->height; result.network = {state(0), view.node->initial_download ? "Initial block download" : "Network identity verified"}; }
    result.last_success = ObservationDate(view.observed[0]);
    result.error = view.errors[0] == rpc::Error::None ? "" : rpc::ErrorName(view.errors[0]);
    const char* names[]{"Chain", "Wallet", "History"};
    for (size_t i = 0; i < 3; ++i) {
        result.screen_observations[i] = {state(i), std::string(names[i]) + ": " + ObservationDate(view.observed[i]) + (view.errors[i] == rpc::Error::None ? "" : std::string(" / ") + rpc::ErrorName(view.errors[i]))};
    }
    result.proxy = {ServiceState::Ready, connection.Active()->candidate.connection.proxy == ProxyKind::Direct ? "Direct connection" : "Explicit proxy"};
    result.activity.state=view.offline?ServiceState::Offline:view.busy?ServiceState::Loading:
        view.errors[0]!=rpc::Error::None?state(0):ServiceState::Ready;
    return result;
}
}
