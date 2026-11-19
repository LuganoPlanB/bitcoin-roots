// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <string>
#include <string_view>
namespace roots::gui {
enum class ExplorerMode { Disabled, NetworkDefault, Custom };
struct PublicPreferences {
    int refresh_seconds{5};
    bool hide_balances{false};
    bool trim_decimals{};
    bool group_address_outputs{true};
    ExplorerMode explorer{ExplorerMode::Disabled};
    std::string explorer_template;
    std::string explorer_genesis;
    bool desktop_notifications{};
    bool camera_keep_awake{};
    bool operator==(const PublicPreferences&) const = default;
};
// Never put credentials, wallet secrets or transaction authority here.
class PreferenceRequests {
public:
    virtual ~PreferenceRequests() = default;
    virtual bool Save(const PublicPreferences&) = 0;
    virtual void Cancel() = 0;
};
bool ValidPreferences(const PublicPreferences&);
bool ValidExplorerTemplate(std::string_view);
bool SavePreferences(const PublicPreferences&, PreferenceRequests&);
void CancelPreferences(PublicPreferences& draft, const PublicPreferences& saved, PreferenceRequests&);
}
