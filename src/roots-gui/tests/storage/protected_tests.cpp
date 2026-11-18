// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/storage/metadata.h"
#include "app/recovery.h"
#include "services/refresh.h"
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace roots::gui;
namespace s=storage;namespace a=app;
namespace {
int checks{};
void Check(bool ok,const char* label){++checks;if(!ok)throw std::runtime_error(label);}
template<class T>T Take(s::Result<T> value){if(!value)throw std::runtime_error(s::ErrorName(value.error));return std::move(*value.value);}
constexpr std::string_view Password="Synthetic local protection password";
std::string Bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void Put(const std::filesystem::path& p,std::string_view bytes){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(bytes.data(),bytes.size());Check(bool(f),"owned fixture write");}
s::Scope Scope(){return {"public-backend",{*Hash256::Parse(std::string(64,'1'))},"wallet/札幌"};}
a::DocumentRegistry Docs(){auto docs=a::PublicFixtureDocuments();docs.Find(1)->sections[1].note.SetDraft("Protected public draft");docs.Find(1)->section=a::Section::Send;docs.Activate(1);return docs;}
void Storage(const std::filesystem::path& path){
    Check(s::MetadataStore::OpenProtected(path,Password).error==s::Error::Invalid,"missing protected file never creates replacement");auto plain=Take(s::MetadataStore::Open(path));s::Metadata metadata;metadata.display_name="Protected label sentinel";metadata.labels={{{s::TargetKind::Address,"public-address"},"Protected label sentinel"}};metadata.freezes={{s::TargetKind::Output,std::string(64,'a')+":7"}};metadata.drafts={{"independent-draft",{9007199254740999ULL},"Protected draft sentinel",Money::Satoshis(-9223372036854775807LL)}};metadata.revision=Take(plain->Save(Scope(),metadata));
    s::Workspace workspace;workspace.restore_documents=true;workspace.revision=Take(plain->SaveWorkspace(workspace));
    s::Profile profile;profile.id="public-profile";profile.candidate.expected_genesis=Scope().network.genesis.hex();profile.candidate.connection.credential={Authentication::SecureStore,"opaque-reference"};profile.revision=Take(plain->SaveProfile(profile));
    auto second=Take(s::MetadataStore::Open(path));Check(plain->Protect(Password).error==s::Error::Busy,"other instance blocks protection conversion");Check(plain->Save(Scope(),metadata).error==s::Error::Busy,"failed guard upgrade fails writes closed");second.reset();metadata.revision=Take(plain->Save(Scope(),metadata));auto protected_store=Take(plain->Protect(Password));plain.reset();Check(protected_store->Protected(),"native encryption provider admitted");
    auto encrypted=path;encrypted += ".protected";Check(!std::filesystem::exists(path) && std::filesystem::exists(encrypted),"plaintext path removed only after protected publication");const auto cipher=Bytes(encrypted);Check(cipher.find(metadata.display_name)==std::string::npos && cipher.find("Protected draft sentinel")==std::string::npos && cipher.find("SQLite format 3")==std::string::npos,"complete metadata is ciphertext");
    Check(Take(protected_store->Load(Scope()))==metadata && Take(protected_store->LoadWorkspace())==workspace && Take(protected_store->LoadProfile(profile.id))==profile,"all metadata categories survive protection");
    Check(s::MetadataStore::OpenProtected(path,Password).error==s::Error::Busy,"protected owner is exclusive");Check(s::MetadataStore::Open(path).error==s::Error::Busy,"plaintext fallback blocked while protected owner exists");
    const auto first=Take(protected_store->SealMemory("public-session")),other=Take(protected_store->SealMemory("public-session"));Check(first!=other,"library generates fresh stream nonce");Check(Take(protected_store->UnsealMemory(first))=="public-session","ephemeral lock snapshot roundtrip");auto damaged=first;damaged.back()^=1;Check(protected_store->UnsealMemory(damaged).error==s::Error::Authentication,"modified stream rejected");damaged.pop_back();Check(protected_store->UnsealMemory(damaged).error==s::Error::Authentication,"truncated stream rejected");
    auto next=metadata;next.display_name="Changed protected label";
    std::filesystem::permissions(path.parent_path(),std::filesystem::perms::owner_read|std::filesystem::perms::owner_exec);
    const auto failed=protected_store->Save(Scope(),next);
    std::filesystem::permissions(path.parent_path(),std::filesystem::perms::owner_all);
    Check(!failed && Take(protected_store->Load(Scope()))==metadata && Bytes(encrypted)==cipher,"write failure restores in-memory CAS state and previous cipher");
    next.revision=Take(protected_store->Save(Scope(),next));Check(protected_store->Save(Scope(),metadata).error==s::Error::Conflict,"protected CAS rejects stale writer");protected_store.reset();
    const auto saved=Bytes(encrypted);Check(s::MetadataStore::OpenProtected(path,"wrong synthetic password").error==s::Error::Authentication && Bytes(encrypted)==saved,"wrong password preserves encrypted bytes");
    auto altered=saved;altered.back()^=1;Put(encrypted,altered);Check(s::MetadataStore::OpenProtected(path,Password).error==s::Error::Authentication && Bytes(encrypted)==altered,"ciphertext tamper rejected before SQLite access");Put(encrypted,saved);protected_store=Take(s::MetadataStore::OpenProtected(path,Password));Check(Take(protected_store->Load(Scope()))==next,"valid protected store reopens after denied attempts");
}
void Session(const std::filesystem::path& path){
    a::RecoverySession session(path);auto docs=Docs();Check(!session.Protect(docs,"short") && !docs.Documents().empty(),"invalid setup password retains views and allows retry");Check(session.Protect(docs,Password),"explicit protection and recovery optin");const auto expected=a::EncodePublicSession({docs,{}});const auto revision=docs.Find(1)->draft_revision;Check(session.Lock(docs) && docs.Documents().empty() && session.Locked(),"lock drops document/editor navigation state");Check(!session.Unlock("wrong synthetic password",docs) && docs.Documents().empty() && session.Locked(),"authentication failure reveals no document");Check(session.Unlock(Password,docs) && a::EncodePublicSession({docs,{}})==expected && docs.Find(1)->draft_revision==revision,"unlock recovers independent state without replay");
    Check(session.Disable(),"disable recovery while retaining encryption");docs.Find(1)->Current().note.SetDraft("In-memory opt-out draft");Check(session.Lock(docs) && session.Unlock(Password,docs) && docs.Find(1)->Current().note.draft()=="In-memory opt-out draft","opt-out work survives lock only through encrypted memory");
    Check(session.Enable(docs) && session.Lock(docs),"prepare concurrent locked-session case");{auto writer=Take(s::MetadataStore::OpenProtected(path,Password));auto record=Take(writer->LoadRecovery());record.public_documents=*a::EncodePublicSession({a::DocumentRegistry(),{}});Take(writer->SaveRecovery(record));}
    Check(!session.Unlock(Password,docs) && session.UnlockConflict() && docs.Documents().empty(),"concurrent recovery cannot be overwritten by old locked views");Check(session.Unlock(Password,docs,true) && session.Pending() && docs.Documents().empty(),"latest recovery requires deliberate choice and restore prompt");
}
RequestContext Context(){return {*BackendSessionId::Make("backend"),{*Hash256::Parse(std::string(64,'1'))},*WalletId::Make("wallet"),*DocumentId::Make("document"),{7},{0}};}
void HeldBoundaries(){
    for(auto kind:{ProtectedOperation::Signing,ProtectedOperation::AddressLoad,ProtectedOperation::LabelSave,ProtectedOperation::Refresh}){
        SessionGate gate;auto context=Context();const auto ticket=gate.Begin(kind,context);Check(bool(ticket),"sensitive operation admitted with full context");std::mutex mutex;std::condition_variable changed;bool entered{},release{},finished{};
        std::jthread worker([&]{std::unique_lock lock(mutex);entered=true;changed.notify_all();if(!changed.wait_for(lock,std::chrono::seconds(2),[&]{return release;}))return;finished=true;});
        {std::unique_lock lock(mutex);Check(changed.wait_for(lock,std::chrono::seconds(1),[&]{return entered;}),"held operation actually entered");}
        Check(gate.Lock() && ticket->cancellation.stop_requested(),"lock cancels local waiting and advances epoch");Check(!gate.Begin(kind,context),"no sensitive command admitted while locked");Check(gate.Unlock(),"authenticated owner can unlock new epoch");context.lock_epoch=gate.Epoch();{std::lock_guard lock(mutex);release=true;}changed.notify_all();worker.join();Check(finished,"held operation released and settled");bool mutated{};Check(!gate.Adopt(*ticket,context,kind,[&]{mutated=true;return true;}) && !mutated,"late signing/address/label/refresh adoption rejected");auto fresh=gate.Begin(kind,context);Check(fresh && gate.Adopt(*fresh,context,kind,[]{return true;}),"new epoch admits only fresh exact-context result");auto wrong=context;wrong.revision.value++;Check(!gate.Accept(*fresh,wrong,kind),"draft revision remains part of lock admission");}
}
void RefreshBoundary(){
    BackendCapabilities caps;caps.authenticated=caps.identity_verified=true;caps.candidate.expected_genesis=caps.genesis=std::string(64,'1');caps.candidate.wallet="wallet";for(const char* m:{"getwalletinfo","getbalances","listsinceblock"})caps.methods[m]={true,true,true,false,{"blockhash","include_removed"}};
    std::mutex mutex;std::condition_variable changed;bool entered{},release{};std::atomic<bool> stopped{};
    RefreshService refresh([&](const RefreshRequest& request,std::stop_token stop){std::unique_lock lock(mutex);entered=true;changed.notify_all();changed.wait_for(lock,std::chrono::seconds(2),[&]{return release;});stopped=stop.stop_requested();RefreshReply reply{request.identity,request.generation,request.sequence};reply.observed_unix_ms=1700000000000LL;reply.node=NodeObservation{10,std::string(64,'a'),false,false};return reply;});
    RefreshIdentity identity{Context(),caps.candidate,1};Check(refresh.Bind(identity,caps,false,0),"actual refresh binds");refresh.Tick(0);{std::unique_lock lock(mutex);Check(changed.wait_for(lock,std::chrono::seconds(1),[&]{return entered;}),"actual refresh held");}refresh.PrivacyLock();Check(refresh.View().locked && !refresh.View().identity && !refresh.View().node && !refresh.RequestNow(0),"actual refresh lock hides retained data and blocks requests");{std::lock_guard lock(mutex);release=true;}changed.notify_all();const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(refresh.Busy() && std::chrono::steady_clock::now()<end){refresh.Tick(0);std::this_thread::yield();}Check(!refresh.Busy() && stopped && !refresh.View().node,"actual late refresh cancelled/discarded and worker settled");
}
}
int main(int argc,char** argv){if(argc!=3)return 2;try{const std::string mode=argv[1];if(mode=="storage")Storage(argv[2]);else if(mode=="session")Session(argv[2]);else if(mode=="held"){HeldBoundaries();RefreshBoundary();}else return 2;std::cout<<"{\"passed\":true,\"checks\":"<<checks<<"}\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<" after "<<checks<<" checks\n";return 1;}}
