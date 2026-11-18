// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <atomic>
#include <chrono>
#include <memory>

using namespace roots::gui;
namespace {
using namespace std::chrono_literals;
namespace shell = ui::shell;

BackendCapabilities Verified(ServerCandidate candidate)
{
    BackendCapabilities backend;
    backend.candidate = std::move(candidate);
    backend.authenticated = backend.identity_verified = true;
    backend.genesis = backend.candidate.expected_genesis;
    for (const auto* method : {"listwallets", "listwalletdir", "getwalletinfo"}) {
        backend.methods.emplace(method, MethodSupport{true, true, false, false, {}});
    }
    backend.methods.emplace("loadwallet", MethodSupport{true, true, false, false, {"filename"}});
    backend.methods.emplace("createwallet", MethodSupport{true, true, false, false, {"wallet_name", "disable_private_keys", "blank", "descriptors", "external_signer"}});
    if (backend.candidate.wallet) {
        backend.wallet = WalletAvailability::Available;
        backend.descriptors = true;
    }
    return backend;
}

struct Fixture final : app::Requests {
    enum class OwnerAction { None, InvalidCreate, Load, ChangeProfile };
    std::atomic<int> probes{}, actions{}, mutations{};
    std::atomic<bool> hold_load{}, entered{}, cancelled{};
    ConnectionService connection{[this](const ServerCandidate& candidate, std::stop_token) {
        ++probes;
        return Verified(candidate);
    }};
    WalletLifecycle wallets{[this](const WalletOperation& work, std::stop_token stop) {
        ++actions;
        WalletResult result;
        if (work.action == WalletAction::List) {
            result.directory_verified = true;
            result.entries = {{"", true}, {"saved wallet", false}};
            return result;
        }
        if (work.action == WalletAction::Load || work.action == WalletAction::Create) {
            ++mutations;
            result.mutation_dispatched = true;
        }
        if (work.action == WalletAction::Load && hold_load) {
            entered = true;
            const auto deadline = std::chrono::steady_clock::now() + 3s;
            while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
            cancelled = stop.stop_requested();
        }
        auto candidate = work.backend.candidate;
        candidate.wallet = work.name;
        result.wallet = Verified(candidate);
        return result;
    }};
    shell::State state;
    shell::Facts facts;
    bool initialized{};
    OwnerAction action{OwnerAction::None};
    std::optional<bool> accepted;
    ConnectionService* Connection() override { return &connection; }
    WalletLifecycle* Wallets() override { return &wallets; }
    app::Availability CanSubmit(const app::Request& request) const override {
        return {request.command == app::Command::Introduction || request.command == app::Command::OpenWallet || request.command == app::Command::NewWallet, ""};
    }
    app::Receipt Submit(const app::Request&) override { return {}; }
};

void Wait(ImGuiTestContext* ctx, auto done)
{
    for (int frame = 0; frame < 120 && !done(); ++frame) {
        std::this_thread::sleep_for(1ms);
        ctx->Yield();
    }
}

void Draw(ImGuiTestContext* ctx, bool introduce)
{
    auto& f = *ctx->GetVars<std::unique_ptr<Fixture>>();
    if (!f.initialized) {
        f.initialized = true;
        if (introduce) shell::BeginOnboarding(f.state, shell::OnboardingPage::Introduction, f);
        else {
            ServerCandidate draft;
            draft.connection.credential.reference = "/unused/test-cookie";
            draft.expected_genesis = std::string(64, 'a');
            f.connection.SetDraft(draft);
            f.connection.Test();
        }
    }
    f.connection.Poll();
    if (!introduce && f.connection.CanApply()) f.connection.Apply();
    switch (std::exchange(f.action, Fixture::OwnerAction::None)) {
    case Fixture::OwnerAction::None: break;
    case Fixture::OwnerAction::InvalidCreate:
        f.accepted = f.wallets.Start(WalletAction::Create, "", f.connection, {}, 0);
        break;
    case Fixture::OwnerAction::Load:
        f.accepted = f.wallets.Start(WalletAction::Load, "saved wallet", f.connection, {}, f.state.documents.ActiveId());
        break;
    case Fixture::OwnerAction::ChangeProfile: {
        auto other = f.connection.Draft();
        other.connection.port = 18443;
        f.accepted = f.connection.SetDraft(other);
        break;
    }
    }
    const auto theme = ui::MakeBootstrapTheme({});
    ui::theme::Scope scope(theme);
    f.facts = shell::DrawWorkspace(f.state, f, theme, {});
}
}

