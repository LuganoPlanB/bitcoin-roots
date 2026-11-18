// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/wallet_lifecycle.h"
#include <algorithm>
#include <charconv>
#include <array>
#include <sodium.h>

namespace roots::gui {
namespace {
using J = rpc::Json;
constexpr size_t MAX_WALLETS = 128;
constexpr size_t MAX_UNCERTAIN = 64;

bool Registered(const BackendCapabilities& backend, const char* method)
{
    const auto found = backend.methods.find(method);
    return backend.CanApply() && found != backend.methods.end() && found->second.registered;
}

constexpr std::array CREATE_ARGS{"wallet_name", "disable_private_keys", "blank", "descriptors", "external_signer"};
constexpr std::array IMPORT_ARGS{"requests", "requests.desc", "requests.active", "requests.range", "requests.timestamp", "requests.internal"};
constexpr unsigned IMPORT_LAST_INDEX = 99;

bool Named(const BackendCapabilities& backend, const char* method, const char* argument)
{
    if (!Registered(backend, method)) return false;
    const auto& support = backend.methods.at(method);
    return support.schema_proven && !support.probe_rejected && support.arguments.contains(argument);
}

bool NewWallet(WalletAction action)
{
    return action == WalletAction::Create || action == WalletAction::Import || action == WalletAction::Restore;
}

bool Mutates(WalletAction action)
{
    return NewWallet(action) || action == WalletAction::Load || action == WalletAction::Backup;
}

std::string MutationKey(const BackendCapabilities& backend, std::string_view wallet)
{
    if (sodium_init() < 0) return {};
    const auto& connection = backend.candidate.connection;
    const auto input = std::string(connection.tls ? "https\n" : "http\n") + rpc::Quote(connection.host)
        + '\n' + std::to_string(connection.port) + '\n' + backend.genesis + '\n' + rpc::Quote(wallet);
    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    if (crypto_hash_sha256(digest.data(), reinterpret_cast<const unsigned char*>(input.data()), input.size()) != 0) return {};
    std::array<char, crypto_hash_sha256_BYTES * 2 + 1> hex{};
    sodium_bin2hex(hex.data(), hex.size(), digest.data(), digest.size());
    return hex.data();
}

std::string BackupKey(const BackendCapabilities& backend, std::string_view wallet, std::string_view path)
{
    return MutationKey(backend, "backup\n" + rpc::Quote(wallet) + '\n' + rpc::Quote(path));
}

bool StringIs(const J* value, std::string_view expected)
{
    return value && value->kind == J::Kind::String && value->text == expected;
}

bool BoolIs(const J* value, bool expected)
{
    return value && value->kind == J::Kind::Boolean && (value->text == "true") == expected;
}

bool ReadNames(const rpc::Reply& reply, bool directory, std::set<std::string>& names)
{
    if (reply.error != rpc::Error::None || !reply.result) return false;
    const auto* rows = directory ? reply.result->Find("wallets") : &*reply.result;
    if (!rows || rows->kind != J::Kind::Array || rows->items.size() > MAX_WALLETS) return false;
    for (const auto& row : rows->items) {
        const auto* name = directory ? row.Find("name") : &row;
        if (!name || name->kind != J::Kind::String || !ValidDaemonWalletName(name->text) || !names.insert(name->text).second) return false;
    }
    return true;
}
}

bool ValidDaemonWalletName(std::string_view name)
{
    return name.size() <= 1024 && rpc::ValidUtf8(name)
        && std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; });
}

std::string NewWalletNameError(std::string_view name)
{
    if (name.empty()) return "Enter a name for the new wallet.";
    if (name.size() > 80) return "Use at most 80 characters.";
    const auto ascii_word = [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };
    if (!ascii_word(name.front()) || name.back() == ' ' || name.back() == '.'
        || !std::all_of(name.begin(), name.end(), [&](unsigned char c) { return ascii_word(c) || c == '-' || c == '_' || c == ' ' || c == '.'; })) {
        return "Start with a letter or digit. Use letters, digits, spaces, dots, hyphens or underscores. End without a space or dot.";
    }
    auto stem = std::string(name.substr(0, name.find('.')));
    while (stem.ends_with(' ')) stem.pop_back();
    for (char& c : stem) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL"
        || (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem[3] >= '0' && stem[3] <= '9')) {
        return "Choose a name other than a reserved device filename.";
    }
    return {};
}

