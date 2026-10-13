// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <algorithm>
#include <barrier>
#include <thread>
#include <util/rbf.h>
#include <wallet/fees.h>
#include <interfaces/wallet.h>
#include <consensus/amount.h>
#include <key.h>
#include <key_io.h>
#include <policy/fees/block_policy_estimator.h>
#include <policy/policy.h>
#include <random.h>
#include <script/solver.h>
#include <script/descriptor.h>
#include <util/strencodings.h>
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/context.h>
#include <wallet/spend.h>
#include <wallet/test/util.h>
#include <wallet/test/wallet_test_fixture.h>

#include <boost/test/unit_test.hpp>

namespace wallet {
BOOST_FIXTURE_TEST_SUITE(spend_tests, WalletTestingSetup)

BOOST_AUTO_TEST_CASE(sweep_destination_requires_local_signature_satisfaction)
{
    auto wallet{std::make_shared<CWallet>(m_node.chain.get(), "", CreateMockableWalletDatabase())};
    const CKey local1{GenerateRandomKey()}, local2{GenerateRandomKey()}, foreign{GenerateRandomKey()};
    const std::string private1{EncodeSecret(local1)}, private2{EncodeSecret(local2)}, public_foreign{HexStr(foreign.GetPubKey())};
    const auto add_descriptor = [&](const std::string& text) {
        FlatSigningProvider keys, public_data;
        std::string error;
        auto descriptors{Parse(text, keys, error, /*require_checksum=*/false)};
        BOOST_REQUIRE_MESSAGE(descriptors.size() == 1, error);
        std::vector<CScript> scripts;
        BOOST_REQUIRE(descriptors[0]->Expand(0, keys, scripts, public_data));
        BOOST_REQUIRE_EQUAL(scripts.size(), 1);
        WalletDescriptor descriptor{std::move(descriptors[0]), 0, 0, 0, 0};
        LOCK(wallet->cs_wallet);
        BOOST_REQUIRE(wallet->AddWalletDescriptor(descriptor, keys, "", false));
        return scripts[0];
    };
    WITH_LOCK(wallet->cs_wallet, wallet->SetWalletFlag(WALLET_FLAG_DESCRIPTORS));
    const auto incomplete{add_descriptor("wsh(multi(2," + private1 + "," + public_foreign + "))")};
    const auto sufficient{add_descriptor("wsh(multi(2," + private1 + "," + private2 + "," + public_foreign + "))")};
    const auto script_path{add_descriptor("tr(" + public_foreign.substr(2) + ",pk(" + private1 + "))")};
    const auto absent_paths{add_descriptor("tr(" + public_foreign.substr(2) + ",pk(" + public_foreign.substr(2) + "))")};
    const auto musig_partial{add_descriptor("tr(musig(" + private1 + "," + public_foreign + "))")};
    const auto musig_complete{add_descriptor("tr(musig(" + private1 + "," + private2 + "))")};
    const auto check = [&] {
        LOCK(wallet->cs_wallet);
        BOOST_CHECK(wallet->IsMine(incomplete));
        BOOST_CHECK(!CanSweepTo(*wallet, incomplete));
        BOOST_CHECK(CanSweepTo(*wallet, sufficient));
        BOOST_CHECK(CanSweepTo(*wallet, script_path));
        BOOST_CHECK(!CanSweepTo(*wallet, absent_paths));
        BOOST_CHECK(!CanSweepTo(*wallet, musig_partial));
        BOOST_CHECK(CanSweepTo(*wallet, musig_complete));
    };
    check();
    BOOST_REQUIRE(wallet->EncryptWallet(SecureString{"test-passphrase"}));
    BOOST_REQUIRE(wallet->IsLocked());
    check();
    WalletContext context;
    auto interface{interfaces::MakeWallet(context, wallet)};
    const SecureString supplied{private2.begin(), private2.end()};
    CTxDestination destination;
    BOOST_REQUIRE(ExtractDestination(incomplete, destination));
    auto rejected{interface->sweepPrivateKey(supplied, destination, false, std::nullopt, std::make_shared<interfaces::SweepAuthorization>())};
    BOOST_CHECK(!rejected);
    BOOST_CHECK_EQUAL(util::ErrorString(rejected).original, "Destination is not controlled by this wallet");
    BOOST_REQUIRE(ExtractDestination(sufficient, destination));
    auto accepted{interface->sweepPrivateKey(supplied, destination, false, std::nullopt, std::make_shared<interfaces::SweepAuthorization>())};
    BOOST_CHECK(!accepted);
    BOOST_CHECK_EQUAL(util::ErrorString(accepted).original, "No spendable UTXOs found");
    BOOST_CHECK(wallet->IsLocked());
}

BOOST_AUTO_TEST_CASE(max_signed_input_size_uses_external_outpoint)
{
    const CKey key{GenerateRandomKey()};
    FillableSigningProvider provider;
    BOOST_REQUIRE(provider.AddKey(key));

    const CTxOut txout{COIN, GetScriptForDestination(PKHash{key.GetPubKey()})};
    const COutPoint outpoint{Txid{}, 0};
    CCoinControl coin_control;
    coin_control.Select(outpoint).SetTxOut(txout);

    const int low_r{CalculateMaximumSignedInputSize(txout, COutPoint{}, &provider, /*can_grind_r=*/true, &coin_control)};
    const int high_r{CalculateMaximumSignedInputSize(txout, outpoint, &provider, /*can_grind_r=*/true, &coin_control)};
    BOOST_CHECK_EQUAL(high_r, low_r + 1);
}

BOOST_AUTO_TEST_CASE(anti_fee_sniping_avoids_reserved_policy_locktime)
{
    FastRandomContext rng{/*fDeterministic=*/true};
    const int first_height{static_cast<int>(PARASITE_CAT21_LOCKTIME)};
    for (int height{first_height}; height < first_height + 100; ++height) {
        for (int sample = 0; sample < 1000; ++sample) {
            const uint32_t lock_time{GetAntiFeeSnipingLockTime(height, rng)};
            BOOST_CHECK_NE(lock_time, PARASITE_CAT21_LOCKTIME);
            BOOST_CHECK_LE(lock_time, height);
        }
    }
}

BOOST_AUTO_TEST_CASE(sweep_cancel_and_broadcast_are_mutually_exclusive)
{
    interfaces::SweepAuthorization authorization;
    std::barrier start{3};
    bool cancelled{false}, broadcast{false};
    std::thread cancellation{[&] { start.arrive_and_wait(); cancelled = authorization.Cancel(); }};
    std::thread submission{[&] { start.arrive_and_wait(); broadcast = authorization.BeginBroadcast(); }};
    start.arrive_and_wait();
    cancellation.join();
    submission.join();
    BOOST_CHECK(cancelled != broadcast);
    BOOST_CHECK_EQUAL(authorization.IsCancelled(), cancelled);
    BOOST_CHECK_EQUAL(authorization.BroadcastStarted(), broadcast);
}

BOOST_FIXTURE_TEST_CASE(sweep_transaction_failure_atomicity, TestChain100Setup)
{
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    auto wallet = CreateSyncedWallet(*m_node.chain, WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain()), coinbaseKey);
    const CKey key{GenerateRandomKey()};
    const CScript script{GetScriptForDestination(PKHash{key.GetPubKey()})};
    std::map<COutPoint, Coin> coins;
    coins.emplace(COutPoint{Txid{}, 1}, Coin{CTxOut{COIN, script}, 1, false});
    int broadcasts{0};
    interfaces::SweepAuthorization signing_authorization;

