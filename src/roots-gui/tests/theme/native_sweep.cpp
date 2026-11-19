// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/history_workspace.h"
#include "platform/shell_composition.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>

using namespace roots::gui;
namespace a = app;
namespace p = platform;
namespace sh = ui::shell;
namespace c = ui::components;
namespace {

constexpr char WALLET[] = "fixture-sweep";
constexpr char GENESIS[] = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";

struct Requests final : a::Requests {
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    ConnectionService connection{[this](const auto& candidate, std::stop_token stop) {
        return ProbeCapabilities(transport, candidate, stop);
    }};

    ConnectionService* Connection() override { return &connection; }
    a::Receipt Submit(const a::Request&) override { throw std::runtime_error("unexpected sweep host dispatch"); }
};

enum class Phase {
    Menu, SweepRow, ReleaseRow, Inspect, ReleaseEscape, StartConnectionTest, ApplyConnection,
    ReleaseClose, CheckRetired
};

struct Observer final : p::NativeObserver {
    explicit Observer(std::filesystem::path folder) : output(std::move(folder)) {}

    Requests requests;
    sh::State state;
    sh::Facts facts;
    ServerCandidate candidate;
    std::filesystem::path output;
    Display* display{};
    int (*motion)(Display*, int, int, int, unsigned long){};
    int (*button)(Display*, unsigned int, Bool, unsigned long){};
    int (*key)(Display*, unsigned int, Bool, unsigned long){};
    Phase phase{Phase::Menu};
    int checks{}, captures{}, settle_frame{};
    uint64_t document{}, stale_close{};
    bool done{}, cleaned{};
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    void Check(bool ok, const char* reason)
    {
        if (!ok) throw std::runtime_error("sweep phase " + std::to_string(int(phase)) + ": " + reason);
        ++checks;
    }

    bool CaptureFramebuffer() const override { return true; }
    void Idle(p::NativeHost&, const p::NativeFrame&) override
    {
        if (std::chrono::steady_clock::now() - start <= std::chrono::seconds(15)) return;
        std::ofstream(output / "native-timeout.txt") << "phase " << int(phase) << '\n' << state.presentation.Snapshot();
        throw std::runtime_error("native sweep deadline");
    }

    void Key(KeySym symbol, bool down)
    {
        Check(key(display, XKeysymToKeycode(display, symbol), down, 0), "native Escape input");
        XSync(display, False);
    }

    void Click(p::NativeHost& host, const c::ItemFacts& item)
    {
        Check(item.visible && !item.disabled && item.rect[0] >= 0 && item.rect[1] >= 0
            && item.rect[2] <= 960 && item.rect[3] <= 640, "native action inside the viewport");
        int x{}, y{};
        glfwGetWindowPos(host.Window(), &x, &y);
        Check(motion(display, -1, x + int((item.rect[0] + item.rect[2]) / 2),
                     y + int((item.rect[1] + item.rect[3]) / 2), 0), "native pointer motion");
        Check(button(display, 1, True, 0), "native pointer press");
        XSync(display, False);
    }

    void Release()
    {
        Check(button(display, 1, False, 0), "native pointer release");
        XSync(display, False);
    }

    std::shared_ptr<const c::SemanticPacket> Packet()
    {
        const auto packet = state.presentation.exchange.Published();
        return packet && packet->valid && !packet->overflow ? packet : nullptr;
    }

    std::pair<uint64_t, c::ActionResult> Queue(const c::SemanticPacket& packet, std::string_view name,
                                            c::ax::SemanticAction action)
    {
        const auto found = std::ranges::find(packet.nodes, name, &c::SemanticEntry::name);
        if (found == packet.nodes.end()) return {0, c::ActionResult::Unavailable};
        return state.presentation.exchange.QueueNext(packet.generation, found->id, action);
    }

