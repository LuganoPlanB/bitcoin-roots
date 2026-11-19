// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/rpc/json.h"
#include "app/message_signing.h"
#include "app/history_workspace.h"
#include "platform/shell_composition.h"
#include "tests/engine/recovery_fixture.h"
#include "tests/wallet_tools_fixture.h"
#include "ui/localization.h"
#include "ui/theme/theme.h"

#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace roots::gui {
namespace {
namespace a = app;
namespace s = ui::shell;
namespace c = ui::components;
namespace fx = test::wallet_tools;
namespace p = protocol;
namespace fs = std::filesystem;

struct Case {
    const char* name;
    bool stress;
};
constexpr Case cases[]{
    {"first-run", false}, {"onboarding", false}, {"transactions", true},
    {"send", true}, {"receive", true}, {"addresses", true}, {"utxos", true},
    {"settings", true}, {"wallet-locked", false}, {"hidden-amounts", true},
    {"workspace-locked", false},
    {"activity-waiting", false}, {"connection-error", false}, {"controls", true},
    {"workbench", true}, {"signing", true}, {"general", true}, {"server", true},
    {"rename", false}, {"delete", false}, {"search", false}, {"summary", false},
    {"transaction-text", false}, {"transaction-qr", false}, {"message", false},
    {"sweep", false}, {"download", false}, {"contacts", false}, {"about", false},
    {"metadata-export", false}, {"metadata-restore", false},
};

void Require(bool condition, std::string_view reason)
{
    if (!condition) throw std::runtime_error(std::string(reason));
}

std::string Read(const fs::path& source)
{
    std::ifstream stream(source, std::ios::binary);
    Require(bool(stream), "missing public fixture");
    return {std::istreambuf_iterator<char>(stream), {}};
}

class NativeInput {
    Display* display{};
    void* xtst{};
    int (*motion)(Display*, int, int, int, unsigned long){};
    int (*button)(Display*, unsigned, Bool, unsigned long){};
    int (*key)(Display*, unsigned, Bool, unsigned long){};
public:
    NativeInput()
    {
        const auto* live = std::getenv("DISPLAY");
        const auto* owned = std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        Require(live && owned && std::string_view(live) == owned, "authenticated owned X11 display required");
        std::unique_ptr<Display, decltype(&XCloseDisplay)> candidate(XOpenDisplay(nullptr), XCloseDisplay);
        const auto close_symbols = [](void* handle) { dlclose(handle); };
        std::unique_ptr<void, decltype(close_symbols)> symbols(dlopen("libXtst.so.6", RTLD_NOW), close_symbols);
        Require(candidate && symbols, "owned X11/XTest capability unavailable");
        motion = reinterpret_cast<decltype(motion)>(dlsym(symbols.get(), "XTestFakeMotionEvent"));
        button = reinterpret_cast<decltype(button)>(dlsym(symbols.get(), "XTestFakeButtonEvent"));
        key = reinterpret_cast<decltype(key)>(dlsym(symbols.get(), "XTestFakeKeyEvent"));
        Require(motion && button && key, "owned native input entry points");
        display = candidate.release();
        xtst = symbols.release();
    }
    ~NativeInput()
    {
        XCloseDisplay(display);
        dlclose(xtst);
    }
    void Move(platform::NativeHost& host, const c::ItemFacts& item)
    {
        int x{}, y{};
        glfwGetWindowPos(host.Window(), &x, &y);
        const auto& r = item.rect;
        Require(item.visible && motion(display, -1, x + int((r[0]+r[2])/2), y + int((r[1]+r[3])/2), 0),
                "move pointer to visible production control");
        XSync(display, False);
    }
    void Button(bool down)
    {
        Require(button(display, 1, down, 0), "actual native button event");
        XSync(display, False);
    }
    void Scroll()
    {
        Require(button(display, 5, True, 0) && button(display, 5, False, 0), "actual native vertical scroll");
        XSync(display, False);
    }
    void Chord(std::initializer_list<KeySym> keys, bool down)
    {
        std::vector<KeySym> order(keys);
        if (!down) std::ranges::reverse(order);
        for (auto symbol : order) {
            Require(key(display, XKeysymToKeycode(display, symbol), down, 0), "actual native key event");
        }
        XSync(display, False);
    }
};

// These ports admit only deterministic public presentation data. No method
// here can start a daemon, contact a backend, unlock a wallet, or submit value.
struct Requests final : a::Requests, a::DesktopActions {
    test::RecoveryFixtureDirectory scratch;
    a::RecoverySession recovery{scratch.Metadata()};
    mutable TransactionDocuments documents;
    fx::Wallets wallets;
    bool multiple_coin_groups{};
    RefreshService refresh{[this](const auto& request, std::stop_token) {
        auto result = wallets.Fetch(request);
        if (multiple_coin_groups && result.history && result.history->normalized) {
            auto& coins = result.history->normalized->coins;
            auto second = coins.front();
            second.outpoint.index = 1;
            second.amount = Money::Satoshis(11);
            coins.push_back(std::move(second));
        }
        return result;
    }};
    ConnectionService connection{ConnectionService::Probe{}};
    WalletLifecycle lifecycle{[](const WalletOperation&, std::stop_token) {
        throw std::runtime_error("unexpected wallet RPC in visual fixture");
        return WalletResult{};
    }};
    ReceiveService receive{[](const ReceiveRequest&, std::stop_token) {
        throw std::runtime_error("unexpected address allocation in visual fixture");
        return ReceiveReply{};
    }};
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    a::MessageSigning messages{transport, connection};
    bool observations{}, connection_visible{};
    a::HostObservations status;

