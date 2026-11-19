// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/restart.h"
#include "adapters/rpc/json.h"
#include <charconv>
#include <filesystem>
#include <vector>
#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace roots::gui::platform {
namespace {
constexpr auto MaxContext=16384U;
constexpr std::string_view RestartArgument="--resume-gui-fd=";
bool Preservable(const ServerCandidate& value)
{
    return ValidServerCandidate(value)
        && (value.connection.credential.kind!=Authentication::Cookie || std::filesystem::path(value.connection.credential.reference).is_absolute())
        && (value.connection.trusted_ca_file.empty() || std::filesystem::path(value.connection.trusted_ca_file).is_absolute());
}
const std::string* AsString(const rpc::Json& object,const char* name)
{
    const auto* value=object.Find(name);
    return value && value->kind==rpc::Json::Kind::String?&value->text:nullptr;
}
std::optional<unsigned> AsUnsigned(const rpc::Json& object,const char* name,unsigned max)
{
    const auto* value=object.Find(name);
    unsigned result{};
    if(!value || value->kind!=rpc::Json::Kind::Number)return {};
    const auto parsed=std::from_chars(value->text.data(),value->text.data()+value->text.size(),result);
    if(parsed.ec!=std::errc{} || parsed.ptr!=value->text.data()+value->text.size() || result>max)return {};
    return result;
}
std::optional<bool> AsBool(const rpc::Json& object,const char* name)
{
    const auto* value=object.Find(name);
    if(!value || value->kind!=rpc::Json::Kind::Boolean)return {};
    return value->text=="true";
}
}

std::optional<std::string> EncodeRestartContext(const RestartContext& context)
{
    std::string bytes=std::string("{\"version\":1,\"offline\":")+(context.offline?"true":"false")+",\"profile\":";
    if(!context.profile)return bytes+"null}";
    const auto& profile=*context.profile;
    if(!Preservable(profile))return {};
    const auto& c=profile.connection;
    bytes+="{\"genesis\":"+rpc::Quote(profile.expected_genesis)+",\"wallet\":"+(profile.wallet?rpc::Quote(*profile.wallet):"null")
        +",\"host\":"+rpc::Quote(c.host)+",\"port\":"+std::to_string(c.port)+",\"tls\":"+(c.tls?"true":"false")
        +",\"ca\":"+rpc::Quote(c.trusted_ca_file)+",\"auth_kind\":"+std::to_string(int(c.credential.kind))
        +",\"auth_reference\":"+rpc::Quote(c.credential.reference)+",\"proxy\":"+std::to_string(int(c.proxy))
        +",\"proxy_host\":"+rpc::Quote(c.proxy_host)+",\"proxy_port\":"+std::to_string(c.proxy_port)+"}}";
    if(bytes.size()>MaxContext)return {};
    return bytes;
}
std::optional<RestartContext> DecodeRestartContext(std::string_view bytes)
{
    if(bytes.empty() || bytes.size()>MaxContext)return {};
    const auto parsed=rpc::Json::Parse(bytes,4,32);
    if(!parsed || parsed->kind!=rpc::Json::Kind::Object || parsed->members.size()!=3 || AsUnsigned(*parsed,"version",1)!=1)return {};
    const auto offline=AsBool(*parsed,"offline");
    const auto* profile=parsed->Find("profile");
    if(!offline || !profile)return {};
    RestartContext context{std::nullopt,*offline};
    if(profile->kind==rpc::Json::Kind::Null)return context;
    if(profile->kind!=rpc::Json::Kind::Object || profile->members.size()!=11)return {};
    const auto* genesis=AsString(*profile,"genesis"),*host=AsString(*profile,"host"),*ca=AsString(*profile,"ca");
    const auto* auth=AsString(*profile,"auth_reference"),*proxy_host=AsString(*profile,"proxy_host");
    const auto* wallet=profile->Find("wallet");
    const auto port=AsUnsigned(*profile,"port",65535),proxy_port=AsUnsigned(*profile,"proxy_port",65535);
    const auto kind=AsUnsigned(*profile,"auth_kind",1),proxy=AsUnsigned(*profile,"proxy",2);
    const auto tls=AsBool(*profile,"tls");
    if(!genesis || !host || !ca || !auth || !proxy_host || !wallet || !port || !proxy_port || !kind || !proxy || !tls)return {};
    if(wallet->kind!=rpc::Json::Kind::Null && wallet->kind!=rpc::Json::Kind::String)return {};
    ConnectionProfile connection{*host,uint16_t(*port),*tls,*ca,{Authentication(*kind),*auth},ProxyKind(*proxy),*proxy_host,uint16_t(*proxy_port)};
    ServerCandidate candidate{std::move(connection),*genesis,wallet->kind==rpc::Json::Kind::String?std::optional(wallet->text):std::nullopt};
    if(!Preservable(candidate))return {};
    context.profile=std::move(candidate);
    return context;
}
bool RestartSupported()
{
#if defined(__linux__)
    return ::access("/proc/self/exe",X_OK)==0;
#else
    return false;
#endif
}

