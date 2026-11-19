// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include <string>
#include <string_view>

namespace roots::gui::app {
enum class HelpTarget { Documentation, Support, Issues, Releases, Source };
std::string_view HelpUrl(HelpTarget);
bool ConfirmedHelpUrl(HelpTarget,std::string_view);

struct DesktopReceipt { bool accepted{}; std::string detail; };
enum class DesktopCapability { Minimize, Sleep, Notifications, Browser, Restart };
class DesktopActions {
public:
    virtual ~DesktopActions()=default;
    virtual DesktopReceipt Available(DesktopCapability) const=0;
    virtual DesktopReceipt Minimize()=0;
    virtual DesktopReceipt TestNotification()=0;
    virtual DesktopReceipt OpenHelp(HelpTarget,std::string_view confirmed)=0;
    virtual DesktopReceipt Restart()=0;
    virtual std::string Status() const{return {};}
};
}