    Requests() { Require(recovery.Start(), "isolated visual metadata store"); }
    a::Receipt Submit(const a::Request&) override { throw std::runtime_error("unexpected command effect in visual fixture"); }
    a::HostObservations Observe() const override { return status; }
    a::RecoverySession* Recovery() override { return &recovery; }
    AddressService* Addresses() override { return observations ? &wallets.addresses : nullptr; }
    RefreshService* Refresh() override { return observations ? &refresh : nullptr; }
    ConnectionService* Connection() override { return connection_visible ? &connection : nullptr; }
    WalletLifecycle* Wallets() override { return &lifecycle; }
    ReceiveService* Receiving() override { return &receive; }
    a::MessageSigning* Messages() override { return &messages; }
    TransactionDocuments* LocalDocuments() const override { return &documents; }
    a::DesktopActions* Desktop() override { return this; }
    a::DesktopReceipt Available(a::DesktopCapability) const override { return {false, "Unavailable in public visual fixture"}; }
    a::DesktopReceipt Minimize() override { return {}; }
    a::DesktopReceipt TestNotification() override { return {}; }
    a::DesktopReceipt OpenHelp(a::HelpTarget, std::string_view) override { return {}; }
    a::DesktopReceipt Restart() override { return {}; }
};

struct Observer final : platform::NativeObserver {
    NativeInput& input;
    s::State state;
    s::Facts facts;
    Requests requests;
    const fs::path output, fixtures;
    const std::string name;
    const int width, height;
    const bool dark;
    const float scale;
    std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
    std::string rendered;
    std::vector<std::string> captures;
    bool drawn_dark{};
    uint64_t source{}, sequence{};
    int stage{}, next_frame{5};
    bool cleaned{}, done{};

    Observer(NativeInput& input, fs::path out, fs::path fixtures, std::string name,
             int width, int height, bool dark, float scale)
        : input(input), output(std::move(out)), fixtures(std::move(fixtures)),
          name(std::move(name)), width(width), height(height), dark(dark), scale(scale)
    {
        Seed();
    }

    void SeedSigning()
    {
        constexpr auto network = p::DisplayNetwork::Regtest;
        const auto parsed = p::ParseTransactionDocument(Read(fixtures/"original.psbt"), network).document;
        Require(parsed && parsed->transaction && parsed->transaction->fee, "public PSBT and exact fee");
        const auto descriptor = p::PreviewPublicWallet(p::PublicImportKind::Descriptor, Read(fixtures/"descriptor.txt"), "regtest");
        Require(descriptor.preview && descriptor.preview->descriptors.size() == 1, "independent public multisig descriptor");
        const auto& proof = descriptor.preview->descriptors.front();
        const a::WalletBinding wallet{*BackendSessionId::Make("release-public-proof"),
            *WalletId::Make("Public multi-signature wallet"), {*Hash256::Parse(fx::REGTEST)}, {}};
        const auto origin = state.documents.OpenWallet("Public multi-signature wallet", wallet, true);
        auto* draft = state.documents.Find(origin);
        a::CoinDraft pin{wallet, {}, {}};
        p::OfflineReviewEvidence review{network, {}, parsed->transaction->outputs, *parsed->transaction->fee, {}};
        size_t index{};
        for (const auto& item : parsed->transaction->inputs) {
            Require(item.previous_value && item.previous_script == proof.script, "independent previous output script");
            const auto previous = Read(fixtures/("previous-"+std::to_string(index++)+".hex"));
            pin.inputs.push_back({item.outpoint, item.previous_value, item.previous_script, {}, {},
                p::OfflineCoinProof{previous, proof.descriptor, Read(fixtures/"redeem.hex"), {}, 1700000000000}});
            review.inputs.push_back({item.outpoint, *item.previous_value, proof.descriptor});
        }
        draft->coin_draft = std::move(pin);
        const auto revision = draft->draft_revision;
        source = state.documents.OpenImportedDocument({parsed, network});
        Require(source && state.documents.ActiveId() == source, "active exact public PSBT");
        auto& view = state.imported_views[source];
        view.wallet = wallet;
        view.review_evidence = std::move(review);
        view.offline_review_origin = std::pair{origin, revision};
    }

