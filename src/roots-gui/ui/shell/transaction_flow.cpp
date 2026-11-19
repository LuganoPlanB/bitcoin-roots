// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/localization.h"
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"

#include <algorithm>

namespace roots::gui::ui::shell {
namespace {
namespace c = components;
using c::ax::SemanticAction;
using c::ax::SemanticRole;

struct FlowGuard {
    State& state;
    std::shared_ptr<const FundingPreview> preview;
    FundingScope scope;
    std::function<bool()> current;
    bool Valid() const
    {
        const auto* draft=ActiveSendDraft(state);
        return draft && draft->id==scope.document && draft->draft_revision==scope.draft && state.funding.flow.open
            && state.funding.scope==scope && state.funding.preview==preview && !state.funding.creating
            && state.actions.kind==DocumentDialog::None && !state.recovery_dialog.open && !state.local_document.dialog.open
            && (!c::native_input || !c::native_input->NativeDialogActive()) && current && current();
    }
    void Changed() const { ++state.funding.revision; }
    bool Select(FlowKey key) const
    {
        if (!Valid() || !preview->flow->Find(key)) return false;
        auto& view=state.funding.flow;
        view.selected=key;
        view.choose_change=false;
        Changed();
        return true;
    }
    const FlowItem* Selected(FlowKey key) const
    {
        return Valid() && state.funding.flow.selected==key ? preview->flow->Find(key) : nullptr;
    }
};

void Text(State& state,const char* id,const char* label,std::string_view value)
{
    ImGui::TextWrapped("%.*s",int(value.size()),value.data());
    state.presentation.Annotate(id,label,value);
}
std::string Name(FlowKind kind, size_t index)
{
    switch (kind) {
    case FlowKind::Input: return "Input "+std::to_string(index+1);
    case FlowKind::Recipient: return "Recipient "+std::to_string(index+1);
    case FlowKind::Change: return "Change";
    case FlowKind::Fee: return "Fee";
    }
    return {};
}
void Button(State& state,Facts& facts,const imkit::Theme& theme,const std::string& key,const char* text,
    std::function<bool()> action,bool enabled=true,bool selected=false,ImVec2 size={},const char* accessible=nullptr)
{
    ImGui::PushID(key.c_str());
    ImGui::BeginDisabled(!enabled);
    const bool pressed=theme::ActionButton(text,selected ? imkit::ActionVariant::Primary : imkit::ActionVariant::Secondary,size,theme);
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    facts.flow[key]={{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),!enabled,
        pressed,pressed,ImGui::IsItemVisible()};
    const auto semantic=state.presentation.Interact(accessible ? accessible : text,SemanticRole::Button,
        SemanticAction::Press|SemanticAction::Focus,!enabled,selected);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted=pressed && enabled && action();
    if (semantic) state.presentation.Defer(*semantic,[enabled,pressed,accepted,action=std::move(action)](const auto&) {
        return enabled && (pressed ? accepted : action());
    });
}
void Icon(FlowKind kind,ImVec2 p,ImU32 color)
{
    auto* draw=ImGui::GetWindowDrawList();
    const float r=ImGui::GetTextLineHeight()*.21f;
    switch (kind) {
    case FlowKind::Input:
        draw->AddRect({p.x-r,p.y-r},{p.x+r,p.y+r},color,1.f);
        break;
    case FlowKind::Recipient:
        draw->AddCircle(p,r,color,0,1.25f);
        break;
    case FlowKind::Change:
        draw->PathArcTo(p,r,.5f,5.6f,12);
        draw->PathStroke(color,0,1.25f);
        draw->AddLine({p.x+r,p.y-r},{p.x+r,p.y},color,1.25f);
        break;
    case FlowKind::Fee:
        draw->AddQuad({p.x-r,p.y},{p.x,p.y-r},{p.x+r,p.y},{p.x,p.y+r},color,1.25f);
        break;
    }
}
size_t PageSize(const FlowView& view) { return view.expanded ? 8 : 4; }

void Page(const FlowGuard& guard,Facts& facts,const imkit::Theme& theme,bool input,size_t first,size_t count,size_t page)
{
    if (count<=page) return;
    auto& state=guard.state;
    const auto kind=input ? "input" : "output";
    const auto change=[guard,input,page,count](bool forward) {
        if (!guard.Valid()) return false;
        auto& view=guard.state.funding.flow;
        auto& start=input ? view.input_first : view.output_first;
        const size_t next=forward ? start+page : start>page ? start-page : 0;
        if (next>=count || next==start) return false;
        start=next;
        guard.Changed();
        return true;
    };
    const auto label=std::string(input ? "Input" : "Output/fee")+" items "+std::to_string(first+1)+"–"
        +std::to_string(std::min(first+page,count))+" of "+std::to_string(count);
    Text(state,input ? "input-group" : "output-group",input ? "Input group" : "Output group",label);
    Button(state,facts,theme,std::string(kind)+"-prev","Earlier",
        [change]{return change(false);},first>0,false,{},input ? "Earlier inputs" : "Earlier outputs");
    ImGui::SameLine();
    Button(state,facts,theme,std::string(kind)+"-next","Later",
        [change]{return change(true);},first+page<count,false,{},input ? "Later inputs" : "Later outputs");
}
void Curves(const std::vector<ImVec2>& left,const std::vector<ImVec2>& right)
{
    if (left.empty() || right.empty()) return;
    const float x=(left.front().x+right.front().x)*.5f;
    const ImVec2 hub{x,(left.front().y+left.back().y+right.front().y+right.back().y)*.25f};
    auto* draw=ImGui::GetWindowDrawList();
    const auto color=ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const auto line=[&](ImVec2 a,ImVec2 b) {
        const float bend=(b.x-a.x)*.54f;
        draw->AddBezierCubic(a,{a.x+bend,a.y},{b.x-bend,b.y},b,color,1.25f);
    };
    for (auto point:left) line(point,hub);
    for (auto point:right) line(hub,point);
    draw->AddCircleFilled(hub,2.2f,color);
}

void Rows(const FlowGuard& guard,Facts& facts,const imkit::Theme& theme)
{
    auto& state=guard.state;
    const auto& view=state.funding.flow;
    const auto& model=*guard.preview->flow;
    const size_t page=PageSize(view);
    const size_t first_in=view.input_first, first_out=view.output_first;
    const size_t inputs=std::min(page,model.inputs.size()-first_in);
    const size_t outputs=std::min(page,model.outputs.size()-first_out);
    const bool table=view.table;
    const float width=ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,{ImGui::GetStyle().CellPadding.x,ImGui::GetTextLineHeight()*.09f});
    if (!ImGui::BeginTable("flow-columns",table ? 2 : 3,ImGuiTableFlags_SizingStretchSame,{width,0})) {
        ImGui::PopStyleVar();
        return;
    }
    ImGui::TableSetupColumn("Inputs",ImGuiTableColumnFlags_WidthStretch,1.f);
    if (!table) ImGui::TableSetupColumn("Flow",ImGuiTableColumnFlags_WidthFixed,width*.24f);
    ImGui::TableSetupColumn("Recipients, change and fee",ImGuiTableColumnFlags_WidthStretch,1.f);
    const int right=table ? 1 : 2;
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    Text(state,"input-heading","Flow inputs","Inputs");
    ImGui::TableSetColumnIndex(right);
    Text(state,"output-heading","Flow outputs","Recipients, change and fee");
    std::vector<ImVec2> left_edges,right_edges;
    for (size_t row=0;row<std::max(inputs,outputs);++row) {
        ImGui::TableNextRow();
        const auto item=[&](bool input,size_t index) {
            const FlowKey key{input,index};
            const auto* data=model.Find(key);
            const auto label=Name(data->kind,index);
            const auto id=std::string(table ? "table-" : "diagram-")+(input ? "input-" : "output-")+std::to_string(index);
            const bool selected=view.selected==key;
            if (table) {
                ImGui::PushID(id.c_str());
                const auto text=label+" · "+std::to_string(data->amount.sats())+" sats";
                const bool pressed=ImGui::Selectable(text.c_str(),selected);
                const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
                facts.flow[id]={{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),false,
                    pressed,pressed,ImGui::IsItemVisible()};
                auto semantic=state.presentation.Interact(label.c_str(),SemanticRole::Button,
                    SemanticAction::Select|SemanticAction::Focus,false,selected,std::to_string(data->amount.sats())+" sats");
                ImGui::PopID();
                if (pressed) guard.Select(key);
                if (semantic) state.presentation.Defer(*semantic,[guard,key](const auto&){return guard.Select(key);});
            } else {
                const float gap=ImGui::GetTextLineHeight()*1.25f;
                Icon(data->kind,{ImGui::GetCursorScreenPos().x+gap*.31f,ImGui::GetCursorScreenPos().y+ImGui::GetFrameHeight()*.5f},
                    ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::SetCursorPosX(ImGui::GetCursorPosX()+gap);
                const auto size=ImVec2{std::max(1.f,ImGui::GetContentRegionAvail().x),
                    ImGui::GetTextLineHeight()*(view.expanded ? 2.1f : 1.65f)};
                Button(state,facts,theme,id,label.c_str(),[guard,key]{return guard.Select(key);},true,selected,size);
                const auto& rect=facts.flow.at(id).rect;
                const auto edge=ImVec2{input ? rect[2]+4.f : rect[0]-gap-3.f,(rect[1]+rect[3])*.5f};
                (input ? left_edges : right_edges).push_back(edge);
            }
        };
        if (row<inputs) {
            ImGui::TableSetColumnIndex(0);
            item(true,first_in+row);
        }
        if (row<outputs) {
            ImGui::TableSetColumnIndex(right);
            item(false,first_out+row);
        }
    }
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    Page(guard,facts,theme,true,first_in,model.inputs.size(),page);
    ImGui::TableSetColumnIndex(right);
    Page(guard,facts,theme,false,first_out,model.outputs.size(),page);
    ImGui::EndTable();
    ImGui::PopStyleVar();
    if (!table) Curves(left_edges,right_edges);
}
void RetireEdited(const FlowGuard& guard,std::string status)
{
    auto& funding=guard.state.funding;
    funding.preview.reset();
    funding.scope.reset();
    funding.flow={};
    funding.status=std::move(status);
    guard.Changed();
}
bool CanExclude(const app::Document& draft,const Outpoint& point)
{
    if (draft.draft_revision.value==UINT64_MAX || draft.excluded_coins.size()>=4096
        || std::ranges::find(draft.excluded_coins,point)!=draft.excluded_coins.end()) return false;
    if (draft.selected_coins.empty()) return !draft.coin_draft;
    return draft.selected_coins.size()>1 && std::ranges::find(draft.selected_coins,point)!=draft.selected_coins.end()
        && (!draft.coin_draft || std::ranges::find(draft.coin_draft->inputs,point,&app::PinnedCoin::outpoint)!=draft.coin_draft->inputs.end());
}
bool Exclude(const FlowGuard& guard,FlowKey key)
{
    const auto* item=guard.Selected(key);
    auto* draft=guard.state.documents.Active();
    if (!item || !item->outpoint || !draft || !CanExclude(*draft,*item->outpoint)) return false;
    const auto point=*item->outpoint;
    draft->excluded_coins.push_back(point);
    std::erase(draft->selected_coins,point);
    if (draft->coin_draft) std::erase_if(draft->coin_draft->inputs,[&](const auto& pin){return pin.outpoint==point;});
    ++draft->draft_revision.value;
    RetireEdited(guard,draft->coin_draft ? "Input excluded. Revalidate the remaining exact pins before funding."
        : "Input excluded. Request a new funding preview.");
    return true;
}
app::Request CopyRequest(const FlowGuard& guard,const FlowItem& item)
{
    return {app::Command::Component,guard.scope.document,app::Section::Send,
        c::CopyIdentifier{item.kind==FlowKind::Input ? "flow-outpoint" : "flow-address",item.identifier,true}};
}
void ChangeChoices(const FlowGuard& guard,Facts& facts,const imkit::Theme& theme,FlowKey key)
{
    auto& state=guard.state;
    auto& view=state.funding.flow;
    const auto& choices=guard.preview->change_choices;
    if (choices.empty()) {
        Text(state,"change-choices-unknown","Eligible change","Other eligible internal change choices are Unknown.");
        return;
    }
    const auto index=std::min(view.change_index,choices.size()-1);
    const auto& choice=choices[index];
    const auto advance=[guard,key,index](bool forward) {
        if (!guard.Selected(key) || !guard.state.funding.flow.choose_change
            || (forward ? index+1>=guard.preview->change_choices.size() : index==0)) return false;
        guard.state.funding.flow.change_index=forward ? index+1 : index-1;
        guard.Changed();
        return true;
    };
    Button(state,facts,theme,"change-prev","Earlier choice",[advance]{return advance(false);},index>0);
    ImGui::SameLine();
    Button(state,facts,theme,"change-next","Later choice",[advance]{return advance(true);},index+1<choices.size());
    Text(state,"change-candidate","Eligible internal address",choice.address);
    Text(state,"change-identity","Internal change proof","Choice "+std::to_string(index+1)+" of "+std::to_string(choices.size())
        +" · Descriptor index "+std::to_string(choice.index)+" · Allocation unconfirmed");
    Button(state,facts,theme,"change-use","Use this change address",[guard,key,index]{
        if (!guard.Selected(key) || !guard.state.funding.flow.choose_change
            || guard.state.funding.flow.change_index!=index) return false;
        auto* draft=guard.state.documents.Active();
        const auto& chosen=guard.preview->change_choices.at(index);
        if (!draft || draft->draft_revision.value==UINT64_MAX || draft->funding_change==chosen) return false;
        draft->funding_change=chosen;
        ++draft->draft_revision.value;
        RetireEdited(guard,"Change preference set. Request a new preview to verify it.");
        return true;
    },state.documents.Active()->draft_revision.value!=UINT64_MAX && state.documents.Active()->funding_change!=choice);
}
void Inspect(const FlowGuard& guard,app::Requests& host,Facts& facts,const imkit::Theme& theme)
{
    auto& state=guard.state;
    const auto& view=state.funding.flow;
    if (!view.selected) return;
    const auto key=*view.selected;
    const auto* item=guard.preview->flow->Find(key);
    if (!item) return;
    Text(state,"flow-selected","Selected flow item",Name(item->kind,key.index)+" · "+locale::Btc(item->amount)+" BTC · "
        +std::to_string(item->amount.sats())+" sats");
    if (!item->identifier.empty()) {
        Text(state,"flow-identifier",item->kind==FlowKind::Input ? "Full outpoint" : "Full address",item->identifier);
        Button(state,facts,theme,"copy",item->kind==FlowKind::Input ? "Copy outpoint" : "Copy address",[guard,&host,key]{
            const auto* exact=guard.Selected(key);
            if (!exact || exact->identifier.empty()) return false;
            const auto request=CopyRequest(guard,*exact);
            if (!host.CanSubmit(request).available) return false;
            const auto receipt=host.Submit(request);
            guard.state.notice=receipt.reason;
            return receipt.accepted;
        },host.CanSubmit(CopyRequest(guard,*item)).available);
    }
    if (item->kind==FlowKind::Input) {
        ImGui::SameLine();
        Button(state,facts,theme,"exclude","Exclude input",[guard,key]{return Exclude(guard,key);},
            CanExclude(*state.documents.Active(),*item->outpoint));
    } else if (item->kind!=FlowKind::Fee) {
        ImGui::SameLine();
        Button(state,facts,theme,"qr","Address QR",[]{return false;},false);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Address QR requires the QR renderer.");
        Text(state,"flow-qr-unavailable","Address QR availability","Address QR requires the QR renderer.");
    }
    if (item->kind!=FlowKind::Change) return;
    if (guard.preview->change) {
        Text(state,"flow-change-proof","Internal change evidence","Verified internal descriptor index "
            +std::to_string(guard.preview->change->index)+" · Allocation unconfirmed");
    }
    Button(state,facts,theme,"change-choices",view.choose_change ? "Hide change choices" : "Choose change",[guard,key]{
        if (!guard.Selected(key) || guard.preview->change_choices.empty()) return false;
        auto& model=guard.state.funding.flow;
        model.choose_change=!model.choose_change;
        model.change_index=0;
        guard.Changed();
        return true;
    },!guard.preview->change_choices.empty());
    if (view.choose_change) ChangeChoices(guard,facts,theme,key);
}
}

void DrawTransactionFlow(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme,std::function<bool()> current)
{
    if (!state.funding.scope || !state.funding.preview || !state.funding.preview->flow) return;
    FlowGuard guard{state,state.funding.preview,*state.funding.scope,std::move(current)};
    if (!guard.Valid()) return;
    auto& view=state.funding.flow;
    ImGui::PushID("transaction-flow");
    Button(state,facts,theme,"close","Back to funding",[guard]{
        if (!guard.Valid()) return false;
        guard.state.funding.flow.open=false;
        guard.Changed();
        return true;
    });
    ImGui::SameLine();
    Button(state,facts,theme,"table",view.table ? "Diagram view" : "Table view",[guard]{
        if (!guard.Valid()) return false;
        guard.state.funding.flow.table=!guard.state.funding.flow.table;
        guard.Changed();
        return true;
    });
    ImGui::SameLine();
    Button(state,facts,theme,"expand",view.expanded ? "Compact flow" : "Expand flow",[guard]{
        if (!guard.Valid()) return false;
        auto& flow=guard.state.funding.flow;
        flow.expanded=!flow.expanded;
        const auto page=PageSize(flow);
        flow.input_first=(flow.selected && flow.selected->input ? flow.selected->index : flow.input_first)/page*page;
        flow.output_first=(flow.selected && !flow.selected->input ? flow.selected->index : flow.output_first)/page*page;
        guard.Changed();
        return true;
    });
    if (view.open) {
        Rows(guard,facts,theme);
        Inspect(guard,host,facts,theme);
    }
    ImGui::PopID();
}
}