    FlatSigningProvider missing_key;
    auto signing_failure{CreateSweepTransaction(*wallet, missing_key, coins, script, [&](const CTransactionRef&, std::string&) { ++broadcasts; return true; }, signing_authorization)};
    BOOST_CHECK(!signing_failure);
    BOOST_CHECK_EQUAL(broadcasts, 0);

    FlatSigningProvider provider;
    provider.keys.emplace(key.GetPubKey().GetID(), key);
    provider.pubkeys.emplace(key.GetPubKey().GetID(), key.GetPubKey());
    interfaces::SweepAuthorization success_authorization;
    auto success{CreateSweepTransaction(*wallet, provider, coins, script, [&](const CTransactionRef&, std::string&) { ++broadcasts; return true; }, success_authorization)};
    BOOST_REQUIRE(success);
    BOOST_CHECK_GE(success->fee, GetMinimumFee(*wallet, GetVirtualTransactionSize(*success->tx), CCoinControl{}, nullptr));
    BOOST_CHECK_EQUAL(broadcasts, 1);

    std::string rejection;
    interfaces::SweepAuthorization failure_authorization;
    auto broadcast_failure{CreateSweepTransaction(*wallet, provider, coins, script, [&](const CTransactionRef&, std::string& error) {
        ++broadcasts;
        error = "txn-mempool-conflict";
        return false;
    }, failure_authorization)};
    BOOST_CHECK(!broadcast_failure);
    BOOST_CHECK(util::ErrorString(broadcast_failure).original.find("txn-mempool-conflict") != std::string::npos);
    BOOST_CHECK_EQUAL(broadcasts, 2);

