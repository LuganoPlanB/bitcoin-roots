// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "tests/engine/recovery_fixture.h"
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include "adapters/protocol/public_wallet.h"
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <chrono>
#include <algorithm>

using namespace roots::gui;
namespace {
namespace shell = ui::shell;
constexpr auto GENESIS = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";
constexpr auto ADDRESS = "1BitcoinEaterAddressDontSendf59kuE";
constexpr auto OTHER = "1BoatSLRHtKNngkdXEeobR76b53LETtpyT";
constexpr auto DESCRIPTOR = "wpkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)";
struct Fixture final : app::Requests {
    shell::State state;
    shell::Facts facts;
    ConnectionService connection{[](const ServerCandidate& candidate, std::stop_token) {
        return BackendCapabilities{.candidate = candidate, .authenticated = true, .identity_verified = true,
            .chain = "main", .genesis = GENESIS, .wallet = WalletAvailability::Available, .descriptors = true};
    }};
    int submitted{};
    uint64_t wallet{}, other_wallet{};
    bool recovery_requested{};
    std::optional<test::RecoveryFixtureDirectory> scratch;
    std::unique_ptr<app::RecoverySession> recovery;

    void PrepareRecovery()
    {
        if (!recovery_requested || recovery) return;
        scratch.emplace();
        recovery=std::make_unique<app::RecoverySession>(scratch->Metadata());
    }

    Fixture()
    {
        ServerCandidate candidate;
        candidate.connection.credential.reference = "/unused/recipient-fixture-cookie";
        candidate.expected_genesis = GENESIS;
        candidate.wallet = "Paying wallet";
        if (!connection.SetDraft(candidate) || !connection.Test()) throw std::runtime_error("fixture connection");
        const auto binding = app::WalletBinding{*app::MetadataBackend(candidate), *WalletId::Make(*candidate.wallet),
            {*Hash256::Parse(GENESIS)}, {}};
        wallet = state.documents.OpenWallet("Paying wallet", binding, true);
        state.documents.Active()->history.enabled = true;
        state.documents.Navigate(app::Section::Send);
        auto second = binding;
        second.wallet = *WalletId::Make("Receiving wallet");
        other_wallet = state.documents.OpenWallet("Local display name", second, true);
        state.documents.Active()->history.enabled = true;
        const auto preview = protocol::PreviewPublicWallet(protocol::PublicImportKind::Descriptor, DESCRIPTOR, "main");
        if (!preview.preview) throw std::runtime_error("fixture descriptor");
        const auto& derived = preview.preview->descriptors.front();
        auto& receive = state.receives[other_wallet];
        receive.scope = app::HistoryMetadataScope(second);
        receive.selection = shell::ReceiveSelection{ReceiveTarget{derived.address, derived.script,
            derived.descriptor, "", "main", {}, 1000}, shell::ReceiveSource::Daemon};
        state.documents.Activate(wallet);
    }
    ConnectionService* Connection() override { return &connection; }
    app::RecoverySession* Recovery() override { return recovery.get(); }
    app::Receipt Submit(const app::Request&) override { ++submitted; return {false, "Unexpected external request"}; }
};

void Draw(ImGuiTestContext* ctx)
{
    auto& owned = ctx->GetVars<std::unique_ptr<Fixture>>();
    if (!owned) owned = std::make_unique<Fixture>();
    auto& fixture = *owned;
    fixture.PrepareRecovery();
    fixture.connection.Poll();
    if (fixture.connection.CanApply()) fixture.connection.Apply();
    const auto theme = ui::theme::Make(false);
    const ui::theme::Scope scope(theme);
    fixture.facts = shell::DrawWorkspace(fixture.state, fixture, theme, {}, {1200, 820});
}

void Edit(ImGuiTestContext* ctx, Fixture& f, const char* field, const std::string& value)
{
    ctx->ItemClick(f.facts.send.at(field).id);
    ctx->KeyCharsReplace(value.c_str());
    ctx->Yield(2);
}
void Press(ImGuiTestContext* ctx, Fixture& f, const std::string& key)
{
    ctx->ItemClick(f.facts.send.at(key).id);
    ctx->Yield(2);
}
}

