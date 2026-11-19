// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include <algorithm>
#include <utility>

namespace roots::gui::ui::shell {
namespace {
namespace a=app;namespace c=components;
constexpr char POPUP[]="Wallet tools###wallet-tools";

bool SearchPanel(WalletTool tool){return tool==WalletTool::CurrentSearch || tool==WalletTool::AllSearch;}
const char* Title(WalletTool tool)
{
    if(SearchPanel(tool))return "Search wallets";
    return tool==WalletTool::Summary?"Wallet balance summary":"Node information";
}
bool Current(const State& state,a::Requests& host)
{
    return state.wallet_tools.dialog.open && state.wallet_tools.scope==CurrentToolScope(state,host);
}
a::WalletSearchQuery Draft(const WalletToolsPanel& panel)
{
    return {std::string(panel.search.draft()),panel.search_type,panel.tool==WalletTool::AllSearch?0:panel.scope.document};
}
c::ItemFacts Last(bool pressed=false,bool disabled=false)
{
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    return {{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),disabled,false,pressed,ImGui::IsItemVisible()};
}
void Text(State& state,const char* id,const char* label,std::string_view value,c::ax::SemanticRole role=c::ax::SemanticRole::Text)
{
    const std::string display(value);
    ImGui::TextWrapped("%s",display.c_str());
    state.presentation.Annotate(id,label,value,role);
}
void Button(State& state,Facts& facts,const imkit::Theme& theme,const char* id,const char* title,
    bool enabled,const std::function<bool()>& action)
{
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    const bool pressed=theme::ActionButton(title,imkit::ActionVariant::Secondary,{},theme);
    facts.tools[id]=Last(pressed,!enabled);
    const auto external=state.presentation.Interact(title,c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus,!enabled);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted=pressed && enabled && action();
    if(external)state.presentation.Defer(*external,[action,pressed,accepted](const auto&){return pressed?accepted:action();});
}
a::WalletSearchResult Search(const State& state,a::Requests& host,const a::WalletSearchQuery& query)
{
    const auto scope=CurrentToolScope(state,host);
    const auto* refresh=host.Refresh();
    if(scope.locked || scope.hidden || !refresh)return {};
    return a::SearchWallets(state.documents,*refresh,host.Addresses(),scope.epoch,query,[&state](const auto& binding){
        if(!state.recovery || state.recovery->Locked() || state.recovery->Pending())return std::optional<storage::Metadata>{};
        return state.recovery->HistoryMetadata(a::HistoryMetadataScope(binding)).value;
    });
}
bool Show(State& state,a::Requests& host)
{
    auto& panel=state.wallet_tools;
    if(!Current(state,host) || panel.query!=Draft(panel) || !panel.results || !panel.selected || *panel.selected>=panel.results->hits.size())return false;
    const auto hit=panel.results->hits[*panel.selected];
    const auto view=Search(state,host,*panel.query);
    if(std::ranges::find(view.hits,hit)==view.hits.end()){
        panel.notice="That wallet observation changed or was removed. Search again.";
        return false;
    }
    auto* document=state.documents.Find(hit.document);
    if(!document || !a::ToolWallet(*document) || !state.documents.Activate(hit.document))return false;
    switch(hit.type){
    case a::SearchType::Transaction:
        state.documents.Navigate(a::Section::Transactions);
        document->history.filter.clear();document->history.selected=hit.key;
        state.histories[hit.document].reveal_selected=true;
        break;
    case a::SearchType::Address:
        state.documents.Navigate(a::Section::Addresses);
        document->addresses.hide_empty_used=false;document->addresses.selected=hit.key;
        state.addresses[hit.document].reveal_selected=true;
        state.retained_address_navigation=RetainedAddressNavigation{hit.document,panel.scope.connection};
        break;
    case a::SearchType::Output:
        state.documents.Navigate(a::Section::UTXOs);
        document->coins.focused=hit.key;
        state.coins[hit.document].reveal=true;
        break;
    case a::SearchType::Any:return false;
    }
    RestoreDocumentFocus(state,hit.document);
    RetireWalletTool(state);
    return true;
}
std::string CurrentSearchCsv(const State& state,a::Requests& host)
{
    const auto& panel=state.wallet_tools;
    if(!Current(state,host) || panel.query!=Draft(panel) || !panel.results)return {};
    const auto current=Search(state,host,*panel.query);
    if(current!=*panel.results)return {};
    return a::WalletSearchCsv(current,panel.show_wallet);
}
void DrawSearch(State& state,a::Requests& host,Facts& facts,const imkit::Theme& theme,float scale,bool focus)
{
    auto& panel=state.wallet_tools;
    if(!host.Refresh()){
        Text(state,"search-unavailable","Search unavailable","Wallet search requires the read-only wallet observation service.");
        return;
    }
    if(panel.scope.hidden){Text(state,"search-hidden","Search unavailable","Reveal wallet details before searching.");return;}
    if(panel.results && !a::SearchSourcesRetained(*panel.results,state.documents,*host.Refresh(),host.Addresses(),panel.scope.epoch)){
        panel.query.reset();panel.results.reset();panel.selected.reset();panel.export_file={};++panel.revision;
        panel.notice="Wallet observations changed. Run Search again.";
    }
    Text(state,"search-scope","Search scope",panel.tool==WalletTool::AllSearch?"All open unlocked wallets · retained observations":
        "Current unlocked wallet · retained observations");
    imkit::BeginFormField("search-query",{"Identifier or label","","",false},{&theme});
    if(focus)ImGui::SetKeyboardFocusHere();
    if(c::EditInput("###wallet-query",panel.search,0,true))++panel.revision;
    facts.tools["query"]=Last();
    const auto external=state.presentation.Interact("Identifier or label",c::ax::SemanticRole::TextField,c::ax::SemanticAction::SetValue|c::ax::SemanticAction::Focus,
        false,false,panel.search.draft());
    state.presentation.BindTextPosition(facts.tools["query"].id,panel.search.draft(),panel.search.cursor);
    if(external)state.presentation.Defer(*external,[&state,&host](const auto& request){
        auto& p=state.wallet_tools;
        if(!Current(state,host) || !p.search.SetExternalDraft(request.value))return false;
        ++p.revision;return true;
    });
    imkit::EndFormField();
    const auto choose_type=[&state,&host](a::SearchType type){if(!Current(state,host))return false;
        state.wallet_tools.search_type=type;++state.wallet_tools.revision;return true;};
    for(int index=0;index<=int(a::SearchType::Output);++index){
        if(index)ImGui::SameLine();
        const auto type=a::SearchType(index);
        const auto* name=a::SearchTypeName(type);
        const bool pressed=ImGui::RadioButton(name,panel.search_type==type);
        facts.tools[name]=Last(pressed);
        if(pressed)choose_type(type);
        const auto item=state.presentation.Interact(name,c::ax::SemanticRole::Radio,c::ax::SemanticAction::Select|c::ax::SemanticAction::Focus,false,panel.search_type==type);
        if(item)state.presentation.Defer(*item,[choose_type,type](const auto&){return choose_type(type);});
    }
    if(ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x-ImGui::GetItemRectMax().x>160*scale)ImGui::SameLine();
    bool wallet_column=panel.show_wallet;
    const bool toggle=ImGui::Checkbox("Wallet column",&wallet_column);
    facts.tools["wallet-column"]=Last(toggle);
    if(toggle){panel.show_wallet=wallet_column;++panel.revision;}
    const auto column=state.presentation.Interact("Wallet column",c::ax::SemanticRole::Toggle,c::ax::SemanticAction::Toggle|c::ax::SemanticAction::Focus,false,panel.show_wallet);
    if(column)state.presentation.Defer(*column,[&state,&host,toggle](const auto&){if(!Current(state,host))return false;
        if(!toggle){state.wallet_tools.show_wallet=!state.wallet_tools.show_wallet;++state.wallet_tools.revision;}return true;});
    const auto draft=Draft(panel);
    Button(state,facts,theme,"search","Search",!panel.export_file.chooser,[&state,&host,draft]{
        if(!Current(state,host))return false;
        auto& p=state.wallet_tools;
        p.query=draft;p.results=Search(state,host,draft);p.selected.reset();p.notice.clear();++p.revision;
        if(!p.results->valid)p.notice="Enter a valid search of up to 192 bytes.";
        return p.results->valid;
    });
    const bool ready=panel.query && *panel.query==draft && panel.results && !panel.results->hits.empty();
    ImGui::SameLine();Button(state,facts,theme,"show","Show",ready && panel.selected.has_value() && !panel.export_file.chooser,[&state,&host]{return Show(state,host);});
    ImGui::SameLine();Button(state,facts,theme,"search-export","Export CSV",ready && !panel.export_file.chooser
        && c::native_input && c::native_input->FileChooserAvailable(),[&state,&host]{
        auto proposed=CurrentSearchCsv(state,host);
        if(proposed.empty()){state.wallet_tools.notice="The search snapshot changed. Search again before exporting.";return false;}
        return BeginToolExport(state.wallet_tools.export_file,CurrentToolScope(state,host),std::move(proposed));
    });
    PollToolExport(panel.export_file,CurrentToolScope(state,host),[&state,&host,draft]{
        if(!state.wallet_tools.query || *state.wallet_tools.query!=draft)return std::string{};
        return CurrentSearchCsv(state,host);
    },panel.notice);
    if(!panel.results)return;
    if(*panel.query!=draft){Text(state,"search-draft","Search","Run Search to apply the edited criteria.");return;}
    const auto count=std::to_string(panel.results->hits.size())+" matches · "+std::to_string(panel.results->observed_wallets)
        +" wallets with retained observations"+(panel.results->limited?" · first 200 matches":"");
    Text(state,"search-count","Search results",count);
    if(!panel.results->observed_wallets)Text(state,"search-empty-source","Search unavailable","No readable wallet observations are retained for this scope.");
    if(panel.results->hits.empty()){
        if(panel.search_type==a::SearchType::Address)
            Text(state,"search-address-source","Address search scope","Address search requires a retained descriptor inventory for an unlocked wallet.");
        return;
    }
    const int columns=panel.show_wallet?4:3;
    const float row_height=ImGui::GetTextLineHeightWithSpacing()+2*ImGui::GetStyle().CellPadding.y;
    const float results_height=std::min(148*scale,row_height*float(std::clamp<size_t>(panel.results->hits.size()+1,3,6)));
    if(ImGui::BeginTable("wallet-search-results",columns,ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_ScrollY,{0,results_height})){
        const auto lo=ImGui::GetWindowDrawList()->GetClipRectMin(),hi=ImGui::GetWindowDrawList()->GetClipRectMax();
        state.presentation.Group("search-grid","Wallet search results",c::ax::SemanticRole::Grid,lo,hi);
        ImGui::TableSetupColumn("Type",ImGuiTableColumnFlags_WidthFixed,94*scale);
        ImGui::TableSetupColumn("Identifier",ImGuiTableColumnFlags_WidthStretch,1.5F);
        ImGui::TableSetupColumn("Label");
        if(panel.show_wallet)ImGui::TableSetupColumn("Wallet");
        ImGui::TableHeadersRow();
        ImGuiListClipper clip;clip.Begin(int(panel.results->hits.size()));
        while(clip.Step())for(int index=clip.DisplayStart;index<clip.DisplayEnd;++index){
            const auto& row=panel.results->hits[size_t(index)];
            ImGui::PushID(std::to_string(row.document).c_str());ImGui::PushID(row.key.c_str());
            ImGui::TableNextRow();ImGui::TableNextColumn();
            const bool selected=panel.selected==size_t(index);
            const bool pressed=ImGui::Selectable(a::SearchTypeName(row.type),selected);
            facts.tools["hit-"+std::to_string(index)]=Last(pressed);
            const auto provenance=std::string(a::ObservationSource(row.freshness))+" · "+ObservationDate(row.observed);
            const auto semantic=state.presentation.Interact(a::SearchTypeName(row.type),c::ax::SemanticRole::Row,c::ax::SemanticAction::Select|c::ax::SemanticAction::Focus,
                false,selected,{},provenance);
            state.presentation.Parent(ImGui::GetItemID());
            const auto select=[&state,&host,index]{if(!Current(state,host))return false;
                state.wallet_tools.selected=size_t(index);++state.wallet_tools.revision;return true;};
            if(pressed)select();
            if(semantic)state.presentation.Defer(*semantic,[select,pressed](const auto&){return pressed || select();});
            const auto short_id=row.identifier.size()>28?row.identifier.substr(0,16)+"…"+row.identifier.substr(row.identifier.size()-10):row.identifier;
            ImGui::TableNextColumn();ImGui::TextUnformatted(short_id.c_str());
            state.presentation.Annotate("identifier","Identifier",row.identifier,c::ax::SemanticRole::Cell);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%s",row.identifier.c_str(),provenance.c_str());
            ImGui::TableNextColumn();ImGui::TextUnformatted(row.label.c_str());
            state.presentation.Annotate("label","Label",row.label,c::ax::SemanticRole::Cell);
            if(ImGui::IsItemHovered() && !row.label.empty())ImGui::SetTooltip("%s",row.label.c_str());
            if(panel.show_wallet){
                ImGui::TableNextColumn();ImGui::TextUnformatted(row.wallet_name.c_str());
                state.presentation.Annotate("wallet","Wallet",row.wallet_name,c::ax::SemanticRole::Cell);
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",row.wallet_name.c_str());
            }
            state.presentation.EndGroup();
            ImGui::PopID();ImGui::PopID();
        }
        ImGui::EndTable();
        state.presentation.EndGroup();
    }
    if(panel.selected && *panel.selected<panel.results->hits.size()){
        const auto& row=panel.results->hits[*panel.selected];
        Text(state,"selected-search-source","Selected observation",std::string(a::ObservationSource(row.freshness))+" · "+ObservationDate(row.observed));
    }
}

void DrawSummary(State& state,a::Requests& host,const imkit::Theme& theme,float scale)
{
    auto* refresh=host.Refresh();
    if(state.privacy.hidden){Text(state,"summary-private","Balances hidden","Amounts and wallet names are hidden.");return;}
    if(!refresh){Text(state,"summary-unavailable","Balances unavailable","Wallet balances require the read-only RPC observation service.");return;}
    const auto rows=a::WalletBalances(state.documents,*refresh,state.wallet_tools.scope.epoch);
    Text(state,"summary-scope","Summary scope","Balances are shown per wallet. Wallets may share outputs, so there is no combined total.");
    if(rows.empty())Text(state,"summary-empty","Balances unavailable","No unlocked wallet with a verified RPC binding is open.");
    constexpr const char* names[]{"Trusted","Pending","Immature","Outside mempool"};
    for(const auto& row:rows){
        ImGui::PushID(std::to_string(row.document).c_str());
        const auto source=std::string(a::ObservationSource(row.freshness))+" · "+ObservationDate(row.observed)+(row.scanning?" · Partial while wallet is scanning":"");
        const bool open=ImGui::TreeNodeEx("balance-wallet",ImGuiTreeNodeFlags_DefaultOpen,"%s",row.wallet_name.c_str());
        state.presentation.Annotate("summary-wallet",row.wallet_name.c_str(),source,c::ax::SemanticRole::Group);
        if(open){
            Text(state,"summary-freshness","Balance provenance",source);
            std::vector<Money> chart;
            for(size_t i=0;i<row.categories.size();++i){
                auto shown=state.privacy.Present(row.categories[i]).text;
                if(row.categories[i] && (state.unit==1 || (state.unit==2 && row.categories[i]->sats()>-100000000 && row.categories[i]->sats()<100000000)))
                    shown=std::to_string(row.categories[i]->sats())+" sats";
                else if(row.categories[i])shown+=" BTC";
                Text(state,names[i],names[i],std::string(names[i])+"  "+shown);
                if(i<3 && row.categories[i])chart.push_back(*row.categories[i]);
            }
            if(chart.size()==3){
                const auto displayed=state.privacy.Chart(chart);
                if(displayed.percentages.size()==3){
                    std::array<imkit::ChartDatum,3> bars;
                    for(size_t i=0;i<bars.size();++i)bars[i]={names[i],displayed.percentages[i],theme.semantic.text};
                    imkit::BarChart("wallet-balance-chart",{bars},{{0,48*scale},imkit::Orientation::Vertical,100,false,false,"%.1f%% share"},{&theme});
                    state.presentation.RecordChart(displayed.percentages);
                }
                Text(state,"exact-chart","Trusted, pending and immature chart",displayed.alternative);
            }
            Text(state,"summary-network","Network genesis",row.network.genesis.hex());
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void DrawNode(State& state,a::Requests& host)
{
    auto* connection=host.Connection();auto* refresh=host.Refresh();
    const auto node=connection && refresh?a::AppliedNodeDetails(*connection,*refresh):std::nullopt;
    if(!node){Text(state,"node-unavailable","Node information unavailable","Test and apply a direct Bitcoin RPC profile to view node observations.");return;}
    Text(state,"node-source","Node provenance",node->observed?std::string(a::ObservationSource(node->freshness))+" · "+ObservationDate(node->observed)
        :"Applied connection test · Observation time unavailable");
    Text(state,"chain","Network","Network  "+node->chain);
    Text(state,"height","Block height","Block height  "+std::to_string(node->height));
    Text(state,"rpc-version","Reported RPC version","Version at connection test  "+std::to_string(node->version));
    Text(state,"sync","Node synchronization",node->initial_download?"Initial block download":"Initial block download complete at observation");
    Text(state,"pruning","Pruning",node->pruned?"Block data pruned":"Full block retention reported");
    Text(state,"genesis","Network genesis","Genesis  "+node->genesis);
    Text(state,"tip","Best observed block",node->tip.empty()?"Best observed block unavailable":"Best observed block  "+node->tip);
}
}

ToolViewScope CurrentToolScope(const State& state,a::Requests& host)
{
    auto* connection=host.Connection();
    const auto* recovery=host.Recovery();
    return {state.documents.ActiveId(),connection?connection->Session():0,recovery?recovery->Gate().Epoch():LockEpoch{},
        state.privacy.hidden || state.semantic_hide_toggle,connection?connection->Offline():host.Refresh() && host.Refresh()->Offline(),
        recovery && (recovery->Locked() || recovery->Pending())};
}
void OpenWalletTool(State& state,a::Requests& host,WalletTool tool)
{
    RetireWalletTool(state);
    auto& panel=state.wallet_tools;
    panel.tool=tool;panel.scope=CurrentToolScope(state,host);panel.show_wallet=tool==WalletTool::AllSearch;
    panel.dialog.open=true;panel.dialog.initialFocus=true;panel.close_pending=false;
}
void RetireWalletTool(State& state)
{
    auto& panel=state.wallet_tools;
    const bool close=panel.dialog.open || panel.close_pending;
    if(panel.tool==WalletTool::Node)state.restore_status=true;
    const auto revision=panel.revision;
    WalletToolsPanel empty;
    panel=std::move(empty);
    panel.close_pending=close;panel.revision=revision+1;
}
bool BeginToolExport(ToolExport& file,const ToolViewScope& scope,std::string bytes)
{
    auto* input=c::native_input;
    if(file.chooser || scope.locked || !input || !input->FileChooserAvailable() || bytes.empty() || bytes.size()>256*1024)return false;
    const auto chooser=input->ChooseFile({},true);
    if(!chooser)return false;
    file={chooser,scope,std::move(bytes)};
    return true;
}
void PollToolExport(ToolExport& file,const ToolViewScope& scope,const std::function<std::string()>& current,std::string& notice)
{
    if(!file.chooser)return;
    if(file.scope!=scope || scope.locked || !c::native_input){file={};notice="The workspace changed. Export canceled.";return;}
    const auto selected=c::native_input->PollFile(file.chooser);
    if(selected.outcome==c::NativeOutcome::Pending)return;
    auto captured=std::exchange(file,ToolExport{});
    if(selected.outcome==c::NativeOutcome::Cancelled){notice="Export canceled.";return;}
    if(selected.outcome!=c::NativeOutcome::Chosen){notice="No local file was selected.";return;}
    if(current()!=captured.bytes){notice="The observation changed. Export canceled.";return;}
    a::WriteCoinCsv(selected.path,captured.bytes,notice);
}
void DrawWalletTools(State& state,a::Requests& host,Facts& facts,const imkit::Theme& theme,float scale)
{
    auto& panel=state.wallet_tools;
    if(panel.dialog.open && !Current(state,host))RetireWalletTool(state);
    if(panel.close_pending){if(ImGui::BeginPopupModal(POPUP)){ImGui::CloseCurrentPopup();ImGui::EndPopup();}panel.close_pending=false;}
    if(!panel.dialog.open)return;
    const auto* vp=ImGui::GetMainViewport();
    ImGui::SetNextWindowSize({std::min(720*scale,std::max(1.F,vp->WorkSize.x-32*scale)),std::min(580*scale,std::max(1.F,vp->WorkSize.y-32*scale))},ImGuiCond_Always);
    const bool focus=panel.dialog.initialFocus;
    if(!imkit::BeginDialog(POPUP,panel.dialog,{&theme})){if(!panel.dialog.open)RetireWalletTool(state);return;}
    facts.tools_visible=true;
    state.presentation.DialogOnly();
    const auto pos=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
    state.presentation.Group("wallet-tool",Title(panel.tool),c::ax::SemanticRole::Dialog,pos,{pos.x+size.x,pos.y+size.y});
    ImGui::BeginChild("wallet-tool-body",{0,-ImGui::GetFrameHeightWithSpacing()},ImGuiChildFlags_NavFlattened);
    ImGui::PushTextWrapPos();
    if(!SearchPanel(panel.tool))Text(state,"tool-title",Title(panel.tool),Title(panel.tool));
    if(SearchPanel(panel.tool))DrawSearch(state,host,facts,theme,scale,focus);
    else if(panel.tool==WalletTool::Summary)DrawSummary(state,host,theme,scale);
    else DrawNode(state,host);
    if(!panel.notice.empty())Text(state,"tool-notice","Wallet tool result",panel.notice,c::ax::SemanticRole::Status);
    ImGui::PopTextWrapPos();ImGui::EndChild();
    Button(state,facts,theme,"tool-close","Close",true,[&state]{RetireWalletTool(state);return true;});
    if(!panel.dialog.open){ImGui::CloseCurrentPopup();panel.close_pending=false;}
    state.presentation.EndGroup();imkit::EndDialog();
}
}
