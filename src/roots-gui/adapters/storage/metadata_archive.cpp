// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/storage/metadata_archive.h"

#include "adapters/rpc/json.h"
#include "adapters/storage/protected_file.h"
#include "services/transaction_documents.h"
#include <algorithm>
#include <sodium.h>

namespace roots::gui::storage {
namespace {
constexpr std::string_view VERSION = "ROOTS-GUI-WALLET-METADATA-1";
using J = rpc::Json;

struct PrivateText {
    std::string bytes;
    ~PrivateText() { if (!bytes.empty()) sodium_memzero(bytes.data(), bytes.size()); }
};

void AppendPart(std::string& text, std::string_view value)
{
    rpc::AppendQuoted(text, value);
}

bool CanUsePath(const std::filesystem::path& path, std::string_view password)
{
    const auto name = path.filename();
    return password.size() >= 12 && password.size() <= 255 && password.find('\0') == std::string_view::npos
        && ValidLocalDocumentPath(path.string()) && !name.empty() && name != "." && name != "..";
}

std::optional<Target> ParseTarget(const std::vector<J>& row)
{
    if (row.size() < 2 || row[0].kind != J::Kind::Number || row[1].kind != J::Kind::String
        || row[0].text.size() != 1 || row[0].text.front() < '0' || row[0].text.front() > '2') return {};
    return Target{TargetKind(row[0].text.front() - '0'), row[1].text};
}

std::optional<WalletMetadataArchive> Decode(std::string_view text)
{
    const auto value = J::Parse(text, 5, 30000);
    if (!value || value->kind != J::Kind::Array || value->items.size() != 5) return {};
    const auto& item = value->items;
    if (item[0].kind != J::Kind::String || item[0].text != VERSION || item[1].kind != J::Kind::Array
        || item[1].items.size() != 3 || item[2].kind != J::Kind::String
        || item[3].kind != J::Kind::Array || item[3].items.size() > 4096
        || item[4].kind != J::Kind::Array || item[4].items.size() > 4096) return {};
    const auto& identity = item[1].items;
    if (!std::ranges::all_of(identity, [](const auto& part) { return part.kind == J::Kind::String; })) return {};
    const auto genesis = Hash256::Parse(identity[1].text);
    if (!genesis || genesis->hex() != identity[1].text) return {};
    WalletMetadataArchive result{{identity[0].text, {*genesis}, identity[2].text}, item[2].text, {}, {}};
    for (const auto& entry : item[3].items) {
        if (entry.kind != J::Kind::Array || entry.items.size() != 3 || entry.items[2].kind != J::Kind::String) return {};
        auto target = ParseTarget(entry.items);
        if (!target) return {};
        result.labels.push_back({std::move(*target), entry.items[2].text});
    }
    for (const auto& entry : item[4].items) {
        if (entry.kind != J::Kind::Array || entry.items.size() != 2) return {};
        auto target = ParseTarget(entry.items);
        if (!target) return {};
        result.freezes.push_back(std::move(*target));
    }
    if (!ValidWalletMetadataArchive(result)) return {};
    return result;
}
}

bool WalletMetadataBackupAvailable()
{
#if defined(__linux__)
    return true;
#else
    return false;
#endif
}

bool ValidWalletMetadataArchive(const WalletMetadataArchive& archive)
{
    return !archive.display_name.empty() && archive.display_name.size() <= 255
        && ValidMetadata(archive.scope, Metadata{.display_name=archive.display_name, .labels=archive.labels, .freezes=archive.freezes});
}

Result<Done> WriteWalletMetadataArchive(const std::filesystem::path& path, std::string_view password,
                                        const WalletMetadataArchive& archive)
{
    try {
        if (!WalletMetadataBackupAvailable()) return {{}, Error::Unsupported};
        if (!CanUsePath(path, password) || !ValidWalletMetadataArchive(archive)) return {{}, Error::Invalid};
        const auto current = std::filesystem::symlink_status(path);
        if (current.type() != std::filesystem::file_type::not_found) return {{}, Error::Conflict};
        PrivateText plaintext;
        auto& text = plaintext.bytes;
        text.push_back('[');
        AppendPart(text, VERSION);
        text += ",[";
        AppendPart(text, archive.scope.backend);
        text.push_back(',');
        AppendPart(text, archive.scope.network.genesis.hex());
        text.push_back(',');
        AppendPart(text, archive.scope.daemon_wallet);
        text += "],";
        AppendPart(text, archive.display_name);
        text += ",[";
        const auto target = [&](const Target& value) {
            text.push_back('[');
            text.push_back(char('0' + int(value.kind)));
            text.push_back(',');
            AppendPart(text, value.key);
        };
        for (size_t i = 0; i < archive.labels.size(); ++i) {
            if (i) text.push_back(',');
            target(archive.labels[i].target);
            text.push_back(',');
            AppendPart(text, archive.labels[i].text);
            text.push_back(']');
            if (text.size() > WALLET_METADATA_ARCHIVE_MAX) return {{}, Error::Resource};
        }
        text += "],[";
        for (size_t i = 0; i < archive.freezes.size(); ++i) {
            if (i) text.push_back(',');
            target(archive.freezes[i]);
            text.push_back(']');
            if (text.size() > WALLET_METADATA_ARCHIVE_MAX) return {{}, Error::Resource};
        }
        text += "]]";
        if (text.size() > WALLET_METADATA_ARCHIVE_MAX) return {{}, Error::Resource};
        auto file = ProtectedFile::Open(path, password, WALLET_METADATA_ARCHIVE_MAX);
        if (!file) return {{}, file.error};
        return (*file.value)->SaveNew({reinterpret_cast<const unsigned char*>(text.data()), text.size()});
    } catch (const std::bad_alloc&) { return {{}, Error::Resource}; }
    catch (...) { return {{}, Error::Io}; }
}

Result<WalletMetadataArchive> ReadWalletMetadataArchive(const std::filesystem::path& path, std::string_view password)
{
    try {
        if (!WalletMetadataBackupAvailable()) return {{}, Error::Unsupported};
        if (!CanUsePath(path, password)) return {{}, Error::Invalid};
        auto file = ProtectedFile::Open(path, password, WALLET_METADATA_ARCHIVE_MAX, true);
        if (!file) return {{}, file.error};
        auto plaintext = (*file.value)->Read();
        if (!plaintext) return {{}, plaintext.error};
        if (plaintext.value->bytes.empty()) return {{}, Error::Corrupt};
        auto archive = Decode({reinterpret_cast<const char*>(plaintext.value->bytes.data()), plaintext.value->bytes.size()});
        if (!archive) return {{}, Error::Corrupt};
        return {std::move(*archive), Error::None};
    } catch (const std::bad_alloc&) { return {{}, Error::Resource}; }
    catch (...) { return {{}, Error::Io}; }
}

}
