// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/localization.h"
#include <limits>
#include "ui/shell/workspace.h"
#include "ui/components/gallery.h"
#include "ui/theme/theme.h"
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_context.h>
#include <algorithm>
#include <thread>
#include <fstream>
using namespace roots::gui;
namespace c=ui::components;namespace sh=ui::shell;namespace a=app;
namespace {
struct Fixture final:a::Requests,c::Commands {
    sh::State shell;sh::Facts facts;a::HostObservations observations;c::Gallery gallery;c::GalleryFacts controls;
    bool save_available{true},lose_save_availability{};bool primary_available{true},reject_amount{},unavailable_after_amount{};bool component{},overflow_controls{},early_button{},add_text{},huge_text{},ack{true};std::string long_text;c::PresentationFrame capacity_frame;c::ItemFacts capacity_button;std::vector<a::Request> requests;std::vector<c::Command> commands;uint64_t seq{};std::vector<uint64_t> cancel_on_owner;std::vector<bool> cancel_results;
    Fixture(){shell.documents=a::PublicFixtureDocuments();}
    a::HostObservations Observe() const override{return observations;}
    a::Availability CanSubmit(const a::Request& r) const override{if(r.command==a::Command::SaveDraft && !save_available)return {false,"Save service became unavailable after earlier acknowledgement."};return {true,"Explicit effect-free semantic fixture."};}
    a::Receipt Submit(const a::Request& r) override{requests.push_back(r);if(r.command==a::Command::SaveDraft && lose_save_availability)save_available=false;return {ack,ack?"":"Explicit fixture rejection."};}
    bool Available(const c::Command& r) const override {const auto* action=std::get_if<c::Action>(&r);return !action || action->kind!=c::ActionKind::Primary || primary_available;}
    bool Submit(const c::Command& r) override{commands.push_back(r);if(std::holds_alternative<c::CommitAmount>(r)){if(unavailable_after_amount)primary_available=false;if(reject_amount)return false;}return ack;}
    c::PresentationFrame& Frame(){return overflow_controls?capacity_frame:component?gallery.presentation:shell.presentation;}
    std::shared_ptr<const c::SemanticPacket> Packet(){return Frame().exchange.Published();}
    const c::SemanticEntry* Named(std::string_view name){const auto p=Packet();if(!p)return nullptr;for(const auto& n:p->nodes)if(n.name==name)return &n;return nullptr;}
    c::ActionResult Queue(c::ax::StableId id,c::ax::SemanticAction action,std::string value={}){auto p=Packet();return Frame().exchange.Queue({++seq,p?p->generation:0,id,action,std::move(value)});}
};
void Draw(ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();for(auto seq:s.cancel_on_owner)s.cancel_results.push_back(s.Frame().exchange.CancelQueued(seq));s.cancel_on_owner.clear();const auto theme=ui::theme::Make(false);ui::theme::Scope scope(theme);if(s.overflow_controls){s.capacity_frame.Begin("bounded-shared-control");ImGui::SetNextWindowPos({10,10},ImGuiCond_Always);ImGui::SetNextWindowSize({700,400},ImGuiCond_Always);ImGui::Begin("Capacity publication###semantic-capacity");if(s.early_button)s.capacity_button=c::ActionButton("capacity-action","Inspect fixture",c::ActionKind::Primary,s,theme,true,"",&s.capacity_frame);ImGui::BeginChild("text",{0,160});if(s.huge_text){ImGui::TextUnformatted(s.long_text.c_str());s.capacity_frame.Annotate("large-text","Public observation",s.long_text);}if(s.add_text)for(int i=0;i<300;++i){ImGui::PushID(i);ImGui::TextUnformatted("Public read-only observation");s.capacity_frame.Annotate("observation","Public observation","No financial payload");ImGui::PopID();}ImGui::EndChild();if(!s.early_button)s.capacity_button=c::ActionButton("capacity-action","Inspect fixture",c::ActionKind::Primary,s,theme,true,"",&s.capacity_frame);ImGui::End();s.capacity_frame.Seal();}else if(s.component)s.controls=c::DrawGallery(s.gallery,s,theme,{});else s.facts=sh::DrawWorkspace(s.shell,s,theme,{}, {1200,820});}
ImGuiTest* Test(ImGuiTestEngine* e,const char* name){auto* t=IM_REGISTER_TEST(e,"roots.semantics",name);t->SetVarsDataType<Fixture>();t->GuiFunc=Draw;return t;}
}
void RegisterSemanticTests(ImGuiTestEngine* engine)
{
    auto* t=Test(engine,"immutable_lifetimes_overflow_and_thread_boundary");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();ctx->Yield(2);
        const auto old=s.Packet();IM_CHECK(old && old->valid);IM_CHECK(old->nodes.size()>40);const auto tab=s.facts.tabs.at(1).id;const auto name=old->Find(tab)->name;const auto value=old->Find(tab)->value;IM_CHECK(name.size()>20);
        s.shell.documents.Rename(1,std::string(200,'L')+"##literal");s.shell.documents.Move(1,1);s.shell.documents.Close(std::array<uint64_t,1>{2});ctx->Yield(3);
        IM_CHECK_EQ(old->Find(tab)->name,name);IM_CHECK_EQ(old->Find(tab)->value,value);IM_CHECK(old->generation<s.Packet()->generation);s.shell.documents.Close(std::array<uint64_t,1>{1});ctx->Yield(3);IM_CHECK_EQ(old->Find(tab)->name,name);IM_CHECK_EQ(old->Find(tab)->value,value);
        c::SemanticExchange exchange;std::array<c::ax::SemanticNode,2> nodes{};std::string dynamic(240,'X');nodes[0].id=11;nodes[0].name=dynamic;nodes[0].description=dynamic;nodes[0].value=dynamic;nodes[0].actions=c::ax::SemanticAction::Press;
        std::array<c::ax::StableId,1> children{12};nodes[0].children=children;nodes[1].id=12;nodes[1].parent=11;
        IM_CHECK(exchange.Publish({nodes,7,false},"scope"));const auto packet=exchange.Published();dynamic.clear();children[0]=99;nodes[0].id=99;
        IM_CHECK_EQ(packet->nodes[0].name,std::string(240,'X'));IM_CHECK_EQ(packet->nodes[0].description,std::string(240,'X'));IM_CHECK_EQ(packet->nodes[0].value,std::string(240,'X'));IM_CHECK_EQ(packet->nodes[0].children[0],12);
        c::ActionResult cross{};bool wrong_publish=true;std::thread worker([&]{cross=exchange.Queue({1,7,11,c::ax::SemanticAction::Press,{}});wrong_publish=exchange.Publish({{},8,false},"bad");});worker.join();IM_CHECK_EQ(cross,c::ActionResult::Queued);IM_CHECK(!wrong_publish);IM_CHECK_EQ(exchange.Drain("scope").size(),1);
        IM_CHECK_EQ(exchange.Queue({1,7,11,c::ax::SemanticAction::Press,{}}),c::ActionResult::Duplicate);
        IM_CHECK_EQ(exchange.Queue({2,6,11,c::ax::SemanticAction::Press,{}}),c::ActionResult::Stale);
        IM_CHECK_EQ(exchange.Queue({3,7,99,c::ax::SemanticAction::Press,{}}),c::ActionResult::Unavailable);
        IM_CHECK_EQ(exchange.Queue({4,7,11,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus,{}}),c::ActionResult::Unsupported);
        IM_CHECK_EQ(exchange.Queue({5,7,11,c::ax::SemanticAction::Press,std::string(4097,'x')}),c::ActionResult::Overflow);
        for(uint64_t i=6;i<38;++i)IM_CHECK_EQ(exchange.Queue({i,7,11,c::ax::SemanticAction::Press,{}}),c::ActionResult::Queued);
        IM_CHECK_EQ(exchange.Queue({38,7,11,c::ax::SemanticAction::Press,{}}),c::ActionResult::Overflow);IM_CHECK(exchange.Drain("changed").empty());
        nodes[0].id=11;nodes[0].children={};nodes[0].name=std::string_view();nodes[1].parent=0;
        IM_CHECK(!exchange.Publish({nodes,8,true},"scope"));IM_CHECK(exchange.Published()->overflow);IM_CHECK(exchange.Published()->nodes.empty());
        nodes[0].name={};nodes[0].description={};nodes[0].value={};IM_CHECK(exchange.Publish({nodes,9,false},"scope"));IM_CHECK_EQ(exchange.Queue({39,9,11,c::ax::SemanticAction::Press,{}}),c::ActionResult::Queued);bool wrong_close=true;std::thread close_worker([&]{wrong_close=exchange.Close();});close_worker.join();IM_CHECK(!wrong_close);IM_CHECK(exchange.Close());IM_CHECK(!exchange.Published()->valid);IM_CHECK_EQ(exchange.Receipts().back().result,c::ActionResult::Stale);IM_CHECK_EQ(exchange.Queue({40,9,11,c::ax::SemanticAction::Press,{}}),c::ActionResult::Stale);
        c::SemanticExchange bounded;
        std::string huge(65537,'a');nodes[0].name=huge;IM_CHECK(!bounded.Publish({nodes,9,false},"scope"));IM_CHECK(bounded.Published()->overflow);IM_CHECK(bounded.Published()->nodes.empty());
    };
    t=Test(engine,"external_exact_edits_and_acknowledgement_equivalence");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();s.component=true;ctx->Yield(3);ctx->SetRef("###component-gallery");
        IM_CHECK(s.Named("Amount (BTC)"));const auto amount=s.Named("Amount (BTC)")->id;
        IM_CHECK_EQ(s.Queue(amount,c::ax::SemanticAction::SetValue,"0.12345678"),c::ActionResult::Queued);ctx->Yield(2);
        IM_CHECK_EQ(s.gallery.amount.saved,"0.12345678");IM_CHECK_EQ(std::get<c::CommitAmount>(s.commands.back()).value.sats(),12345678);
        const auto count=s.commands.size();IM_CHECK_EQ(s.Queue(amount,c::ax::SemanticAction::SetValue,"0.000000001"),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK_EQ(s.commands.size(),count);IM_CHECK_EQ(s.gallery.amount.saved,"0.12345678");IM_CHECK(!s.gallery.amount.error.empty());IM_CHECK(s.Packet()->Find(amount)->state.invalid);
        s.ack=false;IM_CHECK_EQ(s.Queue(amount,c::ax::SemanticAction::SetValue,"0.2"),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK_EQ(s.commands.size(),count+1);IM_CHECK_EQ(s.gallery.amount.saved,"0.12345678");IM_CHECK_EQ(s.gallery.amount.draft(),"0.2");
        s.ack=true;ctx->ItemInputValue(s.controls.amount.id,"0.2");ctx->KeyPress(ImGuiKey_Enter);ctx->Yield(2);IM_CHECK_EQ(s.gallery.amount.saved,"0.2");
        IM_CHECK_EQ(s.Queue(s.controls.primary.id,c::ax::SemanticAction::Press),c::ActionResult::Queued);IM_CHECK_EQ(s.Queue(s.controls.primary.id,c::ax::SemanticAction::Press),c::ActionResult::Queued);const auto before=s.commands.size();ctx->Yield(2);IM_CHECK_EQ(s.commands.size(),before+1);
        const auto receipts=s.Frame().exchange.Receipts();IM_CHECK(std::any_of(receipts.begin(),receipts.end(),[](const auto& r){return r.result==c::ActionResult::Duplicate;}));
        s.gallery.privacy.hidden=true;ctx->Yield(3);const auto hidden=s.Packet();IM_CHECK(hidden->valid);for(const auto& n:hidden->nodes){IM_CHECK(n.name.find("0.12345678")==std::string::npos);IM_CHECK(n.value.find("0.12345678")==std::string::npos);IM_CHECK(n.description.find("0.12345678")==std::string::npos);IM_CHECK_EQ(n.numeric,0);}
        IM_CHECK_EQ(s.Queue(amount,c::ax::SemanticAction::SetValue,"1"),c::ActionResult::Unavailable);
        s.gallery.privacy.hidden=false;ctx->Yield(3);
        const auto selected=s.controls.rows.at(1).widget;s.gallery.label.saved="Renamed literal##2026###";ctx->Yield(3);IM_CHECK_EQ(s.Packet()->Find(selected)->name,"Renamed literal##2026###");
        IM_CHECK_EQ(s.Queue(selected,c::ax::SemanticAction::Select),c::ActionResult::Queued);std::reverse(s.gallery.rows.begin(),s.gallery.rows.end());ctx->Yield(3);IM_CHECK(s.gallery.selection.selected.contains(202));IM_CHECK_EQ(s.Packet()->Find(selected)->name,"Renamed literal##2026###");
        const auto filtered_count=s.commands.size();IM_CHECK_EQ(s.Queue(selected,c::ax::SemanticAction::Select),c::ActionResult::Queued);std::copy_n("no-match",9,s.gallery.filter.begin());ctx->Yield(3);IM_CHECK_EQ(s.commands.size(),filtered_count);IM_CHECK(!s.Packet()->Find(selected));IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Unavailable);
        s.gallery.filter.fill(0);ctx->Yield(3);IM_CHECK(s.Packet()->Find(selected));const auto primary=s.controls.primary.id;const auto prior=s.commands.size();
        IM_CHECK_EQ(s.Queue(primary,c::ax::SemanticAction::Press),c::ActionResult::Queued);
        for(uint64_t i=1000;i<1300;++i)s.gallery.rows.push_back({i,"Capacity probe",Money::Satoshis(1)});
        ctx->Yield(3);IM_CHECK_EQ(s.commands.size(),prior);IM_CHECK(!s.Packet()->valid);IM_CHECK(s.Packet()->overflow);IM_CHECK(s.Packet()->nodes.empty());
        IM_CHECK_EQ(s.Queue(primary,c::ax::SemanticAction::Press),c::ActionResult::Stale);
    };
    t=Test(engine,"actual_tab_row_focus_switch_lock_and_close_invalidation");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();ctx->Yield(3);IM_CHECK(s.Packet()->valid);
        for(const auto& [key,fact]:s.facts.tabs){const auto* n=s.Packet()->Find(fact.id);IM_CHECK(n);IM_CHECK_EQ(n->name,s.shell.documents.Find(key)->name);IM_CHECK_EQ(n->minimum.x,fact.rect[0]);IM_CHECK_EQ(n->maximum.y,fact.rect[3]);}
        const auto* file=s.Named("File");IM_CHECK(file);IM_CHECK_EQ(s.Queue(file->id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(3);
        IM_CHECK(s.facts.menu_items.contains(a::Command::NewWallet));IM_CHECK(!s.Packet()->Find(s.facts.primary.id));ctx->KeyPress(ImGuiKey_Escape);ctx->Yield(3);
        ctx->SetRef("###workspace");ctx->ItemClick(s.facts.tabs.at(1).id,ImGuiMouseButton_Right);ctx->Yield(3);IM_CHECK(s.Named("Move Right"));IM_CHECK(!s.Packet()->Find(s.facts.primary.id));
        IM_CHECK_EQ(s.Queue(s.Named("Move Right")->id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(3);IM_CHECK_EQ(s.shell.documents.Documents().front().id,2);ctx->KeyPress(ImGuiKey_Escape);ctx->Yield(3);
        auto row=s.facts.rows.at(1).id;IM_CHECK_EQ(s.Queue(row,c::ax::SemanticAction::Select),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK(s.shell.documents.Active()->Current().selection.selected.contains(2));IM_CHECK(std::holds_alternative<c::SelectRows>(*s.requests.back().component));
        auto frozen=s.facts.rows.at(3).id;const auto count=s.requests.size();IM_CHECK_EQ(s.Queue(frozen,c::ax::SemanticAction::Select),c::ActionResult::Unavailable);ctx->Yield(2);IM_CHECK_EQ(s.requests.size(),count);
        IM_CHECK_EQ(s.Queue(s.facts.tabs.at(2).id,c::ax::SemanticAction::Select),c::ActionResult::Queued);ctx->Yield(3);IM_CHECK_EQ(s.shell.documents.ActiveId(),2);IM_CHECK(s.facts.tabs.at(2).focused);
        row=s.facts.rows.at(0).id;IM_CHECK_EQ(s.Queue(row,c::ax::SemanticAction::Select),c::ActionResult::Queued);s.shell.documents.Active()->observation=a::ObservationState::Locked;ctx->Yield(3);IM_CHECK_EQ(s.requests.size(),count);IM_CHECK(s.facts.rows.empty());IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Stale);
        s.shell.documents.Active()->observation=a::ObservationState::Offline;ctx->Yield(3);row=s.facts.rows.at(0).id;IM_CHECK_EQ(s.Queue(row,c::ax::SemanticAction::Select),c::ActionResult::Queued);s.shell.documents.Close(std::array<uint64_t,1>{2});ctx->Yield(3);IM_CHECK_EQ(s.requests.size(),count);IM_CHECK(!s.Packet()->Find(row));
    };
    t=Test(engine,"current_frame_overflow_cannot_execute_a_previous_packet_action");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();s.component=true;s.gallery.show_summary=false;
        bool boundary{};
        for(uint64_t count=1;count<=128;++count){s.gallery.rows.clear();for(uint64_t i=0;i<count;++i)s.gallery.rows.push_back({1000+i,"Capacity observation",Money::Satoshis(1)});ctx->Yield(2);
            const auto packet=s.Packet();if(packet && packet->valid && packet->nodes.size()>=253){boundary=true;break;}}
        IM_CHECK(boundary);IM_CHECK(s.Packet()->valid);const auto before=s.commands.size();
        IM_CHECK_EQ(s.Queue(s.controls.primary.id,c::ax::SemanticAction::Press),c::ActionResult::Queued);
        // A legitimate view option adds amount-summary nodes without changing
        // row identity/count or the previously published action target.
        s.gallery.show_summary=true;ctx->Yield(3);IM_CHECK(!s.Packet()->valid);IM_CHECK(s.Packet()->overflow);IM_CHECK_EQ(s.commands.size(),before);
        IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Stale);
        // Exercise the shared production dispatcher independently of the
        // gallery's structural generation key. The footer stays visible while
        // actual annotated text in a child exceeds the frame's node storage.
        s.overflow_controls=true;ctx->Yield(3);IM_CHECK(s.capacity_button.visible);IM_CHECK(s.Packet()->valid);const auto count=s.commands.size();
        IM_CHECK_EQ(s.Queue(s.capacity_button.id,c::ax::SemanticAction::Press),c::ActionResult::Queued);s.add_text=true;ctx->Yield(3);IM_CHECK(s.capacity_button.visible);IM_CHECK(s.Packet()->overflow);IM_CHECK(!s.Packet()->valid);IM_CHECK_EQ(s.commands.size(),count);IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Overflow);
        s.add_text=false;ctx->Yield(3);IM_CHECK(s.Packet()->valid);IM_CHECK_EQ(s.Queue(s.capacity_button.id,c::ax::SemanticAction::Press),c::ActionResult::Queued);s.long_text=std::string(65537,'X');s.huge_text=true;ctx->Yield(3);IM_CHECK(s.capacity_button.visible);IM_CHECK(s.Packet()->overflow);IM_CHECK(!s.Packet()->valid);IM_CHECK_EQ(s.commands.size(),count);IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Overflow);
    };
    t=Test(engine,"early_gallery_copy_cannot_escape_late_overflow");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();s.component=true;s.gallery.show_summary=false;s.gallery.rows.clear();for(uint64_t i=1;i<=300;++i)s.gallery.rows.push_back({i,"Public capacity row",Money::Satoshis(1)});std::copy_n("no-match",9,s.gallery.filter.begin());ctx->Yield(3);IM_CHECK(s.Packet()->valid);const auto count=s.commands.size();IM_CHECK_EQ(s.Queue(s.controls.copy.id,c::ax::SemanticAction::Press),c::ActionResult::Queued);s.gallery.filter.fill(0);ctx->Yield(3);IM_CHECK(s.Packet()->overflow);IM_CHECK(!s.Packet()->valid);IM_CHECK_EQ(s.commands.size(),count);};
    t=Test(engine,"early_gallery_amount_cannot_escape_late_overflow");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();s.component=true;s.gallery.show_summary=false;s.gallery.rows.clear();for(uint64_t i=1;i<=300;++i)s.gallery.rows.push_back({i,"Public capacity row",Money::Satoshis(1)});std::copy_n("no-match",9,s.gallery.filter.begin());ctx->Yield(3);IM_CHECK(s.Packet()->valid);ctx->SetRef("###component-gallery");ctx->ItemClick(s.controls.amount.id);ctx->KeyCharsReplace("0.11111111");ctx->Yield(2);const auto count=s.commands.size();const auto draft=std::string(s.gallery.amount.draft()),saved=s.gallery.amount.saved;IM_CHECK_EQ(draft,"0.11111111");IM_CHECK_EQ(s.Queue(s.controls.amount.id,c::ax::SemanticAction::SetValue,"0.12345678"),c::ActionResult::Queued);s.gallery.filter.fill(0);ctx->Yield(3);IM_CHECK(s.Packet()->overflow);IM_CHECK(!s.Packet()->valid);IM_CHECK_EQ(s.commands.size(),count);IM_CHECK_EQ(s.gallery.amount.draft(),draft);IM_CHECK_EQ(s.gallery.amount.saved,saved);std::copy_n("no-match",9,s.gallery.filter.begin());ctx->Yield(3);IM_CHECK(s.Packet()->valid);ctx->KeyPress(ImGuiKey_Enter);ctx->Yield(2);IM_CHECK_EQ(s.commands.size(),count+1);IM_CHECK_EQ(std::get<c::CommitAmount>(s.commands.back()).value.sats(),11111111);IM_CHECK_EQ(s.gallery.amount.saved,draft);};
    for(const bool text:{false,true}){t=Test(engine,text?"early_shared_action_before_tail_text_overflow":"early_shared_action_before_tail_node_overflow");t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();s.overflow_controls=true;s.early_button=true;ctx->Yield(3);IM_CHECK(s.Packet()->valid);IM_CHECK(s.capacity_button.visible);const auto count=s.commands.size();IM_CHECK_EQ(s.Queue(s.capacity_button.id,c::ax::SemanticAction::Press),c::ActionResult::Queued);if(std::string_view(ctx->Test->Name).find("text_overflow")!=std::string_view::npos){s.long_text=std::string(65537,'X');s.huge_text=true;}else s.add_text=true;ctx->Yield(3);IM_CHECK(s.capacity_button.visible);IM_CHECK(s.Packet()->overflow);IM_CHECK(!s.Packet()->valid);IM_CHECK_EQ(s.commands.size(),count);};}
    t=Test(engine,"batch_invalid_amount_revalidates_primary");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();s.component=true;ctx->Yield(3);IM_CHECK(!s.controls.primary.disabled);const auto count=s.commands.size();
        IM_CHECK_EQ(s.Queue(s.controls.amount.id,c::ax::SemanticAction::SetValue,"0.000000001"),c::ActionResult::Queued);IM_CHECK_EQ(s.Queue(s.controls.primary.id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(2);
        IM_CHECK_EQ(s.commands.size(),count);IM_CHECK_EQ(s.gallery.amount.draft(),"0.000000001");IM_CHECK(!s.gallery.amount.error.empty());
    };
    t=Test(engine,"native_and_queued_activation_coalesce_at_one_boundary");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();s.component=true;ctx->Yield(3);ctx->SetRef("###component-gallery");ctx->MouseMove(s.controls.primary.id);ctx->MouseDown();const auto before=s.commands.size();
        IM_CHECK_EQ(s.Queue(s.controls.primary.id,c::ax::SemanticAction::Press),c::ActionResult::Queued);IM_CHECK_EQ(s.Queue(s.controls.primary.id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->MouseUp();ctx->Yield(2);IM_CHECK_EQ(s.commands.size(),before+1);IM_CHECK_EQ(std::get<c::Action>(s.commands.back()).kind,c::ActionKind::Primary);const auto receipts=s.Frame().exchange.Receipts();IM_CHECK(std::any_of(receipts.begin(),receipts.end(),[](const auto& r){return r.result==c::ActionResult::Applied;}));IM_CHECK(std::any_of(receipts.begin(),receipts.end(),[](const auto& r){return r.result==c::ActionResult::Duplicate;}));
    };
    t=Test(engine,"valid_and_rejected_batches_keep_independent_effects");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();s.component=true;ctx->Yield(3);const auto base=s.commands.size();
        IM_CHECK_EQ(s.Queue(s.controls.amount.id,c::ax::SemanticAction::SetValue,"0.2"),c::ActionResult::Queued);IM_CHECK_EQ(s.Queue(s.controls.primary.id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK_EQ(s.commands.size(),base+2);IM_CHECK(std::holds_alternative<c::CommitAmount>(s.commands[base]));IM_CHECK_EQ(std::get<c::CommitAmount>(s.commands[base]).value.sats(),20000000);IM_CHECK_EQ(std::get<c::Action>(s.commands.back()).kind,c::ActionKind::Primary);
        IM_CHECK_EQ(s.Queue(s.controls.amount.id,c::ax::SemanticAction::SetValue,"0.3"),c::ActionResult::Queued);IM_CHECK_EQ(s.Queue(s.controls.fee.id,c::ax::SemanticAction::SetValue,"2.5"),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK_EQ(s.commands.size(),base+4);IM_CHECK(std::holds_alternative<c::CommitFee>(s.commands.back()));IM_CHECK_EQ(s.gallery.fee.saved,"2.5");
        IM_CHECK_EQ(s.Queue(s.controls.amount.id,c::ax::SemanticAction::SetValue,"0.000000001"),c::ActionResult::Queued);IM_CHECK_EQ(s.Queue(s.controls.fee.id,c::ax::SemanticAction::SetValue,"3.5"),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK_EQ(s.commands.size(),base+5);IM_CHECK(std::holds_alternative<c::CommitFee>(s.commands.back()));IM_CHECK_EQ(s.gallery.amount.saved,"0.3");IM_CHECK_EQ(s.gallery.amount.draft(),"0.000000001");IM_CHECK(!s.gallery.amount.error.empty());
        IM_CHECK_EQ(s.Queue(s.controls.amount.id,c::ax::SemanticAction::SetValue,"0.3"),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK(s.gallery.amount.error.empty());s.reject_amount=true;const auto before=s.commands.size();
        IM_CHECK_EQ(s.Queue(s.controls.amount.id,c::ax::SemanticAction::SetValue,"0.4"),c::ActionResult::Queued);IM_CHECK_EQ(s.Queue(s.controls.primary.id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK_EQ(s.commands.size(),before+1);IM_CHECK(std::holds_alternative<c::CommitAmount>(s.commands.back()));IM_CHECK_EQ(s.gallery.amount.saved,"0.3");IM_CHECK_EQ(s.gallery.amount.draft(),"0.4");IM_CHECK(!s.gallery.amount.error.empty());IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Unavailable);
    };
    t=Test(engine,"live_host_and_filtered_row_guards");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();s.component=true;ctx->Yield(3);s.unavailable_after_amount=true;const auto before=s.commands.size();
        IM_CHECK_EQ(s.Queue(s.controls.amount.id,c::ax::SemanticAction::SetValue,"0.2"),c::ActionResult::Queued);IM_CHECK_EQ(s.Queue(s.controls.primary.id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK_EQ(s.commands.size(),before+1);IM_CHECK_EQ(s.gallery.amount.saved,"0.2");IM_CHECK(s.controls.primary.disabled);IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Unavailable);
        const auto row=s.controls.rows[0].widget;IM_CHECK_EQ(s.Queue(s.Named("Filter public labels")->id,c::ax::SemanticAction::SetValue,"no-match"),c::ActionResult::Queued);IM_CHECK_EQ(s.Queue(row,c::ax::SemanticAction::Select),c::ActionResult::Queued);ctx->Yield(3);IM_CHECK(s.controls.rows.empty());IM_CHECK_EQ(s.commands.size(),before+1);IM_CHECK(s.gallery.selection.selected.empty());IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Unavailable);
    };
    t=Test(engine,"dialog_batches_keep_current_name_format_and_cancel_guards");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();sh::BeginRename(s.shell,1);ctx->Yield(3);const auto old=s.shell.documents.Find(1)->name;const auto before=s.requests.size();
        IM_CHECK_EQ(s.Queue(s.Named("Document name")->id,c::ax::SemanticAction::SetValue,""),c::ActionResult::Queued);IM_CHECK_EQ(s.Queue(s.Named("Apply Name / Icon")->id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK_EQ(s.requests.size(),before);IM_CHECK_EQ(s.shell.documents.Find(1)->name,old);IM_CHECK_EQ(s.shell.actions.name.draft(),"");IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Unavailable);
        IM_CHECK_EQ(s.Queue(s.Named("Cancel")->id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(3);sh::BeginImport(s.shell,"public.json");ctx->Yield(3);const auto old_format=s.shell.actions.candidate.formats[0];const auto choice=s.Named(a::FormatName(old_format))->id;
        IM_CHECK_EQ(s.Queue(s.Named("Public import path")->id,c::ax::SemanticAction::SetValue,"changed.psbt"),c::ActionResult::Queued);IM_CHECK_EQ(s.Queue(choice,c::ax::SemanticAction::Select),c::ActionResult::Queued);ctx->Yield(3);IM_CHECK_EQ(s.shell.actions.candidate.source,"changed.psbt");IM_CHECK(std::find(s.shell.actions.candidate.formats.begin(),s.shell.actions.candidate.formats.end(),old_format)==s.shell.actions.candidate.formats.end());IM_CHECK_EQ(s.requests.size(),before);IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Unavailable);
        IM_CHECK_EQ(s.Queue(s.Named("Cancel")->id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(3);s.component=true;s.gallery.dialog.state.open=true;ctx->Yield(3);const auto count=s.commands.size();IM_CHECK_EQ(s.Queue(s.Named("Cancel")->id,c::ax::SemanticAction::Press),c::ActionResult::Queued);IM_CHECK_EQ(s.Queue(s.Named("Apply")->id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(3);IM_CHECK_EQ(s.commands.size(),count+1);IM_CHECK_EQ(std::get<c::Action>(s.commands.back()).kind,c::ActionKind::CancelDialog);IM_CHECK(!s.gallery.dialog.state.open);IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Unavailable);
    };
    t=Test(engine,"save_close_revalidates_each_effect_after_acknowledgement");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();s.shell.documents.Find(1)->Current().note.SetDraft("First unsaved public draft");s.shell.documents.Find(2)->Current().note.SetDraft("Second unsaved public draft");sh::BeginClose(s.shell,1,false,true);ctx->Yield(3);s.lose_save_availability=true;const auto before=s.requests.size();IM_CHECK_EQ(s.Queue(s.Named("Save and Close")->id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(3);IM_CHECK_EQ(s.requests.size(),before+1);IM_CHECK_EQ(s.requests.back().command,a::Command::SaveDraft);IM_CHECK_EQ(s.requests.back().document,1);IM_CHECK_EQ(s.shell.documents.Documents().size(),3);IM_CHECK(s.shell.documents.Find(1)->Current().note.dirty());IM_CHECK(s.shell.documents.Find(2)->Current().note.dirty());IM_CHECK(s.shell.actions.error.find("unavailable")!=std::string::npos);IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Rejected);
    };
    t=Test(engine,"import_radios_toggles_and_filter_use_actual_boundaries");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();sh::BeginImport(s.shell,"public.json");ctx->Yield(3);
        IM_CHECK(s.Named("Public import path"));const auto path=s.Named("Public import path")->id;IM_CHECK(s.Named("Public import path")->state.required);
        IM_CHECK_EQ(s.Queue(path,c::ax::SemanticAction::SetValue,"changed.txt"),c::ActionResult::Queued);ctx->Yield(3);IM_CHECK_EQ(s.shell.actions.candidate.source,"changed.txt");IM_CHECK(s.shell.actions.candidate.formats.size()>1);
        const auto request=s.Named("Request Import")->id;IM_CHECK_EQ(s.Queue(request,c::ax::SemanticAction::Press),c::ActionResult::Unavailable);
        const auto* choice=s.Named(a::FormatName(s.shell.actions.candidate.formats[1]));IM_CHECK(choice);IM_CHECK_EQ(s.Queue(choice->id,c::ax::SemanticAction::Select),c::ActionResult::Queued);ctx->Yield(3);IM_CHECK_EQ(s.shell.actions.format,1);IM_CHECK(s.Named(a::FormatName(s.shell.actions.candidate.formats[1]))->state.checked);
        s.ack=false;IM_CHECK_EQ(s.Queue(request,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK_EQ(s.requests.back().command,a::Command::ImportSource);IM_CHECK_EQ(s.requests.back().value,"changed.txt");IM_CHECK(!s.shell.actions.error.empty());IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Rejected);
        IM_CHECK_EQ(s.Queue(path,c::ax::SemanticAction::SetValue,std::string("x\0y",3)),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK_EQ(s.shell.actions.candidate.source,"changed.txt");IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Rejected);
        IM_CHECK_EQ(s.Queue(s.Named("Cancel")->id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(3);sh::BeginRename(s.shell,1);ctx->Yield(3);IM_CHECK_EQ(s.Queue(s.Named("Circle")->id,c::ax::SemanticAction::Select),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK_EQ(s.shell.actions.icon,1);IM_CHECK(s.Named("Circle")->state.checked);IM_CHECK_EQ(s.shell.documents.Find(1)->icon,0);
        IM_CHECK_EQ(s.Queue(s.Named("Cancel")->id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(3);IM_CHECK_EQ(s.shell.documents.Find(1)->icon,0);
        s.observations.activity={a::ServiceState::Loading,"Explicit public fixture progress."};s.observations.tasks={{101,"Public task","Public progress",a::ServiceState::Loading,425}};s.shell.drawer=sh::Drawer::Activity;ctx->Yield(3);
        const auto* progress=s.Named("Background activity");for(const auto& node:s.Packet()->nodes)if(node.role==c::ax::SemanticRole::Progress)progress=&node;
        IM_CHECK(progress);IM_CHECK_EQ(progress->role,c::ax::SemanticRole::Progress);IM_CHECK_EQ(progress->numeric,425);IM_CHECK_EQ(progress->maximum_value,1000);IM_CHECK(progress->state.busy);
        s.observations.tasks[0].title=s.observations.tasks[0].detail="Hidden amount 0.01000000";s.shell.privacy.hidden=true;ctx->Yield(3);for(const auto& node:s.Packet()->nodes){IM_CHECK(node.name.find("0.01000000")==std::string::npos);IM_CHECK(node.value.find("0.01000000")==std::string::npos);IM_CHECK(node.description.find("0.01000000")==std::string::npos);}
        s.component=true;ctx->Yield(3);IM_CHECK_EQ(s.Queue(s.Named("Hide amounts")->id,c::ax::SemanticAction::Toggle),c::ActionResult::Queued);ctx->Yield(3);IM_CHECK(s.gallery.privacy.hidden);IM_CHECK(s.Named("Hide amounts")->state.checked);IM_CHECK(s.Named("Amount (BTC)")->state.disabled);
        IM_CHECK_EQ(s.Queue(s.Named("Reduced motion")->id,c::ax::SemanticAction::Toggle),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK(s.gallery.reduced_motion);
        const auto filter=s.Named("Filter public labels")->id;IM_CHECK_EQ(s.Queue(filter,c::ax::SemanticAction::Focus),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK_EQ(s.Queue(filter,c::ax::SemanticAction::SetValue,"no-match"),c::ActionResult::Queued);ctx->Yield(3);IM_CHECK(s.controls.rows.empty());IM_CHECK_EQ(std::string(s.gallery.filter.data()),"no-match");
    };
    t=Test(engine,"escape_from_active_edit_returns_actual_document_tab_focus");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();ctx->Yield(3);ctx->SetRef("###workspace");ctx->ItemClick(s.facts.note.id);ctx->KeyCharsReplace("dirty public draft");ctx->Yield(2);ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_W);ctx->Yield(3);IM_CHECK(s.facts.document_dialog_visible);ctx->KeyPress(ImGuiKey_Escape);ctx->Yield(5);IM_CHECK(!s.facts.document_dialog_visible);IM_CHECK_EQ(s.shell.documents.Documents().size(),3);IM_CHECK(s.facts.tabs.at(1).focused);IM_CHECK(s.Packet()->Find(s.facts.tabs.at(1).id)->state.focused);
    };
    t=Test(engine,"external_focus_leaves_active_editor_for_actual_tab");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();ctx->Yield(3);ctx->SetRef("###workspace");ctx->ItemClick(s.facts.note.id);ctx->KeyCharsReplace("public active draft");ctx->Yield(2);IM_CHECK_EQ(s.Queue(s.facts.tabs.at(1).id,c::ax::SemanticAction::Focus),c::ActionResult::Queued);ctx->Yield(5);IM_CHECK(s.facts.tabs.at(1).focused);IM_CHECK(s.Packet()->Find(s.facts.tabs.at(1).id)->state.focused);};
    t=Test(engine,"external_table_focus_retains_native_keyboard_activation");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();ctx->Yield(3);ctx->SetRef("###workspace");ctx->ItemClick(s.facts.note.id);ctx->KeyCharsReplace("public editor draft");ctx->KeyPress(ImGuiKey_Enter);ctx->Yield(3);const auto row=s.facts.rows.front().id;IM_CHECK_EQ(s.Queue(row,c::ax::SemanticAction::Focus),c::ActionResult::Queued);ctx->Yield(5);IM_CHECK(s.Packet()->Find(row)->state.focused);IM_CHECK(ImGui::GetIO().NavVisible);const auto before=s.requests.size();ctx->KeyPress(ImGuiKey_Enter);ctx->Yield(3);IM_CHECK_EQ(s.requests.size(),before+1);IM_CHECK(s.shell.documents.Active()->Current().selection.selected.contains(1));};
    t=Test(engine,"production_table_cells_have_owned_row_hierarchy_and_stable_identity");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();ctx->Yield(3);for(const auto& row:s.facts.rows){const auto* node=s.Packet()->Find(row.id);IM_CHECK(node);IM_CHECK_EQ(node->children.size(),3);IM_CHECK_EQ(s.Packet()->Find(node->parent)->role,c::ax::SemanticRole::Grid);for(auto id:node->children){const auto* cell=s.Packet()->Find(id);IM_CHECK_EQ(cell->role,c::ax::SemanticRole::Cell);IM_CHECK_EQ(cell->parent,row.id);}}s.component=true;s.gallery.show_summary=false;s.gallery.rows.front().label="Invoice##2026###literal";ctx->Yield(3);const auto row=s.controls.rows.front().widget;const auto packet=s.Packet();const auto* node=packet->Find(row);IM_CHECK(node);IM_CHECK_EQ(node->children.size(),2);IM_CHECK_EQ(packet->Find(node->parent)->role,c::ax::SemanticRole::Grid);const auto label=node->children.front();IM_CHECK_EQ(packet->Find(label)->value,"Invoice##2026###literal");IM_CHECK_EQ(s.Queue(row,c::ax::SemanticAction::Select),c::ActionResult::Queued);ctx->Yield(3);s.gallery.rows.front().label="Renamed##2026###literal";const auto filter=s.Named("Filter public labels")->id;IM_CHECK_EQ(s.Queue(filter,c::ax::SemanticAction::SetValue,"Renamed"),c::ActionResult::Queued);ctx->Yield(4);IM_CHECK_EQ(s.controls.rows.size(),1);IM_CHECK_EQ(s.controls.rows.front().widget,row);IM_CHECK_EQ(s.Packet()->Find(row)->children.front(),label);IM_CHECK(s.Packet()->Find(row)->state.selected);IM_CHECK_EQ(s.Packet()->Find(label)->value,"Renamed##2026###literal");IM_CHECK_EQ(packet->Find(label)->value,"Invoice##2026###literal");IM_CHECK_EQ(s.Queue(s.Named("Hide amounts")->id,c::ax::SemanticAction::Toggle),c::ActionResult::Queued);ctx->Yield(4);const auto* amount=s.Packet()->Find(s.Packet()->Find(row)->children.back());IM_CHECK_EQ(amount->role,c::ax::SemanticRole::Cell);IM_CHECK_EQ(amount->value,"Hidden");};
    t=Test(engine,"active_lock_returns_focus_to_tab_without_repeating_or_unlock_steal");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();ctx->Yield(3);ctx->SetRef("###workspace");ctx->ItemClick(s.facts.note.id);ctx->KeyCharsReplace("public draft before lock");ctx->Yield(2);const auto old=s.Packet();const auto field=s.facts.note.id;const auto before=s.requests.size();IM_CHECK_EQ(s.Queue(field,c::ax::SemanticAction::SetValue,"stale lock edit"),c::ActionResult::Queued);s.shell.documents.Active()->observation=a::ObservationState::Locked;ctx->Yield(5);IM_CHECK_EQ(s.requests.size(),before);IM_CHECK(s.facts.rows.empty());IM_CHECK(!s.Named("Label"));IM_CHECK(s.facts.tabs.at(1).focused);IM_CHECK(s.Packet()->Find(s.facts.tabs.at(1).id)->state.focused);IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Stale);ctx->KeyPress(ImGuiKey_Tab);ctx->Yield(3);IM_CHECK(!s.facts.tabs.at(1).focused);c::ax::StableId next{};for(const auto& node:s.Packet()->nodes)if(node.state.focused)next=node.id;IM_CHECK(next);s.shell.documents.Active()->observation=a::ObservationState::Offline;ctx->Yield(4);IM_CHECK(s.Packet()->Find(next));IM_CHECK(s.Packet()->Find(next)->state.focused);IM_CHECK(old->Find(field));};
    t=Test(engine,"static_annotation_does_not_borrow_editor_focus");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();s.component=true;ctx->Yield(3);ctx->SetRef("###component-gallery");ctx->ItemClick(s.controls.amount.id);ctx->KeyCharsReplace("0.000000001");ctx->KeyPress(ImGuiKey_Enter);ctx->Yield(3);IM_CHECK(!s.gallery.amount.error.empty());IM_CHECK(s.Packet()->Find(s.controls.amount.id)->state.focused);for(const auto& n:s.Packet()->nodes)if(n.state.focused)IM_CHECK(c::ax::Supports(n.actions,c::ax::SemanticAction::Focus));
    };
    t=Test(engine,"native_unicode_cursor_selection_and_clipped_item_facts");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();s.component=true;s.gallery.show_summary=false;s.gallery.rows.clear();for(uint64_t i=1;i<=40;++i)s.gallery.rows.push_back({i,"Public observation",Money::Satoshis(1)});ctx->Yield(3);ctx->SetRef("###component-gallery");ctx->ItemClick(s.controls.label.id);ctx->KeyCharsReplace("Öffentlich");ctx->Yield(2);const auto field=s.Packet()->Find(s.controls.label.id);IM_CHECK(field);IM_CHECK_EQ(field->value,"Öffentlich");IM_CHECK_EQ(field->text_position.caret,10);ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_A);ctx->Yield(2);const auto selected=s.Packet()->Find(s.controls.label.id);IM_CHECK_EQ(selected->text_position.start,0);IM_CHECK_EQ(selected->text_position.end,10);const auto last=s.controls.rows.back();IM_CHECK(!last.visible);IM_CHECK(!s.Packet()->Find(last.widget)->showing);ctx->ItemClick(last.widget);ctx->Yield(3);IM_CHECK(s.controls.rows.back().visible);IM_CHECK(s.Packet()->Find(last.widget)->showing);
    };
    t=Test(engine,"validation_focus_expires_when_privacy_removes_editor");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();s.component=true;ctx->Yield(3);ctx->SetRef("###component-gallery");IM_CHECK_EQ(s.Queue(s.controls.amount.id,c::ax::SemanticAction::SetValue,"0.000000001"),c::ActionResult::Queued);ctx->Yield(1);s.gallery.privacy.hidden=true;ctx->Yield(3);ctx->ItemClick(s.controls.copy.id);ctx->Yield(2);IM_CHECK(s.controls.copy.focused);s.gallery.privacy.hidden=false;ctx->Yield(4);IM_CHECK(s.controls.copy.focused);IM_CHECK(!s.controls.amount.focused);};
    t=Test(engine,"native_sequence_allocation_and_pre_dispatch_cancellation");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();s.component=true;ctx->Yield(3);auto& exchange=s.Frame().exchange;const auto generation=s.Packet()->generation;const auto target=s.controls.primary.id;std::vector<std::pair<uint64_t,c::ActionResult>> a,b;std::thread one([&]{for(int i=0;i<20;++i)a.push_back(exchange.QueueNext(generation,target,c::ax::SemanticAction::Press));});std::thread two([&]{for(int i=0;i<20;++i)b.push_back(exchange.QueueNext(generation,target,c::ax::SemanticAction::Press));});one.join();two.join();a.insert(a.end(),b.begin(),b.end());std::set<uint64_t> unique;size_t admitted{};for(auto r:a){unique.insert(r.first);admitted+=r.second==c::ActionResult::Queued;}IM_CHECK_EQ(unique.size(),40);IM_CHECK_EQ(admitted,32);const auto first=std::find_if(a.begin(),a.end(),[](auto r){return r.second==c::ActionResult::Queued;})->first;bool wrong=true;std::thread cancel([&]{wrong=exchange.CancelQueued(first);});cancel.join();IM_CHECK(!wrong);for(auto r:a)if(r.second==c::ActionResult::Queued)s.cancel_on_owner.push_back(r.first);const auto before=s.commands.size();ctx->Yield(3);IM_CHECK_EQ(s.cancel_results.size(),32);for(bool accepted:s.cancel_results)IM_CHECK(accepted);IM_CHECK_EQ(s.commands.size(),before);IM_CHECK_EQ(exchange.Receipts().back().result,c::ActionResult::Stale);
    };
    t=Test(engine,"modal_rename_cancel_and_same_boundary_failure");
    t->TestFunc=[](ImGuiTestContext* ctx){auto& s=ctx->GetVars<Fixture>();ctx->Yield(2);sh::BeginRename(s.shell,1);ctx->Yield(3);IM_CHECK(s.Packet()->valid);for(const auto& n:s.Packet()->nodes)IM_CHECK(n.role!=c::ax::SemanticRole::Tab);
        const auto name=s.Named("Document name")->id;const std::string literal=std::string(180,'R')+"###literal";
        IM_CHECK_EQ(s.Queue(name,c::ax::SemanticAction::SetValue,literal),c::ActionResult::Queued);ctx->Yield(2);const auto apply=s.Named("Apply Name / Icon")->id;
        s.ack=false;IM_CHECK_EQ(s.Queue(apply,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(2);IM_CHECK(s.shell.documents.Find(1)->name!=literal);IM_CHECK_EQ(s.shell.actions.name.draft(),literal);IM_CHECK(!s.shell.actions.error.empty());IM_CHECK_EQ(s.Frame().exchange.Receipts().back().result,c::ActionResult::Rejected);
        s.ack=true;IM_CHECK_EQ(s.Queue(apply,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(3);IM_CHECK_EQ(s.shell.documents.Find(1)->name,literal);IM_CHECK_EQ(s.shell.actions.kind,sh::DocumentDialog::None);IM_CHECK(s.facts.tabs.at(1).focused);
        sh::BeginRename(s.shell,1);ctx->Yield(3);IM_CHECK_EQ(s.Queue(s.Named("Cancel")->id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(3);IM_CHECK_EQ(s.shell.documents.Find(1)->name,literal);IM_CHECK(s.facts.tabs.at(1).focused);
        sh::BeginRename(s.shell,1);ctx->Yield(3);s.shell.documents.Close(std::array<uint64_t,1>{1});ctx->Yield(3);IM_CHECK_EQ(s.Queue(s.Named("Close")->id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(3);IM_CHECK_EQ(s.shell.documents.ActiveId(),2);IM_CHECK(s.facts.tabs.at(2).focused);
        sh::BeginRename(s.shell,2);ctx->Yield(3);s.shell.documents.Close(std::array<uint64_t,2>{2,3});ctx->Yield(3);IM_CHECK_EQ(s.Queue(s.Named("Close")->id,c::ax::SemanticAction::Press),c::ActionResult::Queued);ctx->Yield(3);IM_CHECK(s.shell.documents.Documents().empty());IM_CHECK(s.facts.document_actions.at("open-import").focused);
    };
    t=Test(engine,"locale_catalog_exact_money_and_utc");
    t->TestFunc=[](ImGuiTestContext*) {
        namespace l=ui::locale;
        using Language=l::Language;
        IM_CHECK(l::Select("de_DE.UTF-8") == Language::German);
        IM_CHECK(l::Select("ar_EG.UTF-8") == Language::English);
        IM_CHECK(l::Select("invalid/locale/") == Language::English);
        IM_CHECK(l::Select("qps-ploc") == Language::Pseudo);
        IM_CHECK(l::Translate("Send", Language::German) == "Senden");
        IM_CHECK(l::Translate("Pay to", Language::Pseudo) == "[Payment destination]");
        const std::string_view mixed="Wallet محفظة 0.00000001";
        IM_CHECK(l::Translate(mixed, Language::German) == mixed);
        IM_CHECK(l::Select("de_unsafe") == Language::English);
        const std::string_view address="bcrt1qpublic-fixture";
        IM_CHECK(l::Translate(address, Language::German) == address);
        const auto minimum=Money::Satoshis(std::numeric_limits<int64_t>::min());
        const auto maximum=Money::Satoshis(std::numeric_limits<int64_t>::max());
        IM_CHECK(Money::ParseBtc(l::Decimal(minimum, Language::German), ',') == minimum);
        IM_CHECK(Money::ParseBtc(l::Decimal(maximum, Language::German), ',') == maximum);
        IM_CHECK(Money::ParseBtc(l::Decimal(minimum, Language::English)) == minimum);
        IM_CHECK_EQ(l::Decimal(Money::Satoshis(1), Language::German), "0,00000001");
        IM_CHECK_EQ(l::Decimal(Money::Satoshis(100000000), Language::German, true), "1");
        IM_CHECK_EQ(l::Decimal(Money::Satoshis(125000000), Language::German, true), "1,25");
        IM_CHECK_EQ(l::Date(-1, true, Language::German), "31.12.1969 23:59:59 UTC");
        IM_CHECK_EQ(l::Date(1735689599, true, Language::German), "31.12.2024 23:59:59 UTC");
        IM_CHECK_EQ(l::Date(1735689600, false, Language::German), "01.01.2025");
        IM_CHECK_EQ(l::Date(1735689600, true, Language::English), "2025-01-01 00:00:00 UTC");
    };

}
