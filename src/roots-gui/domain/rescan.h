// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <cstdint>
#include <limits>

namespace roots::gui {
struct ScanRange {
    int64_t first{}, last{};
    bool Valid() const {return first >= 0 && last >= first && last <= std::numeric_limits<int>::max();}
    bool operator==(const ScanRange&) const = default;
};
struct PendingRescan {
    uint64_t ticket{};
    ScanRange range;
    bool operator==(const PendingRescan&) const = default;
};
}