    void Seed()
    {
        requests.status.network = {a::ServiceState::Ready, "Regtest"};
        requests.status.connection = {a::ServiceState::Offline, "Offline · dated public data"};
        if (name == "first-run" || name == "onboarding") {
            requests.connection_visible = name == "onboarding";
            return;
        }
        if (name == "workbench" || name == "signing") {
            requests.connection_visible = true;
            SeedSigning();
            return;
        }
        requests.observations = name == "transactions" || name == "addresses" || name == "utxos"
            || name == "hidden-amounts" || name == "search" || name == "summary";
        requests.multiple_coin_groups = name == "utxos";
        if (requests.observations) {
            requests.wallets.Seed(state, requests.refresh, requests.recovery.Gate().Epoch());
        } else {
            const a::WalletBinding wallet{*BackendSessionId::Make("release-public"),
                *WalletId::Make("public-wallet"), {*Hash256::Parse(fx::REGTEST)}, {}};
            const auto id = state.documents.OpenWallet("Public watch-only wallet", wallet, true);
            state.documents.Find(id)->history.enabled = name != "controls" && name != "wallet-locked";
        }
        auto& wallet = *state.documents.Active();
        if (name == "send") {
            constexpr auto target = "bcrt1qrk5wn7dxwy8l6ru39h6xvmdhxnvdh8p0kpjlxw";
            Require(wallet.recipients.ApplyDestination(1, std::string("bitcoin:")+target+"?amount=0.12345678",
                p::DisplayNetwork::Regtest, wallet.draft_revision), "valid exact Send fixture");
        }
        if (name == "receive") {
            constexpr auto pubkey = "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";
            const auto key = p::PreviewPublicWallet(p::PublicImportKind::Descriptor,
                std::string("wpkh(")+pubkey+")", "regtest").preview;
            Require(key && key->descriptors.size() == 1, "known public Receive descriptor");
            const auto& item = key->descriptors.front();
            Require(!item.address.empty(), "known Receive address");
            auto& receive = state.receives[wallet.id];
            receive.scope = a::HistoryMetadataScope(*wallet.binding);
            receive.selection = s::ReceiveSelection{{item.address, item.script, item.descriptor, "", "regtest",
                std::nullopt, fx::WHEN}, s::ReceiveSource::Inventory};
        }
        if (name == "wallet-locked") wallet.observation = a::ObservationState::Locked;
        if (name == "workspace-locked") {
            Require(requests.recovery.Protect(state.documents, "public visual fixture passphrase")
                && requests.recovery.Lock(state.documents), "isolated production workspace lock");
        }
        if (name == "controls") wallet.name = "Public watch-only wallet with a long name for identifier inspection";
        if (name == "server" || name == "settings" || name == "message") requests.connection_visible = true;
        if (name == "activity-waiting") {
            requests.status.activity = {a::ServiceState::Loading, "Waiting for wallet observations"};
            requests.status.tasks = {{1, "Wallet refresh", "Waiting for dated observations", a::ServiceState::Loading, 425}};
        }
        if (name == "connection-error") {
            requests.status.connection = {a::ServiceState::Error, "Backend unavailable"};
            requests.status.error = "The configured loopback endpoint did not respond.";
        }
    }

    void Draw(platform::NativeHost& host, const imkit::Theme& theme, const ui::BootstrapAppearance& appearance)
    {
        // ImGui's diagnostic text log is local to this process. Do not change
        // the real native clipboard while collecting presentation evidence.
        auto& io = ImGui::GetPlatformIO();
        const auto callback = io.Platform_SetClipboardTextFn;
        auto* const data = io.Platform_ClipboardUserData;
        io.Platform_ClipboardUserData = this;
        io.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
            static_cast<Observer*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->rendered = text;
        };
        ImGui::LogToClipboard();
        drawn_dark = appearance.dark;
        facts = platform::DrawShellFrame(host, state, requests, theme, appearance);
        ImGui::LogFinish();
        io.Platform_SetClipboardTextFn = callback;
        io.Platform_ClipboardUserData = data;
    }
    bool CaptureFramebuffer() const override { return true; }
    void Idle(platform::NativeHost&, const platform::NativeFrame&) override
    {
        if (std::chrono::steady_clock::now() - started < std::chrono::seconds(15)) return;
        std::ofstream(output/(name+"-deadline.txt")) << stage << '\n' << state.presentation.Snapshot();
        throw std::runtime_error(name+" visual journey timed out at stage "+std::to_string(stage));
    }

    bool Semantic(std::string_view label, c::ax::SemanticAction action, std::string value = {})
    {
        const auto packet = state.presentation.exchange.Published();
        if (!packet || !packet->valid) return false;
        const auto item = std::ranges::find_if(packet->nodes, [&](const auto& node) {
            return node.name == label && !node.state.disabled && c::ax::Supports(node.actions, action);
        });
        if (item == packet->nodes.end()) return false;
        const auto queued = state.presentation.exchange.QueueNext(packet->generation, item->id, action, std::move(value));
        Require(queued.second == c::ActionResult::Queued, "current published semantic action queued");
        sequence = queued.first;
        std::ofstream(output/(name+"-interaction.log"), std::ios::app) << "queued " << sequence << ' ' << packet->generation << ' ' << label << '\n';
        return true;
    }

    bool Visible(std::string_view label, const platform::NativeFrame& frame) const
    {
        const auto packet = state.presentation.exchange.Published();
        const auto& content = facts.layout.content;
        return packet && std::ranges::any_of(packet->nodes, [&](const auto& node) {
            return node.name == label && node.showing && node.minimum.x >= content.x && node.minimum.y >= content.y
                && node.maximum.x <= std::min(float(frame.width), content.x + content.w - 1)
                && node.maximum.y <= std::min(float(frame.height), content.y + content.h - 1)
                && node.maximum.x > node.minimum.x && node.maximum.y > node.minimum.y;
        });
    }

