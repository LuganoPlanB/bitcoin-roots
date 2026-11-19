// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/storage/protected_file.h"
#include <sodium.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#if defined(__unix__)
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif
#endif
namespace roots::gui::storage {
namespace {
// Fixed, versioned KDF parameters bound attacker-controlled work before KDF.
// Both Argon2id and stream nonces/rotation are implemented by libsodium.
constexpr std::string_view Format="ROOTS-GUI-PROTECTED-1\nArgon2id13 3 67108864\n";
constexpr unsigned long long Passes=3;
constexpr size_t Memory=67108864;
constexpr size_t Header=crypto_secretstream_xchacha20poly1305_HEADERBYTES;
constexpr size_t Salt=crypto_pwhash_SALTBYTES;
struct Fail { Error error; };
void Need(bool b,Error e=Error::Io){if(!b)throw Fail{e};}
struct State {crypto_secretstream_xchacha20poly1305_state value;~State(){sodium_memzero(&value,sizeof(value));}};
#if defined(__unix__)
struct Fd {int value{-1};explicit Fd(int v):value(v){}~Fd(){if(value>=0)close(value);}Fd(const Fd&)=delete;};
void Parent(const std::filesystem::path& path){struct stat s{};Need(path.is_absolute() && lstat(path.parent_path().c_str(),&s)==0 && S_ISDIR(s.st_mode) && s.st_uid==getuid() && (s.st_mode&0077)==0);}
void Private(int fd){struct stat s{};Need(fd>=0 && fstat(fd,&s)==0 && S_ISREG(s.st_mode) && s.st_uid==getuid() && (s.st_mode&0077)==0 && s.st_nlink==1);}
std::optional<std::vector<unsigned char>> ReadFile(const std::filesystem::path& path,size_t limit=ProtectedFile::MaxBytes){Parent(path);Fd fd(open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK));if(fd.value<0 && errno==ENOENT)return {};Private(fd.value);struct stat s{};Need(fstat(fd.value,&s)==0 && s.st_size>=0 && uint64_t(s.st_size)<=limit+4096,Error::Resource);std::vector<unsigned char> bytes(size_t(s.st_size));size_t at{};while(at<bytes.size()){auto n=read(fd.value,bytes.data()+at,bytes.size()-at);if(n<0 && errno==EINTR)continue;Need(n>0);at+=size_t(n);}unsigned char extra{};Need(read(fd.value,&extra,1)==0);return bytes;}
void Publish(const std::filesystem::path& path,std::span<const unsigned char> bytes,bool replace=true){Parent(path);unsigned char random[16];randombytes_buf(random,sizeof(random));char suffix[33];sodium_bin2hex(suffix,sizeof(suffix),random,sizeof(random));auto temporary=path;temporary += std::string(".new-")+suffix;
    Fd fd(open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600));Private(fd.value);bool published{};
    try {
        size_t at{};
        while(at<bytes.size()){auto n=write(fd.value,bytes.data()+at,bytes.size()-at);if(n<0 && errno==EINTR)continue;Need(n>0);at+=size_t(n);}
        Need(fsync(fd.value)==0);
        if (replace) Need(rename(temporary.c_str(),path.c_str())==0);
        else {
#if defined(__linux__) && defined(SYS_renameat2)
            const auto moved=syscall(SYS_renameat2,AT_FDCWD,temporary.c_str(),AT_FDCWD,path.c_str(),RENAME_NOREPLACE);
            Need(moved==0,errno==EEXIST?Error::Conflict:(errno==ENOSYS || errno==EINVAL)?Error::Unsupported:Error::Io);
#else
            throw Fail{Error::Unsupported};
#endif
        }
        published=true;
        Fd directory(open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC));
        Need(directory.value>=0 && fsync(directory.value)==0);
    } catch (...) { if(!published)unlink(temporary.c_str());throw; }
}
#else
std::optional<std::vector<unsigned char>> ReadFile(const std::filesystem::path&,size_t=ProtectedFile::MaxBytes){throw Fail{Error::Unsupported};}
void Publish(const std::filesystem::path&,std::span<const unsigned char>,bool=true){throw Fail{Error::Unsupported};}
#endif
}
CleansedBytes::~CleansedBytes(){if(!bytes.empty())sodium_memzero(bytes.data(),bytes.size());}
CleansedBytes& CleansedBytes::operator=(CleansedBytes&& other) noexcept {if(this!=&other){if(!bytes.empty())sodium_memzero(bytes.data(),bytes.size());bytes=std::move(other.bytes);}return *this;}
Result<std::unique_ptr<FileGuard>> FileGuard::Open(const std::filesystem::path& path,bool exclusive){
#if defined(__unix__)
    try{Parent(path);auto guard=std::make_unique<FileGuard>();auto name=path;name += ".guard";guard->fd=open(name.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);Private(guard->fd);Need(flock(guard->fd,(exclusive?LOCK_EX:LOCK_SH)|LOCK_NB)==0,Error::Busy);return {std::move(guard),Error::None};}catch(Fail e){return {{},e.error};}catch(...){return {{},Error::Io};}
#else
    return {{},Error::Unsupported};
#endif
}
FileGuard::~FileGuard(){
#if defined(__unix__)
    if(fd>=0)close(fd);
#endif
}
bool FileGuard::Exclusive(){
#if defined(__unix__)
    return flock(fd,LOCK_EX|LOCK_NB)==0;
#else
    return false;
#endif
}
void FileGuard::Shared(){
#if defined(__unix__)
    flock(fd,LOCK_SH);
#endif
}
Result<std::unique_ptr<ProtectedFile>> ProtectedFile::Open(const std::filesystem::path& path,std::string_view password,size_t max_plain,bool existing_only){
    try{Need(sodium_init()>=0,Error::Unsupported);Need(!password.empty() && password.size()<=255 && max_plain<=MaxBytes,Error::Invalid);auto file=std::unique_ptr<ProtectedFile>(new ProtectedFile);file->path=path;auto bytes=ReadFile(path,max_plain);Need(!existing_only || bool(bytes),Error::NotFound);file->existed=bool(bytes);
        if(bytes){Need(bytes->size()>=Format.size()+Salt+Header+crypto_secretstream_xchacha20poly1305_ABYTES && std::equal(Format.begin(),Format.end(),bytes->begin()),Error::Authentication);file->accepted=std::move(*bytes);file->prefix.assign(file->accepted.begin(),file->accepted.begin()+std::ptrdiff_t(Format.size()+Salt));}
        else{file->prefix.assign(Format.begin(),Format.end());file->prefix.resize(Format.size()+Salt);randombytes_buf(file->prefix.data()+Format.size(),Salt);}
        file->key=static_cast<unsigned char*>(sodium_malloc(crypto_secretstream_xchacha20poly1305_KEYBYTES));Need(file->key && sodium_mlock(file->key,crypto_secretstream_xchacha20poly1305_KEYBYTES)==0,Error::Resource);
        Need(crypto_pwhash(file->key,crypto_secretstream_xchacha20poly1305_KEYBYTES,password.data(),password.size(),file->prefix.data()+Format.size(),Passes,Memory,crypto_pwhash_ALG_ARGON2ID13)==0,Error::Resource);
        if(file->existed){auto plain=file->Read();Need(bool(plain),plain.error);Need(plain.value->bytes.size()<=max_plain,Error::Resource);}return {std::move(file),Error::None};
    }catch(Fail e){return {{},e.error};}catch(const std::bad_alloc&){return {{},Error::Resource};}catch(...){return {{},Error::Io};}
}
ProtectedFile::~ProtectedFile(){if(key)sodium_free(key);}
Result<std::vector<unsigned char>> ProtectedFile::Seal(std::span<const unsigned char> plain){try{Need(plain.size()<=MaxBytes,Error::Resource);std::vector<unsigned char> result=prefix;const auto at=result.size();result.resize(at+Header+plain.size()+crypto_secretstream_xchacha20poly1305_ABYTES);State stream;Need(crypto_secretstream_xchacha20poly1305_init_push(&stream.value,result.data()+at,key)==0,Error::Resource);unsigned long long length{};
    Need(crypto_secretstream_xchacha20poly1305_push(&stream.value,result.data()+at+Header,&length,plain.data(),plain.size(),prefix.data(),prefix.size(),crypto_secretstream_xchacha20poly1305_TAG_FINAL)==0,Error::Resource);Need(length==plain.size()+crypto_secretstream_xchacha20poly1305_ABYTES,Error::Resource);return {std::move(result),Error::None};}catch(Fail e){return {{},e.error};}catch(...){return {{},Error::Resource};}}
