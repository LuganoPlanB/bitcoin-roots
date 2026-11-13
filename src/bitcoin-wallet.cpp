// Copyright (c) 2016-present The Bitcoin Core developers
// Copyright (C) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <chainparams.h>
#include <chainparamsbase.h>
#include <clientversion.h>
#include <common/args.h>
#include <common/license_info.h>
#include <common/system.h>
#include <compat/compat.h>
#include <crypto/sha256.h>
#include <interfaces/init.h>
#include <key.h>
#include <logging.h>
#include <pubkey.h>
#include <tinyformat.h>
#include <util/exception.h>
#include <util/strencodings.h>
#include <util/translation.h>
#include <wallet/wallettool.h>
#include <wallet/wallettool_input.h>

#include <exception>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <tuple>

using util::Join;

const TranslateFn G_TRANSLATION_FUN{nullptr};

static void SetupWalletToolArgs(ArgsManager& argsman)
{
    SetupHelpOptions(argsman);
    SetupChainParamsBaseOptions(argsman);

    argsman.AddArg("-version", "Print version and exit", ArgsManager::ALLOW_ANY, OptionsCategory::OPTIONS);
    argsman.AddArg("-datadir=<dir>", "Specify data directory", ArgsManager::ALLOW_ANY | ArgsManager::DISALLOW_NEGATION, OptionsCategory::OPTIONS);
    argsman.AddArg("-wallet=<wallet-name>", "Specify wallet name", ArgsManager::ALLOW_ANY | ArgsManager::NETWORK_ONLY, OptionsCategory::OPTIONS);
    argsman.AddArg("-dumpfile=<file name>", "When used with 'dump', writes out the records to this file. When used with 'createfromdump', loads the records into a new wallet. For 'dumpwallet', specifies a new unencrypted recovery export in an existing directory (never '-').", ArgsManager::ALLOW_ANY | ArgsManager::DISALLOW_NEGATION, OptionsCategory::COMMAND_OPTIONS);
    argsman.AddArg("-stdinwalletpassphrase", "For 'dumpwallet' only, read at most 4096 raw passphrase bytes through LF, CRLF or EOF. Strip only that terminator; preserve whitespace, NUL and UTF-8 bytes. Reject already available trailing input without waiting for later bytes. Without this switch use the controlling terminal (POSIX: finish with Ctrl-J). Never supply a passphrase in arguments. Rejected when no encrypted private keys need decryption.", ArgsManager::ALLOW_ANY | ArgsManager::DISALLOW_NEGATION, OptionsCategory::COMMAND_OPTIONS);
    argsman.AddArg("-debug=<category>", "Output debugging information (default: 0).", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);
    argsman.AddArg("-printtoconsole", "Send trace/debug info to console (default: 1 when no -debug is true, 0 otherwise).", ArgsManager::ALLOW_ANY, OptionsCategory::DEBUG_TEST);

    argsman.AddCommand("info", "Get wallet info");
    argsman.AddCommand("create", "Create a new descriptor wallet file");
    argsman.AddCommand("dump", "Print out all of the wallet key-value records", {"-dumpfile"});
    argsman.AddCommand("dumpwallet", "Export keys and recovery metadata from a clean offline legacy BDB copy, preserving the original backup. Requires explicit -wallet=<file or directory>, -dumpfile=<new file>, and -chain=<main|test|testnet4|signet|regtest>. Relative paths use the current directory. No source writes, node or migration. The export may contain unencrypted spending material.", {"-dumpfile", "-stdinwalletpassphrase"});
    argsman.AddCommand("createfromdump", "Create new wallet file from dumped records", {"-dumpfile"});
}

// Inspect argv without copying values or logging them. Existing commands keep
// their parser/error contracts, including additional-argument diagnostics.
static bool IsDumpWalletInvocation(int argc, char* argv[])
{
    for (int i{1}; i < argc; ++i) {
        const std::string_view arg{argv[i]};
#ifdef WIN32
        const auto name{ToLower(arg.substr(0, arg.find('=')))};
#else
        const auto name{arg.substr(0, arg.find('='))};
#endif
        if (name == "dumpwallet") return true;
        if (name == "info" || name == "create" || name == "dump" || name == "createfromdump") return false;
    }
    return false;
}

