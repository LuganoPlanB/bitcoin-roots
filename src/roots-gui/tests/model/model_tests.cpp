// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "domain/model.h"

#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

using namespace roots::gui;
namespace {
int assertions{0};
template <typename T> concept SatoshiInput = requires(T value) { Money::Satoshis(value); };
void Check(bool value, const char* expression, int line)
{
    ++assertions;
    if (!value) throw std::runtime_error(std::string(expression) + " at line " + std::to_string(line));
}
#define CHECK(...) Check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __LINE__)
Hash256 Hash(char c = '1') { return *Hash256::Parse(std::string(64, c)); }
RequestContext Context(std::string_view document = "draft-a")
{
    return {*BackendSessionId::Make("session-a"), {Hash()}, *WalletId::Make("wallet-a"),
            *DocumentId::Make(document), {}, {}};
}
ApprovalIdentity Draft()
{
    return {{Hash()}, *WalletId::Make("wallet-a"), 2, 0, "wpkh(public-key)",
            {{{Hash('2'), 0}, 0xfffffffd, "", Money::Satoshis(10000), "0014aabb", 1}},
            {{Money::Satoshis(9000), "0014ccdd", false, std::nullopt}}};
}
void Prepare(TransactionDocument& document)
{
    CHECK(document.Edit(document.context(), Draft()));
    CHECK(document.Review(document.context()));
    CHECK(document.Approve(document.context()));
}
FinalizationEvidence Finalized()
{
    // Synthetic adapter output, not a cryptographic finalization vector.
    return {Hash('3'), Hash('4'), std::vector<uint8_t>(100, 0), 100, Money::Satoshis(1000)};
}
void MoneyVectors()
{
    CHECK(Money::ParseBtc("0.00000001")->sats() == 1);
    CHECK(Money::ParseBtc("-0.00000001")->sats() == -1);
    CHECK(Money::ParseBtc("21000000")->sats() == 2100000000000000LL);
    CHECK(Money::ParseBtc("92233720368.54775807")->sats() == std::numeric_limits<int64_t>::max());
    CHECK(Money::ParseBtc("-92233720368.54775808")->sats() == std::numeric_limits<int64_t>::min());
    CHECK(!Money::ParseBtc("92233720368.54775808"));
    CHECK(!Money::ParseBtc("-92233720368.54775809"));
    for (const auto text : {"", ".1", "1.", "1.000000001", "0.000000000", " 1", "1 ", "+1", "1e-8", "NaN", "1,000.00", "1'000"}) CHECK(!Money::ParseBtc(text));
    CHECK(Money::ParseBtc("1,00000001", ',')->sats() == 100000001);
    CHECK(!Money::ParseBtc("1,00000001"));
    CHECK(!Money::ParseBtc("1.00000001", ','));
    CHECK(!Money::ParseBtc("1", 'x'));
    CHECK(!Money::Satoshis(std::numeric_limits<int64_t>::max()).Add(Money::Satoshis(1)));
    CHECK(!Money::Satoshis(std::numeric_limits<int64_t>::min()).Add(Money::Satoshis(-1)));
    CHECK(!Money::Satoshis(0).Subtract(Money::Satoshis(std::numeric_limits<int64_t>::min())));
    CHECK(!Money::Satoshis(std::numeric_limits<int64_t>::min()).Subtract(Money::Satoshis(1)));
    CHECK(Money::Satoshis(-1).Subtract(Money::Satoshis(std::numeric_limits<int64_t>::min()))->sats() == std::numeric_limits<int64_t>::max());
}
void MoneyProperties()
{
    uint64_t random = 0x7a55ce10;
    for (int i = 0; i < 10000; ++i) {
        random = random * 6364136223846793005ULL + 1;
        const int64_t value = static_cast<int64_t>(random >> 1) * (i % 2 ? -1 : 1);
        const auto money = Money::Satoshis(value);
        CHECK(Money::ParseBtc(money.btc()) == money);
        CHECK(Money::ParseJsonBtc(money.btc()) == money);
        CHECK(money.Subtract(money) == Money::Satoshis(0));
    }
    CHECK(Money::ParseBtc(Money::Satoshis(std::numeric_limits<int64_t>::min()).btc())->sats() == std::numeric_limits<int64_t>::min());
}
void JsonTokens()
{
    CHECK(Money::ParseJsonBtc("1e-8")->sats() == 1);
    CHECK(Money::ParseJsonBtc("100e-10")->sats() == 1);
    CHECK(Money::ParseJsonBtc("0.0000000100")->sats() == 1);
    CHECK(Money::ParseJsonBtc("1.25E+1")->sats() == 1250000000);
    CHECK(Money::ParseJsonBtc("-9223372036854775808e-8")->sats() == std::numeric_limits<int64_t>::min());
    for (const auto text : {"01", "+1", "1e", "1e+", "1e-9", "1e100", "1,2", "\"1\"", "0x10", "1e--1", "1e1.2", "1.000000001"}) CHECK(!Money::ParseJsonBtc(text));
}
void FeeRates()
{
    CHECK(FeeRate::Parse("0.001")->Fee(1) == Money::Satoshis(1));
    CHECK(FeeRate::Parse("1.001")->Fee(1000) == Money::Satoshis(1001));
    CHECK(FeeRate::Parse("1.00000001")->Fee(100) == Money::Satoshis(101));
    CHECK(FeeRate::Ratio(1, 3)->Fee(4) == Money::Satoshis(2));
    CHECK(FeeRate::Ratio(2, 6) == FeeRate::Ratio(1, 3));
    CHECK(FeeRate::Parse("0")->Fee(std::numeric_limits<uint32_t>::max()) == Money::Satoshis(0));
    CHECK(!FeeRate::Ratio(1, 0));
    CHECK(!FeeRate::Parse("-0.1"));
    CHECK(!FeeRate::Parse("0.000000001"));
    CHECK(!FeeRate::Parse("1,1"));
    CHECK(!FeeRate::Ratio(std::numeric_limits<int64_t>::max(), 1)->Fee(2));
    CHECK(FeeRate::Ratio(std::numeric_limits<int64_t>::max(), 1)->Fee(1)->sats() == std::numeric_limits<int64_t>::max());
    for (uint32_t numerator = 0; numerator < 50; ++numerator) {
        for (uint32_t denominator = 1; denominator < 20; ++denominator) {
            const auto fee = FeeRate::Ratio(numerator, denominator)->Fee(101);
            CHECK(fee->sats() == (numerator * 101 + denominator - 1) / denominator);
        }
    }
    CHECK(FeeRate::Ratio(4294967294ULL, 4294967295U)->Fee(4294967295U)->sats() == 4294967294LL);
}
void Balances()
{
    std::vector<CoinBalance> coins{
        {{Hash(), 0}, Money::Satoshis(10), Confirmation::Confirmed, Availability::Spendable},
        {{Hash(), 1}, Money::Satoshis(20), Confirmation::Confirmed, Availability::Frozen},
        {{Hash(), 2}, Money::Satoshis(30), Confirmation::Unconfirmed, Availability::Reserved},
        {{Hash(), 3}, Money::Satoshis(40), Confirmation::Immature, Availability::Unavailable},
    };
    auto totals = CalculateBalances(coins);
    CHECK(totals->total == Money::Satoshis(100));
    CHECK(totals->confirmed == Money::Satoshis(30));
    CHECK(totals->unconfirmed == Money::Satoshis(30));
    CHECK(totals->immature == Money::Satoshis(40));
    CHECK(totals->frozen == Money::Satoshis(20));
    CHECK(totals->reserved == Money::Satoshis(30));
    CHECK(totals->spendable == Money::Satoshis(10));
    coins[0].amount.reset();
    totals = CalculateBalances(coins);
    CHECK(!totals->total && !totals->confirmed && !totals->spendable);
    CHECK(totals->frozen == Money::Satoshis(20));
    coins.push_back(coins[0]);
    CHECK(!CalculateBalances(coins));
    coins.pop_back();
    coins[0].amount = Money::Satoshis(-1);
    CHECK(!CalculateBalances(coins));
    coins[0].amount = Money::Satoshis(std::numeric_limits<int64_t>::max());
    CHECK(!CalculateBalances(coins));
    coins[0].amount.reset();
    coins[3].availability = Availability::Spendable;
    CHECK(!CalculateBalances(coins));
    CHECK(CalculateBalances({})->total == Money::Satoshis(0));
    CHECK(!CalculateBalances({
        {{Hash(), 0}, std::nullopt, Confirmation::Confirmed, Availability::Unavailable},
        {{Hash(), 1}, Money::Satoshis(2100000000000000LL), Confirmation::Confirmed, Availability::Frozen},
        {{Hash(), 2}, Money::Satoshis(1), Confirmation::Unconfirmed, Availability::Reserved},
    }));
}
void IdentityAndProvenance()
{
    static_assert(!std::is_convertible_v<WalletId, DocumentId>);
    static_assert(!std::is_constructible_v<Money, double>);
    static_assert(!SatoshiInput<double> && !SatoshiInput<float> && !SatoshiInput<uint64_t>);
    static_assert(SatoshiInput<int64_t> && SatoshiInput<int>);
    CHECK(!WalletId::Make(""));
    CHECK(!WalletId::Make("wallet\nsecret"));
    CHECK(!Hash256::Parse("00"));
    CHECK(!Hash256::Parse(std::string(64, 'g')));
    CHECK(Hash256::Parse(std::string(64, 'A')) == Hash256::Parse(std::string(64, 'a')));
    const auto context = Context();
    Provenance provenance{context, Freshness::Live, 123, 100};
    CHECK(provenance.IsLiveFor(context));
    provenance.freshness = Freshness::Cached;
    CHECK(!provenance.IsLiveFor(context));
    CHECK(!provenance.IsLiveFor(Context("other")));
    CHECK(Capability::Unavailable(CapabilityReason::WalletDisabled).reason() == CapabilityReason::WalletDisabled);
    CHECK(Capability::Available().available());
}
void StaleAndLock()
{
    TransactionDocument doc(Context()), other(Context("draft-b"));
    const auto before = doc.context();
    CHECK(doc.Edit(before, Draft()));
    CHECK(!doc.Accepts(before));
    CHECK(!doc.Accepts(other.context()));
    auto wrong = doc.context();
    wrong.backend = *BackendSessionId::Make("old-session"); CHECK(!doc.Accepts(wrong));
    wrong = doc.context(); wrong.network = {Hash('a')}; CHECK(!doc.Accepts(wrong));
    wrong = doc.context(); wrong.wallet = *WalletId::Make("other-wallet"); CHECK(!doc.Accepts(wrong));
    CHECK(doc.Review(doc.context())); CHECK(doc.Approve(doc.context()));
    const auto pending = doc.context();
    CHECK(doc.Lock()); CHECK(doc.visible_draft() == nullptr);
    CHECK(!doc.RecordSatisfaction(pending, {Satisfaction::VerifiedComplete}));
    CHECK(!doc.Approve(doc.context()));
    CHECK(doc.Unlock()); CHECK(!doc.Accepts(pending));
    CHECK(doc.state() == TransactionState::Editable);
    CHECK(other.state() == TransactionState::Editable && other.context().revision.value == 0);
    CHECK(doc.Reconnect(*BackendSessionId::Make("session-b")));
    CHECK(!doc.Accepts(pending));
    auto max = Context(); max.lock_epoch.value = std::numeric_limits<uint64_t>::max();
    TransactionDocument exhausted(max);
    CHECK(!exhausted.Lock()); CHECK(exhausted.locked()); CHECK(!exhausted.Unlock());
    max = Context(); max.revision.value = std::numeric_limits<uint64_t>::max();
    TransactionDocument revision_exhausted(max);
    CHECK(!revision_exhausted.Edit(max, Draft()));
}
void ApprovalCommitments()
{
    auto base = Draft();
    CHECK(base.CanApprove()); CHECK(base.Fee() == Money::Satoshis(1000));
    auto bad = base; bad.inputs.push_back(bad.inputs[0]); CHECK(!bad.ValidShape());
    bad = base; bad.outputs[0].amount = Money::Satoshis(-1); CHECK(!bad.ValidShape());
    bad = base; bad.outputs[0].amount = Money::Satoshis(10001); CHECK(!bad.CanApprove());
    bad = base; bad.inputs[0].prevout_amount.reset(); CHECK(bad.ValidShape() && !bad.CanApprove() && !bad.Fee());
    TransactionDocument unknown(Context());
    CHECK(unknown.Edit(unknown.context(), bad)); CHECK(unknown.Review(unknown.context()));
    CHECK(!unknown.Approve(unknown.context()));
    bad = base; bad.inputs[0].prevout_script.reset(); CHECK(!bad.CanApprove());
    bad = base; bad.inputs[0].sighash.reset(); CHECK(!bad.CanApprove());
    bad = base; bad.inputs[0].sighash = 0x80; CHECK(!bad.ValidShape());
    bad = base; bad.outputs[0].change = true; CHECK(!bad.CanApprove());
    bad.outputs[0].change_proof = ChangeProof{"descriptor", "m/0/1", "0014ccdd"}; CHECK(bad.CanApprove());
    bad.outputs[0].change_proof->derived_script = "0014ffff"; CHECK(!bad.CanApprove());
    // Every committed field change invalidates an existing approval and result.
    const std::vector<std::function<void(ApprovalIdentity&)>> edits{
        [](auto& d) { ++d.version; }, [](auto& d) { ++d.locktime; },
        [](auto& d) { d.wallet_policy += "changed"; }, [](auto& d) { --d.inputs[0].sequence; },
        [](auto& d) { d.inputs[0].outpoint.index++; }, [](auto& d) { d.inputs[0].prevout_amount = Money::Satoshis(11000); },
        [](auto& d) { d.inputs[0].prevout_script = "0014ffff"; }, [](auto& d) { d.inputs[0].sighash = 0x81; },
        [](auto& d) { d.inputs[0].script_sig = "00"; }, [](auto& d) { d.outputs[0].amount = Money::Satoshis(8000); },
        [](auto& d) { d.outputs[0].script = "0014ffff"; },
    };
    for (const auto& edit : edits) {
        TransactionDocument doc(Context()); Prepare(doc); const auto stale = doc.context();
        auto changed = base; edit(changed); CHECK(changed != base);
        CHECK(doc.Edit(stale, changed)); CHECK(doc.state() == TransactionState::Editable);
        CHECK(!doc.RecordSatisfaction(stale, {Satisfaction::VerifiedComplete}));
        CHECK(!doc.Approve(doc.context()));
    }
    auto ordered = base;
    ordered.inputs.push_back({{Hash('6'), 1}, 0xffffffff, "", Money::Satoshis(1000), "00140000", 1});
    ordered.outputs.push_back({Money::Satoshis(100), "0014eeee", true,
                              ChangeProof{"descriptor", "m/0/1", "0014eeee"}});
    CHECK(ordered.CanApprove());
    TransactionDocument order_doc(Context());
    CHECK(order_doc.Edit(order_doc.context(), ordered)); CHECK(order_doc.Review(order_doc.context())); CHECK(order_doc.Approve(order_doc.context()));
    const auto old_order = order_doc.context();
    std::swap(ordered.inputs[0], ordered.inputs[1]);
    std::swap(ordered.outputs[0], ordered.outputs[1]);
    CHECK(order_doc.Edit(old_order, ordered)); CHECK(!order_doc.Accepts(old_order));
    CHECK(order_doc.state() == TransactionState::Editable);
}
void Lifecycle()
{
    TransactionDocument doc(Context());
    CHECK(!doc.Approve(doc.context())); CHECK(!doc.BeginSubmission(doc.context()));
    Prepare(doc);
    CHECK(doc.RecordSatisfaction(doc.context(), {Satisfaction::Unknown}));
    CHECK(doc.state() == TransactionState::AwaitingSignatures);
    CHECK(!doc.RecordFinalization(doc.context(), Finalized()));
    CHECK(!doc.RecordSatisfaction(doc.context(), {}));
    CHECK(doc.RecordSatisfaction(doc.context(), {Satisfaction::VerifiedComplete}));
    auto wrong_fee = Finalized(); wrong_fee.fee = Money::Satoshis(999);
    CHECK(!doc.RecordFinalization(doc.context(), wrong_fee));
    CHECK(doc.RecordFinalization(doc.context(), Finalized()));
    CHECK(doc.visible_finalization()->virtual_bytes == 100);
    CHECK(doc.BeginSubmission(doc.context()));
    CHECK(!doc.Edit(doc.context(), Draft()));
    CHECK(doc.SubmissionTimedOut(doc.context()));
    CHECK(!doc.BeginSubmission(doc.context())); // timeout never triggers a resend
    CHECK(!doc.Observe(doc.context(), Hash('5'), TransactionState::InMempool));
    CHECK(doc.Observe(doc.context(), Hash('3'), TransactionState::InMempool));
    CHECK(doc.Observe(doc.context(), Hash('3'), TransactionState::Confirmed));
    CHECK(doc.Observe(doc.context(), Hash('3'), TransactionState::InMempool)); // reorg
    CHECK(doc.Observe(doc.context(), Hash('3'), TransactionState::Replaced));
    CHECK(!doc.Observe(doc.context(), Hash('3'), TransactionState::Confirmed));
    TransactionDocument multi(Context()); auto draft = Draft();
    draft.inputs.push_back({{Hash('6'), 1}, 0xfffffffd, "", Money::Satoshis(1000), "00140000", 1});
    CHECK(multi.Edit(multi.context(), draft)); CHECK(multi.Review(multi.context())); CHECK(multi.Approve(multi.context()));
    CHECK(multi.RecordSatisfaction(multi.context(), {Satisfaction::VerifiedComplete, Satisfaction::Unknown}));
    CHECK(multi.state() == TransactionState::PartiallySigned);
    CHECK(!multi.RecordFinalization(multi.context(), Finalized()));
    CHECK(multi.RecordSatisfaction(multi.context(), {Satisfaction::VerifiedComplete, Satisfaction::VerifiedComplete}));
    auto multi_final = Finalized(); multi_final.fee = Money::Satoshis(2000);
    CHECK(multi.RecordFinalization(multi.context(), multi_final));
    CHECK(multi.BeginSubmission(multi.context()));
    const auto in_flight = multi.context();
    CHECK(multi.Lock()); CHECK(multi.state() == TransactionState::SubmissionUncertain);
    CHECK(multi.visible_finalization() == nullptr);
    CHECK(multi.Unlock()); CHECK(!multi.Accepts(in_flight));
}
} // namespace
int main()
{
    const std::vector<std::pair<const char*, void(*)()>> tests{
        {"money_vectors", MoneyVectors}, {"money_properties", MoneyProperties}, {"json_tokens", JsonTokens},
        {"fee_rates", FeeRates}, {"balances", Balances}, {"identity_provenance", IdentityAndProvenance},
        {"stale_lock", StaleAndLock}, {"approval_commitments", ApprovalCommitments}, {"lifecycle", Lifecycle},
    };
    int failed{0};
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    }
    std::cout << "executed=" << tests.size() << " passed=" << tests.size() - failed
              << " failed=" << failed << " skipped=0 assertions=" << assertions << '\n';
    return failed || tests.empty() ? 1 : 0;
}