    interfaces::SweepAuthorization cancelled_authorization;
    BOOST_REQUIRE(cancelled_authorization.Cancel());
    auto cancelled{CreateSweepTransaction(*wallet, provider, coins, script, [&](const CTransactionRef&, std::string&) {
        ++broadcasts;
        return true;
    }, cancelled_authorization)};
    BOOST_CHECK(!cancelled);
    BOOST_CHECK_EQUAL(util::ErrorString(cancelled).original, "Sweep cancelled");
    BOOST_CHECK_EQUAL(broadcasts, 2);

    interfaces::SweepAuthorization committed_authorization;
    BOOST_REQUIRE(committed_authorization.BeginBroadcast());
    BOOST_CHECK(!committed_authorization.Cancel());
    BOOST_CHECK(committed_authorization.BroadcastStarted());
}

BOOST_FIXTURE_TEST_CASE(sweep_transaction_uses_one_fee_snapshot, TestChain100Setup)
{
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    auto wallet = CreateSyncedWallet(*m_node.chain, WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain()), coinbaseKey);
    const CKey key{GenerateRandomKey()};
    const CScript script{GetScriptForDestination(PKHash{key.GetPubKey()})};
    const std::map<COutPoint, Coin> coins{{COutPoint{Txid{}, 1}, Coin{CTxOut{COIN, script}, 1, false}}};
    const CFeeRate initial_rate{1000};
    const CFeeRate changed_rate{10000};
    WITH_LOCK(wallet->cs_wallet, wallet->m_fallback_fee = initial_rate);

    // Change the wallet setting when signing the fee-adjusted transaction,
    // after the operation has selected its fee rate. This deterministically
    // models a a fee-settings change without timing-sensitive threads.
    class FeeChangingProvider final : public SigningProvider
    {
        CWallet& m_wallet;
        FlatSigningProvider m_provider;
        CFeeRate m_changed_rate;

    public:
        mutable int key_requests{0};
        FeeChangingProvider(CWallet& wallet, const CKey& key, CFeeRate changed_rate)
            : m_wallet{wallet}, m_changed_rate{changed_rate}
        {
            m_provider.keys.emplace(key.GetPubKey().GetID(), key);
            m_provider.pubkeys.emplace(key.GetPubKey().GetID(), key.GetPubKey());
        }
        bool GetKey(const CKeyID& id, CKey& key_out) const override
        {
            AssertLockNotHeld(m_wallet.cs_wallet);
            if (++key_requests == 2) {
                WITH_LOCK(m_wallet.cs_wallet, m_wallet.m_fallback_fee = m_changed_rate);
            }
            return m_provider.GetKey(id, key_out);
        }
        bool GetPubKey(const CKeyID& id, CPubKey& pubkey) const override
        {
            return m_provider.GetPubKey(id, pubkey);
        }
    } provider{*wallet, key, changed_rate};

    interfaces::SweepAuthorization authorization;
    auto result{CreateSweepTransaction(*wallet, provider, coins, script, [&](const CTransactionRef&, std::string&) {
        AssertLockNotHeld(wallet->cs_wallet);
        return true;
    }, authorization)};
    BOOST_REQUIRE(result);
    BOOST_CHECK_GE(provider.key_requests, 2);
    BOOST_CHECK(WITH_LOCK(wallet->cs_wallet, return wallet->m_fallback_fee == changed_rate));
    const auto vsize{GetVirtualTransactionSize(*result->tx)};
    BOOST_CHECK_GE(result->fee, initial_rate.GetFee(vsize));
    BOOST_CHECK_LT(result->fee, changed_rate.GetFee(vsize));
}

