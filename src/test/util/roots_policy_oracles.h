// Copyright (c) 2026 The Bitcoin Roots developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TEST_UTIL_ROOTS_POLICY_ORACLES_H
#define BITCOIN_TEST_UTIL_ROOTS_POLICY_ORACLES_H

#include <cstdint>
#include <limits>
#include <string_view>

namespace roots_test {
// Convert a signed reference result without asking multiprecision to convert a
// negative magnitude to a signed native integer. Handle INT64_MIN separately:
// its positive magnitude is not representable as int64_t.
template <typename Wide>
int64_t BoundedSignedReference(const Wide& value)
{
    constexpr auto minimum{std::numeric_limits<int64_t>::min()};
    constexpr auto maximum{std::numeric_limits<int64_t>::max()};
    if (value <= minimum) return minimum;
    if (value >= maximum) return maximum;
    if (value < 0) return -static_cast<int64_t>(-value);
    return static_cast<int64_t>(value);
}

// Decimal accumulation with an explicit bound is independent of ToIntegral.
inline bool ValidInteger(std::string_view value, uint64_t maximum)
{
    bool negative{false};
    if (!value.empty() && (value.front() == '+' || value.front() == '-')) {
        negative = value.front() == '-';
        value.remove_prefix(1);
    }
    if (value.empty()) return false;
    uint64_t number{0};
    for (const char digit : value) {
        if (digit < '0' || digit > '9') return false;
        const auto numeric_digit{static_cast<uint64_t>(digit - '0')};
        if (number > maximum / 10 || (number == maximum / 10 && numeric_digit > maximum % 10)) return false;
        number = number * 10 + numeric_digit;
    }
    return !negative || number == 0;
}
} // namespace roots_test

#endif // BITCOIN_TEST_UTIL_ROOTS_POLICY_ORACLES_H
