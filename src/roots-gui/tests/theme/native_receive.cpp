// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include "app/history_workspace.h"
#include "app/connection_observations.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdlib>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
namespace {
namespace shell = ui::shell;
using Clock = std::chrono::steady_clock;
struct Requests final : app::Requests {
    explicit Requests(const std::filesystem::path& metadata) : recovery(metadata) {recovery.Start();}
    app::RecoverySession recovery;
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    ConnectionService connection{[this](const ServerCandidate& profile, std::stop_token stop) {return ProbeCapabilities(transport,profile,stop);}};
    RefreshService refresh{[this](const RefreshRequest& request, std::stop_token stop) {return FetchRefresh(transport,request,stop);}};
    AddressService addresses{[this](const RefreshIdentity& id, const std::vector<std::string>& known, std::stop_token stop) {
        return FetchAddresses(transport,id,stop,{},known);
    }};
    ReceiveService receive{[this](const ReceiveRequest& request, std::stop_token stop) {return FetchReceive(transport,request,stop);}};
    ConnectionService* Connection() override {return &connection;}
    RefreshService* Refresh() override {return &refresh;}
    AddressService* Addresses() override {return &addresses;}
    ReceiveService* Receiving() override {return &receive;}
    app::RecoverySession* Recovery() override {return &recovery;}
    app::HostObservations Observe() const override {return app::ConnectionObservations(connection,refresh);}
    app::Availability CanSubmit(const app::Request&) const override {return {};}
    app::Receipt Submit(const app::Request&) override {throw std::runtime_error("unexpected host request");}
};

struct Observer final : platform::NativeObserver {
    Observer(std::filesystem::path output, const std::filesystem::path& metadata)
        : requests(metadata), out(std::move(output)), metadata(metadata), display(XOpenDisplay(nullptr)), xtst(dlopen("libXtst.so.6",RTLD_NOW))
    {
        Check(display && xtst, "owned native input");
        motion = reinterpret_cast<decltype(motion)>(dlsym(xtst,"XTestFakeMotionEvent"));
        button = reinterpret_cast<decltype(button)>(dlsym(xtst,"XTestFakeButtonEvent"));
        key = reinterpret_cast<decltype(key)>(dlsym(xtst,"XTestFakeKeyEvent"));
        Check(motion && button && key, "XTest input entry points");
    }
    ~Observer() override {if (display) XCloseDisplay(display); if (xtst) dlclose(xtst);}
    Requests requests;
    shell::State state;
    shell::Facts facts;
    std::filesystem::path out, metadata;
    Display* display;
    void* xtst;
    int (*motion)(Display*,int,int,int,unsigned long){};
    int (*button)(Display*,unsigned int,Bool,unsigned long){};
    int (*key)(Display*,unsigned int,Bool,unsigned long){};
    int stage{}, checks{}, frames{}, delay{}, inventory_scrolls{};
    bool lift{}, paste_up{}, enter_up{}, close_up{}, cleaned{};
    uint64_t wallet_tab{}, companion_tab{}, conflict_revision{};
    ReceiveTarget paid, reserved;
    std::string first_clip, uri_clip, previous_target_label;
    Clock::time_point started{Clock::now()};

