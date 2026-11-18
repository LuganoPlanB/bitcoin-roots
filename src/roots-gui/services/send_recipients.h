// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "adapters/protocol/transaction.h"

namespace roots::gui {
inline constexpr size_t MAX_SEND_RECIPIENTS = 1024;
inline constexpr size_t MAX_PAYMENT_TEXT = 2048;
inline constexpr size_t MAX_RECIPIENT_LABEL = 255;
inline constexpr size_t MAX_AMOUNT_TEXT = 128;

enum class RecipientUnit { BTC, Sats };
enum class RecipientField { Destination, Label, Amount };

struct SendRecipient {
    uint64_t id{1};
    std::string pay_to, address, label, amount;
    RecipientUnit unit{RecipientUnit::BTC};
    bool maximum{};
};

struct SendRecipients {
    std::vector<SendRecipient> rows{SendRecipient{}};
    uint64_t selected{1}, next{2};

    const SendRecipient* Find(uint64_t) const;
    bool Select(uint64_t);
    bool Add(DraftRevision&);
    bool Remove(uint64_t, DraftRevision&);
    bool Edit(uint64_t, RecipientField, std::string_view, DraftRevision&);
    bool SetUnit(uint64_t, RecipientUnit, DraftRevision&);
    bool ToggleMaximum(uint64_t, DraftRevision&);
    bool ApplyDestination(uint64_t, std::string_view, protocol::DisplayNetwork, DraftRevision&);
    bool Dirty() const;
    bool ValidPublicData(protocol::DisplayNetwork) const;
private:
    SendRecipient* FindMutable(uint64_t);
};

struct PaymentRequest {
    std::string address, error;
    std::optional<std::string> label;
    std::optional<Money> amount;
};

// Parse only public payment data. Applying accepted data to a draft is a
// separate command. A URI never performs an external effect.
PaymentRequest ParsePaymentRequest(std::string_view, protocol::DisplayNetwork);
std::optional<Money> ParseRecipientAmount(std::string_view, RecipientUnit);

struct RecipientCheck {
    std::string destination_error, amount_error;
    std::optional<Money> amount;
    bool dust_known{}, below_reference_dust{};
};
struct SendFundsCheck {
    std::optional<Money> fixed_outputs;
    bool insufficient{}, above_money_limit{};
};
RecipientCheck CheckRecipient(const SendRecipient&, protocol::DisplayNetwork,
    std::optional<Money> dust_rate_per_kvb = {});
// Funds is a current eligible balance under the draft's coin policy. Coverage
// of fixed outputs alone never establishes fundability or a Max value.
SendFundsCheck CheckSendFunds(const SendRecipients&, std::optional<Money> funds,
    std::optional<Money> exact_fee = {});
}