BOOST_FIXTURE_TEST_CASE(SubtractFee, TestChain100Setup)
{
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    auto wallet = CreateSyncedWallet(*m_node.chain, WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain()), coinbaseKey);

    // Check that a subtract-from-recipient transaction slightly less than the
    // coinbase input amount does not create a change output (because it would
    // be uneconomical to add and spend the output), and make sure it pays the
    // leftover input amount which would have been change to the recipient
    // instead of the miner.
    auto check_tx = [&wallet](CAmount leftover_input_amount) {
        CRecipient recipient{PubKeyDestination({}), 50 * COIN - leftover_input_amount, /*subtract_fee=*/true};
        CCoinControl coin_control;
        coin_control.m_feerate.emplace(10000);
        coin_control.fOverrideFeeRate = true;
        // We need to use a change type with high cost of change so that the leftover amount will be dropped to fee instead of added as a change output
        coin_control.m_change_type = OutputType::LEGACY;
        auto res = CreateTransaction(*wallet, {recipient}, /*change_pos=*/std::nullopt, coin_control);
        BOOST_CHECK(res);
        const auto& txr = *res;
        BOOST_CHECK_EQUAL(txr.tx->vout.size(), 1);
        BOOST_CHECK_EQUAL(txr.tx->vout[0].nValue, recipient.nAmount + leftover_input_amount - txr.fee);
        BOOST_CHECK_GT(txr.fee, 0);
        return txr.fee;
    };

    // Send full input amount to recipient, check that only nonzero fee is
    // subtracted (to_reduce == fee).
    const CAmount fee{check_tx(0)};

    // Send slightly less than full input amount to recipient, check leftover
    // input amount is paid to recipient not the miner (to_reduce == fee - 123)
    BOOST_CHECK_EQUAL(fee, check_tx(123));

    // Send full input minus fee amount to recipient, check leftover input
    // amount is paid to recipient not the miner (to_reduce == 0)
    BOOST_CHECK_EQUAL(fee, check_tx(fee));

    // Send full input minus more than the fee amount to recipient, check
    // leftover input amount is paid to recipient not the miner (to_reduce ==
    // -123). This overpays the recipient instead of overpaying the miner more
    // than double the necessary fee.
    BOOST_CHECK_EQUAL(fee, check_tx(fee + 123));
}

BOOST_FIXTURE_TEST_CASE(wallet_duplicated_preset_inputs_test, TestChain100Setup)
{
    // Verify that the wallet's Coin Selection process does not include pre-selected inputs twice in a transaction.

    // Add 4 spendable UTXO, 50 BTC each, to the wallet (total balance 200 BTC)
    for (int i = 0; i < 4; i++) CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    auto wallet = CreateSyncedWallet(*m_node.chain, WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain()), coinbaseKey);

    LOCK(wallet->cs_wallet);
    auto available_coins = AvailableCoins(*wallet);
    std::vector<COutput> coins = available_coins.All();
    // Preselect the first 3 UTXO (150 BTC total)
    std::set<COutPoint> preset_inputs = {coins[0].outpoint, coins[1].outpoint, coins[2].outpoint};

    // Try to create a tx that spends more than what preset inputs + wallet selected inputs are covering for.
    // The wallet can cover up to 200 BTC, and the tx target is 299 BTC.
    std::vector<CRecipient> recipients{{*Assert(wallet->GetNewDestination(OutputType::BECH32, "dummy")),
                                           /*nAmount=*/299 * COIN, /*fSubtractFeeFromAmount=*/true}};
    CCoinControl coin_control;
    coin_control.m_allow_other_inputs = true;
    for (const auto& outpoint : preset_inputs) {
        coin_control.Select(outpoint);
    }

    // Attempt to send 299 BTC from a wallet that only has 200 BTC. The wallet should exclude
    // the preset inputs from the pool of available coins, realize that there is not enough
    // money to fund the 299 BTC payment, and fail with "Insufficient funds".
    //
    // Even with SFFO, the wallet can only afford to send 200 BTC.
    // If the wallet does not properly exclude preset inputs from the pool of available coins
    // prior to coin selection, it may create a transaction that does not fund the full payment
    // amount or, through SFFO, incorrectly reduce the recipient's amount by the difference
    // between the original target and the wrongly counted inputs (in this case 99 BTC)
    // so that the recipient's amount is no longer equal to the user's selected target of 299 BTC.

    // First case, use 'subtract_fee_from_outputs=true'
    BOOST_CHECK(!CreateTransaction(*wallet, recipients, /*change_pos=*/std::nullopt, coin_control));

    // Second case, don't use 'subtract_fee_from_outputs'.
    recipients[0].fSubtractFeeFromAmount = false;
    BOOST_CHECK(!CreateTransaction(*wallet, recipients, /*change_pos=*/std::nullopt, coin_control));
}

