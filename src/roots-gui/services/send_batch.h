// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "services/send_recipients.h"
#include <mutex>
#include <thread>

namespace roots::gui {
inline constexpr size_t MAX_BATCH_CSV = 1024 * 1024;
inline constexpr size_t MAX_BATCH_CELL = 4096;
struct BatchIssue {
    std::string address, amount, label, unit;
    bool reference_dust{};
    bool Invalid() const { return !address.empty() || !amount.empty() || !label.empty() || !unit.empty(); }
};
struct BatchValidation {
    std::optional<SendRecipients> ready;
    std::optional<Money> fixed;
    std::vector<BatchIssue> rows;
    size_t errors{};
    bool maximum{};
    std::string error;
};
// A staged row uses pay_to as its raw address, and amount as its raw amount or
// "Max". Applied rows keep the existing independent maximum flag.
SendRecipients StageRecipients(const SendRecipients&);
BatchValidation ValidateBatch(const SendRecipients&,protocol::DisplayNetwork,std::optional<Money> dust={});
bool SamePayments(const SendRecipients&,const SendRecipients&);
struct BatchCsv {
    std::optional<SendRecipients> staged;
    std::string error;
};
BatchCsv ReadBatchCsv(std::string_view,uint64_t next_id,std::stop_token={});
std::optional<std::string> WriteBatchCsv(const SendRecipients&);
void WipeRecipients(SendRecipients&);
class BatchFileRead {
    const std::thread::id owner{std::this_thread::get_id()};
    std::mutex mutex;
    std::optional<BatchCsv> result;
    std::jthread worker;
public:
    BatchFileRead(std::string path,uint64_t next_id);
    ~BatchFileRead();
    void Cancel();
    std::optional<BatchCsv> Take();
};
}
