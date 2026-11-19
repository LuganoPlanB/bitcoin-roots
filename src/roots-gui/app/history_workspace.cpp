// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/history_workspace.h"
#include <sodium.h>
#include <algorithm>
#include <cctype>
#include <ctime>
namespace roots::gui::app {
std::optional<BackendSessionId> MetadataBackend(const ServerCandidate& candidate){
    if(!ValidServerCandidate(candidate) || sodium_init()<0)return {};
    // Canonical public origin only: rotating credentials, CA files and proxy
    // routes do not rename an endpoint's metadata. Genesis is a separate key.
    const auto origin="roots-gui-origin-v1\n"+std::string(candidate.connection.tls?"https":"http")+"\n"+rpc::Quote(candidate.connection.host)+"\n"+std::to_string(candidate.connection.port);
    unsigned char digest[crypto_hash_sha256_BYTES];char hex[crypto_hash_sha256_BYTES*2+1];if(crypto_hash_sha256(digest,reinterpret_cast<const unsigned char*>(origin.data()),origin.size())!=0)return {};sodium_bin2hex(hex,sizeof(hex),digest,sizeof(digest));return BackendSessionId::Make(std::string("rpc-origin-")+hex);
}
std::optional<WalletBinding> VerifiedWalletBinding(const BackendCapabilities& caps){if(!caps.CanApply() || caps.wallet!=WalletAvailability::Available || !caps.candidate.wallet)return {};auto backend=MetadataBackend(caps.candidate);auto wallet=WalletId::Make(*caps.candidate.wallet);std::optional<std::string> actual;
    if(!wallet){const auto& name=*caps.candidate.wallet;if(std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32 || c==127;}))return {};unsigned char digest[crypto_hash_sha256_BYTES];char hex[crypto_hash_sha256_BYTES*2+1];if(crypto_hash_sha256(digest,reinterpret_cast<const unsigned char*>(name.data()),name.size())!=0)return {};sodium_bin2hex(hex,sizeof(hex),digest,sizeof(digest));wallet=WalletId::Make(std::string("rpc-wallet-")+hex);actual=name;}
    auto genesis=Hash256::Parse(caps.genesis);if(!backend || !wallet || !genesis)return {};return WalletBinding{*backend,*wallet,{*genesis},actual};}
