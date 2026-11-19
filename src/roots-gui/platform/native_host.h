// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "ui/bootstrap.h"
#include "platform/linux/accessibility.h"
#include "platform/input.h"
#include "platform/desktop.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
struct GLFWwindow;
namespace roots::gui::platform {
struct NativeOptions {
    int width{1200}, height{820};
    bool dark{false};
    float font_scale{1};
    std::filesystem::path font_directory;
    std::filesystem::path appearance_file; // Empty for isolated callers/captures.
    float fixed_delta_seconds{}; // Optional deterministic presentation clock; 0 uses native time.
    // Optional host adapter; empty uses the read-only desktop integration.
    // Supplying a reader does not manufacture UI flags or change OS settings.
    std::function<std::optional<bool>()> system_appearance_reader;
    ui::components::SemanticExchange* accessibility{}; // Owner-lifetime association, never a borrowed tree.
    std::function<void()> on_draw_suspended; // Retire side effects when no window content is visible.
    std::function<bool()> before_close; // Optional owner-thread acknowledgement of pending local edits.
    std::optional<DesktopPorts> desktop_ports;
};
struct NativeFrame {
    int iteration{}, rendered_frames{}, width{}, height{}, framebuffer_width{}, framebuffer_height{};
    float dpi_x{1}, dpi_y{1}, effective_scale{1};
    bool iconified{}, decorated{}, should_close{}, space_down{}, mouse_down{}, capture_keyboard{}, capture_mouse{};
    int frame_left{}, frame_top{}, frame_right{}, frame_bottom{};
    int frame_extents_queries{};
    int texture_creates{}, texture_updates{}, live_textures{};
    unsigned int font_texture{};
    unsigned int gl_error{};
    ui::BootstrapFacts draw;
    std::span<const unsigned char> rgba; // Borrowed bottom-up framebuffer; callback lifetime only.
};
struct NativeTeardown {
    bool renderer_cleared{}, platform_cleared{}, textures_released{}, imgui_cleared{}, context_cleared{};
    bool accessibility_cleared{true};
    bool input_cleared{true};
};
struct DroppedPaths { std::vector<std::string> paths;double x{},y{}; };
class NativeHost;
class NativeObserver {
public:
    virtual ~NativeObserver() = default;
    virtual bool CaptureFramebuffer() const { return false; }
    virtual void Idle(NativeHost&, const NativeFrame&) {}
    virtual void Frame(NativeHost&, const NativeFrame&) {}
    virtual void Teardown(const NativeTeardown&) {}
};
class NativeHost {
public:
    // Host-thread composition inside the actual backend frame. Empty selects
    // the production bootstrap; components never own contexts or backends.
    using Draw = std::function<void(const imkit::Theme&, const ui::BootstrapAppearance&)>;
    explicit NativeHost(NativeOptions options, NativeObserver* observer = nullptr, Draw draw = {});
    ~NativeHost();
    NativeHost(const NativeHost&) = delete;
    NativeHost& operator=(const NativeHost&) = delete;
    int Run();
    GLFWwindow* Window(); // Borrowed host-thread-only handle, never transferred.
    void RequestClose();
    bool Minimize();
    DesktopRuntime* Desktop();
    std::vector<DroppedPaths> TakeDroppedPaths(); // Owned copy, drained only on host thread.
    void SetFontScale(float scale);
    bool SetAppearance(int choice); // Light=0, Dark=1, System=2; acknowledges actual saved preference.
    ui::AppearanceStatus AppearanceState() const; // Current owner-thread snapshot, not mutable host flags.
    AccessibilityStatus AccessibilityState() const;
    NativeInput& Input();
    InputStatus InputState() const;
    void RecreateRendererResources(); // Must be outside frame submission on the owning thread.
    const std::string& Renderer() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
std::filesystem::path ExecutableFontDirectory();
}
