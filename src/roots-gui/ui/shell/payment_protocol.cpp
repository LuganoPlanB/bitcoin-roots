// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "services/payment_protocols.h"
#include "ui/theme/theme.h"
#include <algorithm>

namespace roots::gui::ui::shell {
namespace {
constexpr char POPUP[]="Payment protocols###payment-protocol-notice";
using components::ax::SemanticRole;
using components::ax::SemanticAction;

uint64_t ProfileRevision(app::Requests& host)
{
    const auto* connection=host.Connection();
    return connection ? connection->Revision() : 0;
}

bool Current(const State& state,app::Requests& host)
{
    const auto scope=CurrentToolScope(state,host);
    return state.payment_protocol.dialog.open && !scope.locked && state.payment_protocol.scope==scope
        && state.payment_protocol.profile_revision==ProfileRevision(host)
        && !(state.recovery && state.recovery->Pending());
}

void Text(State& state,const char* id,const char* name,const char* message)
{
    ImGui::TextWrapped("%s",message);
    state.presentation.Annotate(id,name,message);
}
}

bool OpenPaymentProtocolNotice(State& state,app::Requests& host)
{
    const auto scope=CurrentToolScope(state,host);
    if (scope.locked || state.payment_protocol.dialog.open || (state.recovery && state.recovery->Pending())) return false;
    state.payment_protocol={.dialog={.open=true,.initialFocus=true},.scope=scope,.profile_revision=ProfileRevision(host)};
    return true;
}

void RetirePaymentProtocolNotice(State& state)
{
    state.payment_protocol={.close_pending=true};
}

void DrawPaymentProtocolNotice(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme,float scale)
{
    auto& notice=state.payment_protocol;
    if (notice.dialog.open && !Current(state,host)) RetirePaymentProtocolNotice(state);
    if (notice.close_pending) {
        if (ImGui::BeginPopupModal(POPUP)) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        notice.close_pending=false;
    }
    if (!notice.dialog.open) return;
    const auto* viewport=ImGui::GetMainViewport();
    const float width=std::clamp(viewport->WorkSize.x-32*scale,1.F,510*scale);
    ImGui::SetNextWindowSizeConstraints({width,0},{width,std::max(1.F,viewport->WorkSize.y-32*scale)});
    const bool focus=notice.dialog.initialFocus;
    if (!imkit::BeginDialog(POPUP,notice.dialog,{&theme})) {
        if (!notice.dialog.open) RetirePaymentProtocolNotice(state);
        return;
    }
    facts.payment_protocol_visible=true;
    state.presentation.DialogOnly();
    const auto pos=ImGui::GetWindowPos();
    const auto size=ImGui::GetWindowSize();
    state.presentation.Group("payment-protocol-unavailable","Payment protocols",SemanticRole::Dialog,pos,{pos.x+size.x,pos.y+size.y});
    const auto& registry=ShippedExtensions();
    constexpr auto title="Payment protocols unavailable";
    imkit::PushTextRole(theme,imkit::TextRole::Heading);
    ImGui::TextUnformatted(title);
    imkit::PopTextRole();
    state.presentation.Annotate("payment-protocol-status",title,{},SemanticRole::Status);
    Text(state,"payjoin","Payjoin",PayjoinStatus(registry).detail);
    Text(state,"contacts","Payment-code contacts",PaymentCodeContactStatus(registry).detail);
    Text(state,"reusable-private","Reusable private addresses",
        "Silent payments require a qualified ReusableReceiveService for the selected public format and network.");
    Text(state,"permission","Disclosure and review",
        "No payment or contact data is shared here. An enabled negotiation would require permission for the exact recipient, service and data, then a fresh transaction review.");
    if (focus) ImGui::SetKeyboardFocusHere();
    const bool pressed=theme::ActionButton("Close",imkit::ActionVariant::Secondary,{},theme);
    const auto external=state.presentation.Interact("Close",SemanticRole::Button,SemanticAction::Press|SemanticAction::Focus);
    const auto close=[&state,&host]{
        if (!Current(state,host)) return false;
        RetirePaymentProtocolNotice(state);
        return true;
    };
    if (pressed && close()) {
        ImGui::CloseCurrentPopup();
        notice.close_pending=false;
    }
    if (external) state.presentation.Defer(*external,[close](const auto&){return close();});
    state.presentation.EndGroup();
    imkit::EndDialog();
}
} // namespace roots::gui::ui::shell
