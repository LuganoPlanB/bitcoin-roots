// Copyright (c) 2016-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallettool.h>
#include <wallet/wallettool_input.h>

#include <common/args.h>
#include <util/check.h>
#include <util/fs.h>
#include <util/translation.h>
#include <wallet/dump.h>
#include <wallet/legacy_export.h>
#include <wallet/wallet.h>
#include <wallet/walletutil.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#ifdef WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace wallet {
namespace WalletTool {

// The standard wallet deleter function blocks on the validation interface
// queue, which doesn't exist for the bitcoin-wallet. Define our own
// deleter here.
static void WalletToolReleaseWallet(CWallet* wallet)
{
    wallet->WalletLogPrintf("Releasing wallet\n");
    wallet->Close();
    delete wallet;
}

static void WalletCreate(CWallet* wallet_instance, uint64_t wallet_creation_flags)
{
    LOCK(wallet_instance->cs_wallet);

    wallet_instance->InitWalletFlags(wallet_creation_flags);

    Assert(wallet_instance->IsWalletFlagSet(WALLET_FLAG_DESCRIPTORS));
    wallet_instance->SetupDescriptorScriptPubKeyMans();

    tfm::format(std::cout, "Topping up keypool...\n");
    wallet_instance->TopUpKeyPool();
}

static std::shared_ptr<CWallet> MakeWallet(const std::string& name, const fs::path& path, DatabaseOptions options)
{
    DatabaseStatus status;
    bilingual_str error;
    std::vector<bilingual_str> warnings;
    std::unique_ptr<WalletDatabase> database = MakeDatabase(path, options, status, error);
    if (!database) {
        tfm::format(std::cerr, "%s\n", error.original);
        return nullptr;
    }

    // dummy chain interface
    std::shared_ptr<CWallet> wallet_instance{new CWallet(/*chain=*/nullptr, name, std::move(database)), WalletToolReleaseWallet};
    DBErrors load_wallet_ret;
    try {
        load_wallet_ret = wallet_instance->PopulateWalletFromDB(error, warnings);
    } catch (const std::runtime_error&) {
        tfm::format(std::cerr, "Error loading %s. Is wallet being used by another process?\n", name);
        return nullptr;
    }

    if (!error.empty()) {
        tfm::format(std::cerr, "%s", error.original);
    }

    for (const auto &warning : warnings) {
        tfm::format(std::cerr, "%s", warning.original);
    }

    if (load_wallet_ret != DBErrors::LOAD_OK && load_wallet_ret != DBErrors::NONCRITICAL_ERROR && load_wallet_ret != DBErrors::NEED_RESCAN) {
        return nullptr;
    }

    if (options.require_create) WalletCreate(wallet_instance.get(), options.create_flags);

    return wallet_instance;
}

static void WalletShowInfo(CWallet* wallet_instance)
{
    LOCK(wallet_instance->cs_wallet);

    tfm::format(std::cout, "Wallet info\n===========\n");
    tfm::format(std::cout, "Name: %s\n", wallet_instance->GetName());
    tfm::format(std::cout, "Format: %s\n", wallet_instance->GetDatabase().Format());
    tfm::format(std::cout, "Descriptors: %s\n", wallet_instance->IsWalletFlagSet(WALLET_FLAG_DESCRIPTORS) ? "yes" : "no");
    tfm::format(std::cout, "Encrypted: %s\n", wallet_instance->HasEncryptionKeys() ? "yes" : "no");
    tfm::format(std::cout, "HD (hd seed available): %s\n", wallet_instance->IsHDEnabled() ? "yes" : "no");
    tfm::format(std::cout, "Keypool Size: %u\n", wallet_instance->GetKeyPoolSize());
    tfm::format(std::cout, "Transactions: %zu\n", wallet_instance->mapWallet.size());
    tfm::format(std::cout, "Address Book: %zu\n", wallet_instance->m_address_book.size());
}

// Early diagnostics only: these observations neither create output nor replace
// the writer's retained-directory, source-identity and atomic publication checks.
static void CheckLegacyDestination(const fs::path& source, const fs::path& destination)
{
    if (destination.empty() || destination == "-" || !destination.has_filename()) {
        throw std::runtime_error("Legacy export requires an explicit filesystem destination");
    }
    std::error_code error;
    const auto status{fs::symlink_status(destination, error)};
    if (fs::exists(status) || (error && error != std::errc::no_such_file_or_directory)) {
        throw std::runtime_error("Legacy output destination already exists or cannot be inspected");
    }
    auto parent{fs::absolute(destination).parent_path()};
    if (!fs::is_directory(fs::symlink_status(parent))) throw std::runtime_error("Legacy output requires an existing non-symlink parent directory");
#ifndef WIN32
    struct stat st{};
    if (stat(parent.c_str(), &st) != 0 || st.st_uid != geteuid() || (st.st_mode & (S_IWGRP | S_IWOTH))) {
        throw std::runtime_error("Legacy output requires a trusted owner-controlled parent directory");
    }
#endif
    fs::path source_directory{source};
    if (!fs::is_directory(source)) source_directory = source.parent_path();
    source_directory = fs::canonical(source_directory);
    parent = fs::canonical(parent);
    while (true) {
        if (parent == source_directory) throw std::runtime_error("Legacy output cannot be inside the source wallet directory");
        const auto up{parent.parent_path()};
        if (up == parent) break;
        parent = up;
    }
}

// Publication has committed before this path runs. Native writes and to_chars
// need no formatting/path allocations, and a missing report channel must never
// turn a complete export into a claimed export failure. Only dumpwallet ignores
// SIGPIPE; the other commands retain their existing signal/stream behavior.
class LegacyExportReporter
{
    // Encode before source/output work, and pin the original report channels
    // before any opened wallet handle could reuse a closed standard descriptor.
    const std::string m_destination;
    using Identity = std::array<uint64_t, 2>;
#ifdef WIN32
    HANDLE m_stdout{INVALID_HANDLE_VALUE}, m_stderr{INVALID_HANDLE_VALUE};
    static HANDLE Pin(DWORD name) noexcept
    {
        HANDLE result{INVALID_HANDLE_VALUE};
        if (!DuplicateHandle(GetCurrentProcess(), GetStdHandle(name), GetCurrentProcess(), &result, 0, FALSE, DUPLICATE_SAME_ACCESS)) return INVALID_HANDLE_VALUE;
        return result;
    }
    static std::optional<Identity> ChannelIdentity(HANDLE& handle) noexcept
    {
        if (handle == INVALID_HANDLE_VALUE) return std::nullopt;
        const auto type{GetFileType(handle)};
        if (type == FILE_TYPE_PIPE || type == FILE_TYPE_CHAR) return std::nullopt;
        BY_HANDLE_FILE_INFORMATION info{};
        if (type != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &info)) {
            Close(handle); // Never write through an unidentifiable file channel.
            return std::nullopt;
        }
        return Identity{info.dwVolumeSerialNumber, (uint64_t{info.nFileIndexHigh} << 32) | info.nFileIndexLow};
    }
    static void Close(HANDLE& handle) noexcept
    {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
    }
#else
    int m_stdout{-1}, m_stderr{-1};
    static std::optional<Identity> ChannelIdentity(int& descriptor) noexcept
    {
        if (descriptor == -1) return std::nullopt;
        struct stat info{};
        if (fstat(descriptor, &info) != 0) {
            Close(descriptor);
            return std::nullopt;
        }
        if (!S_ISREG(info.st_mode)) return std::nullopt;
        return Identity{static_cast<uint64_t>(info.st_dev), static_cast<uint64_t>(info.st_ino)};
    }
    static void Close(int& descriptor) noexcept
    {
        if (descriptor != -1) close(descriptor);
        descriptor = -1;
    }
#endif
    static std::optional<Identity> ObservedIdentity(const fs::path& path, bool directory_wallet)
    {
        std::error_code error;
        const auto status{fs::status(path, error)};
        if (error == std::errc::no_such_file_or_directory) return std::nullopt;
        if (error) throw std::runtime_error("Cannot inspect legacy export paths for report channels");
        if (!directory_wallet && fs::is_directory(status)) return std::nullopt;
        const fs::path file{fs::is_directory(status) ? fs::path{path / "wallet.dat"} : path};
#ifdef WIN32
        if (!fs::is_regular_file(fs::status(file))) return std::nullopt;
        auto handle{CreateFileW(file.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        const auto identity{ChannelIdentity(handle)};
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        if (!identity) throw std::runtime_error("Cannot inspect legacy export paths for report channels");
        return identity;
#else
        struct stat info{};
        if (stat(file.c_str(), &info) != 0) {
            if (errno == ENOENT) return std::nullopt;
            throw std::runtime_error("Cannot inspect legacy export paths for report channels");
        }
        if (!S_ISREG(info.st_mode)) return std::nullopt;
        return Identity{static_cast<uint64_t>(info.st_dev), static_cast<uint64_t>(info.st_ino)};
#endif
    }
public:
    explicit LegacyExportReporter(const fs::path& destination) : m_destination(fs::PathToString(destination))
    {
#ifdef WIN32
        m_stdout = Pin(STD_OUTPUT_HANDLE);
        m_stderr = Pin(STD_ERROR_HANDLE);
#else
        m_stdout = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 3);
        m_stderr = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 3);
#endif
    }
    LegacyExportReporter(const LegacyExportReporter&) = delete;
    LegacyExportReporter& operator=(const LegacyExportReporter&) = delete;
    ~LegacyExportReporter()
    {
        Close(m_stdout);
        Close(m_stderr);
    }
    void ProtectChannels(const fs::path& path, bool directory_wallet)
    {
        const auto output{ChannelIdentity(m_stdout)}, error{ChannelIdentity(m_stderr)};
        try {
            const auto identity{ObservedIdentity(path, directory_wallet)};
            if (identity && output == identity) Close(m_stdout);
            if (identity && error == identity) Close(m_stderr);
        } catch (...) {
            // Unknown source identity must not send a diagnostic into an
            // unverified regular-file channel. Pipes/terminals stay available.
            if (output) Close(m_stdout);
            if (error) Close(m_stderr);
            throw;
        }
    }
    std::string_view Destination() const noexcept { return m_destination; }
    bool Write(std::string_view bytes, bool error = false) const noexcept
    {
        while (!bytes.empty()) {
            if (!error && LegacyWalletToolInterrupted()) return false;
#ifdef WIN32
            DWORD written{0};
            if (!WriteFile(error ? m_stderr : m_stdout, bytes.data(), static_cast<DWORD>(std::min<size_t>(bytes.size(), 4096)), &written, nullptr) || written == 0) return false;
#else
            const auto written{write(error ? m_stderr : m_stdout, bytes.data(), bytes.size())};
            if (written < 0 && errno == EINTR) {
                if (LegacyWalletToolInterrupted()) return false;
                continue;
            }
            if (written <= 0) return false;
#endif
            bytes.remove_prefix(written);
        }
        return true;
    }
    void Error(std::string_view message) const noexcept
    {
        Write("Error: ", /*error=*/true);
        Write(message, /*error=*/true);
        Write(". Use a clean offline copy and preserve the original backup.\n", /*error=*/true);
    }
};

