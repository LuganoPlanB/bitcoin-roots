// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/recovery.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
using protocol::DisplayNetwork;

namespace {
int checks{};
constexpr std::string_view FIRST = "1BitcoinEaterAddressDontSendf59kuE";
constexpr std::string_view SECOND = "1BoatSLRHtKNngkdXEeobR76b53LETtpyT";
constexpr auto MAIN = DisplayNetwork::Mainnet;

void Check(bool condition, const char* reason)
{
    if (!condition) throw std::runtime_error(reason);
    ++checks;
}

void UriVectors()
{
    // Cases follow the BIP 21 specification and examples. Replace its
    // intentionally invalid sample address with a public checksum-valid vector.
    // https://github.com/bitcoin/bips/blob/master/bip-0021.mediawiki
    const auto uri = "BiTcOiN:" + std::string(FIRST);
    const auto parse = [&](std::string_view query) { return ParsePaymentRequest(uri + std::string(query), MAIN); };
    auto request = parse("");
    Check(request.error.empty() && request.address == FIRST && !request.label && !request.amount, "bare BIP21");
    request = parse("?amount=20.3&label=Luke-Jr");
    Check(request.error.empty() && request.label == "Luke-Jr" && request.amount == Money::Satoshis(2030000000),
        "BIP21 exact amount and label");
    request = parse("?amount=50&message=Donation%20for%20project%20xyz&other=ignored");
    Check(request.error.empty() && !request.label && request.amount == Money::Satoshis(5000000000),
        "BIP21 message and optional fields");
    request = parse("?label=J%C3%B6rg+%26+Mia%3D%20team");
    Check(request.error.empty() && request.label == "Jörg+&+Mia= team", "UTF-8 percent decode with literal plus");
    Check(parse("?amount=0.00000001").amount == Money::Satoshis(1), "one satoshi URI");
    Check(parse("?amount=21000000.00000000").amount == Money::Satoshis(2100000000000000), "max money");
    Check(parse("?Amount=1").error.empty() && !parse("?Amount=1").amount, "case-sensitive query keys");

    for (const auto query : {"?amount=1&amount=2", "?amount=1&%61mount=2",
         "?label=a&label=b", "?unknown=1&unknown=2", "?req-unknown=1", "?%72eq-unknown=1",
         "?amount=1,1", "?amount=0.000000001", "?amount=21000000.00000001", "?amount=1e2",
         "?amount=-1", "?amount=+1", "?label=%00", "?label=%0A", "?label=%FF", "?label=bad%2",
         "?label=a b", "?label=a#other", "?amount", "?label"}) {
        request = parse(query);
        Check(!request.error.empty() && request.address.empty() && !request.amount && !request.label, query);
    }
    Check(!ParsePaymentRequest(uri, DisplayNetwork::Regtest).error.empty(), "exact network validation");
    Check(!ParsePaymentRequest(uri, DisplayNetwork::Unknown).error.empty(), "unknown network");
    Check(!ParsePaymentRequest(uri + std::string(MAX_PAYMENT_TEXT, 'x'), MAIN).error.empty(), "bounded URI");
    Check(!ParsePaymentRequest("bitcoin:175tWpb8K1S7NmH4Zx6rewF9WQrcZv245W", MAIN).error.empty(),
        "BIP21 example has an intentionally invalid checksum");
    const std::string witness = "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4";
    auto uppercase = witness;
    std::ranges::transform(uppercase, uppercase.begin(), [](char c) {return char(c >= 'a' && c <= 'z' ? c - 32 : c);});
    Check(ParsePaymentRequest(uppercase, MAIN).address == witness, "canonical uniform uppercase witness address");
    uppercase.back() = 'a';
    Check(!ParsePaymentRequest(uppercase, MAIN).error.empty(), "mixed case witness address");
}

void ExactEditorAndPolicy()
{
    Check(ParseRecipientAmount("0,00000001", RecipientUnit::BTC) == Money::Satoshis(1), "manual comma decimal");
    Check(ParseRecipientAmount("90071992", RecipientUnit::Sats) == Money::Satoshis(90071992), "integer sats");
    Check(ParseRecipientAmount("0.00000001", RecipientUnit::BTC) == Money::Satoshis(1), "manual point decimal");
    Check(ParseRecipientAmount("1,000", RecipientUnit::BTC) == Money::Satoshis(100000000), "single separator is decimal");
    for (const std::string_view malformed : {"1,000.01", "1.000,01", "1,000,000", "1 000", "1.", ",1", "1e-8"})
        Check(!ParseRecipientAmount(malformed, RecipientUnit::BTC), "grouping and malformed output input refused");
    Check(ParseRecipientAmount("21000000,00000000", RecipientUnit::BTC) == Money::Satoshis(2100000000000000), "maximum Bitcoin amount");
    Check(!ParseRecipientAmount("21000000.00000001", RecipientUnit::BTC), "one sat above maximum");
    Check(ParseRecipientAmount("2100000000000000", RecipientUnit::Sats) == Money::Satoshis(2100000000000000), "integer sat maximum");
    Check(!ParseRecipientAmount("2100000000000001", RecipientUnit::Sats), "integer sat above maximum");
    Check(!ParseRecipientAmount("1,5", RecipientUnit::Sats), "integer sat input has no comma");
    Check(!ParseRecipientAmount("0.000000010", RecipientUnit::BTC), "manual precision limit");
    Check(!ParseRecipientAmount("1.0", RecipientUnit::Sats), "no fractional sat editor");
    Check(!ParseRecipientAmount("0", RecipientUnit::Sats), "positive output");
    Check(!ParseRecipientAmount("18446744073709551615", RecipientUnit::Sats), "sats overflow");
    SendRecipients draft;
    DraftRevision revision{};
    Check(draft.ApplyDestination(1, "bitcoin:" + std::string(FIRST) + "?amount=0.00000545&label=Invoice",
        MAIN, revision), "apply URI as a single command");
    Check(revision.value == 1 && draft.Find(1)->label == "Invoice" && draft.Find(1)->amount == "0.00000545", "exact initial fields");
    Check(!draft.ApplyDestination(1, "bitcoin:" + std::string(SECOND) + "?amount=1&label=Changed&req-x=1",
        MAIN, revision), "failed URI does not alter draft");
    Check(revision.value == 1 && draft.Find(1)->address == FIRST && draft.Find(1)->label == "Invoice"
        && draft.Find(1)->amount == "0.00000545", "no partial URI application");
    auto validation = CheckRecipient(*draft.Find(1), MAIN);
    Check(!validation.dust_known && validation.below_reference_dust && validation.amount_error.empty(), "reference dust separate from live policy");
    validation = CheckRecipient(*draft.Find(1), MAIN, Money::Satoshis(3000));
    Check(validation.dust_known && !validation.amount_error.empty(), "below eligible policy dust");
    Check(draft.SetUnit(1, RecipientUnit::Sats, revision) && draft.Find(1)->amount == "545", "unit conversion preserves value");
    Check(draft.Edit(1, RecipientField::Amount, "546", revision), "replace amount");
    validation = CheckRecipient(*draft.Find(1), MAIN, Money::Satoshis(3000));
    Check(validation.amount_error.empty() && validation.amount == Money::Satoshis(546), "exact dust boundary");
    Check(CheckSendFunds(draft, std::nullopt).fixed_outputs == Money::Satoshis(546) &&
        !CheckSendFunds(draft, std::nullopt).insufficient, "unknown funds stay unknown");
    Check(CheckSendFunds(draft, Money::Satoshis(545)).insufficient, "insufficient eligible funds");
    Check(CheckSendFunds(draft, Money::Satoshis(600), Money::Satoshis(55)).insufficient, "exact fee participates");
    Check(draft.ToggleMaximum(1, revision) && draft.Add(revision), "add second recipient");
    const auto second = draft.selected;
    Check(second != 1 && draft.ToggleMaximum(second, revision) && !draft.Find(1)->maximum
        && draft.Find(second)->maximum, "one Max transfers explicitly");
    Check(CheckSendFunds(draft, Money::Satoshis(546)).insufficient, "Max cannot consume zero");
    Check(draft.Edit(second, RecipientField::Destination, "bc1bad", revision), "stage independent invalid destination");
    Check(!CheckRecipient(*draft.Find(second), MAIN).destination_error.empty()
        && CheckRecipient(*draft.Find(1), MAIN).destination_error.empty(), "independent recipient errors");
    Check(draft.Edit(1, RecipientField::Destination, SECOND, revision), "stage destination change");
    Check(!CheckRecipient(*draft.Find(1), MAIN).destination_error.empty(), "old destination unready after staging");
    Check(draft.ApplyDestination(1, SECOND, MAIN, revision), "apply changed destination");
    Check(draft.Find(1)->label.empty() && draft.Find(1)->amount.empty() && draft.Find(second)->maximum,
        "destination-specific values invalidated without changing other rows");
    Check(draft.Remove(1, revision) && draft.rows[0].id == second && draft.selected == second, "stable keys after removal");
    revision.value = UINT64_MAX;
    Check(!draft.Add(revision) && !draft.Edit(second, RecipientField::Amount, "100", revision)
        && !draft.ApplyDestination(second, FIRST, MAIN, revision) && draft.rows.size() == 1,
        "revision exhaustion is nonmutating");
}

void PublicRecovery()
{
    app::PublicSession session;
    const auto wallet = app::WalletBinding{*BackendSessionId::Make("public-test"), *WalletId::Make("recipient-test"),
        {*Hash256::Parse("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f")}, {}};
    session.documents.OpenWallet("Recipient test", wallet, true);
    auto& doc = *session.documents.Active();
    Check(doc.recipients.ApplyDestination(1, "bitcoin:" + std::string(FIRST) + "?label=Invoice&amount=0.25",
        MAIN, doc.draft_revision), "valid public destination");
    Check(doc.recipients.Add(doc.draft_revision) && doc.recipients.ToggleMaximum(doc.recipients.selected, doc.draft_revision),
        "public Max intent");
    Check(doc.Dirty(), "recipient edits engage existing dirty-close gate");
    const auto encoded = app::EncodePublicSession(session);
    const auto restored = encoded ? app::DecodePublicSession(*encoded) : std::nullopt;
    Check(restored && restored->documents.Documents().size() == 1, "valid public recovery");
    const auto* restored_doc = restored->documents.Find(doc.id);
    Check(restored_doc && restored_doc->observation == app::ObservationState::Offline
        && restored_doc->draft_revision == doc.draft_revision && restored_doc->recipients.selected == 2
        && restored_doc->recipients.Find(1)->label == "Invoice" && restored_doc->recipients.Find(2)->maximum,
        "recovery preserves only draft and local fields");
    doc.recipients.rows[0].maximum = true;
    Check(!app::EncodePublicSession(session), "reject conflicting recovered Max intents");
}
}

int main()
{
    try {
        UriVectors();
        ExactEditorAndPolicy();
        PublicRecovery();
        std::cout << checks << " recipient model assertions passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
