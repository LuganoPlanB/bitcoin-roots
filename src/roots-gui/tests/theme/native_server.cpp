// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace roots::gui;
namespace {
struct Requests final : app::Requests {
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    ConnectionService service{[this](const ServerCandidate& value, std::stop_token stop) { return ProbeCapabilities(transport, value, stop); }};
    ConnectionService* Connection() override { return &service; }
    app::Availability CanSubmit(const app::Request& request) const override { return {request.command == app::Command::Server, "Fixture service unavailable"}; }
    app::Receipt Submit(const app::Request&) override { return {false, "No unrelated fixture actions"}; }
};
struct Observer final : platform::NativeObserver {
    Requests requests;
    ui::shell::State state;
    ui::shell::Facts facts;
    std::filesystem::path output;
    int stage{}, tick{}, assertions{}, original_port{}, replacement_port{};
    bool captured{}, cleaned{};
    Display* display{};
    int (*motion)(Display*, int, int, int, unsigned long){};
    int (*button)(Display*, unsigned int, Bool, unsigned long){};
    int (*key)(Display*, unsigned int, Bool, unsigned long){};
    std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
    bool CaptureFramebuffer() const override { return true; }
    void Check(bool ok) { if (!ok) throw std::runtime_error("native Server contract " + std::to_string(assertions + 1)); ++assertions; }
    void Idle(platform::NativeHost&, const platform::NativeFrame&) override { if (std::chrono::steady_clock::now() - started > std::chrono::seconds(15)) throw std::runtime_error("native Server deadline at stage " + std::to_string(stage)); }
    void Key(KeySym sym, bool down) { Check(key(display, XKeysymToKeycode(display, sym), down, 0)); }
    void Point(platform::NativeHost& host, std::string_view id) {
        const auto& f = facts.server.items.at(std::string(id)); Check(f.visible);
        int x{}, y{}; glfwGetWindowPos(host.Window(), &x, &y);
        Check(motion(display, -1, x + int((f.rect[0] + f.rect[2]) / 2), y + int((f.rect[1] + f.rect[3]) / 2), 0));
    }
    void Capture(const platform::NativeFrame& frame,const char* name){
        Check(frame.width==960 && frame.height==640 && frame.effective_scale==1.5F && frame.decorated && !frame.gl_error);
        const auto semantic=state.presentation.Snapshot();
        Check(semantic.find("Working profile is active")!=std::string::npos && !frame.rgba.empty());
        std::ofstream ppm(output/(std::string(name)+".ppm"),std::ios::binary);
        ppm<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for(int y=frame.framebuffer_height-1;y>=0;--y)for(int x=0;x<frame.framebuffer_width;++x)
            ppm.write(reinterpret_cast<const char*>(frame.rgba.data()+(size_t(y)*frame.framebuffer_width+x)*4),3);
        Check(bool(ppm));
        std::ofstream(output/(std::string(name)+"-semantics.txt"))<<semantic;
    }
    void Frame(platform::NativeHost& host, const platform::NativeFrame& frame) override {
        if (!facts.server.visible || frame.rendered_frames < 3) return;
        auto& service = requests.service;
        switch (stage) {
        case 0: Check(!service.Active() && !service.CanApply()); Point(host, "test"); Check(button(display, 1, True, 0)); stage = 1; break;
        case 1: Check(button(display, 1, False, 0)); stage = 2; break;
        case 2: if (service.CanApply()) { Check(service.Tested()->authenticated && service.Tested()->identity_verified); Point(host, "apply"); Check(button(display, 1, True, 0)); stage = 3; } break;
        case 3: Check(button(display, 1, False, 0)); stage = 4; break;
        case 4: if (service.Session() == 1 && state.presentation.Snapshot().find("Working profile is active")!=std::string::npos) { Check(service.Active()->candidate.connection.port == original_port); Capture(frame,"server-applied"); Point(host, "port"); Check(button(display, 1, True, 0)); stage = 5; } break;
        case 5: Check(button(display, 1, False, 0)); Check(host.Input().Copy(std::to_string(replacement_port))); Key(XK_Control_L, true); Key(XK_a, true); stage = 6; break;
        case 6: Key(XK_a, false); Key(XK_Control_L, false); stage = 7; break;
        case 7: Key(XK_Control_L, true); Key(XK_v, true); stage = 8; break;
        case 8: Key(XK_v, false); Key(XK_Control_L, false); stage = 9; break;
        case 9: if (service.Draft().connection.port == replacement_port) { Check(!service.CanApply()); Point(host, "test"); Check(button(display, 1, True, 0)); stage = 10; } break;
        case 10: Check(button(display, 1, False, 0)); stage = 11; break;
        case 11:
            if (!service.Busy() && service.Tested()) {
                Check(service.Tested()->error == rpc::Error::Authentication && !service.CanApply());
                Check(service.Active()->candidate.connection.port == original_port && service.Session() == 1);
                // Scroll only the owned form body so the failed replacement and
                // retained working profile are visible in the representative frame.
                int x{}, y{}; glfwGetWindowPos(host.Window(), &x, &y); Check(motion(display, -1, x + 600, y + 330, 0));
                for (int i = 0; i < 25; ++i) Check(button(display, 5, True, 0) && button(display, 5, False, 0));
                tick = frame.rendered_frames; stage = 12;
            } break;
        case 12:
            if (frame.rendered_frames < tick + 5) break;
            Check(state.presentation.Tree().Validate() && !state.presentation.Tree().nodes.empty());
            {
                const auto packet = state.presentation.exchange.Published(); Check(packet && packet->valid && !packet->nodes.empty());
                for (const auto& entry : packet->nodes) Check(entry.name != "Transactions" && entry.name != "File" && entry.name != "Open public document");
            }
            for (const auto* id : {"test", "apply", "cancel"}) { const auto& item = facts.server.items.at(id); Check(item.visible && item.rect[0] >= 0 && item.rect[3] <= frame.height); }
            Capture(frame,"server-test-connection");
            captured=true;host.RequestClose();stage=13;break;
        }
        XSync(display, False);
    }
    void Teardown(const platform::NativeTeardown& value) override { cleaned = value.renderer_cleared && value.platform_cleared && value.textures_released && value.imgui_cleared && value.context_cleared; Check(cleaned); }
};
}
int main(int argc, char** argv)
{
    if (argc != 7) return 2;
    Observer observer; observer.output = argv[1]; std::filesystem::create_directories(observer.output);
    void* xtst{};
    try {
        const char* display = std::getenv("DISPLAY"), *owned = std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if (!display || !owned || std::string(display) != owned) throw std::runtime_error("owned native display required");
        observer.display = XOpenDisplay(nullptr); xtst = dlopen("libXtst.so.6", RTLD_NOW);
        if (!observer.display || !xtst) throw std::runtime_error("native input unavailable");
        observer.motion = reinterpret_cast<decltype(observer.motion)>(dlsym(xtst, "XTestFakeMotionEvent"));
        observer.button = reinterpret_cast<decltype(observer.button)>(dlsym(xtst, "XTestFakeButtonEvent"));
        observer.key = reinterpret_cast<decltype(observer.key)>(dlsym(xtst, "XTestFakeKeyEvent"));
        if (!observer.motion || !observer.button || !observer.key) throw std::runtime_error("native input entry points unavailable");
        ServerCandidate candidate; candidate.connection.port = uint16_t(std::stoi(argv[3])); candidate.connection.credential.reference = argv[5]; candidate.expected_genesis = argv[6]; candidate.wallet = "fixture-public";
        observer.original_port = candidate.connection.port; observer.replacement_port = std::stoi(argv[4]);
        observer.requests.service.SetDraft(candidate); observer.state.server.initialized = true; ui::OpenServer(observer.state.server, observer.requests.service);
        platform::NativeOptions options{960, 640, false, 1.5F, argv[2]}; options.fixed_delta_seconds = 1.F / 60;
        platform::NativeHost* current{};
        { platform::NativeHost host(options, &observer, [&](const imkit::Theme& theme, const ui::BootstrapAppearance& appearance) {
            observer.requests.service.Poll(); observer.facts = platform::DrawShellFrame(*current, observer.state, observer.requests, theme, appearance);
        }); current = &host; host.Run(); }
        observer.Check(observer.captured && observer.cleaned && !observer.requests.service.Busy());
        std::ofstream(observer.output / "result.json") << "{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"assertions\":" << observer.assertions << ",\"workingProfileRetained\":true,\"session\":1,\"teardown\":true}\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; if (observer.display) XCloseDisplay(observer.display); if (xtst) dlclose(xtst); return 1; }
    if (observer.display) XCloseDisplay(observer.display); if (xtst) dlclose(xtst); return 0;
}