    void Capture(const p::NativeFrame& frame, const c::SemanticPacket& packet, const char* name)
    {
        Check(frame.width == 960 && frame.height == 640 && frame.effective_scale == 1.5F
            && frame.decorated && !frame.gl_error && !frame.rgba.empty(), "actual decorated native framebuffer");
        std::string semantics;
        unsigned actions{};
        for (const auto& node : packet.nodes) {
            Check(node.role != c::ax::SemanticRole::TextField
                && !c::ax::Supports(node.actions, c::ax::SemanticAction::SetValue), "no key editor or semantic input");
            if (unsigned(node.actions)) {
                ++actions;
                Check(node.name == "Close" && node.role == c::ax::SemanticRole::Button, "Close is the only control");
            }
            semantics += node.name + " | " + node.value + " | " + node.description + '\n';
        }
        Check(actions == 1 && semantics.find("Unsigned sweep unavailable") != std::string::npos
            && semantics.find("signs even its preview") != std::string::npos
            && semantics.find("a separate compatible signer") != std::string::npos
            && semantics.find("sends nothing to the node") != std::string::npos, "truthful sweep contract");
        Check(Queue(packet, "Close", c::ax::SemanticAction::SetValue).second == c::ActionResult::Unsupported,
              "semantic SetValue cannot enter key data");
        auto ppm = std::ofstream(output / (std::string(name) + ".ppm"), std::ios::binary);
        ppm << "P6\n" << frame.framebuffer_width << ' ' << frame.framebuffer_height << "\n255\n";
        for (int y = frame.framebuffer_height - 1; y >= 0; --y) {
            for (int x = 0; x < frame.framebuffer_width; ++x) {
                ppm.write(reinterpret_cast<const char*>(frame.rgba.data() + (size_t(y) * frame.framebuffer_width + x) * 4), 3);
            }
        }
        Check(bool(ppm), "native framebuffer retained");
        std::ofstream(output / (std::string(name) + "-semantics.txt")) << semantics;
        ++captures;
    }

    void Frame(p::NativeHost& host, const p::NativeFrame& frame) override
    {
        if (frame.rendered_frames < 4) return;
        switch (phase) {
        case Phase::Menu:
            if (const auto packet = Packet()) {
                Check(Queue(*packet, "Tools", c::ax::SemanticAction::Press).second == c::ActionResult::Queued,
                      "open actual Tools menu");
                phase = Phase::SweepRow;
            }
            break;
        case Phase::SweepRow: {
            const auto row = facts.menu_rows.find(a::Command::Sweep);
            if (row == facts.menu_rows.end() || !row->second.visible) break;
            Click(host, row->second);
            phase = Phase::ReleaseRow;
            break;
        }
        case Phase::ReleaseRow:
            Release();
            settle_frame = frame.rendered_frames + 3;
            phase = Phase::Inspect;
            break;
        case Phase::Inspect:
            if (!facts.sweep_visible || frame.rendered_frames < settle_frame) break;
            if (const auto packet = Packet()) {
                Check(facts.sweep_close.focused && facts.sweep_close.visible, "initial focus reaches the Close control");
                Check(state.invocation && state.invocation->command == a::Command::Sweep && state.invocation->accepted
                    && !state.invocation->dispatched, "menu action is local");
                if (!captures) {
                    Check(!requests.connection.Active(), "first view has no applied backend");
                    Capture(frame, *packet, "sweep-private-key");
                    Key(XK_Escape, true);
                    phase = Phase::ReleaseEscape;
                } else if (captures == 1) {
                    Check(requests.connection.Active() && state.documents.ActiveId() == document, "matching applied wallet context");
                    Capture(frame, *packet, "sweep-private-key-advertised-rpc");
                    Click(host, facts.sweep_close);
                    phase = Phase::ReleaseClose;
                } else {
                    const auto queued = Queue(*packet, "Close", c::ax::SemanticAction::Press);
                    Check(queued.second == c::ActionResult::Queued, "queue a current local Close action");
                    stale_close = queued.first;
                    Check(requests.connection.SetOffline(true), "retire the applied online context");
                    state.privacy.hidden = true;
                    phase = Phase::CheckRetired;
                }
            }
            break;
        case Phase::ReleaseEscape:
            Key(XK_Escape, false);
            phase = Phase::StartConnectionTest;
            break;
        case Phase::StartConnectionTest:
            if (facts.sweep_visible) break;
            Check(!state.sweep.dialog.open, "Escape closes the unavailable flow");
            Check(requests.connection.SetDraft(candidate) && requests.connection.Test(), "explicit fixture-only Server Test");
            phase = Phase::ApplyConnection;
            break;
        case Phase::ApplyConnection:
            if (requests.connection.Busy()) break;
            Check(requests.connection.CanApply() && requests.connection.Apply(), "apply the controlled fixture profile");
            Check(requests.connection.Active()->Supports("sweepprivkeys", "broadcast") == SupportReason::Available,
                  "fixture genuinely advertises the direct-spend RPC");
            if (const auto binding = a::VerifiedWalletBinding(*requests.connection.Active())) {
                document = state.documents.OpenWallet("Sweep test wallet", *binding, true);
            }
            Check(document != 0, "open the exact verified fixture wallet");
            state.documents.Active()->sections[4].note.SetDraft("Retained wallet work");
            phase = Phase::Menu;
            break;
        case Phase::ReleaseClose:
            Release();
            phase = Phase::CheckRetired;
            break;
        case Phase::CheckRetired:
            if (facts.sweep_visible) break;
            Check(!state.sweep.dialog.open && state.documents.ActiveId() == document && state.documents.Documents().size() == 1
                && state.documents.Active()->sections[4].note.draft() == "Retained wallet work", "close preserves wallet and draft");
            if (!stale_close) {
                phase = Phase::Menu;
            } else {
                const auto receipts = state.presentation.exchange.Receipts();
                const auto action = std::ranges::find(receipts, stale_close, &c::SemanticReceipt::sequence);
                Check(action != receipts.end() && action->result != c::ActionResult::Applied,
                      "retired scope rejects the pending action");
                Check(!sh::CommandAvailability(state, a::Command::Sweep, requests).available
                    && !sh::OpenSweepNotice(state, requests), "hidden workspace cannot open the notice");
                done = true;
                host.RequestClose();
            }
            break;
        }
    }

