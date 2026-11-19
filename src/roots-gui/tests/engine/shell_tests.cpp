// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <algorithm>
#include <cmath>
using namespace roots::gui;
namespace a=app;namespace sh=ui::shell;
namespace {
struct Fixture final:a::Requests {
    sh::State workspace;sh::Facts facts;std::vector<a::Request> requests;
    Fixture(){workspace.documents=a::PublicFixtureDocuments();}
    a::Availability CanSubmit(const a::Request&) const override {return {true,"Explicit effect-free fixture handler."};}
    a::Receipt Submit(const a::Request& r) override {
        requests.push_back(r);
        if(r.command==a::Command::Component)return {true,{}};
        const auto* spec=a::Spec(r.command);return {false,spec?spec->dependency:"Fixture section service unavailable."};
    }
};
void Draw(ImGuiTestContext* ctx) { auto& s=ctx->GetVars<Fixture>();auto theme=ui::theme::Make(false);ui::theme::Scope scope(theme);s.facts=sh::DrawWorkspace(s.workspace,s,theme,{}, {1200,820}); }
ImGuiTest* Test(ImGuiTestEngine* e,const char* name){auto* t=IM_REGISTER_TEST(e,"roots.shell",name);t->SetVarsDataType<Fixture>();t->GuiFunc=Draw;return t;}
}
void RegisterShellTests(ImGuiTestEngine* engine)
{
    auto* t=Test(engine,"registry_identity_incomplete_and_transaction_state");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();auto& r=s.workspace.documents;const auto binding=r.Find(1)->binding.value();
        IM_CHECK_EQ(r.OpenWallet("renamed display",binding,true),1);IM_CHECK_EQ(r.Documents().size(),3);
        auto other=binding;other.wallet=*WalletId::Make("incomplete-public-wallet");const auto id=r.OpenWallet("New public wallet",other,false);
        IM_CHECK_EQ(r.ActiveId(),id);IM_CHECK_EQ(r.Active()->section,a::Section::Settings);IM_CHECK(r.Navigate(a::Section::Send));IM_CHECK(!a::Dependency(*r.Active(),a::Section::Send).empty());
        IM_CHECK(!r.Navigate(a::Section(99)));IM_CHECK(!r.Activate(9999));IM_CHECK(r.Activate(3));r.Active()->transaction.note.SetDraft("Transaction draft");
        IM_CHECK(!r.Navigate(a::Section::Transactions));r.Activate(1);r.Activate(3);IM_CHECK_EQ(r.Active()->transaction.note.draft(),"Transaction draft");
    };
    t=Test(engine,"mouse_all_sections_retain_draft_selection_scroll_per_wallet");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();ctx->SetRef("###workspace");
        std::array<float,6> scrolls{},table_scrolls{};const std::array<int,6> keys{1,2,3,5,6,7};
        for(int i=0;i<6;++i) {
            ctx->ItemClick(s.facts.navigation[i].id);ctx->Yield(2);IM_CHECK_EQ(int(s.workspace.documents.Active()->section),i);
            ctx->ItemClick(s.facts.note.id);ctx->KeyCharsReplace(("Draft section "+std::to_string(i)).c_str());
            IM_CHECK(s.facts.rows.size()>size_t(keys[i]));ctx->ItemClick(s.facts.rows[keys[i]-1].id);IM_CHECK(s.workspace.documents.Active()->Current().selection.selected.contains(keys[i]));
            const auto& table=s.facts.table_hover;ctx->MouseMoveToPos({(table[0]+table[2])/2,(table[1]+table[3])/2});ctx->MouseWheelX(-float(i+1));ctx->Yield(3);
            IM_CHECK(s.workspace.documents.Active()->Current().table_x>0);table_scrolls[i]=s.workspace.documents.Active()->Current().table_x;
            const auto& r=s.facts.layout.content;ctx->MouseMoveToPos({r.x+30,r.y+70});ctx->MouseWheelY(-float(i+1));ctx->Yield(3);
            IM_CHECK(s.workspace.documents.Active()->Current().scroll_y>0);scrolls[i]=s.workspace.documents.Active()->Current().scroll_y;
        }
        ctx->ItemClick(s.facts.tabs.at(2).id);IM_CHECK_EQ(s.workspace.documents.ActiveId(),2);ctx->Yield(2);
        ctx->ItemInputValue(s.facts.note.id,"Second wallet note");IM_CHECK_EQ(s.requests.back().document,2);
        ctx->ItemClick(s.facts.tabs.at(1).id);ctx->Yield(2);
        for(int i=0;i<6;++i) {
            ctx->ItemClick(s.facts.navigation[i].id);ctx->Yield(3);const auto& state=s.workspace.documents.Active()->Current();
            IM_CHECK_EQ(state.note.draft(),"Draft section "+std::to_string(i));IM_CHECK(state.selection.selected.contains(keys[i]));IM_CHECK(std::abs(state.scroll_y-scrolls[i])<1.F);IM_CHECK(std::abs(state.table_x-table_scrolls[i])<1.F);
        }
        IM_CHECK_EQ(s.workspace.documents.Find(2)->sections[0].note.saved,"Second wallet note");
    };
    t=Test(engine,"keyboard_routes_and_document_tabs_preserve_isolation");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();ctx->SetRef("###workspace");
        for(int i=0;i<6;++i){ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey(ImGuiKey_1+i));ctx->Yield(2);IM_CHECK_EQ(int(s.workspace.documents.Active()->section),i);}
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiKey_H);IM_CHECK(s.workspace.privacy.hidden);
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_B);IM_CHECK_EQ(s.workspace.unit,1);
        ctx->ItemNavActivate(s.facts.tabs.at(2).id);IM_CHECK_EQ(s.workspace.documents.ActiveId(),2);
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_2);IM_CHECK_EQ(s.workspace.documents.Active()->section,a::Section::Send);
        IM_CHECK_EQ(s.workspace.documents.Find(1)->section,a::Section::Settings);
        ctx->ItemNavActivate(s.facts.tabs.at(3).id);IM_CHECK_EQ(s.workspace.documents.ActiveId(),3);ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_1);IM_CHECK_EQ(s.workspace.documents.ActiveId(),3);
        ctx->ItemInputValue(s.facts.note.id,"Transaction document note");IM_CHECK(!s.requests.empty());IM_CHECK_EQ(s.requests.back().document,3);
        ctx->ItemNavActivate(s.facts.tabs.at(1).id);ctx->ItemNavActivate(s.facts.tabs.at(3).id);
        IM_CHECK_EQ(s.workspace.documents.Active()->transaction.note.saved,"Transaction document note");
    };
    t=Test(engine,"menus_routes_dependencies_and_incomplete_default");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();ctx->SetRef("###workspace");
        for(int i=0;i<4;++i){ctx->ItemNavActivate(s.facts.menus[i]);ctx->KeyPress(ImGuiKey_Escape);}
        ctx->ItemNavActivate(s.facts.menus[0]);ctx->Yield(2);IM_CHECK(s.facts.menu_items.contains(a::Command::NewWallet));ctx->ItemNavActivate(s.facts.menu_items.at(a::Command::NewWallet));IM_CHECK(!s.requests.empty());IM_CHECK_EQ(s.requests.back().command,a::Command::NewWallet);
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_N);IM_CHECK(!s.requests.empty());IM_CHECK_EQ(s.requests.back().command,a::Command::NewWallet);IM_CHECK(s.workspace.notice.find("daemon-wallet")!=std::string::npos);
        const auto inventory=a::CommandInventory();std::set<int> ids,expected;for(const auto& item:inventory)IM_CHECK(ids.insert(int(item.id)).second);
        for(int id=int(a::Command::NewWallet);id<=int(a::Command::About);++id)expected.insert(id);
        expected.insert(int(a::Command::Recovery));
        expected.insert(int(a::Command::SubmissionAttempts));
        IM_CHECK(ids==expected);IM_CHECK(a::Spec(a::Command::Recovery)->menu==a::Menu::File);
        IM_CHECK(a::Spec(a::Command::SubmissionAttempts)->menu==a::Menu::File);
        auto binding=s.workspace.documents.Find(1)->binding.value();binding.wallet=*WalletId::Make("new-wallet");const auto id=s.workspace.documents.OpenWallet("New wallet",binding,false);ctx->Yield(2);
        IM_CHECK_EQ(s.workspace.documents.Active()->section,a::Section::Settings);ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_2);ctx->Yield(2);
        IM_CHECK_EQ(s.workspace.documents.ActiveId(),id);IM_CHECK(s.facts.primary.disabled);IM_CHECK(!a::Dependency(*s.workspace.documents.Active(),a::Section::Send).empty());
        IM_CHECK(!s.facts.accounts_submitted);IM_CHECK_EQ(s.facts.layout.accounts.w,0.F);IM_CHECK_EQ(s.facts.layout.content.x,s.facts.layout.rail.x+s.facts.layout.rail.w);
    };
    t=Test(engine,"empty_runtime_shell_keeps_menus_and_explains_open_dependency");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();s.workspace.documents=a::DocumentRegistry{};ctx->Yield(2);ctx->SetRef("###workspace");
        IM_CHECK(s.facts.primary.visible);IM_CHECK_EQ(s.facts.layout.accounts.w,0.F);
        ctx->ItemNavActivate(s.facts.primary.id);IM_CHECK(!s.requests.empty());IM_CHECK_EQ(s.requests.back().command,a::Command::OpenWallet);IM_CHECK_EQ(s.requests.back().document,0);
        IM_CHECK(s.workspace.notice.find("daemon-wallet")!=std::string::npos);IM_CHECK(s.workspace.documents.Documents().empty());
    };

}
