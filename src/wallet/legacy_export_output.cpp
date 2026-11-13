// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/legacy_export.h>

#include <random.h>

#include <cerrno>
#include <cstring>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <utility>

#include <fcntl.h>

#ifndef WIN32
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/syscall.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <sys/sysmacros.h>
#endif
#else
#include <io.h>
#include <windows.h>
#include <aclapi.h>
#include <winternl.h>
#endif

namespace wallet {
namespace {
void OutputRequire(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

#ifndef WIN32
struct OutputIdentity {
    dev_t device;
    ino_t inode;
    bool operator==(const OutputIdentity&) const = default;
};

OutputIdentity OutputId(const struct stat& st) { return {st.st_dev, st.st_ino}; }

class OutputFd
{
    int m_fd{-1};
public:
    explicit OutputFd(int fd = -1) : m_fd(fd) {}
    ~OutputFd() { if (m_fd != -1) close(m_fd); }
    OutputFd(const OutputFd&) = delete;
    OutputFd& operator=(const OutputFd&) = delete;
    int Get() const { return m_fd; }
    int Release() { return std::exchange(m_fd, -1); }
    void Reset(int fd) { if (m_fd != -1) close(m_fd); m_fd = fd; }
};

struct stat OutputStat(int fd)
{
    struct stat st{};
    OutputRequire(fstat(fd, &st) == 0, "Cannot inspect legacy output identity");
    return st;
}

// Neither RenameOver (overwrites) nor FileCommit (accepts unsupported sync on
// some filesystems) provides this export's guarantees. No link/unlink fallback:
// it would introduce a fallible cleanup after the publication commit point.
bool MoveOutputNoReplace(int directory, const char* from, const char* to)
{
#if defined(__linux__) && defined(SYS_renameat2)
    return syscall(SYS_renameat2, directory, from, directory, to, RENAME_NOREPLACE) == 0;
#elif defined(__APPLE__) && defined(RENAME_EXCL)
    return renameatx_np(directory, from, directory, to, RENAME_EXCL) == 0;
#else
    return false;
#endif
}
#else
struct OutputIdentity {
    DWORD volume;
    DWORD high;
    DWORD low;
    bool operator==(const OutputIdentity&) const = default;
};

class OutputHandle
{
    HANDLE m_handle{INVALID_HANDLE_VALUE};
public:
    explicit OutputHandle(HANDLE handle = INVALID_HANDLE_VALUE) : m_handle(handle) {}
    ~OutputHandle() { if (m_handle != INVALID_HANDLE_VALUE) CloseHandle(m_handle); }
    OutputHandle(const OutputHandle&) = delete;
    OutputHandle& operator=(const OutputHandle&) = delete;
    OutputHandle(OutputHandle&& other) noexcept : m_handle(other.Release()) {}
    OutputHandle& operator=(OutputHandle&& other) noexcept
    {
        if (this != &other) { OutputHandle old{Release()}; m_handle = other.Release(); }
        return *this;
    }
    HANDLE Get() const { return m_handle; }
    HANDLE Release() { return std::exchange(m_handle, INVALID_HANDLE_VALUE); }
};

BY_HANDLE_FILE_INFORMATION OutputInformation(HANDLE handle)
{
    BY_HANDLE_FILE_INFORMATION info{};
    OutputRequire(GetFileType(handle) == FILE_TYPE_DISK && GetFileInformationByHandle(handle, &info),
                  "Cannot inspect legacy output identity");
    return info;
}

OutputIdentity OutputId(const BY_HANDLE_FILE_INFORMATION& info)
{
    return {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow};
}

// SDK declaration supplies the ABI. Resolve the system's already loaded ntdll;
// no new runtime/dependency or unchecked Win32 pathname fallback is introduced.
using OutputCreateFile = decltype(&NtCreateFile);
OutputCreateFile NativeOutputCreate()
{
    const auto library{GetModuleHandleW(L"ntdll.dll")};
    const auto create{library ? reinterpret_cast<OutputCreateFile>(GetProcAddress(library, "NtCreateFile")) : nullptr};
    OutputRequire(create != nullptr, "Handle-relative legacy output is unsupported");
    return create;
}

struct OutputOpenResult {
    NTSTATUS status;
    ULONG_PTR disposition;
    OutputHandle handle;
};

OutputOpenResult OpenOutputAt(OutputCreateFile create, HANDLE parent, const std::wstring& name,
                              ACCESS_MASK access, ULONG share, ULONG disposition,
                              PSECURITY_DESCRIPTOR security = nullptr)
{
    OutputRequire(!name.empty() && name.size() <= (std::numeric_limits<USHORT>::max() / sizeof(wchar_t)),
                  "Legacy output component exceeds supported limits");
    UNICODE_STRING string{};
    string.Length = static_cast<USHORT>(name.size() * sizeof(wchar_t));
    string.MaximumLength = string.Length;
    string.Buffer = const_cast<PWSTR>(name.data());
    OBJECT_ATTRIBUTES attributes{};
    attributes.Length = sizeof(attributes);
    attributes.RootDirectory = parent;
    attributes.ObjectName = &string;
    attributes.Attributes = OBJ_CASE_INSENSITIVE;
    attributes.SecurityDescriptor = security;
    IO_STATUS_BLOCK io{};
    HANDLE handle{INVALID_HANDLE_VALUE};
    // Omit FILE_DIRECTORY_FILE: its documented compatible-option list does
    // not include FILE_OPEN_REPARSE_POINT. Inspect the returned handle's type.
    const auto status{create(&handle, access | SYNCHRONIZE, &attributes, &io, nullptr,
                             FILE_ATTRIBUTE_NORMAL, share, disposition,
                             FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT, nullptr, 0)};
    return {status, io.Information, OutputHandle{status >= 0 ? handle : INVALID_HANDLE_VALUE}};
}

std::vector<unsigned char> OutputOwnerSid()
{
    HANDLE raw{INVALID_HANDLE_VALUE};
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &raw)) {
        OutputRequire(GetLastError() == ERROR_NO_TOKEN && OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw),
                      "Cannot identify legacy output owner");
    }
    OutputHandle token{raw};
    DWORD size{0};
    GetTokenInformation(token.Get(), TokenUser, nullptr, 0, &size);
    OutputRequire(size >= sizeof(TOKEN_USER) && size <= 65536, "Cannot identify legacy output owner");
    std::vector<DWORD> storage((size + sizeof(DWORD) - 1) / sizeof(DWORD));
    OutputRequire(GetTokenInformation(token.Get(), TokenUser, storage.data(), size, &size),
                  "Cannot identify legacy output owner");
    const auto user{reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid};
    OutputRequire(IsValidSid(user), "Cannot identify legacy output owner");
    std::vector<unsigned char> sid(GetLengthSid(user));
    OutputRequire(CopySid(static_cast<DWORD>(sid.size()), sid.data(), user), "Cannot identify legacy output owner");
    return sid;
}

