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
namespace shell = ui::shell;
struct Requests final : app::Requests {
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    ConnectionService connection{[this](const ServerCandidate& profile, std::stop_token stop) {return ProbeCapabilities(transport, profile, stop);}};
    WalletLifecycle wallets{[this](const WalletOperation& operation, std::stop_token stop) {return RunWalletOperation(transport, operation, stop);}};
    ConnectionService* Connection() override {return &connection;}
    WalletLifecycle* Wallets() override {return &wallets;}
    app::Availability CanSubmit(const app::Request& request) const override {return {request.command == app::Command::ExportWallet, {}};}
    app::Receipt Submit(const app::Request&) override {return {};}
};

struct Observer final : platform::NativeObserver {
    Requests requests;
    shell::State state;
    shell::Facts facts;
    std::filesystem::path output;
    std::string xpub, path;
    protocol::PublicWalletPreview imported;
    Display* display{};
    int (*motion)(Display*, int, int, int, unsigned long){};
    int (*button)(Display*, unsigned int, Bool, unsigned long){};
    int (*key)(Display*, unsigned int, Bool, unsigned long){};
    std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
    bool lift{}, paste_up{}, settled{};
    int stage{}, checks{}, frames{};

    void Check(bool valid, const char* label)
    {
        if (!valid) throw std::runtime_error("stage " + std::to_string(stage) + ": " + label);
        ++checks;
    }
    bool Publication() const
    {
        const auto packet = state.presentation.exchange.Published();
        return packet && packet->valid;
    }
    bool CaptureFramebuffer() const override {return true;}
    void Idle(platform::NativeHost&, const platform::NativeFrame&) override
    {
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(38)) {
            std::ofstream state_file(output / "deadline-state.txt");
            const auto& panel = state.onboarding;
            state_file << "stage=" << stage << " page=" << int(panel.page) << " method=" << int(panel.import_method)
                << " open=" << panel.dialog.open << " close_pending=" << panel.close_pending << " current_packet=" << Publication() << '\n';
            for (const auto& [id, item] : facts.onboarding) {
                state_file << id << " visible=" << item.visible << " disabled=" << item.disabled << '\n';
            }
            throw std::runtime_error("native import deadline at stage " + std::to_string(stage) + " (wallet error: " + state.onboarding.error + ')');
        }
    }
    void Capture(const platform::NativeFrame& frame, const char* tag)
    {
        Check(frame.width == 960 && frame.height == 640 && frame.effective_scale == 1.5F && frame.decorated && !frame.gl_error, "native compact geometry");
        Check(Publication(), "valid current semantic packet");
        std::ofstream ppm(output / (std::string(tag) + ".ppm"), std::ios::binary);
        ppm << "P6\n" << frame.framebuffer_width << ' ' << frame.framebuffer_height << "\n255\n";
        for (int y = frame.framebuffer_height - 1; y >= 0; --y) {
            for (int x = 0; x < frame.framebuffer_width; ++x) {
                ppm.write(reinterpret_cast<const char*>(frame.rgba.data() + (size_t(y) * frame.framebuffer_width + x) * 4), 3);
            }
        }
        Check(bool(ppm), "native framebuffer retained");
        std::ofstream semantics(output / (std::string(tag) + ".txt"));
        for (const auto& node : state.presentation.exchange.Published()->nodes) {
            if (state.onboarding.dialog.open) Check(node.name != "File", "modal owns published semantics");
            semantics << node.name << " | " << node.value << " | " << node.description << '\n';
        }
        Check(bool(semantics), "native semantic packet retained");
        ++frames;
    }
    void Click(platform::NativeHost& host, float x, float y)
    {
        int wx{}, wy{};
        glfwGetWindowPos(host.Window(), &wx, &wy);
        Check(motion(display, -1, wx + int(x), wy + int(y), 0) && button(display, 1, True, 0), "owned pointer activation");
        lift = true;
        XSync(display, False);
    }
    void Click(platform::NativeHost& host, const ui::components::ItemFacts& item)
    {
        Check(item.visible && !item.disabled, "visible enabled control");
        Click(host, (item.rect[0] + item.rect[2])/2, (item.rect[1] + item.rect[3])/2);
    }
    void File(platform::NativeHost& host)
    {
        const auto& menu = facts.layout.menus;
        Click(host, menu.x + 27, menu.y + menu.h/2);
    }
    void Key(KeySym symbol, bool down)
    {
        Check(key(display, XKeysymToKeycode(display, symbol), down, 0), "owned native key event");
    }
    void Paste(platform::NativeHost& host, std::string_view text)
    {
        Check(host.Input().Copy(text), "owned clipboard");
        Key(XK_Control_L, true);
        Key(XK_v, true);
        paste_up = true;
        XSync(display, False);
    }
    void Frame(platform::NativeHost& host, const platform::NativeFrame& frame) override
    {
        if (frame.rendered_frames < 3) return;
        if (lift) {
            Check(button(display, 1, False, 0), "owned pointer release");
            lift = false;
            XSync(display, False);
            return;
        }
        if (paste_up) {
            Key(XK_v, false);
            Key(XK_Control_L, false);
            paste_up = false;
            XSync(display, False);
            return;
        }
        const auto click = [&](const char* id) {Click(host, facts.onboarding.at(id));};
        const auto& panel = state.onboarding;
        switch (stage) {
        case 0: Click(host, facts.document_actions.at("open-import")); break;
        case 1:
            if (!facts.onboarding.contains("method-xpub") || !Publication()) return;
            Capture(frame, "import-methods");
            click("method-xpub");
            break;
        case 2:
            if (!facts.onboarding.contains("name")) return;
            click("name");
            break;
        case 3: Paste(host, "native-watch"); break;
        case 4:
            if (panel.name.draft() != "native-watch") return;
            Check(host.Input().Copy(xpub), "public extended key on fixture clipboard");
            click("paste-preview");
            break;
        case 5:
            if (!panel.preview || !Publication()) return;
            Check(panel.preview->descriptors.size() == 2 && panel.preview->chain == "regtest", "public receive and change preview on verified chain");
            Check(!panel.preview->descriptors[0].internal && panel.preview->descriptors[1].internal, "explicit branch preview");
            imported = *panel.preview;
            Capture(frame, "import-preview");
            click("import-wallet");
            break;
        case 6:
            if (panel.dialog.open || panel.close_pending || !state.documents.Active() || !Publication()) return;
            Check(state.documents.Active()->section == app::Section::Settings
                && state.documents.Active()->binding->DaemonWallet() == "native-watch", "verified watch-only tab");
            Check(requests.connection.Active()->private_keys == false, "daemon disabled private keys");
            File(host);
            break;
        case 7:
            if (!facts.menu_rows.contains(app::Command::ExportWallet)) return;
            Click(host, facts.menu_rows.at(app::Command::ExportWallet));
            break;
        case 8:
            if (!facts.onboarding.contains("daemon-path")) return;
            click("daemon-path");
            break;
        case 9: Paste(host, path); break;
        case 10:
            if (panel.daemon_path.draft() != path) return;
            click("review");
            break;
        case 11:
            if (!panel.path_reviewed || !Publication()) return;
            Capture(frame, "backup-scope");
            click("transfer");
            break;
        case 12:
            if (!panel.error.starts_with("Backup acknowledged")) return;
            click("cancel");
            break;
        case 13:
            if (panel.dialog.open || panel.close_pending) return;
            File(host);
            break;
        case 14:
            if (!facts.menu_rows.contains(app::Command::ImportWallet)) return;
            Click(host, facts.menu_rows.at(app::Command::ImportWallet));
            break;
        case 15:
            if (!facts.onboarding.contains("method-restore") || !Publication()) return;
            click("method-restore");
            break;
        case 16:
            if (!facts.onboarding.contains("name")) return;
            click("name");
            break;
        case 17: Paste(host, "native-restored"); break;
        case 18:
            if (panel.name.draft() != "native-restored") return;
            click("daemon-path");
            break;
        case 19: Paste(host, path); break;
        case 20:
            if (panel.daemon_path.draft() != path) return;
            click("review");
            break;
        case 21:
            if (!panel.path_reviewed || !Publication()) return;
            Capture(frame, "restore-preview");
            click("transfer");
            break;
        case 22:
            if (panel.dialog.open || panel.close_pending || state.documents.Documents().size() != 2 || !Publication()) return;
            Check(state.documents.Active()->binding->DaemonWallet() == "native-restored" && requests.connection.Active()->private_keys == false, "restored wallet exact identity and watch-only custody");
            Capture(frame, "restored-wallet");
            host.RequestClose();
            break;
        default: return;
        }
        ++stage;
    }
    void Teardown(const platform::NativeTeardown& result) override
    {
        settled = result.renderer_cleared && result.platform_cleared && result.textures_released && result.imgui_cleared && result.context_cleared;
        Check(settled, "native resources settled");
    }
};
}

