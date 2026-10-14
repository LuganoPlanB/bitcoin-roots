// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/amount.h>
#include <policy/feerate.h>
#include <tinyformat.h>

#include <algorithm>
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
    if (m_feerate >> FeePerVSize{maximum, virtual_bytes}) return maximum;
    if (m_feerate << FeePerVSize{minimum, virtual_bytes}) return minimum;
    CAmount nFee = CAmount(m_feerate.EvaluateFeeUp(virtual_bytes));
    if (nFee == 0 && virtual_bytes != 0 && m_feerate.fee < 0) return CAmount(-1);
    return nFee;
}

CAmount CFeeRate::GetFeePerK() const
{
    if (m_feerate.IsEmpty()) return 0;
    constexpr CAmount maximum{std::numeric_limits<CAmount>::max()};
    constexpr CAmount minimum{std::numeric_limits<CAmount>::min()};
    if (m_feerate >> FeePerVSize{maximum, 1000}) return maximum;
    if (m_feerate << FeePerVSize{minimum, 1000}) return minimum;
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
    switch (fee_rate_format) {
    case FeeRateFormat::BTC_KVB: return strprintf("%d.%08d %s/kvB", feerate_per_kvb / COIN, feerate_per_kvb % COIN, CURRENCY_UNIT);
    case FeeRateFormat::SAT_VB: return strprintf("%d.%03d %s/vB", feerate_per_kvb / 1000, feerate_per_kvb % 1000, CURRENCY_ATOM);
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}