void RegisterSendTests(ImGuiTestEngine* engine)
{
    auto* test = IM_REGISTER_TEST(engine, "roots.send", "atomic_uri_recipient_tabs_max_and_privacy");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc = Draw;
    test->TestFunc = [](ImGuiTestContext* ctx) {
        auto& f = *ctx->GetVars<std::unique_ptr<Fixture>>();
        ctx->SetRef("###workspace");
        const auto doc = [&] { return f.state.documents.Find(f.wallet); };
        const auto row = [&](uint64_t id) { return doc()->recipients.Find(id); };
        Edit(ctx, f, "pay-to", "bitcoin:" + std::string(ADDRESS) + "?amount=0.00001234&label=Invoice");
        Press(ctx, f, "apply");
        IM_CHECK_EQ(row(1)->address, ADDRESS);
        IM_CHECK_EQ(row(1)->amount, "0.00001234");
        IM_CHECK_EQ(row(1)->label, "Invoice");
        Press(ctx, f, "max");
        IM_CHECK(row(1)->maximum);
        Press(ctx, f, "add");
        IM_CHECK_EQ(doc()->recipients.selected, 2);
        Press(ctx, f, "max");
        IM_CHECK(!row(1)->maximum && row(2)->maximum);
        Edit(ctx, f, "pay-to", "bitcoin:" + std::string(OTHER) + "?amount=1&req-unknown=yes");
        Press(ctx, f, "apply");
        IM_CHECK(row(2)->address.empty() && row(2)->maximum && row(2)->amount.empty());
        IM_CHECK(f.state.presentation.Snapshot().find("unsupported parameter") != std::string::npos);
        Press(ctx, f, "recipient-1");
        IM_CHECK_EQ(row(1)->label, "Invoice");
        IM_CHECK(f.state.presentation.Snapshot().find("unsupported parameter") == std::string::npos);
        Edit(ctx, f, "pay-to", OTHER);
        ctx->KeyPress(ImGuiKey_Enter);
        ctx->Yield(2);
        IM_CHECK_EQ(row(1)->address, OTHER);
        IM_CHECK(row(1)->label.empty() && row(1)->amount.empty());
        Press(ctx, f, "unit");
        Edit(ctx, f, "amount", "654321");
        IM_CHECK_EQ(row(1)->amount, "654321");
        IM_CHECK(f.facts.send.at("qr").disabled);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_H);
        ctx->Yield(2);
        IM_CHECK(f.state.privacy.hidden && f.facts.send.empty());
        IM_CHECK(f.state.presentation.Snapshot().find("654321") == std::string::npos);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_H);
        ctx->Yield(2);
        Press(ctx, f, "add");
        Press(ctx, f, "add");
        IM_CHECK_EQ(doc()->recipients.selected, 4);
        IM_CHECK(f.facts.send.at("recipient-4").visible);
        IM_CHECK_EQ(f.submitted, 0);
    };
    test = IM_REGISTER_TEST(engine, "roots.send", "explicit_open_wallet_destination_rechecks_scope");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc = Draw;
    test->TestFunc = [](ImGuiTestContext* ctx) {
        auto& f = *ctx->GetVars<std::unique_ptr<Fixture>>();
        using namespace std::chrono_literals;
        for (int attempt = 0; attempt < 40 && !f.connection.Active(); ++attempt) {
            std::this_thread::sleep_for(1ms);
            ctx->Yield();
        }
        IM_CHECK(f.connection.Active().has_value());
        ctx->SetRef("###workspace");
        Press(ctx, f, "wallet");
        const auto option = "use-wallet-" + std::to_string(f.other_wallet);
        IM_CHECK(f.facts.send.contains(option));
        const auto valid = f.state.receives.at(f.other_wallet).scope;
        f.state.receives.at(f.other_wallet).scope->daemon_wallet = "Same display name, wrong daemon wallet";
        ctx->Yield(2);
        IM_CHECK(!f.facts.send.contains(option));
        f.state.receives.at(f.other_wallet).scope = valid;
        ctx->Yield(2);
        Press(ctx, f, option);
        IM_CHECK_EQ(f.state.documents.Active()->recipients.Find(1)->address,
            f.state.receives.at(f.other_wallet).selection->target.address);
        IM_CHECK(f.state.documents.Active()->recipients.Find(1)->label.empty());
        IM_CHECK_EQ(f.submitted, 0);
    };
    test = IM_REGISTER_TEST(engine, "roots.send", "clear_confirmation_is_revisioned_and_recoverable");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc = Draw;
    test->TestFunc = [](ImGuiTestContext* ctx) {
        auto& f = *ctx->GetVars<std::unique_ptr<Fixture>>();
        f.recovery_requested=true;
        ctx->Yield();
        IM_CHECK(f.recovery && !f.state.privacy.hidden);
        ctx->SetRef("###workspace");
        const auto draft = [&] { return f.state.documents.Find(f.wallet); };
        const auto click = [&](const char* id) {
            ctx->ItemClick(f.facts.document_actions.at(id).id);
            ctx->Yield(2);
        };
        const auto clear = [&] {
            ctx->ItemClick(f.facts.funding.at("clear").id);
            ctx->Yield(2);
        };
        IM_CHECK(draft()->recipients.ApplyDestination(1, "bitcoin:" + std::string(ADDRESS) + "?amount=1&label=Draft",
            protocol::DisplayNetwork::Mainnet, draft()->draft_revision));
        draft()->funding_rate = "2.125";
        draft()->selected_coins = {{*Hash256::Parse(std::string(64,'a')), 1}};
        ctx->Yield(2);
        clear();
        IM_CHECK(f.facts.document_dialog_visible);
        ++draft()->draft_revision.value;
        ctx->Yield(2);
        IM_CHECK(f.facts.document_actions.at("confirm-clear-send").disabled);
        click("cancel");
        IM_CHECK(draft()->recipients.Dirty());
        IM_CHECK(!f.recovery->HasClearedSend());
        clear();
        click("confirm-clear-send");
        IM_CHECK(!draft()->recipients.Dirty());
        IM_CHECK(draft()->funding_rate.empty() && draft()->selected_coins.empty());
        IM_CHECK(f.recovery->HasClearedSend() && f.recovery->RecoverSend(f.state.documents));
        IM_CHECK_EQ(draft()->recipients.Find(1)->label, "Draft");
        IM_CHECK_EQ(draft()->funding_rate, "2.125");
        IM_CHECK_EQ(draft()->selected_coins.size(), 1);
        IM_CHECK(f.state.documents.Find(f.other_wallet));
        IM_CHECK_EQ(f.submitted, 0);
    };

    test = IM_REGISTER_TEST(engine, "roots.send", "create_public_funding_opens_unsigned_inspector");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc = Draw;
    test->TestFunc = [](ImGuiTestContext* ctx) {
        // Exact public fixture captured from an owned Roots regtest run. Both
        // outputs are fixed for this local admission test; live RPC is tested
        // separately with the production construction/recheck transport.
        constexpr auto source =
            "cHNidP8BAgQCAAAAAQMEAAAAAAEEAQEBBQECAfsEAgAAAAABAHECAAAAAfHWv0joY2btC1fa60NmA9cb+qhLG6gyXUMesIEb8TS1AAAAAA"
            "D9////AoDDyQEAAAAAFgAU/i9WudMFkk8bUsUmjr2A66DLhdBmLTwoAQAAABYAFAXGjQMWsLCg6f8HhRMjfoWSTOwgZQAAAAEBH4DDyQEA"
            "AAAAFgAU/i9WudMFkk8bUsUmjr2A66DLhdABDiD1e7hIdV1bvAkCIc53NFgZ0vnVg0oImwAZ0q5DFW6NNQEPBAAAAAABEAT9////AAEDCI"
            "CWmAAAAAAAAQQWABQdqOn5pnEP/Q+RLfRmbbc02NucLwABAwjUKzEBAAAAAAEEFgAU/sLycBw0j8I26rF7CvdSlU5BpAUA";
        constexpr auto previous =
            "02000000000101f1d6bf48e86366ed0b57daeb436603d71bfaa84b1ba8325d431eb0811bf134b50000000000fdffffff0280c3c901"
            "00000000160014fe2f56b9d305924f1b52c5268ebd80eba0cb85d0662d3c280100000016001405c68d0316b0b0a0e9ff078513237e"
            "85924cec200247304402200f277a1046696f0514ae9a67d18643f6ce0a4bd97f312b6189586d89c2c0e557022016fcca806df9747a"
            "579cb4b75da47b8ed60998a14af5cf74e04614f6cada0721012103b63f5cc741de344da3a57e4128d0ee281c125c37fc34f9ff3297"
            "2cc8fbd2d75f65000000";
        auto& f = *ctx->GetVars<std::unique_ptr<Fixture>>();
        f.recovery_requested=true;
        ctx->Yield();
        IM_CHECK(f.recovery && !f.state.privacy.hidden);
        const app::WalletBinding wallet{*BackendSessionId::Make("test-regtest"), *WalletId::Make("app command fixture"),
            {*Hash256::Parse("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")}, {}};
        const auto id=f.state.documents.OpenWallet("Funded fixture",wallet,true);
        auto* draft=f.state.documents.Find(id);
        draft->section=app::Section::Send;
        const auto network=protocol::DisplayNetwork::Regtest;
        IM_CHECK(draft->recipients.ApplyDestination(1,"bitcoin:bcrt1qrk5wn7dxwy8l6ru39h6xvmdhxnvdh8p0kpjlxw?amount=0.1",network,draft->draft_revision));
        IM_CHECK(draft->recipients.Add(draft->draft_revision));
        IM_CHECK(draft->recipients.ApplyDestination(2,"bitcoin:bcrt1qlmp0yuquxj8uydh2k9as4a6jj48yrfq9segr4s?amount=0.199997",network,draft->draft_revision));
        const auto point=Outpoint{*Hash256::Parse("358d6e1543aed219009b084a83d5f9d219583477ce210209bc5b5d7548b87bf5"),0};
        draft->selected_coins={point};
        const auto revision=draft->draft_revision;
        const auto parsed=protocol::ParseTransactionDocument(source,network);
        IM_CHECK(parsed.document);
        app::CoinDraft evidence{wallet,{{point,Money::Satoshis(30000000),"0014fe2f56b9d305924f1b52c5268ebd80eba0cb85d0","","",
            protocol::OfflineCoinProof{previous,"wpkh([6e919d95/84h/1h/0h/0/0]02b654d7b8dfd1b9949ac475d07867fff668919ab2ba82e62d7edecb61f93c3b96)#0y6kdrw6",{},{},1700000000000}}}};
        const app::ImportedDocument imported{parsed.document,network,app::DocumentRepresentation::Source,
            app::CreatedFunding{id,revision,Money::Satoshis(300),std::move(evidence)}};
        std::string error;
        const auto created=f.recovery->CreateFunded(f.state.documents,imported,error);
        IM_CHECK(created && created!=id);
        ctx->Yield(3);
        IM_CHECK_EQ(f.state.documents.Active()->imported->parsed->source,source);
        IM_CHECK_EQ(f.state.documents.Find(id)->draft_revision.value,revision.value);
        IM_CHECK(f.state.presentation.Snapshot().find("Created unsigned transaction")!=std::string::npos);
        IM_CHECK(!f.state.imported_views.at(created).reviewed && !f.state.imported_views.at(created).review_commitment);
        IM_CHECK_EQ(f.submitted,0);
    };
}

