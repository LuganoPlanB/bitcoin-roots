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
namespace c=ui::components;
namespace {
struct State final:c::Commands {
    c::AmountPrivacy privacy;c::PresentationFrame frame;
    c::Edit amount{"1.23456789"},fee{"3.14159265"};
    std::vector<c::Row> rows{{101,"First##literal",Money::Satoshis(123456789)},
        {202,"Frozen###literal",Money::Satoshis(222222222),false,"Frozen by host"},
        {303,"Unavailable",Money::Satoshis(333333333),false,"Observation incomplete"}};
    c::Selection selection;c::ItemFacts copy;
    std::vector<c::Command> commands;
    c::FreshnessStatus status{Freshness::Stale,true,false,"Fixture failure retained","2026-11-15 12:00 UTC"};
    bool capture{},review{},enabled{true};std::string rendered,semantic;
    bool Submit(const c::Command& cmd) override { commands.push_back(cmd);return true; }
};
void Draw(ImGuiTestContext* ctx)
{
    auto& s=ctx->GetVars<State>();const auto theme=ui::theme::Make(false);ui::theme::Scope scope(theme);s.frame.Begin();
    ImGui::SetNextWindowPos({10,10},ImGuiCond_Always);ImGui::SetNextWindowSize({1100,900},ImGuiCond_Always);
    ImGui::Begin("Privacy###privacy-test",nullptr,ImGuiWindowFlags_NoSavedSettings);
    auto& p=ImGui::GetPlatformIO();const auto callback=p.Platform_SetClipboardTextFn;void* const user=p.Platform_ClipboardUserData;
    if (s.capture) {
        p.Platform_ClipboardUserData=&s;p.Platform_SetClipboardTextFn=[](ImGuiContext*,const char* text) {
            static_cast<State*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->rendered=text;
        };ImGui::LogToClipboard();
    }
    ImGui::Checkbox("Hide amounts###hide",&s.privacy.hidden);
    if (s.review) c::ReviewDetails("review",Money::Satoshis(123456789),"bcrt1qfull-destination###literal-%s-000000000000000000000001",s.privacy,&s.frame);
    else {
        c::StatusBanner("status",s.status,theme,&s.frame);
        c::AmountField("amount",s.amount,s,theme,true,s.privacy,&s.frame);
        c::FeeField("fee",s.fee,s,theme,true,s.privacy,&s.frame);
        c::AmountSummary("summary",s.rows,theme,s.privacy,&s.frame);
        s.copy=c::CopyableAmount("amount-copy",s.rows[0].amount,s,theme,s.privacy,&s.frame,s.enabled);
        c::SelectionTable("rows",s.rows,s.selection,s,theme,130,s.privacy,&s.frame);
    }
    s.semantic=s.frame.Snapshot();
    if (s.capture) { ImGui::LogFinish();s.capture=false;p.Platform_SetClipboardTextFn=callback;p.Platform_ClipboardUserData=user; }
    ImGui::End();
}
void Capture(ImGuiTestContext* ctx,const char* phase)
{
    auto& s=ctx->GetVars<State>();s.capture=true;ctx->Yield(2);
    const auto path=std::filesystem::path(ImGuiTestEngine_GetIO(ctx->Engine).ExportResultsFilename).replace_extension(".presentation.txt");
    std::ofstream(path,std::ios::app)<<phase<<"\nRENDERED\n"<<s.rendered<<"\nANNOTATIONS\n"<<s.semantic<<'\n';
}
bool ContainsSensitive(std::string_view text)
{
    for (const char* value:{"1.23456789","123456789","2.22222222","222222222","3.33333333","333333333","6.79012344","679012344","3.14159265"})
        if (text.find(value)!=std::string_view::npos) return true;
    return false;
}
ImGuiTest* Test(ImGuiTestEngine* e,const char* name)
{
    auto* t=IM_REGISTER_TEST(e,"roots.privacy",name);t->SetVarsDataType<State>();t->GuiFunc=Draw;return t;
}
}
void RegisterPrivacyTests(ImGuiTestEngine* engine)
{
    auto* test=Test(engine,"projection_redacts_every_amount_channel");
    test->TestFunc=[](ImGuiTestContext* ctx) {
        const auto output=std::filesystem::path(ImGuiTestEngine_GetIO(ctx->Engine).ExportResultsFilename).replace_extension(".presentation.txt");
        std::ofstream(output,std::ios::trunc)<<"Production rendered-text and owned amount-annotation snapshots; private in-memory clipboard callback.\n";
        const c::AmountPrivacy hidden{true},visible{};const auto value=Money::Satoshis(123456789);
        const auto redacted=hidden.Present(value);IM_CHECK_EQ(redacted.text,"Hidden");IM_CHECK_EQ(redacted.tooltip,"Hidden");IM_CHECK_EQ(redacted.semantic,"Hidden");IM_CHECK(!redacted.copy);
        IM_CHECK_EQ(hidden.Draft("invalid 1.23456789").text,"Hidden");IM_CHECK_EQ(hidden.Present(std::nullopt).text,"Hidden");
        IM_CHECK_EQ(visible.Present(value).copy.value(),"1.23456789");IM_CHECK_EQ(visible.Present(std::nullopt).text,"Unavailable");
        const std::array values{value,Money::Satoshis(222222222)};
        IM_CHECK(hidden.Chart(values).percentages.empty());IM_CHECK(!ContainsSensitive(hidden.Chart(values).alternative));
        IM_CHECK_EQ(visible.Chart(values).percentages.size(),2);IM_CHECK(ContainsSensitive(visible.Chart(values).alternative));
        const std::array overflow{Money::Satoshis(INT64_MAX),Money::Satoshis(1)};
        IM_CHECK(visible.Chart(overflow).percentages.empty());IM_CHECK_EQ(value.sats(),123456789);
        const auto review= c::DeliberateReview(value,"full###destination",hidden);
        IM_CHECK_EQ(review.amount.text,"Hidden");IM_CHECK_EQ(review.amount.semantic,"Hidden");IM_CHECK(!review.amount.copy);IM_CHECK(review.reveal_required);IM_CHECK_EQ(review.destination,"full###destination");
    };
    test=Test(engine,"hide_show_native_widgets_tooltips_copy_and_annotations");
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& s=ctx->GetVars<State>();ctx->SetRef("###privacy-test");Capture(ctx,"visible positive leak detector control");
        IM_CHECK(ContainsSensitive(s.rendered));IM_CHECK(ContainsSensitive(s.semantic));IM_CHECK(s.frame.Tree().Validate());
        ctx->ItemClick("###hide");Capture(ctx,"hidden");
        IM_CHECK(!ContainsSensitive(s.rendered));IM_CHECK(!ContainsSensitive(s.semantic));IM_CHECK(s.copy.disabled);IM_CHECK(s.frame.Tree().Validate());IM_CHECK(s.frame.ChartSamples().empty());
        IM_CHECK(s.rendered.find("Frozen###literal (Frozen by host)")!=std::string::npos);
        const auto tree=s.frame.Tree();const auto total=std::find_if(tree.nodes.begin(),tree.nodes.end(),[](const auto& n){return n.name=="Observed total (BTC)";});IM_CHECK(total!=tree.nodes.end());
        ctx->MouseMoveToPos({(total->minimum.x+total->maximum.x)/2,(total->minimum.y+total->maximum.y)/2});ctx->Yield(3);Capture(ctx,"hidden actual hovered total tooltip");
        IM_CHECK(!ContainsSensitive(s.rendered));IM_CHECK(!ContainsSensitive(s.semantic));
        ctx->ItemClick("amount-copy/###copy");IM_CHECK(s.commands.empty());
        ctx->ItemClick("###hide");s.enabled=false;ctx->Yield();ctx->ItemClick("amount-copy/###copy");IM_CHECK(s.commands.empty());
        s.enabled=true;ctx->Yield();ctx->ItemClick("amount-copy/###copy");
        IM_CHECK_EQ(s.commands.size(),1);IM_CHECK_EQ(std::get<c::CopyAmount>(s.commands.back()).value.sats(),123456789);
        ctx->ItemInputValue("amount/###input","0.00000001");IM_CHECK_EQ(std::get<c::CommitAmount>(s.commands.back()).value.sats(),1);
    };
    test=Test(engine,"hide_active_draft_without_commit_or_loss");
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& s=ctx->GetVars<State>();ctx->SetRef("###privacy-test");ctx->ItemClick("amount/###input");ctx->KeyCharsReplace("1.234567891");
        ctx->ItemClick("###hide");Capture(ctx,"hidden active invalid draft");
        IM_CHECK(s.commands.empty());IM_CHECK_EQ(s.amount.draft(),"1.234567891");IM_CHECK_EQ(s.amount.saved,"1.23456789");IM_CHECK(!ContainsSensitive(s.rendered));IM_CHECK(!ContainsSensitive(s.semantic));
        ctx->ItemClick("###hide");IM_CHECK_EQ(s.amount.draft(),"1.234567891");IM_CHECK(s.commands.empty());
    };
    test=Test(engine,"review_hides_amount_until_explicit_reveal_keeps_full_destination");
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& s=ctx->GetVars<State>();s.review=true;s.privacy.hidden=true;ctx->SetRef("###privacy-test");Capture(ctx,"review hidden before explicit reveal");
        IM_CHECK(!ContainsSensitive(s.rendered));IM_CHECK(!ContainsSensitive(s.semantic));
        IM_CHECK(s.rendered.find("Turn off Hide amounts")!=std::string::npos);
        IM_CHECK(s.rendered.find("bcrt1qfull-destination###literal-%s-000000000000000000000001")!=std::string::npos);
        IM_CHECK(s.semantic.find("bcrt1qfull-destination###literal-%s-000000000000000000000001")!=std::string::npos);
        const auto amount_node=s.frame.Tree().nodes[0].id;
        ctx->ItemClick("###hide");Capture(ctx,"review after explicit user show-amounts choice");IM_CHECK(!s.privacy.hidden);
        IM_CHECK(s.rendered.find("1.23456789")!=std::string::npos);IM_CHECK(s.semantic.find("1.23456789")!=std::string::npos);
        IM_CHECK(s.rendered.find("bcrt1qfull-destination###literal-%s-000000000000000000000001")!=std::string::npos);IM_CHECK(s.frame.Tree().Validate());IM_CHECK(s.commands.empty());
        IM_CHECK_EQ(s.frame.Tree().nodes[0].id,amount_node);
        ctx->ItemClick("###hide");Capture(ctx,"review hidden again");IM_CHECK(s.privacy.hidden);
        IM_CHECK(!ContainsSensitive(s.rendered));IM_CHECK(!ContainsSensitive(s.semantic));IM_CHECK(s.commands.empty());
    };
    test=Test(engine,"motion_interrupt_reduce_and_identity_budget");
    test->TestFunc=[](ImGuiTestContext* ctx) {
        c::ViewPolish polish;polish.Begin();IM_CHECK(polish.ClaimIdentityAccent());IM_CHECK(!polish.ClaimIdentityAccent());
        const auto id=ImGuiID(123);IM_CHECK_EQ(polish.PrivacyFeedback(id,false,false,1.F/60,1),0.F);
        const auto moving=polish.PrivacyFeedback(id,true,false,1.F/60,2);IM_CHECK(moving>0 && moving<1);
        IM_CHECK_EQ(polish.PrivacyFeedback(id,true,true,1.F/60,3),1.F);
        IM_CHECK_EQ(polish.PrivacyFeedback(id,false,true,1.F/60,4),0.F);
        const auto restart=polish.PrivacyFeedback(id,true,false,1.F/60,5);IM_CHECK(restart>0 && restart<1);
        IM_CHECK_EQ(polish.PrivacyFeedback(id,true,false,.2F,6),1.F);polish.Begin();IM_CHECK(polish.ClaimIdentityAccent());
    };
}