void RegisterWalletOnboardingTests(ImGuiTestEngine* engine)
{
    auto* offline = IM_REGISTER_TEST(engine, "roots.wallet_onboarding", "first_launch_offline_requires_no_transport");
    offline->SetVarsDataType<std::unique_ptr<Fixture>>();
    offline->GuiFunc = [](ImGuiTestContext* ctx) {
        auto& fixture = ctx->GetVars<std::unique_ptr<Fixture>>();
        if (!fixture) fixture = std::make_unique<Fixture>();
        Draw(ctx, true);
    };
    offline->TestFunc = [](ImGuiTestContext* ctx) {
        auto& f = *ctx->GetVars<std::unique_ptr<Fixture>>();
        IM_CHECK(f.state.onboarding.page == shell::OnboardingPage::Introduction);
        IM_CHECK(f.facts.onboarding.at("indexing").disabled);
        ctx->ItemClick(f.facts.onboarding.at("offline").id);
        ctx->Yield(4);
        IM_CHECK(f.connection.Offline() && !f.connection.Active());
        IM_CHECK(!f.state.onboarding.dialog.open && !f.state.recovery_dialog.open);
        IM_CHECK(f.state.documents.Documents().empty() && f.facts.drop_target.w > 0);
        IM_CHECK_EQ(f.probes.load(), 0);
        IM_CHECK_EQ(f.actions.load(), 0);
        ctx->SetRef("###workspace");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_O);
        IM_CHECK(f.state.onboarding.page == shell::OnboardingPage::Open);
        IM_CHECK(f.facts.onboarding.contains("configure") && !f.facts.onboarding.contains("refresh"));
        IM_CHECK_EQ(f.actions.load(), 0);
        ctx->ItemClick(f.facts.onboarding.at("cancel").id);
        ctx->Yield(3);
        const auto introduction = [&] {
            ctx->SetRef("###workspace");
            ctx->ItemClick(f.facts.menus.at(3));
            ctx->ItemClick(f.facts.menu_rows.at(app::Command::Introduction).id);
            ctx->Yield(2);
        };
        introduction();
        IM_CHECK(f.state.onboarding.page == shell::OnboardingPage::Introduction);
        ctx->ItemClick(f.facts.onboarding.at("local").id);
        ctx->Yield(2);
        IM_CHECK(f.state.server.dialog.open && f.facts.server.visible && !f.state.server.tls);
        ctx->ItemClick(f.facts.server.items.at("cancel").id);
        ctx->Yield(3);
        introduction();
        IM_CHECK(f.state.onboarding.page == shell::OnboardingPage::Introduction);
        ctx->ItemClick(f.facts.onboarding.at("remote").id);
        ctx->Yield(2);
        IM_CHECK(f.state.server.dialog.open && f.facts.server.visible && f.state.server.tls);
        IM_CHECK(!f.connection.Active());
        IM_CHECK_EQ(f.probes.load(), 0);
        IM_CHECK_EQ(f.actions.load(), 0);
    };

    auto* lifecycle = IM_REGISTER_TEST(engine, "roots.wallet_onboarding", "open_default_and_fence_cancelled_load");
    lifecycle->SetVarsDataType<std::unique_ptr<Fixture>>();
    lifecycle->GuiFunc = [](ImGuiTestContext* ctx) {
        auto& fixture = ctx->GetVars<std::unique_ptr<Fixture>>();
        if (!fixture) fixture = std::make_unique<Fixture>();
        Draw(ctx, false);
    };
    lifecycle->TestFunc = [](ImGuiTestContext* ctx) {
        auto& f = *ctx->GetVars<std::unique_ptr<Fixture>>();
        Wait(ctx, [&] { return f.connection.Active().has_value(); });
        IM_CHECK(f.connection.Active());
        ctx->SetRef("###workspace");
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_O);
        ctx->ItemClick(f.facts.onboarding.at("refresh").id);
        Wait(ctx, [&] { return f.state.onboarding.wallets.size() == 2; });
        IM_CHECK_EQ(f.state.onboarding.wallets.size(), 2);
        ctx->ItemClick(f.facts.onboarding.at("wallet-0").id);
        ctx->ItemClick(f.facts.onboarding.at("open").id);
        Wait(ctx, [&] { return f.state.documents.Active() != nullptr; });
        IM_CHECK(f.state.documents.Active() && f.state.documents.Active()->binding->DaemonWallet().empty());
        IM_CHECK_EQ(f.state.documents.Active()->name, "Default daemon wallet");
        IM_CHECK_EQ(f.mutations.load(), 0);
        ctx->Yield(4);
        ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_O);
        ctx->ItemClick(f.facts.onboarding.at("refresh").id);
        Wait(ctx, [&] { return f.state.onboarding.wallets.size() == 2; });
        IM_CHECK_EQ(f.state.onboarding.wallets.size(), 2);
        f.hold_load = true;
        ctx->ItemClick(f.facts.onboarding.at("wallet-1").id);
        ctx->ItemClick(f.facts.onboarding.at("open").id);
        Wait(ctx, [&] { return f.entered.load(); });
        IM_CHECK(f.entered);
        ctx->ItemClick(f.facts.onboarding.at("cancel").id);
        Wait(ctx, [&] { return !f.wallets.Busy(); });
        IM_CHECK(!f.wallets.Busy() && f.cancelled && !f.state.onboarding.dialog.open);
        IM_CHECK_EQ(f.mutations.load(), 1);
        IM_CHECK_EQ(f.state.documents.Documents().size(), 1);
        IM_CHECK(f.wallets.Uncertain(*f.connection.Active(), "saved wallet"));
        f.action = Fixture::OwnerAction::Load;
        ctx->Yield();
        IM_CHECK(f.accepted == false);
        IM_CHECK_EQ(f.mutations.load(), 1);
    };

    auto* identity = IM_REGISTER_TEST(engine, "roots.wallet_onboarding", "names_and_stale_profile_retire_mutation_completion");
    identity->SetVarsDataType<std::unique_ptr<Fixture>>();
    identity->GuiFunc = lifecycle->GuiFunc;
    identity->TestFunc = [](ImGuiTestContext* ctx) {
        auto& f = *ctx->GetVars<std::unique_ptr<Fixture>>();
        Wait(ctx, [&] { return f.connection.Active().has_value(); });
        IM_CHECK(f.connection.Active());
        for (const auto* invalid : {"", ".", "../wallet", "/daemon/wallet", "bad\\name", "COM1.txt", "name. ", "a:b"}) {
            IM_CHECK(!NewWalletNameError(invalid).empty());
        }
        IM_CHECK(NewWalletNameError("Desk wallet_01").empty() && ValidDaemonWalletName(""));
        f.action = Fixture::OwnerAction::InvalidCreate;
        ctx->Yield();
        IM_CHECK(f.accepted == false);
        f.hold_load = true;
        f.action = Fixture::OwnerAction::Load;
        ctx->Yield();
        IM_CHECK(f.accepted == true);
        Wait(ctx, [&] { return f.entered.load(); });
        IM_CHECK(f.entered);
        f.accepted.reset();
        f.action = Fixture::OwnerAction::ChangeProfile;
        ctx->Yield();
        IM_CHECK(f.accepted == true);
        Wait(ctx, [&] { return !f.wallets.Busy(); });
        IM_CHECK(f.cancelled && !f.wallets.Busy() && f.state.documents.Documents().empty());
        IM_CHECK(!f.connection.Active()->candidate.wallet);
        IM_CHECK(f.wallets.Uncertain(*f.connection.Active(), "saved wallet"));
    };
}
