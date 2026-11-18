// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"

#include <algorithm>
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_engine.h>

using namespace roots::gui;
namespace {
namespace shell=ui::shell;
namespace c=ui::components;
constexpr auto GENESIS="000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";
constexpr auto NETWORK=protocol::DisplayNetwork::Mainnet;
constexpr auto CHANGE="wpkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)";
constexpr auto ALTERNATE="sh(wpkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798))";
std::optional<FundingChange> PublicFixtureChange(std::string_view desc)
{
    const auto parsed=protocol::PreviewPublicWallet(protocol::PublicImportKind::Descriptor,desc,"main");
    return parsed.preview ? DeriveFundingChange(parsed.preview->descriptors.front().descriptor,0,NETWORK) : std::nullopt;
}

struct Fixture final : app::Requests {
    shell::State state;
    shell::Facts facts;
    uint64_t wallet{};
    std::optional<FundingScope> scope;
    bool current{true};
    bool replace_before_dispatch{};
    std::vector<c::CopyIdentifier> copies;

    Fixture()
    {
        const auto network=NetworkIdentity{*Hash256::Parse(GENESIS)};
        const auto binding=app::WalletBinding{*BackendSessionId::Make("fixture-backend"),*WalletId::Make("fixture"),network,{}};
        wallet=state.documents.OpenWallet("Fixture wallet",binding,true);
        auto* draft=state.documents.Active();
        draft->history.enabled=true;
        draft->section=app::Section::Send;
        draft->draft_revision={7};
        ServerCandidate server;
        server.expected_genesis=GENESIS;
        server.wallet="fixture";
        scope=FundingScope{{{*BackendSessionId::Make("connection-1"),network,binding.wallet,
            *DocumentId::Make("wallet-document-1"),{3},{9}},server,1},
            wallet,1,2,3,4,draft->draft_revision,std::string(64,'b'),2125,0};
        SetPreview(2,1);
    }
    void SetPreview(size_t inputs,size_t outputs)
    {
        state.funding.scope=scope;
        state.funding.scope->draft=state.documents.Active()->draft_revision;
        auto tx=std::make_shared<protocol::TransactionDocument>();
        tx->transaction=protocol::TransactionInspection{};
        auto& decoded=*tx->transaction;
        decoded.version=2;
        decoded.locktime=0;
        const auto sat=[](int64_t value){return Money::Satoshis(value);};
        const auto script=[](size_t value){return "0014"+std::string(40,"abcdef0123456789"[value]);};
        FundingPreview value{tx,{},PublicFixtureChange(CHANGE),sat(int64_t(inputs)*800000),sat(567)};
        if (!value.change) throw std::runtime_error("Fixture internal descriptor");
        for (size_t n=0;n<inputs;++n) {
            const auto point=Outpoint{*Hash256::Parse(std::string(64,"123456789abcdef0"[n])),uint32_t(n)};
            const auto amount=sat(800000);
            decoded.inputs.push_back({point,0xfffffffdU,amount,script(n),protocol::PrevoutEvidence::TxidMatched});
            value.inputs.push_back({point,amount,"fixture-public-coin-proof"});
        }
        for (size_t n=0;n<outputs;++n) decoded.outputs.push_back({sat(10000),script(n)});
        decoded.outputs.push_back({sat(value.total_inputs.sats()-int64_t(outputs)*10000-value.fee.sats()),value.change->script});
        decoded.fee=value.fee;
        const auto other=PublicFixtureChange(ALTERNATE);
        if (!other) throw std::runtime_error("Fixture alternative descriptor");
        value.change_choices={*value.change,*other};
        value.flow=DescribeFunding(value,NETWORK);
        if (!value.flow) throw std::runtime_error("Fixture public flow");
        state.funding.preview=std::make_shared<const FundingPreview>(std::move(value));
        state.funding.flow={.open=true};
    }
    app::Availability CanSubmit(const app::Request& request) const override
    {
        return {request.component && std::holds_alternative<c::CopyIdentifier>(*request.component),{}};
    }
    app::Receipt Submit(const app::Request& request) override
    {
        if (!CanSubmit(request).available) throw std::runtime_error("Unexpected host action");
        copies.push_back(std::get<c::CopyIdentifier>(*request.component));
        return {true,"Fixture clipboard acknowledged"};
    }
};

void Draw(ImGuiTestContext* ctx)
{
    auto& fixture=ctx->GetVars<std::unique_ptr<Fixture>>();
    if (!fixture) fixture=std::make_unique<Fixture>();
    auto& s=fixture->state;
    const auto theme=ui::theme::Make(false);
    const ui::theme::Scope style(theme);
    fixture->facts={};
    s.presentation.Begin(shell::SemanticScope(s),[&s]{return shell::SemanticScope(s);});
    ImGui::SetNextWindowSize({1000,750},ImGuiCond_Always);
    ImGui::Begin("Transaction flow fixture");
    shell::DrawTransactionFlow(s,*fixture,fixture->facts,theme,[&fixture]{return fixture->current;});
    ImGui::End();
    if (std::exchange(fixture->replace_before_dispatch,false)) {
        s.funding.preview=std::make_shared<const FundingPreview>(*s.funding.preview);
    }
    s.presentation.Seal();
}
void Click(ImGuiTestContext* ctx,Fixture& fixture,const char* key)
{
    ctx->ItemClick(fixture.facts.flow.at(key).id);
    ctx->Yield(2);
}
uint64_t QueueCopy(Fixture& fixture)
{
    const auto packet=fixture.state.presentation.exchange.Published();
    if (!packet || !packet->valid) throw std::runtime_error("Current flow packet");
    const auto copy=std::ranges::find_if(packet->nodes,[](const auto& node){return node.name=="Copy outpoint";});
    if (copy==packet->nodes.end()) throw std::runtime_error("Current scoped copy");
    const auto result=fixture.state.presentation.exchange.QueueNext(packet->generation,copy->id,c::ax::SemanticAction::Press);
    if (result.second!=c::ActionResult::Queued) throw std::runtime_error("Queued flow copy");
    return result.first;
}
bool Retired(const Fixture& fixture,uint64_t sequence)
{
    const auto receipts=fixture.state.presentation.exchange.Receipts();
    return std::ranges::any_of(receipts,[&](const auto& row){
        return row.sequence==sequence && row.result!=c::ActionResult::Applied && row.result!=c::ActionResult::Queued;
    });
}
}

