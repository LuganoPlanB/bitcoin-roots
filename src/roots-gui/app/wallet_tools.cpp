// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/wallet_tools.h"
#include "app/coin_inventory.h"
#include <algorithm>

namespace roots::gui::app {
namespace {
bool SearchText(std::string_view text,size_t bound)
{
    return text.size()<=bound && rpc::ValidUtf8(text) && std::ranges::none_of(text,[](unsigned char c){return c<32 || c==127;});
}
std::string DisplayText(std::string_view text,size_t bound=255)
{
    return SearchText(text,bound)?std::string(text):std::string{};
}
std::string Fold(std::string_view text)
{
    auto folded=std::string(text);
    for(char& c:folded)if(c>='A' && c<='Z')c=char(c-'A'+'a');
    return folded;
}
std::string CsvCell(std::string text)
{
    const auto first=text.find_first_not_of(" \t\r\n");
    if(first!=std::string::npos && std::string_view("=+-@").find(text[first])!=std::string_view::npos)text.insert(text.begin(),'\'');
    std::string cell="\"";
    for(char c:text){cell+=c;if(c=='"')cell+='"';}
    return cell+'"';
}
std::string WalletName(const Document& document)
{
    const auto name=DisplayText(document.name);
    return name.empty()?"Wallet "+std::to_string(document.id):name;
}
Freshness AddressFreshness(const AddressView& view)
{
    return view.offline?Freshness::Offline:view.error!=rpc::Error::None?Freshness::Stale:view.current?Freshness::Live:Freshness::Cached;
}
struct SearchObservations {
    std::optional<RefreshView> history;
    AddressView addresses;
    std::optional<WalletSearchSource> source;
};
SearchObservations SearchViews(const Document& document,const RefreshService& refresh,const AddressService* addresses,LockEpoch epoch)
{
    SearchObservations result;
    if(!ToolWallet(document))return result;
    result.history=HistoryForDocument(document,refresh,epoch);
    if(!result.history || !result.history->identity)return result;
    const auto* identity=result.history->identity;
    const auto address_view=addresses?addresses->View(*identity):AddressView{};
    if(address_view.snapshot && address_view.identity && *address_view.identity==*identity
        && address_view.snapshot->context==identity->context)result.addresses=address_view;
    const auto& history=result.history;
    if(!history->ledger->Observed() && !result.addresses.snapshot)return result;
    result.source=WalletSearchSource{document.id,*identity,history->history_sequence,result.addresses.sequence,
        history->ledger->Observed(),result.addresses.observed,history->freshness[2],AddressFreshness(result.addresses),history->chain_current};
    return result;
}
}

bool ToolWallet(const Document& document)
{
    return document.kind==DocumentKind::Wallet && document.binding && document.history.enabled
        && document.observation!=ObservationState::Locked;
}
bool SearchSourcesRetained(const WalletSearchResult& results,const DocumentRegistry& documents,const RefreshService& refresh,
    const AddressService* addresses,LockEpoch epoch)
{
    return std::ranges::all_of(results.sources,[&](const auto& source){
        const auto* document=documents.Find(source.document);
        return document && SearchViews(*document,refresh,addresses,epoch).source==source;
    });
}
const char* SearchTypeName(SearchType type)
{
    switch(type){case SearchType::Transaction:return "Transaction";case SearchType::Address:return "Address";
    case SearchType::Output:return "Output";case SearchType::Any:return "All types";}
    return "Unknown";
}
const char* ObservationSource(Freshness freshness)
{
    switch(freshness){case Freshness::Live:return "Current RPC";case Freshness::Cached:return "Dated cache";
    case Freshness::Stale:return "Stale after refresh failure";case Freshness::Offline:return "Offline cache";
    case Freshness::Unknown:return "Unavailable";}
    return "Unavailable";
}

WalletSearchResult SearchWallets(const DocumentRegistry& documents,const RefreshService& refresh,const AddressService* addresses,
    LockEpoch epoch,const WalletSearchQuery& query,const WalletLabelReader& local_labels)
{
    WalletSearchResult result;
    if(!SearchText(query.text,192) || query.type<SearchType::Any || query.type>SearchType::Output)return result;
    result.valid=true;
    const auto needle=Fold(query.text);
    const auto accepts=[&](SearchType type){return query.type==SearchType::Any || query.type==type;};
    for(const auto& document:documents.Documents()){
        if(!ToolWallet(document) || (query.document && query.document!=document.id))continue;
        ++result.wallets;
        const auto views=SearchViews(document,refresh,addresses,epoch);
        if(!views.source)continue;
        const auto& history=views.history;
        const auto& address_view=views.addresses;
        result.sources.push_back(*views.source);
        ++result.observed_wallets;
        const auto metadata=local_labels?local_labels(*document.binding):std::nullopt;
        std::map<std::pair<storage::TargetKind,std::string>,std::string_view> labels;
        if(metadata)for(const auto& item:metadata->labels)labels.try_emplace(std::pair{item.target.kind,item.target.key},item.text);
        const auto label=[&](storage::TargetKind type,const std::string& key,std::string_view fallback){
            const auto found=labels.find({type,key});
            return DisplayText(found==labels.end()?fallback:found->second);
        };
        const auto name=WalletName(document);
        const auto add=[&](SearchType type,const std::string& key,std::string_view identifier,std::string value,int64_t observed,Freshness freshness){
            if(identifier.empty() || identifier.size()>192 || !std::ranges::all_of(identifier,[](unsigned char c){return c>=33 && c<=126;}))return;
            if(!needle.empty() && Fold(identifier).find(needle)==std::string::npos && Fold(value).find(needle)==std::string::npos)return;
            if(result.hits.size()==200){result.limited=true;return;}
            result.hits.push_back({document.id,views.source->identity.connection_session,document.binding->network,type,key,
                std::string(identifier),std::move(value),name,observed,freshness});
        };
        if(history && history->ledger->Observed()>0){
            const auto& ledger=*history->ledger;
            if(accepts(SearchType::Transaction))for(const auto& [id,tx]:ledger.Rows()){
                auto detail=std::ranges::find_if(tx.details,[&](const HistoryDetail& item){
                    return !item.label.empty() && Fold(DisplayText(item.label)).find(needle)!=std::string::npos;
                });
                if(detail==tx.details.end())detail=std::ranges::find_if(tx.details,[](const HistoryDetail& item){return !item.label.empty();});
                add(SearchType::Transaction,id,id,label(storage::TargetKind::Transaction,id,detail==tx.details.end()?std::string_view{}:std::string_view(detail->label)),
                    ledger.Observed(),history->freshness[2]);
                if(result.limited)break;
            }
            if(!result.limited && accepts(SearchType::Output))for(const auto& coin:ledger.Coins()){
                const auto key=CoinKey(coin.outpoint);
                add(SearchType::Output,key,key,label(storage::TargetKind::Output,key,coin.label),ledger.Observed(),history->chain_current?Freshness::Live:
                    history->offline?Freshness::Offline:history->errors[2]!=rpc::Error::None?Freshness::Stale:Freshness::Cached);
                if(result.limited)break;
            }
        }
        if(!result.limited && address_view.snapshot && accepts(SearchType::Address))for(const auto& item:address_view.snapshot->rows){
            add(SearchType::Address,item.key,item.address,label(storage::TargetKind::Address,item.address,item.label),address_view.observed.value_or(0),
                AddressFreshness(address_view));
            if(result.limited)break;
        }
        if(result.limited)break;
    }
    return result;
}

std::string WalletSearchCsv(const WalletSearchResult& result,bool show_wallet)
{
    if(!result.valid || result.hits.empty())return {};
    std::string csv=show_wallet?"Type,Identifier,Label,Wallet,Genesis,Source,Observed UTC\r\n":"Type,Identifier,Label,Genesis,Source,Observed UTC\r\n";
    for(const auto& hit:result.hits){
        csv+=std::string(SearchTypeName(hit.type))+','+CsvCell(hit.identifier)+','+CsvCell(hit.label)+',';
        if(show_wallet)csv+=CsvCell(hit.wallet_name)+',';
        csv+=hit.network.genesis.hex()+','+ObservationSource(hit.freshness)+','+ObservationDate(hit.observed>0?std::optional(hit.observed):std::nullopt)+"\r\n";
    }
    return csv;
}

std::vector<WalletBalanceSummary> WalletBalances(const DocumentRegistry& documents,const RefreshService& refresh,LockEpoch epoch)
{
    std::vector<WalletBalanceSummary> result;
    for(const auto& document:documents.Documents()){
        if(!ToolWallet(document))continue;
        WalletBalanceSummary row{document.id,WalletName(document),document.binding->network,{},{}};
        if(const auto view=WalletBalanceForDocument(document,refresh,epoch)){
            row.categories={view->wallet->trusted,view->wallet->pending,view->wallet->immature,view->wallet->nonmempool};
            row.observed=view->observed[1];row.freshness=view->freshness[1];row.scanning=view->wallet->scanning;
        }
        result.push_back(std::move(row));
    }
    return result;
}

std::optional<NodeDetails> AppliedNodeDetails(const ConnectionService& connection,const RefreshService& refresh)
{
    const auto& active=connection.Active();
    if(!active || !active->CanApply() || connection.PrivacyLocked())return {};
    const auto& chain=active->chain;
    const bool known=chain=="main" || chain=="test" || chain=="testnet4" || chain=="signet" || chain=="regtest";
    NodeDetails result{known?chain:"Custom network",active->genesis,{},active->version,active->height,active->initial_download,active->pruned,
        connection.Offline()?Freshness::Offline:Freshness::Cached,{}};
    const auto view=refresh.View();
    if(view.identity && view.node && view.identity->connection_session==connection.Session()
        && view.identity->profile==active->candidate && view.identity->context.network.genesis.hex()==active->genesis){
        result.tip=view.node->tip;result.height=view.node->height;result.initial_download=view.node->initial_download;
        result.pruned=view.node->pruned;result.freshness=view.freshness[0];result.observed=view.observed[0];
    }
    return result;
}

const char* ActivityKindName(ActivityKind kind)
{
    switch(kind){case ActivityKind::Connection:return "Connection";case ActivityKind::Chain:return "Chain observation";
    case ActivityKind::Wallet:return "Wallet observation";case ActivityKind::History:return "History observation";
    case ActivityKind::Background:return "Background work";}
    return "Activity";
}
void ActivityJournal::Observe(const HostObservations& observations,int64_t now)
{
    if(now<=0)return;
    std::optional<unsigned> progress;
    if(observations.tasks.size()==1 && observations.tasks.front().progress_per_mille
        && *observations.tasks.front().progress_per_mille<=1000)progress=observations.tasks.front().progress_per_mille;
    const auto background=observations.tasks.size()==1?observations.tasks.front().state:observations.activity.state;
    const std::array<Status,5> current{{{observations.connection.state,{}},
        {observations.screen_observations[0].state,{}},{observations.screen_observations[1].state,{}},
        {observations.screen_observations[2].state,{}},{background,progress}}};
    for(size_t i=0;i<current.size();++i){
        if(latest[i]==current[i])continue;
        if(latest[i] || current[i].state!=ServiceState::Unavailable){
            if(records.size()==64)records.pop_front();
            records.push_back({ActivityKind(i),current[i].state,current[i].progress,now});
            ++revision;
        }
        latest[i]=current[i];
    }
}
std::string ActivityCsv(const ActivityJournal& activity)
{
    if(activity.Records().empty())return {};
    std::string csv="Observed UTC,Activity,State,Progress per mille\r\n";
    for(const auto& record:activity.Records())csv+=ObservationDate(record.observed)+','+ActivityKindName(record.kind)+','
        +std::string(StateName(record.state))+','+(record.progress?std::to_string(*record.progress):std::string{})+"\r\n";
    return csv;
}
}
