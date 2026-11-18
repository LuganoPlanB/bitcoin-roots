// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/transaction_documents.h"
#include "adapters/rpc/json.h"
#include <sodium.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
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
LocalDocumentReply Read(std::string argument, protocol::DisplayNetwork network,bool path,std::stop_token stop)
{
    if (stop.stop_requested()) return {protocol::DocumentResult{{},protocol::DocumentError::Cancelled}};
    CleanText source;
    if (!path) source.text=std::move(argument);
    else {
#if defined(__unix__)
        const int fd=::open(argument.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
        if (fd<0) return {{}, "Choose a readable local document file."};
        struct File {int fd; ~File(){::close(fd);}} file{fd};
        struct stat before{};
        if (::fstat(fd,&before)!=0 || !S_ISREG(before.st_mode)) return {{}, "Choose a regular local file."};
        if (before.st_size<0 || uint64_t(before.st_size)>protocol::MAX_TRANSACTION_SOURCE) return {protocol::DocumentResult{{},protocol::DocumentError::Limit}};
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        const auto within_budget=[&]{return !stop.stop_requested() && std::chrono::steady_clock::now()<deadline;};
        source.text.resize(size_t(before.st_size));
        size_t position{};
        while (position<source.text.size() && within_budget()) {
            const auto n=::read(fd,source.text.data()+position,source.text.size()-position);
            if (n<0 && errno==EINTR) continue;
            if (n<=0) return {{}, "The local file could not be read in full."};
            position+=size_t(n);
        }
        if (!within_budget())
            return {protocol::DocumentResult{{},stop.stop_requested() ? protocol::DocumentError::Cancelled : protocol::DocumentError::Limit}};
        struct stat after{};
        if (::fstat(fd,&after)!=0 || before.st_size!=after.st_size || before.st_mtime!=after.st_mtime || before.st_ctime!=after.st_ctime
#if defined(__linux__)
            || before.st_mtim.tv_nsec!=after.st_mtim.tv_nsec || before.st_ctim.tv_nsec!=after.st_ctim.tv_nsec
#endif
        ) return {{}, "The file changed while reading. Select it again to continue."};
#else
        return {{}, "The native transaction file reader is unavailable on this platform."};
#endif
    }
    return {protocol::ParseTransactionDocument(source.text,network,stop,!path)};
}
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
    return ValidLocalDocumentPath(path) && Start(context,std::move(path),network,true);
}
bool TransactionDocuments::ReadText(LocalDocumentContext context,std::string text,protocol::DisplayNetwork network)
{
    return Start(context,std::move(text),network,false);
}
bool TransactionDocuments::Start(LocalDocumentContext context,std::string source,protocol::DisplayNetwork network,bool path)
{
    CleanText scrub{std::move(source)};
    if (std::this_thread::get_id()!=owner || Busy() || !context.generation) return false;
    try {
        active=context;
        retired=false;
        worker=std::jthread([this,source=std::move(scrub.text),network,path](std::stop_token stop) mutable {
            LocalDocumentReply reply;
            try {reply=Read(std::move(source),network,path,stop);}
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
std::string SaveOriginalDocument(std::string_view path,const protocol::TransactionDocument& document)
{
    if (!ValidLocalDocumentPath(path) || document.source.empty() || document.source.size()>protocol::MAX_TRANSACTION_SOURCE)
        return "Choose an absolute local path for a new file.";
#if defined(__unix__)
    const std::filesystem::path target(path);
    const int fd=::open(target.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if (fd<0) return "Choose a new writable filename. Existing files are retained.";
    size_t position{};
    while (position<document.source.size()) {
        const auto n=::write(fd,document.source.data()+position,document.source.size()-position);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) break;
        position+=size_t(n);
    }
    const bool written=position==document.source.size() && ::fsync(fd)==0;
    const bool closed=::close(fd)==0;
    if (!written || !closed) return "Write failed; a partial file may remain. Choose a new path to try again.";
    const int dir=::open(target.parent_path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    const bool durable=dir>=0 && ::fsync(dir)==0;
    if (dir>=0) ::close(dir);
    if (!durable) return "Bytes were written. Check the file because directory durability could not be confirmed.";
    return {};
#else
    return "The native transaction file writer is unavailable on this platform.";
#endif
}
}