    void ScrollContent(platform::NativeHost& host, const platform::NativeFrame& f)
    {
        const auto& r = facts.layout.content;
        input.Move(host, {{r.x,r.y,r.x+r.w,r.y+r.h},0,false,false,false,false,true});
        input.Scroll();
        next_frame = f.rendered_frames+3;
    }

    bool SettledAction()
    {
        if (!sequence) return true;
        const auto receipts = state.presentation.exchange.Receipts();
        const auto receipt = std::ranges::find_if(receipts, [&](const auto& r) { return r.sequence == sequence; });
        if (receipt == receipts.end()) return false;
        std::ofstream(output/(name+"-interaction.log"), std::ios::app) << "settled " << sequence << ' ' << int(receipt->result) << '\n';
        Require(receipt->result == c::ActionResult::Applied, "expected applied semantic receipt");
        sequence = 0;
        return true;
    }

    void Capture(platform::NativeHost& host, const platform::NativeFrame& f, const std::string& id)
    {
        const auto packet = state.presentation.exchange.Published();
        Require(packet && packet->valid && !packet->overflow, "fresh bounded published semantics");
        Require(f.width == width && f.height == height && f.effective_scale == scale
            && f.decorated && f.frame_top > 0 && f.font_texture && !f.gl_error
            && f.rgba.size() == size_t(f.framebuffer_width)*f.framebuffer_height*4, "actual native geometry, font and valid framebuffer");
        Require(drawn_dark == dark, "actual requested appearance");
        auto path = [&](const char* extension) { return output/(id+extension); };
        std::ofstream ppm(path(".ppm"), std::ios::binary);
        ppm << "P6\n" << f.framebuffer_width << ' ' << f.framebuffer_height << "\n255\n";
        std::vector<char> row(size_t(f.framebuffer_width)*3);
        for (int y = f.framebuffer_height-1; y >= 0; --y) {
            for (int x = 0; x < f.framebuffer_width; ++x) {
                std::copy_n(f.rgba.begin()+(size_t(y)*f.framebuffer_width+x)*4, 3, row.begin()+x*3);
            }
            ppm.write(row.data(), row.size());
        }
        std::ofstream semantics(path(".semantics.json"));
        semantics << std::boolalpha << "{\"generation\":" << packet->generation << ",\"nodes\":[\n";
        bool comma{};
        for (const auto& node : packet->nodes) {
            semantics << (std::exchange(comma, true) ? ",\n" : "") << "{\"id\":" << node.id << ",\"parent\":" << node.parent << ",\"role\":" << int(node.role)
                << ",\"name\":" << rpc::Quote(node.name) << ",\"value\":" << rpc::Quote(node.value)
                << ",\"description\":" << rpc::Quote(node.description) << ",\"showing\":" << node.showing
                << ",\"selected\":" << node.state.selected << ",\"focused\":" << node.state.focused
                << ",\"disabled\":" << node.state.disabled << ",\"actions\":" << int(node.actions)
                << ",\"rect\":[" << node.minimum.x << ',' << node.minimum.y << ',' << node.maximum.x << ',' << node.maximum.y << "]}";
        }
        semantics << "\n]}\n";
        std::ofstream meta(path(".json"));
        meta << std::boolalpha << "{\"id\":" << rpc::Quote(id) << ",\"case\":" << rpc::Quote(name)
            << ",\"width\":" << f.width << ",\"height\":" << f.height << ",\"framebufferWidth\":" << f.framebuffer_width
            << ",\"framebufferHeight\":" << f.framebuffer_height << ",\"dpiX\":" << f.dpi_x << ",\"dpiY\":" << f.dpi_y
            << ",\"locale\":" << rpc::Quote(ui::locale::LanguageTag())
            << ",\"scale\":" << f.effective_scale << ",\"dark\":" << dark << ",\"fontSize\":" << ImGui::GetFontSize()
            << ",\"frame\":" << f.rendered_frames << ",\"renderer\":" << rpc::Quote(host.Renderer())
            << ",\"imgui\":" << rpc::Quote(ImGui::GetVersion()) << ",\"navVisible\":" << ImGui::GetIO().NavVisible
            << ",\"hiddenAmounts\":" << state.privacy.hidden << ",\"activeDocument\":" << facts.active_document
            << ",\"section\":" << facts.section << ",\"pointerDown\":" << f.mouse_down << "}\n";
        std::ofstream text(path(".presentation.txt"));
        text << rendered;
        Require(bool(ppm) && bool(meta) && bool(semantics) && bool(text), "capture evidence written");
        captures.push_back(id);
    }

    void Final(platform::NativeHost& host, const platform::NativeFrame& frame)
    {
        Capture(host, frame, name);
        done = true;
        host.RequestClose();
    }

