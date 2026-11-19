// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <fstream>
#include <set>
using namespace roots::gui;
namespace sh=ui::shell;namespace a=app;
namespace {
struct Fixture final:a::Requests {
    sh::State state;sh::Facts facts;a::HostObservations observations;
    std::vector<a::Request> requests;std::vector<int> appearance;std::optional<a::Command> unavailable;bool acknowledge{true};std::string rendered;
    Fixture(){state.documents=a::PublicFixtureDocuments();observations.connection={a::ServiceState::Offline,"Explicit fixture connection, no RPC."};observations.network={a::ServiceState::Ready,"regtest fixture"};observations.activity={a::ServiceState::Ready,"Explicit public activity fixture."};observations.tasks={{101,"Public fixture sync","No wallet effect.",a::ServiceState::Loading,425}};observations.log={"Public operational fixture started.","No financial payloads in this fixture log."};}
    a::Availability CanSubmit(const a::Request& r) const override {return {unavailable!=r.command,unavailable==r.command?"Explicit missing fixture adapter.":"Explicit effect-free handler."};}
    a::Receipt Submit(const a::Request& r) override {requests.push_back(r);return {acknowledge,acknowledge?"Explicit effect-free fixture acknowledged.":"Fixture acknowledgement rejected; state retained."};}
    a::HostObservations Observe() const override{return observations;}
};
void Draw(ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();auto theme=ui::theme::Make(false);ui::theme::Scope scope(theme);
    auto& p=ImGui::GetPlatformIO();const auto fn=p.Platform_SetClipboardTextFn;void* user=p.Platform_ClipboardUserData;p.Platform_ClipboardUserData=&s;
    p.Platform_SetClipboardTextFn=[](ImGuiContext*,const char* text){static_cast<Fixture*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->rendered=text;};
    ImGui::LogToClipboard();s.facts=sh::DrawWorkspace(s.state,s,theme,{}, {1200,820});ImGui::LogFinish();p.Platform_SetClipboardTextFn=fn;p.Platform_ClipboardUserData=user;
    if(s.facts.appearance_requested>=0)s.appearance.push_back(s.facts.appearance_requested);
}
ImGuiTest* Test(ImGuiTestEngine* e,const char* name){auto* t=IM_REGISTER_TEST(e,"roots.commands",name);t->SetVarsDataType<Fixture>();t->GuiFunc=Draw;return t;}
bool Transaction(a::Command c){return c==a::Command::SaveTransaction || c==a::Command::ShowQR || c==a::Command::TransactionHex;}
void Reset(ImGuiTestContext* ctx,Fixture& s,a::Command command){ctx->KeyPress(ImGuiKey_Escape);ctx->KeyPress(ImGuiKey_Escape);s.state.documents=a::PublicFixtureDocuments();if(Transaction(command))s.state.documents.Activate(3);s.state.actions=sh::DocumentActions();s.state.drawer=sh::Drawer::None;s.state.invocation.reset();s.requests.clear();ctx->Yield(2);ctx->SetRef("###workspace");}
}
void RegisterCommandTests(ImGuiTestEngine* engine)
{
    auto* t=Test(engine,"every_menu_route_keyboard_and_context");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();std::set<int> visited;std::ofstream log("commands.routes.txt");
        for(const auto& spec:a::CommandInventory()) {
            Reset(ctx,s,spec.id);
            if(spec.id==a::Command::Sweep){
                IM_CHECK(sh::SetAmountVisibility(s.state,true));
                IM_CHECK(!sh::CommandAvailability(s.state,spec.id,s).available);
                IM_CHECK(sh::SetAmountVisibility(s.state,false));
                ctx->Yield(1);
            }
            const auto id=s.state.documents.ActiveId();ctx->ItemNavActivate(s.facts.menus[int(spec.menu)]);ctx->Yield(1);
            IM_CHECK(s.facts.menu_items.contains(spec.id));ctx->ItemNavActivate(s.facts.menu_items.at(spec.id));ctx->Yield(1);
            IM_CHECK(s.state.invocation.has_value());IM_CHECK_EQ(s.state.invocation->command,spec.id);IM_CHECK_EQ(s.state.invocation->document,id);IM_CHECK(visited.insert(int(spec.id)).second);
            if(s.state.invocation->dispatched){IM_CHECK_EQ(s.requests.size(),1);IM_CHECK_EQ(s.requests.back().command,spec.id);IM_CHECK_EQ(s.requests.back().document,id);IM_CHECK(s.state.invocation->accepted);}
            else {IM_CHECK(s.requests.empty());if(!s.state.invocation->available)IM_CHECK(!s.state.notice.empty());}
            if(spec.id==a::Command::ImportWallet)IM_CHECK_EQ(s.state.actions.kind,sh::DocumentDialog::Import);
            if(spec.id==a::Command::RenameWallet)IM_CHECK_EQ(s.state.actions.kind,sh::DocumentDialog::Rename);
            if(spec.id==a::Command::DeleteWallet)IM_CHECK_EQ(s.state.actions.kind,sh::DocumentDialog::Delete);
            if(spec.id==a::Command::LoadingLog)IM_CHECK_EQ(s.state.drawer,sh::Drawer::Activity);
            if(spec.id==a::Command::Sweep){
                IM_CHECK(s.facts.sweep_visible && s.state.invocation->accepted);
                IM_CHECK(!s.state.invocation->dispatched && s.requests.empty());
                IM_CHECK(s.rendered.find("signs even its preview")!=std::string::npos);
            }
            if(spec.id==a::Command::Contacts){
                IM_CHECK(s.facts.payment_protocol_visible && s.state.invocation->accepted);
                IM_CHECK(!s.state.invocation->dispatched && s.requests.empty());
                IM_CHECK(s.rendered.find("PayjoinModule")!=std::string::npos);
                IM_CHECK(s.rendered.find("PaymentCodeContacts")!=std::string::npos);
            }
            if(spec.id==a::Command::CloseTab)IM_CHECK(!s.state.documents.Find(id));
            log<<int(spec.id)<<' '<<spec.label<<" | "<<s.facts.shortcut_labels.at(spec.id)<<" | document="<<id<<" | available="<<s.state.invocation->available<<" | dispatched="<<s.state.invocation->dispatched<<"\n";
        }
        IM_CHECK_EQ(visited.size(),a::CommandInventory().size());
    };
    t=Test(engine,"all_primary_shortcuts_and_platform_mapping");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();
        struct Key {a::Command command;ImGuiKey key;ImGuiKeyChord modifiers;};
        const Key keys[]{{a::Command::NewWallet,ImGuiKey_N,0},{a::Command::OpenWallet,ImGuiKey_O,0},{a::Command::TransactionFile,ImGuiKey_F,0},{a::Command::TransactionId,ImGuiKey_I,0},{a::Command::TransactionText,ImGuiKey_T,0},{a::Command::TransactionQR,ImGuiKey_U,0},{a::Command::SaveTransaction,ImGuiKey_S,0},{a::Command::Preferences,ImGuiKey_Comma,0},{a::Command::CloseTab,ImGuiKey_W,0},
          {a::Command::Transactions,ImGuiKey_1,0},{a::Command::Send,ImGuiKey_2,0},{a::Command::Receive,ImGuiKey_3,0},{a::Command::Addresses,ImGuiKey_4,0},{a::Command::UTXOs,ImGuiKey_5,0},{a::Command::Settings,ImGuiKey_6,0},{a::Command::Refresh,ImGuiKey_R,0},{a::Command::Units,ImGuiKey_B,0},{a::Command::HideAmounts,ImGuiKey_H,ImGuiMod_Shift},{a::Command::LockCurrent,ImGuiKey_L,0},{a::Command::LockAll,ImGuiKey_L,ImGuiMod_Shift},{a::Command::SearchCurrent,ImGuiKey_S,ImGuiMod_Shift},{a::Command::SearchAll,ImGuiKey_S,ImGuiMod_Shift|ImGuiMod_Alt},{a::Command::SignMessage,ImGuiKey_M,0}};
        std::set<int> visited;
        for(const auto& key:keys) {
            Reset(ctx,s,key.command);const auto* spec=a::Spec(key.command);IM_CHECK(spec);
            IM_CHECK_EQ(sh::CommandChord(spec->shortcut,false),ImGuiMod_Ctrl|key.modifiers|key.key);IM_CHECK_EQ(sh::CommandChord(spec->shortcut,true),ImGuiMod_Super|key.modifiers|key.key);
            IM_CHECK(sh::CommandShortcutLabel(spec->shortcut,true).starts_with("Cmd+"));IM_CHECK_EQ(s.facts.shortcut_labels.at(key.command),std::string("Ctrl+")+spec->shortcut);
            ctx->KeyPress(ImGuiMod_Ctrl|key.modifiers|key.key);ctx->Yield(1);IM_CHECK(s.state.invocation.has_value());IM_CHECK_EQ(s.state.invocation->command,key.command);IM_CHECK(visited.insert(int(key.command)).second);
        }
        IM_CHECK_EQ(visited.size(),23);size_t shortcuts{};for(const auto& spec:a::CommandInventory())if(*spec.shortcut)++shortcuts;IM_CHECK_EQ(shortcuts,23);
    };
    t=Test(engine,"unavailable_and_failed_acknowledgement_preserve_state");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();Reset(ctx,s,a::Command::Refresh);s.state.documents.Find(1)->sections[4].note.SetDraft("Retain after rejection");s.state.documents.Find(1)->sections[4].selection.selected={8};
        s.unavailable=a::Command::Refresh;ctx->Yield(2);IM_CHECK(!s.facts.availability.at(a::Command::Refresh).available);ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_R);ctx->Yield(1);
        IM_CHECK(s.requests.empty());IM_CHECK(!s.state.invocation->dispatched);IM_CHECK(s.state.notice.find("missing fixture adapter")!=std::string::npos);
        s.unavailable.reset();s.acknowledge=false;ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_R);ctx->Yield(1);IM_CHECK_EQ(s.requests.size(),1);IM_CHECK(!s.state.invocation->accepted);
        IM_CHECK_EQ(s.state.documents.Find(1)->sections[4].note.draft(),"Retain after rejection");IM_CHECK(s.state.documents.Find(1)->sections[4].selection.selected.contains(8));IM_CHECK_EQ(s.state.documents.Documents().size(),3);
        s.state.documents.Activate(3);ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_L);ctx->Yield(1);IM_CHECK_EQ(s.requests.size(),1);IM_CHECK(!s.state.invocation->available);IM_CHECK_EQ(s.state.documents.ActiveId(),3);
        s.state.documents=a::DocumentRegistry{};ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_2);ctx->Yield(1);IM_CHECK(!s.state.invocation->available);IM_CHECK(s.state.documents.Documents().empty());
    };
    t=Test(engine,"live_status_activity_privacy_and_escape_return");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();ctx->SetRef("###workspace");s.state.documents.Find(1)->sections[4].note.SetDraft("Background retains draft");s.state.documents.Find(1)->sections[4].selection.selected={6};
        ctx->ItemNavActivate(s.facts.status_button.id);ctx->Yield(2);IM_CHECK_EQ(s.state.drawer,sh::Drawer::Services);IM_CHECK(s.facts.drawer_visible);
        for(const char* label:{"Devices:","Proxy:","Updates:","Network:","Connection:","Backend height:","Last success:","Error:"})IM_CHECK(s.rendered.find(label)!=std::string::npos);
        s.observations.connection={a::ServiceState::Error,"Explicit fixture connection failure"};s.observations.backend_height=123;s.observations.last_success="2026-11-16 08:00 UTC";s.observations.error="Explicit fixture timeout";ctx->Yield(2);
        IM_CHECK(s.rendered.find("Connection: Error")!=std::string::npos);IM_CHECK(s.rendered.find("Backend height: 123")!=std::string::npos);IM_CHECK(s.state.presentation.Snapshot().find("Explicit fixture timeout")!=std::string::npos);
        ctx->KeyPress(ImGuiKey_Escape);ctx->Yield(2);IM_CHECK_EQ(s.state.drawer,sh::Drawer::None);IM_CHECK(s.facts.status_button.focused);
        ctx->ItemNavActivate(s.facts.activity_button.id);ctx->Yield(2);IM_CHECK_EQ(s.state.drawer,sh::Drawer::Activity);IM_CHECK(s.rendered.find("Operational log")!=std::string::npos);IM_CHECK_EQ(s.state.documents.Documents().size(),3);IM_CHECK_EQ(s.state.documents.Find(1)->sections[4].note.draft(),"Background retains draft");IM_CHECK(s.state.documents.Find(1)->sections[4].selection.selected.contains(6));
        s.observations.tasks[0].title="Sensitive 123.45678901";s.observations.tasks[0].detail="Sensitive 123.45678901";s.observations.log={"Sensitive 123.45678901"};s.state.privacy.hidden=true;ctx->Yield(2);
        IM_CHECK(s.rendered.find("123.45678901")==std::string::npos);IM_CHECK(s.state.presentation.Snapshot().find("123.45678901")==std::string::npos);IM_CHECK(s.rendered.find("Details hidden")!=std::string::npos);
        ctx->ItemNavActivate(s.facts.drawer_close.id);ctx->Yield(2);IM_CHECK_EQ(s.state.drawer,sh::Drawer::None);IM_CHECK(s.facts.status_button.focused);IM_CHECK(s.requests.empty());
    };
    t=Test(engine,"ctrl_tab_leaves_grid_and_does_not_steal_note_focus");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();ctx->SetRef("###workspace");ctx->Yield(2);ctx->ItemClick(s.facts.rows[0].id);ctx->Yield(2);
        const auto exits=s.state.grid_exits;ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_Tab);ctx->Yield(3);IM_CHECK_EQ(s.state.grid_exits,exits+1);IM_CHECK(s.facts.tabs.at(1).focused);IM_CHECK(ImGui::GetIO().NavVisible);IM_CHECK_EQ(s.state.documents.ActiveId(),1);
        ctx->ItemClick(s.facts.note.id);ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_Tab);ctx->Yield(2);IM_CHECK_EQ(s.state.grid_exits,exits+1);
    };
}
