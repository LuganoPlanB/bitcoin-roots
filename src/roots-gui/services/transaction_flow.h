// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "adapters/protocol/transaction.h"

namespace roots::gui {
struct FundingPreview;
enum class FlowKind { Input, Recipient, Change, Fee };
struct FlowItem {
    FlowKind kind;
    std::optional<Outpoint> outpoint;
    std::string identifier;
    Money amount;
};
struct FlowKey {
    bool input;
    size_t index;
    bool operator==(const FlowKey&) const = default;
};
struct TransactionFlow {
    std::vector<FlowItem> inputs, outputs; // payment and optional change in PSBT order, then fee
    Money total_inputs, total_recipients, total_change, fee;
    const FlowItem* Find(FlowKey) const;
};
std::optional<TransactionFlow> DescribeFunding(const FundingPreview&,protocol::DisplayNetwork);
}