    void Check(bool condition, const char* label)
    {
        if (!condition) throw std::runtime_error(std::to_string(stage) + ": " + label);
        ++checks;
    }
    bool CaptureFramebuffer() const override {return true;}
    void Idle(platform::NativeHost&, const platform::NativeFrame&) override
    {
        if (Clock::now() - started < std::chrono::seconds(32)) return;
        auto& trouble = state.receives[state.documents.ActiveId()];
        std::ofstream diagnostic(out / "deadline.txt");
        diagnostic << stage << ' ' << trouble.status << ' ' << trouble.label.draft() << ' ' << trouble.label.error << '\n';
        for (const auto& receipt : state.presentation.exchange.Receipts())
            diagnostic << "semantic receipt " << receipt.sequence << ' ' << int(receipt.result) << '\n';
        diagnostic << state.presentation.Snapshot();
        throw std::runtime_error("receive deadline at stage " + std::to_string(stage) + ": " + trouble.status);
    }
    void Capture(const platform::NativeFrame& frame, const char* name)
    {
        Check(frame.width == 960 && frame.height == 640 && frame.effective_scale == 1.5F && frame.decorated && !frame.gl_error && frame.font_texture,
            "owned compact native geometry");
        Check(state.presentation.exchange.Published() && state.presentation.exchange.Published()->valid
            && state.presentation.Tree().Validate() && !state.presentation.Tree().overflow, "bounded current semantics");
        std::ofstream image(out / (std::string(name)+".ppm"), std::ios::binary);
        image << "P6\n" << frame.framebuffer_width << ' ' << frame.framebuffer_height << "\n255\n";
        for (int y = frame.framebuffer_height - 1; y >= 0; --y) {
            for (int x = 0; x < frame.framebuffer_width; ++x)
                image.write(reinterpret_cast<const char*>(frame.rgba.data() + (size_t(y) * frame.framebuffer_width + x) * 4), 3);
        }
        Check(bool(image), "native framebuffer written");
        std::ofstream semantics(out / (std::string(name) + ".semantics.txt"));
        semantics << state.presentation.Snapshot();
        Check(bool(semantics), "native semantics written");
        ++frames;
    }
    void Click(platform::NativeHost& host, const ui::components::ItemFacts& item)
    {
        const auto& r = item.rect;
        if (!item.visible || item.disabled || r[0] < 0 || r[1] < 0 || r[2] > 960 || r[3] > 640) {
            std::ofstream trouble(out / "reachability.txt");
            for (const auto& [name, fact] : facts.receive) {
                trouble << name << ' ' << fact.visible << ' ' << fact.disabled;
                for (auto position : fact.rect) trouble << ' ' << position;
                trouble << '\n';
            }
            trouble << state.presentation.Snapshot();
            Check(false,"reachable native control");
        }
        ++checks;
        int x{}, y{};
        glfwGetWindowPos(host.Window(), &x, &y);
        Check(motion(display,-1,x+int((r[0]+r[2])/2),y+int((r[1]+r[3])/2),0) && button(display,1,True,0), "native pointer pressed");
        lift = true;
        XSync(display, False);
    }
    void Key(KeySym symbol, bool down) {Check(key(display,XKeysymToKeycode(display,symbol),down,0),"native key event");}
    std::string Clipboard(platform::NativeHost& host)
    {
        constexpr uint32_t clipboard_probe = 12345;
        Check(host.Input().RequestInspection(clipboard_probe), "inspect owned native clipboard");
        const auto value = host.Input().Paste(clipboard_probe, "", 0, 0, 0, 4097);
        Check(value.has_value(), "read exact clipboard from owned process");
        return *value;
    }
    bool Node(std::string_view name, std::string_view value) const
    {
        return std::ranges::any_of(state.presentation.Tree().nodes, [&](const auto& node) {
            return node.name == name && node.value == value;
        });
    }
    void Queue(ImGuiID item, ui::components::ax::SemanticAction action)
    {
        namespace c = ui::components;
        const auto packet = state.presentation.exchange.Published();
        Check(packet && packet->valid, "current accessibility packet");
        Check(state.presentation.exchange.QueueNext(packet->generation,item,action).second == c::ActionResult::Queued,
            "actual native semantic selection");
    }
    void PasteLabel(platform::NativeHost& host, const char* text)
    {
        Check(host.Input().Copy(text),"fixture label clipboard");
        Key(XK_Control_L,true); Key(XK_v,true); paste_up = true; XSync(display,False);
    }
    void CloseTab()
    {
        Key(XK_Control_L,true); Key(XK_w,true); close_up = true; XSync(display,False);
    }
    std::pair<uint64_t,std::string> Stored(const storage::Scope& scope, std::string_view address)
    {
        auto stored = requests.recovery.HistoryMetadata(scope);
        Check(bool(stored),"read exact underlying store");
        const auto label = std::ranges::find_if(stored.value->labels,[&](const auto& entry) {
            return entry.target.kind == storage::TargetKind::Address && entry.target.key == address;
        });
        return {stored.value->revision,label==stored.value->labels.end() ? "" : label->text};
    }
    void Frame(platform::NativeHost& host, const platform::NativeFrame& frame) override
    {
        if (lift) {
            Check(button(display,1,False,0),"native pointer released");
            lift = false;
            XSync(display,False);
            return;
        }
        if (paste_up) {
            Key(XK_v,false); Key(XK_Control_L,false); paste_up = false; XSync(display,False);
            return;
        }
        if (enter_up) {
            Key(XK_Return,false); enter_up = false; XSync(display,False);
            return;
        }
        if (close_up) {
            Key(XK_w,false); Key(XK_Control_L,false); close_up = false; XSync(display,False);
            return;
        }
        if (frame.rendered_frames < 5 || !state.documents.Active()) return;
        if (delay > 0) {--delay; return;}
        auto* doc = state.documents.Active();
        const auto click = [&](const char* item) {Click(host,facts.receive.at(item));};
        const auto& runtime = state.receives[doc->id];
        const auto& selection = runtime.selection;
        switch (stage) {
        case 0: Click(host,facts.navigation[2]); break;
        case 1:
            if (!facts.receive.contains("next")) return;
            click("next");
            break;
        case 2:
            if (!selection || !Node("Receive address",selection->target.address)) return;
            Check(selection->source == shell::ReceiveSource::Daemon && !requests.receive.Busy(), "allocated target adoption");
            paid = selection->target;
            Check(Node("Last used","Last used: Unknown"), "missing payment stays Unknown");
            {
                std::ofstream target(out / "displayed.partial");
                target << "{\"address\":" << rpc::Quote(paid.address) << ",\"script\":" << rpc::Quote(paid.script)
                    << ",\"descriptor\":" << rpc::Quote(paid.descriptor) << ",\"path\":" << rpc::Quote(paid.path) << "}\n";
                target.close();
                Check(bool(target),"exact address evidence");
                std::filesystem::rename(out / "displayed.partial",out / "displayed.json");
            }
            if (!facts.receive.at("label").visible) Capture(frame,"label-reachability");
            click("label");
            break;
        case 3:
            PasteLabel(host,"Tips & café");
            break;
        case 4:
            if (runtime.label.draft() != "Tips & café") return;
            Queue(facts.navigation[0].id,ui::components::ax::SemanticAction::Select);
            break;
        case 5: {
            if (doc->section != app::Section::Transactions) return;
            const auto metadata = requests.recovery.HistoryMetadata(*runtime.scope);
            Check(bool(metadata), "read real persisted address labels after leaving Receive");
            const bool saved = std::ranges::any_of(metadata.value->labels, [&](const auto& entry) {
                return entry.target.kind == storage::TargetKind::Address && entry.target.key == paid.address && entry.text == "Tips & café";
            });
            std::ofstream(out / "label-navigation.txt") << "section=Transactions draft=" << runtime.label.draft() << " stored=" << saved << '\n';
            Check(saved,"section navigation without Enter persisted the exact label");
            Click(host,facts.navigation[2]);
            break;
        }
        case 6:
            if (doc->section != app::Section::Receive || !std::filesystem::exists(out / "paid.ready")) return;
            requests.refresh.RequestNow(RefreshMonotonicMs());
            break;
        case 7: {
            const auto history = requests.refresh.View();
            if (!history.chain_current || !history.ledger) return;
            const auto last = ReceiveLastUsed(*history.ledger,paid);
            if (!last || !Node("Last used","Last used: " + app::HistoryDate(*last,true))) return;
            Check(Node("scriptPubKey",paid.script) && Node("Public descriptor",paid.descriptor), "full script and descriptor presented");
            Capture(frame,"receive");
            click("copy-address");
            break;
        }
        case 8:
            if (host.InputState().clipboard_writes < 2) return;
            first_clip = Clipboard(host);
            Check(first_clip == paid.address,"exact address copied");
            click("copy-uri");
            break;
        case 9:
            if (host.InputState().clipboard_writes < 3) return;
            uri_clip = Clipboard(host);
            Check(uri_clip == "bitcoin:" + paid.address + "?label=Tips%20%26%20caf%C3%A9","exact URI copied");
            Click(host,facts.navigation[3]);
            break;
        case 10: {
            if (!facts.address.contains("receive-to")) return;
            const auto history = requests.refresh.View();
            if (!history.identity) return;
            const auto view = requests.addresses.View(*history.identity);
            if (!view.current || !view.snapshot) return;
            const auto row = std::ranges::find(view.snapshot->rows,paid.address,&DerivedAddress::address);
            if (row == view.snapshot->rows.end()) return;
            const auto widget = facts.address.find("row-" + row->key);
            if (widget == facts.address.end() || !widget->second.visible) {
                const auto pane = facts.address_tables[0];
                Check(++inventory_scrolls <= 12 && pane.h > 50,"receive inventory row reachable by scrolling");
                int x{}, y{};
                glfwGetWindowPos(host.Window(), &x, &y);
                Check(motion(display,-1,x+int(pane.x+pane.w/2),y+int(pane.y+pane.h/2),0)
                    && button(display,5,True,0) && button(display,5,False,0),"scroll receive inventory using native wheel");
                XSync(display,False);
                delay = 4;
                return;
            }
            Click(host,widget->second);
            break;
        }
        case 11:
            if (!facts.address.contains("receive-to") || facts.address.at("receive-to").disabled) return;
            Click(host,facts.address.at("receive-to"));
            break;
        case 12:
            if (!selection || !facts.receive.contains("reserve")) return;
            Check(selection->source == shell::ReceiveSource::Inventory && selection->target.address == paid.address
                && selection->target.script == paid.script && selection->target.index, "Receive To retains exact ranged selection");
            Check(runtime.label.saved == "Tips & café","label is scoped to the exact address");
            click("reserve");
            break;
        case 13: {
            const auto pool = requests.recovery.ReceivePool(*runtime.scope);
            if (!pool || pool.value->available != 1) return;
            Check(selection && selection->target.address == paid.address,"reserving does not switch selected address");
            Check(requests.connection.SetOffline(true),"fixture invokes the explicit offline setting");
            requests.addresses.SetOffline(true);
            requests.refresh.SetOffline(true,RefreshMonotonicMs());
            break;
        }
        case 14:
            if (!facts.receive.contains("offline-next")) return;
            click("label");
            break;
        case 15:
            PasteLabel(host," + before replacement");
            break;
        case 16:
            if (runtime.label.draft().find("before replacement") == std::string_view::npos) return;
            previous_target_label = runtime.label.draft();
            Queue(facts.receive.at("offline-next").id,ui::components::ax::SemanticAction::Press);
            break;
        case 17:
            if (!selection || selection->source != shell::ReceiveSource::Reservation || !facts.receive.contains("offline-next")) return;
            if (!Node("Last used","Last used: Unknown") || !Node("Offline address pool","Offline pool exhausted (0 reservations)")) return;
            reserved = selection->target;
            Check(Stored(*runtime.scope,paid.address).second == previous_target_label && runtime.label.saved.empty(),
                "target replacement saves the original label and keeps the reserved address separate");
            Check(reserved.address != paid.address && reserved.script != paid.script && facts.receive.at("offline-next").disabled,
                "a distinct verified offline reservation is displayed once and pool is exhausted");
            Capture(frame,"receive-unknown-history");
            requests.addresses.Unbind();
            requests.refresh.Unbind();
            break;
        case 18:
            if (requests.receive.Busy() || requests.addresses.Busy() || requests.refresh.Busy()) return;
            wallet_tab = doc->id;
            companion_tab = state.documents.OpenTransaction("Public companion");
            Check(companion_tab && state.documents.Activate(wallet_tab),"open a distinct public tab for focus retirement");
            break;
        case 19:
            if (!facts.receive.contains("label") || !facts.tabs.contains(companion_tab)) return;
            click("label");
            break;
        case 20:
            PasteLabel(host,"Tab transition label");
            break;
        case 21:
            if (runtime.label.draft() != "Tab transition label") return;
            Queue(facts.tabs.at(companion_tab).id,ui::components::ax::SemanticAction::Select);
            break;
        case 22:
            if (doc->id != companion_tab) return;
            Check(Stored(*state.receives.at(wallet_tab).scope,reserved.address).second == "Tab transition label",
                "tab change without Enter persists the exact offline scoped label");
            Click(host,facts.tabs.at(wallet_tab));
            break;
        case 23:
            if (doc->id != wallet_tab || !facts.receive.contains("label")) return;
            click("label");
            break;
        case 24:
            PasteLabel(host," + owner draft");
            break;
        case 25: {
            if (runtime.label.draft().find("owner draft") == std::string_view::npos) return;
            auto independent = storage::MetadataStore::Open(metadata);
            Check(bool(independent),"open independent metadata writer");
            auto latest = (*independent.value)->Load(*runtime.scope);
            Check(bool(latest),"independent writer reads current metadata");
            const auto label = std::ranges::find_if(latest.value->labels,[&](const auto& entry) {
                return entry.target.kind == storage::TargetKind::Address && entry.target.key == reserved.address;
            });
            Check(label!=latest.value->labels.end(),"independent writer finds exact reservation label");
            label->text = "Concurrent store label";
            auto write = (*independent.value)->Save(*runtime.scope,*latest.value);
            Check(bool(write),"independent CAS write makes active label stale");
            conflict_revision = *write.value;
            CloseTab();
            break;
        }
        case 26:
            Check(doc->id == wallet_tab && state.invocation && state.invocation->command == app::Command::CloseTab
                && !state.invocation->accepted, "keyboard tab close refuses to discard the live conflicting draft");
            Check(runtime.label.dirty() && runtime.label.draft().find("owner draft") != std::string_view::npos
                && runtime.label.error.find("metadata-conflict") != std::string::npos
                && Node("Label save",runtime.label.error), "visible conflict retains original draft");
            Check(Stored(*runtime.scope,reserved.address) == std::pair{conflict_revision,std::string("Concurrent store label")},
                "tab close does not overwrite concurrent label");
            host.RequestClose();
            break;
        case 27:
            Check(doc->id == wallet_tab && runtime.label.dirty()
                && Stored(*runtime.scope,reserved.address) == std::pair{conflict_revision,std::string("Concurrent store label")},
                "native close flag was refused without retrying the failed write");
            if (!facts.receive.contains("reload-label")) return;
            click("reload-label");
            break;
        case 28:
            if (!runtime.label.error.empty()) return;
            Check(runtime.label.dirty() && runtime.label.saved == "Concurrent store label"
                && Stored(*runtime.scope,reserved.address).first == conflict_revision,
                "reload updates CAS base and retains draft without automatic retry");
            click("label");
            break;
        case 29:
            Key(XK_Return,true); enter_up = true; XSync(display,False);
            break;
        case 30:
            if (runtime.label.dirty()) return;
            Check(Stored(*runtime.scope,reserved.address) == std::pair{conflict_revision+1,runtime.label.saved}
                && runtime.label.saved.find("owner draft") != std::string::npos,"explicit Enter commits retained draft after reload");
            CloseTab();
            break;
        case 31:
            Check(!state.documents.Find(wallet_tab) && doc->id == companion_tab, "clean tab closes after acknowledgement");
            std::ofstream(out / "label-conflict.txt") << "tab-save=1 target-replacement-save=1 close-refused=1 native-close-refused=1 auto-retry=0 explicit-retry=1\n";
            host.RequestClose();
            break;
        default: return;
        }
        ++stage;
        delay = 2;
    }
    void Teardown(const platform::NativeTeardown& result) override
    {
        cleaned = result.renderer_cleared && result.platform_cleared && result.textures_released
            && result.imgui_cleared && result.context_cleared && result.accessibility_cleared && result.input_cleared;
        Check(cleaned,"native resources settled");
    }
};
}

