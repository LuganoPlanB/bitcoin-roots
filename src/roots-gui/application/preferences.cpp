// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "application/preferences.h"
namespace roots::gui {
bool ValidPreferences(const PublicPreferences& value) { return value.refresh_seconds >= 1 && value.refresh_seconds <= 60; }
bool SavePreferences(const PublicPreferences& value, PreferenceRequests& service)
{
    return ValidPreferences(value) && service.Save(value);
}
void CancelPreferences(PublicPreferences& draft, const PublicPreferences& saved, PreferenceRequests& service)
{
    draft = saved;
    service.Cancel();
}
}
