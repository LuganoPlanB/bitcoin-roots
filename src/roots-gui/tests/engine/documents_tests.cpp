// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <algorithm>
#include <fstream>
#include <utility>
using namespace roots::gui;
namespace sh=ui::shell;namespace a=app;
namespace {
struct Fixture final:a::Requests {
    sh::State state;sh::Facts facts;std::vector<a::Request> requests;bool metadata_ack{true};int sequence{};
    struct Expected {uint64_t id,key;std::string name;};
    struct Publication {std::string operation,snapshot;std::vector<Expected> expected;bool captured{},done{},names_owned{true},values_owned{true},identity{true},rectangles{true};} publication;
    std::string publication_arm,watch_context;bool watch_rename{};
    void Capture(const std::string& operation) {
        publication=Publication();publication.operation=operation;publication.captured=true;size_t index{};
        for(const auto& node:state.presentation.Tree().nodes)if(node.role==imkit::accessibility::SemanticRole::Tab) {
            if(index>=facts.annotated_tabs.size()){publication.identity=false;continue;}
            const auto key=facts.annotated_tabs[index++];const auto* doc=state.documents.Find(key);
            if(!doc){publication.identity=false;continue;}
            publication.names_owned &= node.name.data()!=doc->name.data();
            publication.values_owned &= node.value.data()!=doc->name.data();
            publication.identity &= node.name==doc->name && node.value==doc->name;
            publication.expected.push_back({node.id,key,doc->name});
        }
        publication.identity &= index==facts.annotated_tabs.size() && index==3;
    }
    void Publish() {
        // A red lifetime probe never dereferences a known dangling name. It
        // records failed ownership before mutation; green must publish bytes.
        for(const auto& expected:publication.expected) {
            const auto* node=state.presentation.Tree().Find(expected.id);
            if(!node || !facts.tabs.contains(expected.key)){publication.identity=false;publication.rectangles=false;continue;}
            const auto& rect=facts.tabs.at(expected.key).rect;
            publication.rectangles &= node->minimum.x==rect[0] && node->minimum.y==rect[1] && node->maximum.x==rect[2] && node->maximum.y==rect[3];
            if(publication.names_owned && publication.values_owned)publication.identity &= node->name==expected.name && node->value==expected.name;
        }
        if(publication.names_owned && publication.values_owned)publication.snapshot=state.presentation.Snapshot();
        else publication.snapshot="UNSAFE_BORROWED_NAME: snapshot deliberately not dereferenced";
        publication.done=true;publication.captured=false;
        std::ofstream log("documents.publication.txt",std::ios::app);
        log<<publication.operation<<" namesOwned="<<publication.names_owned<<" valuesOwned="<<publication.values_owned<<" identity="<<publication.identity<<" rectangles="<<publication.rectangles<<" count="<<publication.expected.size()<<"\n"<<publication.snapshot<<"\n";
    }
    a::Availability CanSubmit(const a::Request&) const override {return {true,"Explicit effect-free fixture handler."};}
    a::Receipt Submit(const a::Request& r) override {
        requests.push_back(r);
        if(r.command==a::Command::NewWallet) {
            ++sequence;state.documents.OpenWallet("Explicit fixture wallet "+std::to_string(sequence),{*BackendSessionId::Make("fixture-daemon"),*WalletId::Make("fixture-wallet-"+std::to_string(sequence)),{*Hash256::Parse(std::string(64,'0'))}},true);return {true,{}};
        }
        if(r.command==a::Command::TransactionFile){state.documents.OpenTransaction("Explicit fixture transaction");return {true,{}};}
        if(r.command==a::Command::DocumentMetadata){if(metadata_ack && watch_rename){Capture("acknowledged-rename");watch_rename=false;}return {metadata_ack,metadata_ack?"":"Metadata acknowledgement failed; draft retained."};}
        if(r.command==a::Command::Component)return {true,{}};
        return {false,"Explicit fixture dependency unavailable; no durable effect."};
    }
};
void Draw(ImGuiTestContext* ctx){
    auto& s=ctx->GetVars<Fixture>();auto theme=ui::theme::Make(false);ui::theme::Scope scope(theme);
    std::map<uint64_t,std::pair<std::string,const char*>> before;
    if(!s.watch_context.empty())for(const auto& doc:s.state.documents.Documents())before[doc.id]={doc.name,doc.name.data()};
    s.facts=sh::DrawWorkspace(s.state,s,theme,{}, {1200,820});
    if(!s.watch_context.empty() && ((s.watch_context=="context-close" && s.state.documents.Documents().size()==2) ||
        (s.watch_context=="context-reorder" && s.state.documents.Documents().front().id==2))) {
        s.publication=Fixture::Publication();s.publication.operation=std::exchange(s.watch_context,{});s.publication.captured=true;size_t index{};
        std::set<uint64_t> unique;
        for(const auto& node:s.state.presentation.Tree().nodes)if(node.role==imkit::accessibility::SemanticRole::Tab) {
            if(index>=s.facts.annotated_tabs.size()){s.publication.identity=false;continue;}
            const auto key=s.facts.annotated_tabs[index++];const auto& original=before.at(key);
            s.publication.names_owned &= node.name.data()!=original.second;s.publication.values_owned &= node.value.data()!=original.second;
            s.publication.identity &= unique.insert(key).second;s.publication.expected.push_back({node.id,key,original.first});
        }
        s.publication.identity &= index==3;
    }
    if(!s.publication_arm.empty()) {
        const auto operation=std::exchange(s.publication_arm,{});s.Capture(operation);
        if(operation=="rename")s.state.documents.Rename(1,"New allocated replacement###"+std::string(190,'R'));
        if(operation=="close")s.state.documents.Close(std::array<uint64_t,2>{1,2});
        if(operation=="reorder"){s.state.documents.Move(1,1);s.state.documents.Move(3,-1);}
        // Pressure freed allocations before publication, never dereference a
        // borrowed name after mutation in the intentionally red candidate.
        const std::vector<std::string> pressure(16,std::string(230,'X'));s.Publish();
    } else if(s.publication.captured)s.Publish();
}
ImGuiTest* Test(ImGuiTestEngine* e,const char* name){auto* t=IM_REGISTER_TEST(e,"roots.documents",name);t->SetVarsDataType<Fixture>();t->GuiFunc=Draw;return t;}
void Context(ImGuiTestContext* ctx,Fixture& s,uint64_t id){ctx->SetRef("###workspace");ctx->ItemClick(s.facts.tabs.at(id).id,ImGuiMouseButton_Right);ctx->Yield(2);}
}
void RegisterDocumentTests(ImGuiTestEngine* engine)
{
    auto* t=Test(engine,"open_reorder_literal_rename_and_acknowledgement");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();ctx->SetRef("###workspace");ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_N);ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_N);ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_F);ctx->Yield(2);
        IM_CHECK_EQ(s.state.documents.Documents().size(),3);IM_CHECK_EQ(s.state.documents.Find(3)->kind,a::DocumentKind::Transaction);
        ctx->ItemClick(s.facts.tabs.at(1).id);ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_2);ctx->Yield(2);
        s.state.documents.Find(1)->Current().selection.selected={7};s.state.documents.Find(1)->Current().note.SetDraft("Retained draft");
        const auto widget=s.facts.tabs.at(1).id;ctx->ItemNavActivate(widget);ctx->KeyPress(ImGuiMod_Shift|ImGuiKey_F10);ctx->Yield(2);ctx->ItemNavActivate(s.facts.document_actions.at("Move Right").id);ctx->Yield(2);
        IM_CHECK_EQ(s.state.documents.Documents()[1].id,1);IM_CHECK_EQ(s.state.documents.ActiveId(),1);IM_CHECK_EQ(s.facts.tabs.at(1).id,widget);
        Context(ctx,s,1);ctx->ItemClick(s.facts.document_actions.at("Rename / Icon").id);ctx->Yield(2);
        ctx->ItemInputValue(s.facts.document_actions.at("name").id,"Gemeinschaftskasse##2026###literal");
        ctx->ItemClick(s.facts.document_actions.at("Diamond").id);s.metadata_ack=false;ctx->ItemClick(s.facts.document_actions.at("apply-name").id);ctx->Yield(2);
        IM_CHECK(s.facts.document_dialog_visible);IM_CHECK_EQ(s.state.documents.Find(1)->name,"Explicit fixture wallet 1");IM_CHECK_EQ(s.state.actions.name.draft(),"Gemeinschaftskasse##2026###literal");IM_CHECK(!ImGui::GetIO().NavVisible);
        IM_CHECK_EQ(s.state.documents.Find(1)->icon,0);IM_CHECK_EQ(s.state.actions.icon,2);
        s.metadata_ack=true;ctx->ItemNavActivate(s.facts.document_actions.at("apply-name").id);ctx->Yield(2);
        IM_CHECK_EQ(s.state.documents.Find(1)->name,"Gemeinschaftskasse##2026###literal");IM_CHECK_EQ(s.state.documents.Find(1)->icon,2);IM_CHECK_EQ(s.facts.tabs.at(1).id,widget);
        ctx->ItemClick(s.facts.tabs.at(2).id);ctx->ItemNavActivate(s.facts.tabs.at(1).id);ctx->Yield(2);
        IM_CHECK_EQ(s.state.documents.Find(1)->section,a::Section::Send);IM_CHECK(s.state.documents.Find(1)->Current().selection.selected.contains(7));IM_CHECK_EQ(s.state.documents.Find(1)->Current().note.draft(),"Retained draft");
        IM_CHECK(s.state.presentation.Snapshot().find("Gemeinschaftskasse##2026###literal")!=std::string::npos);
        Context(ctx,s,1);ctx->ItemClick(s.facts.document_actions.at("Reset Name / Icon").id);ctx->Yield(2);IM_CHECK_EQ(s.state.documents.Find(1)->name,"Explicit fixture wallet 1");IM_CHECK_EQ(s.state.documents.Find(1)->icon,0);
    };
    t=Test(engine,"dirty_close_cancel_failed_save_discard_and_focus_return");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();ctx->SetRef("###workspace");ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_N);ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_N);ctx->Yield(2);
        s.state.documents.Find(2)->sections[4].note.SetDraft("Unsaved UTXO note");s.state.documents.Find(2)->sections[4].selection.selected={9};
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_W);ctx->Yield(2);IM_CHECK(s.facts.document_dialog_visible);IM_CHECK_EQ(s.state.actions.close_ids.size(),1);
        ctx->ItemClick(s.facts.document_actions.at("save-close").id);ctx->Yield(2);IM_CHECK_EQ(s.state.documents.Documents().size(),2);IM_CHECK(!s.state.actions.error.empty());
        ctx->KeyPress(ImGuiKey_Escape);ctx->Yield(3);IM_CHECK(!s.facts.document_dialog_visible);IM_CHECK_EQ(s.state.documents.ActiveId(),2);IM_CHECK_EQ(s.state.documents.Find(2)->sections[4].note.draft(),"Unsaved UTXO note");IM_CHECK(s.state.documents.Find(2)->sections[4].selection.selected.contains(9));
        IM_CHECK(s.facts.tabs.at(2).focused);
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_W);ctx->Yield(2);ctx->ItemClick(s.facts.document_actions.at("discard-close").id);ctx->Yield(3);
        IM_CHECK(!s.state.documents.Find(2));IM_CHECK_EQ(s.state.documents.ActiveId(),1);
        IM_CHECK(std::none_of(s.requests.begin(),s.requests.end(),[](const auto& r){return r.command==a::Command::DeleteWallet || r.command==a::Command::LockCurrent;}));
    };
    t=Test(engine,"close_others_all_and_separate_delete_lock_window_dependencies");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();ctx->SetRef("###workspace");ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_N);ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_N);ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_F);ctx->Yield(2);
        Context(ctx,s,1);ctx->ItemClick(s.facts.document_actions.at("Delete Wallet...").id);ctx->Yield(2);
        ctx->ItemClick(s.facts.document_actions.at("confirm-delete").id);ctx->Yield(2);IM_CHECK_EQ(s.state.documents.Documents().size(),3);IM_CHECK_EQ(s.requests.back().command,a::Command::DeleteWallet);IM_CHECK_EQ(s.requests.back().document,1);
        ctx->ItemClick(s.facts.document_actions.at("cancel").id);ctx->Yield(2);
        Context(ctx,s,1);ctx->ItemClick(s.facts.document_actions.at("Lock Wallet").id);ctx->Yield(2);IM_CHECK_EQ(s.state.documents.Find(1)->observation,a::ObservationState::Offline);IM_CHECK_EQ(s.requests.back().command,a::Command::LockCurrent);
        Context(ctx,s,1);ctx->ItemClick(s.facts.document_actions.at("Separate Wallet Window...").id);ctx->Yield(2);IM_CHECK(s.state.notice.find("one context/thread owner")!=std::string::npos);IM_CHECK_EQ(s.state.documents.Documents().size(),3);
        Context(ctx,s,1);ctx->ItemClick(s.facts.document_actions.at("Close Others").id);ctx->Yield(2);IM_CHECK_EQ(s.state.documents.Documents().size(),1);IM_CHECK_EQ(s.state.documents.ActiveId(),1);
        Context(ctx,s,1);ctx->ItemClick(s.facts.document_actions.at("Close All").id);ctx->Yield(2);IM_CHECK(s.state.documents.Documents().empty());IM_CHECK(s.facts.primary.visible);IM_CHECK_EQ(s.facts.layout.accounts.w,0.F);
    };
    t=Test(engine,"empty_drop_import_ambiguity_and_rejected_dispatch");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();ctx->SetRef("###workspace");ctx->Yield(2);
        IM_CHECK(s.facts.drop_target.w>400 && s.facts.drop_target.h>=230);ctx->ItemNavActivate(s.facts.document_actions.at("open-import").id);ctx->Yield(2);
        IM_CHECK(s.facts.document_actions.at("import").disabled);ctx->ItemInputValue(s.facts.document_actions.at("source").id,"/public/Invoice##2026.json");ctx->Yield(2);
        IM_CHECK_EQ(s.state.actions.candidate.formats.size(),2);IM_CHECK(s.facts.document_actions.at("import").disabled);
        ctx->ItemClick(s.facts.document_actions.at("Public wallet JSON").id);ctx->ItemClick(s.facts.document_actions.at("import").id);ctx->Yield(2);
        IM_CHECK_EQ(s.requests.back().command,a::Command::ImportSource);IM_CHECK_EQ(s.requests.back().value,"/public/Invoice##2026.json");IM_CHECK_EQ(s.requests.back().option,int(a::ImportFormat::PublicWallet));IM_CHECK(s.state.documents.Documents().empty());IM_CHECK(!s.state.actions.error.empty());
        ctx->KeyPress(ImGuiKey_Escape);ctx->Yield(2);IM_CHECK(!s.facts.document_dialog_visible);
        IM_CHECK_EQ(a::ClassifyImportPath("PUBLIC.PSBT").formats.size(),1);IM_CHECK_EQ(a::ClassifyImportPath("unknown.bin").formats.size(),0);IM_CHECK(a::ClassifyImportPath(std::string("bad\0path.psbt",13)).source.empty());
    };
    t=Test(engine,"identity_negative_moves_close_targets_and_reopen");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();auto& r=s.state.documents;r=a::PublicFixtureDocuments();const auto binding=*r.Find(1)->binding;
        r.Find(1)->sections[0].selection.selected={2};r.Find(1)->sections[5].note.SetDraft("Protected draft");
        IM_CHECK(!r.Move(1,-1));IM_CHECK(!r.Move(999,1));IM_CHECK(!r.Move(1,2));IM_CHECK(!r.Rename(1,""));IM_CHECK(!r.SetIcon(1,3));
        IM_CHECK(!r.Close(std::array<uint64_t,2>{1,999}));IM_CHECK(!r.Close(std::array<uint64_t,2>{1,1}));IM_CHECK_EQ(r.Documents().size(),3);
        IM_CHECK(r.Move(1,1));IM_CHECK_EQ(r.OpenWallet("Ignored duplicate",binding,true),1);IM_CHECK_EQ(r.Documents().size(),3);IM_CHECK_EQ(r.Find(1)->sections[5].note.draft(),"Protected draft");
        sh::BeginClose(s.state,2,true);IM_CHECK_EQ(s.state.actions.kind,sh::DocumentDialog::Close);IM_CHECK_EQ(r.Documents().size(),3);IM_CHECK_EQ(s.state.actions.close_ids.size(),2);
        IM_CHECK(r.Find(1)->sections[0].selection.selected.contains(2));IM_CHECK_EQ(r.Find(1)->id,1);
    };
    for(const char* name:{"publication_rename","publication_close","publication_reorder"}) {
        t=Test(engine,name);
        t->TestFunc=[](ImGuiTestContext* ctx){
            auto& s=ctx->GetVars<Fixture>();s.state.documents=a::PublicFixtureDocuments();
            for(uint64_t id:{1,2,3})s.state.documents.Find(id)->name="Allocated literal##2026###"+std::to_string(id)+std::string(180,char('A'+id));
            ctx->Yield(2);s.publication_arm=std::string(ctx->Test->Name).substr(std::string("publication_").size());ctx->Yield(1);
            IM_CHECK(s.publication.done);IM_CHECK_EQ(s.publication.expected.size(),3);
            IM_CHECK(s.publication.names_owned);IM_CHECK(s.publication.values_owned);IM_CHECK(s.publication.rectangles);IM_CHECK(s.publication.identity);
            for(const auto& expected:s.publication.expected)IM_CHECK(s.publication.snapshot.find(expected.name+" | "+expected.name)!=std::string::npos);
        };
    }
    t=Test(engine,"publication_acknowledged_rename");
    t->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();s.state.documents=a::PublicFixtureDocuments();
        for(uint64_t id:{1,2,3})s.state.documents.Find(id)->name="Allocated initial##2026###"+std::to_string(id)+std::string(180,char('A'+id));
        ctx->Yield(2);Context(ctx,s,1);ctx->ItemClick(s.facts.document_actions.at("Rename / Icon").id);ctx->Yield(2);
        ctx->ItemInputValue(s.facts.document_actions.at("name").id,("Allocated replacement###"+std::string(180,'Z')).c_str());
        s.watch_rename=true;ctx->ItemClick(s.facts.document_actions.at("apply-name").id);ctx->Yield(1);
        IM_CHECK(s.publication.done);IM_CHECK_EQ(s.publication.operation,"acknowledged-rename");IM_CHECK(s.publication.names_owned);IM_CHECK(s.publication.values_owned);IM_CHECK(s.publication.rectangles);IM_CHECK(s.publication.identity);
        for(const auto& expected:s.publication.expected)IM_CHECK(s.publication.snapshot.find(expected.name+" | "+expected.name)!=std::string::npos);
        IM_CHECK(s.state.documents.Find(1)->name.starts_with("Allocated replacement###"));
    };

    for(const char* name:{"publication_context_close","publication_context_reorder"}) {
        t=Test(engine,name);t->TestFunc=[](ImGuiTestContext* ctx){
            auto& s=ctx->GetVars<Fixture>();s.state.documents=a::PublicFixtureDocuments();
            for(uint64_t id:{1,2,3})s.state.documents.Find(id)->name="Context allocated##2026###"+std::to_string(id)+std::string(180,char('A'+id));
            ctx->Yield(2);Context(ctx,s,1);
            const bool close=std::string_view(ctx->Test->Name).ends_with("close");s.watch_context=close?"context-close":"context-reorder";
            ctx->ItemClick(s.facts.document_actions.at(close?"Close":"Move Right").id);ctx->Yield(1);
            IM_CHECK(s.publication.done);IM_CHECK_EQ(s.publication.expected.size(),3);IM_CHECK(s.publication.names_owned);IM_CHECK(s.publication.values_owned);IM_CHECK(s.publication.rectangles);IM_CHECK(s.publication.identity);
            for(const auto& expected:s.publication.expected)IM_CHECK(s.publication.snapshot.find(expected.name+" | "+expected.name)!=std::string::npos);
        };
    }

}