bool CanListWallets(const BackendCapabilities& backend)
{
    return Registered(backend, "listwallets") && Registered(backend, "getwalletinfo");
}

bool CanLoadWallet(const BackendCapabilities& backend)
{
    return CanListWallets(backend) && Registered(backend, "listwalletdir")
        && backend.Supports("loadwallet", "filename") == SupportReason::Available;
}

bool CanCreateWallet(const BackendCapabilities& backend)
{
    return CanListWallets(backend) && Registered(backend, "listwalletdir")
        && std::all_of(CREATE_ARGS.begin(), CREATE_ARGS.end(), [&](auto arg) {
            return backend.Supports("createwallet", arg) == SupportReason::Available;
        });
}

bool CanImportWallet(const BackendCapabilities& backend)
{
    return CanCreateWallet(backend) && Named(backend, "getdescriptorinfo", "descriptor") && Named(backend, "getaddressinfo", "address")
        && std::ranges::all_of(IMPORT_ARGS, [&](auto arg) { return Named(backend, "importdescriptors", arg); });
}

bool CanRestoreWallet(const BackendCapabilities& backend)
{
    return CanListWallets(backend) && Registered(backend, "listwalletdir")
        && Named(backend, "restorewallet", "backup_file") && Named(backend, "restorewallet", "wallet_name");
}

bool CanBackupWallet(const BackendCapabilities& backend)
{
    return backend.candidate.wallet && backend.descriptors == true
        && backend.Supports("backupwallet", "destination") == SupportReason::Available;
}

bool CanReadWalletPolicy(const BackendCapabilities& backend)
{
    return backend.CanApply() && backend.wallet == WalletAvailability::Available && backend.candidate.wallet
        && backend.descriptors == true && !protocol::ReceiveChain(backend.genesis).empty()
        && protocol::ReceiveChain(backend.genesis) == backend.chain
        && backend.Supports("getwalletinfo") == SupportReason::Available
        && backend.Supports("listdescriptors", "private") == SupportReason::Available;
}

std::optional<SavedWalletPolicy> ValidateWalletPolicy(const rpc::Json& value, std::string_view wallet, std::string_view chain)
{
    const auto* descriptors = value.Find("descriptors");
    if (!StringIs(value.Find("wallet_name"),wallet) || !descriptors || descriptors->kind != J::Kind::Array
        || descriptors->items.size() > 64 || chain.empty()) return {};
    SavedWalletPolicy result{std::string(chain)};
    const auto number = [](const J* item) -> std::optional<uint32_t> {
        uint32_t n{};
        if (!item || item->kind != J::Kind::Number || item->text.empty()) return {};
        const auto parsed = std::from_chars(item->text.data(),item->text.data()+item->text.size(),n);
        if (parsed.ec != std::errc{} || parsed.ptr != item->text.data()+item->text.size() || n > 0x80000000U) return {};
        return n;
    };
    std::set<std::string> seen;
    for (const auto& entry : descriptors->items) {
        const auto* text = entry.Find("desc");
        const auto* active = entry.Find("active");
        const auto* internal = entry.Find("internal");
        if (!text || text->kind != J::Kind::String || text->text.find('#') == std::string::npos
            || !active || active->kind != J::Kind::Boolean || (internal && internal->kind != J::Kind::Boolean)
            || (BoolIs(active,true) && !internal)) return {};
        auto proof = protocol::PreviewPublicWallet(protocol::PublicImportKind::Descriptor,text->text,chain);
        if (!proof.preview || proof.preview->descriptors.front().descriptor != text->text) {
            ++result.unsupported;
            continue;
        }
        auto descriptor = std::move(proof.preview->descriptors.front());
        if (!seen.insert(descriptor.descriptor).second) return {};
        descriptor.internal = BoolIs(internal,true);
        std::optional<PolicyRange> indices;
        const auto* range = entry.Find("range");
        const auto* next = entry.Find("next_index");
        if (descriptor.ranged) {
            if (!range || range->kind != J::Kind::Array || range->items.size() != 2 || !next) return {};
            const auto first = number(&range->items[0]), last = number(&range->items[1]), cursor = number(next);
            if (const auto* old_next = entry.Find("next"); old_next && number(old_next) != cursor) return {};
            if (!first || !last || !cursor || *first > *last || *last >= 0x80000000U
                || *cursor < *first || *cursor > *last + 1) return {};
            indices = PolicyRange{*first,*last,*cursor};
        } else if (range || next || BoolIs(active,true)) return {};
        result.branches.push_back({std::move(descriptor),BoolIs(active,true),internal != nullptr,indices});
    }
    return result;
}

