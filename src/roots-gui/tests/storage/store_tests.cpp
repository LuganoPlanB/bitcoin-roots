// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/storage/metadata.h"
#include "adapters/protocol/public_wallet.h"
#include "adapters/storage/schema.h"
#include <sqlite3.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
using namespace roots::gui;
namespace s=roots::gui::storage;
namespace {
int checks{};
void Check(bool ok){if(!ok)throw std::runtime_error("store contract "+std::to_string(checks+1));++checks;}
template<class T>T Take(s::Result<T> r){if(!r)throw std::runtime_error(std::string("store result ")+s::ErrorName(r.error));return std::move(*r.value);}
struct Raw {
    sqlite3* db{};
    explicit Raw(const std::filesystem::path& path){if(!std::filesystem::exists(path)){std::ofstream file(path);file.close();std::filesystem::permissions(path,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write);}Check(sqlite3_open_v2(path.string().c_str(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK);}
    ~Raw(){if(sqlite3_close(db)!=SQLITE_OK)std::terminate();}
    void Exec(std::string_view sql){Check(sqlite3_exec(db,std::string(sql).c_str(),nullptr,nullptr,nullptr)==SQLITE_OK);}
    int64_t Number(const char* sql){sqlite3_stmt* q{};Check(sqlite3_prepare_v2(db,sql,-1,&q,nullptr)==SQLITE_OK);Check(sqlite3_step(q)==SQLITE_ROW);const auto n=sqlite3_column_int64(q,0);sqlite3_finalize(q);return n;}
};
s::Scope Scope(std::string backend="backend",char genesis='1',std::string wallet="wallet/札幌"){return {std::move(backend),{*Hash256::Parse(std::string(64,genesis))},std::move(wallet)};}
s::Metadata Example(){s::Metadata m;m.display_name="Public vault ## literal";m.labels={{{s::TargetKind::Address,"public-address"},"Public receive"},{{s::TargetKind::Transaction,std::string(64,'a')},"Invoice"}};m.layouts={{"history","value",true}};m.widths={{"history","label",252},{"history","value",140}};m.freezes={{s::TargetKind::Address,"public-address"},{s::TargetKind::Output,std::string(64,'a')+":4294967295"}};m.drafts={{"a-min",{std::numeric_limits<uint64_t>::max()},"Public note",Money::Satoshis(std::numeric_limits<int64_t>::min())},{"b-max",{1},"Other public note",Money::Satoshis(std::numeric_limits<int64_t>::max())},{"c-unknown",{0},"Unknown value",{}}};return m;}
Provenance ProvenanceFor(const s::Scope& scope,int64_t observed=1700000000000LL){return {{*BackendSessionId::Make("live-session"),scope.network,*WalletId::Make("opaque-wallet"),*DocumentId::Make("observation-document"),{std::numeric_limits<uint64_t>::max()},{std::numeric_limits<uint64_t>::max()}},Freshness::Live,observed,123};}
void Persistence(const std::filesystem::path& path){
    auto store=Take(s::MetadataStore::Open(path));auto scope=Scope();auto m=Example();Check(Take(store->Load(scope)).revision==0);Check(Take(store->Save(scope,m))==1);m.revision=1;Check(Take(store->Load(scope))==m);
    const auto p=ProvenanceFor(scope);Check(bool(store->SaveObservation(scope,"balance",p,Money::Satoshis(std::numeric_limits<int64_t>::min()))));
    Check(Take(store->Load(scope))==m);auto cache=Take(store->LoadObservation(scope,"balance"));Check(cache.provenance.context==p.context && cache.provenance.observed_unix_ms==p.observed_unix_ms && cache.provenance.height==p.height);Check(cache.provenance.freshness==Freshness::Cached && cache.observed_freshness==Freshness::Live && !cache.provenance.IsLiveFor(p.context));Check(cache.amount->sats()==std::numeric_limits<int64_t>::min());
    auto old=ProvenanceFor(scope,*p.observed_unix_ms-1);Check(store->SaveObservation(scope,"balance",old,Money::Satoshis(7)).error==s::Error::Conflict);Check(Take(store->LoadObservation(scope,"balance")).amount==cache.amount);
    for(auto other:{Scope("other"),Scope("backend",'2'),Scope("backend",'1',"other"),Scope("backend",'1',"")}){auto value=Example();value.display_name="Distinct wallet";Check(Take(store->Save(other,value))==1);Check(store->LoadObservation(other,"balance").error==s::Error::NotFound);Check(Take(store->Load(scope))==m);}
    auto bad=p;bad.context.network=Scope("backend",'2').network;Check(store->SaveObservation(scope,"balance",bad,{}).error==s::Error::Invalid);
    auto invalid=m;invalid.labels.push_back(invalid.labels[0]);Check(store->Save(scope,invalid).error==s::Error::Invalid);Check(Take(store->Load(scope))==m);
    invalid=m;invalid.freezes.push_back({s::TargetKind::Transaction,std::string(64,'b')});Check(store->Save(scope,invalid).error==s::Error::Invalid);
    invalid=m;invalid.freezes.push_back({s::TargetKind::Output,std::string(64,'A')+":1"});Check(store->Save(scope,invalid).error==s::Error::Invalid);
    invalid=m;invalid.labels.push_back({{s::TargetKind::Transaction,std::string(64,'A')},"Noncanonical alias"});Check(store->Save(scope,invalid).error==s::Error::Invalid);
    invalid=m;invalid.revision=uint64_t(std::numeric_limits<int64_t>::max());Check(store->Save(scope,invalid).error==s::Error::Invalid);Check(Take(store->Load(scope))==m);
    s::Workspace w;w.preferences={9,true,true,false,ExplorerMode::Custom,"https://explorer.example/tx/{txid}",std::string(64,'1')};
    w.preferences.desktop_notifications=true;w.preferences.camera_keep_awake=true;
    w.restore_documents=true;w.appearance=2;w.unit=1;Check(Take(store->SaveWorkspace(w))==1);w.revision=1;Check(Take(store->LoadWorkspace())==w);
    auto stale=w;stale.revision=0;Check(store->SaveWorkspace(stale).error==s::Error::Conflict);
    auto unsafe=w;unsafe.preferences.explorer_template="http://explorer.example/tx/{txid}";
    Check(store->SaveWorkspace(unsafe).error==s::Error::Invalid);Check(Take(store->LoadWorkspace())==w);
    s::Profile profile;profile.id="backend";profile.candidate.expected_genesis=scope.network.genesis.hex();profile.candidate.wallet=scope.daemon_wallet;profile.candidate.connection.credential={Authentication::SecureStore,"os-item-public-fixture"};Check(Take(store->SaveProfile(profile))==1);profile.revision=1;Check(Take(store->LoadProfile(profile.id))==profile);
    auto cookie=profile;cookie.candidate.connection.credential={Authentication::Cookie,"/explicit/disposable-cookie"};Check(store->SaveProfile(cookie).error==s::Error::Invalid);Check(Take(store->LoadProfile(profile.id))==profile);
    auto malformed=profile;malformed.candidate.connection.credential.reference=std::string(1,char(255));Check(store->SaveProfile(malformed).error==s::Error::Invalid);
    auto malformed_provenance=p;malformed_provenance.context.backend=*BackendSessionId::Make(std::string(1,char(255)));Check(store->SaveObservation(scope,"balance",malformed_provenance,{}).error==s::Error::Invalid);
    bool denied{};std::thread foreign([&]{denied=store->Load(scope).error==s::Error::WrongThread && store->Save(scope,m).error==s::Error::WrongThread;});foreign.join();Check(denied);
    Check(Take(store->LoadRecentWallets()).empty());
    for(int i=0;i<20;++i)Take(store->RememberWallet(Scope("backend",'1',"recent-"+std::to_string(i))));
    const auto unnamed=Scope("backend",'2',"");
    Take(store->RememberWallet(unnamed));Take(store->RememberWallet(unnamed));
    store.reset();store=Take(s::MetadataStore::Open(path));Check(Take(store->Load(scope))==m && Take(store->LoadWorkspace())==w && Take(store->LoadProfile(profile.id))==profile);Check(Take(store->LoadObservation(scope,"balance")).amount==cache.amount);
    const auto recents=Take(store->LoadRecentWallets());Check(recents.size()==16 && recents.front()==unnamed && recents.back().daemon_wallet=="recent-5");
    Check(store->RememberWallet(Scope("backend",'1',"invalid\nname")).error==s::Error::Invalid && Take(store->LoadRecentWallets())==recents);
    Raw raw(path);Check(raw.Number("SELECT typeof(amount)='integer' FROM drafts WHERE document='a-min'")==1);Check(raw.Number("SELECT typeof(revision)='text' FROM drafts WHERE document='a-min'")==1);Check(raw.Number("SELECT count(*) FROM sqlite_schema WHERE name LIKE '%account%' OR name LIKE '%seed%' OR name LIKE '%private_key%' OR name LIKE '%password%'")==0);
}
int64_t VersionOne(const std::filesystem::path& path){Raw raw(path);raw.Exec("BEGIN IMMEDIATE");for(auto sql:s::schema::V1)raw.Exec(sql);raw.Exec("PRAGMA application_id="+std::to_string(s::schema::ApplicationId));raw.Exec("PRAGMA user_version=1");raw.Exec("INSERT INTO scopes VALUES(1,'backend','"+std::string(64,'1')+"','wallet/札幌',7,'Preserved display')");raw.Exec("INSERT INTO labels VALUES(1,0,'public-address','Preserved label')");raw.Exec("COMMIT");return raw.Number("PRAGMA page_count");}
void Migrations(const std::filesystem::path& path){VersionOne(path);auto store=Take(s::MetadataStore::Open(path));const auto m=Take(store->Load(Scope()));Check(m.revision==7 && m.display_name=="Preserved display" && m.labels[0].text=="Preserved label");Check(store->LoadObservation(Scope(),"new-cache").error==s::Error::NotFound);store.reset();
    {Raw raw(path);Check(raw.Number("PRAGMA user_version")==s::schema::Version);Check(raw.Number("PRAGMA application_id")==s::schema::ApplicationId);Check(raw.Number("SELECT count(*) FROM observations")==0);Check(raw.Number("SELECT count(*) FROM recovery")==0);}
    store=Take(s::MetadataStore::Open(path));Take(store->SaveObservation(Scope(),"existing-cache",ProvenanceFor(Scope()),Money::Satoshis(9)));store.reset();
    {Raw raw(path);raw.Exec("DROP TABLE wallet_icons");raw.Exec("DROP TABLE desktop_settings");raw.Exec("DROP TABLE general_settings");raw.Exec("DROP TABLE broadcast_details");raw.Exec("DROP TABLE broadcast_attempts");raw.Exec("DROP TABLE rescan_state");raw.Exec("DROP TABLE receive_addresses");raw.Exec("DROP TABLE receive_state");raw.Exec("DROP TABLE recent_wallets");raw.Exec("DROP TABLE recovery");raw.Exec("PRAGMA user_version=2");}
    store=Take(s::MetadataStore::Open(path));Check(Take(store->Load(Scope()))==m);Check(Take(store->LoadObservation(Scope(),"existing-cache")).amount==Money::Satoshis(9));Check(!Take(store->LoadRecovery()).enabled);store.reset();Raw raw(path);Check(raw.Number("PRAGMA user_version")==s::schema::Version);
}
void ReceiveReservations(const std::filesystem::path& path)
{
    auto store = Take(s::MetadataStore::Open(path));
    const s::Scope scope{"backend",{*Hash256::Parse("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")},""};
    auto other = scope;
    other.daemon_wallet = "another-wallet";
    const auto preview = protocol::PreviewPublicWallet(protocol::PublicImportKind::Descriptor,
        "wpkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)", "regtest");
    Check(preview.preview.has_value());
    const auto& descriptor = preview.preview->descriptors.front();
    const ReceiveTarget receive{descriptor.address,descriptor.script,descriptor.descriptor,"m/0/1","regtest",{},1700000000000};
    const auto ticket = Take(store->BeginReceive(scope,true));
    auto second = Take(s::MetadataStore::Open(path));
    Check(second->BeginReceive(scope,false).error == s::Error::Conflict);
    Check(second->FinishReceive(other,ticket,receive).error == s::Error::Conflict);
    Check(second->FinishReceive(scope,ticket+1,receive).error == s::Error::Conflict);
    auto forged = receive;
    forged.chain = "test";
    Check(second->FinishReceive(scope,ticket,forged).error == s::Error::Invalid);
    Check(Take(second->LoadReceivePool(scope)).pending == ticket);
    Check(Take(second->LoadReceivePool(other)).pending == 0);
    second.reset();
    store.reset();
    store = Take(s::MetadataStore::Open(path));
    Check(Take(store->LoadReceivePool(scope)).pending == ticket);
    Check(store->BeginReceive(scope,true).error == s::Error::Conflict);
    Take(store->FinishReceive(scope,ticket,receive));
    Check(Take(store->LoadReceivePool(scope)).available == 1);
    Check(store->ConsumeReceive(other).error == s::Error::NotFound);

    auto protected_store = Take(store->Protect("disposable-fixture-password"));
    store.reset();
    Check(!std::filesystem::exists(path) && protected_store->Protected());
    Check(Take(protected_store->ConsumeReceive(scope)) == receive);
    protected_store.reset();
    protected_store = Take(s::MetadataStore::OpenProtected(path,"disposable-fixture-password"));
    Check(Take(protected_store->LoadReceivePool(scope)).available == 0);
    Check(protected_store->ConsumeReceive(scope).error == s::Error::NotFound);
    const auto newer = Take(protected_store->BeginReceive(scope,true));
    Check(newer > ticket && protected_store->FinishReceive(scope,newer,receive).error == s::Error::Conflict);
    Check(Take(protected_store->LoadReceivePool(scope)).pending == newer);
    Take(protected_store->FinishReceive(scope,newer,{}));
    Check(Take(protected_store->LoadReceivePool(scope)).pending == 0);
}
void RescanTickets(const std::filesystem::path& path)
{
    auto store = Take(s::MetadataStore::Open(path));
    const s::Scope first{"backend",{*Hash256::Parse("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")},""};
    auto second = first;
    second.daemon_wallet="another-wallet";
    store.reset();
    {
        Raw raw(path);
        raw.Exec("DROP TABLE wallet_icons");
        raw.Exec("DROP TABLE desktop_settings");raw.Exec("DROP TABLE general_settings");
        raw.Exec("DROP TABLE broadcast_details");
        raw.Exec("DROP TABLE broadcast_attempts");
        raw.Exec("DROP TABLE rescan_state");
        raw.Exec("PRAGMA user_version=5");
    }
    store=Take(s::MetadataStore::Open(path));
    Check(!Take(store->LoadRescan(first)) && !Take(store->LoadRescan(second)));
    Check(store->BeginRescan(first,{8,6}).error==s::Error::Invalid);
    const auto pending=PendingRescan{Take(store->BeginRescan(first,{6,8})),{6,8}};
    auto concurrent=Take(s::MetadataStore::Open(path));
    Check(concurrent->BeginRescan(first,{6,8}).error==s::Error::Conflict);
    Check(!Take(concurrent->LoadRescan(second)));
    Check(concurrent->FinishRescan(second,pending.ticket).error==s::Error::Conflict);
    Check(concurrent->FinishRescan(first,pending.ticket+1).error==s::Error::Conflict);
    Check(Take(concurrent->LoadRescan(first))==pending);
    const auto other=Take(concurrent->BeginRescan(second,{1,2}));
    Take(concurrent->FinishRescan(second,other));
    Check(Take(concurrent->LoadRescan(first))==pending);
    concurrent.reset();
    store.reset();
    store=Take(s::MetadataStore::Open(path));
    Check(Take(store->LoadRescan(first))==pending);
    auto protected_store=Take(store->Protect("disposable-fixture-password"));
    store.reset();
    protected_store.reset();
    protected_store=Take(s::MetadataStore::OpenProtected(path,"disposable-fixture-password"));
    Check(Take(protected_store->LoadRescan(first))==pending);
    Take(protected_store->FinishRescan(first,pending.ticket));
    const auto newer=Take(protected_store->BeginRescan(first,{6,8}));
    Check(newer>pending.ticket);
    Check(protected_store->FinishRescan(first,pending.ticket).error==s::Error::Conflict);
    Take(protected_store->FinishRescan(first,newer));
    Check(!Take(protected_store->LoadRescan(first)) && !Take(protected_store->LoadRescan(second)));
}
// Intercept only the sync/close methods on the real pinned SQLite VFS. Other
// methods use their original file allocation and implementation. No product hook.
struct FaultVfs {
    struct File {const sqlite3_io_methods* original;sqlite3_io_methods methods;};
    static inline FaultVfs* current{};
    sqlite3_vfs* original{sqlite3_vfs_find(nullptr)};sqlite3_vfs vfs{*original};std::map<sqlite3_file*,File> files;int sync_failures{};bool fail{};
    FaultVfs(){current=this;vfs.zName="roots-gui-owned-sync-fault";vfs.xOpen=[](sqlite3_vfs*,const char* name,sqlite3_file* file,int flags,int* out){auto& f=*current;const int rc=f.original->xOpen(f.original,name,file,flags,out);if(rc!=SQLITE_OK)return rc;auto [it,added]=f.files.emplace(file,File{file->pMethods,*file->pMethods});if(!added)return SQLITE_ERROR;it->second.methods.xSync=[](sqlite3_file* file,int flags){auto& f=*current;if(f.fail){f.fail=false;++f.sync_failures;return SQLITE_IOERR_FSYNC;}return f.files.at(file).original->xSync(file,flags);};it->second.methods.xClose=[](sqlite3_file* file){auto& f=*current;const auto* original=f.files.at(file).original;file->pMethods=original;const int rc=original->xClose(file);f.files.erase(file);return rc;};file->pMethods=&it->second.methods;return SQLITE_OK;};Check(sqlite3_vfs_register(&vfs,1)==SQLITE_OK);}
    ~FaultVfs(){sqlite3_vfs_register(original,1);sqlite3_vfs_unregister(&vfs);current=nullptr;}
};
void Rollback(const std::filesystem::path& path){FaultVfs fault;{auto store=Take(s::MetadataStore::Open(path));auto before=Example();Take(store->Save(Scope(),before));before.revision=1;auto after=before;after.display_name="Uncommitted replacement";after.labels.clear();after.freezes.clear();fault.fail=true;const auto result=store->Save(Scope(),after);Check(result.error==s::Error::Io && fault.sync_failures==1 && !fault.fail);Check(Take(store->Load(Scope()))==before);store.reset();store=Take(s::MetadataStore::Open(path));Check(Take(store->Load(Scope()))==before);Check(Take(store->Save(Scope(),after))==2);}Check(fault.files.empty());}
struct DiskFullBounds {int64_t page_size, seeded_pages, legacy_pages;};
DiskFullBounds DiskFull(const std::filesystem::path& path)
{
    const auto scope=Scope();
    auto before=Example();
    {
        auto store=Take(s::MetadataStore::Open(path));
        before.revision=Take(store->Save(scope,before));
        Check(before.revision==1);
    }
    const auto [page_size,seeded_pages]=[&] {
        Raw raw(path);
        Check(raw.Number("PRAGMA user_version")==s::schema::Version);
        return std::pair{raw.Number("PRAGMA page_size"),raw.Number("PRAGMA page_count")};
    }();
    Check(page_size>0 && seeded_pages>0 && seeded_pages<s::OpenOptions{}.max_pages);
    // Use the committed current schema as the floor and admit no extra pages.
    const s::OpenOptions bounded{50,int(seeded_pages)};
    auto large=before;
    large.display_name="Must roll back";
    large.drafts.clear();
    for (int i=0;i<32;++i) {
        large.drafts.push_back({"draft-"+std::to_string(i),{1},std::string(65536,'x'),Money::Satoshis(i)});
    }
    {
        auto store=Take(s::MetadataStore::Open(path,bounded));
        Check(Take(store->Load(scope))==before);
        Check(store->Save(scope,large).error==s::Error::Full);
        Check(Take(store->Load(scope))==before);
    }
    {
        Raw raw(path);
        Check(raw.Number("PRAGMA page_count")==seeded_pages);
    }
    {
        auto store=Take(s::MetadataStore::Open(path,bounded));
        Check(Take(store->Load(scope))==before);
    }
    const auto migration_path=path.parent_path()/"migration-full.sqlite";
    const auto legacy_pages=VersionOne(migration_path);
    Check(legacy_pages>0 && legacy_pages<seeded_pages);
    const auto failed=s::MetadataStore::Open(migration_path,{50,int(legacy_pages)});
    Check(failed.error==s::Error::Full);
    {
        Raw raw(migration_path);
        Check(raw.Number("PRAGMA user_version")==1);
        Check(raw.Number("PRAGMA page_count")==legacy_pages);
        Check(raw.Number("SELECT revision FROM scopes")==7);
        Check(raw.Number("SELECT count(*) FROM sqlite_schema WHERE name='observations'")==0);
    }
    auto recovered=Take(s::MetadataStore::Open(migration_path));
    const auto migrated=Take(recovered->Load(scope));
    Check(migrated.revision==7 && migrated.display_name=="Preserved display"
        && migrated.labels.size()==1 && migrated.labels[0].text=="Preserved label");
    return {page_size,seeded_pages,legacy_pages};
}
std::string Bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void Corruption(const std::filesystem::path& path){
    {std::ofstream f(path,std::ios::binary);f<<std::string(512,'x');}std::filesystem::permissions(path,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write);auto bytes=Bytes(path);Check(s::MetadataStore::Open(path).error==s::Error::Corrupt);Check(Bytes(path)==bytes);
    const auto foreign=path.parent_path()/"foreign.sqlite";{Raw raw(foreign);raw.Exec("CREATE TABLE unrelated(data TEXT)");raw.Exec("INSERT INTO unrelated VALUES('preserve')");}bytes=Bytes(foreign);Check(s::MetadataStore::Open(foreign).error==s::Error::Schema);Check(Bytes(foreign)==bytes);
    const auto future=path.parent_path()/"future.sqlite";VersionOne(future);{Raw raw(future);raw.Exec("PRAGMA user_version=99");}bytes=Bytes(future);Check(s::MetadataStore::Open(future).error==s::Error::Schema);Check(Bytes(future)==bytes);
    const auto link=path.parent_path()/"link.sqlite";std::filesystem::create_symlink(future.filename(),link);Check(s::MetadataStore::Open(link).error==s::Error::Io);Check(Bytes(future)==bytes);
    Check(s::MetadataStore::Open("file:unexpected?mode=memory").error==s::Error::Invalid);
    auto entries=[&]{std::set<std::string> names;for(const auto& e:std::filesystem::directory_iterator(path.parent_path()))names.insert(e.path().filename().string());return names;};
    const auto before=entries();
    for(const auto& invalid:{std::filesystem::path("file:unexpected?mode=memory"),path.parent_path()/std::filesystem::path(std::string("invalid\0path",12)),path.parent_path()/std::string(4097,'x')}){
        Check(s::MetadataStore::Open(invalid).error==s::Error::Invalid);
        Check(s::MetadataStore::OpenProtected(invalid,"unused-fixture-password").error==s::Error::Invalid);
        Check(entries()==before); // reject before any guard/store pathname is opened
    }
}
void SchemaCollision(const std::filesystem::path& path){
    const std::pair<const char*,const char*> objects[]{
        {"trigger","CREATE TRIGGER sqliteXunexpected AFTER UPDATE OF display_name ON scopes BEGIN UPDATE scopes SET display_name='unexpected-trigger-write' WHERE id=new.id; END"},
        {"trigger","CREATE TRIGGER sqliteXunexpected AFTER INSERT ON observations BEGIN UPDATE scopes SET display_name='unexpected-cache-write' WHERE id=new.scope; END"},
        {"table","CREATE TABLE sqliteXunexpected(data TEXT)"},
        {"index","CREATE INDEX sqliteXunexpected ON scopes(display_name)"},
        {"view","CREATE VIEW sqliteXunexpected AS SELECT display_name FROM scopes"}
    };
    int index{};
    for(const auto& [type,sql]:objects){
        const auto file=path.parent_path()/("collision-"+std::to_string(index++)+".sqlite");auto before=Example();
        {auto store=Take(s::MetadataStore::Open(file));Take(store->Save(Scope(),before));before.revision=1;Take(store->SaveObservation(Scope(),"balance",ProvenanceFor(Scope()),Money::Satoshis(7)));}
        {Raw raw(file);raw.Exec("ANALYZE");Check(raw.Number("SELECT count(*) FROM sqlite_schema WHERE name='sqlite_stat1'")==1);Check(raw.Number("SELECT count(*) FROM sqlite_schema WHERE name GLOB 'sqlite_autoindex_*'")>0);}
        {auto store=Take(s::MetadataStore::Open(file));Check(Take(store->Load(Scope()))==before);}
        {Raw raw(file);raw.Exec(sql);}
        const auto bytes=Bytes(file);
        {auto rejected=s::MetadataStore::Open(file);Check(rejected.error==s::Error::Schema && !rejected.value);}
        Check(Bytes(file)==bytes);
        {Raw raw(file);Check(raw.Number("SELECT revision=1 AND display_name='Public vault ## literal' FROM scopes")==1);raw.Exec(std::string("DROP ")+type+" sqliteXunexpected");}
        {auto store=Take(s::MetadataStore::Open(file));Check(Take(store->Load(Scope()))==before);Check(Take(store->LoadObservation(Scope(),"balance")).amount==Money::Satoshis(7));}
    }
}
void BroadcastAttempts(const std::filesystem::path& path)
{
    auto store=Take(s::MetadataStore::Open(path));
    auto previous=Example();
    previous.revision=Take(store->Save(Scope(),previous));
    store.reset();
    {
        Raw raw(path);
        raw.Exec("DROP TABLE wallet_icons");
        raw.Exec("DROP TABLE desktop_settings");raw.Exec("DROP TABLE general_settings");
        raw.Exec("DROP TABLE broadcast_details");
        raw.Exec("DROP TABLE broadcast_attempts");
        raw.Exec("PRAGMA user_version=6");
    }
    store=Take(s::MetadataStore::Open(path));
    Check(Take(store->Load(Scope()))==previous);
    const auto digest=[](char c){return *Hash256::Parse(std::string(64,c));};
    const s::BroadcastAttempt first{{digest('1')},digest('a'),digest('b'),digest('c'),1700000000000};
    auto alias=first;
    alias.wtxid=digest('d');
    alias.wire_digest=digest('e');
    Check(!Take(store->HasBroadcastAttempt(first.network,first.txid)));
    Take(store->RecordBroadcastAttempt(first));
    auto separate=Take(s::MetadataStore::Open(path));
    Check(Take(separate->HasBroadcastAttempt(first.network,first.txid)));
    Check(separate->RecordBroadcastAttempt(alias).error==s::Error::Conflict);
    alias.network.genesis=digest('2');
    Check(!Take(separate->HasBroadcastAttempt(alias.network,alias.txid)));
    Take(separate->RecordBroadcastAttempt(alias));
    auto invalid=first;
    invalid.observed_ms=0;
    invalid.txid=digest('3');
    Check(store->RecordBroadcastAttempt(invalid).error==s::Error::Invalid);
    Check(!Take(store->HasBroadcastAttempt(invalid.network,invalid.txid)));
    {
        Raw raw(path);
        Check(raw.Number("SELECT count(*) FROM broadcast_attempts")==2);
        Check(raw.Number("SELECT count(*) FROM broadcast_attempts WHERE genesis LIKE '1%' AND wire_digest LIKE 'c%' AND wtxid LIKE 'b%'")==1);
    }
    separate.reset();
    auto protected_store=Take(store->Protect("disposable-fixture-password"));
    store.reset();
    Check(!std::filesystem::exists(path) && Take(protected_store->HasBroadcastAttempt(first.network,first.txid)));
    auto later=first;
    later.txid=digest('f');
    Take(protected_store->RecordBroadcastAttempt(later));
    protected_store.reset();
    protected_store=Take(s::MetadataStore::OpenProtected(path,"disposable-fixture-password"));
    Check(Take(protected_store->HasBroadcastAttempt(first.network,first.txid))
        && Take(protected_store->HasBroadcastAttempt(alias.network,alias.txid))
        && Take(protected_store->HasBroadcastAttempt(later.network,later.txid)));
    Check(protected_store->RecordBroadcastAttempt(later).error==s::Error::Conflict);
}
void BroadcastRecovery(const std::filesystem::path& path)
{
    using Outcome=s::BroadcastOutcome;
    const auto txid=*Hash256::Parse("e5c502b049318a05d97fd5765758d878c84c1e747744b852b4d0badbd9b51586");
    const auto digest=*Hash256::Parse("fb07b331126f5ce4d26e3e5b82f471b63cc106e6aee1fdebb665af8c5b64614a");
    const NetworkIdentity regtest{*Hash256::Parse("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")};
    const std::string wire="0200000001"+std::string(64,'1')+"0000000000fdffffff010100000000000000016a00000000";
    const s::BroadcastAttempt first{regtest,txid,txid,digest,1700000000000};
    auto store=Take(s::MetadataStore::Open(path));
    Take(store->RecordBroadcastAttempt(first));
    store.reset();
    {
        Raw raw(path);
        raw.Exec("DROP TABLE wallet_icons");
        raw.Exec("DROP TABLE desktop_settings");raw.Exec("DROP TABLE general_settings");
        raw.Exec("DROP TABLE broadcast_details");
        raw.Exec("PRAGMA user_version=7");
    }
    store=Take(s::MetadataStore::Open(path));
    auto legacy=Take(store->LoadBroadcastAttempt(regtest,txid));
    Check(legacy && legacy->serial==0 && legacy->wire_hex.empty() && legacy->outcome==Outcome::Pending);
    s::BroadcastRecord next{first,wire,0,first.observed_ms,Outcome::Pending,{},"verified-backend","watch-only"};
    auto alternate=next;
    alternate.wire_hex.back()='1';
    Check(store->BeginBroadcastAttempt(alternate,0).error==s::Error::Invalid);
    Check(store->BeginBroadcastAttempt(next,{}).error==s::Error::Conflict);
    Check(Take(store->BeginBroadcastAttempt(next,0))==1);
    store.reset();
    store=Take(s::MetadataStore::Open(path));
    const auto pending=Take(store->LoadBroadcastAttempt(regtest,txid));
    Check(pending && pending->serial==1 && pending->outcome==Outcome::Pending && !pending->outcome_ms && pending->wire_hex==wire);
    Take(store->FinishBroadcastAttempt(first,1,Outcome::Uncertain,first.observed_ms+1));
    Check(store->FinishBroadcastAttempt(first,1,Outcome::Acknowledged,first.observed_ms+2).error==s::Error::Conflict);
    auto stale=next;
    stale.identity.wire_digest=txid;
    Check(store->BeginBroadcastAttempt(stale,1).error==s::Error::Invalid);
    next.identity.observed_ms+=2;
    next.attempted_ms=next.identity.observed_ms;
    auto competing=Take(s::MetadataStore::Open(path));
    Check(Take(competing->BeginBroadcastAttempt(next,1))==2);
    Check(store->BeginBroadcastAttempt(next,1).error==s::Error::Conflict);
    Check(store->FinishBroadcastAttempt(first,1,Outcome::Rejected,next.attempted_ms).error==s::Error::Conflict);
    Take(store->FinishBroadcastAttempt(next.identity,2,Outcome::Acknowledged,next.attempted_ms));
    competing.reset();
    auto protected_store=Take(store->Protect("disposable-fixture-password"));
    store.reset();
    Check(!std::filesystem::exists(path));
    protected_store.reset();
    protected_store=Take(s::MetadataStore::OpenProtected(path,"disposable-fixture-password"));
    const auto recent=Take(protected_store->RecentBroadcastAttempts());
    Check(recent.size()==1 && recent.front().serial==2 && recent.front().wire_hex==wire
        && recent.front().outcome==Outcome::Acknowledged && recent.front().identity.observed_ms==first.observed_ms
        && recent.front().attempted_ms==next.attempted_ms && recent.front().outcome_ms==next.attempted_ms);
}
void Actor(const std::filesystem::path& path){
    auto store=Take(s::MetadataStore::Open(path));auto snapshot=Take(store->Load(Scope()));Raw raw(path);std::cout<<"ready\n"<<std::flush;std::string command;
    while(std::getline(std::cin,command)){
        if(command=="save"){snapshot.display_name="Concurrent writer";auto r=store->Save(Scope(),snapshot);if(r)snapshot.revision=*r.value;std::cout<<s::ErrorName(r.error)<<'\n';}
        else if(command=="reload"){snapshot=Take(store->Load(Scope()));std::cout<<snapshot.revision<<'\n';}
        else if(command=="hold"){raw.Exec("BEGIN IMMEDIATE");std::cout<<"held\n";}
        else if(command=="release"){raw.Exec("ROLLBACK");std::cout<<"released\n";}
        else throw std::runtime_error("unknown actor command");std::cout<<std::flush;
    }
    sqlite3_exec(raw.db,"ROLLBACK",nullptr,nullptr,nullptr);
}
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    try{
        const std::string mode=argv[1];const std::filesystem::path path=argv[2];
        if(mode=="actor"){Actor(path);return 0;}
        std::optional<DiskFullBounds> bounds;
        if(mode=="persistence")Persistence(path);else if(mode=="migrations")Migrations(path);else if(mode=="rollback")Rollback(path);else if(mode=="disk-full")bounds=DiskFull(path);else if(mode=="corruption")Corruption(path);else if(mode=="schema-collision")SchemaCollision(path);else if(mode=="receive-reservations")ReceiveReservations(path);else if(mode=="rescan-tickets")RescanTickets(path);else if(mode=="broadcast-attempts")BroadcastAttempts(path);else if(mode=="broadcast-recovery")BroadcastRecovery(path);else return 2;
        std::cout<<"{\"case\":\""<<mode<<"\",\"passed\":true,\"checks\":"<<checks<<",\"sqliteVersion\":\""<<sqlite3_libversion()<<"\",\"openConnectionsSettled\":true";
        if (bounds) {
            std::cout<<",\"pageSize\":"<<bounds->page_size<<",\"seededPagesAndWriteCap\":"<<bounds->seeded_pages
                <<",\"legacyPagesAndMigrationCap\":"<<bounds->legacy_pages;
        }
        std::cout<<"}\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