struct OutputSecurity {
    PSECURITY_DESCRIPTOR descriptor{nullptr};
    ~OutputSecurity() { if (descriptor) LocalFree(descriptor); }
    OutputSecurity() = default;
    OutputSecurity(const OutputSecurity&) = delete;
    OutputSecurity& operator=(const OutputSecurity&) = delete;
};

void CheckOutputAcl(HANDLE handle, PSID owner, bool private_file)
{
    PSID actual_owner{nullptr};
    PACL acl{nullptr};
    OutputSecurity security;
    OutputRequire(GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                 &actual_owner, nullptr, &acl, nullptr, &security.descriptor) == ERROR_SUCCESS &&
                      actual_owner && IsValidSid(actual_owner) && EqualSid(owner, actual_owner) && acl && IsValidAcl(acl),
                  "Legacy output requires a trusted owner-controlled ACL");
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision{0};
    OutputRequire(GetSecurityDescriptorControl(security.descriptor, &control, &revision), "Cannot inspect legacy output ACL");
    ACL_SIZE_INFORMATION info{};
    OutputRequire(GetAclInformation(acl, &info, sizeof(info), AclSizeInformation), "Cannot inspect legacy output ACL");
    if (private_file) {
        OutputRequire((control & SE_DACL_PROTECTED) && info.AceCount == 1, "Legacy output ACL is not owner-private");
    }
    for (DWORD index{0}; index < info.AceCount; ++index) {
        void* raw{nullptr};
        OutputRequire(GetAce(acl, index, &raw), "Cannot inspect legacy output ACL");
        const auto header{static_cast<ACE_HEADER*>(raw)};
        if (!private_file && (header->AceFlags & INHERIT_ONLY_ACE)) continue;
        if (!private_file && header->AceType == ACCESS_DENIED_ACE_TYPE) continue;
        OutputRequire(header->AceType == ACCESS_ALLOWED_ACE_TYPE, "Legacy output ACL type is unsupported");
        const auto ace{static_cast<ACCESS_ALLOWED_ACE*>(raw)};
        const auto sid{static_cast<PSID>(&ace->SidStart)};
        OutputRequire(IsValidSid(sid), "Invalid legacy output ACL identity");
        if (private_file) {
            OutputRequire(header->AceFlags == 0 && EqualSid(sid, owner) && ace->Mask == FILE_ALL_ACCESS,
                          "Legacy output ACL is not owner-private");
        } else if (!EqualSid(sid, owner)) {
            ACCESS_MASK mask{ace->Mask};
            GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
            MapGenericMask(&mask, &mapping);
            constexpr ACCESS_MASK WRITE_ACCESS{FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_DELETE_CHILD |
                                                FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | DELETE | WRITE_DAC | WRITE_OWNER};
            OutputRequire(!(mask & WRITE_ACCESS), "Legacy output parent permits writes by another identity");
        }
    }
}