#if defined(__linux__)
namespace {
constexpr int Seals=F_SEAL_SEAL|F_SEAL_SHRINK|F_SEAL_GROW|F_SEAL_WRITE;
struct InnerFd {
    int value;
    ~InnerFd(){if(value>=0)::close(value);}
};
}
#endif
std::optional<RestartContext> ConsumeRestartContext(std::string_view argument)
{
#if defined(__linux__)
    if(!argument.starts_with(RestartArgument))return {};
    argument.remove_prefix(RestartArgument.size());
    int fd=-1;
    const auto number=std::from_chars(argument.data(),argument.data()+argument.size(),fd);
    if(number.ec!=std::errc{} || number.ptr!=argument.data()+argument.size() || fd<3)return {};
    const auto seals=::fcntl(fd,F_GET_SEALS);
    struct stat info{};
    if(seals<0 || (seals&Seals)!=Seals || ::fstat(fd,&info) || !S_ISREG(info.st_mode) || info.st_uid!=::geteuid()
        || (info.st_mode&0777)!=0600 || info.st_size<1 || info.st_size>MaxContext)return {};
    const InnerFd close_on_exit{fd};
    std::string content(size_t(info.st_size),'\0');
    size_t read{};
    while(read<content.size()){
        const auto received=::pread(fd,content.data()+read,content.size()-read,off_t(read));
        if(received<0 && errno==EINTR)continue;
        if(received<=0)return {};
        read+=size_t(received);
    }
    return DecodeRestartContext(content);
#else
    (void)argument;return {};
#endif
}
app::DesktopReceipt ReplaceGui(const RestartContext& context,std::span<const std::string> retained_arguments)
{
#if defined(__linux__)
    const auto data=EncodeRestartContext(context);
    if(!data)return {false,"The GUI restart context could not be validated."};
    const InnerFd file{::memfd_create("bitcoin-roots-gui-restart",MFD_ALLOW_SEALING|MFD_CLOEXEC)};
    if(file.value<0 || ::fchmod(file.value,0600)!=0)return {false,"The GUI restart context could not be allocated privately."};
    size_t written{};
    while(written<data->size()){
        const auto count=::write(file.value,data->data()+written,data->size()-written);
        if(count<0 && errno==EINTR)continue;
        if(count<=0)return {false,"The GUI restart context could not be written."};
        written+=size_t(count);
    }
    if(::fcntl(file.value,F_ADD_SEALS,Seals)!=0 || ::fcntl(file.value,F_SETFD,0)!=0)
        return {false,"The GUI restart context could not be sealed."};
    std::string executable="roots-gui",argument=std::string(RestartArgument)+std::to_string(file.value);
    std::vector<std::string> argv{executable,argument};
    argv.insert(argv.end(),retained_arguments.begin(),retained_arguments.end());
    std::vector<char*> native;
    for(auto& item:argv)native.push_back(item.data());
    native.push_back(nullptr);
    ::execv("/proc/self/exe",native.data());
    return {false,"The GUI executable could not be reopened. This workspace remains open."};
#else
    (void)context;(void)retained_arguments;
    return {false,"GUI restart requires the Linux process replacement adapter."};
#endif
}
}
