// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/recovery.h"
#include "app/coin_inventory.h"
#include "adapters/storage/protected_file.h"
#include "adapters/rpc/json.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <sodium.h>
namespace roots::gui::app {
storage::Result<storage::Metadata> RecoverySession::HistoryMetadata(const storage::Scope& scope){if(!started)Start();if(Locked())return {{},storage::Error::Authentication};if(!store && !std::filesystem::exists(path))return {storage::Metadata{},storage::Error::None};if(!Open())return {{},storage::Error::Io};return store->Load(scope);}
storage::Result<std::vector<storage::Scope>> RecoverySession::RecentWallets()
{
    if (Locked()) return {{}, storage::Error::Authentication};
    if ((!started && !Start()) || !error.empty()) return {{}, storage::Error::Io};
    if (!store) return {std::vector<storage::Scope>{}, storage::Error::None};
    return store->LoadRecentWallets();
}
storage::Result<storage::Done> RecoverySession::RememberWallet(const storage::Scope& scope)
{
    if (Locked()) return {{}, storage::Error::Authentication};
    if ((!started && !Start()) || !error.empty()) return {{}, storage::Error::Io};
    if (!Open()) return {{}, storage::Error::Io};
    return store->RememberWallet(scope);
}
storage::Result<std::optional<PendingRescan>> RecoverySession::PendingScan(const storage::Scope& scope)
{
    if (!started && !Start()) return {{}, storage::Error::Io};
    if (Locked() || Pending()) return {{}, storage::Error::Authentication};
    if (!error.empty()) return {{}, storage::Error::Io};
    return store ? store->LoadRescan(scope) : storage::Result<std::optional<PendingRescan>>{std::optional<PendingRescan>{}};
}
storage::Result<std::shared_ptr<storage::FileGuard>> RecoverySession::RescanLease()
{
    if (Locked() || Pending()) return {{}, storage::Error::Authentication};
    if ((!started && !Start()) || !error.empty() || !Open()) return {{}, storage::Error::Io};
    auto guard = storage::FileGuard::Open(path.string()+".rescan", true);
    if (!guard) return {{}, guard.error};
    return {std::shared_ptr<storage::FileGuard>(std::move(*guard.value))};
}
storage::Result<uint64_t> RecoverySession::BeginScan(const storage::Scope& scope, ScanRange range)
{
    if (Locked() || Pending()) return {{}, storage::Error::Authentication};
    return store ? store->BeginRescan(scope, range) : storage::Result<uint64_t>{{}, storage::Error::Io};
}
storage::Result<storage::Done> RecoverySession::FinishScan(const storage::Scope& scope, uint64_t ticket)
{
    if (Locked() || Pending()) return {{}, storage::Error::Authentication};
    return store ? store->FinishRescan(scope, ticket) : storage::Result<storage::Done>{{}, storage::Error::Io};
}
storage::Result<storage::ReceivePool> RecoverySession::ReceivePool(const storage::Scope& scope)
{
    if (!started && !Start()) return {{}, storage::Error::Io};
    if (Locked() || Pending()) return {{}, storage::Error::Authentication};
    if (!error.empty()) return {{}, storage::Error::Io};
    return store ? store->LoadReceivePool(scope) : storage::Result<storage::ReceivePool>{storage::ReceivePool{}};
}
storage::Result<uint64_t> RecoverySession::BeginReceive(const storage::Scope& scope, bool reserve)
{
    const auto current = ReceivePool(scope);
    if (!current) return {{}, current.error};
    if (!Open()) return {{}, storage::Error::Io};
    return store->BeginReceive(scope, reserve);
}
storage::Result<storage::Done> RecoverySession::FinishReceive(const storage::Scope& scope, uint64_t ticket, const std::optional<ReceiveTarget>& address)
{
    if (Locked() || Pending()) return {{}, storage::Error::Authentication};
    return store ? store->FinishReceive(scope, ticket, address) : storage::Result<storage::Done>{{}, storage::Error::NotFound};
}
storage::Result<ReceiveTarget> RecoverySession::ConsumeReceive(const storage::Scope& scope)
{
    if (Locked() || Pending()) return {{}, storage::Error::Authentication};
    return store ? store->ConsumeReceive(scope) : storage::Result<ReceiveTarget>{{}, storage::Error::NotFound};
}
storage::Result<uint64_t> RecoverySession::SaveReceiveLabel(const storage::Scope& scope, std::string_view address, std::string label, uint64_t expected)
{
    if (Locked() || Pending()) return {{}, storage::Error::Authentication};
    if (label.size() > 255 || !rpc::ValidUtf8(label)
        || std::ranges::any_of(label, [](unsigned char c) {return c < 32 || c == 127;})) return {{}, storage::Error::Invalid};
    auto current = HistoryMetadata(scope);
    if (!current) return {{}, current.error};
    if (expected != current.value->revision) return {{}, storage::Error::Conflict};
    auto& labels = current.value->labels;
    const auto found = std::ranges::find_if(labels, [&](const auto& entry) {
        return entry.target.kind == storage::TargetKind::Address && entry.target.key == address;
    });
    if (found == labels.end()) labels.push_back({{storage::TargetKind::Address, std::string(address)}, std::move(label)});
    else found->text = std::move(label);
    if (!Open()) return {{}, storage::Error::Io};
    return store->Save(scope, *current.value);
}
storage::Result<uint64_t> RecoverySession::SaveHistoryLabel(const storage::Scope& scope,const Hash256& txid,std::string label,uint64_t expected_revision){auto current=HistoryMetadata(scope);if(!current)return {{},current.error};if(current.value->revision!=expected_revision)return {{},storage::Error::Conflict};auto& labels=current.value->labels;auto found=std::find_if(labels.begin(),labels.end(),[&](const auto& item){return item.target.kind==storage::TargetKind::Transaction && item.target.key==txid.hex();});if(found==labels.end())labels.push_back({{storage::TargetKind::Transaction,txid.hex()},std::move(label)});else found->text=std::move(label);if(!Open())return {{},storage::Error::Io};return store->Save(scope,*current.value);}
storage::Result<uint64_t> RecoverySession::SaveOutputLabel(const storage::Scope& scope,const Outpoint& outpoint,std::string label,uint64_t expected_revision){auto current=HistoryMetadata(scope);if(!current)return {{},current.error};if(current.value->revision!=expected_revision)return {{},storage::Error::Conflict};const auto key=CoinKey(outpoint);auto& labels=current.value->labels;auto found=std::find_if(labels.begin(),labels.end(),[&](const auto& item){return item.target.kind==storage::TargetKind::Output && item.target.key==key;});if(found==labels.end())labels.push_back({{storage::TargetKind::Output,key},std::move(label)});else found->text=std::move(label);if(!Open())return {{},storage::Error::Io};return store->Save(scope,*current.value);}

storage::Result<uint64_t> RecoverySession::SaveCoinFreezes(const storage::Scope& scope,const std::vector<storage::Target>& freezes,uint64_t expected_revision){auto current=HistoryMetadata(scope);if(!current)return {{},current.error};if(current.value->revision!=expected_revision)return {{},storage::Error::Conflict};current.value->freezes=freezes;if(!Open())return {{},storage::Error::Io};return store->Save(scope,*current.value);}

namespace {
using J=rpc::Json;
void Need(bool b){if(!b)throw std::runtime_error("invalid public session");}
J S(std::string s){Need(s.size()<=4096 && rpc::ValidUtf8(s) && std::none_of(s.begin(),s.end(),[](unsigned char c){return c<32 || c==127;}));J j;j.kind=J::Kind::String;j.text=std::move(s);return j;}
J N(uint64_t n){return S(std::to_string(n));}
J A(std::vector<J> v={}){J j;j.kind=J::Kind::Array;j.items=std::move(v);return j;}
const std::vector<J>& Array(const J& j,size_t max){Need(j.kind==J::Kind::Array && j.items.size()<=max);return j.items;}
std::string Text(const J& j,size_t max=4096){Need(j.kind==J::Kind::String && j.text.size()<=max);S(j.text);return j.text;}
uint64_t Number(const J& j){auto s=Text(j,20);uint64_t n{};auto p=std::from_chars(s.data(),s.data()+s.size(),n);Need(p.ec==std::errc{} && p.ptr==s.data()+s.size() && std::to_string(n)==s);return n;}
uint64_t Small(const J& j,uint64_t max){auto n=Number(j);Need(n<=max);return n;}
uint64_t Position(float f){Need(std::isfinite(f) && f>=0 && f<=10000000);return uint64_t(std::round(double(f)*1000));}
J Section(const SectionState& s){Need(s.note.saved.size()<256 && s.note.draft().size()<256 && s.selection.selected.size()<=4096);std::vector<J> selected;for(auto id:s.selection.selected)selected.push_back(N(id));return A({S(s.note.saved),S(std::string(s.note.draft())),A(std::move(selected)),s.selection.focused?N(*s.selection.focused):S(""),N(Position(s.scroll_x)),N(Position(s.scroll_y)),N(Position(s.table_x)),N(Position(s.table_y))});}
SectionState ReadSection(const J& j){const auto& a=Array(j,8);Need(a.size()==8);SectionState s;s.note=ui::components::Edit(Text(a[0],255));Need(s.note.SetDraft(Text(a[1],255)));for(const auto& key:Array(a[2],4096))Need(s.selection.selected.insert(Number(key)).second);if(Text(a[3],20)!="")s.selection.focused=Number(a[3]);s.scroll_x=float(Small(a[4],10000000000ULL))/1000;s.scroll_y=float(Small(a[5],10000000000ULL))/1000;s.table_x=float(Small(a[6],10000000000ULL))/1000;s.table_y=float(Small(a[7],10000000000ULL))/1000;return s;}
J Coins(const std::vector<Outpoint>& coins){Need(coins.size()<=4096);std::vector<J> v;std::set<std::string> seen;for(const auto& c:coins){const auto key=c.txid.hex()+":"+std::to_string(c.index);Need(seen.insert(key).second);v.push_back(A({S(c.txid.hex()),N(c.index)}));}return A(std::move(v));}
std::vector<Outpoint> ReadCoins(const J& j){std::vector<Outpoint> v;for(const auto& c:Array(j,4096)){const auto& a=Array(c,2);Need(a.size()==2);const auto text=Text(a[0],64);auto h=Hash256::Parse(text);Need(h && h->hex()==text);v.push_back({*h,uint32_t(Small(a[1],UINT32_MAX))});}Coins(v);return v;}
J PublicText(std::string s){Need(s.size()<=4096 && rpc::ValidUtf8(s) && std::none_of(s.begin(),s.end(),[](unsigned char c){return (c<32 && c!='\n' && c!='\t' && c!='\r') || c==127;}));J j;j.kind=J::Kind::String;j.text=std::move(s);return j;}
std::string ReadPublicText(const J& j){Need(j.kind==J::Kind::String);PublicText(j.text);return j.text;}
J History(const HistoryDocumentState& h){Need(h.filter.size()<=255 && h.sort>=0 && h.sort<=3 && h.expanded.size()<=4096 && h.drafts.size()<=32);std::vector<J> expanded,drafts;for(const auto& id:h.expanded){Need(bool(Hash256::Parse(id)));expanded.push_back(S(id));}Need(h.selected.empty() || bool(Hash256::Parse(h.selected)));for(const auto& [id,d]:h.drafts){Need(bool(Hash256::Parse(id)));drafts.push_back(A({S(id),PublicText(d.saved),PublicText(d.draft),N(d.base_revision)}));}return A({N(h.enabled),S(h.selected),S(h.filter),N(h.sort),N(h.descending),A(std::move(expanded)),A(std::move(drafts))});}
HistoryDocumentState ReadHistory(const J& j){const auto& a=Array(j,7);Need(a.size()==7);HistoryDocumentState h;h.enabled=Small(a[0],1);h.selected=Text(a[1],64);h.filter=Text(a[2],255);h.sort=int(Small(a[3],3));h.descending=Small(a[4],1);for(const auto& id:Array(a[5],4096))Need(h.expanded.insert(Text(id,64)).second);for(const auto& draft:Array(a[6],32)){const auto& d=Array(draft,4);Need(d.size()==4);Need(h.drafts.emplace(Text(d[0],64),HistoryLabelDraft{ReadPublicText(d[1]),ReadPublicText(d[2]),"",Number(d[3])}).second);}History(h);return h;}
bool AddressKey(std::string_view key){if(key.empty())return true;if(key.size()<66 || key.size()>75 || key[64]!=':' || !Hash256::Parse(key.substr(0,64)))return false;uint64_t n{};const auto p=std::from_chars(key.data()+65,key.data()+key.size(),n);return p.ec==std::errc{} && p.ptr==key.data()+key.size() && n<=0x7fffffff && std::to_string(n)==key.substr(65);}
J Addresses(const AddressPresentation& a){Need(AddressKey(a.selected) && a.expanded.size()<=128 && std::isfinite(a.split) && a.split>=0.2F && a.split<=0.8F);std::vector<J> expanded;for(const auto& key:a.expanded){Need(!key.empty() && AddressKey(key));expanded.push_back(S(key));}return A({S(a.selected),A(std::move(expanded)),N(a.group),N(a.hide_empty_used),N(a.transactions),N(Position(a.split)),N(Position(a.receive_x)),N(Position(a.receive_y)),N(Position(a.change_x)),N(Position(a.change_y))});}
AddressPresentation ReadAddresses(const J& j){const auto& a=Array(j,10);Need(a.size()==10);AddressPresentation p;p.selected=Text(a[0],75);for(const auto& key:Array(a[1],128))Need(p.expanded.insert(Text(key,75)).second);p.group=Small(a[2],1);p.hide_empty_used=Small(a[3],1);p.transactions=Small(a[4],1);p.split=float(Small(a[5],800))/1000;p.receive_x=float(Small(a[6],10000000000ULL))/1000;p.receive_y=float(Small(a[7],10000000000ULL))/1000;p.change_x=float(Small(a[8],10000000000ULL))/1000;p.change_y=float(Small(a[9],10000000000ULL))/1000;Addresses(p);return p;}
J CoinView(const CoinPresentation& p){Need(p.selected.size()<=4096 && p.drafts.size()<=32 && (p.focused.empty() || bool(ParseCoinKey(p.focused))));std::vector<J> selected,drafts;for(const auto& key:p.selected){Need(bool(ParseCoinKey(key)));selected.push_back(S(key));}for(const auto& [key,d]:p.drafts){Need(bool(ParseCoinKey(key)));drafts.push_back(A({S(key),PublicText(d.saved),PublicText(d.draft),N(d.base_revision)}));}return A({A(std::move(selected)),S(p.focused),N(p.by_value),N(p.descending),A(std::move(drafts))});}
CoinPresentation ReadCoinView(const J& j){const auto& a=Array(j,5);Need(a.size()==5);CoinPresentation p;for(const auto& key:Array(a[0],4096))Need(p.selected.insert(Text(key,75)).second);p.focused=Text(a[1],75);p.by_value=Small(a[2],1);p.descending=Small(a[3],1);for(const auto& item:Array(a[4],32)){const auto& d=Array(item,4);Need(d.size()==4);Need(p.drafts.emplace(Text(d[0],75),HistoryLabelDraft{ReadPublicText(d[1]),ReadPublicText(d[2]),"",Number(d[3])}).second);}CoinView(p);return p;}
J Reference(const std::optional<ObservedTransaction>& r){if(!r)return A();Need(!r->wallet.daemon_wallet || r->wallet.daemon_wallet->size()<=1024);return A({S(r->wallet.backend.value()),S(r->wallet.wallet.value()),S(r->wallet.network.genesis.hex()),S(r->txid.hex()),A(r->wallet.daemon_wallet?std::vector<J>{S(*r->wallet.daemon_wallet)}:std::vector<J>{})});}
std::optional<ObservedTransaction> ReadReference(const J& j){const auto& a=Array(j,5);if(a.empty())return {};Need(a.size()==4 || a.size()==5);auto backend=BackendSessionId::Make(Text(a[0],256));auto wallet=WalletId::Make(Text(a[1],1024));auto genesis=Hash256::Parse(Text(a[2],64)),txid=Hash256::Parse(Text(a[3],64));Need(backend && wallet && genesis && txid);std::optional<std::string> actual;if(a.size()==5){const auto& name=Array(a[4],1);if(!name.empty())actual=Text(name[0],1024);}return ObservedTransaction{{*backend,*wallet,{*genesis},actual},*txid};}
J PinnedScript(std::string s){Need(s.size()<=10000 && s.size()%2==0 && std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}));J j;j.kind=J::Kind::String;j.text=std::move(s);return j;}
J OfflineProof(const protocol::OfflineCoinProof& proof)
{
    Need(proof.observed_ms>0 && proof.previous_hex.size()<=protocol::MAX_OFFLINE_PREVIOUS*2);
    J hex;
    hex.kind=J::Kind::String;
    hex.text=proof.previous_hex;
    return A({std::move(hex),S(proof.descriptor),PinnedScript(proof.redeem_script),PinnedScript(proof.witness_script),
        N(uint64_t(proof.observed_ms))});
}
J PinnedDraft(const std::optional<CoinDraft>& draft)
{
    if (!draft) return A();
    const auto& b=draft->wallet;
    Need(draft->inputs.size()<=4096 && !draft->inputs.empty());
    std::vector<J> inputs;
    std::set<std::string> seen;
    for (const auto& pin:draft->inputs) {
        Need(seen.insert(CoinKey(pin.outpoint)).second && (!pin.value || pin.value->sats()>=0));
        inputs.push_back(A({S(pin.outpoint.txid.hex()),N(pin.outpoint.index),S(pin.value?pin.value->btc():""),PinnedScript(pin.script),
            PublicText(pin.pinned_reason),pin.offline?OfflineProof(*pin.offline):A()}));
    }
    J change=A();
    if (draft->offline_change) {
        const auto& proof=*draft->offline_change;
        Need(proof.observed_ms>0 && proof.index<0x80000000U);
        change=A({S(proof.descriptor),N(proof.index),N(uint64_t(proof.observed_ms))});
    }
    return A({A({S(b.backend.value()),S(b.wallet.value()),S(b.network.genesis.hex()),
        A(b.daemon_wallet?std::vector<J>{S(*b.daemon_wallet)}:std::vector<J>{})}),A(std::move(inputs)),std::move(change),N(draft->observed_unavailable)});
}
std::optional<CoinDraft> ReadPinnedDraft(const J& j)
{
    const auto& a=Array(j,4);
    if (a.empty()) return {};
    Need(a.size()==2 || a.size()==4);
    const auto& b=Array(a[0],4);Need(b.size()==4);
    auto origin=BackendSessionId::Make(Text(b[0],256));
    auto wallet=WalletId::Make(Text(b[1],256));
    auto genesis=Hash256::Parse(Text(b[2],64));
    Need(origin && wallet && genesis);
    const auto& name=Array(b[3],1);
    CoinDraft d{{*origin,*wallet,{*genesis},name.empty()?std::nullopt:std::optional(Text(name[0],1024))},{}};
    const auto network=protocol::DocumentNetwork(d.wallet.network);
    size_t budget=protocol::MAX_OFFLINE_PREVIOUS*2;
    for (const auto& entry:Array(a[1],4096)) {
        const auto& p=Array(entry,6);
        Need(p.size()==5 || p.size()==6);
        auto tx=Hash256::Parse(Text(p[0],64));
        Need(tx && tx->hex()==p[0].text);
        auto value=Text(p[2],32);
        std::optional<Money> amount;
        if (!value.empty()) {amount=Money::ParseBtc(value);Need(amount && amount->sats()>=0 && amount->btc()==value);}
        Need(p[3].kind==J::Kind::String);
        PinnedScript(p[3].text);
        PinnedCoin pin{{*tx,uint32_t(Small(p[1],UINT32_MAX))},amount,p[3].text,ReadPublicText(p[4]),
            "Unknown: restored public pin requires fresh explicit validation"};
        if (p.size()==6) {
            const auto& proof=Array(p[5],5);
            if (!proof.empty()) {
                Need(proof.size()==5 && proof[0].kind==J::Kind::String && proof[0].text.size()<=budget);
                budget-=proof[0].text.size();
                pin.offline=protocol::OfflineCoinProof{proof[0].text,Text(proof[1],4096),Text(proof[2],10000),Text(proof[3],10000),
                    int64_t(Small(proof[4],INT64_MAX))};
                Need(protocol::ValidOfflineCoin({pin.outpoint,pin.value,pin.script,pin.offline},network));
            }
        }
        d.inputs.push_back(std::move(pin));
    }
    if (a.size()==4) {
        const auto& ch=Array(a[2],3);
        if (!ch.empty()) {
            Need(ch.size()==3);
            auto change=protocol::OfflineChange{Text(ch[0],4096),uint32_t(Small(ch[1],0x7fffffffU)),int64_t(Small(ch[2],INT64_MAX))};
            Need(change.observed_ms>0 && protocol::DerivePublicDescriptor(change.descriptor,protocol::DocumentChain(network),change.index).has_value());
            d.offline_change=std::move(change);
        }
        d.observed_unavailable=Small(a[3],1);
    }
    PinnedDraft(d);
    return d;
}
J Imported(const std::optional<ImportedDocument>& imported)
{
    if (!imported) return A();
    Need(imported->parsed && imported->parsed->source.size()<=protocol::MAX_TRANSACTION_SOURCE
         && unsigned(imported->network)<=unsigned(protocol::DisplayNetwork::Regtest)
         && (imported->representation==DocumentRepresentation::Source
             || (imported->representation==DocumentRepresentation::WireHex && !imported->parsed->wire.empty())));
    J source;
    source.kind=J::Kind::String;
    source.text=protocol::TransactionBase64(imported->parsed->source);
    return A({N(unsigned(imported->parsed->format)),N(unsigned(imported->network)),
              N(unsigned(imported->representation)),std::move(source)});
}
std::optional<ImportedDocument> ReadImported(const J& item)
{
    const auto& fields=Array(item,4);
    if (fields.empty()) return {};
    Need(fields.size()==4 && fields[3].kind==J::Kind::String);
    const auto format=protocol::DocumentFormat(Small(fields[0],unsigned(protocol::DocumentFormat::PublicDescriptor)));
    const auto network=protocol::DisplayNetwork(Small(fields[1],unsigned(protocol::DisplayNetwork::Regtest)));
    const auto representation=DocumentRepresentation(Small(fields[2],unsigned(DocumentRepresentation::WireHex)));
    auto source=protocol::DecodeTransactionBase64(fields[3].text,protocol::MAX_TRANSACTION_SOURCE);
    Need(bool(source));
    const auto verified=protocol::ParseTransactionDocument(*source,network);
    sodium_memzero(source->data(),source->size());
    Need(verified.document && verified.document->format==format
         && (representation==DocumentRepresentation::Source || !verified.document->wire.empty()));
    return ImportedDocument{verified.document,network,representation};
}
J Doc(const Document& d){Need(d.id && d.id<UINT64_MAX && d.name.size()<=255 && !d.name.empty() && d.original_name.size()<=255 && d.icon>=0 && d.icon<=2 && size_t(d.section)<6 && d.observation!=ObservationState::Locked);Need((d.kind==DocumentKind::Wallet || d.kind==DocumentKind::Transaction) && (d.kind==DocumentKind::Wallet)==d.binding.has_value());Need(!d.imported || (d.kind==DocumentKind::Transaction && !d.coin_draft && !d.observed_transaction));std::vector<J> binding;if(d.binding){Need(d.binding->backend.value().size()<=256 && d.binding->wallet.value().size()<=1024 && (!d.binding->daemon_wallet || d.binding->daemon_wallet->size()<=1024));binding={S(d.binding->backend.value()),S(d.binding->wallet.value()),S(d.binding->network.genesis.hex())};if(d.binding->daemon_wallet)binding.push_back(S(*d.binding->daemon_wallet));}std::vector<J> sections;for(const auto& s:d.sections)sections.push_back(Section(s));std::set<std::string> selected;for(const auto& c:d.selected_coins)selected.insert(c.txid.hex()+":"+std::to_string(c.index));for(const auto& c:d.excluded_coins)Need(!selected.contains(c.txid.hex()+":"+std::to_string(c.index)));return A({N(d.id),N(uint64_t(d.kind)),S(d.name),A(std::move(binding)),N(d.complete),N(uint64_t(d.section)),A(std::move(sections)),Section(d.transaction),S(d.original_name),N(uint64_t(d.icon)),N(d.draft_revision.value),Coins(d.selected_coins),Coins(d.excluded_coins),History(d.history),Reference(d.observed_transaction),Addresses(d.addresses),CoinView(d.coins),PinnedDraft(d.coin_draft),Imported(d.imported)});}
Document ReadDoc(const J& j){const auto& a=Array(j,19);Need(a.size()==13 || (a.size()>=15 && a.size()<=19));Document d{Number(a[0]),DocumentKind(Small(a[1],1)),Text(a[2],255),std::nullopt};const auto& b=Array(a[3],4);if(!b.empty()){Need(b.size()==3 || b.size()==4);auto backend=BackendSessionId::Make(Text(b[0],256));auto wallet=WalletId::Make(Text(b[1],1024));auto genesis=Hash256::Parse(Text(b[2],64));Need(backend && wallet && genesis && genesis->hex()==b[2].text);d.binding=WalletBinding{*backend,*wallet,{*genesis},b.size()==4?std::optional(Text(b[3],1024)):std::nullopt};}d.complete=Small(a[4],1);d.section=app::Section(Small(a[5],5));const auto& sections=Array(a[6],6);Need(sections.size()==6);for(size_t i=0;i<6;++i)d.sections[i]=ReadSection(sections[i]);d.transaction=ReadSection(a[7]);d.original_name=Text(a[8],255);d.icon=int(Small(a[9],2));d.draft_revision={Number(a[10])};d.selected_coins=ReadCoins(a[11]);d.excluded_coins=ReadCoins(a[12]);if(a.size()>=15){d.history=ReadHistory(a[13]);d.observed_transaction=ReadReference(a[14]);Need(!d.observed_transaction || d.kind==DocumentKind::Transaction);}if(a.size()>=16)d.addresses=ReadAddresses(a[15]);if(a.size()>=17)d.coins=ReadCoinView(a[16]);if(a.size()>=18){d.coin_draft=ReadPinnedDraft(a[17]);if(d.coin_draft){Need(d.kind==DocumentKind::Transaction && d.coin_draft->inputs.size()==d.selected_coins.size());std::set<std::string> a,b;for(const auto& input:d.coin_draft->inputs)a.insert(CoinKey(input.outpoint));for(const auto& coin:d.selected_coins)b.insert(CoinKey(coin));Need(a==b);}}if(a.size()==19)d.imported=ReadImported(a[18]);d.observation=ObservationState::Offline;Doc(d);return d;}
void Retain(std::vector<Document>& archived,const Document& d){if(archived.size()==8)archived.erase(archived.begin());archived.push_back(d);}
}
std::optional<std::string> EncodePublicSession(const PublicSession& s){try{Need(s.documents.Documents().size()<=32 && s.recoverable.size()<=8 && s.unit>=0 && s.unit<=2);std::vector<J> docs,old;for(const auto& d:s.documents.Documents())docs.push_back(Doc(d));for(const auto& d:s.recoverable)old.push_back(Doc(d));auto result=A({N(7),N(s.documents.ActiveId()),A(std::move(docs)),A(std::move(old)),N(s.hide_amounts),N(uint64_t(s.unit))}).Encode();Need(result.size()<=524288);return result;}catch(...){return {};}}
std::optional<PublicSession> DecodePublicSession(std::string_view text){try{Need(text.size()<=524288);auto json=J::Parse(text,12,150000);Need(bool(json));const auto& a=Array(*json,6);Need(a.size()==6 && (Number(a[0])>=1 && Number(a[0])<=7));PublicSession s;std::vector<Document> docs;uint64_t floor{};for(const auto& d:Array(a[2],32))docs.push_back(ReadDoc(d));for(const auto& d:Array(a[3],8)){s.recoverable.push_back(ReadDoc(d));floor=std::max(floor,s.recoverable.back().id);}s.hide_amounts=Small(a[4],1);s.unit=int(Small(a[5],2));Need(s.documents.RestorePublic(std::move(docs),Number(a[1]),floor));return s;}catch(...){return {};}}
bool RecoverySession::Open(){if(store)return true;if(Locked()){error="Unlock the protected workspace first.";return false;}try{if(!path.is_absolute()){error="Recovery path is unavailable.";return false;}if(!std::filesystem::exists(path.parent_path())){std::filesystem::create_directories(path.parent_path());std::filesystem::permissions(path.parent_path(),std::filesystem::perms::owner_all);}auto opened=storage::MetadataStore::Open(path);if(!opened){error=storage::ErrorName(opened.error);return false;}store=std::move(*opened.value);return true;}catch(...){error="Recovery store unavailable.";return false;}}
bool RecoverySession::LoadOpen(){auto result=store->LoadRecovery();if(!result){error=storage::ErrorName(result.error);return false;}record=*result.value;if(record.enabled && !record.public_documents.empty()){auto decoded=DecodePublicSession(record.public_documents);if(!decoded){error="Invalid public recovery snapshot; previous data retained.";return false;}saved=std::move(*decoded);pending=true;}return true;}
bool RecoverySession::Start(){if(started)return error.empty();started=true;try{auto encrypted=path;encrypted += ".protected";if(std::filesystem::exists(encrypted)){protected_store=true;gate.Lock();return true;}if(!std::filesystem::exists(path))return true;if(!Open())return false;return LoadOpen();}catch(...){error="Recovery store unavailable; no documents were restored.";return false;}}
bool RecoverySession::Commit(PublicSession& candidate,bool enabled){if(Locked())return false;auto encoded=EncodePublicSession(candidate);if(!encoded){error="Public draft exceeds recovery limits or is locked. Previous snapshot retained.";return false;}if(!Open())return false;storage::RecoveryRecord next{record.revision,enabled,enabled?*encoded:""};auto result=store->SaveRecovery(next);if(!result){error=storage::ErrorName(result.error);return false;}next.revision=*result.value;record=std::move(next);saved=candidate;error.clear();return true;}
bool RecoverySession::Enable(DocumentRegistry& documents){if(!started && !Start())return false;if(!error.empty() || pending)return false;PublicSession candidate{documents,saved.recoverable,hidden,unit};if(!Commit(candidate,true))return false;suspended=false;return true;}
bool RecoverySession::Disable(){PublicSession empty;if(!error.empty() || !Commit(empty,false))return false;pending=false;return true;}
bool RecoverySession::Restore(DocumentRegistry& documents){if(!pending || !documents.Documents().empty() || !error.empty())return false;documents=saved.documents;hidden=saved.hide_amounts;unit=saved.unit;pending=false;return true;}
bool RecoverySession::StartEmpty(){if(!pending)return false;pending=false;suspended=true;return true;}
bool RecoverySession::Retry(){if(Locked())return false;store.reset();started=false;pending=false;error.clear();return Start();}
bool RecoverySession::Protect(DocumentRegistry& documents,std::string_view password){if(Locked() || protected_store)return false;if(password.size()<12 || password.size()>255){error="Use a password of 12–255 bytes; existing work is retained.";protection_retry=true;return false;}if(protection_retry){error.clear();protection_retry=false;}if(!Enable(documents)){protection_retry=true;return false;}auto converted=store->Protect(password);if(!converted){error=converted.error==storage::Error::Busy?"Another instance is using this workspace. Close it and retry protection.":storage::ErrorName(converted.error);protection_retry=true;return false;}store=std::move(*converted.value);protected_store=true;error.clear();return true;}
bool RecoverySession::Lock(DocumentRegistry& documents){if(Locked())return true;if(!protected_store || !store){error="Set a local protection password before locking.";return false;}if(Enabled() && !Checkpoint(documents))return false;auto encoded=EncodePublicSession({documents,saved.recoverable,hidden,unit});if(!encoded){error="Current public draft cannot be protected; views retained.";return false;}auto sealed=store->SealMemory(*encoded);sodium_memzero(encoded->data(),encoded->size());if(!sealed){error=storage::ErrorName(sealed.error);return false;}locked_documents=std::move(*sealed.value);locked_revision=record.revision;unlock_conflict=false;gate.Lock();documents.Conceal();saved.documents.Conceal();for(auto& d:saved.recoverable){DocumentRegistry temporary;temporary.RestorePublic({std::move(d)},d.id);temporary.Conceal();}saved={};if(!record.public_documents.empty())sodium_memzero(record.public_documents.data(),record.public_documents.size());record.public_documents.clear();pending=false;store.reset();error.clear();return true;}
bool RecoverySession::Unlock(std::string_view password,DocumentRegistry& documents,bool choose_latest,bool preserve_other_copy){if(!Locked() || !documents.Documents().empty())return false;auto opened=storage::MetadataStore::OpenProtected(path,password,{},preserve_other_copy?storage::ConversionResolution::PreserveOtherCopy:storage::ConversionResolution::RequireIdentical);if(!opened){conversion_conflict=opened.error==storage::Error::ConversionConflict || opened.error==storage::Error::ConversionBackupConflict;error=opened.error==storage::Error::ConversionBackupConflict?"Existing encrypted conversion backup differs or is damaged. All copies are retained. Preserve that backup in a separate private archive before retrying; it will never be overwritten.":conversion_conflict?"Interrupted protection left different copies. Both are retained. Re-enter the password and choose whether to keep the protected workspace and encrypt the other copy as a recovery backup.":opened.error==storage::Error::Authentication?"Password is incorrect or protected data is damaged. Work is retained.":opened.error==storage::Error::Busy?"This workspace is open in another instance. Close it and retry.":"Protected workspace could not be opened. Work is retained.";return false;}store=std::move(*opened.value);conversion_conflict=false;error.clear();if(!LoadOpen()){store.reset();return false;}
    if(!locked_documents.empty() && record.revision!=locked_revision && !choose_latest){unlock_conflict=true;error="Another instance changed recovery. Choose the latest saved workspace explicitly; locked views are retained.";saved.documents.Conceal();saved.recoverable.clear();if(!record.public_documents.empty())sodium_memzero(record.public_documents.data(),record.public_documents.size());record.public_documents.clear();store.reset();return false;}
    if(choose_latest){locked_documents.clear();unlock_conflict=false;}
    if(!locked_documents.empty()){auto plain=store->UnsealMemory(locked_documents);if(!plain){error=storage::ErrorName(plain.error);store.reset();return false;}auto restored=DecodePublicSession(*plain.value);sodium_memzero(plain.value->data(),plain.value->size());if(!restored){error="Invalid protected session; data retained.";store.reset();return false;}saved=std::move(*restored);documents=saved.documents;hidden=saved.hide_amounts;unit=saved.unit;pending=false;locked_documents.clear();}
    if(!gate.Unlock()){store.reset();documents.Conceal();error="Lock epoch exhausted; restart required.";return false;}return true;
}
bool RecoverySession::CanAdmitPublicDocuments(const DocumentRegistry& documents) const
{
    return !Locked() && !Pending() && (!Enabled() || (error.empty() &&
        EncodePublicSession({documents,saved.recoverable,hidden,unit}).has_value()));
}
bool RecoverySession::Checkpoint(DocumentRegistry& documents){if(!Enabled() || pending)return !Enabled();if(!error.empty())return false;PublicSession candidate{documents,saved.recoverable,hidden,unit};
    for(const auto& old:saved.documents.Documents())if(!documents.Find(old.id))Retain(candidate.recoverable,old);
    try{for(const auto& d:documents.Documents()){auto* current=candidate.documents.Find(d.id);const auto* old=saved.documents.Find(d.id);if(old){current->draft_revision.value=std::max(d.draft_revision.value,old->draft_revision.value);auto before=*old,after=d;before.draft_revision={};after.draft_revision={};if(Doc(before).Encode()!=Doc(after).Encode()){if(old->draft_revision.value==UINT64_MAX){error="Draft revision exhausted; previous snapshot retained.";return false;}current->draft_revision.value=std::max(d.draft_revision.value,old->draft_revision.value+1);}}}}catch(...){error="Invalid or locked public draft; previous snapshot retained.";return false;}
    auto encoded=EncodePublicSession(candidate);if(encoded && *encoded==record.public_documents)return true;if(!Commit(candidate,true))return false;for(const auto& d:candidate.documents.Documents())if(auto* current=documents.Find(d.id))current->draft_revision=d.draft_revision;return true;
}
bool RecoverySession::Close(DocumentRegistry& documents,std::span<const uint64_t> ids){if(!Enabled())return documents.Close(ids);if(pending || !error.empty())return false;auto candidate=documents;for(auto id:ids){const auto* d=documents.Find(id);if(!d || d->observation==ObservationState::Locked)return false;}if(!Checkpoint(documents))return false;candidate=documents;if(!candidate.Close(ids) || !Checkpoint(candidate))return false;documents=std::move(candidate);return true;}
bool RecoverySession::Clear(DocumentRegistry& documents,uint64_t id){if(!Enabled() || pending || !Checkpoint(documents))return false;PublicSession candidate{documents,saved.recoverable,hidden,unit};auto* d=candidate.documents.Find(id);if(!d || d->observation==ObservationState::Locked)return false;Retain(candidate.recoverable,*d);for(auto& section:d->sections)section.note=ui::components::Edit("");d->transaction.note=ui::components::Edit("");d->selected_coins.clear();d->excluded_coins.clear();d->history.drafts.clear();d->coins.drafts.clear();d->coins.selected.clear();d->coins.focused.clear();d->coin_draft.reset();if(d->draft_revision.value==UINT64_MAX)return false;++d->draft_revision.value;if(!Commit(candidate,true))return false;documents=candidate.documents;return true;}
bool RecoverySession::RecoverLast(DocumentRegistry& documents){if(!Enabled() || pending || saved.recoverable.empty() || !Checkpoint(documents))return false;PublicSession candidate{documents,saved.recoverable,hidden,unit};auto restored=candidate.recoverable.back();candidate.recoverable.pop_back();std::vector<Document> docs=candidate.documents.Documents();const auto it=std::find_if(docs.begin(),docs.end(),[&](const auto& d){return d.id==restored.id;});
    if(it!=docs.end()){if(it->observation==ObservationState::Locked || it->kind!=restored.kind || it->binding!=restored.binding)return false;Retain(candidate.recoverable,*it);if(std::max(it->draft_revision.value,restored.draft_revision.value)==UINT64_MAX)return false;restored.draft_revision.value=std::max(it->draft_revision.value,restored.draft_revision.value)+1;*it=restored;}else docs.push_back(restored);
    uint64_t floor{};for(const auto& d:candidate.recoverable)floor=std::max(floor,d.id);
    if(!candidate.documents.RestorePublic(std::move(docs),restored.id,floor))return false;if(!Commit(candidate,true))return false;documents=candidate.documents;return true;}
}
