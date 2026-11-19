// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/message_signing.h"
#include "app/history_workspace.h"
#include "adapters/protocol/public_wallet.h"
#include <algorithm>
#include <charconv>

namespace roots::gui::app {
namespace {
using S=MessageSignStatus;
using J=rpc::Json;
int64_t UnixTime()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
const std::string* String(const J* value)
{
    return value && value->kind==J::Kind::String?&value->text:nullptr;
}
std::optional<bool> Boolean(const J* value)
{
    if(!value || value->kind!=J::Kind::Boolean)return {};
    return value->text=="true";
}
std::optional<int64_t> Integer(const J* value)
{
    if(!value || value->kind!=J::Kind::Number)return {};
    int64_t number{};
    const auto& text=value->text;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || number<0)return {};
    return number;
}
std::string Hex(std::span<const unsigned char> bytes)
{
    constexpr std::string_view digits="0123456789abcdef";
    std::string result;
    result.reserve(bytes.size()*2);
    for(auto byte:bytes){result+=digits[byte>>4];result+=digits[byte&15];}
    return result;
}
std::vector<unsigned char> PublicKey(std::string_view hex)
{
    if(hex.size()!=66 && hex.size()!=130)return {};
    std::vector<unsigned char> result;
    result.reserve(hex.size()/2);
    for(size_t i=0;i<hex.size();i+=2){
        unsigned char byte{};
        const auto parsed=std::from_chars(hex.data()+i,hex.data()+i+2,byte,16);
        if(parsed.ec!=std::errc{} || parsed.ptr!=hex.data()+i+2)return {};
        result.push_back(byte);
    }
    return result;
}
S Failure(const rpc::Reply& reply)
{
    if(reply.rpc_code==-13)return S::Locked;
    if(reply.rpc_code==-18 || reply.rpc_code==-19)return S::NoWallet;
    if(reply.rpc_code==-4)return S::NotOwned;
    if(reply.mutation_uncertain)return S::Uncertain;
    if(reply.error==rpc::Error::Cancelled)return S::Cancelled;
    return S::Failed;
}
MessageSignResult Sign(rpc::Transport& transport,const MessageSignRequest& request,std::stop_token stop)
{
    const auto& profile=request.owner.profile;
    const auto address=protocol::CheckMessageAddress(request.network,request.address);
    if(address.check!=protocol::MessageCheck::Valid || !profile.wallet)return {S::InvalidInput,{}};
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(12);
    uint64_t id{};
    const auto call=[&](const char* method,std::string params,bool wallet,rpc::Effect effect=rpc::Effect::ReadOnly){
        rpc::Reply failure;
        if(stop.stop_requested()){failure.error=rpc::Error::Cancelled;return failure;}
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(until-std::chrono::steady_clock::now());
        if(remaining.count()<=0){failure.error=rpc::Error::Timeout;return failure;}
        rpc::Limits limits;
        limits.total_timeout=remaining;
        limits.connect_timeout=std::min(remaining,std::chrono::milliseconds(3000));
        limits.request_bytes=32*1024;limits.response_bytes=32*1024;limits.json_depth=16;limits.json_nodes=1024;
        return transport.Call(profile.connection,{++id,method,std::move(params),wallet?profile.wallet:std::nullopt,effect},stop,limits);
    };
    const auto genesis=call("getblockhash","[0]",false);
    if(genesis.error!=rpc::Error::None || !genesis.result)return {Failure(genesis),{}};
    const auto* chain=String(&*genesis.result);
    if(!chain || *chain!=profile.expected_genesis)return {S::Identity,{}};
    const auto wallet=call("getwalletinfo","[]",true);
    if(wallet.error!=rpc::Error::None || !wallet.result)return {Failure(wallet),{}};
    const auto* name=String(wallet.result->Find("walletname"));
    const auto private_keys=Boolean(wallet.result->Find("private_keys_enabled"));
    const auto external=Boolean(wallet.result->Find("external_signer"));
    if(!name || *name!=*profile.wallet || !private_keys || !external)return {S::Identity,{}};
    if(!*private_keys || *external)return {S::WatchOnly,{}};
    if(const auto* relock=wallet.result->Find("unlocked_until")){
        const auto time=Integer(relock);
        if(!time)return {S::Identity,{}};
        if(*time<=UnixTime())return {S::Locked,{}};
    }
    const auto address_arg="\"address\":"+rpc::Quote(request.address);
    const auto ownership=call("getaddressinfo","{"+address_arg+"}",true);
    if(ownership.error!=rpc::Error::None || !ownership.result)return {Failure(ownership),{}};
    const auto* echoed=String(ownership.result->Find("address"));
    const auto* script=String(ownership.result->Find("scriptPubKey"));
    const auto mine=Boolean(ownership.result->Find("ismine"));
    const auto witness=Boolean(ownership.result->Find("iswitness"));
    if(!echoed || *echoed!=request.address || !script || *script!="76a914"+Hex(address.key_hash)+"88ac" || !mine || witness!=false)
        return {S::Identity,{}};
    if(const auto* watch=ownership.result->Find("iswatchonly"))if(Boolean(watch)!=false)return {S::WatchOnly,{}};
    const auto* public_key=String(ownership.result->Find("pubkey"));
    if(!*mine || !public_key)return {S::NotOwned,{}};
    const auto exact_key=PublicKey(*public_key);
    if(!protocol::MessageKeyMatches(address.key_hash,exact_key))return {S::Identity,{}};
    const auto signed_reply=call("signmessage","{"+address_arg+",\"message\":"+rpc::Quote(request.message)+"}",true,rpc::Effect::Mutation);
    if(signed_reply.error!=rpc::Error::None || !signed_reply.result)return {Failure(signed_reply),{}};
    const auto* signature=String(&*signed_reply.result);
    if(!signature)return {S::BadReply,{}};
    const auto proof=protocol::VerifyMessage(request.format,request.network,request.address,request.message,*signature);
    if(proof.check!=protocol::MessageCheck::Valid || proof.public_key!=exact_key)return {S::BadReply,{}};
    return {S::Signed,*signature};
}
}

