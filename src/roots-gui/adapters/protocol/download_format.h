// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include "services/download_verification.h"

namespace roots::gui::protocol {

struct ManifestEntry {
    DownloadIntegrity outcome{DownloadIntegrity::UnsupportedManifest};
    std::string hash;
};

ManifestEntry FindManifestEntry(std::string_view manifest, std::string_view artifact);
std::optional<std::string> PublicOpenPgpKey(std::string_view);
std::optional<std::string> DetachedOpenPgpSignature(std::string_view);
void InterpretOpenPgpStatus(std::string_view status, int exit_code, std::string_view expected, DownloadResult&);

}
