// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/preferences.h"
#include "ui/bootstrap.h"
#include "ui/theme/theme.h"
#include <imkit/imkit.h>
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
using namespace roots::gui;
struct FakePreferences final : PreferenceRequests {
    std::vector<PublicPreferences> saves;
    int cancels{};
    bool accept{true};
    bool Save(const PublicPreferences& value) override { saves.push_back(value); return accept; }
    void Cancel() override { ++cancels; }
};
struct Fixture { ui::PreferencePanel panel; FakePreferences service; };
void RegisterSemanticTests(ImGuiTestEngine*);
void RegisterComponentTests(ImGuiTestEngine*);
void RegisterPrivacyTests(ImGuiTestEngine*);
void RegisterCommandTests(ImGuiTestEngine*);
void RegisterDocumentTests(ImGuiTestEngine*);
void RegisterShellTests(ImGuiTestEngine*);
void RegisterServerTests(ImGuiTestEngine*);
void RegisterWalletOnboardingTests(ImGuiTestEngine*);
void RegisterHistoryTests(ImGuiTestEngine*);
void RegisterWalletToolsTests(ImGuiTestEngine*);
void RegisterAddressTests(ImGuiTestEngine*);
void RegisterCoinTests(ImGuiTestEngine*);
void RegisterCoinControlTests(ImGuiTestEngine*);
void RegisterAddressControlTests(ImGuiTestEngine*);
void RegisterSendTests(ImGuiTestEngine*);
void RegisterFlowTests(ImGuiTestEngine*);
void RegisterBatchTests(ImGuiTestEngine*);
void RegisterWorkbenchTests(ImGuiTestEngine*);
void RegisterSigningReviewTests(ImGuiTestEngine*);
void RegisterTransactionEditTests(ImGuiTestEngine*);
void RegisterHeadlessTests(ImGuiTestEngine* engine)
{
    RegisterSemanticTests(engine);
    RegisterComponentTests(engine);
    RegisterPrivacyTests(engine);
    RegisterShellTests(engine);
    RegisterDocumentTests(engine);
    RegisterCommandTests(engine);
    RegisterServerTests(engine);
    RegisterWalletOnboardingTests(engine);
    RegisterHistoryTests(engine);
    RegisterWalletToolsTests(engine);
    RegisterAddressTests(engine);
    RegisterCoinTests(engine);
    RegisterCoinControlTests(engine);
    RegisterAddressControlTests(engine);
    RegisterSendTests(engine);
    RegisterFlowTests(engine);
    RegisterBatchTests(engine);
    RegisterWorkbenchTests(engine);
    RegisterSigningReviewTests(engine);
    RegisterTransactionEditTests(engine);
    auto* test = IM_REGISTER_TEST(engine, "roots.preferences", "save_cancel_requests");
    test->SetVarsDataType<Fixture>();
    test->GuiFunc = [](ImGuiTestContext* ctx) {
        auto& s = ctx->GetVars<Fixture>();
        ImGui::SetNextWindowPos({10,10}, ImGuiCond_Always);
        ImGui::SetNextWindowSize({600,300}, ImGuiCond_Always);
        auto theme = ui::MakeBootstrapTheme({});
        imkit::ThemeScope scope(theme);
        ui::DrawPreferences(s.panel, s.service, theme);
    };
    test->TestFunc = [](ImGuiTestContext* ctx) {
        auto& s = ctx->GetVars<Fixture>();
        ctx->SetRef("###preferences");
        ctx->ItemInputValue("###refresh", 17);
        ctx->ItemClick("###hide-balances");
        IM_CHECK_EQ(s.service.saves.size(), 0);
        IM_CHECK_EQ(s.panel.saved.refresh_seconds, 5);
        ctx->ItemClick("###save");
        IM_CHECK_EQ(s.service.saves.size(), 1);
        IM_CHECK_EQ(s.service.saves[0].refresh_seconds, 17);
        IM_CHECK(s.service.saves[0].hide_balances);
        IM_CHECK_EQ(s.panel.saved.refresh_seconds, 17);
        ctx->ItemInputValue("###refresh", 0);
        ctx->ItemClick("###save");
        IM_CHECK_EQ(s.service.saves.size(), 1);
        ctx->ItemNavActivate("###cancel");
        IM_CHECK_EQ(s.service.cancels, 1);
        IM_CHECK_EQ(s.panel.draft.refresh_seconds, 17);
        ctx->ItemInputValue("###refresh", 29);
        s.service.accept = false;
        ctx->ItemNavActivate("###save");
        IM_CHECK_EQ(s.service.saves.size(), 2);
        IM_CHECK_EQ(s.service.saves[1].refresh_seconds, 29);
        IM_CHECK_EQ(s.panel.saved.refresh_seconds, 17);
        IM_CHECK(s.panel.save_rejected);
        ctx->ItemClick("###cancel");
        IM_CHECK_EQ(s.service.cancels, 2);
        IM_CHECK_EQ(s.service.saves.size(), 2);
        IM_CHECK_EQ(s.panel.draft.refresh_seconds, 17);
        IM_CHECK(!s.panel.save_rejected);
    };
    struct Bootstrap { ui::BootstrapState state; bool closed{}; };
    test = IM_REGISTER_TEST(engine, "roots.bootstrap", "production_close");
    test->SetVarsDataType<Bootstrap>();
    test->GuiFunc = [](ImGuiTestContext* ctx) {
        auto& s = ctx->GetVars<Bootstrap>();
        auto theme = ui::MakeBootstrapTheme({});
        imkit::ApplyTheme(theme);
        s.closed |= ui::DrawBootstrap(theme, {}, s.state).close_requested;
    };
    test->TestFunc = [](ImGuiTestContext* ctx) {
        auto& s = ctx->GetVars<Bootstrap>();
        IM_CHECK(!s.closed);
        ctx->SetRef("###roots-bootstrap");
        ctx->ItemClick("**/###bootstrap-close");
        IM_CHECK(s.closed);
    };
    struct AppearanceFixture { ui::BootstrapState state; std::vector<int> requests; };
    test=IM_REGISTER_TEST(engine,"roots.appearance","production_choice_requests");
    test->SetVarsDataType<AppearanceFixture>();
    test->GuiFunc=[](ImGuiTestContext* ctx) {
        auto& s=ctx->GetVars<AppearanceFixture>();s.state.show_appearance=true;
        const auto theme=ui::MakeBootstrapTheme({});ui::theme::Scope scope(theme);
        const auto facts=ui::DrawBootstrap(theme,{},s.state);
        if (facts.appearance_requested>=0) s.requests.push_back(facts.appearance_requested);
    };
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& s=ctx->GetVars<AppearanceFixture>();ctx->SetRef("###roots-bootstrap");
        ctx->ComboClick("###appearance/Dark");
        ctx->ComboClick("###appearance/System");
        ctx->ComboClick("###appearance/Light");
        IM_CHECK_EQ(s.requests.size(),3);
        IM_CHECK_EQ(s.requests[0],1);IM_CHECK_EQ(s.requests[1],2);IM_CHECK_EQ(s.requests[2],0);
        IM_CHECK_EQ(s.state.appearance_choice,0);
    };
    test = IM_REGISTER_TEST(engine, "runner.failure", "intentional_assertion");
    test->TestFunc = [](ImGuiTestContext* ctx) { IM_CHECK(false && "injected assertion failure"); };
    test = IM_REGISTER_TEST(engine, "runner.timeout", "held_callback");
    test->TestFunc = [](ImGuiTestContext*) {
        std::puts("TIMEOUT_PRECONDITION_ENTERED"); std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::seconds(30));
    };
}
