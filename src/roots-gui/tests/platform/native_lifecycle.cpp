// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/native_host.h"
#include <imgui.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace roots::gui;
namespace {
int checks{}, failures{}, cases_started{}, cases_completed{};
void Check(bool ok, const char* text) { ++checks; if (!ok) { ++failures; std::fprintf(stderr, "FAIL %s\n", text); } }
Display* display{};
bool expected_bad_window{};
int bad_windows{};
using FakeKey = int (*)(Display*, unsigned int, Bool, unsigned long);
using FakeButton = int (*)(Display*, unsigned int, Bool, unsigned long);
using FakeMotion = int (*)(Display*, int, int, int, unsigned long);
FakeKey fake_key{}; FakeButton fake_button{}; FakeMotion fake_motion{};
void Key(KeySym key, bool down) { Check(fake_key(display, XKeysymToKeycode(display, key), down, 0), "XTest key dispatch"); XFlush(display); }
void Ppm(const std::filesystem::path& path, const platform::NativeFrame& frame)
{
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << frame.framebuffer_width << ' ' << frame.framebuffer_height << "\n255\n";
    for (int y = frame.framebuffer_height - 1; y >= 0; --y)
        for (int x = 0; x < frame.framebuffer_width; ++x)
            out.write(reinterpret_cast<const char*>(frame.rgba.data() + (static_cast<std::size_t>(y) * frame.framebuffer_width + x) * 4), 3);
    if (!out) throw std::runtime_error("framebuffer capture write failed");
}
void FramePpm(const std::filesystem::path& path, GLFWwindow* window, const platform::NativeFrame& frame)
{
    int x{}, y{}; glfwGetWindowPos(window, &x, &y);
    const int width = frame.width + frame.frame_left + frame.frame_right;
    const int height = frame.height + frame.frame_top + frame.frame_bottom;
    XImage* image = XGetImage(display, DefaultRootWindow(display), x - frame.frame_left, y - frame.frame_top,
                             width, height, AllPlanes, ZPixmap);
    if (!image) throw std::runtime_error("native frame capture failed");
    std::ofstream out(path, std::ios::binary); out << "P6\n" << width << ' ' << height << "\n255\n";
    const auto channel = [](unsigned long value, unsigned long mask) {
        if (!mask) return static_cast<unsigned char>(0);
        while ((mask & 1) == 0) { mask >>= 1; value >>= 1; }
        return static_cast<unsigned char>((value & mask) * 255 / mask);
    };
    for (int row = 0; row < height; ++row) for (int col = 0; col < width; ++col) {
        const auto pixel = XGetPixel(image, col, row);
        const unsigned char rgb[]{channel(pixel, image->red_mask), channel(pixel, image->green_mask), channel(pixel, image->blue_mask)};
        out.write(reinterpret_cast<const char*>(rgb), 3);
    }
    XDestroyImage(image);
    if (!out) throw std::runtime_error("native frame capture write failed");
}
struct Observer final : platform::NativeObserver {
    std::filesystem::path output;
    std::string name;
    int mode{}, phase{}, minimized_idles{}, min_frame{}, capture_frame{8}, logged_phase{-1}, mapped_frame_queries{};
    bool captured{}, teardown{}, iconified{}, resized{}, scaled{}, recreated{}, closed{}, key_seen{}, mouse_seen{}, capture_seen{};
    int creates{}, updates{};
    float expected_font{14}, padding{};
    unsigned long xid{};
    unsigned int old_texture{};
    platform::NativeFrame last;
    std::chrono::steady_clock::time_point start{std::chrono::steady_clock::now()};
    Observer(std::filesystem::path folder, std::string id, int input_mode, float font) : output(std::move(folder)), name(std::move(id)), mode(input_mode), expected_font(font) {}
    bool CaptureFramebuffer() const override { return true; }
    void Idle(platform::NativeHost& host, const platform::NativeFrame& f) override
    {
        if (mode && logged_phase != phase) {
            std::fprintf(stderr,"native-phase scenario=%s phase=%d frame=%d iconified=%d size=%dx%d\n",name.c_str(),phase,f.rendered_frames,int(f.iconified),f.width,f.height);
            logged_phase=phase;
        }
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(10)) throw std::runtime_error("native scenario timed out: " + name);
        if (!xid) xid = glfwGetX11Window(host.Window());
        closed |= f.should_close;
        if (mode == 1 && phase == 2 && f.iconified) {
            if (!iconified) { iconified = true; min_frame = f.rendered_frames; }
            Check(f.rendered_frames == min_frame, "minimized window does not render");
            Check(f.frame_top>0 && f.frame_extents_queries==mapped_frame_queries,"minimized frames reuse mapped decoration extents without an X11 query");
            if (++minimized_idles == 3) { std::fputs("native-restore begin\n",stderr);glfwRestoreWindow(host.Window());std::fputs("native-restore end\n",stderr); phase = 3; }
        }
    }
    void Frame(platform::NativeHost& host, const platform::NativeFrame& f) override
    {
        if (f.rendered_frames == 1 && name == "bootstrap-1200x820-light-100")
            std::printf("production renderer=%s; OpenGL=%s; GLFW=%s\n", host.Renderer().c_str(), reinterpret_cast<const char*>(glGetString(GL_VERSION)), glfwGetVersionString());
        last = f; last.rgba = {};
        creates += f.texture_creates; updates += f.texture_updates;
        key_seen |= f.space_down; mouse_seen |= f.mouse_down;
        capture_seen |= mode == 2 ? f.capture_mouse : f.capture_keyboard;
        closed |= f.draw.close_requested;
        if (f.rendered_frames == capture_frame) {
            Check(f.decorated && f.frame_top > 0, "real native WM frame decorations");
            Check(f.draw.close_visible, "primary action visible");
            Check(f.draw.close_x0 >= 0 && f.draw.close_y0 >= 0 && f.draw.close_x1 <= f.width && f.draw.close_y1 <= f.height, "primary action within client bounds");
            // Pinned ImGui::UpdateCurrentSize rounds final sizes with IM_ROUND.
            Check(std::fabs(f.draw.font_size - std::round(expected_font * f.dpi_x)) < .05F, "font and native DPI applied once with pinned rounding");
            Check((ImGui::GetIO().ConfigFlags & (ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable)) == 0, "no free docking or detached OS viewports");
            Check(f.font_texture && glIsTexture(f.font_texture), "live uploaded font texture");
            Check(!f.gl_error && !f.rgba.empty(), "real framebuffer readback");
            Ppm(output/(name+".ppm"), f);
            if (mode == 0 && expected_font == 14) FramePpm(output/(name+"-frame.ppm"), host.Window(), f);
            captured = true;
            bool rejected{false};
            std::thread wrong_thread([&] { try { host.Window(); } catch (const std::logic_error&) { rejected = true; } });
            wrong_thread.join(); Check(rejected, "foreign thread cannot access native handle");
            if (mode == 0) { host.RequestClose(); return; }
        }
        if (mode == 1) {
            if (phase == 0 && f.rendered_frames >= 9) { glfwSetWindowSize(host.Window(), int(std::ceil(960 * f.dpi_x)), int(std::ceil(640 * f.dpi_y))); phase = 1; }
            else if (phase == 1 && f.width == int(std::ceil(960 * f.dpi_x)) && f.height == int(std::ceil(640 * f.dpi_y))) {
                resized = true;mapped_frame_queries=f.frame_extents_queries;glfwIconifyWindow(host.Window());
                phase = 2;
            }
            else if (phase == 3 && !f.iconified) { host.SetFontScale(1.5F); phase = 4; }
            else if (phase == 4 && std::fabs(f.draw.font_size - std::round(21 * f.dpi_x)) < .05F) {
                scaled = true; padding = f.draw.frame_padding_y; host.SetFontScale(1.5F); phase = 5;
            } else if (phase == 5) {
                Check(f.draw.frame_padding_y == padding, "repeated scale application does not accumulate");
                old_texture = f.font_texture; host.RecreateRendererResources();
                Check(!glIsTexture(old_texture), "old font texture destroyed before recreation"); phase = 6;
            } else if (phase == 6 && f.font_texture && glIsTexture(f.font_texture)) {
                recreated = true; glfwFocusWindow(host.Window()); Key(XK_Tab, true); phase = 7;
            } else if (phase == 7) { Key(XK_Tab, false); phase = 8; }
            else if (phase == 8 && f.draw.close_focused) { Key(XK_space, true); phase = 9; }
            else if (phase == 9) { Key(XK_space, false); phase = 10; }
        } else if (mode == 2 && f.rendered_frames >= 9) {
            if (phase == 0) {
                int x{}, y{}; glfwGetWindowPos(host.Window(), &x, &y); glfwFocusWindow(host.Window());
                Check(fake_motion(display, -1, x + int((f.draw.close_x0 + f.draw.close_x1)/2), y + int((f.draw.close_y0 + f.draw.close_y1)/2), 0), "XTest pointer dispatch"); XFlush(display); phase = 1;
            } else if (phase == 1) { Check(fake_button(display, 1, True, 0), "XTest button press"); XFlush(display); phase = 2; }
            else if (phase == 2) { Check(fake_button(display, 1, False, 0), "XTest button release"); XFlush(display); phase = 3; }
        } else if (mode == 3 && f.rendered_frames >= 9 && phase == 0) {
            glfwFocusWindow(host.Window()); Key(XK_Alt_L, true); Key(XK_F4, true); Key(XK_F4, false); Key(XK_Alt_L, false); phase = 1;
        }
    }
    void Teardown(const platform::NativeTeardown& s) override
    {
        Check(s.renderer_cleared && s.platform_cleared, "stock backend shutdown completed");
        Check(s.textures_released, "all observed font textures released");
        Check(s.imgui_cleared && s.context_cleared, "host contexts released"); teardown = true;
    }
    void Finish()
    {
        Check(captured && teardown, "scenario captured and owner teardown observed");
        expected_bad_window = true; bad_windows = 0;
        XWindowAttributes attributes{}; const auto exists = XGetWindowAttributes(display, xid, &attributes); XSync(display, False);
        expected_bad_window = false;
        Check(!exists && bad_windows, "owned native window no longer exists");
        if (mode == 1) {
            Check(resized && iconified && scaled && recreated, "resize/minimize/restore/font/resource lifecycle");
            Check(creates >= 2 && updates > 0, "actual dynamic texture create/update/recreate requests");
            Check(closed && key_seen && capture_seen, "native keyboard reaches ImGui and closes application");
        }
        if (mode == 2) Check(closed && mouse_seen && capture_seen, "native pointer reaches ImGui and closes application");
        if (mode == 3) Check(closed, "window-manager close reaches production host");
        std::ofstream json(output/(name+".json"));
        json << std::boolalpha << "{\"name\":\"" << name << "\",\"width\":" << last.width << ",\"height\":" << last.height
             << ",\"dpi\":" << last.dpi_x << ",\"font_size\":" << last.draw.font_size << ",\"native_frame_top\":" << last.frame_top
             << ",\"close\":[" << last.draw.close_x0 << ',' << last.draw.close_y0 << ',' << last.draw.close_x1 << ',' << last.draw.close_y1
             << "],\"texture_creates\":" << creates << ",\"texture_updates\":" << updates << ",\"teardown\":" << teardown << "}\n";
    }
};
}
int main(int argc, char** argv)
{
    if (argc != 3 || !std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY") || !std::getenv("DISPLAY") ||
        std::string(std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY")) != std::getenv("DISPLAY")) return 2;
    try {
        const std::filesystem::path output(argv[1]); std::filesystem::create_directories(output);
        void* xtst = dlopen("libXtst.so.6", RTLD_NOW);
        if (!xtst) throw std::runtime_error("native XTest unavailable");
        fake_key = reinterpret_cast<FakeKey>(dlsym(xtst, "XTestFakeKeyEvent"));
        fake_button = reinterpret_cast<FakeButton>(dlsym(xtst, "XTestFakeButtonEvent"));
        fake_motion = reinterpret_cast<FakeMotion>(dlsym(xtst, "XTestFakeMotionEvent"));
        if (!fake_key || !fake_button || !fake_motion) throw std::runtime_error("native XTest API incomplete");
        display = XOpenDisplay(nullptr); if (!display) throw std::runtime_error("owned X display unavailable");
        XSetErrorHandler([](Display*, XErrorEvent* error) { if (expected_bad_window && error->error_code == BadWindow) ++bad_windows; else Check(false, "unexpected X11 protocol error"); return 0; });
        for (int width : {1200, 960}) for (bool dark : {false, true}) for (float scale : {1.F, 1.5F, 2.F}) {
            const int height = width == 1200 ? 820 : 640;
            const auto name = "bootstrap-" + std::to_string(width) + "x" + std::to_string(height) + (dark ? "-dark-" : "-light-") + std::to_string(int(scale * 100));
            Observer observer(output, name, 0, 14 * scale);
            ++cases_started;
            { platform::NativeHost host({width, height, dark, scale, argv[2]}, &observer); Check(host.Run() == 0, "production native run returns success"); }
            observer.Finish(); ++cases_completed;
        }
        for (int mode : {1, 2, 3}) {
            Observer observer(output, "lifecycle-" + std::to_string(mode), mode, 14);
            ++cases_started;
            { platform::NativeHost host({1200, 820, false, 1, argv[2]}, &observer); Check(host.Run() == 0, "repeated production launch/close"); }
            observer.Finish(); ++cases_completed;
        }
        ++cases_started;
        Observer missing(output, "missing-font-assets", 0, 14); bool font_rejected{false};
        try { platform::NativeHost host({1200, 820, false, 1, output/"nonexistent-font-directory"}, &missing); }
        catch (const std::runtime_error&) { font_rejected = true; }
        Check(font_rejected && missing.teardown && ImGui::GetCurrentContext() == nullptr, "partial font initialization failure cleans owned resources"); ++cases_completed;
        ++cases_started;
        struct ThrowObserver final : platform::NativeObserver {
            bool cleaned{};
            void Frame(platform::NativeHost&, const platform::NativeFrame&) override { throw std::runtime_error("intentional observer interruption"); }
            void Teardown(const platform::NativeTeardown& s) override { cleaned = s.renderer_cleared && s.platform_cleared && s.textures_released && s.imgui_cleared && s.context_cleared; }
        } interrupted;
        bool interruption{false};
        try { platform::NativeHost host({1200, 820, false, 1, argv[2]}, &interrupted); host.Run(); }
        catch (const std::runtime_error& error) { interruption = std::string(error.what()) == "intentional observer interruption"; }
        Check(interruption && interrupted.cleaned, "exception after rendered frame cleans all owned resources"); ++cases_completed;
        ++cases_started;
        { platform::NativeHost first({1200, 820, false, 1, argv[2]}); bool duplicate_rejected{false};
          try { platform::NativeHost second({1200, 820, false, 1, argv[2]}); }
          catch (const std::logic_error&) { duplicate_rejected = true; }
          Check(duplicate_rejected, "second simultaneous host rejected"); first.RequestClose(); Check(first.Run() == 0, "original host remains valid after duplicate rejection"); }
        ++cases_completed;
        XCloseDisplay(display); dlclose(xtst);
    } catch (const std::exception& error) { Check(false, error.what()); }
    std::printf("selected_cases=18 started_cases=%d completed_cases=%d unstarted_cases=%d\n", cases_started, cases_completed, 18-cases_started);
    std::printf("executed=%d passed=%d failed=%d skipped=%d\n", checks, checks - failures, failures, 18-cases_started);
    return failures ? 1 : 0;
}
