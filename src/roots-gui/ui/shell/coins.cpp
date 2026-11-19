// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/localization.h"
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include "ui/theme/tokens.h"
#include <imkit/charts.h>
#include <algorithm>
namespace roots::gui::ui::shell {
namespace {
namespace a=app;namespace c=components;
c::ItemFacts Last(bool pressed=false){auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();return {{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),false,pressed,pressed,ImGui::IsItemVisible() && lo.y>=ImGui::GetWindowDrawList()->GetClipRectMin().y && hi.y<=ImGui::GetWindowDrawList()->GetClipRectMax().y};}
LockEpoch Epoch(const State& s){return s.recovery?s.recovery->Gate().Epoch():LockEpoch{};}
bool Usable(const State& s,uint64_t id){auto* d=s.documents.Find(id);return d && d->section==a::Section::UTXOs && s.documents.ActiveId()==id && !s.privacy.hidden && !s.semantic_hide_toggle && (!s.recovery || !s.recovery->Locked());}
std::optional<RefreshView> View(State& s,a::Requests& host,uint64_t id){auto* d=s.documents.Find(id);return d && host.Refresh()?a::HistoryForDocument(*d,*host.Refresh(),Epoch(s)):std::nullopt;}
std::string Safe(std::string_view text){return std::any_of(text.begin(),text.end(),[](unsigned char c){return c<32 || c==127;})?rpc::Quote(text):std::string(text);}
void Value(State& s,const char* id,const char* name,std::optional<Money> amount){auto p=s.privacy.Present(amount);if(!s.privacy.hidden){if(!amount){p.text=p.semantic="Unknown";p.tooltip="Unknown: insufficient scoped evidence";}else p.text=(s.unit==1 || (s.unit==2 && amount->sats()>-100000000 && amount->sats()<100000000))?std::to_string(amount->sats())+" sats":locale::Btc(*amount)+" BTC";}ImGui::TextUnformatted(p.text.c_str());s.presentation.Annotate(id,name,p.text);if(ImGui::IsItemHovered() && !p.tooltip.empty())ImGui::SetTooltip("%s",p.tooltip.c_str());}
bool Button(State& s,Facts& f,const char* key,const char* label,const imkit::Theme& theme,bool enabled,std::function<bool()> action){ImGui::PushID(key);ImGui::BeginDisabled(!enabled);bool pressed=theme::ActionButton(label,imkit::ActionVariant::Secondary,{},theme);f.coin[key]=Last(pressed);f.coin[key].disabled=!enabled;auto external=s.presentation.Interact(label,c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus,!enabled);ImGui::EndDisabled();bool accepted=pressed && enabled && action();if(external)s.presentation.Defer(*external,[action,pressed,accepted](const auto&){return pressed?accepted:action();});ImGui::PopID();return accepted;}
std::string Scope(State& s,uint64_t id){const auto* d=s.documents.Find(id);const auto& r=s.coins.at(id);if(!d || !d->binding || !r.identity)return {};return d->binding->backend.value()+":"+d->binding->network.genesis.hex()+":"+d->binding->DaemonWallet()+":"+std::to_string(id)+":"+std::to_string(Epoch(s).value)+":"+std::to_string(d->history.query_revision)+":"+std::to_string(d->coins.metadata_revision)+":"+std::to_string(r.generation)+":"+std::to_string(r.sequence)+":"+std::to_string(r.observed);}
bool LoadLabels(State& s,uint64_t id){auto* d=s.documents.Find(id);if(!d || !d->binding || !s.recovery || s.recovery->Locked())return false;auto loaded=s.recovery->HistoryMetadata(a::HistoryMetadataScope(*d->binding));auto& r=s.coins.at(id);if(!loaded){r.error=std::string("Coin labels unavailable: ")+storage::ErrorName(loaded.error)+". Drafts retained; reload to retry.";return false;}r.labels.clear();for(const auto& label:loaded.value->labels)if(label.target.kind==storage::TargetKind::Output)r.labels[label.target.key]=label.text;d->coins.metadata_revision=loaded.value->revision;d->coins.metadata_loaded=true;for(auto& [key,draft]:d->coins.drafts){if(r.labels.contains(key))draft.saved=r.labels.at(key);else if(r.inventory && r.inventory->Rows().contains(key))draft.saved=r.inventory->Rows().at(key).label;draft.base_revision=loaded.value->revision;}r.error.clear();return true;}
bool SaveLabel(State& s,a::Requests& host,uint64_t id,const std::string& key){if(!Usable(s,id) || !s.recovery)return false;auto* d=s.documents.Find(id);auto v=View(s,host,id);auto& r=s.coins.at(id);if(!d->binding || !v || !v->identity || !r.identity || *v->identity!=*r.identity || !r.inventory || !r.inventory->Rows().contains(key) || !d->coins.metadata_loaded)return false;auto it=d->coins.drafts.find(key);if(it==d->coins.drafts.end())return false;auto& draft=it->second;if(!draft.dirty()){draft.error.clear();return true;}if(r.editor && r.editor->txid==key)r.editor->attempted=true;auto point=a::ParseCoinKey(key);if(!point)return false;auto saved=s.recovery->SaveOutputLabel(a::HistoryMetadataScope(*d->binding),*point,draft.draft,draft.base_revision);if(!saved){draft.error=std::string("Coin label was not saved: ")+storage::ErrorName(saved.error)+". Draft and previous saved label retained. Reload labels, then press Enter to retry.";r.reveal=true;return false;}draft.saved=draft.draft;draft.error.clear();r.labels[key]=draft.saved;d->coins.metadata_revision=*saved.value;for(auto& [k,pending]:d->coins.drafts)pending.base_revision=*saved.value;if(r.editor)r.editor->metadata_revision=*saved.value;return true;}
void Retire(State& s,a::Requests& host,uint64_t id){auto& r=s.coins.at(id);if(!r.editor)return;auto old=std::move(*r.editor);r.editor.reset();auto* d=s.documents.Find(id);if(!d)return;auto it=d->coins.drafts.find(old.txid);if(it==d->coins.drafts.end() || !it->second.dirty() || old.attempted)return;auto v=View(s,host,id);if(!Usable(s,id) || !v || !v->identity || *v->identity!=old.identity || d->coins.metadata_revision!=old.metadata_revision || it->second.base_revision!=old.metadata_revision){it->second.error="Coin label was not saved: view, revision or privacy state changed. Draft retained; press Enter to retry.";return;}SaveLabel(s,host,id,old.txid);}
bool Toggle(State& s,uint64_t id,const std::vector<std::string>& keys){auto* d=s.documents.Find(id);if(!d || s.documents.ActiveId()!=id || (s.recovery && s.recovery->Locked()) || !s.coins.at(id).inventory)return false;const auto& selected=d->coins.Selection();const bool all=std::all_of(keys.begin(),keys.end(),[&](const auto& key){return selected.contains(key);});auto candidate=selected;for(const auto& key:keys){if(!s.coins.at(id).inventory->Rows().contains(key))return false;if(all)candidate.erase(key);else candidate.insert(key);}if(candidate.size()>4096){s.coins.at(id).error="Selection limit reached (4096). Existing exact outpoints retained.";return false;}if(!d->coins.ReplaceSelection(std::move(candidate)))return false;if(!keys.empty())d->coins.focused=keys.front();s.coins.at(id).reveal=true;return true;}
void Invalidate(CoinRuntime& runtime)
{
    runtime.projection.reset();
    runtime.summary.reset();
    runtime.groups.reset();
    runtime.group_selection.clear();
    runtime.groups_prepared=false;
    runtime.policy_known=false;
}

void PrepareCoins(CoinRuntime& runtime,const a::CoinPresentation& presentation,bool hidden,FrameWork& work)
{
    const bool hide_value_order=hidden && presentation.by_value;
    if (!runtime.projection || runtime.projection->by_value!=presentation.by_value ||
        runtime.projection->descending!=presentation.descending || runtime.projection->hidden!=hide_value_order) {
        auto& view=runtime.projection.emplace();
        view.by_value=presentation.by_value;
        view.descending=presentation.descending;
        view.hidden=hide_value_order;
        view.rows=runtime.inventory->Ordered(view.by_value,view.descending,view.hidden);
        view.keys.reserve(view.rows.size());
        for (size_t i=0;i<view.rows.size();++i) {
            view.keys.push_back(a::CoinKey(view.rows[i]->outpoint));
            view.positions.emplace(view.keys.back(),i);
        }
        work.coins_indexed=runtime.inventory->Rows().size();
    }
    if (!runtime.summary || runtime.selection_revision!=presentation.SelectionRevision()) {
        runtime.summary=runtime.inventory->Summary(presentation.Selection());
        runtime.selection_revision=presentation.SelectionRevision();
        runtime.group_selection.clear();
        work.coins_summarized=runtime.inventory->Rows().size();
    }
    if (hidden) {
        runtime.groups.reset();
        runtime.group_selection.clear();
        runtime.groups_prepared=false;
        return;
    }
    if (!runtime.groups_prepared) {
        if (auto groups=runtime.inventory->Chart()) {
            runtime.groups=std::make_shared<const std::vector<a::CoinGroup>>(std::move(*groups));
            work.coin_chart_indexed=runtime.inventory->Rows().size();
        }
        runtime.groups_prepared=true;
    }
    if (runtime.groups && runtime.group_selection.empty()) {
        for (const auto& group:*runtime.groups)
            runtime.group_selection.push_back(std::ranges::count_if(group.keys,[&](const auto& key){return presentation.Selection().contains(key);}));
    }
}

void Chart(State& s,Facts& f,const imkit::Theme& theme,float scale,uint64_t id){auto& r=s.coins.at(id);if(s.privacy.hidden){ImGui::TextUnformatted("Chart amounts hidden");s.presentation.Annotate("coin-chart-hidden","Coin value chart","Hidden");return;}const auto& groups=r.groups;if(!groups || groups->empty()){ImGui::TextUnformatted(groups?"No observed coins":"Chart Unknown: incomplete inventory or amount overflow");s.presentation.Annotate("coin-chart-empty","Coin value chart",groups?"Empty":"Unknown");return;}r.chart_group=std::clamp(r.chart_group,0,int(groups->size()-1));Money total=Money::Satoshis(0);for(const auto& g:*groups){auto sum=total.Add(g.value);if(!sum){ImGui::TextUnformatted("Chart Unknown: incomplete inventory or amount overflow");return;}total=*sum;}
    std::vector<std::string> labels;std::vector<imkit::ChartDatum> bars;const auto tokens=theme::Wallet(theme.scheme==imkit::ColorScheme::Dark);for(const auto& g:*groups)labels.push_back(g.name);for(size_t n=0;n<groups->size();++n){const auto& g=(*groups)[n];const auto selected=r.group_selection[n];bars.push_back({labels[n].c_str(),total.sats()?float(static_cast<long double>(g.value.sats())/total.sats()*100):0,size_t(selected)==g.keys.size()?tokens.interaction:selected?tokens.soft_accent:tokens.utxo});}
    const int clicked=imkit::BarChart("Top nine coins plus Other",{bars},{{0,36*scale},imkit::Orientation::Vertical,100,false,false,"%.1f%% share"});f.coin["chart"]=Last(clicked>=0);if(ImGui::IsItemHovered()){const auto& rect=f.coin.at("chart").rect;const auto at=size_t(std::clamp(int((ImGui::GetIO().MousePos.x-rect[0])/std::max(1.F,rect[2]-rect[0])*groups->size()),0,int(groups->size()-1)));const auto& hovered=(*groups)[at];ImGui::SetTooltip("%s: %s BTC\n%zu exact outputs; value geometry only is approximate",hovered.name.c_str(),locale::Btc(hovered.value).c_str(),hovered.keys.size());}std::vector<float> geometry;for(const auto& b:bars)geometry.push_back(b.value);s.presentation.RecordChart(geometry);s.presentation.Interact("Coin value chart; use Previous group, Toggle group and Next group",c::ax::SemanticRole::Group,c::ax::SemanticAction::Focus);if(clicked>=0){r.chart_group=clicked;Toggle(s,id,(*groups)[size_t(clicked)].keys);}const auto group_index=size_t(r.chart_group);const auto& group=(*groups)[group_index];const auto text=group.name+" "+locale::Btc(group.value)+" BTC ("+std::to_string(r.group_selection[group_index])+"/"+std::to_string(group.keys.size())+" selected)";imkit::PushTextRole(theme,imkit::TextRole::Caption);ImGui::TextWrapped("%s",text.c_str());imkit::PopTextRole();s.presentation.Annotate("coin-chart-group","Focused coin chart group",text);
    const auto* draw=ImGui::GetWindowDrawList();
    s.presentation.Group("coin-chart-actions","Coin chart controls",c::ax::SemanticRole::Toolbar,
        draw->GetClipRectMin(),draw->GetClipRectMax());
    Button(s,f,"previous-group","Previous",theme,true,[&s,id]{auto& r=s.coins.at(id);r.chart_group=std::max(0,r.chart_group-1);return true;});
    theme::SameLineForAction("Toggle group");
    Button(s,f,"toggle-group","Toggle group",theme,true,[&s,id,groups,group_index]{return Usable(s,id) && s.coins.at(id).groups==groups && Toggle(s,id,groups->at(group_index).keys);});
    theme::SameLineForAction("Next");
    Button(s,f,"next-group","Next",theme,true,[&s,id,count=groups->size()]{auto& r=s.coins.at(id);r.chart_group=std::min(int(count)-1,r.chart_group+1);return true;});
    s.presentation.EndGroup();
}
}
void BeginCoinFrame(State& s){std::erase_if(s.coin_draft_errors,[&](const auto& error){return !s.documents.Find(error.first);});for(auto& [id,r]:s.coins){if(r.editor){r.editor->seen=false;r.editor->active=false;r.editor->finished=false;}if(!Usable(s,id)){r.export_data.clear();r.file_dialog=0;}}}
void FinishCoinFrame(State& s,a::Requests& host){for(auto& [id,r]:s.coins)if(r.editor){auto* d=s.documents.Find(id);if(!r.editor->seen || !r.editor->active || r.editor->finished || !d || d->coins.focused!=r.editor->txid)Retire(s,host,id);}}
void DrawCoins(State& s,a::Requests& host,Facts& f,const imkit::Theme& theme,float scale){auto* d=s.documents.Active();if(!d || !d->binding)return;const auto id=d->id;auto& r=s.coins[id];while(s.coins.size()>8){auto it=s.coins.begin();if(it->first==id)++it;if(auto* old=s.documents.Find(it->first))old->coins.metadata_loaded=false;s.coins.erase(it);}
    auto v=View(s,host,id);const auto* ledger=v && v->ledger && v->ledger->Observed()?v->ledger:nullptr;imkit::PushTextRole(theme,imkit::TextRole::Heading);ImGui::TextUnformatted("UTXOs");imkit::PopTextRole();if(!ledger || !v->identity){Invalidate(r);r.inventory.reset();r.identity.reset();r.export_data.clear();r.file_dialog=0;ImGui::TextWrapped("Coin inventory unavailable. Connect this existing wallet and explicitly refresh; offline transport is suppressed.");s.presentation.Annotate("coin-unavailable","Coin inventory","Unavailable; no verified snapshot");DrawCoinActions(s,host,f,theme,scale);return;}
    if(!r.identity || *r.identity!=*v->identity){Invalidate(r);r.inventory.emplace(v->identity->context,d->binding->DaemonWallet());r.sequence=0;r.generation=0;r.identity=*v->identity;d->coins.metadata_loaded=false;r.labels.clear();r.error.clear();r.editor.reset();r.label_update.reset();}
    const auto policy_revision=s.coin_controls.Revision();if(r.sequence!=v->history_sequence || r.generation!=v->generation || r.chain_current!=v->chain_current || r.policy_revision!=policy_revision){Invalidate(r);auto policy=s.recovery?s.coin_controls.Policy(*d,*v,*s.recovery):std::nullopt;r.policy_known=bool(policy);f.work.coins_adopted=ledger->Coins().size();if(!r.inventory->Apply(*ledger,v->history_sequence,v->chain_current,policy?&*policy:nullptr)){r.inventory->MarkDated();r.error="Coin inventory could not adopt this observation (scope, sequence or retention bound). Dated rows and exact selections retained.";}}
    r.policy_revision=policy_revision;r.chain_current=v->chain_current;r.generation=v->generation;r.sequence=v->history_sequence;r.observed=ledger->Observed();if(r.label_update && r.label_update->first!=d->coins.focused)r.label_update.reset();if(!d->coins.metadata_loaded && r.error.empty())LoadLabels(s,id);
    const auto source=ObservationDate(ledger->Observed())+(!v->chain_current?" / Current state Unknown; dated rows retained":" / Wallet inventory observation");imkit::PushTextRole(theme,imkit::TextRole::Caption);ImGui::TextWrapped("%s",source.c_str());s.presentation.Annotate("coin-source","Coin inventory provenance",source);imkit::PopTextRole();PrepareCoins(r,d->coins,s.privacy.hidden,f.work);const auto& totals=*r.summary;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{ImGui::GetStyle().FramePadding.x,2*scale});
    const auto& style=ImGui::GetStyle();
    const auto chart_column=theme::ActionWidth("Previous")+theme::ActionWidth("Toggle group")
        +theme::ActionWidth("Next")+2*(style.ItemSpacing.x+style.CellPadding.x);
    const float summary_column=std::max({
        ImGui::CalcTextSize("Local policy: acknowledged").x,
        ImGui::CalcTextSize(("Observed coins: "+std::to_string(totals.present)).c_str()).x,
        ImGui::CalcTextSize(("Eligible "+(totals.eligible ? locale::Btc(*totals.eligible)+" BTC" : "Unknown")).c_str()).x,
        ImGui::CalcTextSize(("Total "+(totals.total ? locale::Btc(*totals.total)+" BTC" : "Unknown")).c_str()).x
    })+2*style.CellPadding.x+style.ItemSpacing.x;
    const int columns=ImGui::GetContentRegionAvail().x>=summary_column+chart_column?2:1;
    if(ImGui::BeginTable("coin-upper",columns,ImGuiTableFlags_SizingStretchProp)){
        if(columns==2){
            ImGui::TableSetupColumn("Inventory summary",ImGuiTableColumnFlags_WidthFixed,summary_column);
            ImGui::TableSetupColumn("Coin chart",ImGuiTableColumnFlags_WidthStretch);
        }
        ImGui::TableNextColumn();ImGui::TextUnformatted(s.privacy.hidden?"Observed coins: Hidden":("Observed coins: "+std::to_string(totals.present)).c_str());s.presentation.Annotate("coin-count","Observed coin count",s.privacy.hidden?"Hidden":std::to_string(totals.present));ImGui::TextUnformatted("Total");ImGui::SameLine();Value(s,"coin-total","Total observed inventory",totals.total);ImGui::TextUnformatted("Eligible");ImGui::SameLine();Value(s,"coin-eligible","Eligible amount; scoped local policy",totals.eligible);const auto policy_text=r.policy_known?"Local policy: acknowledged":"Local policy: Unknown";ImGui::TextUnformatted(policy_text);s.presentation.Annotate("coin-policy","Local coin policy",policy_text);ImGui::TableNextColumn();Chart(s,f,theme,scale,id);ImGui::EndTable();}
    const auto* draw=ImGui::GetWindowDrawList();
    s.presentation.Group("coin-inventory-actions","Coin inventory actions",c::ax::SemanticRole::Toolbar,
        draw->GetClipRectMin(),draw->GetClipRectMax());
    Button(s,f,"sort-value","Sort value",theme,!s.privacy.hidden,[&s,id]{if(!Usable(s,id))return false;auto& p=s.documents.Find(id)->coins;p.descending=p.by_value?!p.descending:true;p.by_value=true;s.coins.at(id).reveal=true;return true;});
    theme::SameLineForAction("Sort date");
    Button(s,f,"sort-date","Sort date",theme,true,[&s,id]{auto* d=s.documents.Find(id);if(!d || s.documents.ActiveId()!=id)return false;d->coins.descending=!d->coins.by_value?!d->coins.descending:true;d->coins.by_value=false;s.coins.at(id).reveal=true;return true;});
    theme::SameLineForAction("Export CSV");
    Button(s,f,"csv","Export CSV",theme,Usable(s,id) && c::native_input && c::native_input->FileChooserAvailable() && !r.file_dialog,[&s,id]{auto* d=s.documents.Find(id);auto& r=s.coins.at(id);if(!Usable(s,id) || !r.inventory || !c::native_input)return false;auto rows=r.inventory->Ordered(d->coins.by_value,d->coins.descending);auto csv=a::CoinCsv(*r.inventory,rows,r.labels,false);if(!csv){r.error="CSV was not saved: invalid text or snapshot bounds. Drafts and original files retained.";return false;}r.export_data=std::move(*csv);r.export_scope=Scope(s,id);r.file_dialog=c::native_input->ChooseFile("coins.csv",true);return bool(r.file_dialog);});
    if(s.recovery){
        theme::SameLineForAction("Reload labels");
        Button(s,f,"reload-labels","Reload labels",theme,Usable(s,id),[&s,id]{return Usable(s,id) && LoadLabels(s,id);});
    }
    s.presentation.EndGroup();
    ImGui::PopStyleVar();
    if(r.file_dialog){if(!Usable(s,id) || r.export_scope!=Scope(s,id)){r.export_data.clear();r.file_dialog=0;}else if(c::native_input){auto choice=c::native_input->PollFile(r.file_dialog);if(choice.outcome!=c::NativeOutcome::Pending){if(choice.outcome==c::NativeOutcome::Chosen)a::WriteCoinCsv(choice.path,r.export_data,r.error);else if(choice.outcome!=c::NativeOutcome::Cancelled)r.error="CSV was not saved; choose Export CSV to retry.";r.file_dialog=0;r.export_data.clear();}}}
    if(!r.error.empty()){const auto error=s.privacy.hidden?"Coin operation details hidden":r.error;ImGui::TextWrapped("%s",error.c_str());s.presentation.Annotate("coin-error","Coin operation status",error,c::ax::SemanticRole::Alert);}for(const auto& [key,draft]:d->coins.drafts)if(!draft.error.empty()){const auto error=s.privacy.hidden?"Coin label save failed; details hidden":draft.error;ImGui::TextWrapped("%s",error.c_str());s.presentation.Annotate(("coin-label-error-"+key).c_str(),"Coin label save failed",error,c::ax::SemanticRole::Alert);}
    const auto& projection=*r.projection;const auto& rows=projection.rows;f.coin_order=projection.keys;const auto summary=s.privacy.hidden?std::string("Selection totals hidden"):(std::to_string(totals.selected)+" selected / "+std::to_string(totals.present)+" observed; value "+(totals.selected_value?totals.selected_value->btc()+" BTC":"Unknown")+"; eligible "+(totals.selected_eligible?totals.selected_eligible->btc()+" BTC":"Unknown"));
    const float width=ImGui::GetContentRegionAvail().x;
    imkit::PushTextRole(theme,imkit::TextRole::Caption);
    const auto summary_height=ImGui::CalcTextSize(summary.c_str(),nullptr,false,width).y+ImGui::GetStyle().ItemSpacing.y;
    imkit::PopTextRole();
    const auto spacing=ImGui::GetStyle().ItemSpacing.x;
    const auto actions=theme::ActionWidth("Select All")+theme::ActionWidth("Clear")+theme::ActionWidth("Send Selected")
        +theme::ActionWidth("Coin actions")+3*spacing;
    const float footer=summary_height+(actions>width?2.F:1.F)*ImGui::GetFrameHeightWithSpacing();ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{ImGui::GetStyle().FramePadding.x,2*scale});
    const auto row_height=ImGui::GetFrameHeight()+2*ImGui::GetStyle().CellPadding.y;
    const auto minimum_table_height=2*row_height+ImGui::GetStyle().ScrollbarSize;
    if(ImGui::BeginTable("coin-table",5,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_ScrollY|ImGuiTableFlags_ScrollX|ImGuiTableFlags_Resizable|ImGuiTableFlags_SizingStretchProp,{0,std::max(minimum_table_height,ImGui::GetContentRegionAvail().y-footer)})){
        auto lo=ImGui::GetWindowDrawList()->GetClipRectMin(),hi=ImGui::GetWindowDrawList()->GetClipRectMax();s.presentation.Group("coin-grid","Wallet coins; toggle rows for multi-selection",c::ax::SemanticRole::Grid,lo,hi);ImGui::TableSetupScrollFreeze(0,1);const char* names[]={"Date (UTC)","Output","Address","Label","Value"};for(int n=0;n<5;++n)ImGui::TableSetupColumn(locale::Text(names[n]),ImGuiTableColumnFlags_None,n==4?1.7F:n==2?1.4F:n==1?1.15F:1.F);ImGui::TableHeadersRow();auto& pos=d->sections[size_t(a::Section::UTXOs)];if(pos.restore_table){ImGui::SetScrollX(pos.table_x);ImGui::SetScrollY(pos.table_y);pos.restore_table=false;}ImGuiListClipper clip;clip.Begin(int(rows.size()),row_height);if(r.reveal)if(const auto focused=projection.positions.find(d->coins.focused);focused!=projection.positions.end())clip.IncludeItemByIndex(int(focused->second));while(clip.Step())for(int n=clip.DisplayStart;n<clip.DisplayEnd;++n){++f.work.coins_drawn;const auto& row=*rows[size_t(n)];const auto& key=projection.keys[size_t(n)];ImGui::PushID(key.c_str());ImGui::TableNextRow(ImGuiTableRowFlags_None,row_height);ImGui::TableNextColumn();const auto date=row.date?locale::LocalDate(*row.date):locale::Text("Unknown date");bool clicked=ImGui::Selectable((date+"###coin-row").c_str(),d->coins.Selection().contains(key),ImGuiSelectableFlags_None,{0,ImGui::GetFrameHeight()});f.coin["row-"+key]=Last(clicked);if(clicked)Toggle(s,id,{key});const auto select=s.presentation.Interact("Coin; toggle exact outpoint selection",c::ax::SemanticRole::Row,c::ax::SemanticAction::Select|c::ax::SemanticAction::Focus,false,d->coins.Selection().contains(key),date+" / "+key+" / "+row.reason);s.presentation.Parent(ImGui::GetItemID());if(select)s.presentation.Defer(*select,[&s,id,key](const auto&){return Toggle(s,id,{key});});DrawCoinContext(s,host,f,theme,key);if(r.reveal && d->coins.focused==key){ImGui::SetScrollHereY(.5F);r.reveal=false;}if(ImGui::IsItemHovered()){const auto heuristic=s.privacy.hidden?std::string{}:std::string(row.reused?"\nAddress reuse: heuristic":"")+(row.small_value?"\nSmall value <546 sats: heuristic, NOT relay dust policy":"");ImGui::SetTooltip("%s\n%s%s",key.c_str(),row.reason.c_str(),heuristic.c_str());}
            ImGui::TableNextColumn();const auto output=key.substr(0,8)+"…:"+std::to_string(row.outpoint.index);ImGui::TextUnformatted(output.c_str());s.presentation.Annotate("coin-outpoint","Exact output",key+" / "+row.reason);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s",key.c_str(),row.reason.c_str());ImGui::TableNextColumn();ImGui::TextUnformatted(Safe(row.address.empty()?"Unknown":row.address).c_str());s.presentation.Annotate("coin-address","Coin address",Safe(row.address.empty()?"Unknown":row.address));if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",Safe(row.address).c_str());ImGui::TableNextColumn();const auto label=r.labels.contains(key)?r.labels.at(key):row.label;
            if(s.privacy.hidden){ImGui::TextUnformatted("Hidden");s.presentation.Annotate("coin-label-hidden","Coin label","Hidden");}
            else if(d->coins.focused==key && d->coins.metadata_loaded && s.recovery){auto it=d->coins.drafts.find(key);if(it==d->coins.drafts.end() && d->coins.drafts.size()>=32){for(auto old=d->coins.drafts.begin();old!=d->coins.drafts.end();++old)if(!old->second.dirty()){d->coins.drafts.erase(old);break;}}if(it==d->coins.drafts.end() && d->coins.drafts.size()<32)it=d->coins.drafts.emplace(key,a::HistoryLabelDraft{label,label,"",d->coins.metadata_revision}).first;if(it!=d->coins.drafts.end() && label.size()<=4096){if(r.editor && r.editor->txid!=key)Retire(s,host,id);auto& draft=it->second;std::array<char,4097> buffer{};std::copy(draft.draft.begin(),draft.draft.end(),buffer.begin());std::optional<std::string> external;if(r.label_update && r.label_update->first==key){if(r.label_update->second==draft.draft)external=std::move(r.label_update->second);r.label_update.reset();}const bool external_update=external.has_value();ImGui::SetNextItemWidth(-1);const bool enter=c::TextInput("###coin-label","Label; Enter or leave",buffer.data(),buffer.size(),external,ImGuiInputTextFlags_EnterReturnsTrue);const bool active=ImGui::IsItemActive(),activated=ImGui::IsItemActivated(),edited=ImGui::IsItemEdited(),finished=ImGui::IsItemDeactivatedAfterEdit();f.coin["label-"+key]=Last();draft.draft=buffer.data();if(active && activated && !r.editor)r.editor=HistoryLabelEditor{*v->identity,key,d->coins.metadata_revision,false,false,false,!draft.error.empty()};if(r.editor && r.editor->txid==key){r.editor->seen=true;r.editor->active=active;r.editor->finished=finished;if(edited && !external_update)r.editor->attempted=false;}auto edit=s.presentation.Interact("Coin label; Enter or focus loss saves",c::ax::SemanticRole::TextField,c::ax::SemanticAction::SetValue|c::ax::SemanticAction::Focus,false,false,draft.draft,draft.error,!draft.error.empty());if(enter)SaveLabel(s,host,id,key);if(edit)s.presentation.Defer(*edit,[&s,&host,id,key](const c::SemanticRequest& request){if(!Usable(s,id) || request.value.size()>4096 || request.value.find('\0')!=std::string::npos)return false;auto* d=s.documents.Find(id);auto it=d->coins.drafts.find(key);if(it==d->coins.drafts.end())return false;it->second.draft=request.value;s.coins.at(id).label_update=std::pair(key,request.value);return SaveLabel(s,host,id,key);});}else ImGui::TextUnformatted("Draft limit; save other labels first");}
            else{ImGui::TextUnformatted(Safe(label).c_str());s.presentation.Annotate("coin-label","Coin label",Safe(label));}ImGui::TableNextColumn();Value(s,"coin-value","Observed coin value",row.value);s.presentation.EndGroup();ImGui::PopID();}
        pos.table_x=ImGui::GetScrollX();pos.table_y=ImGui::GetScrollY();s.presentation.EndGroup();ImGui::EndTable();}
    ImGui::PopStyleVar();imkit::PushTextRole(theme,imkit::TextRole::Caption);ImGui::TextWrapped("%s",summary.c_str());s.presentation.Annotate("coin-selection","Exact outpoint selection and eligible amount",summary);imkit::PopTextRole();DrawCoinActions(s,host,f,theme,scale);
}
}
