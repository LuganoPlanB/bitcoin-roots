// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/send_analysis.h"
#include "services/funding.h"

#include <algorithm>
#include <set>

namespace roots::gui {
namespace {
const char* ScriptType(std::string_view script)
{
    if (script.size()==50 && script.starts_with("76a914") && script.ends_with("88ac")) return "P2PKH";
    if (script.size()==46 && script.starts_with("a914") && script.ends_with("87")) return "P2SH";
    if (script.size()==44 && script.starts_with("0014")) return "P2WPKH";
    if (script.size()==68 && script.starts_with("0020")) return "P2WSH";
    if (script.size()==68 && script.starts_with("5120")) return "P2TR";
    return script.empty() ? "Unknown script" : "Other script";
}
}
std::optional<SendAnalysis> AnalyzeSend(const FundingPreview& preview,const HistoryLedger* history)
{
    if (!preview.flow || !preview.psbt || !preview.psbt->transaction) return {};
    const auto& tx=*preview.psbt->transaction;
    if (tx.inputs.size()!=preview.flow->inputs.size() || tx.outputs.size()+1!=preview.flow->outputs.size()) return {};
    SendAnalysis out;
    std::set<std::string> origins,scripts,seen;
    for (const auto& input:tx.inputs) {
        ++out.inputs;
        ++out.input_scripts[ScriptType(input.previous_script)];
        origins.insert(input.outpoint.txid.hex());
    }
    out.origins=origins.size();
    for (size_t i=0;i<tx.outputs.size();++i) {
        const auto& script=tx.outputs[i].script;
        ++out.output_scripts[ScriptType(script)];
        if (preview.flow->outputs[i].kind==FlowKind::Recipient) {
            ++out.recipients;
            if (!script.empty()) scripts.insert(script);
        }
    }
    if (history && history->Stable() && !history->Scanning()) {
        bool decoded=true;
        for (const auto& [id,entry]:history->Rows()) {
            if (entry.state!=HistoryState::Confirmed && entry.state!=HistoryState::Pending
                && entry.state!=HistoryState::Immature) continue;
            decoded&=entry.decoded_available;
            if (!entry.decoded_available) continue;
            for (const auto& row:entry.io) {
                if (row.kind!="Output") continue;
                const bool known=!row.script.empty() && row.script.size()%2==0
                    && std::ranges::all_of(row.script,[](char ch){return (ch>='0' && ch<='9') || (ch>='a' && ch<='f');});
                decoded&=known;
                if (known && scripts.contains(row.script)) seen.insert(row.script);
            }
        }
        out.complete_history=decoded && history->Complete() && !history->Limited();
        if (!seen.empty() || out.complete_history) out.seen_recipients=seen.size();
    }
    return out;
}
}