bool OutputWindowsComponent(const std::wstring& name)
{
    if (name.empty() || name.size() > 32767 || name == L"." || name == L".." || name.back() == L'.' || name.back() == L' ') return false;
    for (const auto ch : name) {
        if (ch < 32 || std::wstring_view{L"<>:\"/\\|?*"}.find(ch) != std::wstring_view::npos) return false;
    }
    const auto stem{name.substr(0, name.find(L'.'))};
    for (const auto reserved : {L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$"}) {
        if (CompareStringOrdinal(stem.c_str(), static_cast<int>(stem.size()), reserved, -1, TRUE) == CSTR_EQUAL) return false;
    }
    if (stem.size() == 4 &&
        (CompareStringOrdinal(stem.c_str(), 3, L"COM", 3, TRUE) == CSTR_EQUAL ||
         CompareStringOrdinal(stem.c_str(), 3, L"LPT", 3, TRUE) == CSTR_EQUAL) &&
        ((stem[3] >= L'1' && stem[3] <= L'9') || stem[3] == L'\u00b9' || stem[3] == L'\u00b2' || stem[3] == L'\u00b3')) return false;
    return true;
}
#endif
} // namespace

struct LegacyExportOutput::Impl {
    const LegacyWalletSource& source;
    const LegacyOutputHook hook;
    bool failed{false};
    bool published{false};
    uint64_t bytes{0};
#ifndef WIN32
    fs::path parent;
    std::string leaf;
    std::string temporary;
    OutputFd directory;
    OutputFd source_parent;
    OutputIdentity parent_identity{};
    OutputIdentity source_directory{};
    std::optional<OutputIdentity> temporary_identity;
    FILE* file{nullptr};
#else
    OutputCreateFile create{nullptr};
    std::wstring leaf;
    std::wstring temporary;
    std::vector<unsigned char> owner;
    std::vector<OutputHandle> directories;
    std::vector<OutputIdentity> directory_identities;
    OutputHandle source_directory;
    OutputIdentity source_directory_identity{};
    OutputHandle temporary_handle; // DELETE/metadata only; never used for data I/O.
    std::optional<OutputIdentity> temporary_identity;
    FILE* file{nullptr};
#endif

    Impl(const LegacyWalletSource& input, LegacyOutputHook callback)
        : source(input), hook(std::move(callback)) {}