BOOST_FIXTURE_TEST_CASE(manual_coin_control_honors_selection_and_rbf, TestChain100Setup)
{
    CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    auto wallet{CreateSyncedWallet(*m_node.chain, WITH_LOCK(Assert(m_node.chainman)->GetMutex(), return m_node.chainman->ActiveChain()), coinbaseKey)};
    const auto coins{WITH_LOCK(wallet->cs_wallet, return AvailableCoins(*wallet).All())};
    BOOST_REQUIRE(!coins.empty());
    const COutput& selected_coin{coins.front()};
    const CTxDestination destination{PKHash{GenerateRandomKey().GetPubKey()}};
    const CRecipient recipient{destination, COIN, /*subtract_fee=*/false};

    CCoinControl coin_control;
    coin_control.m_allow_other_inputs = false;
    coin_control.m_signal_bip125_rbf = false;
    coin_control.Select(selected_coin.outpoint);

    const auto no_rbf{CreateTransaction(*wallet, {recipient}, /*change_pos=*/std::nullopt, coin_control, /*sign=*/false)};
    BOOST_REQUIRE(no_rbf);
    BOOST_REQUIRE_EQUAL(no_rbf->tx->vin.size(), 1U);
    BOOST_CHECK(no_rbf->tx->vin.front().prevout == selected_coin.outpoint);
    BOOST_CHECK(no_rbf->tx->vin.front().nSequence == CTxIn::MAX_SEQUENCE_NONFINAL);
    CAmount outputs{0};
    for (const auto& output : no_rbf->tx->vout) outputs += output.nValue;
    BOOST_CHECK_EQUAL(selected_coin.txout.nValue, outputs + no_rbf->fee);

    coin_control.m_signal_bip125_rbf = true;
    const auto rbf{CreateTransaction(*wallet, {recipient}, /*change_pos=*/std::nullopt, coin_control, /*sign=*/false)};
    BOOST_REQUIRE(rbf);
    BOOST_REQUIRE_EQUAL(rbf->tx->vin.size(), 1U);
    BOOST_CHECK(rbf->tx->vin.front().prevout == selected_coin.outpoint);
    BOOST_CHECK_EQUAL(rbf->tx->vin.front().nSequence, MAX_BIP125_RBF_SEQUENCE);

    WITH_LOCK(wallet->cs_wallet, wallet->LockCoin(selected_coin.outpoint, /*persist=*/false));
    const auto available_after_lock{WITH_LOCK(wallet->cs_wallet, return AvailableCoins(*wallet).All())};
    BOOST_CHECK(std::none_of(available_after_lock.cbegin(), available_after_lock.cend(),
                             [&selected_coin](const COutput& coin) { return coin.outpoint == selected_coin.outpoint; }));
    BOOST_CHECK(WITH_LOCK(wallet->cs_wallet, return wallet->IsLockedCoin(selected_coin.outpoint)));
}

BOOST_AUTO_TEST_SUITE_END()
} // namespace wallet