    void Frame(platform::NativeHost& host, const platform::NativeFrame& f) override
    {
        if (f.rendered_frames < next_frame || !SettledAction()) return;
        const auto packet = state.presentation.exchange.Published();
        if (!packet || !packet->valid) return;
        if (name == "signing") {
            Signing(host, f);
            return;
        }
        if (name == "controls") {
            Controls(host, f);
            return;
        }
        if (stage == 0) {
            Open();
            stage = 1;
            next_frame = f.rendered_frames+3;
            return;
        }
        if (name == "hidden-amounts") {
            if (stage == 1) {
                input.Chord({XK_Control_L, XK_Shift_L, XK_H}, true);
                ++stage;
                return;
            }
            if (stage == 2) {
                input.Chord({XK_Control_L, XK_Shift_L, XK_H}, false);
                ++stage;
                return;
            }
            const auto displayed = ui::locale::Btc(Money::Satoshis(200000003));
            Require(state.privacy.hidden && rendered.find(displayed) == std::string::npos
                && state.presentation.Snapshot().find(displayed) == std::string::npos
                && std::ranges::none_of(packet->nodes, [](const auto& n) {
                    return n.name == "Copy amount" && !n.state.disabled;
                }), "native privacy redacts the exact localized amount and retires copy");
        }
        if (name == "send" && ui::locale::Messages() == ui::locale::Language::Pseudo) {
            constexpr std::string_view destination = "bcrt1qrk5wn7dxwy8l6ru39h6xvmdhxnvdh8p0kpjlxw";
            struct Control { const char* key; const char* visible; const char* accessible; };
            constexpr Control controls[]{
                {"pay-to", "Pay to", "Pay to"}, {"apply", "Apply destination", "Apply destination"},
                {"wallet", "Wallet address", "Choose an open wallet address"},
                {"qr", "Import QR", "Import payment QR"}, {"amount", "Amount (BTC)", "Amount (BTC)"},
                {"unit", "Use sats", "Use satoshis"}, {"max", "Max", "Use remaining funds for this recipient"}
            };
            for (const auto& control : controls) {
                const auto label=ui::locale::Text(control.accessible);
                const auto node=std::ranges::find_if(packet->nodes,[&](const auto& n){return n.name==label;});
                Require(node!=packet->nodes.end(), "localized action published");
                const auto* clip=packet->Find(node->parent);
                const auto& item=facts.send.at(control.key);
                Require(clip && clip->name==ui::locale::Text("Payment recipient")
                    && item.rect[0]>=clip->minimum.x && item.rect[2]<=clip->maximum.x,
                    "entire localized Send control fits its actual table ancestor");
                Require(ImGui::CalcTextSize(ui::locale::Text(control.visible)).x<=item.rect[2]-item.rect[0],
                    "long localized label fits its control");
                if (std::string_view(control.key)=="pay-to") Require(node->value==destination
                    && ui::theme::ActionWidth(node->value.c_str())<=item.rect[2]-item.rect[0],
                    "localized entry fits and exposes the full exact destination");
            }
            if (stage == 1) {
                if (!Semantic(ui::locale::Text("Choose an open wallet address"), c::ax::SemanticAction::Focus)) return;
                ++stage;
                next_frame = f.rendered_frames+3;
                return;
            }
            if (stage == 2) {
                if (!facts.send.at("wallet").focused || !Visible(ui::locale::Text("Choose an open wallet address"), f)) return;
                Capture(host, f, "send-destination");
                if (!Semantic(ui::locale::Text("Amount (BTC)"), c::ax::SemanticAction::Focus)) return;
                ++stage;
                next_frame = f.rendered_frames+3;
                return;
            }
            if (!facts.send.at("amount").focused || !Visible(ui::locale::Text("Amount (BTC)"), f)) return;
        }
        if (name == "workbench") {
            if (stage == 1) {
                if (!Visible("Decoded PSBT", f)) { ScrollContent(host, f); return; }
                Capture(host, f, "workbench-decoded");
                if (!Semantic("Original source", c::ax::SemanticAction::Press)) return;
                ++stage;
                next_frame = f.rendered_frames+3;
                return;
            }
            Require(state.imported_views.at(source).hex
                && state.imported_views.at(source).bytes == p::InspectionBytes::Source, "actual original-source hex selected");
        }
        if (name == "utxos") {
            Coins(host, f);
            return;
        }
        if (name == "onboarding") Require(state.onboarding.dialog.open, "onboarding dialog reached");
        if (name == "general") Require(facts.preferences_visible, "General preferences reached");
        if (name == "server") Require(facts.server.visible, "Server panel reached");
        if (name == "about") Require(facts.desktop_visible, "About dialog reached");
        if (name == "settings") {
            Require(Visible("Review Apply", f), "full Review Apply action in the viewport");
            if (stage == 1) {
                Require(Visible("Multisig", f), "full Multisig choice in the viewport");
                if (!Semantic("Multisig", c::ax::SemanticAction::Press)) return;
                Capture(host, f, "settings-single-signature");
                ++stage;
                next_frame = f.rendered_frames+3;
                return;
            }
            Require(state.settings.at(state.documents.ActiveId()).multisig, "visible policy choice changed the local settings draft");
        }
        if (name == "metadata-export" || name == "metadata-restore") {
            const auto mode = name == "metadata-export" ? s::MetadataBackupMode::Export : s::MetadataBackupMode::Restore;
            Require(state.recovery_dialog.open && state.metadata_backup.mode == mode, "metadata dialog reached");
        }
        if (name == "workspace-locked") Require(requests.recovery.Locked() && !facts.active_document, "protected workspace contents retired");
        if (name == "transactions") {
            const auto& rail = facts.layout.rail;
            for (const auto& item : facts.navigation) {
                Require(item.visible && item.rect[1] >= rail.y && item.rect[3] <= rail.y+rail.h,
                        "all six navigation choices fit the scaled wallet rail");
            }
        }
        Final(host, f);
    }