int main(int argc, char** argv)
{
    if (argc != 8) return 2;
    Observer observer;
    observer.output = argv[1];
    observer.xpub = argv[6];
    observer.path = argv[7];
    std::filesystem::create_directories(observer.output);
    void* xtst{};
    int status = 0;
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
        if (!observer.motion || !observer.button || !observer.key) throw std::runtime_error("native input symbols required");
        ServerCandidate profile;
        profile.connection.port = uint16_t(std::stoi(argv[3]));
        profile.connection.credential.reference = argv[4];
        profile.expected_genesis = argv[5];
        auto& connection = observer.requests.connection;
        observer.Check(connection.SetDraft(profile) && connection.Test(), "explicit fixture connection test");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (connection.Busy() && std::chrono::steady_clock::now() < deadline) {
            connection.Poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        observer.Check(connection.Apply() && CanImportWallet(*connection.Active()) && CanRestoreWallet(*connection.Active()), "actual named transfer capabilities");
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
        observer.Check(observer.stage == 23 && observer.settled && !observer.requests.wallets.Busy(), "owned full transfer journey");
        std::ofstream result(observer.output / "result.json");
        result << "{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":" << observer.checks
            << ",\"frames\":" << observer.frames << ",\"teardown\":true,\"publicBranches\":[";
        bool comma = false;
        for (const auto& branch : observer.imported.descriptors) {
            if (comma) result << ',';
            comma = true;
            result << "{\"script\":" << rpc::Quote(branch.script) << ",\"address\":" << rpc::Quote(branch.address)
                << ",\"descriptor\":" << rpc::Quote(branch.descriptor) << '}';
        }
        result << "]}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        status = 1;
    }
    if (observer.display) XCloseDisplay(observer.display);
    if (xtst) dlclose(xtst);
    return status;
}
