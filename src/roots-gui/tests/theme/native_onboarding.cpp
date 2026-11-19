// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include "app/connection_observations.h"
#include "app/history_workspace.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
namespace {
namespace shell = ui::shell;

struct Requests final : app::Requests {
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    RefreshService refresh{[this](const RefreshRequest& request, std::stop_token stop) { return FetchRefresh(transport, request, stop); }};
    ConnectionService connection{[this](const ServerCandidate& candidate, std::stop_token stop) { return ProbeCapabilities(transport, candidate, stop); },
        [this](bool offline) { refresh.SetOffline(offline, RefreshMonotonicMs()); }};
    WalletLifecycle wallet{[this](const WalletOperation& operation, std::stop_token stop) { return RunWalletOperation(transport, operation, stop); }};
    app::RecoverySession recovery;
    explicit Requests(std::filesystem::path path) : recovery(std::move(path)) {}
    ConnectionService* Connection() override { return &connection; }
    WalletLifecycle* Wallets() override { return &wallet; }
    RefreshService* Refresh() override { return &refresh; }
    app::RecoverySession* Recovery() override { return &recovery; }
    app::HostObservations Observe() const override { return app::ConnectionObservations(connection, refresh); }
    app::Availability CanSubmit(const app::Request& request) const override {
        return {request.command == app::Command::Introduction || request.command == app::Command::OpenWallet ||
            request.command == app::Command::NewWallet || request.command == app::Command::Server, {}};
    }
    app::Receipt Submit(const app::Request&) override { return {}; }
};

struct Observer final : platform::NativeObserver {
    Requests requests;
    shell::State state;
    shell::Facts facts;
    std::filesystem::path output;
    Display* display{};
    int (*motion)(Display*, int, int, int, unsigned long){};
    int (*button)(Display*, unsigned int, Bool, unsigned long){};
    int (*key)(Display*, unsigned int, Bool, unsigned long){};
    int stage{}, checks{}, frames{};
    bool cleaned{}, checked_duplicate{};
    std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
    explicit Observer(const std::filesystem::path& metadata) : requests(metadata) {}

