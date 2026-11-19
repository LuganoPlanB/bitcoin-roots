// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/rpc/transport.h"
#include <curl/curl.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace roots::gui::rpc {
namespace {
void Clear(std::string& text) { volatile char* p = text.data(); for (size_t i = 0; i < text.size(); ++i) p[i] = 0; }
struct SecretBuffer { std::string value; ~SecretBuffer() { Clear(value); } };
bool CleanSecret(std::string_view text)
{
    return text.size() <= 4096 && std::all_of(text.begin(), text.end(), [](unsigned char c) { return c != 0 && c != '\r' && c != '\n'; });
}
struct CurlRuntime {
    CURLcode status{curl_global_init(CURL_GLOBAL_DEFAULT)};
    ~CurlRuntime() { if (status == CURLE_OK) curl_global_cleanup(); }
};
std::string HostPort(std::string_view host, uint16_t port)
{
    return (host.find(':') == std::string_view::npos ? std::string(host) : '[' + std::string(host) + ']') + ':' + std::to_string(port);
}
std::string EncodePath(std::string_view text)
{
    constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '~') out += char(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
struct Receive {
    std::string body;
    size_t headers{}, body_limit{}, header_limit{};
    bool limit{}, allocation_failed{};
    static size_t Body(char* data, size_t size, size_t count, void* raw) noexcept
    {
        auto& self = *static_cast<Receive*>(raw);
        if (size && count > self.body_limit / size) { self.limit = true; return 0; }
        const size_t n = size * count;
        if (n > self.body_limit - self.body.size()) { self.limit = true; return 0; }
        try { self.body.append(data, n); } catch (...) { self.allocation_failed = true; return 0; }
        return n;
    }
    static size_t Header(char*, size_t size, size_t count, void* raw) noexcept
    {
        auto& self = *static_cast<Receive*>(raw);
        if (size && count > self.header_limit / size) { self.limit = true; return 0; }
        const size_t n = size * count;
        if (n > self.header_limit - self.headers) { self.limit = true; return 0; }
        self.headers += n; return n;
    }
};
void ClearJson(Json& value)
{
    Clear(value.text);
    for (auto& [name,child] : value.members) { Clear(name); ClearJson(child); }
    for (auto& child : value.items) ClearJson(child);
}
bool ValidLimits(const Limits& l)
{
    return l.connect_timeout.count() > 0 && l.connect_timeout.count() <= 60000 &&
        l.total_timeout >= l.connect_timeout && l.total_timeout.count() <= 300000 &&
        l.request_bytes > 0 && l.request_bytes <= 8 * 1024 * 1024 &&
        l.response_bytes > 0 && l.response_bytes <= 32 * 1024 * 1024 &&
        l.header_bytes > 0 && l.header_bytes <= 256 * 1024 &&
        l.json_depth > 0 && l.json_depth <= 128 && l.json_nodes > 0 && l.json_nodes <= 1000000;
}
Error CurlError(CURLcode code)
{
    switch (code) {
    case CURLE_OPERATION_TIMEDOUT: return Error::Timeout;
    case CURLE_COULDNT_RESOLVE_HOST: return Error::Resolve;
    case CURLE_COULDNT_RESOLVE_PROXY: case CURLE_PROXY: return Error::Proxy;
    case CURLE_SSL_CONNECT_ERROR: case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CACERT_BADFILE: case CURLE_SSL_CERTPROBLEM: return Error::Tls;
    case CURLE_FILESIZE_EXCEEDED: return Error::Limit;
    default: return Error::Connection;
    }
}
}
Credentials::Credentials(std::string user, std::string pass) : username(std::move(user)), password(std::move(pass)) {}
Credentials::Credentials(Credentials&& other) noexcept : username(std::move(other.username)), password(std::move(other.password)) {}
Credentials::~Credentials() { Clear(username); Clear(password); }
std::optional<Credentials> CookieCredentials::Resolve(const CredentialReference& ref, std::string_view)
{
    if (ref.kind != Authentication::Cookie || !std::filesystem::path(ref.reference).is_absolute()) return {};
    SecretBuffer bytes;
#ifndef _WIN32
    const int fd = open(ref.reference.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return {};
    struct Close { int fd; ~Close() { close(fd); } } close_file{fd};
    struct stat info{};
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size <= 0 || info.st_size > 4096) return {};
    std::array<char, 4097> buffer{};
    const auto n = read(fd, buffer.data(), buffer.size());
    if (n > 0 && n <= 4096) bytes.value.assign(buffer.data(), size_t(n));
    volatile char* p = buffer.data(); for (size_t i = 0; i < buffer.size(); ++i) p[i] = 0;
    if (n <= 0 || n != info.st_size) return {};
#else
    std::error_code ec;
    if (!std::filesystem::is_regular_file(ref.reference, ec)) return {};
    std::ifstream file(ref.reference, std::ios::binary);
    for (char c; bytes.value.size() <= 4096 && file.get(c);) bytes.value += c;
    if (!file.eof() || bytes.value.empty() || bytes.value.size() > 4096) return {};
#endif
    if (!bytes.value.empty() && bytes.value.back() == '\n') bytes.value.pop_back();
    const auto colon = bytes.value.find(':');
    if (colon == std::string::npos || !colon || colon + 1 == bytes.value.size() || !CleanSecret(bytes.value)) return {};
    return Credentials(bytes.value.substr(0, colon), bytes.value.substr(colon + 1));
}
const char* ErrorName(Error error)
{
    switch (error) {
    case Error::None: return "success";
    case Error::InvalidProfile: return "invalid-profile";
    case Error::InvalidRequest: return "invalid-request";
    case Error::CredentialsUnavailable: return "credentials-unavailable";
    case Error::Configuration: return "transport-unavailable";
    case Error::Cancelled: return "cancelled";
    case Error::Timeout: return "timeout";
    case Error::Resolve: return "dns";
    case Error::Proxy: return "proxy";
    case Error::Tls: return "tls";
    case Error::Connection: return "connection";
    case Error::Http: return "http";
    case Error::Authentication: return "authentication";
    case Error::Redirect: return "redirect";
    case Error::Limit: return "limit";
    case Error::Json: return "json";
    case Error::Identity: return "response-identity";
    case Error::Rpc: return "rpc";
    }
    return "transport-unavailable";
}
Reply Transport::Call(const ConnectionProfile& profile, const Request& request, std::stop_token stop, Limits limits) const
{
    if (request.method=="walletpassphrase") return {.error=Error::InvalidRequest};
    return Call(profile,request,stop,limits,{},0);
}
Reply Transport::UnlockWallet(const ConnectionProfile& profile,std::string_view wallet,
    std::string_view passphrase,unsigned seconds,std::stop_token stop) const
{
    if (passphrase.empty() || passphrase.size()>255 || !ValidUtf8(passphrase)
        || passphrase.find('\0')!=passphrase.npos || !seconds || seconds>60)
        return {.error=Error::InvalidRequest};
    if (!profile.tls && (profile.proxy!=ProxyKind::Direct
        || (profile.host!="127.0.0.1" && profile.host!="::1")))
        return {.error=Error::InvalidProfile};
    Limits limits;
    limits.response_bytes=16*1024;
    return Call(profile,{1,"walletpassphrase","[]",std::string(wallet),Effect::Mutation},
        stop,limits,passphrase,seconds);
}
Reply Transport::Call(const ConnectionProfile& profile,const Request& request,std::stop_token stop,
    Limits limits,std::string_view passphrase,unsigned seconds) const
{
    Reply reply;
    const auto fail = [&reply](Error error) { reply.error = error; return reply; };
    if (!ValidConnectionProfile(profile)) return fail(Error::InvalidProfile);
    if (!ValidLimits(limits) || request.params.size() > limits.request_bytes || request.method.empty() || request.method.size() > 128 ||
        !std::all_of(request.method.begin(), request.method.end(), [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }) ||
        (request.wallet && (request.wallet->size() > 1024 || !ValidUtf8(*request.wallet) || request.wallet->find('\0') != std::string::npos))) return fail(Error::InvalidRequest);
    if (passphrase.empty()) {
        const auto params=Json::Parse(request.params,limits.json_depth,limits.json_nodes);
        if (!params || (params->kind!=Json::Kind::Array && params->kind!=Json::Kind::Object)) return fail(Error::InvalidRequest);
    }
    const auto id = std::to_string(request.id);
    SecretBuffer protected_body;
    auto& body=protected_body.value;
    if (!passphrase.empty()) body.reserve(160+passphrase.size()*6);
    body.append("{\"jsonrpc\":\"2.0\",\"id\":").append(id).append(",\"method\":");
    AppendQuoted(body,request.method);
    body.append(",\"params\":");
    if (passphrase.empty()) body.append(request.params);
    else {
        body.append("{\"passphrase\":");
        AppendQuoted(body,passphrase);
        body.append(",\"timeout\":").append(std::to_string(seconds)).append("}");
    }
    body += '}';
    if (body.size() > limits.request_bytes) return fail(Error::Limit);
    if (stop.stop_requested()) return fail(Error::Cancelled);
    // curl/OpenSSL honor this environment variable and can export TLS secrets.
    // Refuse that configuration before initialization; never change process
    // environment from a worker or emit the supplied path in diagnostics.
    if (const auto* keylog = std::getenv("SSLKEYLOGFILE"); keylog && *keylog) return fail(Error::Configuration);
    static CurlRuntime runtime;
    if (runtime.status != CURLE_OK || !(curl_version_info(CURLVERSION_NOW)->features & CURL_VERSION_ASYNCHDNS)) return fail(Error::Configuration);
    const std::string origin = std::string(profile.tls ? "https://" : "http://") + HostPort(profile.host, profile.port);
    const std::string url = origin + (request.wallet ? "/wallet/" + EncodePath(*request.wallet) : "/");
    auto credential = m_credentials.Resolve(profile.credential, origin);
    if (!credential || credential->username.empty() || credential->password.empty() || credential->username.find(':') != std::string::npos || !CleanSecret(credential->username) || !CleanSecret(credential->password)) return fail(Error::CredentialsUnavailable);
    if (stop.stop_requested()) return fail(Error::Cancelled);
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(curl_easy_init(), curl_easy_cleanup);
    std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)> multi(curl_multi_init(), curl_multi_cleanup);
    if (!easy || !multi) return fail(Error::Configuration);
    Receive received{{}, 0, limits.response_bytes, limits.header_bytes};
    struct ClearResponse {
        Receive& received;
        bool sensitive;
        ~ClearResponse() { if (sensitive) Clear(received.body); }
    } clear_response{received,!passphrase.empty()};
    if (clear_response.sensitive) received.body.reserve(limits.response_bytes);
    bool options_ok = true;
    const auto option = [&]<typename T>(CURLoption key, T value) { if (curl_easy_setopt(easy.get(), key, value) != CURLE_OK) options_ok = false; };
    option(CURLOPT_URL, url.c_str());
    option(CURLOPT_PATH_AS_IS, 1L); // daemon-wallet names are opaque components
    option(CURLOPT_PROTOCOLS_STR, "http,https");
    option(CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    option(CURLOPT_FOLLOWLOCATION, 0L);
    option(CURLOPT_UNRESTRICTED_AUTH, 0L);
    option(CURLOPT_NETRC, long(CURL_NETRC_IGNORED));
    option(CURLOPT_HTTPAUTH, long(CURLAUTH_BASIC));
    option(CURLOPT_USERNAME, credential->username.c_str());
    option(CURLOPT_PASSWORD, credential->password.c_str());
    option(CURLOPT_PROXY, ""); // never inherit http_proxy/ALL_PROXY
    option(CURLOPT_NOPROXY, ""); // explicit proxy may not be bypassed by NO_PROXY
    const std::string proxy = profile.proxy == ProxyKind::Direct ? "" :
        std::string(profile.proxy == ProxyKind::Socks5RemoteDns ? "socks5h://" : "http://") + HostPort(profile.proxy_host, profile.proxy_port);
    if (!proxy.empty()) { option(CURLOPT_PROXY, proxy.c_str()); option(CURLOPT_HTTPPROXYTUNNEL, 1L); }
    option(CURLOPT_SSL_VERIFYPEER, 1L);
    option(CURLOPT_SSL_VERIFYHOST, 2L);
    if (!profile.trusted_ca_file.empty()) option(CURLOPT_CAINFO, profile.trusted_ca_file.c_str());
    option(CURLOPT_NOSIGNAL, 1L);
    option(CURLOPT_CONNECTTIMEOUT_MS, long(limits.connect_timeout.count()));
    option(CURLOPT_TIMEOUT_MS, long(limits.total_timeout.count()));
    option(CURLOPT_MAXFILESIZE_LARGE, curl_off_t(limits.response_bytes));
    option(CURLOPT_POST, 1L);
    option(CURLOPT_HTTP_VERSION, long(CURL_HTTP_VERSION_1_1));
    option(CURLOPT_POSTFIELDS, body.data());
    option(CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(body.size()));
    option(CURLOPT_WRITEFUNCTION, &Receive::Body); option(CURLOPT_WRITEDATA, &received);
    option(CURLOPT_HEADERFUNCTION, &Receive::Header); option(CURLOPT_HEADERDATA, &received);
    // New isolated handle/connection per call: no retained credentials, cookies,
    // Alt-Svc, HSTS, redirects or reused-connection request replay.
    option(CURLOPT_FRESH_CONNECT, 1L); option(CURLOPT_FORBID_REUSE, 1L);
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(curl_slist_append(nullptr, "Content-Type: application/json"), curl_slist_free_all);
    if (!headers) return fail(Error::Configuration);
    // Suppress Expect negotiation: a 417 must never cause an automatic replay.
    auto* extended_headers = curl_slist_append(headers.get(), "Expect:");
    if (!extended_headers) return fail(Error::Configuration);
    headers.release(); headers.reset(extended_headers);
    option(CURLOPT_HTTPHEADER, headers.get());
    if (!options_ok || curl_multi_add_handle(multi.get(), easy.get()) != CURLM_OK) return fail(Error::Configuration);
    struct Remove { CURLM* multi; CURL* easy; ~Remove() { curl_multi_remove_handle(multi, easy); } } remove{multi.get(), easy.get()};
    reply.mutation_uncertain = request.effect == Effect::Mutation;
    CURLcode completed = CURLE_FAILED_INIT;
    int running = 1;
    const auto deadline = std::chrono::steady_clock::now() + limits.total_timeout;
    while (running) {
        if (stop.stop_requested()) return fail(Error::Cancelled);
        if (std::chrono::steady_clock::now() >= deadline) return fail(Error::Timeout);
        if (curl_multi_perform(multi.get(), &running) != CURLM_OK) return fail(Error::Connection);
        if (running && curl_multi_poll(multi.get(), nullptr, 0, 50, nullptr) != CURLM_OK) return fail(Error::Connection);
    }
    int remaining{};
    while (auto* message = curl_multi_info_read(multi.get(), &remaining)) if (message->msg == CURLMSG_DONE) completed = message->data.result;
    curl_easy_getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &reply.http_status);
    if (stop.stop_requested()) return fail(Error::Cancelled);
    if (received.limit) return fail(Error::Limit);
    if (completed != CURLE_OK) return fail(received.allocation_failed ? Error::Configuration :
        (completed == CURLE_COULDNT_CONNECT && profile.proxy != ProxyKind::Direct ? Error::Proxy : CurlError(completed)));
    if (reply.http_status == 401) { reply.mutation_uncertain = false; return fail(Error::Authentication); }
    if (reply.http_status >= 300 && reply.http_status < 400) return fail(Error::Redirect);
    if (reply.http_status != 200) return fail(Error::Http);
    auto response = Json::Parse(received.body, limits.json_depth, limits.json_nodes);
    struct ClearParsed {
        std::optional<Json>& response;
        bool sensitive;
        ~ClearParsed() { if (sensitive && response) ClearJson(*response); }
    } clear_parsed{response,!passphrase.empty()};
    if (!response || response->kind != Json::Kind::Object) return fail(Error::Json);
    const auto* version = response->Find("jsonrpc"); const auto* response_id = response->Find("id");
    if (!version || version->kind != Json::Kind::String || version->text != "2.0" || !response_id || response_id->kind != Json::Kind::Number || response_id->text != id) return fail(Error::Identity);
    const auto* result = response->Find("result"); const auto* error = response->Find("error");
    if (error && error->kind != Json::Kind::Null) {
        if (result) return fail(Error::Json);
        const auto* code = error->Find("code"); const auto* message = error->Find("message");
        if (!code || code->kind != Json::Kind::Number || !message || message->kind != Json::Kind::String) return fail(Error::Json);
        int32_t number{}; const auto parsed = std::from_chars(code->text.data(), code->text.data() + code->text.size(), number);
        if (parsed.ec != std::errc{} || parsed.ptr != code->text.data() + code->text.size()) return fail(Error::Json);
        // A remote error need not roll back an operation's earlier side effects.
        reply.rpc_code = number;
        if (request.method == "enumeratesigners" || request.method == "walletdisplayaddress") {
            // Never publish remote error strings. Only these exact messages
            // are useful as evidence of an external action or device state.
            const auto& note = message->text;
            if (note == "Error: restart bitcoind with -signer=<cmd>" || note == "restart bitcoind with -signer=<cmd>")
                reply.signer_notice = SignerNotice::Unconfigured;
            else if (note == "No external signers found") reply.signer_notice = SignerNotice::Absent;
            else if (note == "More than one external signer found. Please connect only one at a time.")
                reply.signer_notice = SignerNotice::Multiple;
            else if (request.method == "walletdisplayaddress" && note == "Signer returned error: Device is locked")
                reply.signer_notice = SignerNotice::Locked;
            else if (request.method == "walletdisplayaddress" && note == "Signer returned error: Passphrase required")
                reply.signer_notice = SignerNotice::Passphrase;
            else if (request.method == "walletdisplayaddress" && note == "Signer returned error: Device disconnected")
                reply.signer_notice = SignerNotice::Disconnected;
        }
        if (request.method == "walletprocesspsbt" && message->text == "External signer not found")
            reply.signer_notice = SignerNotice::Disconnected;
        return fail(Error::Rpc);
    }
    if (!result) return fail(Error::Json);
    if (!passphrase.empty() && result->kind!=Json::Kind::Null) return fail(Error::Json);
    reply.result = std::move(*response->Find("result")); reply.mutation_uncertain = false;
    return reply;
}
}