    bool CoinControl(std::string_view name, std::string_view group_name) const
    {
        const auto packet = state.presentation.exchange.Published();
        const auto action = std::ranges::find_if(packet->nodes, [&](const auto& node) { return node.name == name; });
        Require(action != packet->nodes.end(), "published UTXO control missing");
        const auto* clip = packet->Find(action->parent);
        Require(clip && clip->name == group_name, "UTXO control has its actual ancestor clip");
        const auto label = ImGui::CalcTextSize(action->name.c_str());
        Require(action->minimum.x >= clip->minimum.x && action->maximum.x <= clip->maximum.x
            && label.x <= action->maximum.x-action->minimum.x && label.y <= action->maximum.y-action->minimum.y,
            "entire UTXO action and label fit their ancestor width");
        return action->showing && action->minimum.y >= clip->minimum.y && action->maximum.y <= clip->maximum.y;
    }

    void Coins(platform::NativeHost& host, const platform::NativeFrame& f)
    {
        const auto id = state.documents.ActiveId();
        const auto& chart = state.coins.at(id);
        const auto& selection = state.documents.Active()->coins.Selection();
        const auto exact = a::CoinKey({fx::Hash('2'), 1});
        constexpr std::string_view chart_controls = "Coin chart controls";
        constexpr std::string_view inventory_actions = "Coin inventory actions";
        if (stage == 1) {
            bool full = true;
            for (auto label : {"Previous", "Toggle group", "Next"}) full &= CoinControl(label, chart_controls);
            for (auto label : {"Sort value", "Sort date", "Export CSV", "Reload labels"}) full &= CoinControl(label, inventory_actions);
            if (!full) { ScrollContent(host, f); return; }
            Require(chart.chart_group == 0 && selection.empty(), "two-group UTXO fixture begins unselected");
            input.Move(host, facts.coin.at("next-group"));
            input.Button(true);
        } else if (stage == 2) {
            input.Button(false);
            std::ofstream(output/(name+"-interaction.log"), std::ios::app) << "owned XTest click on fully visible Next\n";
        } else if (stage == 3) {
            Require(chart.chart_group == 1 && CoinControl("Toggle group", chart_controls), "native Next selected the second exact group");
            if (!Semantic("Toggle group", c::ax::SemanticAction::Press)) return;
        } else if (stage == 4) {
            Require(selection.size() == 1 && selection.contains(exact), "toggle selected the intended exact outpoint only");
            const auto exact_amount = Money::Satoshis(11);
            const auto shown = ui::locale::Btc(exact_amount);
            Require(state.presentation.Snapshot().find("#2 "+shown+" BTC (1/1 selected)") != std::string::npos,
                "selected chart group publishes the localized exact 11-satoshi value");
            const auto presented = state.privacy.Present(exact_amount);
            Require(presented.copy == exact_amount.btc(), "localized amounts retain canonical copy bytes");
            const auto rows = chart.inventory->Ordered(false, false);
            const auto csv = a::CoinCsv(*chart.inventory, rows, chart.labels, false);
            Require(csv && csv->find("0.00000011") != std::string::npos
                && (shown==exact_amount.btc() || csv->find(shown)==std::string::npos)
                && !a::CoinCsv(*chart.inventory, rows, chart.labels, true),
                "CSV retains canonical amounts and refuses a hidden export");
            Require(CoinControl("Previous", chart_controls), "fully visible Previous");
            Capture(host, f, "utxos-controls");
            if (!Semantic("Previous", c::ax::SemanticAction::Press)) return;
        } else if (stage == 5) {
            Require(chart.chart_group == 0 && selection.contains(exact), "Previous changed the group without changing selection");
            if (!Visible("Send Selected", f)) { ScrollContent(host, f); return; }
            Capture(host, f, name);
            input.Chord({XK_Control_L, XK_Shift_L, XK_H}, true);
        } else if (stage == 6) {
            input.Chord({XK_Control_L, XK_Shift_L, XK_H}, false);
        } else {
            const auto published = state.presentation.Snapshot();
            Require(state.privacy.hidden && selection.size() == 1 && selection.contains(exact)
                && rendered.find(ui::locale::Btc(Money::Satoshis(11))) == std::string::npos
                && published.find(ui::locale::Btc(Money::Satoshis(11))) == std::string::npos
                && !facts.coin.contains("toggle-group") && facts.coin.at("csv").disabled,
                "privacy hides amounts and chart actions, disables CSV, and retains exact selection");
            Capture(host, f, "utxos-hidden");
            done = true;
            host.RequestClose();
        }
        ++stage;
        next_frame = f.rendered_frames+3;
    }

