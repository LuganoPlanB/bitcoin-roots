// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/download_verification.h"

#include "adapters/protocol/download_format.h"
#include "services/transaction_documents.h"
#include <algorithm>
#include <chrono>
#include <utility>

#if defined(__linux__) && defined(ROOTS_GUI_GNUPG_SHA256)
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sodium.h>
#include <spawn.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#endif

namespace roots::gui {
namespace {
using Issue = DownloadIssue;
using Stage = DownloadStage;
using Clock = std::chrono::steady_clock;

#if defined(__linux__) && defined(ROOTS_GUI_GNUPG_SHA256)
constexpr std::string_view GPG_PATH = "/usr/bin/gpg";
constexpr size_t STATUS_MAX = 32 * 1024;
constexpr uint64_t ARTIFACT_MAX = 8ULL * 1024 * 1024 * 1024;
constexpr std::array<uint64_t, 4> FILE_LIMITS{ARTIFACT_MAX, 1024 * 1024, 64 * 1024, 256 * 1024};

class File {
    int fd{-1};
public:
    File() = default;
    explicit File(int opened) : fd(opened) {}
    File(File&& other) noexcept : fd(std::exchange(other.fd, -1)) {}
    File& operator=(File&& other) noexcept
    {
        if (this != &other) {
            if (fd >= 0) ::close(fd);
            fd = std::exchange(other.fd, -1);
        }
        return *this;
    }
    ~File() { if (fd >= 0) ::close(fd); }
    int Get() const { return fd; }
    explicit operator bool() const { return fd >= 0; }
    std::string ProcPath() const { return "/proc/self/fd/" + std::to_string(fd); }
};

bool SameStat(const struct stat& a, const struct stat& b)
{
    return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_mode == b.st_mode && a.st_size == b.st_size
        && a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec
        && a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
}

struct OpenedInput {
    File fd;
    struct stat stamp{};
    bool Unchanged() const
    {
        struct stat now{};
        return ::fstat(fd.Get(), &now) == 0 && SameStat(stamp, now);
    }
};

class Watch final : public DownloadSnapshot {
    struct Rule { int id; std::string name; };
    File fd{::inotify_init1(IN_NONBLOCK | IN_CLOEXEC)};
    std::vector<Rule> rules;
    bool invalid{};
public:
    bool Add(const File& source, std::string_view child = {})
    {
        constexpr uint32_t SELF = IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT;
        constexpr uint32_t FILE = IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE;
        constexpr uint32_t DIR = IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB;
        if (!fd || rules.size() >= 260) return false;
        const int watched = ::inotify_add_watch(fd.Get(), source.ProcPath().c_str(), SELF | (child.empty() ? FILE : DIR) | IN_MASK_ADD);
        if (watched < 0) return false;
        rules.push_back({watched, std::string(child)});
        return true;
    }