    void Stage(LegacyOutputStage stage) const { if (hook) hook(stage); }

#ifndef WIN32
    ~Impl()
    {
        if (file) std::fclose(file);
        // Never delete an unowned replacement, including a symlink/hardlink.
        // Parent trust excludes another user racing this check and unlink.
        struct stat st{};
        if (!published && temporary_identity &&
            fstatat(directory.Get(), temporary.c_str(), &st, AT_SYMLINK_NOFOLLOW) == 0 &&
            S_ISREG(st.st_mode) && OutputId(st) == *temporary_identity) {
            unlinkat(directory.Get(), temporary.c_str(), 0);
        }
    }

    void CheckParent() const
    {
        const auto st{OutputStat(directory.Get())};
        OutputRequire(S_ISDIR(st.st_mode) && OutputId(st) == parent_identity &&
                          st.st_uid == geteuid() && !(st.st_mode & (S_IWGRP | S_IWOTH)),
                      "Legacy output requires a trusted owner-controlled parent directory");
        struct stat named{};
        OutputRequire(lstat(parent.c_str(), &named) == 0 && S_ISDIR(named.st_mode) &&
                          OutputId(named) == parent_identity,
                      "Legacy output parent directory changed");
#ifdef __linux__
        // '..' can exit a bind mount before reaching the original ancestor.
        // Both retained handles must belong to the same mounted view. Root '/'
        // or equal st_dev alone would also admit unsafe whole-filesystem aliases.
        OutputRequire(OutputId(OutputStat(source_parent.Get())) == source_directory &&
                          SameLinuxMount(InspectLinuxMount(source_parent.Get()), InspectLinuxMount(directory.Get())),
                      "Legacy output requires source and destination parents in the same verifiable Linux mount");
#endif

        // Compare actual directory identities, including through ancestor aliases.
        // Every subsequent operation uses directory, never reopens parent by name.
        int ancestor{dup(directory.Get())};
        OutputRequire(ancestor != -1, "Cannot inspect legacy output ancestry");
        for (size_t depth{0}; depth < 1024; ++depth) {
            OutputFd current{ancestor};
            const auto identity{OutputId(OutputStat(current.Get()))};
            OutputRequire(identity != source_directory, "Legacy output cannot be inside the source wallet directory");
            OutputFd up{openat(current.Get(), "..", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
            OutputRequire(up.Get() != -1, "Cannot inspect legacy output ancestry");
            if (OutputId(OutputStat(up.Get())) == identity) return;
            ancestor = up.Release();
        }
        close(ancestor);
        throw std::runtime_error("Legacy output ancestry exceeds supported limits");
    }

    void CheckTemporary() const
    {
        struct stat named{};
        OutputRequire(temporary_identity &&
                          fstatat(directory.Get(), temporary.c_str(), &named, AT_SYMLINK_NOFOLLOW) == 0 &&
                          S_ISREG(named.st_mode) && OutputId(named) == *temporary_identity &&
                          named.st_nlink == 1 && named.st_uid == geteuid() && (named.st_mode & 0777) == 0600,
                      "Legacy output temporary identity or private permissions changed");
        if (file) {
            const auto st{OutputStat(fileno(file))};
            OutputRequire(OutputId(st) == *temporary_identity, "Legacy output temporary identity changed");
        }
    }

    void Initialize(const fs::path& destination, const fs::path& source_path)
    {
        const auto& native{destination.native()};
        OutputRequire(!native.empty() && native.find('\0') == std::string::npos && destination != "-" &&
                          destination.has_filename() && destination.filename() != "." && destination.filename() != "..",
                      "Legacy output requires an explicit regular-file destination");
        parent = fs::absolute(destination).parent_path();
        leaf = destination.filename().native();
        source_parent.Reset(open(source_path.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        OutputRequire(source_parent.Get() != -1, "Cannot inspect legacy source directory");
        source_directory = OutputId(OutputStat(source_parent.Get()));
        // One open resolves the destination parent. O_NOFOLLOW rejects a final
        // parent symlink; ancestor resolution is pinned by this directory fd.
        OutputFd opened{open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
        OutputRequire(opened.Get() != -1, "Cannot open legacy output parent directory");
        // Transfer only after ownership has entered the long-lived RAII object.
        directory.Reset(opened.Release());
        parent_identity = OutputId(OutputStat(directory.Get()));
        CheckParent();
        struct stat existing{};
        const int status{fstatat(directory.Get(), leaf.c_str(), &existing, AT_SYMLINK_NOFOLLOW)};
        OutputRequire(status == -1 && errno == ENOENT, "Legacy output destination already exists or cannot be inspected");
        source.VerifyUnchanged();
        for (int attempt{0}; attempt < 16; ++attempt) {
            temporary = ".roots-export-" + GetRandHash().GetHex() + ".tmp";
            OutputFd created{openat(directory.Get(), temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600)};
            if (created.Get() == -1 && errno == EEXIST) continue;
            OutputRequire(created.Get() != -1, "Cannot create private legacy output temporary file");
            const auto st{OutputStat(created.Get())};
            temporary_identity = OutputId(st);
            CheckTemporary();
            file = fdopen(created.Get(), "wb");
            OutputRequire(file != nullptr, "Cannot open legacy output stream");
            created.Release();
            // No libc secret-bearing write buffer to cleanse or leave on errors.
            OutputRequire(setvbuf(file, nullptr, _IONBF, 0) == 0, "Cannot configure legacy output stream");
            Stage(LegacyOutputStage::CREATED);
            return;
        }
        throw std::runtime_error("Cannot create unique legacy output temporary file");
    }

    void SyncFile() const
    {
        OutputRequire(fsync(fileno(file)) == 0, "Cannot synchronize legacy output");
    }

    void PublishNative() const
    {
        OutputRequire(MoveOutputNoReplace(directory.Get(), temporary.c_str(), leaf.c_str()),
                      "Cannot publish legacy output without replacement");
    }
#else
    ~Impl()
    {
        if (file) std::fclose(file);
        if (!published && temporary_handle.Get() != INVALID_HANDLE_VALUE) {
            // Delete by the invocation's retained object handle, never by a
            // name that could now identify an existing user's replacement.
            FILE_DISPOSITION_INFO disposition{TRUE};
            SetFileInformationByHandle(temporary_handle.Get(), FileDispositionInfo, &disposition, sizeof(disposition));
        }
    }

    HANDLE Directory() const { return directories.back().Get(); }

    void CheckParent() const
    {
        OutputRequire(IsPhysicalWindowsVolumeRoot(directories.front().Get()),
                      "Legacy output drive root is not a physical volume root");
        for (size_t index{0}; index < directories.size(); ++index) {
            const auto info{OutputInformation(directories[index].Get())};
            OutputRequire((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                              !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                              OutputId(info) == directory_identities[index],
                          "Legacy output parent directory changed");
            OutputRequire(OutputId(info) != source_directory_identity,
                          "Legacy output cannot be inside the source wallet directory");
        }
        CheckOutputAcl(Directory(), const_cast<unsigned char*>(owner.data()), false);
    }

    void CheckTemporary() const
    {
        const auto info{OutputInformation(temporary_handle.Get())};
        OutputRequire(temporary_identity && OutputId(info) == *temporary_identity && info.nNumberOfLinks == 1 &&
                          !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)),
                      "Legacy output temporary identity changed");
        CheckOutputAcl(temporary_handle.Get(), const_cast<unsigned char*>(owner.data()), true);
        if (file) {
            const auto handle{reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file)))};
            OutputRequire(OutputId(OutputInformation(handle)) == *temporary_identity, "Legacy output temporary identity changed");
        }
    }

    void Initialize(const fs::path& destination, const fs::path& source_path)
    {
        const auto& native{destination.native()};
        OutputRequire(!native.empty() && native.find(L'\0') == std::wstring::npos && destination != "-" &&
                          destination.has_filename() && OutputWindowsComponent(destination.filename().native()),
                      "Legacy output requires an explicit regular-file destination");
        const auto absolute{fs::absolute(destination).lexically_normal()};
        const auto drive{absolute.root_name().native()};
        OutputRequire(drive.size() == 2 && drive[1] == L':' && absolute.has_root_directory(),
                      "Legacy output requires a local drive destination");
        leaf = absolute.filename().native();
        create = NativeOutputCreate();
        owner = OutputOwnerSid();
        source_directory = OutputHandle{CreateFileW(source_path.parent_path().c_str(), FILE_READ_ATTRIBUTES,
                                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                                    FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
        const auto source_info{OutputInformation(source_directory.Get())};
        OutputRequire(source_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY, "Cannot inspect legacy source directory");
        source_directory_identity = OutputId(source_info);

        std::vector<std::wstring> components;
        for (const auto& component : absolute.parent_path().relative_path()) {
            OutputRequire(components.size() < 1024 && OutputWindowsComponent(component.native()), "Unsupported legacy output directory component");
            components.push_back(component.native());
        }
        for (size_t index{0}; index <= components.size(); ++index) {
            const bool final{index == components.size()};
            const auto name{index == 0 ? L"\\??\\" + drive + L"\\" : components[index - 1]};
            const auto parent{index == 0 ? nullptr : Directory()};
            const auto access{FILE_READ_ATTRIBUTES | FILE_TRAVERSE | (final ? READ_CONTROL : 0)};
            // Ancestors deny write/delete sharing, pinning both directory
            // identity and reparse state. The trusted final parent permits
            // child creation/rename, but also denies delete sharing.
            auto result{OpenOutputAt(create, parent, name, access,
                                     FILE_SHARE_READ | (final ? FILE_SHARE_WRITE : 0), FILE_OPEN)};
            OutputRequire(result.status == 0, "Cannot open legacy output parent directory");
            const auto info{OutputInformation(result.handle.Get())};
            OutputRequire((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                              !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT),
                          "Legacy output parent reparse points are unsupported");
            if (index == 0) {
                // A DOS drive may name a subdirectory (SUBST). Walking down
                // from it would omit physical ancestors and hide source-tree
                // membership. Require the actual opened volume root instead.
                OutputRequire(IsPhysicalWindowsVolumeRoot(result.handle.Get()),
                              "Legacy output drive root is not a physical volume root");
            }
            directory_identities.push_back(OutputId(info));
            directories.push_back(std::move(result.handle));
        }
        DWORD flags{0};
        DWORD component_limit{0};
        wchar_t filesystem[32]{};
        OutputRequire(GetVolumeInformationByHandleW(Directory(), nullptr, 0, nullptr, &component_limit, &flags,
                                                    filesystem, static_cast<DWORD>(std::size(filesystem))) &&
                          (flags & FILE_PERSISTENT_ACLS) && !(flags & FILE_READ_ONLY_VOLUME) &&
                          CompareStringOrdinal(filesystem, -1, L"NTFS", -1, TRUE) == CSTR_EQUAL &&
                          leaf.size() <= component_limit,
                      "Legacy output requires a writable local NTFS volume with persistent ACLs");
        CheckParent();
        constexpr NTSTATUS NAME_NOT_FOUND{static_cast<NTSTATUS>(0xC0000034UL)};
        const auto existing{OpenOutputAt(create, Directory(), leaf, FILE_READ_ATTRIBUTES,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN)};
        OutputRequire(existing.status == NAME_NOT_FOUND, "Legacy output destination already exists or cannot be inspected");
        source.VerifyUnchanged();

        // This descriptor is supplied to FILE_CREATE, before the new name is
        // visible. Never create broadly then repair its ACL after creation.
        std::vector<DWORD> acl_storage((sizeof(ACL) + sizeof(ACCESS_ALLOWED_ACE) + owner.size() + sizeof(DWORD) - 1) / sizeof(DWORD));
        auto* acl{reinterpret_cast<PACL>(acl_storage.data())};
        SECURITY_DESCRIPTOR security{};
        OutputRequire(InitializeAcl(acl, static_cast<DWORD>(acl_storage.size() * sizeof(DWORD)), ACL_REVISION) &&
                          AddAccessAllowedAce(acl, ACL_REVISION, FILE_ALL_ACCESS, owner.data()) &&
                          InitializeSecurityDescriptor(&security, SECURITY_DESCRIPTOR_REVISION) &&
                          SetSecurityDescriptorOwner(&security, owner.data(), FALSE) &&
                          SetSecurityDescriptorDacl(&security, TRUE, acl, FALSE) &&
                          SetSecurityDescriptorControl(&security, SE_DACL_PROTECTED, SE_DACL_PROTECTED),
                      "Cannot create owner-private legacy output ACL");
        constexpr NTSTATUS NAME_COLLISION{static_cast<NTSTATUS>(0xC0000035UL)};
        for (int attempt{0}; attempt < 16; ++attempt) {
            const auto random{GetRandHash().GetHex()};
            temporary = L".roots-export-" + std::wstring{random.begin(), random.end()} + L".tmp";
            auto created{OpenOutputAt(create, Directory(), temporary, DELETE | FILE_READ_ATTRIBUTES | READ_CONTROL,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE, &security)};
            if (created.status == NAME_COLLISION) continue;
            OutputRequire(created.status == 0, "Cannot create private legacy output temporary file");
            OutputRequire(created.disposition == FILE_CREATED, "Legacy output temporary was not exclusively created");
            // Until FILE_CREATED is positively established, the local handle
            // is close-only and must not enter invocation-owned deletion.
            temporary_handle = std::move(created.handle);
            temporary_identity = OutputId(OutputInformation(temporary_handle.Get()));
            CheckTemporary();
            auto data{OpenOutputAt(create, Directory(), temporary, FILE_GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, FILE_OPEN)};
            OutputRequire(data.status == 0 && OutputId(OutputInformation(data.handle.Get())) == *temporary_identity,
                          "Cannot open owned legacy output stream");
            const int descriptor{_open_osfhandle(reinterpret_cast<intptr_t>(data.handle.Get()), _O_WRONLY | _O_BINARY)};
            OutputRequire(descriptor != -1, "Cannot open legacy output descriptor");
            data.handle.Release(); // CRT now owns this data handle only.
            file = _fdopen(descriptor, "wb");
            if (!file) { _close(descriptor); throw std::runtime_error("Cannot open legacy output stream"); }
            OutputRequire(setvbuf(file, nullptr, _IONBF, 0) == 0, "Cannot configure legacy output stream");
            Stage(LegacyOutputStage::CREATED);
            return;
        }
        throw std::runtime_error("Cannot create unique legacy output temporary file");
    }

    void SyncFile() const
    {
        const auto data_handle{reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file)))};
        OutputRequire(FlushFileBuffers(data_handle), "Cannot synchronize legacy output");
    }

