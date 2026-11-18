// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <algorithm>
#include <stdexcept>

using namespace roots::gui;
namespace {
namespace sh=ui::shell;
namespace c=ui::components;
using c::ax::SemanticAction;
using protocol::InspectionBytes;
using protocol::InspectionNode;
constexpr auto NETWORK=protocol::DisplayNetwork::Regtest;

std::string Large()
{
    std::string out="0200000001"+std::string(64,'1')+"000000000003004000fd0004";
    for (size_t i=0;i<1024;++i) out+=(i==1023 ? "0100000000000000" : "0000000000000000")+std::string("016a");
    return out+"0065cd1d"; // exactly 500,000,000
}
std::string Psbt()
{
    // Exact minimal BIP370 fields, zero final fields. Encode a deliberately
    // opaque key in the input map using the same fixture bytes as the model.
    const std::string hex="70736274ff01fb040200000001020402000000010401010105010100010e20"+std::string(64,'1')
        +"010f040000000001c901aa0001030801000000000000000104160014751e76e8199196d454941c45d1b3a323f1433bd600";
    std::string binary;
    for (size_t i=0;i<hex.size();i+=2) binary+=char(std::stoul(hex.substr(i,2),nullptr,16));
    return "  "+protocol::TransactionBase64(binary)+"\r\n";
}
struct Fixture final : app::Requests {
    sh::State state;
    sh::Facts facts;
    mutable TransactionDocuments documents;
    std::string copy;
    uint64_t sends{},large{},psbt{};
    Fixture()
    {
        const auto open=[&](const std::string& text) {
            const auto parsed=protocol::ParseTransactionDocument(text,NETWORK);
            if (!parsed.document) throw std::runtime_error(protocol::DocumentErrorMessage(parsed));
            return state.documents.OpenImportedDocument({parsed.document,NETWORK});
        };
        psbt=open(Psbt());
        large=open(Large());
    }
    TransactionDocuments* LocalDocuments() const override {return &documents;}
    app::Availability CanSubmit(const app::Request& request) const override
    {
        return {request.component && std::holds_alternative<c::CopyIdentifier>(*request.component),{}};
    }
    app::Receipt Submit(const app::Request& request) override
    {
        if (!CanSubmit(request).available) {++sends;return {false,"Unexpected external action"};}
        copy=std::get<c::CopyIdentifier>(*request.component).value;
        return {true,"Clipboard acknowledged"};
    }
};
void Draw(ImGuiTestContext* ctx)
{
    auto& owned=ctx->GetVars<std::unique_ptr<Fixture>>();
    if (!owned) owned=std::make_unique<Fixture>();
    const auto theme=ui::theme::Make(false);
    const ui::theme::Scope style(theme);
    owned->facts=sh::DrawWorkspace(owned->state,*owned,theme,{}, {1280,900});
}
bool Queue(Fixture& fixture,std::string_view title)
{
    auto& exchange=fixture.state.presentation.exchange;
    const auto packet=exchange.Published();
    if (!packet || !packet->valid) return false;
    const auto item=std::ranges::find_if(packet->nodes,[title](const auto& node){
        return node.name==title && !node.state.disabled && c::ax::Supports(node.actions,SemanticAction::Press);
    });
    return item!=packet->nodes.end() && exchange.QueueNext(packet->generation,item->id,SemanticAction::Press).second==c::ActionResult::Queued;
}
}
void RegisterWorkbenchTests(ImGuiTestEngine* engine)
{
    auto* test=IM_REGISTER_TEST(engine,"roots.workbench","large_tree_maps_exact_ranges_and_zero");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc=Draw;
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& f=*ctx->GetVars<std::unique_ptr<Fixture>>();
        auto& s=f.state;
        const auto snapshot=[&] {return s.presentation.Snapshot();};
        const auto action=[&](auto title) {if (!Queue(f,title)) return false;ctx->Yield(3);return true;};
        ctx->SetRef("###workspace");
        ctx->Yield(3);
        IM_CHECK(snapshot().find("1024 outputs")!=std::string::npos && snapshot().find("500000000 · Unix timestamp")!=std::string::npos);
        IM_CHECK(snapshot().find("Fee: Unknown")!=std::string::npos);
        IM_CHECK(action("Transaction outputs"));
        IM_CHECK(f.facts.transaction_document.contains("output-7") && !f.facts.transaction_document.contains("output-8"));
        IM_CHECK(action("Output 1"));
        IM_CHECK(snapshot().find("0.00000000 BTC · 0 sats")!=std::string::npos && snapshot().find("Spending history: Unknown")!=std::string::npos);
        IM_CHECK(action("Last page"));
        IM_CHECK(f.facts.transaction_document.contains("output-1023") && !f.facts.transaction_document.contains("output-0"));
        IM_CHECK(action("Output 1024"));
        IM_CHECK(s.imported_views.at(f.large).index==1023);
        IM_CHECK(action("Output amount"));
        auto& view=s.imported_views.at(f.large);
        IM_CHECK(view.hex && view.bytes==InspectionBytes::Wire && view.selection==(protocol::ByteSpan{10279,8}));
        IM_CHECK(snapshot().find("0100000000000000")!=std::string::npos);
        IM_CHECK(action("Copy selected bytes as hex"));
        IM_CHECK_EQ(f.copy,"0100000000000000");
        IM_CHECK(action("Original source"));
        IM_CHECK(view.bytes==InspectionBytes::Source && !view.selection);
        IM_CHECK(action("Output amount"));
        IM_CHECK(view.selection==(protocol::ByteSpan{20558,16}));
        IM_CHECK(action("Copy selected bytes as hex"));
        IM_CHECK_EQ(f.copy,"30313030303030303030303030303030");
        IM_CHECK_EQ(f.sends,0);
    };
    test=IM_REGISTER_TEST(engine,"roots.workbench","stale_representation_and_hidden_actions_retire");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc=Draw;
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& f=*ctx->GetVars<std::unique_ptr<Fixture>>();
        auto& s=f.state;
        s.documents.Activate(f.psbt);
        ctx->SetRef("###workspace");
        ctx->Yield(3);
        const auto action=[&](auto title){if (!Queue(f,title)) return false;ctx->Yield(3);return true;};
        const auto snapshot=[&] {return s.presentation.Snapshot();};
        IM_CHECK(action("Input 1"));
        IM_CHECK(snapshot().find("Previous amount and script: Unknown")!=std::string::npos);
        IM_CHECK(action("Show wire hex"));
        IM_CHECK(action("Original source"));
        IM_CHECK(snapshot().find("Base64 source has no direct byte offsets")!=std::string::npos);
        IM_CHECK(!f.facts.transaction_document.contains("field-0"));
        IM_CHECK(action("Derived unsigned"));
        auto& view=s.imported_views.at(f.psbt);
        IM_CHECK(view.bytes==InspectionBytes::Unsigned && !view.selection);
        IM_CHECK(action("Sequence"));
        IM_CHECK(view.selection==(protocol::ByteSpan{42,4}));
        IM_CHECK(action("Save selected bytes"));
        IM_CHECK(s.local_document.dialog.open && s.local_document.inspection);
        const auto original=*s.local_document.inspection;
        IM_CHECK(sh::ValidInspectionExport(s,f,original));
        ++s.documents.Active()->draft_revision.value;
        ctx->Yield(3);
        IM_CHECK(!s.local_document.dialog.open && !sh::ValidInspectionExport(s,f,original));
        IM_CHECK(action("Show wire hex"));
        IM_CHECK(Queue(f,"Copy representation as hex"));
        s.documents.Activate(f.large); // supersede the packet before delivery
        ctx->Yield(3);
        IM_CHECK(f.copy.empty());
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiKey_H);
        ctx->Yield(2);
        IM_CHECK(s.privacy.hidden && f.facts.transaction_document.empty());
        IM_CHECK(snapshot().find("Transaction overview")==std::string::npos && snapshot().find("Copy transaction ID")==std::string::npos);
        IM_CHECK_EQ(f.sends,0);
    };
}
