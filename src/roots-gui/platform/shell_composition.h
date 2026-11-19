// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "platform/native_host.h"
#include "platform/restart.h"
#include "ui/shell/workspace.h"
namespace roots::gui::platform {
// Shared by application Run and native regression hosts. Draws the current
// host snapshot, then acknowledges only an explicit shell appearance request.
ui::shell::Facts DrawShellFrame(NativeHost&,ui::shell::State&,app::Requests&,
                              const imkit::Theme&,const ui::BootstrapAppearance&);
app::Availability GuiRestartAvailability(const ui::shell::State&,app::Requests&);
std::optional<RestartContext> PrepareGuiRestart(ui::shell::State&,app::Requests&,std::string& error);
bool AdoptGuiRestart(ui::shell::State&,app::Requests&,const RestartContext&);
}