void RegisterBatchTests(ImGuiTestEngine* engine)
{
    auto* test=IM_REGISTER_TEST(engine,"roots.batch","atomic_apply_cancel_and_grid_focus");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc=Draw;
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& f=*ctx->GetVars<std::unique_ptr<Fixture>>();
        auto* draft=f.state.documents.Find(f.wallet);
        auto* other=f.state.documents.Find(f.other_wallet);
        const auto network=protocol::DisplayNetwork::Mainnet;
        IM_CHECK(draft->recipients.ApplyDestination(1,"bitcoin:"+std::string(ADDRESS)+"?amount=0.001&label=Original",network,draft->draft_revision));
        other->Current().note.SetDraft("Independent unsaved value");
        draft->funding_rate="2.125";
        const Outpoint coin{*Hash256::Parse(GENESIS),1};
        draft->selected_coins={coin};
        const auto revision=draft->draft_revision.value;
        ctx->SetRef("###workspace");
        ctx->Yield(2);
        const auto click=[&](const std::string& key){ctx->ItemClick(f.facts.batch.at(key).id);ctx->Yield(2);};
        const auto edit=[&](const std::string& key,std::string_view text){
            click(key);
            ctx->KeyCharsReplace(std::string(text).c_str());
            ctx->Yield(2);
        };
        click("open");
        IM_CHECK(f.state.batch && !f.facts.batch.at("apply").disabled);
        edit("label-1","New label");
        IM_CHECK_EQ(draft->recipients.rows[0].label,"Original");
        const auto exits=f.state.grid_exits;
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_Tab);
        ctx->Yield(3);
        IM_CHECK_EQ(f.state.grid_exits,exits+1);
        IM_CHECK(f.facts.tabs.at(f.wallet).focused);
        click("label-1");
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiKey_Tab);
        ctx->Yield(3);
        IM_CHECK_EQ(f.state.grid_exits,exits+2);
        IM_CHECK(f.facts.tabs.at(f.wallet).focused);
        click("add");
        IM_CHECK(f.facts.batch.at("apply").disabled);
        IM_CHECK_EQ(draft->draft_revision.value,revision);
        edit("address-2",OTHER);
        edit("amount-2","0.00001234");
        click("unit-2");
        IM_CHECK_EQ(f.state.batch->work.rows[1].amount,"1234");
        IM_CHECK(!f.facts.batch.at("apply").disabled);
        IM_CHECK_EQ(draft->draft_revision.value,revision);
        click("apply");
        IM_CHECK(!f.state.batch && draft->recipients.rows.size()==2);
        IM_CHECK_EQ(draft->draft_revision.value,revision+1);
        IM_CHECK_EQ(draft->recipients.rows[0].label,"New label");
        IM_CHECK_EQ(draft->recipients.rows[1].amount,"1234");
        IM_CHECK(draft->recipients.rows[1].unit==RecipientUnit::Sats);
        IM_CHECK(draft->selected_coins==std::vector<Outpoint>{coin} && draft->funding_rate=="2.125");
        IM_CHECK_EQ(other->Current().note.draft(),"Independent unsaved value");
        click("open");
        edit("label-1","Discard");
        click("cancel");
        IM_CHECK(!f.state.batch && draft->recipients.rows[0].label=="New label");
        IM_CHECK_EQ(draft->draft_revision.value,revision+1);
        IM_CHECK_EQ(f.submitted,0);
    };
    test=IM_REGISTER_TEST(engine,"roots.batch","stale_apply_and_hidden_data_are_retired");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc=Draw;
    test->TestFunc=[](ImGuiTestContext* ctx) {
        namespace c=ui::components;
        auto& f=*ctx->GetVars<std::unique_ptr<Fixture>>();
        auto* draft=f.state.documents.Find(f.wallet);
        IM_CHECK(draft->recipients.ApplyDestination(1,"bitcoin:"+std::string(ADDRESS)+"?amount=0.001",protocol::DisplayNetwork::Mainnet,draft->draft_revision));
        ctx->SetRef("###workspace");
        ctx->Yield(2);
        const auto open=[&] {ctx->ItemClick(f.facts.batch.at("open").id);ctx->Yield(2);};
        const auto edit=[&] {
            ctx->ItemClick(f.facts.batch.at("label-1").id);
            ctx->KeyCharsReplace("staged-only");
            ctx->Yield(2);
        };
        open();
        edit();
        const auto packet=f.state.presentation.exchange.Published();
        IM_CHECK(packet && packet->valid);
        const auto apply=std::ranges::find(packet->nodes,std::string("Apply batch"),&c::SemanticEntry::name);
        IM_CHECK(apply!=packet->nodes.end() && !apply->state.disabled);
        const auto queued=f.state.presentation.exchange.QueueNext(packet->generation,apply->id,c::ax::SemanticAction::Press);
        IM_CHECK(queued.second==c::ActionResult::Queued);
        const auto current=++draft->draft_revision.value;
        ctx->Yield(3);
        IM_CHECK(!f.state.batch && draft->recipients.rows[0].label.empty());
        IM_CHECK_EQ(draft->draft_revision.value,current);
        open();
        edit();
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiKey_H);
        ctx->Yield(2);
        IM_CHECK(f.state.privacy.hidden && !f.state.batch && f.facts.batch.empty());
        IM_CHECK(f.state.presentation.Snapshot().find("staged-only")==std::string::npos);
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiKey_H);
        ctx->Yield(2);
        ctx->ItemClick(f.facts.batch.at("analysis").id);
        ctx->Yield(2);
        IM_CHECK(f.state.send_analysis && f.state.presentation.Snapshot().find("are Unknown until a current funding preview")!=std::string::npos);
        f.state.documents.Activate(f.other_wallet);
        ctx->Yield(2);
        IM_CHECK(!f.state.send_analysis && !f.state.batch);
        IM_CHECK_EQ(f.submitted,0);
    };
}