std::optional<MessageWalletScope> CurrentMessageWallet(const Document* document,const ConnectionService* service,LockEpoch epoch)
{
    if(!document || !service || service->PrivacyLocked() || service->Offline() || !service->Active()
        || !document->binding || document->kind!=DocumentKind::Wallet || document->observation==ObservationState::Locked)return {};
    const auto& active=*service->Active();
    const auto actual=VerifiedWalletBinding(active);
    if(!actual || *actual!=*document->binding)return {};
    return MessageWalletScope{document->id,service->Session(),service->Revision(),epoch,*actual,active.candidate};
}

MessageSigning::~MessageSigning()
{
    Cancel();
    if(worker.joinable())worker.join();
}
MessageSignStatus MessageSigning::Status(const MessageWalletScope& scope,protocol::MessageNetwork network) const
{
    if(std::this_thread::get_id()!=owner || !scope.document || !scope.session || connection.PrivacyLocked()
        || connection.Offline() || !connection.Active() || connection.Session()!=scope.session || connection.Revision()!=scope.profile_revision)
        return S::NoWallet;
    const auto& active=*connection.Active();
    if(active.candidate!=scope.profile || VerifiedWalletBinding(active)!=scope.binding || active.genesis!=scope.profile.expected_genesis)return S::NoWallet;
    if(protocol::MessageNetworkForGenesis(active.genesis)!=network || protocol::ReceiveChain(active.genesis)!=active.chain)return S::WrongNetwork;
    if(active.private_keys!=true || active.external_signer!=false)return S::WatchOnly;
    if(active.unlocked_until && *active.unlocked_until<=UnixTime())return S::Locked;
    if(!active.SupportsString("signmessage","address") || !active.SupportsString("signmessage","message")
        || !active.SupportsString("getaddressinfo","address"))return S::Unsupported;
    return S::Ready;
}
MessageSignStatus MessageSigning::Start(MessageSignRequest request)
{
    if(std::this_thread::get_id()!=owner)return S::NoWallet;
    if(worker.joinable())return S::Busy;
    if(request.format!=protocol::MessageFormat::Legacy)return S::Unsupported;
    const auto available=Status(request.owner,request.network);
    if(available!=S::Ready)return available;
    if(protocol::CheckMessageAddress(request.network,request.address).check!=protocol::MessageCheck::Valid
        || !protocol::ValidMessageBytes(request.message))return S::InvalidInput;
    running=request;
    try {
        worker=std::jthread([this,request=std::move(request)](std::stop_token stop){
            MessageSignResult result{S::Failed,{}};
            try{result=Sign(transport,request,stop);}catch(...){}
            std::lock_guard lock(mutex);
            completion=std::move(result);
        });
    }catch(...){running.reset();return S::Failed;}
    return S::Ready;
}
std::optional<MessageSignResult> MessageSigning::Poll(const MessageSignRequest* current)
{
    if(std::this_thread::get_id()!=owner || !worker.joinable())return {};
    if(running && (!current || *running!=*current || Status(current->owner,current->network)!=S::Ready))Cancel();
    std::optional<MessageSignResult> result;
    {
        std::lock_guard lock(mutex);
        if(!completion)return {};
        result=std::exchange(completion,std::nullopt);
    }
    worker.join();
    const bool accept=running.has_value();
    running.reset();
    return accept?result:std::nullopt;
}
void MessageSigning::Cancel()
{
    if(std::this_thread::get_id()!=owner)return;
    if(worker.joinable())worker.request_stop();
    running.reset();
}
const char* MessageSignText(MessageSignStatus status)
{
    switch(status){
    case S::Ready:return "Signing request in progress…";
    case S::Signed:return "Signed and verified locally for the exact address and message.";
    case S::NoWallet:return "Select the wallet tab for an online, tested and applied Bitcoin RPC profile.";
    case S::Unsupported:return "The applied wallet needs proven address and legacy signmessage RPC support.";
    case S::WatchOnly:return "A watch-only or external-signer wallet has no supported legacy message signer here.";
    case S::Locked:return "The daemon wallet is locked. Message signing cannot unlock it.";
    case S::WrongNetwork:return "The selected message network must match the verified wallet profile.";
    case S::InvalidInput:return "Check the message bytes, format and P2PKH address.";
    case S::NotOwned:return "This wallet did not prove ownership of the address's public key.";
    case S::Identity:return "The daemon network, wallet or address proof did not match.";
    case S::Failed:return "The message signing request failed.";
    case S::Uncertain:return "The daemon may have received the request. No signature was accepted.";
    case S::BadReply:return "The daemon did not return a valid signature for the requested address and message.";
    case S::Cancelled:return "The message request was cancelled.";
    case S::Busy:return "Wait for the previous message request to settle.";
    }
    return "Message signing unavailable.";
}
}
