// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/send_recipients.h"
#include "adapters/rpc/json.h"

#include <algorithm>
#include <charconv>
#include <set>
#include <utility>

namespace roots::gui {
namespace {
constexpr int64_t MAX_MONEY = 2100000000000000;
constexpr int64_t REFERENCE_DUST_RATE = 3000;
char LowerAscii(char c) { return c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c; }
bool Digit(char c) { return c >= '0' && c <= '9'; }
bool PublicText(std::string_view text, size_t max)
{
    return text.size() <= max && rpc::ValidUtf8(text)
        && std::ranges::none_of(text, [](unsigned char c) { return c < 32 || c == 127; });
}
bool PaymentScheme(std::string_view text)
{
    constexpr std::string_view scheme{"bitcoin:"};
    return text.size() >= scheme.size()
        && std::ranges::equal(text.substr(0, scheme.size()), scheme, {}, LowerAscii);
}
std::optional<std::string> CanonicalAddress(std::string_view text, protocol::DisplayNetwork network)
{
    if (text.size() > 128) return {};
    std::string address(text);
    if (address.starts_with("BC1") || address.starts_with("TB1") || address.starts_with("BCRT1")) {
        if (std::ranges::any_of(address, [](char c) { return c >= 'a' && c <= 'z'; })) return {};
        std::ranges::transform(address, address.begin(), LowerAscii);
    }
    if (!protocol::AddressScript(address, network)) return {};
    return address;
}
int HexDigit(char c)
{
    if (Digit(c)) return c - '0';
    c = LowerAscii(c);
    return c >= 'a' && c <= 'f' ? 10 + c - 'a' : -1;
}
std::optional<std::string> DecodeQuery(std::string_view text)
{
    std::string result;
    result.reserve(text.size());
    for (size_t n = 0; n < text.size(); ++n) {
        const char c = text[n];
        if (c == '%') {
            if (text.size() - n < 3) return {};
            const int high = HexDigit(text[n + 1]), low = HexDigit(text[n + 2]);
            if (high < 0 || low < 0) return {};
            result += char(high * 16 + low);
            n += 2;
        } else {
            const char lower = LowerAscii(c);
            constexpr std::string_view punctuation{"-._~!$'()*+,;:@/?"};
            if (!Digit(c) && !(lower >= 'a' && lower <= 'z') && punctuation.find(c) == std::string_view::npos) return {};
            result += c;
        }
    }
    if (!PublicText(result, MAX_PAYMENT_TEXT)) return {};
    return result;
}
bool Advance(DraftRevision& revision)
{
    if (revision.value == UINT64_MAX) return false;
    ++revision.value;
    return true;
}
Money DustThreshold(std::string_view script, Money rate)
{
    // For public address scripts, CompactSize is one byte. The Core/Roots
    // policy size includes the conventional discounted witness spend (67 vB)
    // or the conventional legacy spend (148 vB).
    const auto version = script.substr(0, 2);
    const bool witness = version == "00" || (version >= "51" && version <= "60");
    const int64_t size = 9 + static_cast<int64_t>(script.size() / 2) + (witness ? 67 : 148);
    const int64_t sats = rate.sats() * size / 1000;
    return Money::Satoshis(sats == 0 && rate.sats() > 0 ? 1 : sats);
}
}

std::optional<Money> ParseRecipientAmount(std::string_view text, RecipientUnit unit)
{
    if (text.empty() || text.size() > MAX_AMOUNT_TEXT || !Digit(text.front())) return {};
    if (unit == RecipientUnit::BTC) {
        const auto amount = Money::ParseBtc(text, text.find(',') == std::string_view::npos ? '.' : ',');
        return amount && amount->sats() > 0 && amount->sats() <= MAX_MONEY ? amount : std::nullopt;
    }
    if (unit != RecipientUnit::Sats) return {};
    int64_t value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value <= 0 || value > MAX_MONEY) return {};
    return Money::Satoshis(value);
}