bool ValidDaemonWalletPath(std::string_view path)
{
    if (!ValidDaemonWalletName(path) || path.empty() || path.back() == '/' || path.back() == '\\'
        || path.front() == ' ' || path.back() == ' ' || path.find("://") != std::string_view::npos) return false;
    if (path.starts_with('/') && !path.starts_with("//")) path.remove_prefix(1);
    else if (path.size() >= 4 && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
        && path[1] == ':' && (path[2] == '/' || path[2] == '\\')) path.remove_prefix(3);
    else return false;
    if (path.find_first_of(":<>|\"?*") != std::string_view::npos) return false;
    for (;;) {
        const auto separator = path.find_first_of("/\\");
        const auto item = path.substr(0, separator);
        if (item.empty() || item == "." || item == "..") return false;
        if (separator == std::string_view::npos) return true;
        path.remove_prefix(separator + 1);
    }
}

WalletResult RunWalletOperation(rpc::Transport& transport, const WalletOperation& work, std::stop_token stop)
{
    WalletResult result;
    const auto& backend = work.backend;
    auto fail = [&](std::string message) {
        result.error = std::move(message);
        return std::move(result);
    };
    const auto action = work.action;
    if (!ValidDaemonWalletName(work.name) || (action != WalletAction::Backup && action != WalletAction::ReadPolicy && !CanListWallets(backend))
        || (NewWallet(action) && !NewWalletNameError(work.name).empty())
        || (action == WalletAction::ReadPolicy && (!CanReadWalletPolicy(backend) || backend.candidate.wallet != work.name))
        || (action == WalletAction::Load && !CanLoadWallet(backend))
        || (action == WalletAction::Create && !CanCreateWallet(backend))
        || (action == WalletAction::Restore && (!CanRestoreWallet(backend) || !ValidDaemonWalletPath(work.transfer.daemon_path)))
        || (action == WalletAction::Backup && (!CanBackupWallet(backend) || backend.candidate.wallet != work.name || !ValidDaemonWalletPath(work.transfer.daemon_path)))
        || (action == WalletAction::Import && (!CanImportWallet(backend) || !work.transfer.preview || !protocol::VerifyPublicPreview(*work.transfer.preview, backend.chain)))) {
        return fail("This wallet operation is unavailable for the verified daemon or name.");
    }
    uint64_t id{};
    auto call = [&](std::string method, std::string params = "[]", rpc::Effect effect = rpc::Effect::ReadOnly, std::optional<std::string> wallet = {}) {
        rpc::Limits limits;
        const bool file_or_scan = method == "importdescriptors" || method == "restorewallet" || method == "backupwallet";
        limits.total_timeout = file_or_scan ? std::chrono::minutes(5) : std::chrono::seconds(8);
        limits.response_bytes = 1024 * 1024;
        return transport.Call(backend.candidate.connection, {++id, std::move(method), std::move(params), std::move(wallet), effect}, stop, limits);
    };
    const auto genesis = call("getblockhash", "[0]");
    if (genesis.error != rpc::Error::None || !genesis.result || genesis.result->kind != J::Kind::String
        || genesis.result->text != backend.genesis) return fail("Current authenticated daemon network could not be verified.");

    if (action == WalletAction::ReadPolicy) {
        const auto info = call("getwalletinfo", "[]", rpc::Effect::ReadOnly, work.name);
        if (info.error != rpc::Error::None || !info.result || !StringIs(info.result->Find("walletname"), work.name)
            || !BoolIs(info.result->Find("descriptors"), true) || stop.stop_requested()) return fail("The wallet identity could not be verified.");
        const auto observed = call("listdescriptors", "{\"private\":false}", rpc::Effect::ReadOnly, work.name);
        if (observed.error != rpc::Error::None || !observed.result || stop.stop_requested())
            return fail("The daemon did not disclose this wallet's public descriptors.");
        auto policy = ValidateWalletPolicy(*observed.result,work.name,backend.chain);
        if (!policy) return fail("The daemon policy or descriptor range could not be verified.");
        const auto verified = call("getblockhash", "[0]");
        if (verified.error != rpc::Error::None || !verified.result || !StringIs(&*verified.result,backend.genesis)
            || stop.stop_requested()) return fail("The authenticated network changed while reading the policy.");
        result.policy = std::move(policy);
        return result;
    }

    if (action == WalletAction::Backup) {
        const auto info = call("getwalletinfo", "[]", rpc::Effect::ReadOnly, work.name);
        if (info.error != rpc::Error::None || !info.result || !StringIs(info.result->Find("walletname"), work.name)
            || !BoolIs(info.result->Find("descriptors"), true) || stop.stop_requested()) return fail("The wallet could not be verified for backup.");
        result.mutation_dispatched = true;
        const auto response = call("backupwallet", "{\"destination\":" + rpc::Quote(work.transfer.daemon_path) + '}', rpc::Effect::Mutation, work.name);
        if (response.error != rpc::Error::None || !response.result || response.result->kind != J::Kind::Null) {
            result.mutation_uncertain = response.mutation_uncertain || response.error == rpc::Error::None;
            return fail(result.mutation_uncertain ? "The backup may have been written. Inspect the destination on the daemon host before choosing another action."
                : "The daemon did not acknowledge the backup request.");
        }
        result.backup_written = true;
        return result;
    }

    std::set<std::string> loaded, directory;
    if (!ReadNames(call("listwallets"), false, loaded)) return fail("The loaded wallet list could not be verified.");
    if (Registered(backend, "listwalletdir")) {
        result.directory_verified = ReadNames(call("listwalletdir"), true, directory);
        if (!result.directory_verified) directory.clear();
    }
    if (work.action == WalletAction::List) {
        for (const auto& name : loaded) result.entries.push_back({name, true});
        for (const auto& name : directory) if (!loaded.contains(name)) result.entries.push_back({name, false});
        if (result.entries.size() > MAX_WALLETS) {
            result.entries.clear();
            return fail("Wallet list exceeds the display limit.");
        }
        return result;
    }
    if (work.action == WalletAction::Open && !loaded.contains(work.name)) {
        return fail("This wallet is no longer loaded. Refresh the wallet list to continue.");
    }
    if (work.action == WalletAction::Load && (!result.directory_verified || !directory.contains(work.name) || loaded.contains(work.name))) {
        return fail("Wallet load state changed. Refresh the wallet list to continue.");
    }
    if (NewWallet(action)) {
        if (!result.directory_verified) return fail("The daemon wallet directory is unavailable. A verified lookup for duplicate names is required.");
        if (loaded.contains(work.name) || directory.contains(work.name)) return fail("That wallet name already exists on the daemon.");
    }
    if (action == WalletAction::Import) {
        const auto chain = call("getblockchaininfo");
        if (chain.error != rpc::Error::None || !chain.result || !StringIs(chain.result->Find("chain"), backend.chain)
            || !BoolIs(chain.result->Find("pruned"), false)) return fail("Full-history import requires the current verified, unpruned daemon.");
        for (const auto& descriptor : work.transfer.preview->descriptors) {
            const auto check = call("getdescriptorinfo", "{\"descriptor\":" + rpc::Quote(descriptor.descriptor) + '}');
            if (check.error != rpc::Error::None || !check.result || !StringIs(check.result->Find("descriptor"), descriptor.descriptor)
                || !BoolIs(check.result->Find("hasprivatekeys"), false) || !BoolIs(check.result->Find("issolvable"), true)
                || !BoolIs(check.result->Find("isrange"), descriptor.ranged) || check.result->Find("multipath_expansion")) {
                return fail("The daemon did not confirm the exact public descriptor. No wallet was created.");
            }
        }
    }
    if (stop.stop_requested()) return fail("Wallet request cancelled.");
    if (Mutates(action)) {
        result.mutation_dispatched = true;
        const auto name = rpc::Quote(work.name);
        rpc::Reply reply;
        if (action == WalletAction::Restore) {
            reply = call("restorewallet", "{\"wallet_name\":" + name + ",\"backup_file\":" + rpc::Quote(work.transfer.daemon_path) + '}', rpc::Effect::Mutation);
        } else if (action == WalletAction::Load) {
            reply = call("loadwallet", "{\"filename\":" + name + '}', rpc::Effect::Mutation);
        } else {
            const std::string blank = action == WalletAction::Import ? "true" : "false";
            reply = call("createwallet", "{\"wallet_name\":" + name + ",\"disable_private_keys\":" + blank
                + ",\"blank\":" + blank + ",\"descriptors\":true,\"external_signer\":false}", rpc::Effect::Mutation);
        }
        const auto* actual = reply.result ? reply.result->Find("name") : nullptr;
        if (reply.error != rpc::Error::None || !actual || actual->kind != J::Kind::String || actual->text != work.name) {
            result.mutation_uncertain = reply.mutation_uncertain || reply.error == rpc::Error::None;
            return fail(result.mutation_uncertain ? "The daemon may have changed wallet state. Refresh the wallet list before choosing the next action."
                : "The daemon did not acknowledge the wallet request. Refresh the wallet list before retrying.");
        }
    }

    auto candidate = backend.candidate;
    candidate.wallet = work.name;
    auto wallet = ProbeCapabilities(transport, candidate, stop);
    if (stop.stop_requested() || !wallet.CanApply() || wallet.wallet != WalletAvailability::Available || wallet.descriptors != true) {
        result.mutation_uncertain = result.mutation_dispatched;
        return fail(result.mutation_uncertain ? "The daemon acknowledged the request, but wallet identity could not be verified. Refresh the wallet list."
            : "The selected descriptor wallet could not be verified.");
    }
    if (action == WalletAction::Import) {
        const auto partial = [&] {
            result.mutation_uncertain = true;
            return fail("The watch-only wallet was created, but its public import could not be fully verified. Inspect the wallet on the daemon before taking another action.");
        };
        if (wallet.private_keys != false || wallet.external_signer != false || !CanImportWallet(wallet) || wallet.pruned || stop.stop_requested()) return partial();
        std::string params = "{\"requests\":[";
        const auto& descriptors = work.transfer.preview->descriptors;
        for (auto item = descriptors.begin(); item != descriptors.end(); ++item) {
            if (item != descriptors.begin()) params += ',';
            params += "{\"desc\":" + rpc::Quote(item->descriptor) + ",\"timestamp\":0,\"active\":" + (item->ranged ? "true" : "false")
                + ",\"internal\":" + (item->internal ? "true" : "false");
            if (item->ranged) params += ",\"range\":[0," + std::to_string(IMPORT_LAST_INDEX) + ']';
            params += '}';
        }
        params += "]}";
        const auto imported = call("importdescriptors", params, rpc::Effect::Mutation, work.name);
        if (imported.error != rpc::Error::None || !imported.result || imported.result->kind != J::Kind::Array
            || imported.result->items.size() != descriptors.size()
            || !std::ranges::all_of(imported.result->items, [](const auto& item) { return BoolIs(item.Find("success"), true); })) return partial();
        for (const auto& item : descriptors) {
            const auto observed = call("getaddressinfo", "{\"address\":" + rpc::Quote(item.address) + '}', rpc::Effect::ReadOnly, work.name);
            if (observed.error != rpc::Error::None || !observed.result || !StringIs(observed.result->Find("address"), item.address)
                || !StringIs(observed.result->Find("scriptPubKey"), item.script) || !StringIs(observed.result->Find("parent_desc"), item.descriptor)
                || !BoolIs(observed.result->Find("ismine"), true)) return partial();
        }
    }
    result.wallet = std::move(wallet);
    return result;
}

