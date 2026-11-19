// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include <algorithm>

namespace roots::gui::ui::shell {
namespace {
namespace a=app;
namespace c=components;
namespace p=protocol;
constexpr char POPUP[]="Sign / Verify Message###message-utility";
constexpr char QR_REASON[]="Sign by QR requires a message-signing QR protocol and a compatible signer.";
uint64_t ProfileRevision(a::Requests& host)
{
    const auto* connection=host.Connection();
    return connection?connection->Revision():0;
}
std::optional<a::MessageWalletScope> Wallet(const State& s,a::Requests& host)
{
    return a::CurrentMessageWallet(s.documents.Active(),host.Connection(),CurrentToolScope(s,host).epoch);
}
bool Current(const State& s,a::Requests& host)
{
    const auto scope=CurrentToolScope(s,host);
    return s.message.dialog.open && !scope.locked && !scope.hidden && s.message.scope==scope
        && s.message.profile_revision==ProfileRevision(host) && s.message.wallet==Wallet(s,host);
}
a::MessageSignRequest ExactRequest(const MessagePanel& panel)
{
    return {*panel.wallet,panel.revision,panel.format,panel.network,panel.address.data(),panel.text.data()};
}
void Invalidate(State& s,a::Requests& host)
{
    auto& panel=s.message;
    const bool retired=panel.pending;
    if(retired)if(auto* signer=host.Messages())signer->Cancel();
    panel.pending=false;panel.status_ok=false;++panel.revision;
    panel.status=retired?"Request retired after an edit. The daemon may already have received it.":"";
}
void Text(State& s,const char* id,const char* name,std::string_view line,c::ax::SemanticRole role=c::ax::SemanticRole::Text)
{
    ImGui::TextWrapped("%.*s",int(line.size()),line.data());
    s.presentation.Annotate(id,name,line,role);
}
c::ItemFacts Last(bool pressed,bool disabled)
{
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    return {{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),disabled,false,pressed,ImGui::IsItemVisible()};
}
void Button(State& s,Facts& f,const imkit::Theme& theme,const char* id,const char* label,bool enabled,std::function<bool()> action,
    const char* reason="")
{
    ImGui::PushID(id);ImGui::BeginDisabled(!enabled);
    const bool pressed=theme::ActionButton(label,imkit::ActionVariant::Secondary,{},theme);
    f.message[id]=Last(pressed,!enabled);
    using enum c::ax::SemanticAction;
    const auto request=s.presentation.Interact(label,c::ax::SemanticRole::Button,Press|Focus,!enabled,false,{},reason);
    if(!enabled && *reason && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",reason);
    ImGui::EndDisabled();ImGui::PopID();
    const bool accepted=pressed && enabled && action();
    if(request)s.presentation.Defer(*request,[action=std::move(action),pressed,accepted](const auto&){return pressed?accepted:action();});
}
template<size_t Size>
void Field(State& s,a::Requests& host,Facts& f,const char* id,const char* label,std::array<char,Size>& text,
    std::optional<std::string>& injected,bool focus,ImVec2 multiline={})
{
    ImGui::PushID(id);
    ImGui::TextUnformatted(label);
    if(multiline.y==0)ImGui::SameLine();
    if(focus)ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    const bool external=injected.has_value();
    const auto flags=multiline.y>0?ImGuiInputTextFlags_AllowTabInput:ImGuiInputTextFlags_None;
    if(c::TextInput("##message-field",nullptr,text.data(),text.size(),injected,flags,nullptr,true,multiline) && !external)
        Invalidate(s,host);
    f.message[id]=Last(false,false);
    const auto request=s.presentation.Interact(label,c::ax::SemanticRole::TextField,c::ax::SemanticAction::SetValue|c::ax::SemanticAction::Focus);
    ImGui::PopID();
    if(request)s.presentation.Defer(*request,[&s,&host,&text,&injected](const auto& value){
        if(!Current(s,host) || !rpc::ValidUtf8(value.value) || !c::SetExternalBuffer(text,injected,value.value))return false;
        Invalidate(s,host);return true;
    });
}
template<class Value>
void Choice(State& s,a::Requests& host,Facts& f,const char* id,const char* title,Value& current,Value next)
{
    ImGui::PushID(id);
    const bool pressed=ImGui::RadioButton(title,current==next);
    f.message[id]=Last(pressed,false);
    const auto request=s.presentation.Interact(title,c::ax::SemanticRole::Radio,c::ax::SemanticAction::Select|c::ax::SemanticAction::Focus,
        false,current==next);
    ImGui::PopID();
    auto select=[&s,&host,&current,next]{
        if(!Current(s,host))return false;
        if(current!=next){current=next;Invalidate(s,host);}
        return true;
    };
    const bool accepted=pressed && select();
    if(request)s.presentation.Defer(*request,[select,pressed,accepted](const auto&){return pressed?accepted:select();});
}
void Poll(State& s,a::Requests& host)
{
    auto* signer=host.Messages();
    if(!signer)return;
    auto& panel=s.message;
    const auto current=panel.pending && panel.wallet && Current(s,host)?std::optional(ExactRequest(panel)):std::nullopt;
    const auto reply=signer->Poll(current?&*current:nullptr);
    if(!reply){
        if(panel.pending && !signer->Busy()){
            panel.pending=false;panel.status_ok=false;panel.status="The message signing context changed. No signature was accepted."; ++panel.revision;
        }
        return;
    }
    panel.pending=false;panel.status_ok=reply->status==a::MessageSignStatus::Signed;
    panel.status=a::MessageSignText(reply->status);
    if(panel.status_ok)c::SetExternalBuffer(panel.signature,panel.external_signature,reply->signature);
    ++panel.revision;
}
bool StartSign(State& s,a::Requests& host)
{
    auto* signer=host.Messages();
    auto& panel=s.message;
    if(!Current(s,host) || !signer || !panel.wallet || panel.pending)return false;
    const auto result=signer->Start(ExactRequest(panel));
    panel.pending=result==a::MessageSignStatus::Ready;
    panel.status=a::MessageSignText(result);panel.status_ok=false;
    return panel.pending;
}
bool Verify(State& s,a::Requests& host)
{
    auto& panel=s.message;
    if(!Current(s,host) || panel.pending)return false;
    const auto result=p::VerifyMessage(panel.format,panel.network,panel.address.data(),panel.text.data(),panel.signature.data());
    panel.status=p::MessageCheckText(result.check);
    panel.status_ok=result.check==p::MessageCheck::Valid;
    return panel.status_ok;
}
}

bool OpenMessage(State& s,a::Requests& host)
{
    const auto scope=CurrentToolScope(s,host);
    if(scope.hidden || scope.locked || s.message.dialog.open)return false;
    auto& panel=s.message;
    panel.scope=scope;panel.profile_revision=ProfileRevision(host);panel.wallet=Wallet(s,host);
    if(auto* connection=host.Connection();connection && connection->Active())
        panel.network=p::MessageNetworkForGenesis(connection->Active()->genesis).value_or(p::MessageNetwork::Main);
    panel.dialog.open=true;panel.dialog.initialFocus=true;++panel.revision;
    return true;
}
void RetireMessage(State& s,a::Requests& host)
{
    if(auto* signer=host.Messages())signer->Cancel();
    const auto revision=s.message.revision;
    s.message=MessagePanel{};
    s.message.close_pending=true;s.message.revision=revision+1;
}
void DrawMessage(State& s,a::Requests& host,Facts& f,const imkit::Theme& theme,float scale)
{
    auto& panel=s.message;
    if(panel.dialog.open && !Current(s,host))RetireMessage(s,host);
    Poll(s,host);
    if(panel.close_pending){
        if(ImGui::BeginPopupModal(POPUP)){ImGui::CloseCurrentPopup();ImGui::EndPopup();}
        panel.close_pending=false;
    }
    if(!panel.dialog.open)return;
    const auto* vp=ImGui::GetMainViewport();
    ImGui::SetNextWindowSize({std::min(700*scale,vp->WorkSize.x-32*scale),std::min(540*scale,vp->WorkSize.y-32*scale)},ImGuiCond_Always);
    const bool focus=panel.dialog.initialFocus;
    if(!imkit::BeginDialog(POPUP,panel.dialog,{&theme})){if(!panel.dialog.open)RetireMessage(s,host);return;}
    f.message_visible=true;
    s.presentation.DialogOnly();
    const auto position=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
    s.presentation.Group("messages","Sign / Verify Message",c::ax::SemanticRole::Dialog,position,{position.x+size.x,position.y+size.y});
    const float footer=ImGui::GetFrameHeightWithSpacing()+ImGui::GetTextLineHeightWithSpacing();
    ImGui::BeginChild("message-fields",{0,-footer},ImGuiChildFlags_NavFlattened);
    Choice(s,host,f,"legacy","Legacy Bitcoin Signed Message",panel.format,p::MessageFormat::Legacy);
    ImGui::SameLine();
    Choice(s,host,f,"bip322","BIP322",panel.format,p::MessageFormat::Bip322);
    if(ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x-ImGui::GetItemRectMax().x>220*scale)ImGui::SameLine();
    Choice(s,host,f,"main","Mainnet",panel.network,p::MessageNetwork::Main);
    ImGui::SameLine();
    Choice(s,host,f,"test-family","Test-family",panel.network,p::MessageNetwork::TestFamily);
    if(panel.network==p::MessageNetwork::TestFamily)
        Text(s,"test-family-note","Address network","Testnet, signet and regtest share the legacy address prefix.");
    Field(s,host,f,"address","Address",panel.address,panel.external_address,focus);
    Field(s,host,f,"message","Message",panel.text,panel.external_text,false,{-1,72*scale});
    Field(s,host,f,"signature","Base64 signature",panel.signature,panel.external_signature,false);
    const bool legacy=panel.format==p::MessageFormat::Legacy;
    const auto address=p::CheckMessageAddress(panel.network,panel.address.data()).check;
    const auto input_ok=address==p::MessageCheck::Valid && p::ValidMessageBytes(panel.text.data());
    auto available=a::MessageSignStatus::NoWallet;
    if(auto* signer=host.Messages();signer && panel.wallet)available=signer->Status(*panel.wallet,panel.network);
    const bool busy=host.Messages() && host.Messages()->Busy();
    if(!legacy)Text(s,"unsupported-format","Message scheme",p::MessageCheckText(p::MessageCheck::UnsupportedFormat));
    else{
        if(panel.address.front() && address!=p::MessageCheck::Valid)Text(s,"address-format","Address format",p::MessageCheckText(address));
        else Text(s,"legacy-format","Message scheme","Legacy P2PKH · recoverable ECDSA · compact headers 27–34");
        if(available!=a::MessageSignStatus::Ready)Text(s,"signer-dependency","Signing availability",a::MessageSignText(available));
        else Text(s,"sign-transport","Signing scope","Sign sends the address and exact message to the applied daemon wallet.");
    }
    ImGui::EndChild();
    if(!panel.status.empty())Text(s,"message-result","Message result",panel.status,
        panel.status_ok || panel.pending?c::ax::SemanticRole::Status:c::ax::SemanticRole::Alert);
    else ImGui::Dummy({0,ImGui::GetTextLineHeight()});
    Button(s,f,theme,"sign","Sign",legacy && input_ok && !busy && available==a::MessageSignStatus::Ready,[&s,&host]{return StartSign(s,host);});
    ImGui::SameLine();
    Button(s,f,theme,"sign-qr","Sign by QR",false,[]{return false;},QR_REASON);
    ImGui::SameLine();
    Button(s,f,theme,"verify","Verify",legacy && !busy,[&s,&host]{return Verify(s,host);});
    if(panel.pending){
        ImGui::SameLine();
        Button(s,f,theme,"cancel","Cancel request",true,[&s,&host]{
            if(!Current(s,host) || !s.message.pending)return false;
            Invalidate(s,host);s.message.status="Request cancelled. The daemon may already have received it.";return true;
        });
    }
    ImGui::SameLine();
    Button(s,f,theme,"done","Done",true,[&s,&host]{RetireMessage(s,host);return true;});
    if(!panel.dialog.open){ImGui::CloseCurrentPopup();panel.close_pending=false;}
    s.presentation.EndGroup();imkit::EndDialog();
}
}