    void PublishNative() const
    {
        std::vector<uint64_t> storage((sizeof(FILE_RENAME_INFO) + (leaf.size() + 1) * sizeof(wchar_t) + sizeof(uint64_t) - 1) / sizeof(uint64_t));
        auto* rename{reinterpret_cast<FILE_RENAME_INFO*>(storage.data())};
        rename->ReplaceIfExists = FALSE;
        rename->RootDirectory = Directory();
        rename->FileNameLength = static_cast<DWORD>(leaf.size() * sizeof(wchar_t));
        std::memcpy(rename->FileName, leaf.c_str(), (leaf.size() + 1) * sizeof(wchar_t));
        OutputRequire(SetFileInformationByHandle(temporary_handle.Get(), FileRenameInfo, rename, static_cast<DWORD>(storage.size() * sizeof(uint64_t))),
                      "Cannot publish legacy output without replacement");
    }
#endif

    void Write(std::string_view data)
    {
        OutputRequire(!failed && !published && file, "Legacy output is no longer writable");
        try {
            OutputRequire(data.size() <= (uint64_t{1} << 30) - bytes, "Legacy output exceeds supported size limit");
            Stage(LegacyOutputStage::WRITE);
            CheckTemporary();
            if (data.empty()) return;
            OutputRequire(std::fwrite(data.data(), 1, data.size(), file) == data.size() && !std::ferror(file),
                          "Cannot write legacy output");
            bytes += data.size();
        } catch (...) { failed = true; throw; }
    }

