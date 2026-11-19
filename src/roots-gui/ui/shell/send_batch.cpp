// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "services/send_analysis.h"
#include "ui/theme/theme.h"

#include <algorithm>
#include <cfloat>
#include <sodium.h>

namespace roots::gui::ui::shell {
namespace {
namespace c=components;
using c::ax::SemanticAction;
using c::ax::SemanticRole;
constexpr size_t ROWS_PER_PAGE=12;

std::optional<BatchBinding> Binding(const State& state,app::Requests& host)
{
    const auto* draft=ActiveSendDraft(state);
    if (!draft || state.lock_panel.setup) return {};
    const auto* connection=host.Connection();
    return BatchBinding{draft->id,connection ? connection->Session() : 0,connection ? connection->Revision() : 0,
        draft->draft_revision,state.recovery ? state.recovery->Gate().Epoch() : LockEpoch{},*draft->SendWallet(),
        connection && connection->Offline()};
}
bool Unobstructed(const State& state)
{
    return state.actions.kind==DocumentDialog::None && !state.local_document.dialog.open
        && !state.recovery_dialog.open && !state.onboarding.dialog.open && !state.server.dialog.open
        && !state.close_tab_popup && !state.close_document_popup
        && (!c::native_input || !c::native_input->NativeDialogActive());
}
BatchEditor* Current(State& state,app::Requests& host,uint64_t generation,bool idle=true)
{
    if (!state.batch || state.batch->generation!=generation || Binding(state,host)!=state.batch->binding
        || !Unobstructed(state) || (idle && (state.batch_read || state.batch->file_dialog))) return nullptr;
    return &*state.batch;
}
void Wipe(std::string& value)
{
    if (!value.empty()) sodium_memzero(value.data(),value.size());
    value.clear();
}
void ForgetValidation(BatchEditor& editor)
{
    if (editor.checked && editor.checked->ready) WipeRecipients(*editor.checked->ready);
    editor.checked.reset();
}
void Changed(BatchEditor& editor)
{
    editor.selected_index.reset();
    ForgetValidation(editor);
    editor.status.clear();
    ++editor.revision;
}
void Text(State& state,const char* key,const char* name,std::string_view value,SemanticRole role=SemanticRole::Text)
{
    ImGui::TextWrapped("%.*s",int(value.size()),value.data());
    state.presentation.Annotate(key,name,value,role);
}
void Item(Facts& facts,const std::string& key,bool pressed,bool disabled)
{
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    facts.batch[key]={{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),disabled,
        pressed,pressed,ImGui::IsItemVisible()};
}
bool Button(State& state,Facts& facts,const imkit::Theme& theme,const std::string& key,const char* label,
    bool enabled,std::function<bool()> action,bool primary=false,std::string_view name={},std::string_view description={})
{
    ImGui::PushID(key.c_str());
    ImGui::BeginDisabled(!enabled);
    const bool pressed=theme::ActionButton(label,primary ? imkit::ActionVariant::Primary : imkit::ActionVariant::Secondary,{},theme);
    Item(facts,key,pressed,!enabled);
    const auto semantic_name=name.empty() ? std::string(label) : std::string(name);
    const auto request=state.presentation.Interact(semantic_name.c_str(),SemanticRole::Button,
        SemanticAction::Press|SemanticAction::Focus,!enabled,false,{},description,!description.empty());
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted=enabled && pressed && action();
    if (request) state.presentation.Defer(*request,[enabled,pressed,accepted,action=std::move(action)](const auto&){
        return enabled && (pressed ? accepted : action());
    });
    return accepted;
}
std::string& Field(SendRecipient& row,RecipientField field)
{
    if (field==RecipientField::Destination) return row.pay_to;
    if (field==RecipientField::Amount) return row.amount;
    return row.label;
}
bool Edit(State& state,app::Requests& host,uint64_t token,uint64_t id,RecipientField field,std::string_view text,bool external)
{
    auto* editor=Current(state,host,token);
    if (!editor || text.size()>MAX_BATCH_CELL || !rpc::ValidUtf8(text) || text.find('\0')!=std::string_view::npos) return false;
    const auto row=std::ranges::find(editor->work.rows,id,&SendRecipient::id);
    if (row==editor->work.rows.end() || Field(*row,field)==text) return false;
    Field(*row,field)=text;
    editor->work.selected=id;
    if (external) {
        if (editor->external) Wipe(editor->external->text);
        editor->external=BatchEditor::External{id,field,std::string(text)};
    }
    Changed(*editor);
    return true;
}
void DrawField(State& state,app::Requests& host,Facts& facts,const SendRecipient& row,RecipientField field,size_t index,
    std::string_view error,bool busy)
{
    auto& view=*state.batch;
    const auto token=view.generation,id=row.id;
    const char* name=field==RecipientField::Destination ? "address" : field==RecipientField::Amount ? "amount" : "label";
    const auto key=std::string(name)+"-"+std::to_string(id);
    const auto label="Row "+std::to_string(index+1)+" "+name;
    const auto& current=field==RecipientField::Destination ? row.pay_to : field==RecipientField::Amount ? row.amount : row.label;
    const bool text_visible=current.size()<=MAX_RECIPIENT_LABEL
        && std::ranges::none_of(current,[](unsigned char ch){return ch<32 || ch==127;});
    std::array<char,MAX_BATCH_CELL+1> buffer{};
    std::copy_n(current.begin(),std::min(current.size(),MAX_BATCH_CELL),buffer.begin());
    std::optional<std::string> external;
    if (view.external && view.external->row==id && view.external->field==field) {
        external=std::move(view.external->text);
        view.external.reset();
    }
    c::TextPosition cursor;
    ImGui::PushID(key.c_str());
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::BeginDisabled(busy);
    const bool edited=c::TextInput("##batch-field",name,buffer.data(),buffer.size(),external,0,&cursor,true);
    Item(facts,key,edited,busy);
    const auto request=state.presentation.Interact(label.c_str(),SemanticRole::TextField,SemanticAction::SetValue|SemanticAction::Focus,
        busy,false,text_visible ? std::string_view(current) : std::string_view{},error,!error.empty());
    if (text_visible) state.presentation.BindTextPosition(ImGui::GetItemID(),current,cursor);
    if (!busy && ImGui::IsItemActive() && view.work.selected!=id) {
        view.work.selected=id;
        ++view.revision;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    if (edited) Edit(state,host,token,id,field,buffer.data(),false);
    if (request) state.presentation.Defer(*request,[&state,&host,token,id,field](const auto& action){
        return Edit(state,host,token,id,field,action.value,true);
    });
    sodium_memzero(buffer.data(),buffer.size());
}
bool Apply(State& state,app::Requests& host,uint64_t token)
{
    auto* editor=Current(state,host,token);
    if (!editor) return false;
    auto validation=ValidateBatch(editor->work,protocol::DocumentNetwork(editor->binding.wallet.network));
    if (!validation.ready) return false;
    auto* draft=state.documents.Active();
    if (!draft || draft->draft_revision.value==UINT64_MAX) {
        WipeRecipients(*validation.ready);
        return false;
    }
    if (!SamePayments(draft->recipients,*validation.ready)) {
        for (auto& row:validation.ready->rows) {
            const auto* old=draft->recipients.Find(row.id);
            if (old && old->maximum && row.maximum && old->address==row.address) row.amount=old->amount;
        }
        draft->recipients=std::move(*validation.ready);
        ++draft->draft_revision.value;
        if (state.funding.scope && state.funding.scope->document==draft->id) {
            if (auto* funding=host.Funding()) funding->Cancel();
            RetireFunding(state);
        }
        state.notice="Batch applied to Send.";
    } else {
        WipeRecipients(*validation.ready);
        state.notice="The staged payments match the Send draft.";
    }
    RetireSendView(state);
    RestoreDocumentFocus(state,draft->id);
    RetireBatch(state);
    return true;
}
void Files(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme)
{
    auto& panel=*state.batch;
    const auto token=panel.generation;
    const bool idle=!state.batch_read && !panel.file_dialog;
    const bool local=host.LocalDocuments()!=nullptr;
    c::TextPosition cursor;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::BeginDisabled(!idle || !local);
    const bool edit=c::TextInput("##batch-file","Absolute local CSV path",panel.path.data(),panel.path.size(),panel.path_edit,0,&cursor,true);
    if (edit) {++panel.revision;panel.status.clear();}
    Item(facts,"csv-path",edit,!idle || !local);
    const auto path=state.presentation.Interact("Local CSV path",SemanticRole::TextField,SemanticAction::SetValue|SemanticAction::Focus,
        !idle || !local,false,panel.path.data());
    state.presentation.BindTextPosition(ImGui::GetItemID(),panel.path.data(),cursor);
    ImGui::EndDisabled();
    if (path) state.presentation.Defer(*path,[&state,&host,token](const auto& action){
        auto* editor=Current(state,host,token);
        if (!editor || !host.LocalDocuments() || !c::SetExternalBuffer(editor->path,editor->path_edit,action.value)) return false;
        ++editor->revision;
        editor->status.clear();
        return true;
    });
    const bool chooser=c::native_input && c::native_input->FileChooserAvailable();
    const auto browse=[&state,&host,token](bool save){
        auto* editor=Current(state,host,token);
        auto* native=c::native_input;
        if (!editor || !host.LocalDocuments() || !native || !native->FileChooserAvailable()) return false;
        editor->file_dialog=native->ChooseFile(editor->path.data(),save);
        if (!editor->file_dialog) return false;
        ++editor->revision;
        return true;
    };
    Button(state,facts,theme,"browse-csv","Browse CSV",idle && local && chooser,[browse]{return browse(false);});
    ImGui::SameLine();
    Button(state,facts,theme,"browse-csv-save","Choose export path",idle && local && chooser,[browse]{return browse(true);});
    Button(state,facts,theme,"load","Load CSV",idle && local && ValidLocalDocumentPath(panel.path.data()),[&state,&host,token]{
        auto* editor=Current(state,host,token);
        if (!editor || !host.LocalDocuments() || !ValidLocalDocumentPath(editor->path.data())) return false;
        try {
            state.batch_read=std::make_unique<BatchFileRead>(editor->path.data(),editor->work.next);
        } catch (...) {
            editor->status="The local read could not be started.";
            ++editor->revision;
            return false;
        }
        editor->status="Reading CSV into staged rows…";
        state.batch_read_generation=token;
        state.batch_read_revision=++editor->revision;
        return true;
    });
    ImGui::SameLine();
    Button(state,facts,theme,"export","Export CSV",idle && local && panel.checked && panel.checked->ready
        && ValidLocalDocumentPath(panel.path.data()),[&state,&host,token]{
        auto* editor=Current(state,host,token);
        if (!editor || !host.LocalDocuments()) return false;
        auto valid=ValidateBatch(editor->work,protocol::DocumentNetwork(editor->binding.wallet.network));
        if (!valid.ready) return false;
        auto csv=WriteBatchCsv(*valid.ready);
        WipeRecipients(*valid.ready);
        if (!csv) return false;
        const auto result=SavePublicFile(editor->path.data(),*csv,MAX_BATCH_CSV,true);
        Wipe(*csv);
        editor->status=result.empty() ? "CSV saved as a new local file." : result;
        ++editor->revision;
        return result.empty();
    });
    Text(state,"csv-schema","CSV format","UTF-8 CSV uses Address, Amount, Label, Unit. Amount is a number or Max. Unit is BTC or sat.");
}
void Grid(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme,bool busy)
{
    auto& editor=*state.batch;
    const auto token=editor.generation;
    const auto count=editor.work.rows.size();
    const auto first=editor.page*ROWS_PER_PAGE;
    const auto last=std::min(count,first+ROWS_PER_PAGE);
    if (ImGui::BeginTable("batch-grid",5,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV|
            ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchSame,{ImGui::GetContentRegionAvail().x,0})) {
        ImGui::TableSetupColumn("Row",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFontSize()*2.4f);
        ImGui::TableSetupColumn("Address",ImGuiTableColumnFlags_WidthStretch,3);
        ImGui::TableSetupColumn("Amount",ImGuiTableColumnFlags_WidthStretch,1.45f);
        ImGui::TableSetupColumn("Label",ImGuiTableColumnFlags_WidthStretch,2.4f);
        ImGui::TableSetupColumn("Unit",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFontSize()*3.4f);
        ImGui::TableHeadersRow();
        for (size_t i=first;i<last;++i) {
            ++facts.work.batch_drawn;
            const auto& row=editor.work.rows[i];
            const auto id=row.id;
            const auto issue=!editor.checked || editor.checked->rows.empty() ? BatchIssue{} : editor.checked->rows[i];
            ImGui::PushID(std::to_string(id).c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(std::to_string(i+1).c_str(),editor.work.selected==id,0)
                && !busy && Current(state,host,token)) {editor.work.selected=id;++editor.revision;}
            const auto label="Select row "+std::to_string(i+1);
            const auto select=state.presentation.Interact(label.c_str(),SemanticRole::Button,SemanticAction::Select|SemanticAction::Focus,
                busy,editor.work.selected==id);
            if (select) state.presentation.Defer(*select,[&state,&host,token,id](const auto&){
                auto* current=Current(state,host,token);
                if (!current || !current->work.Select(id)) return false;
                ++current->revision;return true;
            });
            ImGui::TableNextColumn();
            DrawField(state,host,facts,row,RecipientField::Destination,i,issue.address,busy);
            ImGui::TableNextColumn();
            DrawField(state,host,facts,row,RecipientField::Amount,i,issue.amount,busy);
            ImGui::TableNextColumn();
            DrawField(state,host,facts,row,RecipientField::Label,i,issue.label,busy);
            ImGui::TableNextColumn();
            const auto unit=row.unit==RecipientUnit::BTC ? "BTC" : row.unit==RecipientUnit::Sats ? "sat" : "Set";
            Button(state,facts,theme,"unit-"+std::to_string(id),unit,!busy,[&state,&host,token,id]{
                auto* current=Current(state,host,token);
                if (!current) return false;
                const auto row=std::ranges::find(current->work.rows,id,&SendRecipient::id);
                if (row==current->work.rows.end()) return false;
                const bool known=row->unit==RecipientUnit::BTC || row->unit==RecipientUnit::Sats;
                const auto old=known ? ParseRecipientAmount(row->amount,row->unit) : std::nullopt;
                row->unit=row->unit==RecipientUnit::BTC ? RecipientUnit::Sats : RecipientUnit::BTC;
                if (old) row->amount=row->unit==RecipientUnit::BTC ? old->btc() : std::to_string(old->sats());
                else if (known && row->amount!="Max") Wipe(row->amount);
                if (current->external) Wipe(current->external->text);
                current->external=BatchEditor::External{id,RecipientField::Amount,row->amount};
                Changed(*current);
                return true;
            },false,"Row "+std::to_string(i+1)+" unit ("+unit+")",issue.unit);
            ImGui::PopID();
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)
            && (ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiKey_Tab,ImGuiInputFlags_RouteFocused)
                || ImGui::Shortcut(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiKey_Tab,ImGuiInputFlags_RouteFocused))) {
            RestoreDocumentFocus(state,editor.binding.document);
            ++state.grid_exits;
            facts.grid_escape=true;
        }
        ImGui::EndTable();
    }
    if (count<=ROWS_PER_PAGE) return;
    const auto change=[&state,&host,token,count](bool forward){
        auto* current=Current(state,host,token);
        if (!current || (forward ? (current->page+1)*ROWS_PER_PAGE>=count : current->page==0)) return false;
        current->page=forward ? current->page+1 : current->page-1;
        current->work.selected=current->work.rows.at(current->page*ROWS_PER_PAGE).id;
        ++current->revision;
        return true;
    };
    Text(state,"batch-page","Visible payment rows","Rows "+std::to_string(first+1)+"–"+std::to_string(last)+" of "+std::to_string(count));
    Button(state,facts,theme,"earlier","Earlier rows",!busy && first>0,[change]{return change(false);});
    ImGui::SameLine();
    Button(state,facts,theme,"later","Later rows",!busy && last<count,[change]{return change(true);});
}
void Review(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme,bool busy)
{
    auto& panel=*state.batch;
    if (!panel.checked) return;
    const auto& result=*panel.checked;
    if (!result.error.empty()) Text(state,"batch-error","Batch validation",result.error,SemanticRole::Alert);
    if (!panel.selected_index || panel.selected_index->first!=panel.work.selected) {
        const auto selected=std::ranges::find(panel.work.rows,panel.work.selected,&SendRecipient::id);
        panel.selected_index=std::pair{panel.work.selected,size_t(selected-panel.work.rows.begin())};
    }
    const auto index=panel.selected_index->second;
    if (index<result.rows.size()) {
        const auto& issue=result.rows[index];
        std::string detail;
        for (const auto* field:{&issue.address,&issue.amount,&issue.label,&issue.unit}) if (!field->empty()) {
            if (!detail.empty()) detail+=" ";
            detail+=*field;
        }
        if (!detail.empty()) Text(state,"batch-row-errors","Selected row errors","Row "+std::to_string(index+1)+". "+detail,SemanticRole::Alert);
        if (issue.reference_dust) Text(state,"batch-dust","Dust reference",
            "Selected row is below the reference dust threshold. Current relay policy is Unknown.");
    }
    if (result.errors) {
        Text(state,"batch-errors","Invalid payment rows",std::to_string(result.errors)+" rows need correction.");
        Button(state,facts,theme,"first-error","Show first issue",!busy,[&state,&host,token=panel.generation]{
            auto* current=Current(state,host,token);
            if (!current || !current->checked) return false;
            const auto& rows=current->checked->rows;
            const auto at=std::ranges::find_if(rows,&BatchIssue::Invalid);
            if (at==rows.end()) return false;
            const size_t index=at-rows.begin();
            current->page=index/ROWS_PER_PAGE;
            current->work.selected=current->work.rows[index].id;
            ++current->revision;
            return true;
        });
    } else if (result.fixed) {
        Text(state,"batch-total","Exact fixed recipient total","Fixed total "+result.fixed->btc()+" BTC · "
            +std::to_string(result.fixed->sats())+" sats");
        if (result.maximum)
            Text(state,"batch-max","Batch Max","Max is calculated only in a fresh funding preview.");
    }
    if (panel.work.rows.size()>MAX_FUNDING_OUTPUTS)
        Text(state,"batch-limit","Funding limit","This draft exceeds the current funding limit of 64 recipients.");
}
void DrawBatch(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme)
{
    auto& panel=*state.batch;
    const auto token=panel.generation;
    const bool busy=state.batch_read || panel.file_dialog;
    Text(state,"batch-heading","Send to Many","Send to Many");
    if (Button(state,facts,theme,"cancel","Cancel",true,[&state,&host,token]{
        const auto* current=Current(state,host,token,false);
        if (!current) return false;
        RestoreDocumentFocus(state,current->binding.document);
        RetireBatch(state);
        return true;
    })) return;
    ImGui::SameLine();
    if (!panel.checked) {
        facts.work.batch_validated=panel.work.rows.size();
        panel.checked=ValidateBatch(panel.work,protocol::DocumentNetwork(panel.binding.wallet.network));
    }
    if (Button(state,facts,theme,"apply","Apply batch",!busy && panel.checked->ready
        && panel.binding.revision.value!=UINT64_MAX,[&state,&host,token]{return Apply(state,host,token);},true)) return;
    ImGui::SameLine();
    Button(state,facts,theme,"files",panel.file_panel ? "Hide CSV files" : "CSV files",!busy,[&state,&host,token]{
        auto* current=Current(state,host,token);
        if (!current) return false;
        current->file_panel=!current->file_panel;
        ++current->revision;
        return true;
    });
    if (panel.file_panel) Files(state,host,facts,theme);
    if (!panel.status.empty()) Text(state,"batch-status","Batch operation status",panel.status);
    Grid(state,host,facts,theme,busy);
    Button(state,facts,theme,"add","Add row",!busy && panel.work.rows.size()<MAX_SEND_RECIPIENTS,[&state,&host,token]{
        auto* current=Current(state,host,token);
        DraftRevision local;
        if (!current || !current->work.Add(local)) return false;
        current->page=(current->work.rows.size()-1)/ROWS_PER_PAGE;
        Changed(*current);
        return true;
    });
    ImGui::SameLine();
    Button(state,facts,theme,"remove","Remove selected row",!busy && panel.work.rows.size()>1,
        [&state,&host,token,row=panel.work.selected]{
        auto* current=Current(state,host,token);
        DraftRevision local;
        if (!current || current->work.selected!=row || !current->work.Remove(row,local)) return false;
        current->page=std::min(current->page,(current->work.rows.size()-1)/ROWS_PER_PAGE);
        Changed(*current);
        return true;
    });
    Review(state,host,facts,theme,busy);
}
std::string Counts(const std::map<std::string,size_t>& scripts)
{
    std::string output;
    for (const auto& [kind,count]:scripts) {
        if (!output.empty()) output+=" · ";
        output+=std::to_string(count)+" "+kind;
    }
    return output.empty() ? "Unknown" : output;
}
void Analysis(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme)
{
    const auto binding=*state.send_analysis;
    if (Button(state,facts,theme,"analysis-back","Back to Send",true,[&state,&host,binding]{
        if (Binding(state,host)!=binding || state.send_analysis!=binding || !Unobstructed(state)) return false;
        state.send_analysis.reset();return true;
    })) return;
    Text(state,"analysis-heading","Transaction analysis","Transaction analysis");
    const auto current=CurrentFundingPreview(state,host);
    const HistoryLedger* history=nullptr;
    if (current && state.funding.scope && host.Refresh()) {
        const auto view=host.Refresh()->View();
        if (view.identity && *view.identity==state.funding.scope->wallet && view.ledger && view.chain_current
            && view.freshness[2]==Freshness::Live && view.history_sequence==state.funding.scope->history_sequence
            && view.generation==state.funding.scope->generation && view.ledger->Tip()==state.funding.scope->tip)
            history=view.ledger;
    }
    const auto summary=current ? AnalyzeSend(*current,history) : std::nullopt;
    if (!summary) {
        Text(state,"analysis-unknown","Analysis availability","Address reuse, input linkage and script mix are Unknown until a current funding preview is available.");
        return;
    }
    Text(state,"analysis-source","Analysis source","Current verified unsigned preview");
    const auto& view=*summary;
    if (view.seen_recipients && *view.seen_recipients) {
        Text(state,"analysis-reuse","Address reuse",std::to_string(*view.seen_recipients)+" of "
            +std::to_string(view.recipients)+" recipient scripts match earlier outputs in this wallet's current history.");
    } else if (view.complete_history) {
        Text(state,"analysis-reuse","Address reuse","No matching recipient script in this wallet's current history.");
    } else Text(state,"analysis-reuse","Address reuse","Unknown. Current wallet history does not establish address reuse.");
    Text(state,"analysis-linkage","Input linkage",std::to_string(view.inputs)+" inputs from "+std::to_string(view.origins)
        +" previous transactions. "+(view.inputs>1 ? "This transaction would publicly link these inputs." : "This preview has no co-spent inputs."));
    Text(state,"analysis-inputs","Input script mix","Inputs: "+Counts(view.input_scripts));
    Text(state,"analysis-outputs","Output script mix","Outputs: "+Counts(view.output_scripts));
    Text(state,"analysis-coverage","Analysis limits","Wallet history covers this wallet's observations. These facts do not establish anonymity.");
}
}

