// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>

using namespace roots::gui;
namespace p = platform;
namespace sh = ui::shell;
namespace c = ui::components;
namespace fs = std::filesystem;

namespace {
struct Chooser final : c::NativeInputPort {
    std::array<std::string, 4> paths;
    size_t opened{};
    bool pending{}, first_poll{};

    void BeginView(std::string_view) override {}
    void Privacy(bool) override {}
    void EnterEditor(uint32_t, bool) override {}
    void LeaveEditor() override {}
    std::optional<std::string> Paste(uint32_t, std::string_view, int, int, int, size_t) override { return {}; }
    bool FileChooserAvailable() const override { return !pending && opened < paths.size(); }
    uint64_t ChooseFile(std::string_view, bool save) override
    {
        if (save || !FileChooserAvailable()) throw std::runtime_error("unexpected file workflow");
        pending = true;
        first_poll = true;
        return ++opened;
    }
    c::FileChoice PollFile(uint64_t id) override
    {
        if (!pending || id != opened) return {c::NativeOutcome::Stale, {}, {}};
        if (std::exchange(first_poll, false)) return {c::NativeOutcome::Pending, {}, {}};
        pending = false;
        return {c::NativeOutcome::Chosen, paths[id - 1], {}};
    }
};

struct Requests final : app::Requests {
    app::Receipt Submit(const app::Request&) override { throw std::runtime_error("download used a host command"); }
};

enum class Step { OpenMenu, MenuRow, ReleaseMenuRow, Browse, EnterFingerprint, Verify, Trusted, Changed,
                  Mismatch, ClearTrust, Untrusted, BeginCancel, Hashing, Cancelled, Done };

struct Observer final : p::NativeObserver {
    Observer(fs::path directory, const fs::path& fixture, std::string verified_fingerprint)
        : output(std::move(directory)), artifact(fixture / "fixture-release.bin"), fingerprint(std::move(verified_fingerprint))
    {
        chooser.paths = {artifact.string(), (fixture / "SHA256SUMS").string(), (fixture / "current.asc").string(),
                         (fixture / "current.key").string()};
        std::ifstream file(artifact, std::ios::binary);
        original.assign(std::istreambuf_iterator<char>(file), {});
    }
    Requests requests;
    Chooser chooser;
    sh::State state;
    sh::Facts facts;
    fs::path output, artifact;
    std::string fingerprint, original;
    Display* display{};
    int (*motion)(Display*, int, int, int, unsigned long){};
    int (*button)(Display*, unsigned int, Bool, unsigned long){};
    Step step{Step::OpenMenu};
    unsigned checks{}, captures{};
    int raw_ready_at{}, last_stage{-1};
    bool done{}, cleaned{};
    static constexpr auto TIMEOUT = std::chrono::seconds(19);
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

    void Check(bool good, const char* error)
    {
        if (!good) throw std::runtime_error("download stage " + std::to_string(int(step)) + ": " + error);
        ++checks;
    }

    void Idle(p::NativeHost&, const p::NativeFrame&) override
    {
        if (std::chrono::steady_clock::now() - start > TIMEOUT) {
            std::ofstream(output / "native-timeout.txt") << int(step) << '\n' << state.presentation.Snapshot();
            throw std::runtime_error("native download deadline");
        }
    }

    bool CaptureFramebuffer() const override { return true; }

    bool Settled(const p::NativeFrame& frame)
    {
        if (last_stage != int(step)) { last_stage = int(step); raw_ready_at = frame.rendered_frames + 2; }
        const auto packet = state.presentation.exchange.Published();
        return frame.rendered_frames >= raw_ready_at && packet && packet->valid && !packet->overflow;
    }

    bool Action(std::string_view name, c::ax::SemanticAction action, std::string value = {})
    {
        const auto packet = state.presentation.exchange.Published();
        if (!packet || !packet->valid || packet->overflow) return false;
        const auto node = std::ranges::find(packet->nodes, name, &c::SemanticEntry::name);
        if (node == packet->nodes.end() || node->state.disabled) return false;
        Check(state.presentation.exchange.QueueNext(packet->generation, node->id, action, std::move(value)).second
            == c::ActionResult::Queued, "current native semantic action queued");
        return true;
    }