// ArgsManager's early parse errors can contain arbitrary rejected values, and
// negation parsing may log them. Validate the new command's raw grammar first.
// Help/version need no source arguments; all other checks precede wallet I/O.
static std::optional<std::string> ValidateDumpWalletArguments(int argc, char* argv[], bool require_paths)
{
    std::map<std::string, unsigned> seen;
    bool command_seen{false};
    for (int i{1}; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "dumpwallet" && !command_seen) {
            command_seen = true;
            continue;
        }
        const bool option{arg.starts_with('-')
#ifdef WIN32
                          || arg.starts_with('/')
#endif
        };
        if (command_seen || !option) return "Additional or invalid positional arguments for dumpwallet (values omitted)";
        const auto equal{arg.find('=')};
        std::string name{arg.substr(0, equal)};
        const auto value{equal == arg.npos ? std::string_view{} : arg.substr(equal + 1)};
        if (name == "-wallet" || name == "-dumpfile" || name == "-chain") {
            if (equal == arg.npos || value.empty()) return "dumpwallet requires nonempty -wallet=, -dumpfile= and -chain= values";
            if (name == "-chain" && value != "main" && value != "test" && value != "testnet4" && value != "signet" && value != "regtest") {
                return "Unsupported -chain for dumpwallet (value omitted)";
            }
            if ((name == "-wallet" || name == "-dumpfile") && value == "-") return "dumpwallet requires source and destination filesystem paths, never '-'";
        } else if (name == "-stdinwalletpassphrase") {
            if (equal != arg.npos) return "-stdinwalletpassphrase must be a bare switch (value omitted)";
        } else {
            // Generic help/version, datadir and logging options retain their
            // normal parser semantics; chain aliases/customization do not.
#ifdef WIN32
            name = ToLower(name);
            if (name.starts_with('/')) name[0] = '-';
#endif
            if (name.starts_with("--")) name.erase(0, 1);
            if (name == "-nodebug" || name == "-noprinttoconsole" || name == "-nohelp" || name == "-nohelp-debug" || name == "-noversion" || name == "-noh" || name == "-no?") {
                if (equal != arg.npos && !value.empty() && value != "0" && value != "1") return "Invalid negated generic option for dumpwallet (value omitted)";
                name.erase(0, 3);
            } else if (name == "-debug" || name == "-printtoconsole" || name == "-help" || name == "-help-debug" || name == "-version" || name == "-h" || name == "-?") {
                name.erase(0, 1);
            } else if (name == "-datadir") {
                name.erase(0, 1);
            } else {
                return "Unsupported option for dumpwallet (argument values omitted)";
            }
        }
        if (++seen[name] != 1) return "Duplicate option for dumpwallet (argument values omitted)";
    }
    if (require_paths && (!seen.contains("-wallet") || !seen.contains("-dumpfile") || !seen.contains("-chain"))) {
        return "dumpwallet requires explicit -wallet=, -dumpfile= and -chain= exactly once";
    }
    return std::nullopt;
}

