// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "platform/restart.h"
#include <string>
namespace roots::gui::platform {
std::string BuildInfo();
int Run(std::optional<RestartContext> resumed={});
}
