// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/components/controls.h"
#include "ui/theme/theme.h"
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
using namespace roots::gui;
namespace c=roots::gui::ui::components;
namespace {
struct State final:c::Commands {
    std::vector<c::Command> commands;
    c::Edit amount{"0.01000000"},fee{"1.25"},label{"Original"};
    c::Selection selection;c::Dialog dialog;
    bool accept{true},enabled{true},filter{};int mode{};
    std::vector<c::Row> rows{{101,"First",Money::Satoshis(1)},{202,"Second",Money::Satoshis(2)},{303,"Disabled",Money::Satoshis(3),false}};
    std::vector<c::RowFacts> facts;c::ItemFacts opener;
    c::ItemFacts amount_receipt,fee_receipt,label_receipt;
    bool capture_rendered{};std::string rendered_text;
    bool Submit(const c::Command& command) override { commands.push_back(command);return accept; }
    template<typename T> int Count() const { return std::count_if(commands.begin(),commands.end(),[](const auto& x){return std::holds_alternative<T>(x);}); }
    int Actions(c::ActionKind kind) const { return std::count_if(commands.begin(),commands.end(),[&](const auto& x){const auto* a=std::get_if<c::Action>(&x);return a && a->kind==kind;}); }
};
void Draw(ImGuiTestContext* ctx)
{
    auto& s=ctx->GetVars<State>();const auto theme=ui::theme::Make(false);ui::theme::Scope scope(theme);
    ImGui::SetNextWindowPos({10,10},ImGuiCond_Always);ImGui::SetNextWindowSize({1000,740},ImGuiCond_Always);
    ImGui::Begin("Controls###controls-test",nullptr,ImGuiWindowFlags_NoSavedSettings);
    if (s.mode==0) {
        auto receipt=c::AmountField("amount",s.amount,s,theme,s.enabled);if (receipt.requested) s.amount_receipt=receipt;
        receipt=c::FeeField("fee",s.fee,s,theme,s.enabled);if (receipt.requested) s.fee_receipt=receipt;
        receipt=c::LabelEditor("label",s.label,s,theme,s.enabled);if (receipt.requested) s.label_receipt=receipt;
        c::CopyableIdentifier("copy","public-identifier-with-full-value-%s-000000000000000001",s,theme,s.enabled);
        c::ActionButton("primary","Primary",c::ActionKind::Primary,s,theme,s.enabled,"Host busy");
        ImGui::SameLine();c::ActionButton("secondary","Secondary",c::ActionKind::Secondary,s,theme,s.enabled);
    } else if (s.mode==1) {
        std::vector<c::Row> rows;for (auto row:s.rows) if (!s.filter || row.id==202) rows.push_back(std::move(row));
        auto& platform=ImGui::GetPlatformIO();
        const auto clipboard=platform.Platform_SetClipboardTextFn;void* const clipboard_user=platform.Platform_ClipboardUserData;
        if (s.capture_rendered) {
            platform.Platform_ClipboardUserData=&s;
            platform.Platform_SetClipboardTextFn=[](ImGuiContext*,const char* text) {
                static_cast<State*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->rendered_text=text;
            };
            ImGui::LogToClipboard();
        }
        s.facts=c::SelectionTable("rows",rows,s.selection,s,theme,240);
        if (s.capture_rendered) {
            ImGui::LogFinish();s.capture_rendered=false;
            platform.Platform_SetClipboardTextFn=clipboard;platform.Platform_ClipboardUserData=clipboard_user;
        }
    } else {
        s.opener=c::DialogLauncher("details","Review label",s.dialog,s,theme);
        c::DialogShell("Fixture review###dialog","Review a public fixture label; no signing or broadcast.",s.dialog,s,theme,s.enabled);
    }
    ImGui::End();
}
ImGuiTest* Test(ImGuiTestEngine* engine,const char* name)
{
    auto* test=IM_REGISTER_TEST(engine,"roots.components",name);test->SetVarsDataType<State>();test->GuiFunc=Draw;return test;
}
}
void RegisterComponentTests(ImGuiTestEngine* engine)
{
    auto* test=Test(engine,"exact_amount_fee_and_invalid_drafts");
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& s=ctx->GetVars<State>();ctx->SetRef("###controls-test");
        ctx->ItemInputValue("amount/###input","0.000000001");
        IM_CHECK_EQ(s.Count<c::CommitAmount>(),0);IM_CHECK_EQ(s.amount.saved,"0.01000000");IM_CHECK_EQ(s.amount.draft(),"0.000000001");IM_CHECK(!s.amount.error.empty());
        IM_CHECK(s.amount_receipt.requested && !s.amount_receipt.activated);
        ctx->ItemInputValue("amount/###input","0.00000001");
        IM_CHECK_EQ(s.Count<c::CommitAmount>(),1);IM_CHECK_EQ(std::get<c::CommitAmount>(s.commands.back()).value.sats(),1);
        IM_CHECK(s.amount_receipt.activated);
        ctx->ItemClick("amount/###input");ctx->KeyCharsReplace("0.00000002");ctx->ItemClick("fee/###input");
        IM_CHECK_EQ(s.Count<c::CommitAmount>(),2);IM_CHECK_EQ(std::get<c::CommitAmount>(s.commands.back()).value.sats(),2);
        ctx->ItemInputValue("fee/###input","1.00000001");
        IM_CHECK_EQ(s.Count<c::CommitFee>(),1);const auto fee=std::get<c::CommitFee>(s.commands.back()).value;
        IM_CHECK_EQ(fee.numerator(),100000001);IM_CHECK_EQ(fee.denominator(),100000000);
        ctx->ItemClick("fee/###input");ctx->KeyCharsReplace("2.125");ctx->ItemClick("label/###input");
        IM_CHECK_EQ(s.Count<c::CommitFee>(),2);IM_CHECK_EQ(std::get<c::CommitFee>(s.commands.back()).value.numerator(),17);IM_CHECK_EQ(std::get<c::CommitFee>(s.commands.back()).value.denominator(),8);
        ctx->ItemInputValue("amount/###input","21000000.00000001");ctx->ItemInputValue("fee/###input","1e3");
        IM_CHECK_EQ(s.Count<c::CommitAmount>(),2);IM_CHECK_EQ(s.Count<c::CommitFee>(),2);IM_CHECK_EQ(s.fee.draft(),"1e3");
    };
    test=Test(engine,"label_enter_focus_loss_and_rejected_persistence");
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& s=ctx->GetVars<State>();ctx->SetRef("###controls-test");
        ctx->ItemInputValue("label/###input","Renamed public label");IM_CHECK_EQ(s.Count<c::CommitLabel>(),1);IM_CHECK_EQ(s.label.saved,"Renamed public label");
        ctx->ItemClick("label/###input");ctx->KeyCharsReplace("Focus loss commit");ctx->ItemClick("amount/###input");
        IM_CHECK_EQ(s.Count<c::CommitLabel>(),2);IM_CHECK_EQ(s.label.saved,"Focus loss commit");
        s.accept=false;ctx->ItemInputValue("label/###input","Rejected draft");ctx->Yield(3);
        IM_CHECK_EQ(s.Count<c::CommitLabel>(),3);IM_CHECK_EQ(s.label.saved,"Focus loss commit");IM_CHECK_EQ(s.label.draft(),"Rejected draft");IM_CHECK(!s.label.error.empty());
        IM_CHECK(s.label_receipt.requested && !s.label_receipt.activated);
        ctx->ItemClick("amount/###input");ctx->Yield(2);IM_CHECK_EQ(s.Count<c::CommitLabel>(),3);
        s.accept=true;ctx->ItemInputValue("label/###input","Recovered");IM_CHECK_EQ(s.Count<c::CommitLabel>(),4);IM_CHECK_EQ(s.label.saved,"Recovered");IM_CHECK(s.label.error.empty());
        IM_CHECK(s.label_receipt.activated);
    };
    test=Test(engine,"copy_actions_and_disabled_semantics");
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& s=ctx->GetVars<State>();ctx->SetRef("###controls-test");ctx->ItemClick("copy/###copy");
        IM_CHECK_EQ(s.Count<c::CopyIdentifier>(),1);IM_CHECK_EQ(std::get<c::CopyIdentifier>(s.commands.back()).value,"public-identifier-with-full-value-%s-000000000000000001");
        ctx->ItemNavActivate("primary/Primary");ctx->ItemClick("secondary/Secondary");
        IM_CHECK_EQ(s.Actions(c::ActionKind::Primary),1);IM_CHECK_EQ(s.Actions(c::ActionKind::Secondary),1);
        s.enabled=false;ctx->Yield(2);ctx->ItemClick("copy/###copy");ctx->ItemClick("primary/Primary");ctx->KeyPress(ImGuiKey_Enter);
        IM_CHECK_EQ(s.Count<c::CopyIdentifier>(),1);IM_CHECK_EQ(s.Actions(c::ActionKind::Primary),1);
    };
    test=Test(engine,"stable_rows_rename_filter_and_focus_not_selection");
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& s=ctx->GetVars<State>();s.mode=1;ctx->Yield(2);ctx->SetRef("###controls-test");
        ctx->NavMoveTo("**/202/###select");ctx->Yield(2);IM_CHECK(s.selection.focused==202);IM_CHECK(s.selection.selected.empty());
        ctx->NavActivate();IM_CHECK(s.selection.selected.contains(202));IM_CHECK_EQ(s.Count<c::SelectRows>(),1);
        const auto widget=std::find_if(s.facts.begin(),s.facts.end(),[](const auto& f){return f.key==202;})->widget;
        s.rows[1].label="Renamed / public label %s";s.filter=true;ctx->Yield(2);
        IM_CHECK_EQ(s.facts.size(),1);IM_CHECK_EQ(s.facts[0].widget,widget);IM_CHECK(s.selection.selected.contains(202));
        s.filter=false;ctx->Yield(2);ctx->ItemClick("**/303/###select");IM_CHECK(!s.selection.selected.contains(303));IM_CHECK_EQ(s.Count<c::SelectRows>(),1);
        ctx->KeyDown(ImGuiMod_Ctrl);ctx->ItemClick("**/101/###select");ctx->KeyUp(ImGuiMod_Ctrl);
        IM_CHECK(s.selection.selected.contains(101));IM_CHECK(s.selection.selected.contains(202));
    };
    test=Test(engine,"dialog_rejection_acceptance_escape_and_return_focus");
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& s=ctx->GetVars<State>();s.mode=2;ctx->Yield(2);ctx->SetRef("###controls-test");ctx->ItemClick("details/Review label");
        ctx->SetRef("###dialog");s.accept=false;ctx->ItemClick("accept/Apply");IM_CHECK(s.dialog.state.open);IM_CHECK(!s.dialog.error.empty());IM_CHECK_EQ(s.Actions(c::ActionKind::AcceptDialog),1);
        s.accept=true;ctx->ItemNavActivate("accept/Apply");ctx->Yield(2);IM_CHECK(!s.dialog.state.open);IM_CHECK_EQ(s.Actions(c::ActionKind::AcceptDialog),2);
        IM_CHECK_EQ(s.Actions(c::ActionKind::Primary),0);
        ctx->SetRef("###controls-test");IM_CHECK(s.opener.focused);ctx->ItemClick("details/Review label");ctx->KeyPress(ImGuiKey_Escape);ctx->Yield(2);
        IM_CHECK(!s.dialog.state.open);IM_CHECK(s.opener.focused);IM_CHECK_EQ(s.Actions(c::ActionKind::CancelDialog),1);
    };
    test=Test(engine,"dialog_invalid_primary_and_cancel_without_effects");
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& s=ctx->GetVars<State>();s.mode=2;ctx->Yield(2);ctx->SetRef("###controls-test");ctx->ItemClick("details/Review label");s.enabled=false;ctx->Yield(2);
        ctx->SetRef("###dialog");ctx->ItemClick("accept/Apply");IM_CHECK_EQ(s.Actions(c::ActionKind::AcceptDialog),0);IM_CHECK(s.dialog.state.open);
        ctx->ItemNavActivate("cancel/Cancel");ctx->Yield(2);IM_CHECK(!s.dialog.state.open);IM_CHECK_EQ(s.Actions(c::ActionKind::CancelDialog),1);
    };
    test=Test(engine,"commands_validate_before_host_and_preserve_rejected_values");
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& s=ctx->GetVars<State>();
        s.amount.SetDraft("-0.00000001");IM_CHECK(!c::CommitAmountEdit("amount",s.amount,s));IM_CHECK_EQ(s.Count<c::CommitAmount>(),0);
        s.fee.SetDraft("0.000000001");IM_CHECK(!c::CommitFeeEdit("fee",s.fee,s));IM_CHECK_EQ(s.Count<c::CommitFee>(),0);
        s.label.SetDraft("invalid\nlabel");IM_CHECK(!c::CommitLabelEdit("label",s.label,s));IM_CHECK_EQ(s.Count<c::CommitLabel>(),0);
        IM_CHECK(!s.label.SetDraft(std::string(256,'x')));IM_CHECK_EQ(s.label.draft(),"invalid\nlabel");
        s.amount.SetDraft("21000000.00000000");s.accept=false;IM_CHECK(!c::CommitAmountEdit("amount",s.amount,s));IM_CHECK_EQ(s.amount.saved,"0.01000000");
        s.accept=true;IM_CHECK(c::CommitAmountEdit("amount",s.amount,s));IM_CHECK_EQ(std::get<c::CommitAmount>(s.commands.back()).value.sats(),2100000000000000LL);
    };
    test=Test(engine,"invalid_stable_row_keys_fail_closed");
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& s=ctx->GetVars<State>();s.mode=1;s.rows[1].id=s.rows[0].id;ctx->Yield(2);
        IM_CHECK(s.facts.empty());IM_CHECK(!s.selection.error.empty());IM_CHECK_EQ(s.Count<c::SelectRows>(),0);
        s.rows[1].id=0;ctx->Yield(2);IM_CHECK(s.facts.empty());IM_CHECK_EQ(s.Count<c::SelectRows>(),0);
        s.rows[1].id=202;ctx->Yield(2);IM_CHECK_EQ(s.facts.size(),3);IM_CHECK(s.selection.error.empty());IM_CHECK_EQ(s.Count<c::SelectRows>(),0);
    };
    test=Test(engine,"literal_hash_labels_render_through_rename_filter");
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& s=ctx->GetVars<State>();s.mode=1;
        s.rows[0].label="Invoice##2026";s.rows[1].label="Notes###Q1";
        s.rows[2].label="Disabled##row";s.rows[2].disabled_reason="Needs###review";
        const auto output=std::filesystem::path(ImGuiTestEngine_GetIO(ctx->Engine).ExportResultsFilename).replace_extension(".literal-labels.txt");
        std::ofstream(output,std::ios::trunc)<<"Production ImGui rendered-text log; private clipboard callback, no OS clipboard.\n";
        const auto capture=[&](const char* phase) {
            s.capture_rendered=true;ctx->Yield(2);
            std::ofstream(output,std::ios::app)<<phase<<'\n'<<s.rendered_text<<'\n';
        };
        capture("initial");
        IM_CHECK(s.rendered_text.find("Invoice##2026")!=std::string::npos);
        IM_CHECK(s.rendered_text.find("Notes###Q1")!=std::string::npos);
        IM_CHECK(s.rendered_text.find("Disabled##row (Needs###review)")!=std::string::npos);
        ctx->SetRef("###controls-test");ctx->ItemClick("**/202/###select");
        const auto widget=std::find_if(s.facts.begin(),s.facts.end(),[](const auto& row){return row.key==202;})->widget;
        s.rows[1].label="Renamed###2026 / literal##label";s.filter=true;capture("renamed and filtered");
        IM_CHECK(s.rendered_text.find(s.rows[1].label)!=std::string::npos);
        IM_CHECK_EQ(s.facts.size(),1);IM_CHECK_EQ(s.facts[0].widget,widget);IM_CHECK(s.selection.selected.contains(202));
        s.filter=false;capture("filter restored");
        IM_CHECK(s.rendered_text.find("Disabled##row (Needs###review)")!=std::string::npos);
        ctx->ItemClick("**/303/###select");IM_CHECK(!s.selection.selected.contains(303));IM_CHECK(s.selection.selected.contains(202));
    };
}
