// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/transaction_documents.h"
#include "adapters/protocol/qr.h"
#include "adapters/rpc/json.h"
#include <sodium.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <utility>
#if defined(__unix__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace roots::gui {
namespace {
struct CleanText {
    std::string text;
    ~CleanText(){if (!text.empty()) sodium_memzero(text.data(),text.size());}
};
}
LocalDocumentReply TransactionDocuments::Read(std::string argument,protocol::DisplayNetwork network,Source mode,std::stop_token stop)
{
    if (stop.stop_requested()) return {protocol::DocumentResult{{},protocol::DocumentError::Cancelled}};
    CleanText source;
    if (mode==Source::Text || mode==Source::Binary) source.text=std::move(argument);
    else {
        const size_t limit=mode==Source::QrImage ? protocol::MAX_QR_IMAGE : protocol::MAX_TRANSACTION_SOURCE;
        auto read=ReadPublicFile(argument,limit,stop,mode==Source::QrImage);
        source.text=std::move(read.bytes);
        if (read.cancelled) return {protocol::DocumentResult{{},protocol::DocumentError::Cancelled}};
        if (read.limited) return {protocol::DocumentResult{{},protocol::DocumentError::Limit}};
        if (!read.error.empty()) return {{},std::move(read.error)};
    }
    if (mode==Source::QrImage) {
        auto text=protocol::DecodeQrImage(source.text,stop);
        if (text) return {.qr_text=std::move(text)};
        return {.file_error="Choose a PNG within 1536 pixels per side containing one readable QR code."};
    }
    return {protocol::ParseTransactionDocument(source.text,network,stop,mode==Source::Text)};
}
bool ValidLocalDocumentPath(std::string_view text)
{
    return !text.empty() && text.size()<=4096 && rpc::ValidUtf8(text)
        && std::ranges::none_of(text,[](unsigned char c){return c<32 || c==127;})
        && std::filesystem::path(text).is_absolute();
}
TransactionDocuments::~TransactionDocuments()
{
    Cancel();
    if (worker.joinable()) worker.join();
}
bool TransactionDocuments::ReadFile(LocalDocumentContext context,std::string path,protocol::DisplayNetwork network)
{
    return ValidLocalDocumentPath(path) && Start(context,std::move(path),network,Source::File);
}
bool TransactionDocuments::ReadText(LocalDocumentContext context,std::string text,protocol::DisplayNetwork network)
{
    return Start(context,std::move(text),network,Source::Text);
}
bool TransactionDocuments::ReadQrImage(LocalDocumentContext context,std::string path)
{
    return ValidLocalDocumentPath(path) && Start(context,std::move(path),protocol::DisplayNetwork::Unknown,Source::QrImage);
}
bool TransactionDocuments::ReadBinary(LocalDocumentContext context,std::string payload,protocol::DisplayNetwork network)
{
    return Start(context,std::move(payload),network,Source::Binary);
}
bool TransactionDocuments::Start(LocalDocumentContext context,std::string source,protocol::DisplayNetwork network,Source mode)
{
    CleanText scrub{std::move(source)};
    if (std::this_thread::get_id()!=owner || Busy() || !context.generation) return false;
    try {
        active=context;
        retired=false;
        worker=std::jthread([this,source=std::move(scrub.text),network,mode](std::stop_token stop) mutable {
            LocalDocumentReply reply;
            try {reply=Read(std::move(source),network,mode,stop);}
            catch (...) {reply.file_error="The local document could not be read within the available resources.";}
            std::lock_guard guard(mutex);
            completed=std::move(reply);
        });
    } catch (...) {
        active.reset();
        return false;
    }
    return true;
}
void TransactionDocuments::Cancel()
{
    if (std::this_thread::get_id()!=owner || !Busy()) return;
    retired=true;
    worker.request_stop();
}
std::optional<LocalDocumentReply> TransactionDocuments::Poll(const std::optional<LocalDocumentContext>& context)
{
    if (std::this_thread::get_id()!=owner || !active) return {};
    if (active!=context) Cancel();
    std::optional<LocalDocumentReply> reply;
    {
        std::lock_guard guard(mutex);
        if (!completed) return {};
        reply=std::move(completed);
        completed.reset();
    }
    worker.join();
    active.reset();
    if (retired) return {};
    return reply;
}
#if defined(__unix__)
namespace {
struct File {
    int fd;
    ~File(){if(fd>=0)::close(fd);}
};
int OpenParent(const std::filesystem::path& target,bool strict)
{
    if (!strict) return ::open(target.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    File directory{::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC)};
    for (const auto& part:target.parent_path().relative_path()) {
        if (directory.fd<0 || part=="." || part==".." || part.empty()) return -1;
        const int next=::openat(directory.fd,part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        ::close(directory.fd);
        directory.fd=next;
    }
    return std::exchange(directory.fd,-1);
}
}
#endif
PublicFileRead ReadPublicFile(std::string_view path,size_t limit,std::stop_token stop,bool strict)
{
    if (stop.stop_requested()) return {.cancelled=true};
    if (!ValidLocalDocumentPath(path)) return {.error="Choose an absolute local file path."};
#if defined(__unix__)
    const std::filesystem::path target(path);
    File parent{strict ? OpenParent(target,true) : -1};
    if (strict && parent.fd<0) return {.error="Choose a local path without symbolic links."};
    constexpr int flags=O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK;
    File file{strict ? ::openat(parent.fd,target.filename().c_str(),flags) : ::open(target.c_str(),flags)};
    if (file.fd<0) return {.error="Choose a readable local document file."};
    struct stat before{};
    if (::fstat(file.fd,&before)!=0 || !S_ISREG(before.st_mode)) return {.error="Choose a regular local file."};
    if (before.st_size<0 || uint64_t(before.st_size)>limit) return {.limited=true};
    CleanText source;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    const auto within_budget=[&]{return !stop.stop_requested() && std::chrono::steady_clock::now()<deadline;};
    source.text.resize(size_t(before.st_size));
    size_t position{};
    while(position<source.text.size() && within_budget()) {
        const auto n=::read(file.fd,source.text.data()+position,source.text.size()-position);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) return {.error="The local file could not be read in full."};
        position+=size_t(n);
    }
    if (!within_budget()) return {.limited=!stop.stop_requested(),.cancelled=stop.stop_requested()};
    struct stat after{};
    if (::fstat(file.fd,&after)!=0 || before.st_size!=after.st_size || before.st_mtime!=after.st_mtime || before.st_ctime!=after.st_ctime
#if defined(__linux__)
        || before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec || before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec
#endif
    ) return {.error="The file changed while reading. Select it again to continue."};
    return {std::move(source.text),{}};
#else
    return {.error="The native transaction file reader is unavailable on this platform."};
#endif
}
std::string SavePublicFile(std::string_view path,std::string_view bytes,size_t limit,bool strict)
{
    if (!ValidLocalDocumentPath(path) || bytes.empty() || bytes.size()>limit)
        return "Choose an absolute local path for a new file.";
#if defined(__unix__)
    const std::filesystem::path target(path);
    File parent{OpenParent(target,strict)};
    if (strict && parent.fd<0) return "Choose a local path without symbolic links.";
    constexpr int flags=O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC;
    const int fd=strict ? ::openat(parent.fd,target.filename().c_str(),flags,0600) : ::open(target.c_str(),flags,0600);
    if (fd<0) return "Choose a new writable filename. Existing files are retained.";
    size_t position{};
    while(position<bytes.size()) {
        const auto n=::write(fd,bytes.data()+position,bytes.size()-position);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) break;
        position+=size_t(n);
    }
    const bool written=position==bytes.size() && ::fsync(fd)==0;
    const bool closed=::close(fd)==0;
    if (!written || !closed) return "Write failed; a partial file may remain. Choose a new path to try again.";
    if (parent.fd<0 || ::fsync(parent.fd)!=0)
        return "Bytes were written. Check the file because directory durability could not be confirmed.";
    return {};
#else
    return "The native transaction file writer is unavailable on this platform.";
#endif
}
std::string SaveOriginalDocument(std::string_view path,const protocol::TransactionDocument& document)
{
    return SavePublicFile(path,document.source,protocol::MAX_TRANSACTION_SOURCE,false);
}
}