static void ReportLegacyExport(const LegacyExportResult& result, const LegacyExportReporter& report) noexcept
{
    bool written{report.Write("Exported legacy wallet to ") && report.Write(report.Destination()) &&
                 report.Write("\nState: ") && report.Write(result.state) && report.Write("\n")};
    const auto count = [&](std::string_view name, uint64_t value) {
        std::array<char, 20> bytes{};
        const auto converted{std::to_chars(bytes.data(), bytes.data() + bytes.size(), value)};
        written = written && converted.ec == std::errc{} && report.Write(name) &&
                  report.Write(std::string_view{bytes.data(), static_cast<size_t>(converted.ptr - bytes.data())}) && report.Write("\n");
    };
    count("Records: ", result.counts.records);
    count("Private keys: ", result.counts.private_keys);
    count("Public key identities: ", result.counts.public_key_identities);
    count("Identified seeds: ", result.counts.identified_seeds);
    count("Roots: ", result.counts.roots);
    count("Scripts: ", result.counts.scripts);
    count("Addresses: ", result.counts.addresses);
    count("Reserve entries: ", result.counts.reserve_entries);
    count("Encryption keys: ", result.counts.encryption_keys);
    count("Omitted records: ", result.counts.omitted_records);
    count("Unverified derivations: ", result.counts.unverified_derivations);
    if (result.counts.private_keys > 0) {
        written = written && report.Write("Warning: The export contains unencrypted spending material. Keep it private and preserve the original backup.\n");
    }
    if (!written) {
        // Even this best-effort diagnostic can be unavailable. The completed
        // file remains authoritative and is never removed or retried.
        report.Write("Warning: Export was published, but the success report could not be written. Inspect the requested destination and preserve the original backup.\n", /*error=*/true);
    }
}

