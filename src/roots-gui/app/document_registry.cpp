// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/document_registry.h"
#include <algorithm>
#include <stdexcept>
#include <sodium.h>
namespace roots::gui::app {
Document* DocumentRegistry::Find(uint64_t id) { for(auto& d:documents) if(d.id==id)return &d;return nullptr; }
const Document* DocumentRegistry::Find(uint64_t id) const { for(const auto& d:documents) if(d.id==id)return &d;return nullptr; }
bool DocumentRegistry::Activate(uint64_t id) { if(auto* d=Find(id)) { active=id;d->Current().restore_scroll=d->Current().restore_table=true;return true; }return false; }
uint64_t DocumentRegistry::OpenWallet(std::string name,WalletBinding binding,bool complete)
{
    for(const auto& d:documents) if(d.kind==DocumentKind::Wallet && d.binding==binding) { const auto id=d.id;Activate(id);return id; }
    if(!next)throw std::overflow_error("document identity exhausted");
    const auto id=next++;Document d{id,DocumentKind::Wallet,std::move(name),std::move(binding)};
    d.original_name=d.name;d.complete=complete;d.section=complete?Section::Transactions:Section::Settings;documents.push_back(std::move(d));Activate(id);return id;
}
uint64_t DocumentRegistry::OpenTransaction(std::string name)
{
    if(!next)throw std::overflow_error("document identity exhausted");
    const auto id=next++;documents.push_back(Document{id,DocumentKind::Transaction,std::move(name),std::nullopt});documents.back().original_name=documents.back().name;Activate(id);return id;
}
uint64_t DocumentRegistry::OpenImportedDocument(ImportedDocument imported)
{
    if (documents.size()>=32 || !next || next==UINT64_MAX || !imported.parsed) return 0;
    const auto name=imported.parsed->descriptor ? "Public descriptor" : imported.parsed->psbt_version
        ? "PSBT v"+std::to_string(*imported.parsed->psbt_version) : "Raw transaction";
    const auto id=OpenTransaction(name);
    auto* document=Find(id);
    document->imported=std::move(imported);
    document->transaction.note=ui::components::Edit("");
    return id;
}
uint64_t DocumentRegistry::OpenObservedTransaction(ObservedTransaction reference){for(const auto& d:documents)if(d.observed_transaction==reference){const auto id=d.id;Activate(id);return id;}if(documents.size()>=32)return 0;const auto id=OpenTransaction("Transaction "+reference.txid.hex().substr(0,12));auto* d=Find(id);d->observed_transaction=std::move(reference);d->transaction.note=ui::components::Edit("");return id;}
bool DocumentRegistry::Navigate(Section section)
{
    if(size_t(section)>=SectionNames.size())return false;
    auto* d=Active();if(!d || d->kind!=DocumentKind::Wallet)return false;
    if(d->section!=section) { d->section=section;d->Current().restore_scroll=d->Current().restore_table=true; }return true;
}
bool Document::Dirty() const
{
    if(coin_draft)return true;
    for(const auto& [key,draft]:coins.drafts)if(draft.dirty())return true;
    for(const auto& [id,draft]:history.drafts)if(draft.dirty())return true;
    if(kind==DocumentKind::Transaction)return transaction.note.dirty();
    return std::any_of(sections.begin(),sections.end(),[](const auto& s){return s.note.dirty();});
}
bool DocumentRegistry::Move(uint64_t id,int direction)
{
    if(direction!=-1 && direction!=1)return false;
    const auto it=std::find_if(documents.begin(),documents.end(),[&](const auto& d){return d.id==id;});
    if(it==documents.end())return false;const auto index=it-documents.begin(),to=index+direction;
    if(to<0 || to>=std::ptrdiff_t(documents.size()))return false;
    std::iter_swap(it,documents.begin()+to);return true;
}
bool DocumentRegistry::Rename(uint64_t id,std::string name)
{
    auto* d=Find(id);if(!d || name.empty() || name.size()>255 || name.find_first_of("\r\n")!=std::string::npos)return false;
    d->name=std::move(name);return true;
}
bool DocumentRegistry::SetIcon(uint64_t id,int icon)
{
    auto* d=Find(id);if(!d || icon<0 || icon>2)return false;d->icon=icon;return true;
}
std::vector<uint64_t> DocumentRegistry::CloseTargets(uint64_t id,bool others,bool all) const
{
    if(!Find(id))return {};std::vector<uint64_t> ids;
    for(const auto& d:documents)if(all || (others?d.id!=id:d.id==id))ids.push_back(d.id);return ids;
}
bool DocumentRegistry::Close(std::span<const uint64_t> ids)
{
    if(ids.empty())return false;std::set<uint64_t> unique;
    for(auto id:ids)if(!Find(id)||!unique.insert(id).second)return false;
    const auto old=std::find_if(documents.begin(),documents.end(),[&](const auto& d){return d.id==active;});
    const auto index=old==documents.end()?0:size_t(old-documents.begin());
    std::erase_if(documents,[&](const auto& d){return unique.contains(d.id);});
    if(unique.contains(active))active=documents.empty()?0:documents[std::min(index,documents.size()-1)].id;
    if(active)Activate(active);return true;
}
std::string Dependency(const Document& d,Section section)
{
    if(d.observation==ObservationState::Locked)return "Wallet is locked. A wallet unlock adapter is required to inspect its contents.";
    if(!d.complete && section!=Section::Settings)return "Complete the wallet policy in Settings before using this section. A daemon-wallet adapter is required.";
    return {};
}
bool DocumentRegistry::RestorePublic(std::vector<Document> restored,uint64_t active_id,uint64_t reserved_through)
{
    if(restored.size()>32)return false;uint64_t highest{};std::set<uint64_t> ids;
    for(const auto& d:restored){if(!d.id || d.id==UINT64_MAX || !ids.insert(d.id).second)return false;highest=std::max(highest,d.id);}
    if((restored.empty() && active_id) || (!restored.empty() && !ids.contains(active_id)))return false;
    if(reserved_through==UINT64_MAX)return false;
    documents=std::move(restored);active=active_id;next=std::max(highest,reserved_through)+1;return true;
}
void DocumentRegistry::Conceal()
{
    const auto wipe=[](std::string& s){if(!s.empty())sodium_memzero(s.data(),s.size());s.clear();};
    for(auto& d:documents){wipe(d.name);wipe(d.original_name);wipe(d.history.filter);wipe(d.history.selected);for(auto& [id,draft]:d.history.drafts){wipe(draft.saved);wipe(draft.draft);wipe(draft.error);}d.history.drafts.clear();d.history.expanded.clear();for(auto& [key,draft]:d.coins.drafts){wipe(draft.saved);wipe(draft.draft);wipe(draft.error);}d.coins=CoinPresentation();if(d.coin_draft){for(auto& p:d.coin_draft->inputs){wipe(p.script);wipe(p.pinned_reason);wipe(p.current_reason);if(p.offline){wipe(p.offline->previous_hex);wipe(p.offline->descriptor);wipe(p.offline->redeem_script);wipe(p.offline->witness_script);}}if(d.coin_draft->offline_change)wipe(d.coin_draft->offline_change->descriptor);d.coin_draft.reset();}for(auto* s:{&d.transaction,&d.sections[0],&d.sections[1],&d.sections[2],&d.sections[3],&d.sections[4],&d.sections[5]}){sodium_memzero(s->note.text.data(),s->note.text.size());wipe(s->note.saved);wipe(s->note.error);wipe(s->note.last_attempt);if(s->note.external_buffer)wipe(*s->note.external_buffer);s->selection.selected.clear();s->selection.focused.reset();}d.selected_coins.clear();d.excluded_coins.clear();}
    documents.clear();active=0;
}
DocumentRegistry PublicFixtureDocuments()
{
    DocumentRegistry r;const NetworkIdentity network{*Hash256::Parse(std::string(64,'0'))};
    r.OpenWallet("Public watch-only wallet",{*BackendSessionId::Make("fixture-daemon-A"),*WalletId::Make("fixture-wallet"),network},true);
    r.OpenWallet("Second public wallet",{*BackendSessionId::Make("fixture-daemon-B"),*WalletId::Make("fixture-wallet"),network},true);
    r.OpenTransaction("Public transaction###fixture");r.Activate(1);return r;
}
}
