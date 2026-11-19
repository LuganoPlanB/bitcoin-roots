// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/server.h"
#include "services/extensions.h"
#include "ui/components/native_input.h"
#include "ui/theme/theme.h"
#include <algorithm>
#include <charconv>
#include <filesystem>

namespace roots::gui::ui {
namespace {
namespace c = components;
uint16_t Port(std::string_view text)
{
    unsigned port{}; const auto parsed = std::from_chars(text.data(), text.data() + text.size(), port);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && port > 0 && port <= 65535 ? uint16_t(port) : 0;
}
c::ItemFacts Last()
{
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    return {{a.x, a.y, b.x, b.y}, ImGui::GetItemID(), ImGui::IsItemFocused(), false, false, false, ImGui::IsItemVisible()};
}
const char* TestMessage(const BackendCapabilities& value)
{
    if (value.network_mismatch) return "Network identity mismatch. Check the expected genesis and Test again; the working profile stays active.";
    switch (value.error) {
    case rpc::Error::Authentication: return "Authentication failed. Check the credential reference and Test again; the working profile stays active.";
    case rpc::Error::CredentialsUnavailable: return "Credential reference unavailable. Select an explicit cookie file or an available secure-store item.";
    case rpc::Error::Tls: return "TLS verification failed. Check the server identity and trusted CA; verification remains enabled.";
    case rpc::Error::Timeout: return "Connection test timed out. Check the server and proxy, then Test again.";
    case rpc::Error::Resolve: return "Server name could not be resolved. Check the host or proxy DNS settings.";
    case rpc::Error::Proxy: return "Proxy connection failed. Check the proxy host, port and type.";
    case rpc::Error::Connection: return "Could not connect to the server. Check the endpoint and Test again.";
    case rpc::Error::Cancelled: return "Connection test cancelled. Test the current profile before applying it.";
    case rpc::Error::Json: case rpc::Error::Identity: return "The server returned invalid connection metadata. The working profile stays active.";
    case rpc::Error::Limit: return "The server exceeded the connection test limits. The working profile stays active.";
    default: return "Connection test failed. Check the connection settings and Test again; the working profile stays active.";
    }
}
void Close(ServerPanel& panel, ConnectionService& service)
{
    service.Cancel();
    const ServerPanel defaults;
    const auto baseline=service.Active()?service.Active()->candidate:defaults.Candidate();
    service.SetDraft(baseline);
    panel.Load(baseline);
    panel.cookie_dialog=0;
    panel.close_pending = true; panel.dialog.open = false;
    panel.scope_revision = service.Revision(); panel.scope_session = service.Session();
}
}
ServerCandidate ServerPanel::Candidate() const
{
    ServerCandidate value;
    value.connection.host = host.draft(); value.connection.port = Port(port.draft()); value.connection.tls = tls;
    if (tls) value.connection.trusted_ca_file = ca.draft();
    value.connection.credential = {secure_store ? Authentication::SecureStore : Authentication::Cookie, std::string(credential.draft())};
    value.connection.proxy = proxy == 1 ? ProxyKind::Socks5RemoteDns : proxy == 2 ? ProxyKind::HttpTunnel : ProxyKind::Direct;
    if (proxy) { value.connection.proxy_host = proxy_host.draft(); value.connection.proxy_port = Port(proxy_port.draft()); }
    value.expected_genesis = genesis.draft();
    if (select_wallet) value.wallet = std::string(wallet.draft());
    return value;
}
void ServerPanel::Load(const ServerCandidate& value)
{
    const auto& p = value.connection;
    host = c::Edit(p.host); port = c::Edit(std::to_string(p.port)); tls = p.tls; ca = c::Edit(p.trusted_ca_file);
    credential = c::Edit(p.credential.reference); secure_store = p.credential.kind == Authentication::SecureStore;
    select_wallet = value.wallet.has_value(); wallet = c::Edit(value.wallet.value_or("")); genesis = c::Edit(value.expected_genesis);
    proxy = int(p.proxy); proxy_host = c::Edit(p.proxy_host.empty() ? "127.0.0.1" : p.proxy_host); proxy_port = c::Edit(std::to_string(p.proxy_port));
}
void OpenServer(ServerPanel& panel, ConnectionService& service)
{
    if (panel.dialog.open) return;
    if (service.Active()) panel.Load(service.Active()->candidate);
    else if (panel.initialized) panel.Load(service.Draft());
    panel.source=ServerSource::DirectRpc;
    panel.initialized = true; panel.notice.clear(); panel.close_pending = false;
    service.SetDraft(panel.Candidate()); panel.scope_revision = service.Revision(); panel.scope_session = service.Session(); panel.dialog.open = true; panel.dialog.initialFocus = true;
}
ServerFacts DrawServer(ServerPanel& panel, ConnectionService& service, c::PresentationFrame& presentation, const imkit::Theme& theme, float scale,bool hidden)
{
    ServerFacts facts;
    service.Poll();
    if (panel.close_pending) {
        if (ImGui::BeginPopupModal("Server###server")) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); }
        panel.close_pending = false;
    }
    if (!panel.dialog.open) return facts;
    const auto* viewport = ImGui::GetMainViewport();
    const float width = std::min(750 * scale, viewport->WorkSize.x - 32 * scale);
    const float height = std::min(630 * scale, viewport->WorkSize.y - 32 * scale);
    ImGui::SetNextWindowSize({width, height}, ImGuiCond_Always);
    if (!imkit::BeginDialog("Server###server", panel.dialog, {&theme})) {
        if (!panel.dialog.open) Close(panel, service);
        return facts;
    }
    facts.visible = true; presentation.DialogOnly();
    const auto origin = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
    presentation.Group("server-dialog", "Server connection", c::ax::SemanticRole::Dialog, origin, {origin.x + size.x, origin.y + size.y});
    ImGui::BeginChild("server-body", {0, -58 * scale}, ImGuiChildFlags_NavFlattened);
    ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x);
    constexpr const char* sources[]{"Direct RPC","Private index","Public index"};
    const auto select_source=[&panel,&service](ServerSource selected){
        if(!panel.dialog.open || service.PrivacyLocked())return false;
        if(panel.source==selected)return true;
        panel.source=selected;
        panel.notice.clear();
        panel.cookie_dialog=0;
        service.Cancel();
        panel.scope_revision=service.Revision();
        return true;
    };
    for(int i=0;i<3;++i){
        if(i)ImGui::SameLine();
        const auto source=ServerSource(i);
        const bool pressed=ImGui::RadioButton(sources[i],panel.source==source);
        facts.items[sources[i]]=Last();
        if(pressed)select_source(source);
        const auto external=presentation.Interact(sources[i],c::ax::SemanticRole::Radio,c::ax::SemanticAction::Select|c::ax::SemanticAction::Focus,false,panel.source==source);
        if(external)presentation.Defer(*external,[select_source,source](const auto&){return select_source(source);});
    }
    const bool direct=panel.source==ServerSource::DirectRpc;
    bool valid{};
    if(service.Active()){
        ImGui::TextUnformatted("Working profile is active.");
        presentation.Annotate("active","Working connection","Working profile is active",c::ax::SemanticRole::Status);
    }
    if(!direct){
        const auto genesis=service.Active() ? Hash256::Parse(service.Active()->genesis) : std::nullopt;
        const auto index=HistoryIndexStatus(ShippedExtensions(),
            panel.source==ServerSource::PrivateIndex ? IndexAccess::Private : IndexAccess::Public,
            genesis ? std::optional(NetworkIdentity{*genesis}) : std::nullopt);
        ImGui::TextWrapped("%s",index.detail);
        presentation.Annotate("index-status",index.available ? "History index available" : "History index unavailable",
            index.detail,c::ax::SemanticRole::Status);
        constexpr char limits[]="Direct RPC covers the selected wallet. Retained mempool samples report observed fee conditions. Arbitrary address or output history requires an index client.";
        ImGui::TextWrapped("%s",limits);
        presentation.Annotate("index-rpc-coverage","RPC history coverage",limits);
    }else{
    const auto sync = [&panel, &service] { panel.notice.clear(); const bool accepted = service.SetDraft(panel.Candidate()); panel.scope_revision = service.Revision(); panel.scope_session = service.Session(); return accepted; };
    const auto field = [&](const char* id, const char* label, c::Edit& edit, const char* hint = "") {
        imkit::BeginFormField(id, {label, hint, "", std::string_view(id) != "ca"}, {&theme});
        const bool changed = c::EditInput("###input", edit);
        facts.items[id] = Last();
        if (changed) sync();
        const auto external = presentation.Interact(label, c::ax::SemanticRole::TextField, c::ax::SemanticAction::SetValue | c::ax::SemanticAction::Focus, false, false, edit.draft());
        presentation.BindTextPosition(facts.items.at(id).id, edit.draft(), edit.cursor);
        if (external) presentation.Defer(*external, [&edit, sync](const c::SemanticRequest& request) { return edit.SetExternalDraft(request.value) && sync(); });
        imkit::EndFormField();
    };
    const auto toggle = [&](const char* id, const char* label, bool& value) {
        const bool clicked = ImGui::Checkbox(id, &value); facts.items[id] = Last(); if (clicked) sync();
        const auto external = presentation.Interact(label, c::ax::SemanticRole::Toggle, c::ax::SemanticAction::Toggle | c::ax::SemanticAction::Focus, false, value);
        if (external) presentation.Defer(*external, [&value, clicked, sync](const c::SemanticRequest&) { if (!clicked) value = !value; return sync(); });
    };
    ImGui::TextUnformatted("Test a replacement before applying it. Your working connection stays active.");
    {
        bool offline=service.Offline();const bool clicked=ImGui::Checkbox("Offline mode###offline-mode",&offline);facts.items["offline"]=Last();
        const auto apply_offline=[&panel,&service](bool value){const bool ok=service.SetOffline(value);panel.scope_revision=service.Revision();return ok;};
        if(clicked)apply_offline(offline);
        const auto external=presentation.Interact("Offline mode",c::ax::SemanticRole::Toggle,c::ax::SemanticAction::Toggle|c::ax::SemanticAction::Focus,false,service.Offline(),{},"Disable all new network requests and retain dated cached observations.");
        if(external)presentation.Defer(*external,[&service,clicked,apply_offline](const c::SemanticRequest&){return clicked || apply_offline(!service.Offline());});
        if(service.Offline())ImGui::TextWrapped("Offline: network tests and refresh are disabled. Cached observations keep their original date.");
    }
    field("host", "Host", panel.host); field("port", "RPC port", panel.port);
    toggle("Verified TLS###tls", "Verified TLS", panel.tls);
    if (panel.tls) field("ca", "Trusted CA file (optional)", panel.ca, "Leave empty to use system trust. Certificate verification is always enabled.");
    field("credential", "Explicit cookie file", panel.credential, "Enter the exact cookie path. Password authentication requires an OS credential provider.");
    auto* input=c::native_input;
    if(panel.cookie_dialog && input){
        if(panel.cookie_revision!=service.Revision() || panel.cookie_session!=service.Session() || service.PrivacyLocked())panel.cookie_dialog=0;
        else{
            const auto choice=input->PollFile(panel.cookie_dialog);
            if(choice.outcome!=c::NativeOutcome::Pending){
                panel.cookie_dialog=0;
                if(choice.outcome==c::NativeOutcome::Chosen && std::filesystem::path(choice.path).is_absolute()
                    && panel.credential.SetExternalDraft(choice.path))sync();
                else if(choice.outcome!=c::NativeOutcome::Cancelled)panel.notice="Cookie file was not selected. Enter an explicit local path.";
            }
        }
    }
    const auto can_choose=[&panel,&service,input]{return panel.dialog.open && panel.source==ServerSource::DirectRpc
        && !panel.cookie_dialog && !panel.secure_store && !service.PrivacyLocked() && input && input->FileChooserAvailable();};
    const auto choose=[&panel,&service,input,can_choose]{
        if(!can_choose())return false;
        panel.cookie_dialog=input->ChooseRpcCookie();
        panel.cookie_revision=service.Revision();panel.cookie_session=service.Session();
        return panel.cookie_dialog!=0;
    };
    ImGui::BeginDisabled(!can_choose());
    const bool browse=theme::ActionButton("Browse cookie file###browse-cookie",imkit::ActionVariant::Secondary,{},theme);
    facts.items["browse-cookie"]=Last();facts.items["browse-cookie"].disabled=!can_choose();
    const auto picker=presentation.Interact("Browse cookie file",c::ax::SemanticRole::Button,c::ax::SemanticAction::Press|c::ax::SemanticAction::Focus,!can_choose());
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !can_choose())ImGui::SetTooltip("Requires the local file chooser. An explicit path can also be entered.");
    ImGui::EndDisabled();
    const bool browsing=browse && choose();
    if(picker)presentation.Defer(*picker,[browse,browsing,choose](const auto&){return browse?browsing:choose();});
    toggle("Select a daemon wallet###wallet-selected", "Select a daemon wallet", panel.select_wallet);
    if (panel.select_wallet && !hidden) field("wallet", "Daemon wallet name", panel.wallet, "Exact daemon name; an empty name selects the unnamed wallet.");
    if (panel.select_wallet && hidden) {ImGui::TextUnformatted("Wallet identity hidden");presentation.Annotate("wallet-hidden","Wallet identity","Hidden");}
    field("genesis", "Expected network genesis", panel.genesis, "Compare against an independently known network identity.");
    ImGui::TextUnformatted("Proxy");
    const char* proxies[]{"Direct", "SOCKS5 / proxy DNS", "HTTP CONNECT"};
    for (int i = 0; i < 3; ++i) {
        const bool pressed = ImGui::RadioButton(proxies[i], panel.proxy == i); if (pressed) { panel.proxy = i; sync(); }
        const auto external = presentation.Interact(proxies[i], c::ax::SemanticRole::Radio, c::ax::SemanticAction::Select | c::ax::SemanticAction::Focus, false, panel.proxy == i);
        if (external) presentation.Defer(*external, [&panel, i, sync](const c::SemanticRequest&) { panel.proxy = i; return sync(); });
    }
    if (panel.proxy) { field("proxy-host", "Proxy host", panel.proxy_host); field("proxy-port", "Proxy port", panel.proxy_port); }
    valid = !panel.secure_store && std::filesystem::path(panel.credential.draft()).is_absolute() && ValidServerCandidate(panel.Candidate());
    if (!valid) { const char* message="Enter a valid endpoint, cookie reference and 64-digit genesis. Onion hosts require an explicit proxy."; ImGui::PushStyleColor(ImGuiCol_Text, theme.semantic.error); ImGui::TextWrapped("%s",message); ImGui::PopStyleColor(); presentation.Annotate("validation", "Connection validation",message,c::ax::SemanticRole::Alert); }
    if (service.Busy()) { ImGui::TextUnformatted("Testing connection…"); presentation.Annotate("testing", "Connection test", "Testing connection", c::ax::SemanticRole::Status, false, true, true); }
    if (const auto& tested = service.Tested()) {
        const char* diagnostic = TestMessage(*tested);
        if (tested->error != rpc::Error::None || tested->network_mismatch) { ImGui::PushStyleColor(ImGuiCol_Text, theme.semantic.error); ImGui::TextWrapped("%s", diagnostic); ImGui::PopStyleColor(); presentation.Annotate("test-error", "Connection test error", diagnostic, c::ax::SemanticRole::Alert); }
        const std::string authentication = std::string("Authentication: ") + (tested->authenticated ? "Authenticated" : "Not authenticated");
        const std::string sync_text = std::string("Node: ") + (!tested->identity_verified ? "Unverified network" : tested->initial_download ? "Initial block download" : "Initial download complete") + (tested->pruned ? " / pruned" : "");
        const std::string wallet = std::string("Wallet: ") + WalletAvailabilityName(tested->wallet) + (tested->wallet_scanning ? " / scanning" : "");
        ImGui::TextUnformatted(authentication.c_str()); presentation.Annotate("authentication", "Authentication", authentication, c::ax::SemanticRole::Status);
        ImGui::TextUnformatted(sync_text.c_str()); presentation.Annotate("node-sync", "Node sync", sync_text, c::ax::SemanticRole::Status);
        ImGui::TextUnformatted(wallet.c_str()); presentation.Annotate("wallet-state", "Wallet availability", wallet, c::ax::SemanticRole::Status);
        const auto descriptors = SupportName(tested->Supports("listdescriptors", "private")); ImGui::Text("Public descriptor inventory: %s", descriptors);
        presentation.Annotate("descriptor-capability", "Public descriptor inventory", descriptors, c::ax::SemanticRole::Status);
    }
    }
    if (!panel.notice.empty()) { ImGui::TextUnformatted(panel.notice.c_str()); presentation.Annotate("notice", "Server result", panel.notice, c::ax::SemanticRole::Status); }
    ImGui::PopTextWrapPos(); ImGui::EndChild();
    struct Actions final : c::Commands {
        ServerPanel& panel; ConnectionService& service;
        Actions(ServerPanel& p, ConnectionService& s) : panel(p), service(s) {}
        bool Available(const c::Command& command) const override {
            const auto* action = std::get_if<c::Action>(&command); if (!action || !panel.dialog.open) return false;
            if (action->id == "cancel") return true;
            if (panel.source!=ServerSource::DirectRpc || panel.cookie_dialog || panel.secure_store || service.PrivacyLocked()
                || !std::filesystem::path(panel.credential.draft()).is_absolute())return false;
            if (action->id == "test") return !service.Offline() && !service.Busy() && ValidServerCandidate(panel.Candidate()) && service.Draft() == panel.Candidate();
            return action->id == "apply" && service.CanApply() && service.Draft() == panel.Candidate();
        }
        bool Submit(const c::Command& command) override {
            if (!Available(command)) return false;
            const auto& id = std::get<c::Action>(command).id;
            if (id == "test") { panel.notice.clear(); return service.Test(); }
            if (id == "apply") { if (!service.Apply()) return false; panel.scope_session = service.Session(); panel.notice = "Connection profile applied."; return true; }
            Close(panel, service); return true;
        }
        c::CommandEndpoint CaptureAvailability() const override { return [p = &panel, s = &service](const c::Command& c) { return Actions(*p, *s).Available(c); }; }
        c::CommandEndpoint Capture() override { return [p = &panel, s = &service](const c::Command& c) { return Actions(*p, *s).Request(c); }; }
    } actions(panel, service);
    const char* test_reason=direct ? "Go online, complete the profile and wait for any pending test."
        : "Testing an index requires a shipped HistoryIndexAdapter.";
    const char* apply_reason=direct ? "Test the unchanged replacement profile first."
        : "Applying an index requires a shipped HistoryIndexAdapter.";
    facts.items["test"] = c::ActionButton("test", "Test Connection", c::ActionKind::Secondary, actions, theme, valid && !service.Busy() && !service.Offline(), test_reason, &presentation);
    ImGui::SameLine(); facts.items["apply"] = c::ActionButton("apply", "Apply", c::ActionKind::Primary, actions, theme, direct && service.CanApply(), apply_reason, &presentation);
    ImGui::SameLine(); facts.items["cancel"] = c::ActionButton("cancel", "Cancel", c::ActionKind::CancelDialog, actions, theme, true, "", &presentation);
    if (!panel.dialog.open) { ImGui::CloseCurrentPopup(); panel.close_pending = false; }
    presentation.EndGroup(); imkit::EndDialog(); return facts;
}
}