bool ExecuteWalletToolFunc(const ArgsManager& args, const std::string& command)
{
    {
        std::vector<std::string> details;
        if (!args.CheckCommandOptions(command, &details)) {
            tfm::format(std::cerr, "Error: Invalid arguments provided:\n%s\n", util::MakeUnorderedList(details));
            return false;
        }
    }
    if (command == "dumpwallet") {
        std::optional<LegacyExportReporter> report;
        try {
            CheckLegacyWalletToolInterrupted();
            // Explicit paths are relative to cwd, independent of the old
            // directory-oriented GetWalletPath/MakeWallet loading route.
            const auto source_path{fs::absolute(args.GetPathArg("-wallet"))};
            const auto destination{args.GetPathArg("-dumpfile")};
            report.emplace(destination);
            report->ProtectChannels(destination, /*directory_wallet=*/false);
            report->ProtectChannels(source_path, /*directory_wallet=*/true);
            const LegacyWalletSource source{source_path};
            // The reader has now pinned and verified the admitted source;
            // repeat the channel check across the initial open interval.
            report->ProtectChannels(source_path, /*directory_wallet=*/true);
            const auto inventory{DecodeLegacyWalletRecords(source.Records())};
            CheckLegacyWalletToolInterrupted();
            CheckLegacyDestination(source_path, destination);
            const bool encrypted{std::any_of(inventory.keys.begin(), inventory.keys.end(), [](const auto& entry) { return entry.second.ciphertext.has_value(); })};
            if (!encrypted && args.IsArgSet("-stdinwalletpassphrase")) {
                throw std::runtime_error("-stdinwalletpassphrase requires encrypted private keys");
            }
            const auto passphrase{encrypted ? ReadLegacyWalletToolPassphrase(args.IsArgSet("-stdinwalletpassphrase")) : SecureString{}};
            report->ProtectChannels(destination, /*directory_wallet=*/false);
            report->ProtectChannels(source_path, /*directory_wallet=*/true);
            static_assert(std::is_nothrow_move_constructible_v<LegacyExportResult>);
            const auto result{WriteLegacyWalletExport(source, passphrase, destination,
                                                     [](LegacyOutputStage) { CheckLegacyWalletToolInterrupted(); },
                                                     [](LegacyExportStage) { CheckLegacyWalletToolInterrupted(); })};
            ReportLegacyExport(result, *report);
            return true;
        } catch (const fs::filesystem_error&) {
            if (report) report->Error("Invalid legacy export source or destination filesystem path (values omitted)");
            else tfm::format(std::cerr, "Error: Invalid legacy export source or destination filesystem path (values omitted). Use a clean offline copy and preserve the original backup.\n");
            return false;
        } catch (const std::exception& e) {
            const std::string_view message{LegacyWalletToolInterrupted() ? "Legacy export interrupted" : e.what()};
            if (report) report->Error(message);
            else tfm::format(std::cerr, "Error: %s. Use a clean offline copy and preserve the original backup.\n", message);
            return false;
        }
    }
    if ((command == "create" || command == "createfromdump") && !args.IsArgSet("-wallet")) {
        tfm::format(std::cerr, "Wallet name must be provided when creating a new wallet.\n");
        return false;
    }
    const std::string name = args.GetArg("-wallet", "");
    util::Result<fs::path> path_res = GetWalletPath(name);
    if (!path_res) {
        tfm::format(std::cerr, "%s\n", util::ErrorString(path_res).original);
        return false;
    }
    const fs::path& path = *path_res;

    if (command == "create") {
        if (name.empty()) {
            tfm::format(std::cerr, "Wallet name cannot be empty\n");
            return false;
        }
        DatabaseOptions options;
        ReadDatabaseArgs(args, options);
        options.require_create = true;
        options.create_flags |= WALLET_FLAG_DESCRIPTORS;
        options.require_format = DatabaseFormat::SQLITE;

        const std::shared_ptr<CWallet> wallet_instance = MakeWallet(name, path, options);
        if (wallet_instance) {
            WalletShowInfo(wallet_instance.get());
            wallet_instance->Close();
        }
    } else if (command == "info") {
        DatabaseOptions options;
        ReadDatabaseArgs(args, options);
        options.require_existing = true;
        const std::shared_ptr<CWallet> wallet_instance = MakeWallet(name, path, options);
        if (!wallet_instance) return false;
        WalletShowInfo(wallet_instance.get());
        wallet_instance->Close();
    } else if (command == "dump") {
        DatabaseOptions options;
        ReadDatabaseArgs(args, options);
        options.require_existing = true;
        DatabaseStatus status;

        if (IsBDBFile(BDBDataFile(path))) {
            options.require_format = DatabaseFormat::BERKELEY_RO;
        }

        bilingual_str error;
        std::unique_ptr<WalletDatabase> database = MakeDatabase(path, options, status, error);
        if (!database) {
            tfm::format(std::cerr, "%s\n", error.original);
            return false;
        }

        bool ret = DumpWallet(args, *database, error);
        if (!ret && !error.empty()) {
            tfm::format(std::cerr, "%s\n", error.original);
            return ret;
        }
        tfm::format(std::cout, "The dumpfile may contain private keys. To ensure the safety of your Bitcoin, do not share the dumpfile.\n");
        return ret;
    } else if (command == "createfromdump") {
        bilingual_str error;
        bool ret = CreateFromDump(args, name, path, error);
        if (!ret && !error.empty()) {
            tfm::format(std::cerr, "%s\n", error.original);
        }
        return ret;
    } else {
        tfm::format(std::cerr, "Invalid command: %s\n", command);
        return false;
    }

    return true;
}
} // namespace WalletTool
} // namespace wallet