WalletLifecycle::~WalletLifecycle() { Cancel(); }

bool WalletLifecycle::Uncertain(const BackendCapabilities& backend, std::string_view name) const
{
    return uncertain.contains(MutationKey(backend, name));
}

bool WalletLifecycle::BackupUncertain(const BackendCapabilities& backend, std::string_view name, std::string_view path) const
{
    return uncertain.contains(BackupKey(backend, name, path));
}

bool WalletLifecycle::Start(WalletAction action, std::string name, const ConnectionService& connection, LockEpoch epoch, uint64_t document, WalletTransfer transfer)
{
    if (std::this_thread::get_id() != owner || !run || Busy() || connection.Offline() || connection.PrivacyLocked()
        || !connection.Active() || (action != WalletAction::Backup && action != WalletAction::ReadPolicy && !CanListWallets(*connection.Active())) || !ValidDaemonWalletName(name)) return false;
    const auto& active = *connection.Active();
    if (action == WalletAction::ReadPolicy && (!CanReadWalletPolicy(active) || active.candidate.wallet != name)) return false;
    std::string key;
    bool tracked = false;
    try {
        if (action == WalletAction::Backup) key = BackupKey(active, name, transfer.daemon_path);
        else if (action != WalletAction::List) key = MutationKey(active, name);
        if (Mutates(action)) {
            if ((NewWallet(action) && !NewWalletNameError(name).empty()) || (action == WalletAction::Load && !CanLoadWallet(active))
                || (action == WalletAction::Create && !CanCreateWallet(active))
                || (action == WalletAction::Restore && (!CanRestoreWallet(active) || !ValidDaemonWalletPath(transfer.daemon_path)))
                || (action == WalletAction::Import && (!CanImportWallet(active) || !transfer.preview || active.pruned))
                || (action == WalletAction::Backup && (!CanBackupWallet(active) || active.candidate.wallet != name || !ValidDaemonWalletPath(transfer.daemon_path)))) return false;
            if (key.empty() || uncertain.size() >= MAX_UNCERTAIN || !uncertain.insert(key).second) return false;
            tracked = true;
        }
        WalletOperation work{action, std::move(name), active, connection.Session(), connection.Revision(), document, epoch, std::move(transfer)};
        pending = Pending{action, work.session, work.revision, document, epoch, key, work.name};
        retired = false;
        worker = std::jthread([this, work = std::move(work)](std::stop_token stop) {
            WalletResult result;
            try {
                result = run(work, stop);
            } catch (...) {
                result.mutation_dispatched = result.mutation_uncertain = Mutates(work.action);
                result.error = "Wallet operation failed before its outcome could be verified.";
            }
            std::lock_guard guard(mutex);
            completed = std::move(result);
        });
    } catch (...) {
        if (tracked) uncertain.erase(key);
        pending.reset();
        return false;
    }
    return true;
}