    void Publish()
    {
        OutputRequire(!failed && !published && file, "Legacy output is no longer publishable");
        try {
            Stage(LegacyOutputStage::FLUSH);
            OutputRequire(std::fflush(file) == 0 && !std::ferror(file), "Cannot flush legacy output");
            Stage(LegacyOutputStage::SYNC);
            SyncFile();
            Stage(LegacyOutputStage::CLOSE);
            FILE* closing{std::exchange(file, nullptr)};
            OutputRequire(std::fclose(closing) == 0, "Cannot close legacy output");
            Stage(LegacyOutputStage::PUBLISH);
            source.VerifyUnchanged();
            CheckParent();
            CheckTemporary();
            PublishNative();
            published = true; // Final fallible step; never delete a final name.
#ifdef WIN32
            // The retained metadata-only handles perform no writes or flushes
            // on destruction. All data-stream close results were checked above.
            temporary_handle = OutputHandle{};
#endif
        } catch (...) { failed = true; throw; }
    }
};

#ifdef __linux__
LegacyExportOutput::LinuxMountFacts LegacyExportOutput::InspectLinuxMount(int directory)
{
    LinuxMountFacts result;
#if defined(SYS_statx) && defined(STATX_MNT_ID)
    struct stat identity{};
    if (fstat(directory, &identity) != 0) return result;
    result.expected_major = major(identity.st_dev);
    result.expected_minor = minor(identity.st_dev);
    result.expected_inode = identity.st_ino;
    struct statx extended{};
    result.status = static_cast<int>(syscall(SYS_statx, directory, "", AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW,
                                             STATX_TYPE | STATX_INO | STATX_MNT_ID, &extended));
    result.mask = extended.stx_mask;
    result.mode = extended.stx_mode;
    result.device_major = extended.stx_dev_major;
    result.device_minor = extended.stx_dev_minor;
    result.inode = extended.stx_ino;
    result.mount_id = extended.stx_mnt_id;
#endif
    return result;
}

