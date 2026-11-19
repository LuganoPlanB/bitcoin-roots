// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include "adapters/storage/metadata.h"

namespace roots::gui::storage {

// Precisely scoped local labels and exclusions. No database, document, profile,
// approval, private key, cached observation or daemon recovery material.
struct WalletMetadataArchive {
    Scope scope;
    std::string display_name;
    std::vector<Label> labels;
    std::vector<Target> freezes;
    bool operator==(const WalletMetadataArchive&) const = default;
};

inline constexpr size_t WALLET_METADATA_ARCHIVE_MAX = 2 * 1024 * 1024;

bool WalletMetadataBackupAvailable();
Result<Done> WriteWalletMetadataArchive(const std::filesystem::path&, std::string_view password,
                                        const WalletMetadataArchive&);
Result<WalletMetadataArchive> ReadWalletMetadataArchive(const std::filesystem::path&, std::string_view password);
bool ValidWalletMetadataArchive(const WalletMetadataArchive&);

}
