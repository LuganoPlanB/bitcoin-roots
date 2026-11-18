// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace roots::gui {
struct ReceiveTarget {
    std::string address, script, descriptor, path, chain;
    // Index is relative to the descriptor. A fixed inferred descriptor has
    // no child index, even when the wallet reports an absolute key path.
    std::optional<uint32_t> index;
    int64_t observed_ms{};
    bool operator==(const ReceiveTarget&) const = default;
};
}
