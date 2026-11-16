// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/foundation.h"
#include "platform/native_host.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#if !defined(__APPLE__)
#include <imgui_impl_opengl3.h>
#endif
#include <cstdio>
namespace roots::gui::platform {
std::string BuildInfo()
{
    auto* volatile platform_entry = &ImGui_ImplGlfw_InitForOther;
#if defined(__APPLE__)
    const bool renderer_linked = false; // Metal native host remains follow-on work.
#else
    auto* volatile renderer_entry = &ImGui_ImplOpenGL3_Init;
    const bool renderer_linked = renderer_entry != nullptr;
#endif
    return "\"glfw\":\"" + std::string(glfwGetVersionString()) + "\",\"backend_symbols_linked\":" +
           (platform_entry && renderer_linked ? "true" : "false") + ",\"native_initialized\":false";
}
int Run()
{
    NativeHost host({});
    return host.Run();
}
}
