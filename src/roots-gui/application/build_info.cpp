// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "application/build_info.h"
#include <curl/curl.h>
#include <sqlite3.h>
#include <wally_core.h>
#include <cstdint>
#include <stdexcept>
namespace roots::gui {
std::string ProviderBuildInfo()
{
    std::uint32_t version{};
    if (wally_get_build_version(&version) != WALLY_OK) throw std::runtime_error("provider version unavailable");
    return "\"wally\":" + std::to_string(version) + ",\"curl\":" + std::to_string(curl_version_info(CURLVERSION_NOW)->version_num) +
           ",\"sqlite\":" + std::to_string(sqlite3_libversion_number());
}
}