void WalletLifecycle::Cancel()
{
    if (std::this_thread::get_id() != owner || !Busy()) return;
    retired = true;
    worker.request_stop();
}

std::optional<std::pair<WalletAction, WalletResult>> WalletLifecycle::Poll(const ConnectionService& connection, LockEpoch epoch, uint64_t document)
{
    if (std::this_thread::get_id() != owner || !pending) return {};
    if (connection.Offline() || connection.PrivacyLocked() || connection.Session() != pending->session
        || connection.Revision() != pending->revision || epoch != pending->epoch || document != pending->document) Cancel();
    std::optional<WalletResult> result;
    {
        std::lock_guard guard(mutex);
        if (!completed) return {};
        result = std::move(completed);
        completed.reset();
    }
    worker.join();
    const auto action = pending->action;
    if (result->wallet && result->wallet->candidate.wallet != pending->name) {
        result->wallet.reset();
        result->mutation_uncertain = result->mutation_dispatched;
        result->error = "The daemon wallet identity differed from the requested wallet.";
    }
    if (Mutates(action)) {
        if (!result->mutation_dispatched || (!retired && !result->mutation_uncertain)) uncertain.erase(pending->mutation_key);
    } else if (action == WalletAction::Open && !retired && result->wallet) {
        // An explicit, fresh loaded-wallet verification reconciles this identity.
        uncertain.erase(pending->mutation_key);
    }
    pending.reset();
    if (retired) return {};
    return std::pair{action, std::move(*result)};
}
}