    void Open()
    {
        using a::Section;
        constexpr std::array<std::string_view,6> sections{"transactions", "send", "receive", "addresses", "utxos", "settings"};
        const auto section = std::ranges::find(sections, name);
        if (section != sections.end()) {
            Require(state.documents.Navigate(Section(section-sections.begin())), "intended actual section selected");
        } else if (name == "onboarding") {
            s::BeginOnboarding(state, s::OnboardingPage::Create, requests);
        } else if (name == "general") {
            s::OpenGeneralPreferences(state);
        } else if (name == "server") {
            ui::OpenServer(state.server, requests.connection);
        } else if (name == "workbench") {
            Require(s::ToggleTransactionHex(state, requests), "actual workbench open action");
        } else if (name == "activity-waiting" || name == "connection-error") {
            state.drawer = name == "activity-waiting" ? s::Drawer::Activity : s::Drawer::Services;
        } else if (name == "rename") {
            s::BeginRename(state, state.documents.ActiveId());
        } else if (name == "delete") {
            s::BeginDelete(state, state.documents.ActiveId());
        } else if (name == "search" || name == "summary") {
            s::OpenWalletTool(state, requests, name == "search" ? s::WalletTool::CurrentSearch : s::WalletTool::Summary);
        } else if (name == "transaction-text" || name == "transaction-qr") {
            Require(s::BeginLocalDocument(state, requests, name == "transaction-text" ? s::LocalDocumentMode::Text
                : s::LocalDocumentMode::Qr), "actual public transaction import dialog");
        } else if (name == "message") {
            Require(s::OpenMessage(state, requests), "actual local message tool");
        } else if (name == "sweep") {
            Require(s::OpenSweepNotice(state, requests), "actual unavailable sweep notice");
        } else if (name == "download") {
            Require(s::OpenDownload(state, requests), "actual local file verifier");
        } else if (name == "contacts") {
            Require(s::OpenPaymentProtocolNotice(state, requests), "actual unavailable contacts notice");
        } else if (name == "about") {
            Require(s::OpenDesktop(state, requests, s::DesktopPage::About), "actual About route");
        } else if (name == "metadata-export" || name == "metadata-restore") {
            state.recovery_dialog.open = true;
            state.recovery_dialog.initialFocus = true;
            Require(s::OpenMetadataBackup(state, requests, name == "metadata-export" ? s::MetadataBackupMode::Export
                : s::MetadataBackupMode::Restore), "actual local public metadata dialog");
        }
    }

    void Controls(platform::NativeHost& host, const platform::NativeFrame& f)
    {
        const auto id = state.documents.ActiveId();
        const auto& tab = facts.tabs.at(id);
        if (stage == 0) {
            Capture(host, f, "controls-normal");
            input.Move(host, tab);
        } else if (stage == 1) {
            Require(facts.tab_hovered.at(id), "actual pointer hover");
            Capture(host, f, "controls-hover");
            input.Button(true);
        } else if (stage == 2) {
            Require(facts.tab_held.at(id), "actual pointer press");
            Capture(host, f, "controls-pressed");
            input.Button(false);
        } else if (stage == 3 || stage == 4) {
            input.Chord({XK_Tab}, stage == 3);
            std::ofstream(output/(name+"-interaction.log"), std::ios::app) << "owned XTest Tab " << (stage == 3 ? "press" : "release") << '\n';
        } else {
            const auto packet = state.presentation.exchange.Published();
            Require(ImGui::GetIO().NavVisible && std::ranges::any_of(packet->nodes, [](const auto& node) {
                return node.state.focused;
            }), "native Tab yields visible focus in the published workspace");
            Require(std::ranges::any_of(packet->nodes, [](const auto& node) {
                return node.role == c::ax::SemanticRole::Tab && node.state.selected;
            }), "selected tab retained independently of focus");
            Final(host, f);
        }
        ++stage;
        next_frame = f.rendered_frames+2;
    }

