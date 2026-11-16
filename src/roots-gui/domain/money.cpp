// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "model.h"

#include <limits>
#include <numeric>

namespace roots::gui {
namespace {
constexpr uint64_t MAX = static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
bool Digit(char c) { return c >= '0' && c <= '9'; }

std::optional<int64_t> Decimal(std::string_view text, int scale, char decimal, bool json)
{
    if (text.empty() || text.size() > 128 || (decimal != '.' && decimal != ',')) return std::nullopt;
    const bool negative = text.front() == '-';
    size_t pos = negative ? 1 : 0;
    const size_t integer_start = pos;
    std::string digits;
    while (pos < text.size() && Digit(text[pos])) digits += text[pos++];
    if (pos == integer_start) return std::nullopt;
    if (json && pos - integer_start > 1 && text[integer_start] == '0') return std::nullopt;
    int fractional = 0;
    if (pos < text.size() && text[pos] == decimal) {
        const size_t start = ++pos;
        while (pos < text.size() && Digit(text[pos])) { digits += text[pos++]; ++fractional; }
        if (pos == start || (!json && fractional > scale)) return std::nullopt;
    }
    int exponent = 0;
    if (json && pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
        ++pos;
        bool minus = false;
        if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) minus = text[pos++] == '-';
        const size_t start = pos;
        while (pos < text.size() && Digit(text[pos])) {
            if (exponent > 1000) return std::nullopt;
            exponent = exponent * 10 + text[pos++] - '0';
        }
        if (pos == start) return std::nullopt;
        if (minus) exponent = -exponent;
    }
    if (pos != text.size()) return std::nullopt;
    const size_t first = digits.find_first_not_of('0');
    if (first == std::string::npos) return int64_t{0};
    digits.erase(0, first);
    int shift = scale + exponent - fractional;
    while (shift < 0 && !digits.empty() && digits.back() == '0') { digits.pop_back(); ++shift; }
    if (shift < 0 || shift > 19 || digits.size() + static_cast<size_t>(shift) > 19) return std::nullopt;
    digits.append(static_cast<size_t>(shift), '0');
    uint64_t magnitude{0};
    const uint64_t limit = MAX + (negative ? 1 : 0);
    for (char c : digits) {
        const unsigned digit = c - '0';
        if (magnitude > (limit - digit) / 10) return std::nullopt;
        magnitude = magnitude * 10 + digit;
    }
    if (!negative) return static_cast<int64_t>(magnitude);
    if (magnitude == MAX + 1) return std::numeric_limits<int64_t>::min();
    return -static_cast<int64_t>(magnitude);
}
} // namespace

std::optional<Money> Money::ParseBtc(std::string_view text, char decimal)
{
    auto value = Decimal(text, 8, decimal, false);
    return value ? std::optional{Satoshis(*value)} : std::nullopt;
}
std::optional<Money> Money::ParseJsonBtc(std::string_view text)
{
    auto value = Decimal(text, 8, '.', true);
    return value ? std::optional{Satoshis(*value)} : std::nullopt;
}
std::string Money::btc() const
{
    const uint64_t magnitude = m_sats < 0 ? static_cast<uint64_t>(-(m_sats + 1)) + 1 : m_sats;
    const std::string fraction = std::to_string(magnitude % 100000000);
    return (m_sats < 0 ? "-" : "") + std::to_string(magnitude / 100000000) + "." +
           std::string(8 - fraction.size(), '0') + fraction;
}
std::optional<Money> Money::Add(Money other) const
{
    if ((other.m_sats > 0 && m_sats > std::numeric_limits<int64_t>::max() - other.m_sats) ||
        (other.m_sats < 0 && m_sats < std::numeric_limits<int64_t>::min() - other.m_sats)) return std::nullopt;
    return Satoshis(m_sats + other.m_sats);
}
std::optional<Money> Money::Subtract(Money other) const
{
    if ((other.m_sats > 0 && m_sats < std::numeric_limits<int64_t>::min() + other.m_sats) ||
        (other.m_sats < 0 && m_sats > std::numeric_limits<int64_t>::max() + other.m_sats)) return std::nullopt;
    return Satoshis(m_sats - other.m_sats);
}
FeeRate::FeeRate(uint64_t numerator, uint32_t denominator)
{
    const auto divisor = std::gcd(numerator, static_cast<uint64_t>(denominator));
    m_numerator = numerator / divisor;
    m_denominator = denominator / divisor;
}
std::optional<FeeRate> FeeRate::Ratio(uint64_t satoshis, uint32_t virtual_bytes)
{
    if (!virtual_bytes || satoshis > MAX) return std::nullopt;
    return FeeRate(satoshis, virtual_bytes);
}
std::optional<FeeRate> FeeRate::Parse(std::string_view text)
{
    auto scaled = Decimal(text, 8, '.', false);
    if (!scaled || *scaled < 0) return std::nullopt;
    return FeeRate(static_cast<uint64_t>(*scaled), 100000000);
}
std::optional<Money> FeeRate::Fee(uint32_t bytes) const
{
    const uint64_t whole = m_numerator / m_denominator;
    // remainder and bytes are each at most UINT32_MAX; their product fits.
    const uint64_t remainder = (m_numerator % m_denominator) * bytes;
    const uint64_t tail = remainder / m_denominator + (remainder % m_denominator != 0);
    if (bytes && whole > (MAX - tail) / bytes) return std::nullopt;
    return Money::Satoshis(static_cast<int64_t>(whole * bytes + tail));
}
} // namespace roots::gui