PaymentRequest ParsePaymentRequest(std::string_view text, protocol::DisplayNetwork network)
{
    const auto failure = [](std::string reason) { return PaymentRequest{.error = std::move(reason)}; };
    if (text.empty()) return failure("Enter a Bitcoin address or payment URI.");
    if (!PublicText(text, MAX_PAYMENT_TEXT)) return failure("Payment text is invalid or too long.");
    const bool uri = PaymentScheme(text);
    if (uri) text.remove_prefix(8);
    const auto separator = uri ? text.find('?') : std::string_view::npos;
    if (network == protocol::DisplayNetwork::Unknown) return failure("Wallet network is unknown.");
    auto address = CanonicalAddress(text.substr(0, separator), network);
    if (!address) return failure("The address is invalid for this wallet network.");
    PaymentRequest result{.address = std::move(*address)};
    if (separator == std::string_view::npos) return result;

    auto query = text.substr(separator + 1);
    std::set<std::string> keys;
    while (!query.empty()) {
        const auto ampersand = query.find('&');
        const auto field = query.substr(0, ampersand);
        const auto delimiter = field.find('=');
        const auto key = DecodeQuery(field.substr(0, delimiter));
        const auto value = DecodeQuery(delimiter == std::string_view::npos ? "" : field.substr(delimiter + 1));
        if (!key || key->empty() || !value) return failure("A payment URI parameter has invalid encoding.");
        if (!keys.insert(*key).second) return failure("The payment URI specifies a parameter more than once.");
        if (*key=="pj" || *key=="pjos" || *key=="req-pj")
            return failure("This payment request requires a PayjoinModule. Choose an independent plain Bitcoin payment request to pay without Payjoin.");
        if (key->starts_with("req-")) return failure("The payment URI requires an unsupported parameter.");
        if (*key == "label") {
            if (delimiter == std::string_view::npos || value->size() > MAX_RECIPIENT_LABEL)
                return failure("The payment URI label is invalid or too long.");
            result.label = *value;
        } else if (*key == "amount") {
            // BIP21 always uses a period and ungrouped BTC regardless of editor locale.
            if (value->empty() || !Digit(value->front()) || value->find(',') != std::string::npos)
                return failure("The payment URI amount must be an exact decimal BTC value.");
            const auto amount = Money::ParseBtc(*value);
            if (!amount || amount->sats() < 0 || amount->sats() > MAX_MONEY)
                return failure("The payment URI amount must be an exact decimal BTC value.");
            result.amount = amount;
        }
        if (ampersand == std::string_view::npos) break;
        query.remove_prefix(ampersand + 1);
    }
    return result;
}