    void Check(bool valid, const char* contract)
    {
        if (!valid) throw std::runtime_error("stage " + std::to_string(stage) + ": " + contract);
        ++checks;
    }
    void Idle(platform::NativeHost&, const platform::NativeFrame&) override
    {
        Check(std::chrono::steady_clock::now() - started < std::chrono::seconds(35), "native onboarding deadline");
        --checks;
    }
    bool CaptureFramebuffer() const override { return true; }
    bool PublicationReady() const
    {
        const auto packet = state.presentation.exchange.Published();
        return packet && packet->valid;
    }
    void Capture(const platform::NativeFrame& frame, const char* id)
    {
        Check(frame.width == 960 && frame.height == 640 && frame.effective_scale == 1.5F && frame.decorated && !frame.gl_error, "native geometry, scale and renderer");
        Check(state.presentation.Tree().Validate(), "valid production semantic tree");
        std::ofstream ppm(output / (std::string(id) + ".ppm"), std::ios::binary);
        ppm << "P6\n" << frame.framebuffer_width << ' ' << frame.framebuffer_height << "\n255\n";
        for (int y = frame.framebuffer_height - 1; y >= 0; --y) {
            for (int x = 0; x < frame.framebuffer_width; ++x) {
                ppm.write(reinterpret_cast<const char*>(frame.rgba.data() + (size_t(y) * frame.framebuffer_width + x) * 4), 3);
            }
        }
        Check(bool(ppm), "native framebuffer retained");
        std::ofstream semantics(output / (std::string(id) + ".txt"));
        const auto packet = state.presentation.exchange.Published();
        Check(packet && packet->valid, "valid published semantics");
        for (const auto& node : packet->nodes) {
            if (state.onboarding.dialog.open) Check(node.name != "File", "onboarding modal owns published accessibility");
            semantics << node.name << " | " << node.value << " | " << node.description << '\n';
        }
        Check(bool(semantics), "native semantics retained");
        ++frames;
    }
    void Point(platform::NativeHost& host, const ui::components::ItemFacts& item)
    {
        Check(item.visible && !item.disabled, "native target is visible and enabled");
        int x{}, y{};
        glfwGetWindowPos(host.Window(), &x, &y);
        Check(motion(display, -1, x + int((item.rect[0] + item.rect[2]) / 2), y + int((item.rect[1] + item.rect[3]) / 2), 0), "native pointer move");
        Check(button(display, 1, True, 0), "native press");
    }
    void Lift() { Check(button(display, 1, False, 0), "native release"); }
    void Key(KeySym symbol, bool down) { Check(key(display, XKeysymToKeycode(display, symbol), down, 0), "native key event"); }
    void Chord(KeySym symbol, bool down)
    {
        if (down) Key(XK_Control_L, true);
        Key(symbol, down);
        if (!down) Key(XK_Control_L, false);
    }
    bool PointToMenu(platform::NativeHost& host,std::string_view name)
    {
        const auto packet=state.presentation.exchange.Published();
        if(!packet || !packet->valid)return false;
        const auto item=std::ranges::find_if(packet->nodes,[name](const auto& node){
            return node.name==name && node.showing && !node.state.disabled;
        });
        if(item==packet->nodes.end())return false;
        Point(host,{{item->minimum.x,item->minimum.y,item->maximum.x,item->maximum.y},
            0,false,false,false,false,true});
        return true;
    }
    void Frame(platform::NativeHost& host, const platform::NativeFrame& frame) override
    {
        if (frame.rendered_frames < 3) return;
        auto& connection = requests.connection;
        auto& panel = state.onboarding;
        const auto click = [&](const char* id) { Point(host, facts.onboarding.at(id)); };
        switch (stage) {
        case 0:
            Check(!connection.Active() && !connection.Busy() && !requests.wallet.Busy(), "connection-independent first launch");
            Capture(frame, "first-launch");
            click("offline"); ++stage; break;
        case 1: Lift(); ++stage; break;
        case 2:
            if (panel.dialog.open || panel.close_pending || !PublicationReady()) break;
            Check(connection.Offline() && !connection.Active() && state.documents.Documents().empty(), "offline public workspace and no fabricated documents");
            Capture(frame, "offline");
            if(PointToMenu(host,"Tools"))++stage;
            break;
        case 3: Lift(); stage=30; break;
        case 30:
            if(facts.menu_rows.contains(app::Command::Server)
                    && facts.menu_rows.at(app::Command::Server).visible){
                Point(host,facts.menu_rows.at(app::Command::Server));
                stage=31;
            }
            break;
        case 31: Lift(); stage=4; break;
        case 4: {
            if (!facts.server.visible) break;
            const auto packet=state.presentation.exchange.Published();
            if(!packet || !packet->valid)break;
            namespace ax=ui::components::ax;
            const auto item=std::ranges::find_if(packet->nodes,[](const auto& node){
                return node.name=="Offline mode" && node.showing && ax::Supports(node.actions,ax::SemanticAction::Toggle);
            });
            if(item==packet->nodes.end())break;
            Check(state.presentation.exchange.QueueNext(packet->generation,item->id,ax::SemanticAction::Toggle).second
                  ==ui::components::ActionResult::Queued,"native offline toggle through the current server dialog");
            stage=6;
            break;
        }
        case 6:
            if (connection.Offline() || !facts.server.visible || !facts.server.items.contains("test")) break;
            if (facts.server.items.at("test").disabled || !facts.server.items.at("test").visible) {
                Check(connection.Draft()==state.server.Candidate() && ValidServerCandidate(connection.Draft()),
                    "the server editor retains the exact valid fixture target after going online");
                break;
            }
            Point(host, facts.server.items.at("test")); ++stage; break;
        case 7: Lift(); ++stage; break;
        case 8:
            if (!connection.CanApply()) break;
            Check(CanCreateWallet(*connection.Tested()) && CanLoadWallet(*connection.Tested()), "daemon advertises named lifecycle capability");
            Point(host, facts.server.items.at("apply")); ++stage; break;
        case 9: Lift(); ++stage; break;
        case 10:
            if (!connection.Active()) break;
            Point(host, facts.server.items.at("cancel")); stage = 40; break;
        case 40: Lift(); stage = 41; break;
        case 41:
            if (state.server.dialog.open) break;
            Chord(XK_n, true); stage = 11; break;
        case 11: Chord(XK_n, false); ++stage; break;
        case 12:
            if (!facts.onboarding.contains("name")) break;
            Check(facts.onboarding.at("create").disabled, "empty name is rejected");
            Capture(frame, "new-wallet");
            Point(host, facts.onboarding.at("name")); ++stage; break;
        case 13: Lift(); ++stage; break;
        case 14:
            Check(host.Input().Copy("existing-cold"), "seed owned native clipboard");
            Chord(XK_v, true); ++stage; break;
        case 15: Chord(XK_v, false); ++stage; break;
        case 16:
            if (panel.name.draft() != (checked_duplicate ? "native created wallet" : "existing-cold")) break;
            click("create"); ++stage; break;
        case 17: Lift(); ++stage; break;
        case 18:
            if (!checked_duplicate) {
                if (panel.error != "That wallet name already exists on the daemon.") break;
                Check(state.documents.Documents().empty() && !connection.Active()->candidate.wallet, "duplicate creates no wallet and does not select a tab");
                checked_duplicate = true;
                Point(host, facts.onboarding.at("name")); stage = 50; break;
            }
            if (panel.dialog.open || !state.documents.Active() || facts.active_document != state.documents.ActiveId() || !PublicationReady()) break;
            Check(state.documents.Documents().size() == 1 && state.documents.Active()->section == app::Section::Settings
                && state.documents.Active()->kind == app::DocumentKind::Wallet, "creation opens Settings and no transaction");
            Check(state.documents.Active()->binding->DaemonWallet() == "native created wallet", "exact created identity");
            Capture(frame, "created-settings");
            Chord(XK_o, true); ++stage; break;
        case 19: Chord(XK_o, false); ++stage; break;
        case 20:
            if (!facts.onboarding.contains("refresh")) break;
            click("refresh"); ++stage; break;
        case 21: Lift(); ++stage; break;
        case 22:
            if (!panel.listed || !PublicationReady()) break;
            Check(panel.wallets == std::vector<DaemonWalletEntry>{{"native created wallet",true},{"existing-cold",false}}, "exact loaded and unloaded names");
            Check(panel.recent.size() == 1 && panel.recent.front().daemon_wallet == "native created wallet", "recent workspace persisted after verification");
            Capture(frame, "wallet-open");
            click("wallet-1"); ++stage; break;
        case 23: Lift(); ++stage; break;
        case 24:
            if (!facts.onboarding.contains("open")) break;
            click("open"); ++stage; break;
        case 25: Lift(); ++stage; break;
        case 26:
            if (panel.dialog.open || state.documents.Documents().size() != 2 || facts.active_document != state.documents.ActiveId() || !PublicationReady()) break;
            if (!app::HistoryForDocument(*state.documents.Active(), requests.refresh, {})) break;
            Check(state.documents.Active()->binding->DaemonWallet() == "existing-cold", "loaded wallet verified and selected");
            Check(state.documents.Active()->section == app::Section::Transactions, "existing wallet uses its observation workspace");
            Capture(frame, "loaded-wallet");
            ++stage; host.RequestClose(); break;
        case 50: Lift(); stage = 51; break;
        case 51:
            Check(host.Input().Copy("native created wallet"), "replace owned native clipboard");
            Chord(XK_a, true); stage = 52; break;
        case 52: Chord(XK_a, false); stage = 53; break;
        case 53: Chord(XK_v, true); stage = 15; break;
        default: break;
        }
        XSync(display, False);
    }
    void Teardown(const platform::NativeTeardown& result) override
    {
        cleaned = result.renderer_cleared && result.platform_cleared && result.textures_released && result.imgui_cleared && result.context_cleared;
        Check(cleaned, "host teardown");
    }
};
}