    bool Changed() override
    {
        if (invalid || !fd) return true;
        alignas(inotify_event) std::array<char, 4096> bytes{};
        for (unsigned batch = 0; batch < 4; ++batch) {
            const auto count = ::read(fd.Get(), bytes.data(), bytes.size());
            if (count < 0 && errno == EAGAIN) return false;
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            size_t offset{};
            while (offset + sizeof(inotify_event) <= size_t(count)) {
                const auto& event = *reinterpret_cast<const inotify_event*>(bytes.data() + offset);
                offset += sizeof(inotify_event);
                if (event.len > size_t(count) - offset) return invalid = true;
                const std::string_view name = event.len ? std::string_view(bytes.data() + offset, ::strnlen(bytes.data() + offset, event.len)) : std::string_view{};
                if (event.mask & (IN_Q_OVERFLOW | IN_IGNORED | IN_UNMOUNT)) return invalid = true;
                for (const auto& rule : rules) {
                    if (rule.id == event.wd && (rule.name.empty() || !event.len || name == rule.name)) return invalid = true;
                }
                offset += event.len;
            }
            if (offset != size_t(count)) return invalid = true;
        }
        return invalid = true;
    }
};

std::optional<OpenedInput> OpenInput(std::string_view path, Watch* watch = nullptr)
{
    if (!ValidLocalDocumentPath(path)) return {};
    const std::filesystem::path target(path);
    File directory(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    size_t depth{};
    for (const auto& part : target.parent_path().relative_path()) {
        if (!directory || ++depth > 63 || part.empty() || part == "." || part == "..") return {};
        if (watch && !watch->Add(directory, part.native())) return {};
        directory = File(::openat(directory.Get(), part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    }
    const auto leaf = target.filename();
    if (!directory || leaf.empty() || leaf == "." || leaf == ".." || (watch && !watch->Add(directory, leaf.native()))) return {};
    OpenedInput result{File(::openat(directory.Get(), leaf.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)), {}};
    if (!result.fd || ::fstat(result.fd.Get(), &result.stamp) || !S_ISREG(result.stamp.st_mode) || result.stamp.st_size < 0) return {};
    if (watch && !watch->Add(result.fd)) return {};
    return result;
}

struct Budget {
    std::stop_token stop;
    Clock::time_point deadline{Clock::now() + std::chrono::seconds(90)};
    Issue Check() const
    {
        if (stop.stop_requested()) return Issue::Cancelled;
        return Clock::now() >= deadline ? Issue::Limit : Issue::None;
    }
};

Issue Read(OpenedInput& input, uint64_t limit, const Budget& budget, const auto& consume)
{
    const auto length = uint64_t(input.stamp.st_size);
    if (length > limit) return Issue::Limit;
    std::array<char, 64 * 1024> buffer{};
    uint64_t remaining = length;
    while (remaining) {
        if (const auto issue = budget.Check(); issue != Issue::None) return issue;
        const auto count = ::read(input.fd.Get(), buffer.data(), std::min<uint64_t>(remaining, buffer.size()));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return Issue::Unreadable;
        if (count == 0) return Issue::Changed;
        consume(std::string_view(buffer.data(), size_t(count)));
        remaining -= uint64_t(count);
    }
    // Pseudo-files can report a regular-file mode and a size of zero despite
    // exposing bytes. Reject any stream that differs from the captured size.
    char extra{};
    for (;;) {
        if (const auto issue = budget.Check(); issue != Issue::None) return issue;
        const auto count = ::read(input.fd.Get(), &extra, 1);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return Issue::Unreadable;
        if (count != 0) return Issue::Changed;
        break;
    }
    if (!input.Unchanged()) return Issue::Changed;
    return budget.Check();
}

Issue Hash(OpenedInput& input, uint64_t limit, const Budget& budget, std::string& output)
{
    crypto_hash_sha256_state sha;
    crypto_hash_sha256_init(&sha);
    const auto issue = Read(input, limit, budget, [&](std::string_view block) {
        crypto_hash_sha256_update(&sha, reinterpret_cast<const unsigned char*>(block.data()), block.size());
    });
    if (issue != Issue::None) return issue;
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    std::array<char, crypto_hash_sha256_BYTES * 2 + 1> hex{};
    crypto_hash_sha256_final(&sha, digest.data());
    sodium_bin2hex(hex.data(), hex.size(), digest.data(), digest.size());
    output = hex.data();
    return Issue::None;
}

class Workspace {
    std::filesystem::path path;
    File directory;
public:
    Workspace()
    {
        char name[] = "/tmp/roots-gui-verify-XXXXXX";
        if (::mkdtemp(name)) {
            path = name;
            directory = File(::open(name, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        }
    }
    ~Workspace()
    {
        if (!path.empty()) {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    }
    bool Write(const char* name, std::string_view data) const
    {
        if (!directory) return false;
        File output(::openat(directory.Get(), name, O_CREAT | O_EXCL | O_NOFOLLOW | O_WRONLY | O_CLOEXEC, 0600));
        if (!output) return false;
        while (!data.empty()) {
            const auto count = ::write(output.Get(), data.data(), data.size());
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) return false;
            data.remove_prefix(size_t(count));
        }
        return ::fchmod(output.Get(), 0400) == 0;
    }
    std::string Path() const { return path.string(); }
    std::string Path(const char* name) const { return (path / name).string(); }
};

class Child {
    pid_t pid{-1};
public:
    void Adopt(pid_t owned) { pid = owned; }
    ~Child()
    {
        if (pid <= 0) return;
        ::kill(-pid, SIGKILL);
        ::kill(pid, SIGKILL);
        while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
    }
    int Poll(int& status)
    {
        const auto reaped = ::waitpid(pid, &status, WNOHANG);
        if (reaped == pid || (reaped < 0 && errno == ECHILD)) pid = -1;
        if (reaped < 0 && errno == EINTR) return 0;
        return reaped < 0 ? -1 : reaped > 0 ? 1 : 0;
    }
};

struct StatusResult { std::string data; int exit{}; };
std::optional<StatusResult> VerifyGpg(const File& executable, const Workspace& scratch, const Budget& budget)
{
    int descriptors[2]{};
    if (::pipe2(descriptors, O_CLOEXEC) != 0) return {};
    File read_end(descriptors[0]), write_end(descriptors[1]);
    if (::fcntl(read_end.Get(), F_SETFL, O_NONBLOCK) != 0) return {};
    File null(::open("/dev/null", O_RDWR | O_CLOEXEC));
    if (!null || read_end.Get() <= STDERR_FILENO || write_end.Get() <= STDERR_FILENO || null.Get() <= STDERR_FILENO) return {};
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attrs;
    if (::posix_spawn_file_actions_init(&actions)) return {};
    const int init_attrs = ::posix_spawnattr_init(&attrs);
    const bool configured = !init_attrs
        && !::posix_spawnattr_setflags(&attrs, POSIX_SPAWN_SETPGROUP)
        && !::posix_spawnattr_setpgroup(&attrs, 0)
        && !::posix_spawn_file_actions_adddup2(&actions, null.Get(), STDIN_FILENO)
        && !::posix_spawn_file_actions_adddup2(&actions, write_end.Get(), STDOUT_FILENO)
        && !::posix_spawn_file_actions_adddup2(&actions, null.Get(), STDERR_FILENO)
        && !::posix_spawn_file_actions_adddup2(&actions, executable.Get(), 3)
        && !::posix_spawn_file_actions_addclosefrom_np(&actions, 4);
    std::vector<std::string> args{std::string(GPG_PATH), "--no-options", "--homedir", scratch.Path(), "--batch", "--no-tty",
        "--no-default-keyring", "--keyring", scratch.Path("public.gpg"), "--trust-model", "always",
        "--no-auto-check-trustdb", "--no-auto-key-import", "--no-auto-key-retrieve", "--no-auto-key-locate",
        "--disable-dirmngr", "--no-autostart", "--lock-never", "--no-random-seed-file", "--weak-digest", "SHA1",
        "--status-fd", "1", "--verify", "--", scratch.Path("detached.sig"), scratch.Path("manifest")};
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    char locale[] = "LC_ALL=C", timezone[] = "TZ=UTC", path[] = "PATH=/usr/bin:/bin";
    char* const environment[]{locale, timezone, path, nullptr};
    pid_t pid{-1};
    const int spawned = configured ? ::posix_spawn(&pid, "/proc/self/fd/3", &actions, &attrs,
                                                   argv.data(), environment) : -1;
    if (!init_attrs) ::posix_spawnattr_destroy(&attrs);
    ::posix_spawn_file_actions_destroy(&actions);
    if (spawned) return {};
    Child child;
    child.Adopt(pid);
    write_end = {};

    const auto deadline = std::min(budget.deadline, Clock::now() + std::chrono::seconds(12));
    StatusResult output;
    bool eof{};
    while (Clock::now() < deadline && budget.Check() == Issue::None) {
        if (!eof) {
            std::array<char, 4096> block{};
            const auto amount = ::read(read_end.Get(), block.data(), block.size());
            if (amount > 0) {
                if (size_t(amount) > STATUS_MAX - output.data.size()) return {};
                output.data.append(block.data(), size_t(amount));
            } else if (!amount) eof = true;
            else if (errno != EAGAIN && errno != EINTR) return {};
        }
        // Keep the child unreaped while anyone holds its pipe, so timeout
        // can still retire its owned process group without a reused PID.
        if (eof) {
            int status{};
            const auto state = child.Poll(status);
            if (state < 0 || (state > 0 && !WIFEXITED(status))) return {};
            if (state > 0) {
                output.exit = WEXITSTATUS(status);
                return output;
            }
        }
        struct pollfd waiting{read_end.Get(), POLLIN, 0};
        if (::poll(&waiting, eof ? 0 : 1, 25) < 0 && errno != EINTR) return {};
    }
    return {};
}

DownloadResult Verify(const DownloadSelections& selected, std::stop_token stop, std::atomic<Stage>& stage)
{
    const Budget budget{stop};
    DownloadResult result;
    auto failed = [](Issue issue) { DownloadResult failure; failure.issue = issue; return failure; };
    if (sodium_init() < 0) return failed(Issue::Unavailable);

    auto lease = std::make_unique<Watch>();
    std::array<OpenedInput, 4> inputs{};
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (const auto issue = budget.Check(); issue != Issue::None) return failed(issue);
        auto input = OpenInput(selected.files[i], lease.get());
        if (!input) return failed(Issue::Unreadable);
        if (uint64_t(input->stamp.st_size) > FILE_LIMITS[i]) return failed(Issue::Limit);
        for (size_t previous = 0; previous < i; ++previous) {
            if (inputs[previous].stamp.st_dev == input->stamp.st_dev && inputs[previous].stamp.st_ino == input->stamp.st_ino)
                return failed(Issue::Unreadable);
        }
        inputs[i] = std::move(*input);
    }

    std::array<std::string, 3> control;
    for (size_t i = 1; i < inputs.size(); ++i) {
        auto& content = control[i - 1];
        const auto issue = Read(inputs[i], FILE_LIMITS[i], budget, [&](std::string_view bytes) { content.append(bytes); });
        if (issue != Issue::None) return failed(issue);
    }
    const auto& manifest = control[0];
    const auto entry = protocol::FindManifestEntry(manifest, std::filesystem::path(selected.files[0]).filename().native());
    result.integrity = entry.outcome;
    if (!entry.hash.empty()) {
        stage.store(Stage::HashArtifact, std::memory_order_relaxed);
        const auto issue = Hash(inputs[0], FILE_LIMITS[0], budget, result.artifact_hash);
        if (issue != Issue::None) return failed(issue);
        result.manifest_hash = entry.hash;
        std::ranges::transform(result.manifest_hash, result.manifest_hash.begin(), [](char c) {
            return c >= 'A' && c <= 'F' ? c - 'A' + 'a' : c;
        });
        result.integrity = result.artifact_hash == result.manifest_hash ? DownloadIntegrity::Match : DownloadIntegrity::Mismatch;
    }

    stage.store(Stage::VerifyManifest, std::memory_order_relaxed);
    auto signature = protocol::DetachedOpenPgpSignature(control[1]);
    auto key = protocol::PublicOpenPgpKey(control[2]);
    if (!signature || !key) result.signature = DownloadSignature::Unsupported;
    else {
        auto executable = OpenInput(GPG_PATH);
        std::string executable_hash;
        if (!executable || Hash(*executable, 16 * 1024 * 1024, budget, executable_hash) != Issue::None
            || executable_hash != ROOTS_GUI_GNUPG_SHA256) result.issue = Issue::Unavailable;
        else {
            Workspace scratch;
            if (scratch.Write("manifest", manifest) && scratch.Write("detached.sig", *signature)
                && scratch.Write("public.gpg", *key)) {
                const auto status = VerifyGpg(executable->fd, scratch, budget);
                if (status && executable->Unchanged()) protocol::InterpretOpenPgpStatus(status->data, status->exit, selected.expected_fingerprint, result);
                else result.signature = DownloadSignature::VerifierError;
            } else result.signature = DownloadSignature::VerifierError;
        }
    }

    if (const auto issue = budget.Check(); issue != Issue::None) return failed(issue);
    if (lease->Changed()) return failed(Issue::Changed);
    for (size_t i = 0; i < inputs.size(); ++i) {
        const auto again = OpenInput(selected.files[i]);
        if (!inputs[i].Unchanged() || !again || !SameStat(inputs[i].stamp, again->stamp)) return failed(Issue::Changed);
    }
    if (lease->Changed()) return failed(Issue::Changed);
    result.files = std::move(lease);
    return result;
}
#endif

} // namespace

bool DownloadVerifierConfigured()
{
#if defined(__linux__) && defined(ROOTS_GUI_GNUPG_SHA256)
    return true;
#else
    return false;
#endif
}

DownloadVerifier::~DownloadVerifier()
{
    Cancel();
    if (worker.joinable()) worker.join();
}

bool DownloadVerifier::Start(DownloadRequest request)
{
#if defined(__linux__) && defined(ROOTS_GUI_GNUPG_SHA256)
    if (owner != std::this_thread::get_id() || worker.joinable()) return false;
    if (!request.selections.expected_fingerprint.empty()) {
        auto expected = DownloadFingerprint(request.selections.expected_fingerprint);
        if (!expected) return false;
        request.selections.expected_fingerprint = std::move(*expected);
    }
    active = request;
    stage.store(Stage::ReadInputs, std::memory_order_relaxed);
    try {
        worker = std::jthread([this, input = std::move(request.selections)](std::stop_token stop) {
            DownloadResult result;
            try { result = Verify(input, stop, stage); }
            catch (...) { result.issue = Issue::Unreadable; }
            std::lock_guard guard(mutex);
            completed = std::move(result);
        });
    } catch (...) {
        active.reset();
        stage.store(Stage::Idle, std::memory_order_relaxed);
        return false;
    }
    return true;
#else
    return false;
#endif
}

std::optional<DownloadResult> DownloadVerifier::Poll(const DownloadRequest* request)
{
    if (owner != std::this_thread::get_id() || !worker.joinable()) return {};
    // The editor normalizes the fingerprint before submitting and polling.
    if (active && (!request || *active != *request)) Cancel();
    std::optional<DownloadResult> output;
    {
        std::lock_guard guard(mutex);
        if (!completed) return {};
        output = std::exchange(completed, {});
    }
    worker.join();
    stage.store(Stage::Idle, std::memory_order_relaxed);
    const bool accept = active.has_value();
    active.reset();
    if (!accept) return {};
    if (output->Changed()) {
        DownloadResult changed;
        changed.issue = Issue::Changed;
        return changed;
    }
    return output;
}

void DownloadVerifier::Cancel()
{
    if (owner != std::this_thread::get_id()) return;
    if (worker.joinable()) worker.request_stop();
    active.reset();
}

const char* DownloadText(DownloadIntegrity value)
{
    switch (value) {
    case DownloadIntegrity::Unchecked: return "Unchecked";
    case DownloadIntegrity::Match: return "SHA-256 matches the exact manifest entry.";
    case DownloadIntegrity::Mismatch: return "SHA-256 does not match the exact manifest entry.";
    case DownloadIntegrity::MissingEntry: return "The manifest has no entry for this exact filename.";
    case DownloadIntegrity::AmbiguousEntry: return "The manifest contains duplicate filenames.";
    case DownloadIntegrity::UnsupportedManifest: return "Use a flat SHA-256 manifest with unambiguous filenames.";
    }
    return "Unavailable";
}

const char* DownloadText(DownloadSignature value)
{
    switch (value) {
    case DownloadSignature::Unchecked: return "Unchecked";
    case DownloadSignature::Valid: return "Valid for the exact manifest bytes.";
    case DownloadSignature::Invalid: return "Signature does not match the manifest.";
    case DownloadSignature::UnknownSigner: return "The selected public key cannot verify this signer.";
    case DownloadSignature::Unsupported: return "Unsupported signature or public key format.";
    case DownloadSignature::VerifierError: return "The verifier did not return a usable result.";
    }
    return "Unavailable";
}

const char* DownloadText(DownloadTrust value)
{
    switch (value) {
    case DownloadTrust::Unestablished: return "No signer trust established.";
    case DownloadTrust::ExplicitFingerprint: return "Primary signer matches your independently supplied fingerprint.";
    case DownloadTrust::FingerprintMismatch: return "The verified signer differs from your expected fingerprint.";
    case DownloadTrust::ExpiredKey: return "The signing key has expired.";
    case DownloadTrust::ExpiredSignature: return "The signature has expired.";
    case DownloadTrust::RevokedKey: return "The selected certificate reports a revoked signing key.";
    }
    return "Unavailable";
}

const char* DownloadText(DownloadIssue value)
{
    switch (value) {
    case Issue::None: return "";
    case Issue::Unavailable: return "The build-pinned GnuPG 2.4.7 verifier is missing or has changed.";
    case Issue::Unreadable: return "Choose distinct, readable local regular files without symbolic links.";
    case Issue::Limit: return "The file size or verification time limit was exceeded.";
    case Issue::Changed: return "A selected file changed. Verify the new files.";
    case Issue::Cancelled: return "Verification cancelled.";
    }
    return "";
}

const char* DownloadText(DownloadStage value)
{
    switch (value) {
    case Stage::Idle: return "";
    case Stage::ReadInputs: return "Reading selected files…";
    case Stage::HashArtifact: return "Hashing release file…";
    case Stage::VerifyManifest: return "Checking the detached signature…";
    }
    return "";
}

} // namespace roots::gui
