// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>
#include <policy/feerate.h>
#include <tinyformat.h>

#include <algorithm>
#include <cstdlib>
#include <limits>

CFeeRate::CFeeRate(const CAmount& nFeePaid, int32_t virtual_bytes)
{
    if (virtual_bytes > 0) {
        m_feerate = FeePerVSize(nFeePaid, virtual_bytes);
    } else {
        m_feerate = FeePerVSize();
    }
}

CAmount CFeeRate::GetFee(int32_t virtual_bytes) const
{
    Assume(virtual_bytes >= 0);
    if (m_feerate.IsEmpty()) { return CAmount(0);}
    if (virtual_bytes == 0) return 0;
    constexpr CAmount maximum{std::numeric_limits<CAmount>::max()};
    constexpr CAmount minimum{std::numeric_limits<CAmount>::min()};
    if (ByRatio{m_feerate} > ByRatio{FeePerVSize{maximum, virtual_bytes}}) return maximum;
    if (ByRatio{m_feerate} < ByRatio{FeePerVSize{minimum, virtual_bytes}}) return minimum;
    CAmount nFee = CAmount(m_feerate.EvaluateFeeUp(virtual_bytes));
    if (nFee == 0 && virtual_bytes != 0 && m_feerate.fee < 0) return CAmount(-1);
    return nFee;
}

CAmount CFeeRate::GetFeePerK() const
{
    if (m_feerate.IsEmpty()) return 0;
    constexpr CAmount maximum{std::numeric_limits<CAmount>::max()};
    constexpr CAmount minimum{std::numeric_limits<CAmount>::min()};
    if (ByRatio{m_feerate} > ByRatio{FeePerVSize{maximum, 1000}}) return maximum;
    if (ByRatio{m_feerate} < ByRatio{FeePerVSize{minimum, 1000}}) return minimum;
    return m_feerate.EvaluateFeeDown(1000);
}

CFeeRate ScaleDustFee(const CFeeRate& estimate, int multiplier)
{
    Assume(multiplier > 0);
    const CAmount rate{std::max<CAmount>(0, estimate.GetFeePerK())};
    constexpr CAmount maximum{std::numeric_limits<CAmount>::max()};
    const CAmount whole{rate / 1000};
    if (whole > maximum / multiplier) return CFeeRate{maximum};
    const CAmount base{whole * multiplier};
    const CAmount remainder{(rate % 1000) * int64_t{multiplier} / 1000};
    return CFeeRate{remainder > maximum - base ? maximum : base + remainder};
}

std::string CFeeRate::ToString(FeeRateFormat fee_rate_format) const
{
    const CAmount feerate_per_kvb{GetFeePerK()};
    const auto format_feerate = [](const CAmount fee_rate, const CAmount divisor, const int decimals, const std::string& currency_unit, const std::string& size_unit) {
        Assert(divisor > 0);
        const char* sign{fee_rate < 0 ? "-" : ""};
        const CAmount quotient{std::abs(fee_rate / divisor)};
        const CAmount remainder{std::abs(fee_rate % divisor)};
        return strprintf("%s%d.%0*d %s/%s", sign, quotient, decimals, remainder, currency_unit, size_unit);
    };
    switch (fee_rate_format) {
    case FeeRateFormat::BTC_KVB: return format_feerate(feerate_per_kvb, COIN, /*decimals=*/8, CURRENCY_UNIT, "kvB");
    case FeeRateFormat::SAT_VB: return format_feerate(feerate_per_kvb, 1000, /*decimals=*/3, CURRENCY_ATOM, "vB");
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}