    void Teardown(const p::NativeTeardown& result) override
    {
        cleaned = result.renderer_cleared && result.platform_cleared && result.textures_released
            && result.imgui_cleared && result.context_cleared && result.input_cleared;
        Check(cleaned, "native resources settled");
    }
};
}

int main(int argc, char** argv)
{
    if (argc != 5) return 2;
    Display* display{};
    void* xtst{};
    try {
        const char* name = std::getenv("DISPLAY");
        const char* owned = std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if (!name || !owned || std::string_view(name) != owned) throw std::runtime_error("owned X11 display required");
        display = XOpenDisplay(nullptr);
        xtst = dlopen("libXtst.so.6", RTLD_NOW);
        if (!display || !xtst) throw std::runtime_error("XTest input unavailable");

        Observer observer(argv[1]);
        observer.display = display;
        observer.motion = reinterpret_cast<decltype(observer.motion)>(dlsym(xtst, "XTestFakeMotionEvent"));
        observer.button = reinterpret_cast<decltype(observer.button)>(dlsym(xtst, "XTestFakeButtonEvent"));
        observer.key = reinterpret_cast<decltype(observer.key)>(dlsym(xtst, "XTestFakeKeyEvent"));
        if (!observer.motion || !observer.button || !observer.key) throw std::runtime_error("XTest entry points unavailable");
        std::filesystem::create_directories(observer.output);
        observer.candidate.connection.port = uint16_t(std::stoi(argv[3]));
        observer.candidate.connection.credential.reference = argv[4];
        observer.candidate.wallet = WALLET;
        observer.candidate.expected_genesis = GENESIS;
        p::NativeOptions options{960, 640, false, 1.5F, argv[2]};
        options.fixed_delta_seconds = 1.F / 60;
        p::NativeHost* current{};
        {
            p::NativeHost host(options, &observer, [&](const imkit::Theme& theme, const ui::BootstrapAppearance& appearance) {
                observer.facts = p::DrawShellFrame(*current, observer.state, observer.requests, theme, appearance);
            });
            current = &host;
            host.Run();
        }
        observer.Check(observer.done && observer.cleaned && observer.captures == 2 && !observer.requests.connection.Busy(),
                       "journey and workers settled");
        std::ofstream(observer.output / "result.json")
            << "{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"assertions\":"
            << observer.checks << ",\"captures\":2,\"teardown\":true}\n";
        XCloseDisplay(display);
        dlclose(xtst);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        if (display) XCloseDisplay(display);
        if (xtst) dlclose(xtst);
        return 1;
    }
}