const SendRecipient* SendRecipients::Find(uint64_t id) const
{
    const auto row = std::ranges::find(rows, id, &SendRecipient::id);
    return row == rows.end() ? nullptr : &*row;
}
SendRecipient* SendRecipients::FindMutable(uint64_t id)
{
    return const_cast<SendRecipient*>(std::as_const(*this).Find(id));
}
bool SendRecipients::Select(uint64_t id)
{
    if (!Find(id)) return false;
    selected = id;
    return true;
}
bool SendRecipients::Add(DraftRevision& revision)
{
    if (rows.size() >= MAX_SEND_RECIPIENTS || next == UINT64_MAX || revision.value == UINT64_MAX) return false;
    rows.push_back(SendRecipient{.id = next});
    selected = next++;
    return Advance(revision);
}
bool SendRecipients::Remove(uint64_t id, DraftRevision& revision)
{
    const auto row = std::ranges::find(rows, id, &SendRecipient::id);
    if (rows.size() <= 1 || row == rows.end() || !Advance(revision)) return false;
    const auto index = size_t(row - rows.begin());
    rows.erase(row);
    if (selected == id) selected = rows[std::min(index, rows.size() - 1)].id;
    return true;
}
bool SendRecipients::Edit(uint64_t id, RecipientField field, std::string_view text, DraftRevision& revision)
{
    auto* row = FindMutable(id);
    if (!row) return false;
    switch (field) {
    case RecipientField::Destination:
        if (!PublicText(text, MAX_PAYMENT_TEXT) || text == row->pay_to) return false;
        if (!Advance(revision)) return false;
        row->pay_to = text;
        return true;
    case RecipientField::Label:
        if (!PublicText(text, MAX_RECIPIENT_LABEL) || text == row->label) return false;
        if (!Advance(revision)) return false;
        row->label = text;
        return true;
    case RecipientField::Amount:
        if (!PublicText(text, MAX_AMOUNT_TEXT) || (text == row->amount && !row->maximum)) return false;
        if (!Advance(revision)) return false;
        row->amount = text;
        row->maximum = false;
        return true;
    }
    return false;
}
bool SendRecipients::SetUnit(uint64_t id, RecipientUnit unit, DraftRevision& revision)
{
    auto* row = FindMutable(id);
    if (!row || row->unit == unit || (unit != RecipientUnit::BTC && unit != RecipientUnit::Sats)
        || !Advance(revision)) return false;
    const auto old = ParseRecipientAmount(row->amount, row->unit);
    row->unit = unit;
    // Never reinterpret digits from the other unit, even if the old edit was invalid.
    row->amount = old ? (unit == RecipientUnit::BTC ? old->btc() : std::to_string(old->sats())) : "";
    return true;
}
bool SendRecipients::ToggleMaximum(uint64_t id, DraftRevision& revision)
{
    auto* row = FindMutable(id);
    if (!row || !Advance(revision)) return false;
    const bool enable = !row->maximum;
    if (enable) for (auto& candidate : rows) candidate.maximum = false;
    row->maximum = enable;
    return true;
}
bool SendRecipients::ApplyDestination(uint64_t id, std::string_view input,
    protocol::DisplayNetwork network, DraftRevision& revision)
{
    auto* row = FindMutable(id);
    if (!row) return false;
    auto payment = ParsePaymentRequest(input, network);
    if (!payment.error.empty() || !Advance(revision)) return false;
    if (!row->address.empty() && payment.address != row->address) {
        row->label.clear();
        row->amount.clear();
        row->maximum = false;
    }
    row->address = std::move(payment.address);
    row->pay_to = row->address;
    if (payment.label) row->label = std::move(*payment.label);
    if (payment.amount) {
        row->amount = row->unit == RecipientUnit::BTC ? payment.amount->btc() : std::to_string(payment.amount->sats());
        row->maximum = false;
    }
    return true;
}
bool SendRecipients::Dirty() const
{
    if (rows.size() != 1) return true;
    const auto& row = rows.front();
    return !row.pay_to.empty() || !row.address.empty() || !row.label.empty() || !row.amount.empty() || row.maximum;
}
bool SendRecipients::ValidPublicData(protocol::DisplayNetwork network) const
{
    if (rows.empty() || rows.size() > MAX_SEND_RECIPIENTS || !Find(selected)) return false;
    uint64_t previous{};
    bool maximum{};
    for (const auto& row : rows) {
        if (row.id <= previous || row.id >= next || !PublicText(row.pay_to, MAX_PAYMENT_TEXT)
            || !PublicText(row.label, MAX_RECIPIENT_LABEL) || !PublicText(row.amount, MAX_AMOUNT_TEXT)
            || (!row.address.empty() && CanonicalAddress(row.address, network) != row.address)
            || (row.unit != RecipientUnit::BTC && row.unit != RecipientUnit::Sats)
            || (maximum && row.maximum)) return false;
        previous = row.id;
        maximum |= row.maximum;
    }
    return true;
}
RecipientCheck CheckRecipient(const SendRecipient& row, protocol::DisplayNetwork network,
    std::optional<Money> dust_rate_per_kvb)
{
    RecipientCheck result;
    if (row.pay_to != row.address) {
        const auto payment = ParsePaymentRequest(row.pay_to, network);
        result.destination_error = payment.error.empty() ? "Apply this destination before continuing." : payment.error;
    } else if (row.address.empty()) {
        result.destination_error = "Enter a Bitcoin address or payment URI.";
    }
    const auto script = result.destination_error.empty() ? protocol::AddressScript(row.address, network) : std::nullopt;
    if (!script && result.destination_error.empty()) result.destination_error = "The address is invalid for this wallet network.";
    if (row.maximum) return result;
    result.amount = ParseRecipientAmount(row.amount, row.unit);
    if (!result.amount) {
        result.amount_error = row.unit == RecipientUnit::BTC
            ? "Enter a positive BTC amount with at most eight decimals."
            : "Enter a positive whole number of satoshis.";
    }
    if (!script || !result.amount) return result;
    if (dust_rate_per_kvb && dust_rate_per_kvb->sats() >= 0 && dust_rate_per_kvb->sats() <= MAX_MONEY) {
        result.dust_known = true;
        if (*result.amount < DustThreshold(*script, *dust_rate_per_kvb))
            result.amount_error = "Amount is below the current dust threshold for this address.";
    } else {
        result.below_reference_dust = *result.amount < DustThreshold(*script, Money::Satoshis(REFERENCE_DUST_RATE));
    }
    return result;
}
SendFundsCheck CheckSendFunds(const SendRecipients& recipients, std::optional<Money> funds,
    std::optional<Money> exact_fee)
{
    int64_t fixed{};
    bool complete = true;
    bool maximum = false;
    for (const auto& row : recipients.rows) {
        maximum |= row.maximum;
        if (row.maximum) continue;
        const auto amount = ParseRecipientAmount(row.amount, row.unit);
        if (!amount) { complete = false; continue; }
        if (amount->sats() > MAX_MONEY - fixed) return {.above_money_limit = true};
        fixed += amount->sats();
    }
    const auto fee = exact_fee && exact_fee->sats() >= 0 && exact_fee->sats() <= MAX_MONEY ? exact_fee->sats() : 0;
    const bool insufficient = funds && funds->sats() >= 0 && funds->sats() <= MAX_MONEY
        && (fixed + fee > funds->sats() || (maximum && fixed + fee == funds->sats()));
    return {complete ? std::optional(Money::Satoshis(fixed)) : std::nullopt, insufficient};
}
}
