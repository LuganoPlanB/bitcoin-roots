// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/document_registry.h"
#include <algorithm>
#include <stdexcept>
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
bool DocumentRegistry::Navigate(Section section)
{
    if(size_t(section)>=SectionNames.size())return false;
    auto* d=Active();if(!d || d->kind!=DocumentKind::Wallet)return false;
    if(d->section!=section) { d->section=section;d->Current().restore_scroll=d->Current().restore_table=true; }return true;
}
bool Document::Dirty() const
{
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
DocumentRegistry PublicFixtureDocuments()
{
    DocumentRegistry r;const NetworkIdentity network{*Hash256::Parse(std::string(64,'0'))};
    r.OpenWallet("Public watch-only wallet",{*BackendSessionId::Make("fixture-daemon-A"),*WalletId::Make("fixture-wallet"),network},true);
    r.OpenWallet("Second public wallet",{*BackendSessionId::Make("fixture-daemon-B"),*WalletId::Make("fixture-wallet"),network},true);
    r.OpenTransaction("Public transaction###fixture");r.Activate(1);return r;
}
}
