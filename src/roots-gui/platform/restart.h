// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "app/desktop.h"
#include "services/capabilities.h"
#include <span>

namespace roots::gui::platform {
// A hand-off carries server form fields and an offline preference only. The
// same process environment preserves the GUI configuration and data roots.
struct RestartContext {
    std::optional<ServerCandidate> profile;
    bool offline{};
    bool operator==(const RestartContext&) const=default;
};
std::optional<std::string> EncodeRestartContext(const RestartContext&);
std::optional<RestartContext> DecodeRestartContext(std::string_view);
bool RestartSupported();
std::optional<RestartContext> ConsumeRestartContext(std::string_view argument);
// Success replaces this GUI process. A returned receipt is always a failure.
app::DesktopReceipt ReplaceGui(const RestartContext&,std::span<const std::string> retained_arguments={});
}