    void Press(p::NativeHost& host, const c::ItemFacts& target)
    {
        Check(target.visible && !target.disabled, "native Tools route visible");
        int x{}, y{};
        glfwGetWindowPos(host.Window(), &x, &y);
        Check(motion(display, -1, x + int((target.rect[0] + target.rect[2]) / 2),
                     y + int((target.rect[1] + target.rect[3]) / 2), 0) && button(display, 1, True, 0), "native pointer press");
        XSync(display, False);
    }

    void Change(std::string_view bytes)
    {
        std::ofstream stream(artifact, std::ios::binary | std::ios::trunc);
        stream.write(bytes.data(), bytes.size());
        Check(bool(stream), "fixture file changed by tester");
    }

    void Capture(const p::NativeFrame& frame, const char* name, std::initializer_list<std::string_view> values)
    {
        Check(frame.width == 960 && frame.height == 640 && frame.effective_scale == 1.5F && frame.decorated
            && !frame.gl_error && !frame.rgba.empty(), "actual native geometry and framebuffer");
        const auto packet = state.presentation.exchange.Published();
        Check(packet && packet->valid && !packet->overflow, "bounded current native semantics");
        std::string text;
        for (const auto& node : packet->nodes) text += node.name + " | " + node.value + " | " + node.description + '\n';
        Check(text.find(artifact.parent_path().string()) == std::string::npos && text.find("fixture-release.bin") == std::string::npos,
              "semantic output excludes selected file paths");
        for (const std::string_view result : {"File integrity", "Manifest signature", "Signer trust", "Primary fingerprint"}) {
            const auto shown = std::ranges::find(packet->nodes, result, &c::SemanticEntry::name);
            Check(shown != packet->nodes.end() && shown->showing, "separate result is visible in the native view");
        }
        for (const auto value : values) Check(text.find(value) != std::string::npos, "required separate result");
        std::ofstream ppm(output / (std::string(name) + ".ppm"), std::ios::binary);
        ppm << "P6\n" << frame.framebuffer_width << ' ' << frame.framebuffer_height << "\n255\n";
        for (int y = frame.framebuffer_height - 1; y >= 0; --y) {
            for (int x = 0; x < frame.framebuffer_width; ++x) {
                ppm.write(reinterpret_cast<const char*>(frame.rgba.data() + (size_t(y) * frame.framebuffer_width + x) * 4), 3);
            }
        }
        Check(bool(ppm), "framebuffer written");
        std::ofstream(output / (std::string(name) + "-semantics.txt")) << text;
        ++captures;
    }