int main(int argc, char** argv)
{
    if (argc != 7) return 2;
    Observer observer(argv[6]);
    observer.output = argv[1];
    std::filesystem::create_directories(observer.output);
    void* xtst{};
    int status{};
    try {
        const char* display = std::getenv("DISPLAY");
        const char* owned = std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if (!display || !owned || std::string(display) != owned) throw std::runtime_error("owned display required");
        observer.display = XOpenDisplay(nullptr);
        xtst = dlopen("libXtst.so.6", RTLD_NOW);
        if (!observer.display || !xtst) throw std::runtime_error("native input unavailable");
        observer.motion = reinterpret_cast<decltype(observer.motion)>(dlsym(xtst, "XTestFakeMotionEvent"));
        observer.button = reinterpret_cast<decltype(observer.button)>(dlsym(xtst, "XTestFakeButtonEvent"));
        observer.key = reinterpret_cast<decltype(observer.key)>(dlsym(xtst, "XTestFakeKeyEvent"));
        if (!observer.motion || !observer.button || !observer.key) throw std::runtime_error("native input entry points unavailable");
        ServerCandidate candidate;
        candidate.connection.port = uint16_t(std::stoi(argv[3]));
        candidate.connection.credential.reference = argv[4];
        candidate.expected_genesis = argv[5];
        observer.requests.connection.SetDraft(candidate);
        observer.state.server.initialized = true;
        observer.requests.recovery.Start();
        shell::BeginOnboarding(observer.state, shell::OnboardingPage::Introduction, observer.requests);
        platform::NativeOptions options{960, 640, false, 1.5F, argv[2]};
        options.fixed_delta_seconds = 1.F / 60;
        platform::NativeHost* current{};
        {
            platform::NativeHost host(options, &observer, [&](const imkit::Theme& theme, const ui::BootstrapAppearance& appearance) {
                observer.facts = platform::DrawShellFrame(*current, observer.state, observer.requests, theme, appearance);
            });
            current = &host;
            host.Run();
        }
        observer.Check(observer.stage == 27 && observer.cleaned && !observer.requests.wallet.Busy(), "complete staged journey");
        app::RecoverySession restart(argv[6]);
        observer.Check(restart.Start(), "restart reads same public store");
        const auto recents = restart.RecentWallets();
        observer.Check(recents && recents.value->size() == 2 && recents.value->front().daemon_wallet == "existing-cold"
            && !restart.Pending() && !restart.Enabled(), "recents are independent of automatic document restore");
        std::ofstream(observer.output / "result.json") << "{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":"
            << observer.checks << ",\"frames\":" << observer.frames << ",\"teardown\":true}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        status = 1;
    }
    if (observer.display) XCloseDisplay(observer.display);
    if (xtst) dlclose(xtst);
    return status;
}
