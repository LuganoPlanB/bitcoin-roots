// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "app/commands.h"
#include "services/refresh.h"
namespace roots::gui::app {
std::optional<RefreshIdentity> ConnectionRefreshIdentity(const BackendCapabilities&, uint64_t session);
HostObservations ConnectionObservations(const ConnectionService&, const RefreshService&);
}
