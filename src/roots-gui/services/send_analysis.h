// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <map>
#include <optional>
#include <string>

namespace roots::gui {
struct FundingPreview;
class HistoryLedger;
struct SendAnalysis {
    size_t inputs{}, origins{}, recipients{};
    std::map<std::string,size_t> input_scripts, output_scripts;
    std::optional<size_t> seen_recipients;
    bool complete_history{};
};
std::optional<SendAnalysis> AnalyzeSend(const FundingPreview&, const HistoryLedger* scoped_history);
}
