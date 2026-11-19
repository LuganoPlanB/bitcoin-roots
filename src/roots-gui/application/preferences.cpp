// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "application/preferences.h"
#include <algorithm>
namespace roots::gui {
namespace {
bool Alphanumeric(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
}
bool ValidExplorerTemplate(std::string_view url)
{
    constexpr std::string_view scheme{"https://"}, placeholder{"{txid}"};
    if (url.size()>512 || !url.starts_with(scheme) || !url.ends_with(placeholder)) return false;
    url.remove_prefix(scheme.size());
    url.remove_suffix(placeholder.size());
    const auto slash=url.find('/');
    if (slash==std::string_view::npos) return false;
    const auto host=url.substr(0,slash);
    const auto path=url.substr(slash);
    if (host.empty() || host.size()>253 || host.back()=='.' || path.back()!='/') return false;
    if (!std::ranges::all_of(path,[](unsigned char c){return Alphanumeric(c) || c=='/' || c=='-' || c=='_' || c=='.';})) return false;
    // Avoid browser URL normalization, alternate IP literals, userinfo and
    // implicit SOCKS escape through a custom onion or local-only destination.
    if (path.find("//")!=std::string_view::npos || path.find("/./")!=std::string_view::npos || path.find("/../")!=std::string_view::npos) return false;
    size_t start{};
    for (auto end=host.find('.');;end=host.find('.',start)) {
        const auto label=host.substr(start,end==std::string_view::npos ? end : end-start);
        if (label.empty() || label.size()>63 || !Alphanumeric(label.front()) || !Alphanumeric(label.back())
            || !std::ranges::all_of(label,[](unsigned char c){return Alphanumeric(c) || c=='-';})) return false;
        if (end==std::string_view::npos) {
            std::string suffix(label);
            for (auto& c:suffix) if(c>='A' && c<='Z') c=char(c-'A'+'a');
            return start!=0 && suffix!="onion" && suffix!="local" && suffix!="localhost" && suffix!="internal"
                && std::ranges::all_of(suffix,[](unsigned char c){return c>='a' && c<='z';});
        }
        start=end+1;
    }
}
bool ValidPreferences(const PublicPreferences& value)
{
    if (value.refresh_seconds<1 || value.refresh_seconds>60) return false;
    switch(value.explorer) {
    case ExplorerMode::Disabled: case ExplorerMode::NetworkDefault:
        return value.explorer_template.empty() && value.explorer_genesis.empty();
    case ExplorerMode::Custom:
        return ValidExplorerTemplate(value.explorer_template) && value.explorer_genesis.size()==64
            && std::ranges::all_of(value.explorer_genesis,[](unsigned char c){return (c>='0' && c<='9') || (c>='a' && c<='f');});
    }
    return false;
}
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
