// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/storage/metadata.h"
#include "adapters/storage/schema.h"
#include "adapters/storage/protected_file.h"
#include "adapters/rpc/json.h"
#include "adapters/protocol/public_wallet.h"
#include "adapters/protocol/transaction.h"
#include <sqlite3.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <limits>
#include <map>
#include <set>
#include <sodium.h>
#if defined(__unix__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace roots::gui::storage {
namespace {
struct Failure { Error error; };
void Require(bool ok,Error error=Error::Invalid){if(!ok)throw Failure{error};}
void Check(int rc){
    if(rc==SQLITE_OK)return;
    switch(rc&255){
    case SQLITE_BUSY:case SQLITE_LOCKED:throw Failure{Error::Busy};
    case SQLITE_FULL:throw Failure{Error::Full};
    case SQLITE_CORRUPT:case SQLITE_NOTADB:throw Failure{Error::Corrupt};
    case SQLITE_NOMEM:case SQLITE_TOOBIG:throw Failure{Error::Resource};
    case SQLITE_INTERRUPT:throw Failure{Error::Timeout};
    case SQLITE_CONSTRAINT:case SQLITE_SCHEMA:case SQLITE_ERROR:throw Failure{Error::Schema};
    default:throw Failure{Error::Io};
    }
}
void Exec(sqlite3* db,std::string_view sql){Check(sqlite3_exec(db,std::string(sql).c_str(),nullptr,nullptr,nullptr));}
class Statement {
    sqlite3_stmt* stmt{};
public:
    Statement(sqlite3* db,std::string_view sql){Check(sqlite3_prepare_v2(db,sql.data(),int(sql.size()),&stmt,nullptr));}
    ~Statement(){sqlite3_finalize(stmt);}
    Statement(const Statement&)=delete;
    void Bind(int i,std::string_view s){Check(sqlite3_bind_text(stmt,i,s.data(),int(s.size()),SQLITE_TRANSIENT));}
    void Bind(int i,int64_t n){Check(sqlite3_bind_int64(stmt,i,n));}
    void Null(int i){Check(sqlite3_bind_null(stmt,i));}
    bool Row(){const int rc=sqlite3_step(stmt);if(rc==SQLITE_ROW)return true;if(rc==SQLITE_DONE)return false;Check(rc);return false;}
    void Done(){Require(!Row(),Error::Schema);}
    int Type(int i)const{return sqlite3_column_type(stmt,i);}
    int64_t Int(int i)const{Require(Type(i)==SQLITE_INTEGER,Error::Corrupt);return sqlite3_column_int64(stmt,i);}
    std::string Text(int i)const{Require(Type(i)==SQLITE_TEXT,Error::Corrupt);const auto* p=sqlite3_column_text(stmt,i);const int n=sqlite3_column_bytes(stmt,i);Require(p!=nullptr,Error::Resource);return std::string(reinterpret_cast<const char*>(p),size_t(n));}
    std::optional<Money> Amount(int i)const{return Type(i)==SQLITE_NULL?std::nullopt:std::optional<Money>(Money::Satoshis(Int(i)));}
};
int64_t Scalar(sqlite3* db,std::string_view sql){Statement q(db,sql);Require(q.Row(),Error::Schema);const auto n=q.Int(0);Require(!q.Row(),Error::Schema);return n;}
class Transaction {
    sqlite3* db;bool committed{};
public:
    Transaction(sqlite3* d,bool write):db(d){Exec(db,write?"BEGIN IMMEDIATE":"BEGIN");}
    ~Transaction(){if(!committed)sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);}
    void Commit(){Exec(db,"COMMIT");committed=true;}
};
bool Text(std::string_view s,size_t max,bool empty=true,bool lines=false){return (empty || !s.empty()) && s.size()<=max && rpc::ValidUtf8(s) && std::none_of(s.begin(),s.end(),[&](unsigned char c){return c==127 || (c<32 && !(lines && (c=='\n' || c=='\t')));});}
bool Id(std::string_view s){return Text(s,256,false);}
bool Valid(const Scope& s){return Id(s.backend) && Text(s.daemon_wallet,1024) && Hash256::Parse(s.network.genesis.hex()).has_value();}
bool Valid(const Target& t){
    if(t.kind==TargetKind::Address)return Text(t.key,512,false);
    if(t.kind==TargetKind::Transaction){const auto hash=Hash256::Parse(t.key);return hash && hash->hex()==t.key;}
    if(t.kind!=TargetKind::Output || t.key.size()<66 || t.key[64]!=':')return false;
    const auto hash=Hash256::Parse(t.key.substr(0,64));if(!hash || hash->hex()!=t.key.substr(0,64))return false;
    uint32_t index{};const auto n=std::string_view(t.key).substr(65);const auto p=std::from_chars(n.data(),n.data()+n.size(),index);
    return p.ec==std::errc{} && p.ptr==n.data()+n.size() && std::to_string(index)==n;
}
void Validate(const Metadata& m){
    Require(m.revision<uint64_t(std::numeric_limits<int64_t>::max()) && Text(m.display_name,1024));
    Require(!m.display_icon || (*m.display_icon>=0 && *m.display_icon<=2));
    Require(m.labels.size()<=4096 && m.layouts.size()<=64 && m.widths.size()<=1024 && m.freezes.size()<=4096 && m.drafts.size()<=64);
    std::set<std::pair<int,std::string>> labels,freezes;std::set<std::string> tables,drafts;std::set<std::pair<std::string,std::string>> widths;
    for(const auto& l:m.labels)Require(Valid(l.target) && Text(l.text,4096,true,true) && labels.emplace(int(l.target.kind),l.target.key).second);
    for(const auto& f:m.freezes)Require(Valid(f) && f.kind!=TargetKind::Transaction && freezes.emplace(int(f.kind),f.key).second);
    for(const auto& t:m.layouts)Require(Id(t.table) && Id(t.sort_column) && tables.insert(t.table).second);
    for(const auto& c:m.widths)Require(tables.contains(c.table) && Id(c.column) && c.width>=16 && c.width<=8192 && widths.emplace(c.table,c.column).second);
    for(const auto& d:m.drafts)Require(Id(d.document) && Text(d.note,65536,true,true) && drafts.insert(d.document).second);
}
uint64_t Counter(int64_t n){Require(n>=0,Error::Corrupt);return uint64_t(n);}
uint64_t Unsigned(std::string_view s){uint64_t n{};const auto p=std::from_chars(s.data(),s.data()+s.size(),n);Require(p.ec==std::errc{} && p.ptr==s.data()+s.size() && std::to_string(n)==s,Error::Corrupt);return n;}
bool Bool(int64_t n){Require(n==0 || n==1,Error::Corrupt);return n==1;}
struct ScopeRow {int64_t id{};uint64_t revision{};std::string display;};
ScopeRow FindScope(sqlite3* db,const Scope& s,bool create){
    Require(Valid(s));
    if(create){Statement q(db,"INSERT INTO scopes(backend,genesis,wallet,revision,display_name) VALUES(?,?,?,0,'') ON CONFLICT(backend,genesis,wallet) DO NOTHING");q.Bind(1,s.backend);q.Bind(2,s.network.genesis.hex());q.Bind(3,s.daemon_wallet);q.Done();}
    Statement q(db,"SELECT id,revision,display_name FROM scopes WHERE backend=? AND genesis=? AND wallet=?");q.Bind(1,s.backend);q.Bind(2,s.network.genesis.hex());q.Bind(3,s.daemon_wallet);
    if(!q.Row())return {};ScopeRow r{q.Int(0),Counter(q.Int(1)),q.Text(2)};Require(r.id>0 && Text(r.display,1024),Error::Corrupt);Require(!q.Row(),Error::Corrupt);return r;
}
void ValidateSchema(sqlite3* db,int version){
    std::set<std::string> expected;for(auto sql:schema::V1)expected.emplace(sql);if(version>=2)expected.emplace(schema::V2);if(version>=3)expected.emplace(schema::V3);if(version>=4)expected.emplace(schema::V4);if(version>=5)for(auto sql:schema::V5)expected.emplace(sql);if(version>=6)expected.emplace(schema::V6);if(version>=7)expected.emplace(schema::V7);if(version>=8)expected.emplace(schema::V8);
    if(version>=9)expected.emplace(schema::V9);
    if(version>=10)expected.emplace(schema::V10);
    if(version>=11)expected.emplace(schema::V11);
    // GLOB keeps '_' literal: sqliteX... is an ordinary user object and must
    // reach the exact-schema check, unlike SQLite's reserved sqlite_ objects.
    Statement q(db,"SELECT type,sql FROM sqlite_schema WHERE name NOT GLOB 'sqlite_*'");
    while(q.Row()){Require(q.Text(0)=="table" && expected.erase(q.Text(1))==1,Error::Schema);}
    Require(expected.empty(),Error::Schema);
}
void PrivateFile(const std::filesystem::path& path){
    Require(path.is_absolute() && Text(path.string(),4096,false));
#if defined(__unix__)
    struct stat parent{};Require(lstat(path.parent_path().c_str(),&parent)==0 && S_ISDIR(parent.st_mode) && parent.st_uid==getuid() && (parent.st_mode&0077)==0,Error::Io);
    const int fd=open(path.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);Require(fd>=0,Error::Io);
    struct stat file{};const bool ok=fstat(fd,&file)==0 && S_ISREG(file.st_mode) && file.st_uid==getuid() && (file.st_mode&0077)==0;close(fd);Require(ok,Error::Io);
#else
    throw Failure{Error::Unsupported}; // native private-file qualification required
#endif
}
struct Budget {
    sqlite3* db;
    std::chrono::steady_clock::time_point end{std::chrono::steady_clock::now()+std::chrono::seconds(2)};
    explicit Budget(sqlite3* d):db(d){sqlite3_progress_handler(db,1000,[](void* p){return int(std::chrono::steady_clock::now()>static_cast<Budget*>(p)->end);},this);}
    ~Budget(){sqlite3_progress_handler(db,0,nullptr,nullptr);}
};
void BindAmount(Statement& q,int i,std::optional<Money> value){if(value)q.Bind(i,value->sats());else q.Null(i);}
Target ReadTarget(const Statement& q){const auto kind=q.Int(0);Require(kind>=0 && kind<=2,Error::Corrupt);return {TargetKind(kind),q.Text(1)};}
void Initialize(sqlite3* db,OpenOptions options){
    Require(options.busy_ms>=0 && options.busy_ms<=1000 && options.max_pages>=1 && options.max_pages<=65536);
    Budget budget(db);Check(sqlite3_extended_result_codes(db,1));Check(sqlite3_busy_timeout(db,options.busy_ms));
    for(const auto [option,value]:{std::pair{SQLITE_DBCONFIG_DEFENSIVE,1},{SQLITE_DBCONFIG_TRUSTED_SCHEMA,0},{SQLITE_DBCONFIG_DQS_DDL,0},{SQLITE_DBCONFIG_DQS_DML,0},{SQLITE_DBCONFIG_ENABLE_ATTACH_CREATE,0},{SQLITE_DBCONFIG_ENABLE_ATTACH_WRITE,0}})Check(sqlite3_db_config(db,option,value,nullptr));
    sqlite3_limit(db,SQLITE_LIMIT_LENGTH,1024*1024);sqlite3_limit(db,SQLITE_LIMIT_SQL_LENGTH,65536);sqlite3_limit(db,SQLITE_LIMIT_ATTACHED,0);
    {Statement q(db,"PRAGMA quick_check(1)");Require(q.Row() && q.Text(0)=="ok" && !q.Row(),Error::Corrupt);}
    const auto app=Scalar(db,"PRAGMA application_id"),version=Scalar(db,"PRAGMA user_version");
    Require((app==schema::ApplicationId && version>=1 && version<=schema::Version) || (app==0 && version==0 && Scalar(db,"SELECT count(*) FROM sqlite_schema")==0),Error::Schema);
    Exec(db,"PRAGMA foreign_keys=ON");Exec(db,"PRAGMA journal_mode=DELETE");Exec(db,"PRAGMA synchronous=EXTRA");Exec(db,"PRAGMA temp_store=MEMORY");Exec(db,"PRAGMA secure_delete=ON");
    Exec(db,"PRAGMA max_page_count="+std::to_string(options.max_pages));Transaction tx(db,true);auto v=Scalar(db,"PRAGMA user_version");
    if(v==0){Require(Scalar(db,"PRAGMA application_id")==0 && Scalar(db,"SELECT count(*) FROM sqlite_schema")==0,Error::Schema);for(auto sql:schema::V1)Exec(db,sql);Exec(db,"PRAGMA application_id="+std::to_string(schema::ApplicationId));Exec(db,"PRAGMA user_version=1");v=1;}
    Require(Scalar(db,"PRAGMA application_id")==schema::ApplicationId && v>=1 && v<=schema::Version,Error::Schema);ValidateSchema(db,int(v));
    if(v==1){Exec(db,schema::V2);Exec(db,"PRAGMA user_version=2");v=2;}
    if(v==2){Exec(db,schema::V3);Exec(db,"PRAGMA user_version=3");v=3;}
    if(v==3){Exec(db,schema::V4);Exec(db,"PRAGMA user_version=4");v=4;}
    if(v==4){for(auto sql:schema::V5)Exec(db,sql);Exec(db,"PRAGMA user_version=5");v=5;}
    if(v==5){Exec(db,schema::V6);Exec(db,"PRAGMA user_version=6");v=6;}
    if(v==6){Exec(db,schema::V7);Exec(db,"PRAGMA user_version=7");v=7;}
    if(v==7){Exec(db,schema::V8);Exec(db,"PRAGMA user_version=8");v=8;}
    if(v==8){Exec(db,schema::V9);Exec(db,"PRAGMA user_version=9");v=9;}
    if(v==9){Exec(db,schema::V10);Exec(db,"PRAGMA user_version=10");v=10;}
    if(v==10){Exec(db,schema::V11);Exec(db,"PRAGMA user_version=11");}
    ValidateSchema(db,schema::Version);
    {Statement q(db,"PRAGMA foreign_key_check");Require(!q.Row(),Error::Corrupt);}tx.Commit();
}
CleansedBytes Serialize(sqlite3* db){sqlite3_int64 size{};auto* memory=sqlite3_serialize(db,"main",&size,0);if(!memory || size<=0 || uint64_t(size)>ProtectedFile::MaxBytes){if(memory){if(size>0)sodium_memzero(memory,size);sqlite3_free(memory);}throw Failure{Error::Resource};}CleansedBytes result;try{result.bytes.assign(memory,memory+size);}catch(...){sodium_memzero(memory,size);sqlite3_free(memory);throw;}sodium_memzero(memory,size);sqlite3_free(memory);return result;}
void Deserialize(sqlite3* db,std::span<const unsigned char> bytes){Require(!bytes.empty() && bytes.size()<=ProtectedFile::MaxBytes,Error::Resource);auto* memory=static_cast<unsigned char*>(sqlite3_malloc64(bytes.size()+32));Require(memory,Error::Resource);std::copy(bytes.begin(),bytes.end(),memory);std::fill_n(memory+bytes.size(),32,0);Check(sqlite3_deserialize(db,"main",memory,sqlite3_int64(bytes.size()),sqlite3_int64(bytes.size()+32),SQLITE_DESERIALIZE_FREEONCLOSE|SQLITE_DESERIALIZE_RESIZEABLE));/* FREEONCLOSE also frees the buffer on failure. */}
}
bool ValidMetadata(const Scope& scope, const Metadata& metadata)
{
    try { Validate(metadata); return Valid(scope); }
    catch (...) { return false; }
}
const char* ErrorName(Error e){switch(e){case Error::None:return "success";case Error::Invalid:return "invalid-metadata";case Error::Conflict:return "metadata-conflict";case Error::Busy:return "store-busy";case Error::Full:return "store-full";case Error::Corrupt:return "store-corrupt";case Error::Schema:return "unsupported-store-schema";case Error::Resource:return "store-resource-limit";case Error::Timeout:return "store-timeout";case Error::WrongThread:return "store-owner-thread";case Error::Unsupported:return "private-store-unavailable";case Error::NotFound:return "not-found";case Error::Authentication:return "password-or-protected-data-invalid";case Error::ConversionConflict:return "interrupted-conversion-conflict";case Error::ConversionBackupConflict:return "conversion-backup-conflict";default:return "store-io";}}
template<class F> auto MetadataStore::Run(F&& fn)->Result<std::invoke_result_t<F>>{
    if(std::this_thread::get_id()!=owner)return {{},Error::WrongThread};
    try{Budget budget(db);return {fn(),Error::None};}catch(Failure e){return {{},e.error};}catch(const std::bad_alloc&){return {{},Error::Resource};}catch(...){return {{},Error::Io};}
}
MetadataStore::~MetadataStore(){sqlite3_close_v2(db);}
template<class F> auto MetadataStore::Write(F&& fn)->Result<std::invoke_result_t<F>>{
    if(std::this_thread::get_id()!=owner)return {{},Error::WrongThread};
    if(migration_blocked){if(protection || !file_guard || !file_guard->Exclusive())return {{},Error::Busy};file_guard->Shared();migration_blocked=false;}
    if(!protection){try{auto protected_path=file_path;protected_path += ".protected";if(std::filesystem::exists(protected_path))return {{},Error::Busy};}catch(...){return {{},Error::Io};}return Run(std::forward<F>(fn));}
    try{auto before=Serialize(db);auto result=Run(std::forward<F>(fn));if(!result)return result;try{auto after=Serialize(db);const auto saved=protection->Save(after.bytes);Require(bool(saved),saved.error);return result;}catch(...){try{Deserialize(db,before.bytes);}catch(...){migration_blocked=true;}throw;}}catch(Failure e){return {{},e.error};}catch(...){return {{},Error::Resource};}
}
Result<std::unique_ptr<MetadataStore>> MetadataStore::Open(const std::filesystem::path& path,OpenOptions options){
    try{
        Require(path.is_absolute() && Text(path.string(),4096,false));
        Require(options.busy_ms>=0 && options.busy_ms<=1000 && options.max_pages>=1 && options.max_pages<=65536);
        auto store=std::unique_ptr<MetadataStore>(new MetadataStore);
        auto guard=FileGuard::Open(path,false);Require(bool(guard),guard.error);store->file_guard=std::move(*guard.value);auto protected_path=path;protected_path += ".protected";Require(!std::filesystem::exists(protected_path),Error::Authentication);PrivateFile(path);store->file_path=path;
        Check(sqlite3_open_v2(path.string().c_str(),&store->db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_NOMUTEX|SQLITE_OPEN_NOFOLLOW|SQLITE_OPEN_PRIVATECACHE,nullptr));
        Initialize(store->db,options);
        return {std::move(store),Error::None};
    }catch(Failure e){return {{},e.error};}catch(const std::bad_alloc&){return {{},Error::Resource};}catch(...){return {{},Error::Io};}
}
Result<std::unique_ptr<MetadataStore>> MetadataStore::OpenProtected(const std::filesystem::path& path,std::string_view password,OpenOptions options,ConversionResolution resolution){
    try {
        Require(path.is_absolute() && Text(path.string(), 4096, false));
        auto store = std::unique_ptr<MetadataStore>(new MetadataStore);
        auto guard = FileGuard::Open(path, true);
        Require(bool(guard), guard.error);
        store->file_guard = std::move(*guard.value);
        store->file_path = path;
        auto encrypted = path;
        encrypted += ".protected";
        auto file = ProtectedFile::Open(encrypted, password, ProtectedFile::MaxBytes, true);
        Require(bool(file), file.error==Error::NotFound ? Error::Invalid : file.error);
        store->protection = std::move(*file.value);
        auto plain = store->protection->Read();
        Require(bool(plain), plain.error);
        Require(!plain.value->bytes.empty(), Error::Corrupt);
        Check(sqlite3_open_v2(":memory:", &store->db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE
                             | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_MEMORY, nullptr));
        Deserialize(store->db, plain.value->bytes);
        const auto before = Scalar(store->db, "PRAGMA user_version");
        Initialize(store->db, options);
        const auto finished = store->protection->FinishConversion(path, resolution);
        Require(bool(finished), finished.error);
        if (before != schema::Version) {
            auto migrated = Serialize(store->db);
            const auto written = store->protection->Save(migrated.bytes);
            Require(bool(written), written.error);
        }
        return {std::move(store), Error::None};
    } catch (Failure e) { return {{}, e.error}; }
    catch (...) { return {{}, Error::Io}; }
}
Result<std::unique_ptr<MetadataStore>> MetadataStore::Protect(std::string_view password){
    if(std::this_thread::get_id()!=owner)return {{},Error::WrongThread};
    try{Require(!protection && file_guard && !migration_blocked,Error::Invalid);if(!file_guard->Exclusive()){migration_blocked=true;return {{},Error::Busy};}auto encrypted=file_path;encrypted += ".protected";Require(!std::filesystem::exists(encrypted),Error::Conflict);auto bytes=Serialize(db);auto file=ProtectedFile::Open(encrypted,password);Require(bool(file),file.error);auto result=std::unique_ptr<MetadataStore>(new MetadataStore);result->file_path=file_path;result->protection=std::move(*file.value);Check(sqlite3_open_v2(":memory:",&result->db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_NOMUTEX|SQLITE_OPEN_MEMORY,nullptr));Deserialize(result->db,bytes.bytes);Initialize(result->db,{});auto written=result->protection->Save(bytes.bytes);Require(bool(written),written.error);auto verified=result->protection->Read();Require(verified && verified.value->bytes==bytes.bytes,Error::Authentication);Require(std::filesystem::remove(file_path),Error::Io);result->file_guard=std::move(file_guard);migration_blocked=true;return {std::move(result),Error::None};}catch(Failure e){return {{},e.error};}catch(...){return {{},Error::Io};}
}
Result<std::vector<unsigned char>> MetadataStore::SealMemory(std::string_view text){if(std::this_thread::get_id()!=owner)return {{},Error::WrongThread};if(!protection)return {{},Error::Invalid};return protection->Seal({reinterpret_cast<const unsigned char*>(text.data()),text.size()});}
Result<std::string> MetadataStore::UnsealMemory(std::span<const unsigned char> bytes){if(std::this_thread::get_id()!=owner)return {{},Error::WrongThread};if(!protection)return {{},Error::Invalid};auto result=protection->Unseal(bytes);if(!result)return {{},result.error};return {std::string(reinterpret_cast<const char*>(result.value->bytes.data()),result.value->bytes.size()),Error::None};}
Result<Metadata> MetadataStore::Load(const Scope& scope){return Run([&]{
    Transaction tx(db,false);const auto row=FindScope(db,scope,false);Metadata m;m.revision=row.revision;m.display_name=row.display;
    if(row.id){
        {Statement q(db,"SELECT icon FROM wallet_icons WHERE scope=?");q.Bind(1,row.id);if(q.Row()){const auto icon=q.Int(0);Require(icon>=0 && icon<=2,Error::Corrupt);m.display_icon=int(icon);}}
        {Statement q(db,"SELECT kind,target,label FROM labels WHERE scope=? ORDER BY kind,target");q.Bind(1,row.id);while(q.Row()){Require(m.labels.size()<4096,Error::Resource);m.labels.push_back({ReadTarget(q),q.Text(2)});}}
        {Statement q(db,"SELECT table_id,sort_column,descending FROM layouts WHERE scope=? ORDER BY table_id");q.Bind(1,row.id);while(q.Row()){Require(m.layouts.size()<64,Error::Resource);m.layouts.push_back({q.Text(0),q.Text(1),Bool(q.Int(2))});}}
        {Statement q(db,"SELECT table_id,column_id,width FROM widths WHERE scope=? ORDER BY table_id,column_id");q.Bind(1,row.id);while(q.Row()){Require(m.widths.size()<1024 && q.Int(2)>=16 && q.Int(2)<=8192,Error::Corrupt);m.widths.push_back({q.Text(0),q.Text(1),int(q.Int(2))});}}
        {Statement q(db,"SELECT kind,target FROM freezes WHERE scope=? ORDER BY kind,target");q.Bind(1,row.id);while(q.Row()){Require(m.freezes.size()<4096,Error::Resource);m.freezes.push_back(ReadTarget(q));}}
        {Statement q(db,"SELECT document,revision,note,amount FROM drafts WHERE scope=? ORDER BY document");q.Bind(1,row.id);while(q.Row()){Require(m.drafts.size()<64,Error::Resource);m.drafts.push_back({q.Text(0),{Unsigned(q.Text(1))},q.Text(2),q.Amount(3)});}}
    }
    // Exhausted counters can still be inspected, but never advanced/wrapped.
    auto valid=m;valid.revision=0;Validate(valid);tx.Commit();return m;
});}
Result<uint64_t> MetadataStore::Save(const Scope& scope,const Metadata& m){return Write([&]{
    Validate(m);Transaction tx(db,true);const auto row=FindScope(db,scope,true);Require(row.revision==m.revision,Error::Conflict);
    {Statement q(db,"UPDATE scopes SET revision=?,display_name=? WHERE id=?");q.Bind(1,int64_t(m.revision+1));q.Bind(2,m.display_name);q.Bind(3,row.id);q.Done();}
    if(m.display_icon){
        Statement q(db,"INSERT INTO wallet_icons VALUES(?,?) ON CONFLICT(scope) DO UPDATE SET icon=excluded.icon");
        q.Bind(1,row.id);q.Bind(2,int64_t(*m.display_icon));q.Done();
    }else{
        Statement q(db,"DELETE FROM wallet_icons WHERE scope=?");q.Bind(1,row.id);q.Done();
    }
    for(const auto* table:{"widths","layouts","labels","freezes","drafts"}){Statement q(db,std::string("DELETE FROM ")+table+" WHERE scope=?");q.Bind(1,row.id);q.Done();}
    for(const auto& l:m.labels){Statement q(db,"INSERT INTO labels VALUES(?,?,?,?)");q.Bind(1,row.id);q.Bind(2,int64_t(l.target.kind));q.Bind(3,l.target.key);q.Bind(4,l.text);q.Done();}
    for(const auto& t:m.layouts){Statement q(db,"INSERT INTO layouts VALUES(?,?,?,?)");q.Bind(1,row.id);q.Bind(2,t.table);q.Bind(3,t.sort_column);q.Bind(4,int64_t(t.descending));q.Done();}
    for(const auto& c:m.widths){Statement q(db,"INSERT INTO widths VALUES(?,?,?,?)");q.Bind(1,row.id);q.Bind(2,c.table);q.Bind(3,c.column);q.Bind(4,int64_t(c.width));q.Done();}
    for(const auto& f:m.freezes){Statement q(db,"INSERT INTO freezes VALUES(?,?,?)");q.Bind(1,row.id);q.Bind(2,int64_t(f.kind));q.Bind(3,f.key);q.Done();}
    for(const auto& d:m.drafts){Statement q(db,"INSERT INTO drafts VALUES(?,?,?,?,?)");q.Bind(1,row.id);q.Bind(2,d.document);q.Bind(3,std::to_string(d.revision.value));q.Bind(4,d.note);BindAmount(q,5,d.amount);q.Done();}
    tx.Commit();return m.revision+1;
});}
Result<Workspace> MetadataStore::LoadWorkspace(){return Run([&]{
    Workspace w;
    Statement q(db,"SELECT revision,refresh,hidden,restore,appearance,unit,g.slot,trim_decimals,group_outputs,explorer,explorer_template,explorer_genesis,d.slot,notifications,camera_keep_awake FROM workspace w LEFT JOIN general_settings g USING(slot) LEFT JOIN desktop_settings d USING(slot) WHERE w.slot=1");
    if(q.Row()){
        w.revision=Counter(q.Int(0));
        const auto refresh=q.Int(1),appearance=q.Int(4),unit=q.Int(5);
        Require(refresh>=1 && refresh<=60 && appearance>=0 && appearance<=2 && unit>=0 && unit<=2,Error::Corrupt);
        w.preferences.refresh_seconds=int(refresh);
        w.preferences.hide_balances=Bool(q.Int(2));
        w.restore_documents=Bool(q.Int(3));w.appearance=int(appearance);w.unit=int(unit);
        if(q.Type(6)!=SQLITE_NULL){
            const auto explorer=q.Int(9);
            Require(explorer>=0 && explorer<=2,Error::Corrupt);
            w.preferences.trim_decimals=Bool(q.Int(7));
            w.preferences.group_address_outputs=Bool(q.Int(8));
            w.preferences.explorer=ExplorerMode(explorer);
            w.preferences.explorer_template=q.Text(10);
            w.preferences.explorer_genesis=q.Text(11);
        }
        if(q.Type(12)!=SQLITE_NULL){
            w.preferences.desktop_notifications=Bool(q.Int(13));
            w.preferences.camera_keep_awake=Bool(q.Int(14));
        }
        Require(!q.Row(),Error::Corrupt);
    }
    Require(ValidPreferences(w.preferences),Error::Corrupt);
    return w;
});}
Result<uint64_t> MetadataStore::SaveWorkspace(const Workspace& w){return Write([&]{
    Require(w.revision<uint64_t(std::numeric_limits<int64_t>::max()) && ValidPreferences(w.preferences) && w.appearance>=0 && w.appearance<=2 && w.unit>=0 && w.unit<=2);Transaction tx(db,true);
    const auto revision=Scalar(db,"SELECT coalesce((SELECT revision FROM workspace WHERE slot=1),0)");Require(Counter(revision)==w.revision,Error::Conflict);
    Statement q(db,"INSERT INTO workspace VALUES(1,?,?,?,?,?,?) ON CONFLICT(slot) DO UPDATE SET revision=excluded.revision,refresh=excluded.refresh,hidden=excluded.hidden,restore=excluded.restore,appearance=excluded.appearance,unit=excluded.unit");q.Bind(1,int64_t(w.revision+1));q.Bind(2,int64_t(w.preferences.refresh_seconds));q.Bind(3,int64_t(w.preferences.hide_balances));q.Bind(4,int64_t(w.restore_documents));q.Bind(5,int64_t(w.appearance));q.Bind(6,int64_t(w.unit));q.Done();
    Statement extra(db,"INSERT INTO general_settings VALUES(1,?,?,?,?,?) ON CONFLICT(slot) DO UPDATE SET trim_decimals=excluded.trim_decimals,group_outputs=excluded.group_outputs,explorer=excluded.explorer,explorer_template=excluded.explorer_template,explorer_genesis=excluded.explorer_genesis");
    extra.Bind(1,int64_t(w.preferences.trim_decimals));extra.Bind(2,int64_t(w.preferences.group_address_outputs));extra.Bind(3,int64_t(w.preferences.explorer));
    extra.Bind(4,w.preferences.explorer_template);extra.Bind(5,w.preferences.explorer_genesis);extra.Done();
    Statement desktop(db,"INSERT INTO desktop_settings VALUES(1,?,?) ON CONFLICT(slot) DO UPDATE SET notifications=excluded.notifications,camera_keep_awake=excluded.camera_keep_awake");
    desktop.Bind(1,int64_t(w.preferences.desktop_notifications));desktop.Bind(2,int64_t(w.preferences.camera_keep_awake));desktop.Done();
    tx.Commit();return w.revision+1;
});}
Result<RecoveryRecord> MetadataStore::LoadRecovery(){return Run([&]{
    RecoveryRecord r;Statement q(db,"SELECT revision,enabled,public_documents FROM recovery WHERE slot=1");
    if(q.Row()){r={Counter(q.Int(0)),Bool(q.Int(1)),q.Text(2)};Require(!q.Row() && Text(r.public_documents,524288) && (r.enabled || r.public_documents.empty()),Error::Corrupt);}return r;
});}
Result<uint64_t> MetadataStore::SaveRecovery(const RecoveryRecord& r){return Write([&]{
    Require(r.revision<uint64_t(std::numeric_limits<int64_t>::max()) && Text(r.public_documents,524288) && (r.enabled || r.public_documents.empty()));Transaction tx(db,true);
    Require(Counter(Scalar(db,"SELECT coalesce((SELECT revision FROM recovery WHERE slot=1),0)"))==r.revision,Error::Conflict);
    Statement q(db,"INSERT INTO recovery VALUES(1,?,?,?) ON CONFLICT(slot) DO UPDATE SET revision=excluded.revision,enabled=excluded.enabled,public_documents=excluded.public_documents");q.Bind(1,int64_t(r.revision+1));q.Bind(2,int64_t(r.enabled));q.Bind(3,r.public_documents);q.Done();tx.Commit();return r.revision+1;
});}
Result<bool> MetadataStore::HasBroadcastAttempt(const NetworkIdentity& network,const Hash256& txid)
{
    return Run([&]{
        Statement query(db,"SELECT 1 FROM broadcast_attempts WHERE genesis=? AND txid=?");
        query.Bind(1,network.genesis.hex());
        query.Bind(2,txid.hex());
        return query.Row();
    });
}
namespace {
constexpr auto BROADCAST_QUERY="SELECT a.genesis,a.txid,a.wtxid,a.wire_digest,a.observed_ms,d.wire,d.serial,d.attempted_ms,d.outcome,d.outcome_ms,d.backend,d.wallet FROM broadcast_attempts a LEFT JOIN broadcast_details d USING(genesis,txid)";
void BindAttemptKey(Statement& statement,const NetworkIdentity& net,const Hash256& txid)
{
    statement.Bind(1,net.genesis.hex());
    statement.Bind(2,txid.hex());
}
bool SameBytes(const BroadcastAttempt& a,const BroadcastAttempt& b)
{
    return a.network==b.network && a.txid==b.txid && a.wtxid==b.wtxid && a.wire_digest==b.wire_digest;
}
bool ValidBroadcastWire(const BroadcastRecord& record)
{
    const auto& source=record.wire_hex;
    if (source.empty() || source.size()>2*protocol::MAX_TRANSACTION_WIRE || source.size()%2
        || !std::ranges::all_of(source,[](unsigned char c){return (c>='0' && c<='9') || (c>='a' && c<='f');})) return false;
    const auto parsed=protocol::ParseTransactionDocument(source,protocol::DocumentNetwork(record.identity.network));
    if (!parsed.document || parsed.document->psbt_version || !parsed.document->transaction
        || parsed.document->transaction->txid!=record.identity.txid || parsed.document->transaction->wtxid!=record.identity.wtxid)
        return false;
    const auto& wire=parsed.document->wire;
    unsigned char digest[32];
    if (crypto_hash_sha256(digest,reinterpret_cast<const unsigned char*>(wire.data()),wire.size())!=0) return false;
    return protocol::TransactionHex({reinterpret_cast<const char*>(digest),sizeof(digest)})==record.identity.wire_digest.hex();
}
BroadcastRecord ReadBroadcast(const Statement& row)
{
    const auto network=Hash256::Parse(row.Text(0)),txid=Hash256::Parse(row.Text(1)),wtxid=Hash256::Parse(row.Text(2)),
        digest=Hash256::Parse(row.Text(3));
    Require(network && txid && wtxid && digest && row.Int(4)>0,Error::Corrupt);
    BroadcastRecord result{{{*network},*txid,*wtxid,*digest,row.Int(4)}};
    result.attempted_ms=result.identity.observed_ms;
    if (row.Type(5)==SQLITE_NULL) return result;
    result.wire_hex=row.Text(5);
    result.serial=Counter(row.Int(6));
    result.attempted_ms=row.Int(7);
    const auto state=row.Int(8);
    if (row.Type(9)!=SQLITE_NULL) result.outcome_ms=row.Int(9);
    result.backend=row.Text(10);
    result.wallet=row.Text(11);
    Require(result.serial>0 && result.attempted_ms>=result.identity.observed_ms && state>=0 && state<=4
        && ((state==0)==!result.outcome_ms) && (!result.outcome_ms || *result.outcome_ms>=result.attempted_ms)
        && Valid({result.backend,result.identity.network,result.wallet}) && ValidBroadcastWire(result),Error::Corrupt);
    result.outcome=BroadcastOutcome(state);
    return result;
}
std::optional<BroadcastRecord> FindBroadcast(sqlite3* db,const NetworkIdentity& net,const Hash256& txid)
{
    Statement row(db,std::string(BROADCAST_QUERY)+" WHERE a.genesis=? AND a.txid=?");
    BindAttemptKey(row,net,txid);
    return row.Row() ? std::optional(ReadBroadcast(row)) : std::nullopt;
}
void InsertBroadcast(sqlite3* db,const BroadcastAttempt& attempt)
{
    Require(Scalar(db,"SELECT count(*) FROM broadcast_attempts")<4096,Error::Full);
    Statement write(db,"INSERT INTO broadcast_attempts VALUES(?,?,?,?,?)");
    BindAttemptKey(write,attempt.network,attempt.txid);
    write.Bind(3,attempt.wtxid.hex());
    write.Bind(4,attempt.wire_digest.hex());
    write.Bind(5,attempt.observed_ms);
    write.Done();
}
}
Result<Done> MetadataStore::RecordBroadcastAttempt(const BroadcastAttempt& attempt)
{
    return Write([&]{
        Require(attempt.observed_ms>0);
        Transaction tx(db,true);
        {
            Statement existing(db,"SELECT 1 FROM broadcast_attempts WHERE genesis=? AND txid=?");
            existing.Bind(1,attempt.network.genesis.hex());
            existing.Bind(2,attempt.txid.hex());
            Require(!existing.Row(),Error::Conflict);
        }
        InsertBroadcast(db,attempt);
        tx.Commit();
        return Done{};
    });
}
Result<std::optional<BroadcastRecord>> MetadataStore::LoadBroadcastAttempt(const NetworkIdentity& net,const Hash256& txid)
{
    return Run([&]{return FindBroadcast(db,net,txid);});
}
Result<std::vector<BroadcastRecord>> MetadataStore::RecentBroadcastAttempts()
{
    return Run([&]{
        Statement rows(db,std::string(BROADCAST_QUERY)+" ORDER BY coalesce(d.attempted_ms,a.observed_ms) DESC,a.genesis,a.txid LIMIT 24");
        std::vector<BroadcastRecord> result;
        while (rows.Row()) result.push_back(ReadBroadcast(rows));
        return result;
    });
}
Result<uint64_t> MetadataStore::BeginBroadcastAttempt(const BroadcastRecord& next,std::optional<uint64_t> expected)
{
    return Write([&]{
        const auto& id=next.identity;
        Require(id.observed_ms>0 && next.attempted_ms==id.observed_ms
            && Valid({next.backend,id.network,next.wallet}) && ValidBroadcastWire(next));
        Transaction tx(db,true);
        const auto previous=FindBroadcast(db,id.network,id.txid);
        Require(bool(previous)==expected.has_value(),Error::Conflict);
        if (previous) {
            Require(previous->serial==expected && SameBytes(previous->identity,id)
                && (previous->wire_hex.empty() || previous->wire_hex==next.wire_hex),Error::Conflict);
            Require(previous->serial<INT64_MAX && next.attempted_ms>=previous->attempted_ms,Error::Conflict);
        } else InsertBroadcast(db,id);
        if (!previous || previous->wire_hex.empty())
            Require(Scalar(db,"SELECT coalesce(sum(length(wire)),0) FROM broadcast_details")+int64_t(next.wire_hex.size())<=8*1024*1024,
                Error::Full);
        const uint64_t serial=expected.value_or(0)+1;
        Statement write(db,"INSERT INTO broadcast_details VALUES(?,?,?,?,?,0,NULL,?,?) ON CONFLICT(genesis,txid) DO UPDATE SET serial=excluded.serial,attempted_ms=excluded.attempted_ms,outcome=0,outcome_ms=NULL,backend=excluded.backend,wallet=excluded.wallet");
        BindAttemptKey(write,id.network,id.txid);
        write.Bind(3,next.wire_hex);
        write.Bind(4,int64_t(serial));
        write.Bind(5,next.attempted_ms);
        write.Bind(6,next.backend);
        write.Bind(7,next.wallet);
        write.Done();
        tx.Commit();
        return serial;
    });
}
Result<Done> MetadataStore::FinishBroadcastAttempt(const BroadcastAttempt& id,uint64_t serial,BroadcastOutcome outcome,int64_t observed_ms)
{
    return Write([&]{
        Require(serial>0 && serial<=INT64_MAX && outcome>BroadcastOutcome::Pending && outcome<=BroadcastOutcome::Uncertain);
        Transaction tx(db,true);
        const auto current=FindBroadcast(db,id.network,id.txid);
        Require(current && current->serial==serial && SameBytes(current->identity,id)
            && current->outcome==BroadcastOutcome::Pending && observed_ms>=current->attempted_ms,Error::Conflict);
        Statement write(db,"UPDATE broadcast_details SET outcome=?,outcome_ms=? WHERE genesis=? AND txid=?");
        write.Bind(1,int64_t(outcome));
        write.Bind(2,observed_ms);
        write.Bind(3,id.network.genesis.hex());
        write.Bind(4,id.txid.hex());
        write.Done();
        tx.Commit();
        return Done{};
    });
}
namespace {
constexpr size_t MAX_RECENT_WALLETS = 16;
std::vector<Scope> RecentWallets(sqlite3* db)
{
    Statement query(db, "SELECT backend,genesis,wallet,position FROM recent_wallets ORDER BY position");
    std::vector<Scope> wallets;
    while (query.Row()) {
        auto genesis = Hash256::Parse(query.Text(1));
        Require(genesis && wallets.size() < MAX_RECENT_WALLETS && query.Int(3) == int64_t(wallets.size()), Error::Corrupt);
        Scope scope{query.Text(0), {*genesis}, query.Text(2)};
        Require(Valid(scope), Error::Corrupt);
        wallets.push_back(std::move(scope));
    }
    return wallets;
}
}
Result<std::vector<Scope>> MetadataStore::LoadRecentWallets()
{
    return Run([&] { return RecentWallets(db); });
}
Result<Done> MetadataStore::RememberWallet(const Scope& scope)
{
    return Write([&] {
        Require(Valid(scope));
        Transaction tx(db, true);
        auto wallets = RecentWallets(db);
        if (!wallets.empty() && wallets.front() == scope) return Done{};
        std::erase(wallets, scope);
        wallets.insert(wallets.begin(), scope);
        if (wallets.size() > MAX_RECENT_WALLETS) wallets.pop_back();
        Exec(db, "DELETE FROM recent_wallets");
        for (size_t position = 0; position < wallets.size(); ++position) {
            Statement insert(db, "INSERT INTO recent_wallets VALUES(?,?,?,?)");
            insert.Bind(1, wallets[position].backend);
            insert.Bind(2, wallets[position].network.genesis.hex());
            insert.Bind(3, wallets[position].daemon_wallet);
            insert.Bind(4, int64_t(position));
            insert.Done();
        }
        tx.Commit();
        return Done{};
    });
}
namespace {
struct ReceiveState { uint64_t serial{}; int pending{}; };

ReceiveState ReadReceiveState(sqlite3* db, int64_t scope)
{
    Statement query(db, "SELECT serial,pending FROM receive_state WHERE scope=?");
    query.Bind(1, scope);
    if (!query.Row()) return {};
    const auto serial = Counter(query.Int(0));
    const auto mode = query.Int(1);
    Require(serial && mode >= 0 && mode <= 2 && !query.Row(), Error::Corrupt);
    return {serial, int(mode)};
}

ReceivePool ReadReceivePool(sqlite3* db, int64_t scope, ReceiveState state)
{
    Statement query(db, "SELECT count(*),coalesce(sum(queued),0) FROM receive_addresses WHERE scope=?");
    query.Bind(1, scope);
    Require(query.Row(), Error::Corrupt);
    const auto count = Counter(query.Int(0)), queued = Counter(query.Int(1));
    Require(!query.Row() && count <= ReceivePool::MAX_RECORDS && queued <= ReceivePool::MAX_RESERVED && queued <= count, Error::Corrupt);
    return {state.pending ? state.serial : 0, size_t(queued), size_t(count)};
}

bool BoundReceiveTarget(const Scope& scope, const ReceiveTarget& target)
{
    return protocol::ReceiveChain(scope.network.genesis.hex()) == target.chain && !target.index && protocol::VerifyReceiveTarget(target);
}
}
namespace {
struct RescanState {
    uint64_t serial{};
    std::optional<ScanRange> range;
};
RescanState ReadRescanState(sqlite3* db, int64_t wallet)
{
    Statement query(db, "SELECT serial,first,last FROM rescan_state WHERE scope=?");
    query.Bind(1, wallet);
    if (!query.Row()) return {};
    RescanState state{Counter(query.Int(0)), {}};
    if (query.Type(1) != SQLITE_NULL) state.range = ScanRange{query.Int(1), query.Int(2)};
    Require(state.serial && (state.range ? query.Type(2) != SQLITE_NULL && state.range->Valid() : query.Type(2) == SQLITE_NULL) && !query.Row(), Error::Corrupt);
    return state;
}
}
Result<std::optional<PendingRescan>> MetadataStore::LoadRescan(const Scope& scope)
{
    return Run([&]() -> std::optional<PendingRescan> {
        const auto state = ReadRescanState(db, FindScope(db, scope, false).id);
        if (!state.range) return {};
        return PendingRescan{state.serial, *state.range};
    });
}
Result<uint64_t> MetadataStore::BeginRescan(const Scope& scope, ScanRange range)
{
    return Write([&] {
        Require(range.Valid() && !protocol::ReceiveChain(scope.network.genesis.hex()).empty());
        Transaction tx(db, true);
        const auto wallet = FindScope(db, scope, true).id;
        const auto state = ReadRescanState(db, wallet);
        Require(!state.range, Error::Conflict);
        Require(state.serial < uint64_t(std::numeric_limits<int64_t>::max()), Error::Full);
        Statement write(db, "INSERT INTO rescan_state VALUES(?,?,?,?) ON CONFLICT(scope) DO UPDATE SET serial=excluded.serial,first=excluded.first,last=excluded.last");
        write.Bind(1, wallet);
        write.Bind(2, int64_t(state.serial + 1));
        write.Bind(3, range.first);
        write.Bind(4, range.last);
        write.Done();
        tx.Commit();
        return state.serial + 1;
    });
}
Result<Done> MetadataStore::FinishRescan(const Scope& scope, uint64_t ticket)
{
    return Write([&] {
        Transaction tx(db, true);
        const auto wallet = FindScope(db, scope, false).id;
        const auto state = ReadRescanState(db, wallet);
        Require(state.range && state.serial == ticket, Error::Conflict);
        Statement write(db, "UPDATE rescan_state SET first=NULL,last=NULL WHERE scope=?");
        write.Bind(1, wallet);
        write.Done();
        tx.Commit();
        return Done{};
    });
}