bool LegacyExportOutput::SameLinuxMount(const LinuxMountFacts& source, const LinuxMountFacts& output)
{
#if defined(SYS_statx) && defined(STATX_MNT_ID)
    constexpr uint32_t REQUIRED{STATX_TYPE | STATX_INO | STATX_MNT_ID};
    const auto valid = [](const LinuxMountFacts& facts) {
        return facts.status == 0 && (facts.mask & REQUIRED) == REQUIRED && S_ISDIR(facts.mode) &&
               facts.mount_id != 0 && facts.inode == facts.expected_inode &&
               facts.device_major == facts.expected_major && facts.device_minor == facts.expected_minor;
    };
    return valid(source) && valid(output) && source.mount_id == output.mount_id;
#else
    return false; // No unchecked pathname or older-kernel fallback.
#endif
}
#endif

#ifdef WIN32
bool LegacyExportOutput::IsPhysicalWindowsVolumeRoot(void* directory)
{
    // A normalized volume-relative root is exactly "\\". The fixed buffer
    // also rejects insufficient space, unsupported queries and SUBST roots
    // resolving to a physical subdirectory, without allocating a full path.
    wchar_t relative[2]{};
    const DWORD size{GetFinalPathNameByHandleW(static_cast<HANDLE>(directory), relative,
                                              static_cast<DWORD>(std::size(relative)),
                                              FILE_NAME_NORMALIZED | VOLUME_NAME_NONE)};
    return size == 1 && relative[0] == L'\\' && relative[1] == L'\0';
}
#endif

LegacyExportOutput::LegacyExportOutput(const LegacyWalletSource& source, const fs::path& destination, LegacyOutputHook hook) try
    : m_impl(std::make_unique<Impl>(source, std::move(hook)))
{
    m_impl->Initialize(destination, source.SourcePath());
} catch (const fs::filesystem_error&) {
    throw std::runtime_error("Cannot resolve legacy output filesystem path");
}

LegacyExportOutput::~LegacyExportOutput() = default;
void LegacyExportOutput::Write(std::string_view bytes) { m_impl->Write(bytes); }
void LegacyExportOutput::Publish() { m_impl->Publish(); }
} // namespace wallet
