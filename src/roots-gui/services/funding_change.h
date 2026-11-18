// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "adapters/protocol/transaction.h"

namespace roots::gui {
// Public identity of a derived next internal address, without allocation authority.
struct FundingChange {
    std::string address, script, descriptor;
    uint32_t index{};
    bool operator==(const FundingChange&) const = default;
};
std::optional<FundingChange> DeriveFundingChange(std::string_view,uint32_t,protocol::DisplayNetwork);
}