storage::Scope HistoryMetadataScope(const WalletBinding& b){return {b.backend.value(),b.network,b.DaemonWallet()};}
const Document* RegisteredWallet(const DocumentRegistry& docs,const WalletBinding& binding){for(const auto& d:docs.Documents())if(d.kind==DocumentKind::Wallet && d.binding==binding)return &d;return nullptr;}
std::optional<RefreshIdentity> WalletRefreshIdentity(const BackendCapabilities& caps,uint64_t session,const Document& document,LockEpoch epoch){auto binding=VerifiedWalletBinding(caps);if(!binding || !document.binding || document.binding!=binding || !document.history.enabled || !document.history.query_revision || !session)return {};auto backend=BackendSessionId::Make("connection-"+std::to_string(session));auto id=DocumentId::Make("wallet-document-"+std::to_string(document.id));if(!backend || !id)return {};return RefreshIdentity{{*backend,binding->network,binding->wallet,*id,{document.history.query_revision},epoch},caps.candidate,session};}
namespace {
std::optional<RefreshView> WalletObservationFor(const Document& document,const RefreshService& refresh,LockEpoch epoch,bool history)
{
    if(!document.binding || !document.history.enabled)return {};
    std::optional<RefreshView> result;
    const size_t channel=history?2:1;
    for(auto view:refresh.ObservationViews()){
        if(!view.identity || (history?!view.ledger:!view.wallet || !view.observed[1]))continue;
        const auto backend=MetadataBackend(view.identity->profile);
        const auto& c=view.identity->context;
        if(!backend || c.backend.value()!="connection-"+std::to_string(view.identity->connection_session)
            || *backend!=document.binding->backend || view.identity->profile.wallet!=std::optional<std::string>(document.binding->DaemonWallet())
            || c.network!=document.binding->network || c.wallet!=document.binding->wallet
            || c.document.value()!="wallet-document-"+std::to_string(document.id)
            || c.revision.value!=document.history.query_revision || c.lock_epoch!=epoch)continue;
        if(!result || (history?view.ledger->Observed()>result->ledger->Observed():view.observed[channel]>result->observed[channel]))result=view;
    }
    return result;
}
}
std::optional<RefreshView> HistoryForDocument(const Document& document,const RefreshService& refresh,LockEpoch epoch)
{
    return WalletObservationFor(document,refresh,epoch,true);
}
std::optional<RefreshView> WalletBalanceForDocument(const Document& document,const RefreshService& refresh,LockEpoch epoch)
{
    return WalletObservationFor(document,refresh,epoch,false);
}
std::string HistoryDate(int64_t value,bool full){const auto t=std::time_t(value);if(int64_t(t)!=value)return "Unknown date";const auto* utc=std::gmtime(&t);char text[32]{};if(!utc || !std::strftime(text,sizeof(text),full?"%Y-%m-%d %H:%M:%S UTC":"%Y-%m-%d",utc))return "Unknown date";return text;}
const char* HistoryStateText(HistoryState state){switch(state){case HistoryState::Confirmed:return "Confirmed";case HistoryState::Pending:return "Unconfirmed";case HistoryState::Immature:return "Immature";case HistoryState::Orphan:return "Orphan";case HistoryState::Replaced:return "Replaced";case HistoryState::Conflicted:return "Conflicted";case HistoryState::Abandoned:return "Abandoned";}return "Unknown";}
std::optional<std::string> ExplorerUrl(const NetworkIdentity& network,const Hash256& txid){
    // Exact genesis allowlist; unknown forks, signets and regtest have no
    // public explorer default. Never interpolate a host, wallet or credential.
    const auto& genesis=network.genesis.hex();std::string prefix;
    if(genesis=="000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f")prefix="https://mempool.space/tx/";
    else if(genesis=="00000000da84f2bafbbc53dee25a72ae507ff4914b867c565be350b0da8bf043")prefix="https://mempool.space/testnet4/tx/";
    else return {};
    return prefix+txid.hex();
}
std::optional<std::string> ExplorerUrl(const PublicPreferences& preferences,const NetworkIdentity& network,const Hash256& txid)
{
    if(!ValidPreferences(preferences))return {};
    if(preferences.explorer==ExplorerMode::NetworkDefault)return ExplorerUrl(network,txid);
    if(preferences.explorer!=ExplorerMode::Custom || preferences.explorer_genesis!=network.genesis.hex())return {};
    constexpr std::string_view placeholder{"{txid}"};
    return preferences.explorer_template.substr(0,preferences.explorer_template.size()-placeholder.size())+txid.hex();
}
std::string HistoryStatus(const HistoryLedger& ledger,const HistoryTransaction& tx,bool current){
    if(!current)return std::string("Current chain state Unknown; last observed ")+HistoryStateText(tx.state);
    auto text=std::string(HistoryStateText(tx.state))+" / "+std::to_string(tx.confirmations)+" confirmations";
    if(tx.state==HistoryState::Pending)for(const auto& io:tx.io)if(io.kind=="Input" && io.reference.size()>=64){const auto parent=ledger.Rows().find(io.reference.substr(0,64));if(parent!=ledger.Rows().end() && parent->second.state==HistoryState::Pending){text+=" / unconfirmed parent";break;}}
    if(tx.state==HistoryState::Replaced && tx.replaced_by)text+=" / replaced by "+tx.replaced_by->hex();
    return text;
}
namespace {
std::string Label(const HistoryTransaction& tx,const std::map<std::string,std::string>& labels){const auto found=labels.find(tx.txid.hex());if(found!=labels.end())return found->second;for(const auto& d:tx.details)if(!d.label.empty())return d.label;return {};}
std::string Fold(std::string text){for(auto& c:text)if(c>='A' && c<='Z')c=char(c+('a'-'A'));return text;}
std::string Cell(std::string value,bool numeric=false){if(!numeric){const auto i=value.find_first_not_of(' ');if(i!=std::string::npos && (value[i]=='=' || value[i]=='+' || value[i]=='-' || value[i]=='@' || value[i]=='\t' || value[i]=='\r' || value[i]=='\n'))value.insert(value.begin(),'\'');}std::string result="\"";for(const auto c:value){result+=c;if(c=='\"')result+=c;}return result+'\"';}
}
std::vector<const HistoryTransaction*> OrderedHistory(const HistoryLedger& ledger,const HistoryDocumentState& state,const std::map<std::string,std::string>& labels,bool hidden){std::vector<const HistoryTransaction*> rows;const auto filter=hidden?std::string{}:Fold(state.filter);for(const auto& [id,tx]:ledger.Rows())if(filter.empty() || Fold(id+" "+Label(tx,labels)+" "+HistoryStateText(tx.state)).find(filter)!=std::string::npos)rows.push_back(&tx);std::stable_sort(rows.begin(),rows.end(),[&](const auto* a,const auto* b){int compare{};if(state.sort==1 && !hidden){const auto aa=Fold(Label(*a,labels)),bb=Fold(Label(*b,labels));compare=aa<bb?-1:aa>bb?1:0;}else if(state.sort==2 && !hidden)compare=a->wallet_delta<b->wallet_delta?-1:a->wallet_delta>b->wallet_delta?1:0;else if(state.sort==3 && !hidden){if(a->running_balance && b->running_balance)compare=*a->running_balance<*b->running_balance?-1:*a->running_balance>*b->running_balance?1:0;else compare=bool(a->running_balance)-bool(b->running_balance);}else compare=a->time<b->time?-1:a->time>b->time?1:0;if(!compare)return a->txid.hex()<b->txid.hex();return state.descending?compare>0:compare<0;});return rows;}
std::optional<std::string> HistoryCsv(const HistoryLedger& ledger,std::span<const HistoryTransaction* const> rows,const std::map<std::string,std::string>& labels,bool hidden,bool state_current){if(hidden || !ledger.Observed() || rows.size()>4096)return {};std::string csv="Date (UTC),Txid,Label,Value (BTC),Balance including immature (BTC),State,Confirmations\r\n";for(const auto* tx:rows){if(!tx || !ledger.Rows().contains(tx->txid.hex()) || &ledger.Rows().at(tx->txid.hex())!=tx)return {};const auto label=Label(*tx,labels);if(std::any_of(label.begin(),label.end(),[](unsigned char c){return (c<32 && c!='\r' && c!='\n' && c!='\t') || c==127;}))return {};csv+=Cell(HistoryDate(tx->time,true))+','+Cell(tx->txid.hex())+','+Cell(label)+','+Cell(tx->wallet_delta.btc(),true)+','+Cell(tx->running_balance?tx->running_balance->btc():"Unknown",bool(tx->running_balance))+','+Cell(state_current?HistoryStateText(tx->state):std::string("Unknown; last observed ")+HistoryStateText(tx->state))+','+Cell(state_current?std::to_string(tx->confirmations):"Unknown",state_current)+"\r\n";if(csv.size()>8*1024*1024)return {};}return csv;}
}
