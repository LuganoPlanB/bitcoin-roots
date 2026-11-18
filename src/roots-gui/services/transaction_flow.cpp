// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/funding.h"

namespace roots::gui {
const FlowItem* TransactionFlow::Find(FlowKey key) const
{
    const auto& rows = key.input ? inputs : outputs;
    return key.index < rows.size() ? &rows[key.index] : nullptr;
}
std::optional<TransactionFlow> DescribeFunding(const FundingPreview& preview, protocol::DisplayNetwork network)
{
    if (!preview.psbt || !preview.psbt->transaction) return {};
    const auto& tx = *preview.psbt->transaction;
    if (tx.inputs.empty() || tx.inputs.size() != preview.inputs.size() || tx.inputs.size() > MAX_FUNDING_INPUTS
        || tx.outputs.size() <= size_t(preview.change.has_value()) || tx.outputs.size() > MAX_FUNDING_OUTPUTS + size_t(preview.change.has_value())
        || preview.fee.sats() < 0) return {};
    const auto zero = Money::Satoshis(0);
    TransactionFlow flow{{},{},zero,zero,zero,preview.fee};
    for (size_t i=0; i<tx.inputs.size(); ++i) {
        const auto& proof=preview.inputs[i];
        const auto total=flow.total_inputs.Add(proof.value);
        if (!total || proof.value.sats() <= 0 || tx.inputs[i].outpoint != proof.point
            || tx.inputs[i].previous_value != proof.value) return {};
        flow.inputs.push_back({FlowKind::Input,proof.point,proof.point.txid.hex()+":"+std::to_string(proof.point.index),proof.value});
        flow.total_inputs=*total;
    }
    const size_t count = tx.outputs.size() - size_t(preview.change.has_value());
    for (size_t i=0; i<tx.outputs.size(); ++i) {
        const auto& output=tx.outputs[i];
        const auto address=protocol::OutputAddress(output.script,network);
        if (!address || output.value.sats() <= 0) return {};
        const bool change=i==count;
        if (change) {
            if (preview.change->address != *address || preview.change->script != output.script) return {};
            flow.total_change=output.value;
        } else {
            const auto next=flow.total_recipients.Add(output.value);
            if (!next) return {};
            flow.total_recipients=*next;
        }
        flow.outputs.push_back({change ? FlowKind::Change : FlowKind::Recipient,{},*address,output.value});
    }
    const auto outputs=flow.total_recipients.Add(flow.total_change);
    if (!outputs || outputs->Add(flow.fee) != flow.total_inputs || flow.total_inputs != preview.total_inputs) return {};
    flow.outputs.push_back({FlowKind::Fee,{},"",flow.fee});
    return flow;
}
}