void RegisterFlowTests(ImGuiTestEngine* engine)
{
    auto* test=IM_REGISTER_TEST(engine,"roots.flow","diagram_table_selection_and_exact_values");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc=Draw;
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& f=*ctx->GetVars<std::unique_ptr<Fixture>>();
        ctx->SetRef("Transaction flow fixture");
        const auto held=f.state.funding.preview;
        const FlowKey input{true,1}, payment{false,0};
        IM_CHECK(held->flow->total_inputs==held->flow->total_recipients.Add(held->flow->total_change)->Add(held->flow->fee));
        Click(ctx,f,"diagram-input-1");
        IM_CHECK(f.state.funding.flow.selected==input);
        IM_CHECK(f.state.presentation.Snapshot().find(held->flow->inputs[1].identifier)!=std::string::npos);
        Click(ctx,f,"copy");
        IM_CHECK_EQ(f.copies.size(),1);
        IM_CHECK_EQ(f.copies[0].value,held->flow->inputs[1].identifier);
        IM_CHECK(f.copies[0].sensitive && f.copies[0].id=="flow-outpoint");
        Click(ctx,f,"table");
        IM_CHECK(f.state.funding.flow.selected==input && f.facts.flow.contains("table-input-1"));
        ctx->ItemNavActivate(f.facts.flow.at("table-output-0").id);
        ctx->Yield(2);
        IM_CHECK(f.state.funding.flow.selected==payment);
        IM_CHECK(f.facts.flow.at("qr").disabled);
        Click(ctx,f,"table");
        IM_CHECK(f.state.funding.flow.selected==payment && f.state.funding.preview==held);
        IM_CHECK(f.state.presentation.Snapshot().find(held->flow->outputs[0].identifier)!=std::string::npos);
    };
    test=IM_REGISTER_TEST(engine,"roots.flow","group_navigation_and_deliberate_edits");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc=Draw;
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& f=*ctx->GetVars<std::unique_ptr<Fixture>>();
        ctx->SetRef("Transaction flow fixture");
        f.SetPreview(9,8);
        ctx->Yield(2);
        IM_CHECK(!f.facts.flow.contains("diagram-input-4"));
        Click(ctx,f,"input-next");
        Click(ctx,f,"diagram-input-4");
        const FlowKey input{true,4};
        IM_CHECK(f.state.funding.flow.selected==input);
        Click(ctx,f,"expand");
        IM_CHECK(f.state.funding.flow.selected==input && f.facts.flow.contains("diagram-input-7"));
        auto* draft=f.state.documents.Active();
        const auto point=*f.state.funding.preview->flow->inputs[4].outpoint;
        draft->selected_coins={point};
        ctx->Yield(2);
        IM_CHECK(f.facts.flow.at("exclude").disabled);
        draft->selected_coins.push_back(*f.state.funding.preview->flow->inputs[3].outpoint);
        ctx->Yield(2);
        const auto revision=draft->draft_revision.value;
        Click(ctx,f,"exclude");
        IM_CHECK(!f.state.funding.preview && !f.state.funding.scope);
        IM_CHECK(draft->excluded_coins.size()==1 && draft->excluded_coins.front()==point);
        IM_CHECK_EQ(draft->selected_coins.size(),1);
        IM_CHECK_EQ(draft->draft_revision.value,revision+1);
        f.SetPreview(2,1);
        ctx->Yield(2);
        Click(ctx,f,"diagram-output-1");
        Click(ctx,f,"change-choices");
        Click(ctx,f,"change-next");
        const auto choice=f.state.funding.preview->change_choices[1];
        const auto before=draft->draft_revision.value;
        Click(ctx,f,"change-use");
        IM_CHECK(draft->funding_change==choice && !f.state.funding.preview && !f.state.funding.scope);
        IM_CHECK_EQ(draft->draft_revision.value,before+1);
        IM_CHECK(f.copies.empty());
    };
    test=IM_REGISTER_TEST(engine,"roots.flow","stale_revision_hidden_and_replaced_snapshot");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc=Draw;
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& f=*ctx->GetVars<std::unique_ptr<Fixture>>();
        ctx->SetRef("Transaction flow fixture");
        Click(ctx,f,"diagram-input-0");
        auto sequence=QueueCopy(f);
        ++f.state.documents.Active()->draft_revision.value;
        ctx->Yield(2);
        IM_CHECK(f.facts.flow.empty() && f.copies.empty() && Retired(f,sequence));
        f.SetPreview(2,1);
        ctx->Yield(2);
        Click(ctx,f,"diagram-input-0");
        sequence=QueueCopy(f);
        f.state.privacy.hidden=true;
        ctx->Yield(2);
        IM_CHECK(f.facts.flow.empty() && f.copies.empty() && Retired(f,sequence));
        IM_CHECK(f.state.presentation.Snapshot().find(f.state.funding.preview->flow->inputs[0].identifier)==std::string::npos);
        f.state.privacy.hidden=false;
        ctx->Yield(2);
        sequence=QueueCopy(f);
        f.replace_before_dispatch=true;
        ctx->Yield(2);
        IM_CHECK(f.copies.empty() && Retired(f,sequence));
        sequence=QueueCopy(f);
        f.current=false;
        ctx->Yield(2);
        IM_CHECK(f.facts.flow.empty() && f.copies.empty() && Retired(f,sequence));
    };
}