static std::optional<int> WalletAppInit(ArgsManager& args, int argc, char* argv[])
{
    SetupWalletToolArgs(args);
    const bool dumpwallet{IsDumpWalletInvocation(argc, argv)};
    if (dumpwallet) {
        if (const auto error{ValidateDumpWalletArguments(argc, argv, /*require_paths=*/false)}) {
            tfm::format(std::cerr, "Error: %s\n", *error);
            return EXIT_FAILURE;
        }
    }
    std::string error_message;
    if (!args.ParseParameters(argc, argv, error_message)) {
        tfm::format(std::cerr, "Error parsing command line arguments: %s\n", dumpwallet ? "Invalid dumpwallet arguments (values omitted)" : error_message);
        return EXIT_FAILURE;
    }
    const bool missing_args{argc < 2};
    if (missing_args || HelpRequested(args) || args.GetBoolArg("-version", false)) {
        std::string strUsage = strprintf("%s bitcoin-wallet utility version", CLIENT_NAME) + " " + FormatFullVersion() + "\n";

        if (args.GetBoolArg("-version", false)) {
            strUsage += FormatParagraph(LicenseInfo());
        } else {
            strUsage += "\n"
                "bitcoin-wallet is an offline tool for creating and interacting with " CLIENT_NAME " wallet files.\n\n"
                "By default bitcoin-wallet will act on wallets in the default mainnet wallet directory in the datadir.\n\n"
                "To change the target wallet, use the -datadir, -wallet and (test)chain selection arguments.\n"
                "\n"
                "Usage: bitcoin-wallet [options] <command>\n"
                "\n";
            strUsage += "\n" + args.GetHelpMessage();
        }
        tfm::format(std::cout, "%s", strUsage);
        if (missing_args) {
            tfm::format(std::cerr, "Error: too few parameters\n");
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    if (dumpwallet) {
        if (const auto error{ValidateDumpWalletArguments(argc, argv, /*require_paths=*/true)}) {
            tfm::format(std::cerr, "Error: %s\n", *error);
            return EXIT_FAILURE;
        }
    }

    // check for printtoconsole, allow -debug
    LogInstance().m_print_to_console = args.GetBoolArg("-printtoconsole", args.GetBoolArg("-debug", false));

    if (!CheckDataDirOption(args)) {
        if (dumpwallet) {
            tfm::format(std::cerr, "Error: Specified data directory does not exist (value omitted).\n");
        } else {
            tfm::format(std::cerr, "Error: Specified data directory \"%s\" does not exist.\n", args.GetArg("-datadir", ""));
        }
        return EXIT_FAILURE;
    }
    // Check for chain settings (Params() calls are only valid after this clause)
    SelectParams(args.GetChainType());

    return std::nullopt;
}

MAIN_FUNCTION
{
    ArgsManager& args = gArgs;

    int exit_status;
    std::unique_ptr<interfaces::Init> init = interfaces::MakeWalletInit(argc, argv, exit_status);
    if (!init) {
        return exit_status;
    }

    SetupEnvironment();
    SHA256AutoDetect();
    RandomInit();
    std::unique_ptr<wallet::WalletTool::LegacyWalletToolSignals> export_signals;
    try {
        if (IsDumpWalletInvocation(argc, argv)) export_signals = std::make_unique<wallet::WalletTool::LegacyWalletToolSignals>();
        if (const auto maybe_exit{WalletAppInit(args, argc, argv)}) return *maybe_exit;
    } catch (const std::exception& e) {
        if (IsDumpWalletInvocation(argc, argv)) {
            tfm::format(std::cerr, "Error: Cannot initialize dumpwallet (argument values omitted).\n");
            return EXIT_FAILURE;
        }
        PrintExceptionContinue(&e, "WalletAppInit()");
        return EXIT_FAILURE;
    } catch (...) {
        PrintExceptionContinue(nullptr, "WalletAppInit()");
        return EXIT_FAILURE;
    }

    const auto command = args.GetCommand();
    if (!command) {
        tfm::format(std::cerr, "No method provided. Run `bitcoin-wallet -help` for valid methods.\n");
        return EXIT_FAILURE;
    }
    if (command->args.size() != 0) {
        if (command->command == "dumpwallet") {
            tfm::format(std::cerr, "Error: Additional arguments for dumpwallet (values omitted).\n");
            return EXIT_FAILURE;
        }
        tfm::format(std::cerr, "Error: Additional arguments provided (%s). Methods do not take arguments. Please refer to `-help`.\n", Join(command->args, ", "));
        return EXIT_FAILURE;
    }

    ECC_Context ecc_context{};
    if (!wallet::WalletTool::ExecuteWalletToolFunc(args, command->command)) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