Result<ReceivePool> MetadataStore::LoadReceivePool(const Scope& scope)
{
    return Run([&] {
        Transaction tx(db, false);
        const auto wallet = FindScope(db, scope, false).id;
        const auto pool = ReadReceivePool(db, wallet, ReadReceiveState(db, wallet));
        tx.Commit();
        return pool;
    });
}
Result<uint64_t> MetadataStore::BeginReceive(const Scope& scope, bool reserve)
{
    return Write([&] {
        Require(!protocol::ReceiveChain(scope.network.genesis.hex()).empty());
        Transaction tx(db, true);
        const auto wallet = FindScope(db, scope, true).id;
        const auto state = ReadReceiveState(db, wallet);
        const auto pool = ReadReceivePool(db, wallet, state);
        Require(!pool.pending, Error::Conflict);
        Require(pool.retained < ReceivePool::MAX_RECORDS && (!reserve || pool.available < ReceivePool::MAX_RESERVED)
            && state.serial < uint64_t(std::numeric_limits<int64_t>::max()), Error::Full);
        Statement write(db, "INSERT INTO receive_state VALUES(?,?,?) ON CONFLICT(scope) DO UPDATE SET serial=excluded.serial,pending=excluded.pending");
        write.Bind(1, wallet);
        write.Bind(2, int64_t(state.serial + 1));
        write.Bind(3, int64_t(reserve ? 2 : 1));
        write.Done();
        tx.Commit();
        return state.serial + 1;
    });
}
Result<Done> MetadataStore::FinishReceive(const Scope& scope, uint64_t ticket, const std::optional<ReceiveTarget>& address)
{
    return Write([&] {
        Require(ticket && (!address || BoundReceiveTarget(scope, *address)));
        Transaction tx(db, true);
        const auto wallet = FindScope(db, scope, false).id;
        const auto state = ReadReceiveState(db, wallet);
        Require(state.pending && state.serial == ticket, Error::Conflict);
        if (address) {
            Statement check(db, "SELECT 1 FROM receive_addresses WHERE scope=? AND (address=? OR script=?)");
            check.Bind(1, wallet);
            check.Bind(2, address->address);
            check.Bind(3, address->script);
            Require(!check.Row(), Error::Conflict);
            Statement write(db, "INSERT INTO receive_addresses VALUES(?,?,?,?,?,?,?,?)");
            write.Bind(1, wallet);
            write.Bind(2, int64_t(ticket));
            write.Bind(3, address->address);
            write.Bind(4, address->script);
            write.Bind(5, address->descriptor);
            write.Bind(6, address->path);
            write.Bind(7, address->observed_ms);
            write.Bind(8, int64_t(state.pending == 2));
            write.Done();
        }
        Statement finish(db, "UPDATE receive_state SET pending=0 WHERE scope=?");
        finish.Bind(1, wallet);
        finish.Done();
        tx.Commit();
        return Done{};
    });
}
Result<ReceiveTarget> MetadataStore::ConsumeReceive(const Scope& scope)
{
    return Write([&] {
        Transaction tx(db, true);
        const auto wallet = FindScope(db, scope, false).id;
        Statement read(db, "SELECT serial,address,script,descriptor,key_path,observed FROM receive_addresses WHERE scope=? AND queued=1 ORDER BY serial LIMIT 1");
        read.Bind(1, wallet);
        Require(read.Row(), Error::NotFound);
        const auto serial = Counter(read.Int(0));
        const auto state = ReadReceiveState(db, wallet);
        ReceiveTarget result{read.Text(1), read.Text(2), read.Text(3), read.Text(4),
            std::string(protocol::ReceiveChain(scope.network.genesis.hex())), {}, read.Int(5)};
        Require(serial && serial <= state.serial && (!state.pending || serial != state.serial)
            && BoundReceiveTarget(scope, result), Error::Corrupt);
        Require(!read.Row(), Error::Corrupt);
        Statement issue(db, "UPDATE receive_addresses SET queued=0 WHERE scope=? AND serial=?");
        issue.Bind(1, wallet);
        issue.Bind(2, int64_t(serial));
        issue.Done();
        tx.Commit();
        return result;
    });
}
Result<Profile> MetadataStore::LoadProfile(std::string_view id){return Run([&]{
    Require(Id(id));Statement q(db,"SELECT revision,genesis,wallet,host,port,tls,ca,proxy,proxy_host,proxy_port,credential_ref FROM profiles WHERE id=?");q.Bind(1,id);Require(q.Row(),Error::NotFound);Profile p;p.id=id;p.revision=Counter(q.Int(0));auto& c=p.candidate;c.expected_genesis=q.Text(1);if(q.Type(2)!=SQLITE_NULL)c.wallet=q.Text(2);auto& v=c.connection;
    const auto port=q.Int(4),proxy=q.Int(7),proxy_port=q.Int(9);Require(port>0 && port<=65535 && proxy>=0 && proxy<=2 && proxy_port>0 && proxy_port<=65535,Error::Corrupt);
    v.host=q.Text(3);v.port=uint16_t(port);v.tls=Bool(q.Int(5));v.trusted_ca_file=q.Text(6);v.proxy=ProxyKind(proxy);v.proxy_host=q.Text(8);v.proxy_port=uint16_t(proxy_port);v.credential={Authentication::SecureStore,q.Text(10)};Require(!q.Row() && ValidServerCandidate(c) && Text(v.trusted_ca_file,4096) && Text(v.credential.reference,4096,false),Error::Corrupt);return p;
});}
Result<uint64_t> MetadataStore::SaveProfile(const Profile& p){return Write([&]{
    Require(Id(p.id) && p.revision<uint64_t(std::numeric_limits<int64_t>::max()) && p.candidate.connection.credential.kind==Authentication::SecureStore && ValidServerCandidate(p.candidate) && Text(p.candidate.connection.trusted_ca_file,4096) && Text(p.candidate.connection.credential.reference,4096,false));Transaction tx(db,true);
    {Statement q(db,"SELECT revision FROM profiles WHERE id=?");q.Bind(1,p.id);const auto revision=q.Row()?Counter(q.Int(0)):0;Require(revision==p.revision,Error::Conflict);}
    const auto& c=p.candidate;const auto& v=c.connection;
    Statement q(db,"INSERT INTO profiles VALUES(?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET revision=excluded.revision,genesis=excluded.genesis,wallet=excluded.wallet,host=excluded.host,port=excluded.port,tls=excluded.tls,ca=excluded.ca,proxy=excluded.proxy,proxy_host=excluded.proxy_host,proxy_port=excluded.proxy_port,credential_ref=excluded.credential_ref");
    q.Bind(1,p.id);q.Bind(2,int64_t(p.revision+1));q.Bind(3,c.expected_genesis);if(c.wallet)q.Bind(4,*c.wallet);else q.Null(4);q.Bind(5,v.host);q.Bind(6,int64_t(v.port));q.Bind(7,int64_t(v.tls));q.Bind(8,v.trusted_ca_file);q.Bind(9,int64_t(v.proxy));q.Bind(10,v.proxy_host);q.Bind(11,int64_t(v.proxy_port));q.Bind(12,v.credential.reference);q.Done();tx.Commit();return p.revision+1;
});}
Result<Done> MetadataStore::SaveObservation(const Scope& scope,std::string_view key,const Provenance& p,std::optional<Money> amount){return Write([&]{
    Require(Id(key) && Id(p.context.backend.value()) && Id(p.context.wallet.value()) && Id(p.context.document.value()) && p.context.network==scope.network && p.observed_unix_ms && *p.observed_unix_ms>0 && int(p.freshness)>=0 && int(p.freshness)<=4);Transaction tx(db,true);const auto row=FindScope(db,scope,true);
    Statement q(db,"INSERT INTO observations VALUES(?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(scope,key) DO UPDATE SET backend_session=excluded.backend_session,wallet_id=excluded.wallet_id,document=excluded.document,revision=excluded.revision,epoch=excluded.epoch,source_freshness=excluded.source_freshness,observed_ms=excluded.observed_ms,height=excluded.height,amount=excluded.amount WHERE excluded.observed_ms>=observations.observed_ms");
    q.Bind(1,row.id);q.Bind(2,key);q.Bind(3,p.context.backend.value());q.Bind(4,p.context.wallet.value());q.Bind(5,p.context.document.value());q.Bind(6,std::to_string(p.context.revision.value));q.Bind(7,std::to_string(p.context.lock_epoch.value));q.Bind(8,int64_t(p.freshness));q.Bind(9,*p.observed_unix_ms);if(p.height)q.Bind(10,int64_t(*p.height));else q.Null(10);BindAmount(q,11,amount);q.Done();Require(sqlite3_changes(db)==1,Error::Conflict);tx.Commit();return Done{};
});}
Result<CachedObservation> MetadataStore::LoadObservation(const Scope& scope,std::string_view key){return Run([&]{
    Require(Id(key));Transaction tx(db,false);const auto row=FindScope(db,scope,false);Require(row.id!=0,Error::NotFound);Statement q(db,"SELECT backend_session,wallet_id,document,revision,epoch,source_freshness,observed_ms,height,amount FROM observations WHERE scope=? AND key=?");q.Bind(1,row.id);q.Bind(2,key);Require(q.Row(),Error::NotFound);
    const auto backend=BackendSessionId::Make(q.Text(0));const auto wallet=WalletId::Make(q.Text(1));const auto document=DocumentId::Make(q.Text(2));const auto freshness=q.Int(5),observed=q.Int(6);Require(backend && wallet && document && Id(backend->value()) && Id(wallet->value()) && Id(document->value()) && freshness>=0 && freshness<=4 && observed>0,Error::Corrupt);
    std::optional<uint32_t> height;if(q.Type(7)!=SQLITE_NULL){const auto n=q.Int(7);Require(n>=0 && n<=std::numeric_limits<uint32_t>::max(),Error::Corrupt);height=uint32_t(n);}
    CachedObservation result{{{*backend,scope.network,*wallet,*document,{Unsigned(q.Text(3))},{Unsigned(q.Text(4))}},Freshness::Cached,observed,height},Freshness(freshness),q.Amount(8)};Require(!q.Row(),Error::Corrupt);tx.Commit();return result;
});}
}