void RetireBatch(State& state)
{
    state.send_analysis.reset();
    if (state.batch_read) state.batch_read->Cancel();
    if (!state.batch) return;
    auto& old=*state.batch;
    WipeRecipients(old.work);
    ForgetValidation(old);
    sodium_memzero(old.path.data(),old.path.size());
    if (old.path_edit) Wipe(*old.path_edit);
    if (old.external) Wipe(old.external->text);
    Wipe(old.status);
    state.batch.reset();
}
void PollBatch(State& state,app::Requests& host)
{
    const auto binding=Binding(state,host);
    if ((state.batch && state.batch->binding!=binding) || (state.send_analysis && state.send_analysis!=binding))
        RetireBatch(state);
    if (state.batch && state.batch->file_dialog && c::native_input) {
        auto& panel=*state.batch;
        const auto chosen=c::native_input->PollFile(panel.file_dialog);
        if (chosen.outcome!=c::NativeOutcome::Pending) {
            panel.file_dialog=0;
            if (chosen.outcome==c::NativeOutcome::Chosen && ValidLocalDocumentPath(chosen.path)
                && c::SetExternalBuffer(panel.path,panel.path_edit,chosen.path)) panel.status.clear();
            else panel.status=chosen.outcome==c::NativeOutcome::Cancelled ? "File choice cancelled." : "File choice did not return a valid local path.";
            ++panel.revision;
        }
    }
    if (!state.batch_read) return;
    auto result=state.batch_read->Take();
    if (!result) return;
    state.batch_read.reset();
    if (!state.batch || state.batch->generation!=state.batch_read_generation || state.batch->revision!=state.batch_read_revision
        || state.batch->binding!=binding || !Unobstructed(state)) {
        if (result->staged) WipeRecipients(*result->staged);
        return;
    }
    auto& panel=*state.batch;
    if (result->staged) {
        WipeRecipients(panel.work);
        panel.work=std::move(*result->staged);
        panel.page=0;
        if (panel.external) Wipe(panel.external->text);
        panel.external.reset();
        Changed(panel);
        panel.status="CSV staged. Inspect rows before applying.";
    } else {
        panel.status=std::move(result->error);
        ++panel.revision;
    }
}
bool DrawBatchOrAnalysis(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme)
{
    if (state.batch) {DrawBatch(state,host,facts,theme);return true;}
    if (state.send_analysis) {Analysis(state,host,facts,theme);return true;}
    if (state.funding.flow.open) return false;
    const auto binding=Binding(state,host);
    if (!binding) return false;
    Button(state,facts,theme,"open","Send to Many",true,[&state,&host,binding]{
        if (Binding(state,host)!=binding || !Unobstructed(state) || state.batch || state.send_analysis
            || state.funding.flow.open || state.batch_generation==UINT64_MAX) return false;
        state.batch.emplace(BatchEditor{*binding,++state.batch_generation,0,StageRecipients(state.documents.Active()->recipients)});
        SuspendFeeRecording(state,host);
        return true;
    });
    ImGui::SameLine();
    Button(state,facts,theme,"analysis","Transaction analysis",true,[&state,&host,binding]{
        if (Binding(state,host)!=binding || !Unobstructed(state) || state.batch || state.send_analysis || state.funding.flow.open)
            return false;
        state.send_analysis=binding;
        SuspendFeeRecording(state,host);
        return true;
    });
    return state.batch.has_value() || state.send_analysis.has_value();
}
}