Result<CleansedBytes> ProtectedFile::Unseal(std::span<const unsigned char> bytes){try{Need(bytes.size()>=prefix.size()+Header+crypto_secretstream_xchacha20poly1305_ABYTES && bytes.size()<=MaxBytes+4096 && std::equal(prefix.begin(),prefix.end(),bytes.begin()),Error::Authentication);State stream;Need(crypto_secretstream_xchacha20poly1305_init_pull(&stream.value,bytes.data()+prefix.size(),key)==0,Error::Authentication);CleansedBytes result;result.bytes.resize(bytes.size()-prefix.size()-Header-crypto_secretstream_xchacha20poly1305_ABYTES);unsigned char tag{};unsigned long long length{};
    Need(crypto_secretstream_xchacha20poly1305_pull(&stream.value,result.bytes.data(),&length,&tag,bytes.data()+prefix.size()+Header,bytes.size()-prefix.size()-Header,prefix.data(),prefix.size())==0 && tag==crypto_secretstream_xchacha20poly1305_TAG_FINAL && length==result.bytes.size(),Error::Authentication);return {std::move(result),Error::None};}catch(Fail e){return {{},e.error};}catch(...){return {{},Error::Resource};}}
Result<CleansedBytes> ProtectedFile::Read(){if(!existed)return {CleansedBytes{},Error::None};return Unseal(accepted);}
Result<Done> ProtectedFile::Save(std::span<const unsigned char> plain){try{auto current=ReadFile(path);Need((existed && current && *current==accepted) || (!existed && !current),Error::Conflict);auto encrypted=Seal(plain);Need(bool(encrypted),encrypted.error);Publish(path,*encrypted.value);accepted=std::move(*encrypted.value);existed=true;return {Done{},Error::None};}catch(Fail e){return {{},e.error};}catch(...){return {{},Error::Io};}}
Result<Done> ProtectedFile::SaveNew(std::span<const unsigned char> plain)
{
    try {
        Need(!existed, Error::Conflict);
        auto encrypted = Seal(plain);
        Need(bool(encrypted), encrypted.error);
        Publish(path, *encrypted.value, false);
        accepted = std::move(*encrypted.value);
        existed = true;
        return {Done{}, Error::None};
    } catch (Fail e) { return {{}, e.error}; }
    catch (...) { return {{}, Error::Io}; }
}
Result<Done> ProtectedFile::FinishConversion(const std::filesystem::path& plain_path,ConversionResolution resolution){
    try{
        std::unique_ptr<FileGuard> backup_guard;
        Need(existed,Error::Invalid);auto authenticated=Read();Need(bool(authenticated),authenticated.error);
        auto original=ReadFile(plain_path);if(!original)return {Done{},Error::None};CleansedBytes other;other.bytes=std::move(*original);Need(other.bytes.size()<=MaxBytes,Error::Resource);
        if(other.bytes!=authenticated.value->bytes){
            Need(resolution==ConversionResolution::PreserveOtherCopy,Error::ConversionConflict);
            auto base=plain_path;base += ".conversion-backup";auto guard=FileGuard::Open(base,true);Need(bool(guard),guard.error);backup_guard=std::move(*guard.value);
            auto backup=base;backup += ".protected";auto saved=ReadFile(backup);
            if(!saved){auto sealed=Seal(other.bytes);Need(bool(sealed),sealed.error);Publish(backup,*sealed.value);saved=ReadFile(backup);}
            Need(bool(saved),Error::Io);auto verified=Unseal(*saved);Need(bool(verified) && verified.value->bytes==other.bytes,Error::ConversionBackupConflict);
        }
        // Recheck both originals immediately before retiring the plaintext.
        // The advisory guard excludes all cooperating metadata writers.
        auto current=ReadFile(path);Need(current && *current==accepted,Error::Conflict);
        auto latest=ReadFile(plain_path);Need(bool(latest),Error::Conflict);CleansedBytes checked;checked.bytes=std::move(*latest);Need(checked.bytes==other.bytes,Error::Conflict);
#if defined(__unix__)
        Need(unlink(plain_path.c_str())==0);Fd directory(open(plain_path.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC));Need(directory.value>=0 && fsync(directory.value)==0);
#else
        throw Fail{Error::Unsupported};
#endif
        return {Done{},Error::None};
    }catch(Fail e){return {{},e.error};}catch(...){return {{},Error::Io};}
}
}
