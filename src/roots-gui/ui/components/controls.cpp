// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/components/controls.h"
#include "ui/theme/theme.h"
#include "ui/theme/tokens.h"
#include <imkit/charts.h>
#include <algorithm>
namespace roots::gui::ui::components {
namespace {
ItemFacts Facts(bool activated=false)
{
    const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
    return {{a.x,a.y,b.x,b.y},ImGui::GetItemID(),ImGui::IsItemFocused(),bool(ImGui::GetItemFlags()&ImGuiItemFlags_Disabled),activated,false,ImGui::IsItemVisible()};
}
using Commit=bool(*)(const char*,Edit&,Commands&);
bool EditCommandAvailable(const CommandEndpoint& available,Commit commit,const std::string& id,std::string_view draft,const Edit& edit) {
    if(draft==edit.saved)return true;
    if(commit==CommitAmountEdit){const auto value=ParseAmount(draft);return !value || available(CommitAmount{id,*value});}
    if(commit==CommitFeeEdit){const auto value=FeeRate::Parse(draft);return !value || available(CommitFee{id,*value});}
    return available(CommitLabel{id,std::string(draft)});
}
ItemFacts SubmitButton(const char* widget,const char* command,const char* label,ActionKind kind,Commands& host,const imkit::Theme& theme,bool enabled,const char* reason,PresentationFrame* presentation,std::function<void(bool)> after_semantic={},LiveAvailability live={})
{
    const Action request{command,kind};enabled=enabled && (!live || live()) && (kind==ActionKind::CancelDialog || host.Available(request));
    ImGui::PushID(widget);ImGui::BeginDisabled(!enabled);
    // Disabled primary actions use the neutral control/text pair, keeping
    // readable contrast and a distinct state without changing IDs or commands.
    const bool primary=enabled && (kind==ActionKind::Primary || kind==ActionKind::AcceptDialog);
    const bool pressed=theme::ActionButton(label,primary?imkit::ActionVariant::Primary:imkit::ActionVariant::Secondary,{},theme);
    auto facts=Facts();
    const auto external=presentation?presentation->Interact(label,ax::SemanticRole::Button,ax::SemanticAction::Press|ax::SemanticAction::Focus,!enabled,false,{},reason):std::nullopt;
    facts.requested=pressed;ImGui::EndDisabled();
    if (pressed && enabled) facts.activated=host.Request(Action{command,kind});
    if(external)presentation->Defer(*external,[submit=host.Capture(),command=std::string(command),kind,pressed,accepted=facts.activated,after_semantic](const SemanticRequest&){
        if(pressed)return kind==ActionKind::CancelDialog || accepted;
        const bool result=submit(Action{command,kind});const bool applied=kind==ActionKind::CancelDialog || result;
        if(after_semantic)after_semantic(applied);return applied;
    },[live,pressed,available=host.CaptureAvailability(),request]{return pressed || ((!live || live()) && (request.kind==ActionKind::CancelDialog || available(request)));});
    if (!enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",reason);
    ImGui::PopID();return facts;
}
ItemFacts Field(const char* id,const char* label,const char* hint,Edit& edit,Commit commit,Commands& host,const imkit::Theme& theme,bool enabled,AmountPrivacy privacy={},PresentationFrame* presentation=nullptr,LiveAvailability live={})
{
    enabled=enabled && (!live || live());
    imkit::BeginFormField(id,{label,privacy.hidden?"Reveal amounts to edit.":hint,privacy.hidden?"":edit.error.c_str(),false},{&theme});
    if (privacy.hidden) {
        std::array<char,7> hidden{'H','i','d','d','e','n',0};
        ImGui::BeginDisabled();ImGui::InputText("###hidden",hidden.data(),hidden.size(),ImGuiInputTextFlags_ReadOnly);
        auto facts=Facts();
        if (presentation) presentation->Annotate("###input",label,"Hidden",imkit::accessibility::SemanticRole::TextField,true);
        ImGui::EndDisabled();imkit::EndFormField();return facts;
    }
    ImGui::BeginDisabled(!enabled);
    if (!edit.error.empty()) ImGui::PushStyleColor(ImGuiCol_Border,theme.semantic.error);
    const auto previous_error=edit.error;
    const bool enter=EditInput("###input",edit,ImGuiInputTextFlags_EnterReturnsTrue,commit==CommitAmountEdit || commit==CommitFeeEdit);
    const bool finished=ImGui::IsItemDeactivatedAfterEdit();auto facts=Facts();
    const auto external=presentation?presentation->Interact(label,ax::SemanticRole::TextField,ax::SemanticAction::SetValue|ax::SemanticAction::Focus,!enabled,false,edit.draft(),edit.error,!edit.error.empty()):std::nullopt;
    if(presentation)presentation->BindTextPosition(facts.id,edit.draft(),edit.cursor);
    const bool native_commit=enabled && (enter||finished) && (enter || !edit.attempted || edit.last_attempt!=edit.draft());
    if (!edit.error.empty()) ImGui::PopStyleColor();
    ImGui::EndDisabled();
    if (native_commit) {
        const bool changed=edit.dirty();facts.requested=changed;
        const bool accepted=commit(id,edit,host);facts.activated=changed && accepted;
        edit.attempted=true;edit.last_attempt=edit.draft();
    }
    if(enter && !edit.error.empty() && edit.error!=previous_error){ImGui::SetKeyboardFocusHere(-1);ImGui::SetNavCursorVisible(true);}
    if(external)presentation->Defer(*external,[&edit,commit,id=std::string(id),submit=host.Capture(),native_commit,native_draft=std::string(edit.draft()),native_ok=edit.error.empty(),presentation,widget=facts.id](const SemanticRequest& r){
        if(native_commit)return r.value==native_draft && native_ok;
        if(!edit.SetExternalDraft(r.value)){edit.error="Value exceeds the editor limit.";presentation->FocusOnNextDraw(widget);return false;}
        EndpointCommands endpoint(submit);const bool accepted=commit(id.c_str(),edit,endpoint);
        edit.attempted=true;edit.last_attempt=edit.draft();if(!accepted)presentation->FocusOnNextDraw(widget);return accepted;
    },[live,native_commit,available=host.CaptureAvailability(),commit,id=std::string(id),value=external->value,&edit]{return native_commit || ((!live || live()) && EditCommandAvailable(available,commit,id,value,edit));});
    if (!edit.error.empty()) {imkit::ValidationMessage(edit.error.c_str(),true,imkit::ComponentOptions{&theme});if(presentation)presentation->Annotate("validation-error","Validation error",edit.error,ax::SemanticRole::Alert);}
    if (!enabled) ImGui::TextWrapped("Editing unavailable while the host is busy.");
    imkit::EndFormField();return facts;
}
}
ItemFacts AmountField(const char* id,Edit& e,Commands& h,const imkit::Theme& t,bool enabled,AmountPrivacy p,PresentationFrame* f,LiveAvailability live) { return Field(id,"Amount (BTC)","Exact satoshis; Enter or leave the field to commit.",e,CommitAmountEdit,h,t,enabled,p,f,std::move(live)); }
ItemFacts FeeField(const char* id,Edit& e,Commands& h,const imkit::Theme& t,bool enabled,AmountPrivacy p,PresentationFrame* f,LiveAvailability live) { return Field(id,"Fee rate (sat/vB)","Exact decimal rate; Enter or leave the field to commit.",e,CommitFeeEdit,h,t,enabled,p,f,std::move(live)); }
ItemFacts LabelEditor(const char* id,Edit& e,Commands& h,const imkit::Theme& t,bool enabled,PresentationFrame* f,LiveAvailability live) { return Field(id,"Label","Enter or leave the field to save. Failed drafts stay editable.",e,CommitLabelEdit,h,t,enabled,{},f,std::move(live)); }
ItemFacts CopyableIdentifier(const char* id,std::string_view value,Commands& host,const imkit::Theme& theme,bool enabled,ImFont* monospace,PresentationFrame* presentation,LiveAvailability live)
{
    enabled=enabled && (!live || live()) && host.Available(CopyIdentifier{id,std::string(value)});
    ImGui::PushID(id);ImGui::TextUnformatted("Public identifier");
    std::string text(value);ImGui::SetNextItemWidth(std::max(1.F,ImGui::GetContentRegionAvail().x-ImGui::GetFontSize()*5-16));
    ImGui::PushFont(monospace,theme.typography.monospace);
    ImGui::InputText("###identifier",text.data(),text.size()+1,ImGuiInputTextFlags_ReadOnly);
    if(presentation)presentation->Interact("Public identifier",ax::SemanticRole::TextField,ax::SemanticAction::Focus,false,false,text);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",text.c_str());
    ImGui::PopFont();
    ImGui::SameLine();ImGui::BeginDisabled(!enabled);
    const bool copy=theme::ActionButton("Copy###copy",imkit::ActionVariant::Secondary,{},theme);
    auto facts=Facts();const auto external=presentation?presentation->Interact("Copy public identifier",ax::SemanticRole::Button,ax::SemanticAction::Press|ax::SemanticAction::Focus,!enabled):std::nullopt;
    facts.requested=copy;ImGui::EndDisabled();
    if (copy && enabled) facts.activated=host.Request(CopyIdentifier{id,text});
    if(external)presentation->Defer(*external,[submit=host.Capture(),id=std::string(id),text,copy,accepted=facts.activated](const SemanticRequest&){return copy?accepted:submit(CopyIdentifier{id,text});},[live,copy,available=host.CaptureAvailability(),id=std::string(id),text]{return copy || ((!live || live()) && available(CopyIdentifier{id,text}));});
    ImGui::PopID();return facts;
}
ItemFacts ActionButton(const char* id,const char* label,ActionKind kind,Commands& host,const imkit::Theme& theme,bool enabled,const char* reason,PresentationFrame* presentation,LiveAvailability live)
{
    return SubmitButton(id,id,label,kind,host,theme,enabled,reason,presentation,{},std::move(live));
}
void StatusBanner(const char* id,const FreshnessStatus& status,const imkit::Theme& theme,PresentationFrame* presentation)
{
    const char* freshness="Unknown";
    switch (status.freshness) { case Freshness::Live:freshness="Live";break;case Freshness::Cached:freshness="Cached";break;case Freshness::Stale:freshness="Stale";break;case Freshness::Offline:freshness="Offline";break;case Freshness::Unknown:break; }
    const std::string title=std::string(freshness)+(status.failed?" / failed":status.incomplete?" / incomplete":"");
    const std::string detail=status.detail+(status.observed.empty()?"":" / observed "+status.observed);
    const auto tokens=theme::Wallet(theme.scheme==imkit::ColorScheme::Dark);
    const auto tone=status.failed?tokens.error:status.incomplete||status.freshness==Freshness::Stale?tokens.warning:theme::StatusColors{tokens.text,tokens.alternate};
    // Pinned PersistentBanner forces a .10 accent overlay and cannot express
    // the specified opaque foreground/background pairs. Public composition
    // keeps those exact roles and a text alternative without a vendor patch.
    const float pad=12*theme.typography.body/14.F;
    const float width=std::max(1.F,ImGui::GetContentRegionAvail().x),wrap=std::max(1.F,width-2*pad);
    const float height=ImGui::CalcTextSize(title.c_str(),nullptr,false,wrap).y+
        ImGui::CalcTextSize(detail.c_str(),nullptr,false,wrap).y+2*pad+ImGui::GetStyle().ItemSpacing.y;
    const auto origin=ImGui::GetCursorScreenPos();ImGui::PushID(id);ImGui::BeginGroup();ImGui::Dummy({width,height});
    ImGui::GetWindowDrawList()->AddRectFilled(origin,{origin.x+width,origin.y+height},ImGui::GetColorU32(tone.background),theme.radius.control);
    ImGui::SetCursorScreenPos({origin.x+pad,origin.y+pad});ImGui::PushStyleColor(ImGuiCol_Text,tone.foreground);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+wrap);ImGui::TextUnformatted(title.c_str());
    ImGui::SetCursorScreenPos({origin.x+pad,ImGui::GetCursorScreenPos().y});ImGui::TextUnformatted(detail.c_str());
    ImGui::PopTextWrapPos();ImGui::PopStyleColor();ImGui::SetCursorScreenPos({origin.x,origin.y+height});ImGui::Dummy({0,0});ImGui::EndGroup();
    if (presentation) presentation->Annotate("status","Freshness",title+" / "+detail,status.failed?imkit::accessibility::SemanticRole::Alert:imkit::accessibility::SemanticRole::Status,false,true,status.busy);
    ImGui::PopID();
}
std::vector<RowFacts> SelectionTable(const char* id,std::span<const Row> rows,Selection& state,Commands& host,const imkit::Theme& theme,float height,AmountPrivacy privacy,PresentationFrame* presentation,RowAvailability live)
{
    std::vector<RowFacts> facts;state.focused.reset();
    std::set<uint64_t> keys;
    for (const auto& row:rows) if (!row.id || !keys.insert(row.id).second) {
        state.invalid_keys=true;
        state.error="Observation keys must be unique and nonzero; table unavailable.";
        ImGui::TextColored(theme.semantic.error,"%s",state.error.c_str());return facts;
    }
    if (state.invalid_keys) { state.invalid_keys=false;state.error.clear(); }
    ImGui::PushID(id);
    if (ImGui::BeginTable("###table",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_Borders|ImGuiTableFlags_ScrollX|ImGuiTableFlags_ScrollY,{0,height})) {
        if(presentation)presentation->Group("semantic-grid","Public observations",ax::SemanticRole::Grid,ImGui::GetWindowDrawList()->GetClipRectMin(),ImGui::GetWindowDrawList()->GetClipRectMax());
        ImGui::TableSetupColumn("Label");ImGui::TableSetupColumn("Amount (BTC)");ImGui::TableHeadersRow();
        for (const auto& row:rows) {
            ImGui::PushID(std::to_string(row.id).c_str());ImGui::TableNextRow();ImGui::TableNextColumn();const bool row_enabled=row.enabled && (!live || live(row.id));ImGui::BeginDisabled(!row_enabled);
            const std::string label=row.label+(row.enabled?"":" ("+row.disabled_reason+")");
            const auto origin=ImGui::GetCursorScreenPos();
            const float text_height=ImGui::CalcTextSize(label.data(),label.data()+label.size(),false).y;
            // The interactive identity never contains user text. Selectable's
            // label parser treats ## as markup; overlay literal text through
            // the public text API, which also measures the full cell width.
            const bool activate=ImGui::Selectable("###select",state.selected.contains(row.id),ImGuiSelectableFlags_SpanAllColumns,{0,text_height});
            const auto item=Facts(activate);
            const auto external=presentation?presentation->Interact(row.label.c_str(),ax::SemanticRole::Row,ax::SemanticAction::Select|ax::SemanticAction::Focus,!row_enabled,state.selected.contains(row.id),{},row.enabled?"":row.disabled_reason):std::nullopt;
            if (presentation)presentation->Parent(item.id);
            if (item.focused) state.focused=row.id;
            ImGui::SetCursorScreenPos(origin);
            ImGui::TextUnformatted(label.data(),label.data()+label.size());
            if(presentation)presentation->Annotate("literal-label","Public label",label,ax::SemanticRole::Cell,!row_enabled);
            if (!row.enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",row.disabled_reason.c_str());
            facts.push_back({row.id,item.id,item.focused,state.selected.contains(row.id),!row_enabled,item.visible});
            if (activate && row_enabled) {
                auto next=state.selected;
                if (ImGui::GetIO().KeyCtrl) { if (!next.erase(row.id)) next.insert(row.id); }
                else next={row.id};
                if (host.Request(SelectRows{id,{next.begin(),next.end()}})) { state.selected=std::move(next);state.error.clear(); }
                else state.error="Selection request failed; prior selection retained.";

            }
            if(external)presentation->Defer(*external,[&state,submit=host.Capture(),id=std::string(id),key=row.id,ctrl=ImGui::GetIO().KeyCtrl,activate,native_ok=state.error.empty()](const SemanticRequest&){
                if(activate)return native_ok;auto next=state.selected;if(ctrl){if(!next.erase(key))next.insert(key);}else next={key};
                const bool accepted=submit(SelectRows{id,{next.begin(),next.end()}});if(accepted){state.selected=std::move(next);state.error.clear();}else state.error="Selection request failed; prior selection retained.";return accepted;
            },[live,key=row.id,activate]{return activate || !live || live(key);});
            ImGui::EndDisabled();ImGui::TableNextColumn();
            const auto amount=privacy.Present(row.amount);ImGui::TextUnformatted(amount.text.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",amount.tooltip.c_str());
            if (presentation){presentation->Annotate("amount","Observation amount (BTC)",amount.semantic,imkit::accessibility::SemanticRole::Cell,!row_enabled);presentation->EndGroup();}
            ImGui::PopID();
        }
        if(presentation)presentation->EndGroup();
        ImGui::EndTable();
    }
    if (!state.error.empty()) ImGui::TextColored(theme.semantic.error,"%s",state.error.c_str());
    ImGui::PopID();return facts;
}
ItemFacts CopyableAmount(const char* id,Money value,Commands& host,const imkit::Theme& theme,AmountPrivacy privacy,PresentationFrame* presentation,bool enabled,LiveAvailability live)
{
    enabled=enabled && (!live || live()) && host.Available(CopyAmount{id,value});
    const auto amount=privacy.Present(value);ImGui::PushID(id);
    ImGui::Text("Amount (BTC): %s",amount.text.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",amount.tooltip.c_str());
    if (presentation) presentation->Annotate("value","Copyable amount (BTC)",amount.semantic);
    ImGui::SameLine();ImGui::BeginDisabled(!enabled || !amount.copy);
    const bool copy=theme::ActionButton("Copy amount###copy",imkit::ActionVariant::Secondary,{},theme);
    auto facts=Facts();const auto external=presentation?presentation->Interact("Copy amount",ax::SemanticRole::Button,ax::SemanticAction::Press|ax::SemanticAction::Focus,!enabled || !amount.copy):std::nullopt;
    facts.requested=copy;ImGui::EndDisabled();
    if (copy && enabled && amount.copy) facts.activated=host.Request(CopyAmount{id,value});
    if(external)presentation->Defer(*external,[submit=host.Capture(),id=std::string(id),value,copy,accepted=facts.activated](const SemanticRequest&){return copy?accepted:submit(CopyAmount{id,value});},[live,copy,available=host.CaptureAvailability(),id=std::string(id),value]{return copy || ((!live || live()) && available(CopyAmount{id,value}));});
    ImGui::PopID();return facts;
}
void AmountSummary(const char* id,std::span<const Row> rows,const imkit::Theme& theme,AmountPrivacy privacy,PresentationFrame* presentation)
{
    ImGui::PushID(id);std::optional<Money> total=Money::Satoshis(0);std::vector<Money> values;
    for (const auto& row:rows) { if (total) total=total->Add(row.amount);values.push_back(row.amount); }
    const auto amount=privacy.Present(total);ImGui::Text("Observed total (BTC): %s",amount.text.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",amount.tooltip.c_str());
    if (presentation) presentation->Annotate("total","Observed total (BTC)",amount.semantic);
    ImGui::TextUnformatted("Includes frozen and unavailable observations; not a spendable balance.");
    const auto chart=privacy.Chart(values);
    if (presentation) presentation->RecordChart(chart.percentages);
    if (!chart.percentages.empty()) {
        std::vector<imkit::ChartDatum> bars;std::deque<std::string> labels;
        for (size_t i=0;i<chart.percentages.size();++i) {
            labels.push_back(std::to_string(rows[i].id));bars.push_back({labels.back().c_str(),chart.percentages[i],theme.semantic.textSecondary});
        }
        imkit::BarChart("chart",{bars},{{0,64*theme.typography.body/14.F},imkit::Orientation::Horizontal,100,false,false,"%.1f%%"},{&theme});
    }
    ImGui::TextWrapped("%s",chart.alternative.c_str());
    if (presentation) presentation->Annotate("chart-alternative","Amount chart",chart.alternative);
    ImGui::PopID();
}
void ReviewDetails(const char* id,Money value,std::string_view destination,AmountPrivacy privacy,PresentationFrame* presentation)
{
    const auto review=DeliberateReview(value,destination,privacy);ImGui::PushID(id);
    ImGui::Text("Review amount (BTC): %s",review.amount.text.c_str());
    if (presentation) presentation->Annotate("amount","Review amount (BTC)",review.amount.semantic);
    if (review.reveal_required) ImGui::TextWrapped("Turn off Hide amounts to review the exact amount.");
    ImGui::TextWrapped("Full destination: %s",review.destination.c_str());
    if (presentation) presentation->Annotate("destination","Full destination",review.destination);
    ImGui::PopID();
}
ItemFacts DialogLauncher(const char* id,const char* label,Dialog& dialog,Commands& host,const imkit::Theme& theme,PresentationFrame* presentation)
{
    if (dialog.state.restoreFocus) { ImGui::SetKeyboardFocusHere();dialog.state.restoreFocus=false; }
    const auto facts=SubmitButton(id,id,label,ActionKind::OpenDialog,host,theme,true,"",presentation,[&dialog](bool accepted){if(accepted){dialog.state.open=true;dialog.state.initialFocus=true;dialog.error.clear();}});
    if (facts.activated) { dialog.state.open=true;dialog.state.initialFocus=true;dialog.error.clear(); }
    return facts;
}
ItemFacts DialogShell(const char* id,const char* message,Dialog& dialog,Commands& host,const imkit::Theme& theme,bool valid,PresentationFrame* presentation,LiveAvailability live)
{
    valid=valid && (!live || live());
    ItemFacts facts;
    if(dialog.close_pending){if(ImGui::BeginPopupModal(id)){ImGui::CloseCurrentPopup();ImGui::EndPopup();}dialog.close_pending=false;dialog.state.open=false;dialog.state.restoreFocus=true;return facts;}
    const bool was_open=dialog.state.open,initial=dialog.state.initialFocus;
    if (was_open) {
        const auto* vp=ImGui::GetMainViewport();
        ImGui::SetNextWindowSize({std::min(500.F*theme.typography.body/14.F,vp->WorkSize.x-48),0},ImGuiCond_Always);
    }
    if (imkit::BeginDialog(id,dialog.state,{&theme})) {
        if(presentation){presentation->DialogOnly();const auto p=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();presentation->Group("semantic-dialog","Public label review",ax::SemanticRole::Dialog,p,{p.x+size.x,p.y+size.y});}
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x);ImGui::TextUnformatted(message);ImGui::PopTextWrapPos();if(presentation)presentation->Annotate("message","Review information",message);
        if (!dialog.error.empty()) { ImGui::PushStyleColor(ImGuiCol_Text,theme.semantic.error);ImGui::TextWrapped("%s",dialog.error.c_str());ImGui::PopStyleColor(); }
        if (initial) ImGui::SetKeyboardFocusHere();
        const auto cancel=SubmitButton("cancel",id,"Cancel",ActionKind::CancelDialog,host,theme,true,"",presentation,[&dialog](bool){dialog.close_pending=true;},[&dialog]{return dialog.state.open && !dialog.close_pending;});
        ImGui::SameLine();
        facts=SubmitButton("accept",id,"Apply",ActionKind::AcceptDialog,host,theme,valid,"Correct the draft before applying.",presentation,[&dialog](bool accepted){if(accepted)dialog.close_pending=true;else dialog.error="Request rejected. The dialog and draft are retained.";},[&dialog,live]{return dialog.state.open && !dialog.close_pending && (!live || live());});
        // The shell emits one typed acceptance, never wallet approval.
        if (facts.activated) {
            dialog.state.open=false;dialog.state.restoreFocus=true;ImGui::CloseCurrentPopup();
        }
        if (facts.requested && !facts.activated) dialog.error="Request rejected. The dialog and draft are retained.";
        if (cancel.requested) { dialog.state.open=false;dialog.state.restoreFocus=true;ImGui::CloseCurrentPopup(); }
        if(presentation)presentation->EndGroup();imkit::EndDialog();
    } else if (was_open && !dialog.state.open) host.Request(Action{id,ActionKind::CancelDialog});
    return facts;
}
}