    void Signing(platform::NativeHost& host, const platform::NativeFrame& f)
    {
        const auto press = [&](const char* button) { return Semantic(ui::locale::Text(button), c::ax::SemanticAction::Press); };
        const auto visible = [&](const char* label) { return Visible(ui::locale::Text(label), f); };
        if (stage == 0) {
            if (!press("Signing review")) return;
        } else if (stage == 1) {
            const auto& transaction = *state.documents.Find(source)->imported->parsed->transaction;
            const auto packet = state.presentation.exchange.Published();
            for (const auto& output : transaction.outputs) {
                const auto address = p::OutputAddress(output.script, p::DisplayNetwork::Regtest);
                Require(address && std::ranges::any_of(packet->nodes, [&](const auto& node) {
                    return node.name == "Exact destination" && node.value == *address;
                }), "review publishes each full exact destination");
                Require(std::ranges::any_of(packet->nodes, [&](const auto& node) {
                    return node.value.find(ui::locale::Btc(output.value)+" BTC") != std::string::npos;
                }), "review publishes each exact amount");
            }
            if (std::ranges::find(captures, "signing-review") == captures.end()) {
                if (!visible("Exact destination")) { ScrollContent(host, f); return; }
                Capture(host, f, "signing-review");
            }
            if (!visible("Record signing review")) { ScrollContent(host, f); return; }
            if (!press("Record signing review")) return;
            Capture(host, f, "signing-review-controls");
        } else if (stage == 2) {
            if (!visible("Approve for signing")) { ScrollContent(host, f); return; }
            if (!press("Approve for signing")) return;
        } else if (stage == 3) {
            Require(s::CurrentSigningApproval(state, requests, source) != nullptr, "current exact approval");
            if (!visible("Merge signed text")) { ScrollContent(host, f); return; }
            if (!press("Merge signed text")) return;
            Capture(host, f, "signing-approved");
            Require(host.Input().Copy(p::TransactionBase64(Read(fixtures/"complete.psbt")), true), "owned synthetic signed PSBT clipboard");
        } else if (stage == 4) {
            if (!press("Validate clipboard signatures")) return;
        } else if (stage == 5) {
            if (rendered.find("Complete") == std::string::npos || !facts.transaction_document.contains("finalize-signing")
                || facts.transaction_document.at("finalize-signing").disabled) return;
            if (!visible("Finalize transaction")) { ScrollContent(host, f); return; }
            if (!press("Finalize transaction")) return;
            Capture(host, f, "signing-collected");
        } else if (stage == 6) {
            const auto* final = s::FinalizedMaterial(state, source);
            Require(final && final->final.document->wire == Read(fixtures/"expected.tx"),
                "explicit finalization matches independent fixture bytes");
            if (!visible("View final transaction")) { ScrollContent(host, f); return; }
            if (!press("View final transaction")) return;
            Capture(host, f, "signing-finalized");
        } else if (stage == 7) {
            const auto* active = state.documents.Active();
            Require(active && active->id != source && active->imported
                && active->imported->parsed->wire == Read(fixtures/"expected.tx"), "separate final-byte inspection document");
            Require(s::ToggleTransactionHex(state, requests), "open exact final wire representation");
        } else {
            Require(visible("Raw wire"), "new final document opens with its own visible hex controls");
            Final(host, f);
        }
        ++stage;
        next_frame = f.rendered_frames+3;
    }

    void Teardown(const platform::NativeTeardown& t) override
    {
        cleaned = t.renderer_cleared && t.platform_cleared && t.textures_released
            && t.imgui_cleared && t.context_cleared && t.accessibility_cleared && t.input_cleared;
    }
};
} // namespace
} // namespace roots::gui

int main(int argc, char** argv)
{
    using namespace roots::gui;
    if (argc == 2 && std::string_view(argv[1]) == "--list") {
        bool comma{};
        std::cout << std::boolalpha << '[';
        for (const auto& scenario : cases)
            std::cout << (std::exchange(comma, true) ? "," : "") << "{\"case\":" << rpc::Quote(scenario.name)
                << ",\"stress\":" << scenario.stress << '}';
        std::cout << "]\n";
        return 0;
    }
    if (argc != 8) return 2;
    const fs::path output(argv[1]), fonts(argv[2]), fixtures(argv[3]);
    const int width = std::stoi(argv[4]), height = width == 1200 ? 820 : 640;
    const bool dark = std::string_view(argv[5]) == "dark";
    float scale{};
    const std::string_view scale_text(argv[6]);
    const auto parsed_scale = std::from_chars(scale_text.data(), scale_text.data()+scale_text.size(), scale);
    if (parsed_scale.ec != std::errc{} || parsed_scale.ptr != scale_text.data()+scale_text.size()
        || !std::isfinite(scale) || scale <= 0) return 2;
    const std::string_view selection(argv[7]);
    fs::create_directories(output);
    std::ofstream result(output/"result.jsonl");
    int selected{}, failed{};
    try {
        NativeInput input;
        for (const auto& scenario : cases) {
            if (selection != "all" && selection != scenario.name) continue;
            if (scale == 2 && !scenario.stress) continue;
            ++selected;
            std::vector<std::string> captures;
            bool pass{}, cleaned{};
            std::string error;
            try {
                Observer observer(input, output, fixtures, scenario.name, width, height, dark, scale);
                platform::NativeHost* current{};
                platform::NativeOptions options{width, height, dark, scale, fonts};
                options.fixed_delta_seconds = 1.F/60;
                {
                    platform::NativeHost host(options, &observer, [&](const auto& theme, const auto& appearance) {
                        observer.Draw(*current, theme, appearance);
                    });
                    current = &host;
                    Require(host.Run() == 0, "production host exit");
                }
                captures = std::move(observer.captures);
                cleaned = observer.cleaned;
                Require(cleaned && observer.done && !captures.empty(), "scenario rendered and resources settled");
                pass = true;
            } catch (const std::exception& failure) {
                error = failure.what();
                ++failed;
                std::cerr << scenario.name << ": " << error << '\n';
            }
            result << std::boolalpha << "{\"case\":" << rpc::Quote(scenario.name) << ",\"passed\":" << pass
                << ",\"cleaned\":" << cleaned << ",\"failure\":" << rpc::Quote(error) << ",\"captures\":[";
            bool comma{};
            for (const auto& id : captures) result << (std::exchange(comma, true) ? "," : "") << rpc::Quote(id);
            result << "]}\n" << std::flush;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    return !result || !selected || failed ? 1 : 0;
}
