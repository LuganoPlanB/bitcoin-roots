// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/transaction.h"
#include <cstdlib>

using namespace roots::gui::protocol;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* bytes, size_t count)
{
    const std::string_view input(reinterpret_cast<const char*>(bytes),count);
    const auto parsed=ParseTransactionDocument(input,DisplayNetwork::Regtest);
    if (!parsed.document) return 0;
    const auto& document=*parsed.document;
    if (document.source!=input || document.wire.size()>MAX_TRANSACTION_WIRE) std::abort();
    if (!document.wire.empty()) {
        const auto wire=ParseTransactionDocument(document.wire,DisplayNetwork::Unknown);
        if (!wire.document || wire.document->wire!=document.wire
            || wire.document->psbt_version!=document.psbt_version) std::abort();
    }
    if (document.transaction) {
        const auto& tx=*document.transaction;
        if (tx.inputs.size()>MAX_TRANSACTION_IO || tx.outputs.size()>MAX_TRANSACTION_IO) std::abort();
        for (const auto& output:tx.outputs) {
            if (output.value.sats()<0) std::abort();
            if (const auto address=OutputAddress(output.script,DisplayNetwork::Regtest);
                address && !VerifyOutputAddress(*address,output.script,DisplayNetwork::Regtest)) std::abort();
        }
    }
    return 0;
}
