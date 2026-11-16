// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
namespace roots::gui {
struct PublicPreferences {
    int refresh_seconds{5};
    bool hide_balances{false};
    bool operator==(const PublicPreferences&) const = default;
};
// Public presentation preferences only. Persistence is a later adapter.
class PreferenceRequests {
public:
    virtual ~PreferenceRequests() = default;
    virtual bool Save(const PublicPreferences&) = 0;
    virtual void Cancel() = 0;
};
bool ValidPreferences(const PublicPreferences&);
bool SavePreferences(const PublicPreferences&, PreferenceRequests&);
void CancelPreferences(PublicPreferences& draft, const PublicPreferences& saved, PreferenceRequests&);
}
