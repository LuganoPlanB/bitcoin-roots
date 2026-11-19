// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/localization.h"
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include "app/history_workspace.h"
#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <unistd.h>
namespace roots::gui::ui::shell {
namespace {
namespace a=app;namespace c=components;
constexpr const char* DocumentLimitReason="You have 32 open documents. Close a tab, then try again. Your drafts are retained.";
c::ItemFacts Last(bool pressed=false){const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();return {{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),false,pressed,pressed,ImGui::IsItemVisible() && lo.x>=ImGui::GetWindowDrawList()->GetClipRectMin().x && lo.y>=ImGui::GetWindowDrawList()->GetClipRectMin().y && hi.x<=ImGui::GetWindowDrawList()->GetClipRectMax().x && hi.y<=ImGui::GetWindowDrawList()->GetClipRectMax().y};}
bool UnsupportedLabel(std::string_view value){return std::any_of(value.begin(),value.end(),[](unsigned char c){return (c<32 && c!='\r' && c!='\n' && c!='\t') || c==127;});}
LockEpoch Epoch(const State& s){return s.recovery?s.recovery->Gate().Epoch():LockEpoch{};}
std::string Label(const HistoryTransaction& tx,const HistoryRuntime& runtime){if(const auto i=runtime.labels.find(tx.txid.hex());i!=runtime.labels.end())return i->second;for(const auto& d:tx.details)if(!d.label.empty())return d.label;return "";}
std::optional<RefreshView> View(State& s,a::Requests& host,uint64_t id){const auto* d=s.documents.Find(id);return d && host.Refresh()?a::HistoryForDocument(*d,*host.Refresh(),Epoch(s)):std::nullopt;}
bool Usable(const State& s,uint64_t id){return !s.privacy.hidden && !s.semantic_hide_toggle && (!s.recovery || !s.recovery->Locked()) && s.documents.ActiveId()==id && s.documents.Find(id);}
std::optional<std::string> ExplorerTarget(const State& s,a::Requests& host,const RefreshIdentity& identity,std::string_view txid)
{
    if(!s.general.saved)return {};
    if(auto* connection=host.Connection();connection && (!connection->Active() || connection->Offline()
        || connection->Session()!=identity.connection_session || connection->Active()->candidate.connection.proxy!=ProxyKind::Direct))return {};
    const auto hash=Hash256::Parse(txid);
    return hash?a::ExplorerUrl(s.general.saved->preferences,identity.context.network,*hash):std::nullopt;
}
std::string ExportScope(State& s,uint64_t id){const auto* d=s.documents.Find(id);if(!d || !d->binding)return {};return std::to_string(id)+":"+d->binding->backend.value()+":"+d->binding->wallet.value()+":"+d->binding->network.genesis.hex()+":"+std::to_string(d->history.query_revision)+":"+std::to_string(d->history.metadata_revision)+":"+std::to_string(Epoch(s).value)+":"+std::to_string(s.histories.at(id).observed)+":"+std::to_string(s.histories.at(id).generation)+":"+std::to_string(s.histories.at(id).sequence);}
// User-selected exports are new files only. Exclusive creation keeps every
// existing file intact, including a path chosen through an overwrite dialog.
bool WriteCsv(const std::string& path,const std::string& data,std::string& error){const std::filesystem::path target(path);if(!target.is_absolute() || data.empty()){error="Choose an absolute path for a new CSV file.";return false;}const int fd=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0){error="CSV was not saved. Choose a new filename in a writable folder; existing files are retained.";return false;}size_t offset{};bool ok=true;while(offset<data.size()){const auto n=::write(fd,data.data()+offset,data.size()-offset);if(n<0 && errno==EINTR)continue;if(n<=0){ok=false;break;}offset+=size_t(n);}if(::fsync(fd)!=0)ok=false;if(::close(fd)!=0)ok=false;if(!ok){error="CSV write failed; a partial file may remain at the selected path. Choose a new filename to retry.";return false;}const int dir=::open(target.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);const bool durable=dir>=0 && ::fsync(dir)==0;if(dir>=0)::close(dir);error=durable?"CSV saved locally.":"CSV written; directory durability could not be confirmed. Check the selected file.";return durable;}
bool LoadLabels(State& s,uint64_t id){auto* d=s.documents.Find(id);if(!d || !d->binding || !s.recovery || s.recovery->Locked())return false;auto& r=s.histories[id];auto loaded=s.recovery->HistoryMetadata(a::HistoryMetadataScope(*d->binding));if(!loaded){r.error=std::string("Labels unavailable: ")+storage::ErrorName(loaded.error)+". Drafts retained; retry loading.";return false;}r.labels.clear();for(const auto& label:loaded.value->labels)if(label.target.kind==storage::TargetKind::Transaction)r.labels[label.target.key]=label.text;d->history.metadata_revision=loaded.value->revision;d->history.metadata_loaded=true;for(auto& [key,draft]:d->history.drafts){const auto i=r.labels.find(key);if(i!=r.labels.end())draft.saved=i->second;draft.base_revision=loaded.value->revision;}r.error.clear();return true;}
bool SaveLabel(State& s,a::Requests& host,uint64_t id,const std::string& txid){if(!Usable(s,id) || !s.recovery)return false;auto* d=s.documents.Find(id);auto view=View(s,host,id);if(!d || !d->binding || !view || !view->ledger->Rows().contains(txid) || !d->history.metadata_loaded)return false;auto found=d->history.drafts.find(txid);if(found==d->history.drafts.end())return false;auto& draft=found->second;if(!draft.dirty()){draft.error.clear();return true;}auto& runtime=s.histories.at(id);if(runtime.editor && runtime.editor->txid==txid)runtime.editor->attempted=true;auto hash=Hash256::Parse(txid);auto saved=s.recovery->SaveHistoryLabel(a::HistoryMetadataScope(*d->binding),*hash,draft.draft,draft.base_revision);if(!saved){s.histories[id].reveal_selected=true;draft.error=std::string("Label was not saved: ")+storage::ErrorName(saved.error)+". Draft retained. Reload saved labels, then press Enter to retry.";return false;}d->history.metadata_revision=*saved.value;if(runtime.editor && runtime.editor->txid==txid)runtime.editor->metadata_revision=*saved.value;draft.saved=draft.draft;draft.error.clear();s.histories[id].labels[txid]=draft.saved;for(auto& [key,pending]:d->history.drafts)pending.base_revision=*saved.value;return true;}
// Retire once, including when the old widget was omitted after selection,
// filtering or clipping. The activation carries its original public scope;
// it cannot become a delayed write to a replacement wallet/query/epoch.
void RetireLabelEditor(State& s,a::Requests& host,uint64_t id){
    auto slot=s.histories.find(id);if(slot==s.histories.end() || !slot->second.editor)return;
    auto editor=std::move(*slot->second.editor);slot->second.editor.reset();
    auto* doc=s.documents.Find(id);if(!doc)return;auto found=doc->history.drafts.find(editor.txid);
    if(found==doc->history.drafts.end() || !found->second.dirty() || editor.attempted)return;
    auto view=View(s,host,id); // Local labels may also be edited in a dated offline/cache view.
    if(!Usable(s,id) || !view || !view->identity || *view->identity!=editor.identity || !doc->history.metadata_loaded || doc->history.metadata_revision!=editor.metadata_revision || found->second.base_revision!=editor.metadata_revision){
        found->second.error="Label was not saved: view, revision or privacy state changed. Draft retained; select the label and press Enter to retry.";return;
    }
    SaveLabel(s,host,id,editor.txid);
}
bool Button(State& s,Facts& f,const char* key,const char* label,const imkit::Theme& theme,bool enabled,std::function<bool()> action){ImGui::PushID(key);ImGui::BeginDisabled(!enabled);const bool pressed=theme::ActionButton(label,imkit::ActionVariant::Secondary,{},theme);f.history[key]=Last(pressed);f.history[key].disabled=!enabled;const auto external=s.presentation.Interact(label,c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus,!enabled);ImGui::EndDisabled();bool accepted=false;if(pressed && enabled)accepted=action();if(external)s.presentation.Defer(*external,[action,pressed,accepted](const c::SemanticRequest&){return pressed?accepted:action();});ImGui::PopID();return accepted;}
void Amount(State& s,const char* id,const char* name,std::optional<Money> money){auto value=s.privacy.Present(money);if(!s.privacy.hidden && !money){value.text=value.semantic="Unknown";value.tooltip="Unknown: required amount evidence is incomplete";}if(!s.privacy.hidden && money && s.unit!=0){const bool sats=s.unit==1 || (money->sats()>-100000000 && money->sats()<100000000);value.text=sats?std::to_string(money->sats())+" sats":locale::Btc(*money)+" BTC";value.semantic=value.text;}ImGui::TextUnformatted(value.text.c_str());s.presentation.Annotate(id,name,value.semantic);if(ImGui::IsItemHovered() && !value.tooltip.empty())ImGui::SetTooltip("%s",value.tooltip.c_str());}
void PrepareRows(HistoryRuntime& runtime, const HistoryLedger& ledger, const a::HistoryDocumentState& state,
                 bool hidden, FrameWork& work)
{
    auto& cache=runtime.projection;
    const HistoryProjectionKey source{&ledger,runtime.generation,runtime.sequence,ledger.Revision()};
    if (cache.source!=source || cache.metadata_revision!=state.metadata_revision ||
        cache.filter!=state.filter || cache.sort!=state.sort || cache.descending!=state.descending || cache.hidden!=hidden) {
        cache.source=source;
        cache.metadata_revision=state.metadata_revision;
        cache.filter=state.filter;
        cache.sort=state.sort;
        cache.descending=state.descending;
        cache.hidden=hidden;
        cache.transactions=a::OrderedHistory(ledger,state,runtime.labels,hidden);
        cache.keys.clear();
        cache.keys.reserve(cache.transactions.size());
        for (const auto* tx:cache.transactions) cache.keys.push_back(tx->txid.hex());
        cache.expanded_dirty=true;
        work.history_indexed=ledger.Rows().size();
    }
    if (!cache.expanded_dirty) return;
    cache.rows.clear();
    cache.positions.clear();
    for (size_t i=0;i<cache.transactions.size();++i) {
        const auto* tx=cache.transactions[i];
        const auto& key=cache.keys[i];
        cache.positions.emplace(key,cache.rows.size());
        cache.rows.push_back({tx});
        if (!state.expanded.contains(key)) continue;
        if (tx->decoded_available) {
            for (const auto& io:tx->io) cache.rows.push_back({tx,nullptr,&io});
        } else {
            for (const auto& detail:tx->details) cache.rows.push_back({tx,&detail});
        }
    }
    work.history_flattened=cache.rows.size();
    cache.expanded_dirty=false;
}

const HistoryChartProjection& PrepareChart(HistoryRuntime& runtime,const HistoryLedger& ledger,FrameWork& work)
{
    auto& cache=runtime.chart;
    const HistoryProjectionKey source{&ledger,runtime.generation,runtime.sequence,ledger.Revision()};
    if (cache.source==source) return cache;
    cache={};
    cache.source=source;
    a::HistoryDocumentState chronological;
    chronological.descending=false;
    cache.transactions=a::OrderedHistory(ledger,chronological,{},true);
    work.history_chart_indexed=cache.transactions.size();
    cache.low=cache.high=cache.transactions.front()->running_balance->sats();
    for (size_t i=0;i<cache.transactions.size();++i) {
        const auto* point=cache.transactions[i];
        cache.positions.emplace(point->txid.hex(),i);
        const auto amount=point->running_balance->sats();
        cache.low=std::min(cache.low,amount);
        cache.high=std::max(cache.high,amount);
    }
    // Keep each bucket's extrema in chronological order. Interaction still
    // addresses the exact transaction sequence and selected position.
    constexpr size_t MAX_CHART_POINTS=256;
    if (cache.transactions.size()<=MAX_CHART_POINTS) {
        for (size_t i=0;i<cache.transactions.size();++i) cache.geometry.push_back(i);
        return cache;
    }
    constexpr size_t buckets=(MAX_CHART_POINTS-2)/2;
    const size_t bucket=(cache.transactions.size()+buckets-1)/buckets;
    const auto append=[&](size_t point){
        if (cache.geometry.empty() || cache.geometry.back()!=point) cache.geometry.push_back(point);
    };
    append(0);
    for (size_t begin=0;begin<cache.transactions.size();begin+=bucket) {
        const size_t end=std::min(begin+bucket,cache.transactions.size());
        size_t low=begin,high=begin;
        for(size_t i=begin+1;i<end;++i) {
            const auto& value=cache.transactions[i]->running_balance;
            if (value<cache.transactions[low]->running_balance) low=i;
            if (value>cache.transactions[high]->running_balance) high=i;
        }
        append(std::min(low,high));
        append(std::max(low,high));
    }
    append(cache.transactions.size()-1);
    return cache;
}

void Chart(State& s,a::Requests& host,Facts& f,const HistoryLedger& ledger,const imkit::Theme& theme,float scale,uint64_t id){
    auto* document=s.documents.Find(id);
    auto& runtime=s.histories.at(id);
    imkit::PushTextRole(theme,imkit::TextRole::Caption);const auto current=ledger.Rows().find(document->history.selected);const auto caption=!s.privacy.hidden && current!=ledger.Rows().end() && current->second.running_balance?locale::LocalDate(current->second.time)+" / "+current->second.running_balance->btc()+" BTC":std::string("Balance including immature");ImGui::TextUnformatted(caption.c_str());s.presentation.Annotate("chart-current","Balance including immature; selected transaction",caption);imkit::PopTextRole();
    if(s.privacy.hidden){runtime.chart={};ImGui::TextUnformatted("Amounts hidden");s.presentation.Annotate("history-chart-hidden","Balance chart","Amounts hidden");return;}
    if(!ledger.Complete() || ledger.Rows().empty()){runtime.chart={};ImGui::TextWrapped("%s",ledger.Rows().empty()?"No transactions":"Unknown: history is incomplete");s.presentation.Annotate("history-chart-unknown","Balance chart",ledger.Rows().empty()?"No transactions":"Unknown: history is incomplete");return;}
    const auto& chart=PrepareChart(runtime,ledger,f.work);
    const auto& points=chart.transactions;
    const auto origin=ImGui::GetCursorScreenPos();const ImVec2 size{std::max(1.F,ImGui::GetContentRegionAvail().x),18*scale};ImGui::InvisibleButton("history-chart",size);f.history["chart"]=Last();
    auto* draw=ImGui::GetWindowDrawList();
    std::vector<float> geometry;
    geometry.reserve(chart.geometry.size());
    const auto position=[&](size_t i){const auto value=points[i]->running_balance->sats();const float fraction=chart.high==chart.low?.5F:float((static_cast<long double>(value)-chart.low)/(static_cast<long double>(chart.high)-chart.low));return ImVec2{origin.x+(points.size()==1?.5F:float(i)/float(points.size()-1))*size.x,origin.y+(1-fraction)*(size.y-4*scale)+2*scale};};
    std::optional<ImVec2> previous;
    for (const auto i:chart.geometry) {
        const auto point=position(i);
        geometry.push_back(point.y-origin.y);
        if (previous) draw->AddLine(*previous,point,ImGui::GetColorU32(theme.semantic.text),1.5F*scale);
        previous=point;
    }
    f.work.history_chart_drawn=geometry.size();
    if (const auto present=chart.positions.find(document->history.selected);present!=chart.positions.end())
        draw->AddCircleFilled(position(present->second),3*scale,ImGui::GetColorU32(theme.semantic.text));
    s.presentation.RecordChart(geometry);
    if(ImGui::IsItemHovered()){const auto index=points.size()==1?size_t(0):size_t(std::clamp(std::llround((ImGui::GetIO().MousePos.x-origin.x)/size.x*(points.size()-1)),0LL,static_cast<long long>(points.size()-1)));const auto* tx=points[index];ImGui::SetTooltip("%s\n%s BTC",locale::LocalDate(tx->time,true).c_str(),locale::Btc(*tx->running_balance).c_str());if(ImGui::IsMouseClicked(0)){document->history.selected=tx->txid.hex();document->history.filter.clear();s.histories.at(id).reveal_selected=true;}}
    const auto move=[&s,&host,id](int direction){
        const auto view=View(s,host,id);auto* d=s.documents.Find(id);
        if(!Usable(s,id) || !d || !view || !view->ledger->Complete())return false;
        auto& runtime=s.histories.at(id);
        const auto& chart=runtime.chart;
        if(chart.source!=HistoryProjectionKey{view->ledger,view->generation,view->history_sequence,view->ledger->Revision()} ||
            chart.transactions.empty())return false;
        const auto found=chart.positions.find(d->history.selected);
        const int index=found==chart.positions.end()?0:int(found->second);
        d->history.selected=chart.transactions[size_t(std::clamp(index+direction,0,int(chart.transactions.size()-1)))]->txid.hex();
        d->history.filter.clear();
        runtime.reveal_selected=true;
        return true;
    };
    if(ImGui::IsItemFocused()){if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow))move(-1);if(ImGui::IsKeyPressed(ImGuiKey_RightArrow))move(1);}
    s.presentation.Interact("Balance chart; use Previous or Next transaction",c::ax::SemanticRole::Group,c::ax::SemanticAction::Focus);
    Button(s,f,"chart-previous","Previous",theme,true,[move]{return move(-1);});ImGui::SameLine();Button(s,f,"chart-next","Next",theme,true,[move]{return move(1);});

}
}
void BeginHistoryLabelFrame(State& s){for(auto& [id,runtime]:s.histories){if(s.documents.ActiveId()!=id || !Usable(s,id))runtime.explorer_permission.reset();if(runtime.editor){runtime.editor->seen=false;runtime.editor->active=false;runtime.editor->finished=false;}}}
void FinishHistoryLabelFrame(State& s,app::Requests& host){for(auto& [id,runtime]:s.histories)if(runtime.editor){const auto* doc=s.documents.Find(id);if(!runtime.editor->seen || !runtime.editor->active || runtime.editor->finished || !doc || doc->history.selected!=runtime.editor->txid)RetireLabelEditor(s,host,id);}}
void DrawHistory(State& s,a::Requests& host,Facts& f,const imkit::Theme& theme,float scale){
    const auto id=s.documents.ActiveId();auto* d=s.documents.Find(id);auto& runtime=s.histories[id];if(runtime.label_update && runtime.label_update->first!=d->history.selected)runtime.label_update.reset();if(s.histories.size()>8){for(auto it=s.histories.begin();it!=s.histories.end() && s.histories.size()>8;)if(it->first!=id){if(auto* retired=s.documents.Find(it->first))retired->history.metadata_loaded=false;it=s.histories.erase(it);}else ++it;}
    const auto view=View(s,host,id);const auto* ledger=view && view->ledger && view->ledger->Observed()?view->ledger:nullptr;runtime.observed=ledger?ledger->Observed():0;runtime.generation=view?view->generation:0;runtime.sequence=view?view->history_sequence:0;
    if(!ledger){runtime.projection={};runtime.chart={};}
    const bool selected=ledger && ledger->Rows().contains(d->history.selected);const bool online=view && view->identity && !view->offline && (!s.recovery || !s.recovery->Locked());
    imkit::PushTextRole(theme,imkit::TextRole::Heading);ImGui::TextUnformatted("Transactions");imkit::PopTextRole();
    if(ledger){
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{ImGui::GetStyle().FramePadding.x,2*scale});
    theme::SameLineForAction("Scan earlier");Button(s,f,"scan-earlier","Scan earlier",theme,online && !view->busy && !ledger->Complete() && !ledger->Scanning() && !ledger->Pruned() && !view->scan_blocked && (!ledger->Limited() || view->scan_budget<4096),[&s,&host,id]{auto v=View(s,host,id);if(!v || !v->identity || s.documents.ActiveId()!=id || (s.recovery && s.recovery->Locked()))return false;const bool accepted=host.Refresh()->ScanEarlier(*v->identity,RefreshMonotonicMs());if(!accepted)s.histories.at(id).error="Scan could not start: offline, busy, scope changed or bounded scan exhausted. Existing rows and cursor are retained.";return accepted;});
    const bool explorer_ready=selected && online && !s.privacy.hidden && ExplorerTarget(s,host,*view->identity,d->history.selected).has_value();
    theme::SameLineForAction("Explorer...");Button(s,f,"explorer","Explorer...",theme,explorer_ready,[&s,&host,id]{
        auto v=View(s,host,id);auto* doc=s.documents.Find(id);
        if(!Usable(s,id) || !v || v->offline || !v->identity || !doc || !v->ledger->Rows().contains(doc->history.selected))return false;
        auto url=ExplorerTarget(s,host,*v->identity,doc->history.selected);
        if(!url)return false;
        s.histories.at(id).explorer_permission=HistoryPermission{*v->identity,doc->history.selected,v->ledger->Observed(),v->generation,v->history_sequence,std::move(*url)};
        return true;
    });
    if(!explorer_ready && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Enable a supported explorer in Preferences for this online direct RPC profile.");
    theme::SameLineForAction("Fee actions");Button(s,f,"fee-actions","Fee actions",theme,selected && online && !s.privacy.hidden,[&s,id]{if(!Usable(s,id))return false;s.histories.at(id).fee_actions=!s.histories.at(id).fee_actions;return true;});
    ImGui::PopStyleVar();
    }
    const auto observed=ledger?locale::LocalDate(ledger->Observed()/1000,true):std::string("No dated history observation");imkit::PushTextRole(theme,imkit::TextRole::Caption);ImGui::TextWrapped("%s%s",observed.c_str(),ledger && !ledger->Complete()?" / incomplete; balance Unknown":"");imkit::PopTextRole();s.presentation.Annotate("history-date","History observation",observed);if(view){const auto freshness=view->freshness[2];const char* name=freshness==Freshness::Offline?"Offline cache":freshness==Freshness::Stale?"Refresh failed; prior dated rows retained":freshness==Freshness::Cached?"Cached observation":"Read-only wallet observation";if(freshness==Freshness::Offline || freshness==Freshness::Stale || freshness==Freshness::Cached){ImGui::SameLine();imkit::PushTextRole(theme,imkit::TextRole::Caption);ImGui::TextUnformatted(name);imkit::PopTextRole();}s.presentation.Annotate("history-source","History source",name);}
    if(!ledger){runtime.explorer_permission.reset();const bool loading=view && view->busy;const char* message=loading?"Loading read-only wallet history. No opening balance is assumed.":"History is unavailable. Authenticate, test and apply this existing daemon wallet in Server settings. Restored documents do not reconnect automatically.";ImGui::TextWrapped("%s",message);s.presentation.Annotate("history-unavailable","History",loading?"Loading":"Unavailable; reconnect this wallet explicitly");return;}
    const char* scan=ledger->Scanning()?"Daemon rescan in progress; earlier history is unavailable until it finishes.":ledger->Pruned()?"Pruned source: older history may be unavailable. Use a source with the required blocks; this action never rescans the daemon.":(view->scan_blocked || (ledger->Limited() && view->scan_budget>=4096))?"Scan stopped without progress at the entry/coin bound. Prior rows retained; no cursor was advanced. A smaller wallet history or future indexed source is required.":ledger->Limited()?"Partial scan: bounded history retained; earlier balances Unknown. Choose Scan earlier to re-read the same range with a larger budget.":!ledger->Complete()?"Partial history: opening/running balances Unknown. Scan earlier queries only history already known to this daemon wallet.":view->busy?"Refreshing; prior dated rows retained.":"";
    if(*scan){const char* brief=ledger->Scanning()?"Daemon rescan in progress":ledger->Pruned()?"Pruned source; earlier history unavailable":(view->scan_blocked || (ledger->Limited() && view->scan_budget>=4096))?"Scan bound reached; no further progress":ledger->Limited()?"Partial scan; earlier balances Unknown":!ledger->Complete()?"Partial history; opening balance Unknown":"Refreshing dated history";imkit::PushTextRole(theme,imkit::TextRole::Caption);ImGui::TextUnformatted(brief);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",scan);imkit::PopTextRole();s.presentation.Annotate("history-scan","History scan",scan);}
    if(runtime.explorer_permission && (!Usable(s,id) || !view || view->offline || !view->identity || *view->identity!=runtime.explorer_permission->identity || d->history.selected!=runtime.explorer_permission->txid || ledger->Observed()!=runtime.explorer_permission->observed || view->generation!=runtime.explorer_permission->generation || view->history_sequence!=runtime.explorer_permission->sequence || ExplorerTarget(s,host,*view->identity,d->history.selected)!=runtime.explorer_permission->url))runtime.explorer_permission.reset();
    if(!d->history.metadata_loaded && runtime.error.empty())LoadLabels(s,id);
    const int summary_columns=ImGui::GetContentRegionAvail().x>=425*scale?2:1;
    if(ImGui::BeginTable("history-upper",summary_columns,ImGuiTableFlags_SizingStretchSame)){
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{8*scale,3*scale});ImGui::TableNextColumn();
        imkit::PushTextRole(theme,imkit::TextRole::Caption);
        const float summary_height=5*ImGui::GetTextLineHeightWithSpacing();
        ImGui::BeginChild("history-summary",{0,summary_height},ImGuiChildFlags_None);
        const auto& t=ledger->Totals();
        for(const auto& entry:std::initializer_list<std::pair<const char*,std::optional<Money>>>{{"Confirmed",t.confirmed},{"Mempool",t.mempool},{"Immature",t.has_rpc?std::optional(t.rpc.immature):std::nullopt},{"RPC fundable",t.rpc_fundable}}){ImGui::TextUnformatted(entry.first);ImGui::SameLine(112*scale);Amount(s,entry.first,entry.first,entry.second);}ImGui::TextUnformatted("Locally eligible");ImGui::SameLine(112*scale);Amount(s,"spendability","Locally eligible amount; no signing custody or funding authority",view->chain_current?ledger->Totals().spendable:std::nullopt);imkit::PopTextRole();ImGui::EndChild();
        ImGui::TableNextColumn();ImGui::BeginChild("history-chart-panel",{0,78*scale},ImGuiChildFlags_None);Chart(s,host,f,*ledger,theme,scale,id);ImGui::EndChild();ImGui::PopStyleVar();ImGui::EndTable();
    }
    auto filter=std::array<char,256>{};if(!s.privacy.hidden)std::copy(d->history.filter.begin(),d->history.filter.end(),filter.begin());std::optional<std::string> pending;ImGui::SetNextItemWidth(std::max(80.F,ImGui::GetContentRegionAvail().x-156*scale));ImGui::BeginDisabled(s.privacy.hidden);const bool changed=c::TextInput("###history-filter","Filter transaction or label",filter.data(),filter.size(),pending);f.history["filter"]=Last();const auto filter_external=s.presentation.Interact("Filter transactions",c::ax::SemanticRole::TextField,c::ax::SemanticAction::SetValue|c::ax::SemanticAction::Focus,s.privacy.hidden,false,s.privacy.hidden?"":d->history.filter);ImGui::EndDisabled();if(changed && !s.privacy.hidden)d->history.filter=filter.data();if(filter_external)s.presentation.Defer(*filter_external,[&s,id](const c::SemanticRequest& r){auto* current=s.documents.Find(id);if(!Usable(s,id) || !current || r.value.size()>255 || r.value.find('\0')!=std::string::npos)return false;current->history.filter=r.value;return true;});
    ImGui::SameLine();Button(s,f,"csv","Export CSV",theme,!s.privacy.hidden && c::native_input && c::native_input->FileChooserAvailable() && !runtime.file_dialog,[&s,&host,id]{auto v=View(s,host,id);auto* doc=s.documents.Find(id);if(!Usable(s,id) || !doc || !v || !c::native_input)return false;auto& r=s.histories.at(id);auto rows=a::OrderedHistory(*v->ledger,doc->history,r.labels);auto csv=a::HistoryCsv(*v->ledger,rows,r.labels,false,v->chain_current);if(!csv){r.error="CSV was not saved: unsupported control characters or snapshot limits. Original labels and files are retained.";return false;}r.export_data=std::move(*csv);r.export_scope=ExportScope(s,id);r.export_identity=*v->identity;r.file_dialog=c::native_input->ChooseFile("transactions.csv",true);return bool(r.file_dialog);});
    if(runtime.file_dialog){auto export_view=View(s,host,id);if(!Usable(s,id) || !export_view || !export_view->identity || !runtime.export_identity || *export_view->identity!=*runtime.export_identity || runtime.export_scope!=ExportScope(s,id)){runtime.export_data.clear();runtime.file_dialog=0;}else if(c::native_input){const auto choice=c::native_input->PollFile(runtime.file_dialog);if(choice.outcome!=c::NativeOutcome::Pending){if(choice.outcome==c::NativeOutcome::Chosen)WriteCsv(choice.path,runtime.export_data,runtime.error);else if(choice.outcome!=c::NativeOutcome::Cancelled)runtime.error="CSV was not saved. Choose Export CSV to retry.";runtime.file_dialog=0;runtime.export_data.clear();}}}
    if(!runtime.error.empty()){const auto message=s.privacy.hidden && runtime.error!=DocumentLimitReason?"History operation details hidden.":runtime.error;ImGui::TextWrapped("%s",message.c_str());s.presentation.Annotate("history-error","History status",message,c::ax::SemanticRole::Alert);}
    if(s.recovery){Button(s,f,"reload-labels","Reload saved labels",theme,!s.privacy.hidden,[&s,id]{return Usable(s,id) && LoadLabels(s,id);});ImGui::SameLine();}
    if(Button(s,f,"open","Open transaction",theme,selected,[&s,&host,id]{auto v=View(s,host,id);const auto* doc=s.documents.Find(id);if(!doc || !doc->binding || !v || s.documents.ActiveId()!=id || !v->ledger->Rows().contains(doc->history.selected))return false;const auto txid=*Hash256::Parse(doc->history.selected);const auto opened=s.documents.OpenObservedTransaction({*doc->binding,txid});if(!opened){s.histories.at(id).error=DocumentLimitReason;return false;}if(s.histories.at(id).error==DocumentLimitReason)s.histories.at(id).error.clear();RestoreDocumentFocus(s,opened);return true;}))return;
    if(selected){ImGui::SameLine();Button(s,f,"copy-txid","Copy txid",theme,true,[&s,&host,id]{auto* doc=s.documents.Find(id);if(!doc || s.documents.ActiveId()!=id || (s.recovery && s.recovery->Locked()))return false;const a::Request request{a::Command::Component,id,a::Section::Transactions,c::CopyIdentifier{"history-txid",doc->history.selected}};return host.CanSubmit(request).available && host.Submit(request).accepted;});}
    if(selected){ImGui::SameLine();Button(s,f,"copy-value","Copy value",theme,!s.privacy.hidden,[&s,&host,id]{auto view=View(s,host,id);const auto* doc=s.documents.Find(id);if(!Usable(s,id) || !view || !doc)return false;const auto found=view->ledger->Rows().find(doc->history.selected);if(found==view->ledger->Rows().end())return false;const a::Request request{a::Command::Component,id,a::Section::Transactions,c::CopyAmount{"history-value",found->second.wallet_delta}};return host.CanSubmit(request).available && host.Submit(request).accepted;});}
    if(runtime.explorer_permission){
        const auto permission=*runtime.explorer_permission;
        const auto prompt="Open "+permission.url+"? Your browser sends this transaction ID and exposes your network identity. Permission applies once.";
        ImGui::TextWrapped("%s",prompt.c_str());s.presentation.Annotate("explorer-permission","External explorer permission",prompt);
        Button(s,f,"explorer-allow","Allow once",theme,true,[&s,&host,id,permission]{
            auto v=View(s,host,id);const auto* doc=s.documents.Find(id);
            if(!Usable(s,id) || !v || v->offline || !v->identity || *v->identity!=permission.identity || !doc
                || doc->history.selected!=permission.txid || v->ledger->Observed()!=permission.observed
                || v->generation!=permission.generation || v->history_sequence!=permission.sequence
                || !s.histories.at(id).explorer_permission || ExplorerTarget(s,host,*v->identity,permission.txid)!=permission.url)return false;
            s.histories.at(id).explorer_permission.reset();
            const auto receipt=host.OpenExplorer({permission.identity.context,*Hash256::Parse(permission.txid),true,permission.generation,permission.sequence,permission.url});
            s.histories.at(id).error=receipt.reason;return receipt.accepted;
        });
        ImGui::SameLine();Button(s,f,"explorer-cancel","Cancel",theme,true,[&s,id]{s.histories.at(id).explorer_permission.reset();return true;});
    }
    if (runtime.fee_actions && !s.privacy.hidden) {
        const auto txid=Hash256::Parse(d->history.selected);
        std::optional<a::FeeDraftRequest> action;
        auto* replacement=host.ReplacementDrafts();
        const bool busy=replacement && replacement->Busy();
        if (txid && selected && view->identity) {
            std::array<char,16> rate{};
            std::ranges::copy(runtime.replacement_rate,rate.begin());
            std::optional<std::string> pending_rate;
            ImGui::TextUnformatted("New fee rate (sat/vB)");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120*scale);
            ImGui::BeginDisabled(busy);
            c::TextInput("###replacement-rate","New fee rate (sat/vB)",rate.data(),rate.size(),pending_rate);
            if (!busy) runtime.replacement_rate=rate.data();
            const auto edit_rate=s.presentation.Interact("New fee rate (sat/vB)",c::ax::SemanticRole::TextField,
                c::ax::SemanticAction::SetValue|c::ax::SemanticAction::Focus,busy,false,runtime.replacement_rate);
            ImGui::EndDisabled();
            if (edit_rate) s.presentation.Defer(*edit_rate,[&s,&host,id,txid](const c::SemanticRequest& event) {
                const auto* current=s.documents.Find(id);
                if (!Usable(s,id) || !current || current->history.selected!=txid->hex()
                    || (host.ReplacementDrafts() && host.ReplacementDrafts()->Busy()) || event.value.size()>15
                    || !std::ranges::all_of(event.value,[](char ch){return (ch>='0' && ch<='9') || ch=='.';})) return false;
                s.histories.at(id).replacement_rate=event.value;
                return true;
            });
            action=a::FeeDraftRequest{view->identity->context,*txid,a::FeeDraftKind::ReplaceByFee,
                view->generation,view->history_sequence,ParseFundingRate(runtime.replacement_rate).value_or(0)};
        }
        const auto available=action ? ReplacementAvailability(s,host,*action) : a::Availability{false,"Select an outgoing wallet transaction."};
        if (action) ImGui::SameLine();
        Button(s,f,"fee-rbf","Increase Fee",theme,available.available,[&s,&host,id,action] {
            if (!action || !Usable(s,id) || ParseFundingRate(s.histories.at(id).replacement_rate)!=action->rate_per_kvb) return false;
            const auto receipt=host.FeeDraft(*action);
            s.histories.at(id).error=receipt.reason;
            return receipt.accepted;
        });
        if (busy && replacement->Running() && replacement->Running()->document==id) {
            ImGui::SameLine();
            Button(s,f,"fee-rbf-cancel","Cancel fee request",theme,true,[&s,&host,id] {
                auto* service=host.ReplacementDrafts();
                return Usable(s,id) && service && service->Running() && service->Running()->document==id && service->Cancel();
            });
        } else if (!available.available) {
            ImGui::TextWrapped("%s",available.reason.c_str());
            s.presentation.Annotate("fee-rbf-unavailable","Increase Fee availability",available.reason);
        }
        auto cancel=action;
        if (cancel) cancel->kind=a::FeeDraftKind::CancelPayment;
        if (runtime.cancellation_prompt && runtime.cancellation_prompt!=cancel) runtime.cancellation_prompt.reset();
        const auto cancellation=cancel ? ReplacementAvailability(s,host,*cancel) : a::Availability{false,"Select an outgoing wallet payment."};
        if (!runtime.cancellation_prompt) {
            Button(s,f,"fee-cancel","Attempt cancellation",theme,cancellation.available,[&s,&host,id,cancel] {
                if (!cancel || !Usable(s,id) || !ReplacementAvailability(s,host,*cancel).available) return false;
                s.histories.at(id).cancellation_prompt=cancel;
                return true;
            });
            if (!cancellation.available && (!action || available.reason!=cancellation.reason))
                s.presentation.Annotate("fee-cancel-unavailable","Attempt cancellation availability",cancellation.reason);
        } else {
            constexpr auto warning="This draft pays an existing address in your wallet. It competes with the original payment. "
                "The original may confirm first and node policy can refuse the new transaction. Cancellation is never guaranteed. "
                "Preparation uses the existing address and makes no wallet changes.";
            ImGui::TextWrapped("%s",warning);
            s.presentation.Annotate("fee-cancel-warning","Competing self-payment warning",warning,c::ax::SemanticRole::Alert);
            Button(s,f,"fee-cancel-prepare","Create competing self-payment",theme,cancellation.available,[&s,&host,id,cancel] {
                if (!cancel || !Usable(s,id) || s.histories.at(id).cancellation_prompt!=cancel
                    || ParseFundingRate(s.histories.at(id).replacement_rate)!=cancel->rate_per_kvb) return false;
                auto result=host.FeeDraft(*cancel);
                auto& current=s.histories.at(id);
                current.error=result.reason;
                current.cancellation_prompt.reset();
                return result.accepted;
            });
            ImGui::SameLine();
            Button(s,f,"fee-cancel-dismiss","Dismiss",theme,true,[&s,id] {
                if (!Usable(s,id)) return false;
                s.histories.at(id).cancellation_prompt.reset();
                return true;
            });
        }
        if (runtime.child_parent!=txid) {
            runtime.child_parent.reset();
            runtime.child_output.reset();
        }
        if (!runtime.child_parent) {
            Button(s,f,"fee-child","Increase effective fee",theme,action.has_value() && online && view->chain_current && !busy,
                [&s,id,txid] {
                    const auto* doc=s.documents.Find(id);
                    if (!Usable(s,id) || !doc || !txid || doc->history.selected!=txid->hex()) return false;
                    s.histories.at(id).child_parent=txid;
                    return true;
                });
        } else {
            ImGui::TextWrapped("Select an unconfirmed wallet output. The rate above targets the parent, ancestors and child together. "
                "The child pays the remaining value to the selected output's existing wallet address.");
            size_t shown{};
            for (const auto& coin:view->ledger->Coins()) {
                if (!txid || coin.outpoint.txid!=*txid || coin.confirmations || !coin.solvable) continue;
                if (++shown>64) break;
                const auto index=coin.outpoint.index;
                const auto text="Select output #"+std::to_string(index)+" ("+std::to_string(coin.amount.sats())+" sat)";
                const auto key="fee-child-output-"+std::to_string(index);
                Button(s,f,key.c_str(),text.c_str(),theme,!busy,[&s,&host,id,txid,index] {
                    auto current=View(s,host,id);
                    const auto* doc=s.documents.Find(id);
                    if (!Usable(s,id) || !current || !current->ledger || !doc || !txid
                        || doc->history.selected!=txid->hex() || s.histories.at(id).child_parent!=txid) return false;
                    const auto row=std::ranges::find(current->ledger->Coins(),Outpoint{*txid,index},&HistoryCoin::outpoint);
                    if (row==current->ledger->Coins().end() || row->confirmations || !row->solvable) return false;
                    s.histories.at(id).child_output=index;
                    return true;
                });
            }
            if (!shown) ImGui::TextUnformatted("No current wallet-owned unconfirmed outputs are available.");
            if (runtime.child_output) ImGui::Text("Selected output #%u",*runtime.child_output);
            auto child=action;
            if (child) {
                child->kind=a::FeeDraftKind::ChildPaysForParent;
                child->parent_output=runtime.child_output;
            }
            const auto child_available=child ? ReplacementAvailability(s,host,*child)
                : a::Availability{false,"Select a parent in current wallet history."};
            Button(s,f,"fee-child-prepare","Create unsigned child",theme,child_available.available,[&s,&host,id,child] {
                if (!child || !Usable(s,id) || !child->parent_output || s.histories.at(id).child_output!=child->parent_output
                    || ParseFundingRate(s.histories.at(id).replacement_rate)!=child->rate_per_kvb) return false;
                const auto reply=host.FeeDraft(*child);
                s.histories.at(id).error=reply.reason;
                return reply.accepted;
            });
            ImGui::SameLine();
            Button(s,f,"fee-child-back","Hide child options",theme,!busy,[&s,id] {
                if (!Usable(s,id)) return false;
                s.histories.at(id).child_parent.reset();
                s.histories.at(id).child_output.reset();
                return true;
            });
            if (!child_available.available) {
                ImGui::TextWrapped("%s",child_available.reason.c_str());
                s.presentation.Annotate("fee-child-availability","Child draft availability",child_available.reason);
            }
        }
    }

    if(!s.privacy.hidden)for(const auto& [txid,draft]:d->history.drafts)if(!draft.error.empty()){ImGui::TextWrapped("%s",draft.error.c_str());s.presentation.Annotate(("label-error-"+txid).c_str(),"Label save failed",draft.error,c::ax::SemanticRole::Alert);}
    PrepareRows(runtime,*ledger,d->history,s.privacy.hidden,f.work);
    const auto& projection=runtime.projection;
    f.history_order=projection.keys;
    if(projection.keys.empty()){ImGui::TextWrapped("%s",ledger->Rows().empty()?"No transactions in this observed wallet.":"No transactions match the filter.");s.presentation.Annotate("history-empty","Transactions",ledger->Rows().empty()?"No transactions":"No matching transactions");return;}
    const auto& flat=projection.rows;
    const float row_height=ImGui::GetFrameHeight()+2*ImGui::GetStyle().CellPadding.y;
    const float minimum_table_height=2*row_height+ImGui::GetStyle().ScrollbarSize;
    if(ImGui::BeginTable("history-tree",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_ScrollY|ImGuiTableFlags_ScrollX|ImGuiTableFlags_SizingStretchProp,{0,std::max(minimum_table_height,ImGui::GetContentRegionAvail().y)},0)){
        const auto grid_min=ImGui::GetWindowDrawList()->GetClipRectMin(),grid_max=ImGui::GetWindowDrawList()->GetClipRectMax();s.presentation.Group("history-grid","Wallet transactions",c::ax::SemanticRole::Grid,grid_min,grid_max);
        ImGui::TableSetupScrollFreeze(0,1);for(int column=0;column<4;++column){const char* names[]={"Date (UTC)","Label",s.unit==1?"Value (sats)":s.unit==2?"Value":"Value (BTC)",s.unit==1?"Balance (sats)":s.unit==2?"Balance":"Balance (BTC)"};const auto flags=column==d->history.sort?ImGuiTableColumnFlags_DefaultSort|(d->history.descending?ImGuiTableColumnFlags_PreferSortDescending:ImGuiTableColumnFlags_None):ImGuiTableColumnFlags_None;ImGui::TableSetupColumn(locale::Text(names[column]),flags,column==1?1.4F:1.2F,column);}ImGui::TableNextRow(ImGuiTableRowFlags_Headers);for(int column=0;column<4;++column){ImGui::TableSetColumnIndex(column);ImGui::PushID(column);const auto sort=[&s,id,column]{auto* doc=s.documents.Find(id);if(!doc || s.documents.ActiveId()!=id || (s.recovery && s.recovery->Locked()) || (s.privacy.hidden && column>0))return false;doc->history.descending=doc->history.sort==column?!doc->history.descending:false;doc->history.sort=column;s.histories.at(id).reveal_selected=!doc->history.selected.empty();return true;};const bool disabled=s.privacy.hidden && column>0;ImGui::BeginDisabled(disabled);const auto label=std::string(ImGui::TableGetColumnName(column))+(d->history.sort==column?(d->history.descending?" v":" ^"):"")+"###sort-header";const bool pressed=ImGui::Selectable(label.c_str(),false,0,{0,ImGui::GetFrameHeight()});f.history["sort-"+std::to_string(column)]=Last(pressed);const auto external=s.presentation.Interact(ImGui::TableGetColumnName(column),c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus,disabled,false,d->history.sort==column?(d->history.descending?"Descending":"Ascending"):"Not sorted");ImGui::EndDisabled();bool accepted=pressed && sort();if(external)s.presentation.Defer(*external,[sort,pressed,accepted](const c::SemanticRequest&){return pressed?accepted:sort();});ImGui::PopID();}
        auto& position=d->sections[size_t(a::Section::Transactions)];if(position.restore_table){ImGui::SetScrollX(position.table_x);ImGui::SetScrollY(position.table_y);position.restore_table=false;}
        ImGuiListClipper clipper;clipper.Begin(int(flat.size()),row_height);if(runtime.reveal_selected)if(const auto selected_row=projection.positions.find(d->history.selected);selected_row!=projection.positions.end())clipper.IncludeItemByIndex(int(selected_row->second));while(clipper.Step())for(int index=clipper.DisplayStart;index<clipper.DisplayEnd;++index){++f.work.history_drawn;const auto row=flat[size_t(index)];const auto txid=row.tx->txid.hex();ImGui::PushID(txid.c_str());if(row.io)ImGui::PushID((row.io->kind+":"+row.io->reference).c_str());else if(row.detail)ImGui::PushID((row.detail->category+":"+std::to_string(row.detail->vout)).c_str());ImGui::TableNextRow(ImGuiTableRowFlags_None,row_height);ImGui::TableNextColumn();
            if(!row.detail && !row.io){const bool expanded=d->history.expanded.contains(txid);if(ImGui::SmallButton(expanded?"−":"+")){if(expanded)d->history.expanded.erase(txid);else d->history.expanded.insert(txid);runtime.projection.expanded_dirty=true;runtime.reveal_selected=d->history.selected==txid;}f.history["expand-"+txid]=Last();const auto expand=s.presentation.Interact(expanded?"Collapse transaction":"Expand transaction inputs and outputs",c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus);if(expand)s.presentation.Defer(*expand,[&s,id,txid](const c::SemanticRequest&){auto* doc=s.documents.Find(id);if(!doc || s.documents.ActiveId()!=id)return false;if(!doc->history.expanded.erase(txid))doc->history.expanded.insert(txid);auto& runtime=s.histories.at(id);runtime.projection.expanded_dirty=true;runtime.reveal_selected=doc->history.selected==txid;return true;});ImGui::SameLine();const auto date=locale::LocalDate(row.tx->time);if(ImGui::Selectable((date+"###row").c_str(),d->history.selected==txid,ImGuiSelectableFlags_SpanAllColumns|ImGuiSelectableFlags_AllowOverlap,{0,ImGui::GetFrameHeight()}))d->history.selected=txid;f.history["row-"+txid]=Last();if(runtime.reveal_selected && d->history.selected==txid){ImGui::SetScrollHereY(.5F);runtime.reveal_selected=false;}const auto select=s.presentation.Interact("Transaction",c::ax::SemanticRole::Row,c::ax::SemanticAction::Select|c::ax::SemanticAction::Focus,false,d->history.selected==txid,date+" / "+a::HistoryStatus(*ledger,*row.tx,view->chain_current)+" / "+txid);s.presentation.Parent(ImGui::GetItemID());if(select)s.presentation.Defer(*select,[&s,id,txid](const c::SemanticRequest&){auto* doc=s.documents.Find(id);if(!doc || s.documents.ActiveId()!=id)return false;doc->history.selected=txid;return true;});if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s\n%s",txid.c_str(),locale::LocalDate(row.tx->time,true).c_str(),a::HistoryStatus(*ledger,*row.tx,view->chain_current).c_str());
            }else {const auto group_at=ImGui::GetCursorScreenPos();const auto name=std::string(row.io?row.io->kind:"Wallet output")+" of transaction "+txid;s.presentation.Group("io-row",name.c_str(),c::ax::SemanticRole::Row,group_at,{grid_max.x,group_at.y+ImGui::GetFrameHeight()});const auto text=row.io?"  "+row.io->kind+" "+row.io->reference:"  Wallet output "+std::to_string(row.detail->vout)+" / "+row.detail->category;ImGui::TextUnformatted(text.c_str());s.presentation.Annotate("io-kind","Input or output observation",text);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",text.c_str());}
            ImGui::TableNextColumn();
            if(s.privacy.hidden){ImGui::TextUnformatted("Hidden");s.presentation.Annotate("hidden-label","Label","Hidden");}
            else if(!row.detail && !row.io && d->history.selected==txid && UnsupportedLabel(Label(*row.tx,runtime))){const auto escaped=rpc::Quote(Label(*row.tx,runtime));ImGui::TextUnformatted(escaped.c_str());s.presentation.Annotate("invalid-daemon-label","Daemon label contains unsupported controls; shown escaped",escaped);if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",escaped.c_str());}
            else if(!row.detail && !row.io && d->history.selected==txid && s.recovery && d->history.metadata_loaded){auto found=d->history.drafts.find(txid);if(found==d->history.drafts.end() && d->history.drafts.size()>=32){for(auto it=d->history.drafts.begin();it!=d->history.drafts.end();++it)if(!it->second.dirty()){d->history.drafts.erase(it);break;}}if(found==d->history.drafts.end() && d->history.drafts.size()<32){const auto label=Label(*row.tx,runtime);found=d->history.drafts.emplace(txid,a::HistoryLabelDraft{label,label,"",d->history.metadata_revision}).first;}if(found!=d->history.drafts.end()){if(runtime.editor && runtime.editor->txid!=txid)RetireLabelEditor(s,host,id);auto& draft=found->second;std::array<char,4097> buffer{};std::copy(draft.draft.begin(),draft.draft.end(),buffer.begin());std::optional<std::string> external;if(runtime.label_update && runtime.label_update->first==txid){if(runtime.label_update->second==draft.draft)external=std::move(runtime.label_update->second);runtime.label_update.reset();}const bool external_update=external.has_value();ImGui::SetNextItemWidth(-1);const bool enter=c::TextInput("###inline-label","Label; Enter or leave",buffer.data(),buffer.size(),external,ImGuiInputTextFlags_EnterReturnsTrue);const bool active=ImGui::IsItemActive(),activated=ImGui::IsItemActivated(),edited=ImGui::IsItemEdited(),finished=ImGui::IsItemDeactivatedAfterEdit();f.history["label-"+txid]=Last();draft.draft=buffer.data();
                if(active && activated && !runtime.editor)runtime.editor=HistoryLabelEditor{*view->identity,txid,d->history.metadata_revision,false,false,false,!draft.error.empty()};
                if(runtime.editor && runtime.editor->txid==txid){runtime.editor->seen=true;runtime.editor->active=active;runtime.editor->finished=finished;if(edited && !external_update)runtime.editor->attempted=false;}const auto edit=s.presentation.Interact("Transaction label; Enter or focus loss saves",c::ax::SemanticRole::TextField,c::ax::SemanticAction::SetValue|c::ax::SemanticAction::Focus,false,false,draft.draft,draft.error,!draft.error.empty());if(enter)SaveLabel(s,host,id,txid);if(edit)s.presentation.Defer(*edit,[&s,&host,id,txid](const c::SemanticRequest& r){auto* doc=s.documents.Find(id);if(!Usable(s,id) || !doc || r.value.size()>4096 || r.value.find('\0')!=std::string::npos)return false;auto it=doc->history.drafts.find(txid);if(it==doc->history.drafts.end())return false;it->second.draft=r.value;s.histories.at(id).label_update=std::pair(txid,r.value);return SaveLabel(s,host,id,txid);});}else ImGui::TextUnformatted("Draft limit; save or clear other drafts");}
            else {const auto text=row.io?row.io->destination:row.detail?row.detail->address:Label(*row.tx,runtime);const auto display=std::any_of(text.begin(),text.end(),[](unsigned char c){return c<32 || c==127;})?rpc::Quote(text):text;ImGui::TextUnformatted(display.c_str());if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",rpc::Quote(text).c_str());s.presentation.Annotate("history-label",(row.detail || row.io)?"Input or output destination":"Transaction label",display);}
            ImGui::TableNextColumn();Amount(s,"history-value",row.io?"Input or output value":row.detail?"Output wallet value":"Transaction value",row.io?row.io->value:std::optional(row.detail?row.detail->amount.value:row.tx->wallet_delta));ImGui::TableNextColumn();if(!row.detail && !row.io)Amount(s,"history-balance","Chronological balance including immature",row.tx->running_balance);else ImGui::TextUnformatted("—");
            s.presentation.EndGroup();if(row.detail || row.io)ImGui::PopID();ImGui::PopID();
        }position.table_x=ImGui::GetScrollX();position.table_y=ImGui::GetScrollY();s.presentation.EndGroup();ImGui::EndTable();
    }

}
void DrawObservedTransaction(State& s,Facts& f,const imkit::Theme& theme,float){const auto* d=s.documents.Active();if(!d || !d->observed_transaction)return;const auto reference=*d->observed_transaction;ImGui::TextUnformatted("Transaction document");ImGui::TextWrapped("Read-only observation reference. Opening this document does not import, sign or broadcast a transaction.");ImGui::TextWrapped("Transaction: %s",reference.txid.hex().c_str());s.presentation.Annotate("observed-transaction","Transaction identifier",reference.txid.hex());const auto* wallet=a::RegisteredWallet(s.documents,reference.wallet);if(wallet){ImGui::TextUnformatted("Return to the wallet Transactions section for dated values and output observations.");Button(s,f,"show-wallet","Show in wallet",theme,true,[&s,reference]{const auto* active=s.documents.Active();const auto* current=a::RegisteredWallet(s.documents,reference.wallet);if(!active || active->observed_transaction!=std::optional(reference) || !current)return false;const auto id=current->id;s.documents.Activate(id);s.documents.Navigate(a::Section::Transactions);s.documents.Find(id)->history.selected=reference.txid.hex();s.histories[id].reveal_selected=true;RestoreDocumentFocus(s,id);return true;});}else ImGui::TextWrapped("The wallet view is closed. Reconnect the existing wallet explicitly to obtain observations.");}
}
