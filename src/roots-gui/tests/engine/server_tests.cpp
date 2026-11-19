// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/server.h"
#include "ui/components/native_input.h"
#include "ui/bootstrap.h"
#include "ui/theme/theme.h"
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <atomic>
#include <memory>
#include <thread>
using namespace roots::gui;
namespace {
struct Fixture {
    struct Picker final : ui::components::NativeInputPort {
        bool queued{};
        void BeginView(std::string_view) override{}
        void Privacy(bool) override{}
        void EnterEditor(uint32_t,bool) override{}
        void LeaveEditor() override{}
        std::optional<std::string> Paste(uint32_t,std::string_view,int,int,int,size_t) override{return {};}
        bool FileChooserAvailable() const override{return true;}
        uint64_t ChooseFile(std::string_view,bool) override{return 0;}
        uint64_t ChooseRpcCookie() override{queued=true;return 1;}
        ui::components::FileChoice PollFile(uint64_t id) override{
            if(id!=1 || !queued)return {ui::components::NativeOutcome::Stale,{},{}};
            queued=false;
            return {ui::components::NativeOutcome::Chosen,"/explicit/picked-cookie",{}};
        }
    } picker;
    ui::ServerPanel panel;
    ui::components::PresentationFrame presentation;
    ui::ServerFacts facts;
    std::atomic<int> calls{};
    std::unique_ptr<ConnectionService> service;
    bool opened{}, can_apply{};
};
}
void RegisterServerTests(ImGuiTestEngine* engine)
{
    auto* test = IM_REGISTER_TEST(engine, "roots.server", "test_apply_and_failed_replacement");
    test->SetVarsDataType<Fixture>();
    test->GuiFunc = [](ImGuiTestContext* ctx) {
        auto& s = ctx->GetVars<Fixture>();
        if (!s.service) s.service = std::make_unique<ConnectionService>([&s](const ServerCandidate& candidate, std::stop_token) {
            ++s.calls;
            BackendCapabilities result; result.candidate = candidate; result.authenticated = result.identity_verified = true;
            result.wallet = WalletAvailability::Unselected;
            if (candidate.connection.port == 2) result.error = rpc::Error::Connection;
            return result;
        }); // GUI-owned service: Test Engine constructs Vars on its coroutine.
        if (!s.opened) { s.panel.credential.SetDraft("/explicit/fixture-cookie"); ui::OpenServer(s.panel, *s.service); s.opened = true; }
        const auto theme = ui::MakeBootstrapTheme({}); ui::theme::Scope scope(theme);
        const auto current = [&] { return std::to_string(s.panel.dialog.open) + ':' + std::to_string(s.panel.scope_revision) + ':' + std::to_string(s.panel.scope_session); };
        s.presentation.Begin(current(), current);
        const ui::components::NativeInputScope native(&s.picker);
        s.facts = ui::DrawServer(s.panel, *s.service, s.presentation, theme, 1.F);
        s.presentation.Seal();
        s.can_apply = s.service->CanApply();
    };
    test->TestFunc = [](ImGuiTestContext* ctx) {
        auto& s = ctx->GetVars<Fixture>(); ctx->SetRef("###server");
        IM_CHECK(!s.service->Active() && !s.can_apply);
        ctx->ItemClick("**/test/Test Connection");
        for (int i = 0; i < 20 && !s.can_apply; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); ctx->Yield(); }
        IM_CHECK(s.can_apply); IM_CHECK(!s.service->Active());
        ctx->ItemNavActivate("**/apply/Apply");
        IM_CHECK(s.service->Active() && s.service->Session() == 1);
        const auto original=s.service->Active()->candidate;
        ctx->ItemClick(s.facts.items.at("Private index").id);
        IM_CHECK(s.panel.source==ui::ServerSource::PrivateIndex && s.facts.items.at("test").disabled);
        ctx->ItemClick(s.facts.items.at("Public index").id);
        IM_CHECK(s.panel.source==ui::ServerSource::PublicIndex && s.facts.items.at("apply").disabled);
        IM_CHECK(s.calls==1 && s.service->Active()->candidate==original);
        ctx->ItemClick(s.facts.items.at("Direct RPC").id);
        ctx->ItemClick(s.facts.items.at("browse-cookie").id);
        ctx->Yield(2);
        IM_CHECK(s.service->Draft().connection.credential.reference=="/explicit/picked-cookie");
        IM_CHECK(s.calls==1 && s.service->Active()->candidate==original && !s.can_apply);
        ctx->ItemInputValue("**/port/###input", "2");
        IM_CHECK(!s.can_apply);
        ctx->ItemClick("**/test/Test Connection");
        for (int i = 0; i < 20 && s.service->Busy(); ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); ctx->Yield(); }
        IM_CHECK(s.service->Tested() && s.service->Tested()->error == rpc::Error::Connection);
        IM_CHECK(!s.can_apply); IM_CHECK_EQ(s.service->Active()->candidate.connection.port, 8332);
        ctx->ItemNavActivate("**/cancel/Cancel");
        IM_CHECK(!s.panel.dialog.open); IM_CHECK(s.service->Active()->candidate==original && s.service->Session()==1);
    };
}
