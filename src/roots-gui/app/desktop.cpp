// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/desktop.h"

namespace roots::gui::app {
std::string_view HelpUrl(HelpTarget target)
{
    switch(target){
    case HelpTarget::Documentation:return "https://github.com/luganoplanb/bitcoin-roots/tree/main/doc";
    case HelpTarget::Support:return "https://plan-b.foundation/bitcoin-roots";
    case HelpTarget::Issues:return "https://github.com/luganoplanb/bitcoin-roots/issues";
    case HelpTarget::Releases:return "https://github.com/luganoplanb/bitcoin-roots/releases";
    case HelpTarget::Source:return "https://github.com/luganoplanb/bitcoin-roots";
    }
    return {};
}
bool ConfirmedHelpUrl(HelpTarget target,std::string_view confirmed)
{
    const auto expected=HelpUrl(target);
    return !expected.empty() && confirmed==expected;
}
}