    void Frame(p::NativeHost& host, const p::NativeFrame& frame) override
    {
        using A = c::ax::SemanticAction;
        const auto& panel = state.download;
        const auto* verdict = panel.result ? &*panel.result : nullptr;
        if (frame.rendered_frames < 4) return;
        switch (step) {
        case Step::OpenMenu:
            if (Action("Tools", A::Press)) step = Step::MenuRow;
            break;
        case Step::MenuRow:
            if (const auto item = facts.menu_rows.find(app::Command::VerifyDownload); item != facts.menu_rows.end() && item->second.visible) {
                Press(host, item->second);
                step = Step::ReleaseMenuRow;
            }
            break;
        case Step::ReleaseMenuRow:
            Check(button(display, 1, False, 0), "native pointer release");
            XSync(display, False);
            step = Step::Browse;
            break;
        case Step::Browse: {
            constexpr std::array controls{"Browse for Release file", "Browse for SHA-256 manifest",
                                          "Browse for Detached signature", "Browse for Trusted public key"};
            if (!facts.download_visible || panel.chooser || !Settled(frame)) break;
            if (!chooser.opened) {
                Check(facts.download.at("artifact").focused && state.invocation && !state.invocation->dispatched,
                      "native route focuses release input without host dispatch");
            }
            if (chooser.opened < controls.size()) Action(controls[chooser.opened], A::Press);
            else {
                Check(panel.paths[0].data() == chooser.paths[0] && panel.paths[3].data() == chooser.paths[3], "selected local paths retained");
                step = Step::EnterFingerprint;
            }
            break;
        }
        case Step::EnterFingerprint:
            if (Action("Expected fingerprint", A::SetValue, fingerprint)) step = Step::Verify;
            break;
        case Step::Verify:
            if (panel.fingerprint.data() == fingerprint && Action("Verify", A::Press)) step = Step::Trusted;
            break;
        case Step::Trusted:
            if (!verdict || !Settled(frame)) break;
            Check(verdict->integrity == DownloadIntegrity::Match && verdict->signature == DownloadSignature::Valid
                && verdict->trust == DownloadTrust::ExplicitFingerprint, "trusted result");
            Capture(frame, "verify-download", {"SHA-256 matches the exact manifest entry", "Valid for the exact manifest bytes",
                    "matches your independently supplied fingerprint", fingerprint});
            Change(original + "modified artifact");
            step = Step::Changed;
            break;
        case Step::Changed:
            if (verdict || !panel.notice) break;
            Check(std::string_view(panel.notice) == DownloadText(DownloadIssue::Changed), "external change retires old valid result");
            if (Action("Verify", A::Press)) step = Step::Mismatch;
            break;
        case Step::Mismatch:
            if (!verdict || !Settled(frame)) break;
            Check(verdict->integrity == DownloadIntegrity::Mismatch && verdict->signature == DownloadSignature::Valid,
                  "tampered artifact result remains distinct from manifest signature");
            Capture(frame, "verify-download-mismatch", {"SHA-256 does not match", "Valid for the exact manifest bytes"});
            Change(original);
            step = Step::ClearTrust;
            break;
        case Step::ClearTrust:
            if (!verdict && Action("Expected fingerprint", A::SetValue)) step = Step::Untrusted;
            break;
        case Step::Untrusted:
            if (!verdict) {
                if (!panel.request && panel.fingerprint.front() == 0) Action("Verify", A::Press);
                break;
            }
            if (!Settled(frame)) break;
            Check(verdict->integrity == DownloadIntegrity::Match && verdict->signature == DownloadSignature::Valid
                && verdict->trust == DownloadTrust::Unestablished, "signature is valid with no inferred trust");
            Capture(frame, "verify-download-untrusted", {"SHA-256 matches", "Valid for the exact manifest bytes",
                    "No signer trust established", fingerprint});
            fs::resize_file(artifact, 512 * 1024 * 1024);
            step = Step::BeginCancel;
            break;
        case Step::BeginCancel:
            if (!verdict && !panel.request && Action("Verify", A::Press)) step = Step::Hashing;
            break;
        case Step::Hashing:
            if (panel.request && state.download_verifier.Stage() == DownloadStage::HashArtifact
                && Action("Cancel verification", A::Press)) step = Step::Cancelled;
            break;
        case Step::Cancelled:
            if (state.download_verifier.Busy()) break;
            Check(!verdict && !panel.request && panel.notice
                && std::string_view(panel.notice) == DownloadText(DownloadIssue::Cancelled), "native cancellation publishes no result");
            if (Action("Done", A::Press)) step = Step::Done;
            break;
        case Step::Done:
            if (facts.download_visible) break;
            Check(!panel.dialog.open && !panel.result && panel.paths[0].front() == 0 && !chooser.pending,
                  "Done retires inputs and results");
            done = true;
            host.RequestClose();
            break;
        }
    }

    void Teardown(const p::NativeTeardown& t) override
    {
        cleaned = t.renderer_cleared && t.platform_cleared && t.textures_released && t.imgui_cleared && t.context_cleared && t.input_cleared;
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
        Observer observer(argv[1], argv[3], argv[4]);
        observer.display = display;
        observer.motion = reinterpret_cast<decltype(observer.motion)>(dlsym(xtst, "XTestFakeMotionEvent"));
        observer.button = reinterpret_cast<decltype(observer.button)>(dlsym(xtst, "XTestFakeButtonEvent"));
        if (!observer.motion || !observer.button || !DownloadVerifierConfigured()) throw std::runtime_error("test inputs unavailable");
        fs::create_directories(observer.output);
        p::NativeOptions options{960, 640, false, 1.5F, argv[2]};
        options.fixed_delta_seconds = 1.F / 60;
        p::NativeHost* host{};
        {
            p::NativeHost window(options, &observer, [&](const imkit::Theme& theme, const ui::BootstrapAppearance& appearance) {
                c::NativeInputScope chooser(&observer.chooser);
                observer.facts = p::DrawShellFrame(*host, observer.state, observer.requests, theme, appearance);
            });
            host = &window;
            window.Run();
        }
        observer.Check(observer.done && observer.cleaned && observer.captures == 3 && observer.chooser.opened == 4,
                       "native journey and local workers settled");
        std::ofstream(observer.output / "result.json")
            << "{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"assertions\":"
            << observer.checks << ",\"captures\":3,\"chooserSelections\":4,\"teardown\":true}\n";
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