int main(int argc, char** argv)
{
    if (argc != 8) return 2;
    try {
        const char* display = std::getenv("DISPLAY");
        const char* owned = std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if (!display || !owned || std::string_view(display) != owned) throw std::runtime_error("owned display required");
        std::filesystem::create_directories(argv[1]);
        Observer observer(argv[1],argv[3]);
        ServerCandidate profile;
        profile.connection.port = uint16_t(std::stoi(argv[4]));
        profile.connection.credential.reference = argv[5];
        profile.expected_genesis = argv[6];
        profile.wallet = argv[7];
        auto& connection = observer.requests.connection;
        observer.Check(connection.SetDraft(profile) && connection.Test(),"explicit fixture connection test");
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (connection.Busy() && Clock::now() < deadline) {
            connection.Poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        observer.Check(connection.Apply() && CanAllocateReceive(*connection.Active()), "actual address allocation capabilities");
        platform::NativeOptions options{960,640,false,1.5F,argv[2]};
        options.fixed_delta_seconds = 1.F / 60;
        options.before_close=[&observer] {
            auto& state=observer.state;
            return shell::SaveReceiveLabelsBeforeClose(state,state.documents.CloseTargets(state.documents.ActiveId(),false,true));
        };
        platform::NativeHost* current{};
        {
            platform::NativeHost host(options, &observer, [&](const imkit::Theme& theme, const ui::BootstrapAppearance& appearance) {
                observer.facts = platform::DrawShellFrame(*current,observer.state,observer.requests,theme,appearance);
            });
            current = &host;
            host.Run();
        }
        observer.Check(observer.stage == 32 && observer.cleaned,"one complete native Receive journey");
        std::ofstream result(observer.out / "result.json");
        result << "{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":" << observer.checks
            << ",\"frames\":" << observer.frames << ",\"teardown\":true,\"nativeCopiedAddress\":" << rpc::Quote(observer.first_clip)
            << ",\"nativeCopiedUri\":" << rpc::Quote(observer.uri_clip)
            << ",\"reservedAddress\":" << rpc::Quote(observer.reserved.address)
            << ",\"reservedScript\":" << rpc::Quote(observer.reserved.script)
            << ",\"reservedDescriptor\":" << rpc::Quote(observer.reserved.descriptor) << "}\n";
        return result ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
