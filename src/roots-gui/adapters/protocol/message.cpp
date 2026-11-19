// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/message.h"
#include "adapters/protocol/public_wallet.h"
#include "adapters/rpc/json.h"
#include <algorithm>
#include <memory>
#include <wally_address.h>
#include <wally_core.h>
#include <wally_crypto.h>

namespace roots::gui::protocol {
namespace {
const unsigned char* Data(std::string_view bytes){return reinterpret_cast<const unsigned char*>(bytes.data());}
std::optional<std::array<unsigned char,SHA256_LEN>> Digest(std::string_view message)
{
    std::array<unsigned char,SHA256_LEN> hash{};
    if(message.empty()){
        // Core accepts the CompactSize-framed empty message. The provider's
        // message helper requires a nonempty input.
        constexpr std::string_view empty("\x18" "Bitcoin Signed Message:\n" "\x00",26);
        if(wally_sha256d(Data(empty),empty.size(),hash.data(),hash.size())!=WALLY_OK)return {};
    }else{
        size_t written{};
        if(wally_format_bitcoin_message(Data(message),message.size(),BITCOIN_MESSAGE_FLAG_HASH,
            hash.data(),hash.size(),&written)!=WALLY_OK || written!=hash.size())return {};
    }
    return hash;
}
}
std::optional<MessageNetwork> MessageNetworkForGenesis(std::string_view genesis)
{
    const auto chain=ReceiveChain(genesis);
    if(chain.empty())return {};
    return chain=="main"?MessageNetwork::Main:MessageNetwork::TestFamily;
}
bool ValidMessageBytes(std::string_view message)
{
    return message.size()<=MESSAGE_BYTES && message.find('\0')==std::string_view::npos && rpc::ValidUtf8(message);
}
MessageAddress CheckMessageAddress(MessageNetwork network,std::string_view address)
{
    if(address.empty() || address.size()>MESSAGE_ADDRESS_BYTES
        || !std::ranges::all_of(address,[](unsigned char c){return c>=33 && c<=126;})
        || (network!=MessageNetwork::Main && network!=MessageNetwork::TestFamily))return {};
    std::array<unsigned char,25> raw{};
    size_t written{};
    if(wally_base58_n_to_bytes(address.data(),address.size(),BASE58_FLAG_CHECKSUM,raw.data(),raw.size(),&written)==WALLY_OK && written==21){
        const auto version=raw.front();
        if(version!=0 && version!=5 && version!=111 && version!=196)return {};
        const bool main=version==0 || version==5;
        if(main!=(network==MessageNetwork::Main))return {MessageCheck::WrongNetwork};
        if(version==5 || version==196)return {MessageCheck::UnsupportedAddress};
        MessageAddress result{MessageCheck::Valid};
        std::copy_n(raw.begin()+1,result.key_hash.size(),result.key_hash.begin());
        return result;
    }
    for(const std::string_view hrp:{"bc","tb","bcrt"}){
        std::array<unsigned char,42> script{};
        if(wally_addr_segwit_n_to_bytes(address.data(),address.size(),hrp.data(),hrp.size(),0,script.data(),script.size(),&written)!=WALLY_OK)continue;
        const bool main=hrp=="bc";
        return {main==(network==MessageNetwork::Main)?MessageCheck::UnsupportedAddress:MessageCheck::WrongNetwork};
    }
    return {};
}
bool MessageKeyMatches(const MessageKeyHash& target,std::span<const unsigned char> key)
{
    MessageKeyHash actual{};
    return wally_ec_public_key_strict_verify(key.data(),key.size())==WALLY_OK
        && wally_hash160(key.data(),key.size(),actual.data(),actual.size())==WALLY_OK && target==actual;
}
MessageProof VerifyMessage(MessageFormat format,MessageNetwork network,std::string_view address,std::string_view message,std::string_view signature)
{
    if(format!=MessageFormat::Legacy)return {MessageCheck::UnsupportedFormat,{}};
    const auto recipient=CheckMessageAddress(network,address);
    if(recipient.check!=MessageCheck::Valid)return {recipient.check,{}};
    if(!ValidMessageBytes(message))return {MessageCheck::InvalidMessage,{}};
    constexpr size_t compact_len=EC_SIGNATURE_RECOVERABLE_LEN;
    constexpr size_t base64_len=(compact_len+2)/3*4;
    std::array<unsigned char,base64_len/4*3> decoded{};
    size_t written{};
    if(signature.size()!=base64_len || wally_base64_n_to_bytes(signature.data(),signature.size(),0,
        decoded.data(),decoded.size(),&written)!=WALLY_OK || written!=compact_len)return {MessageCheck::MalformedSignature,{}};
    char* encoded{};
    const int encoding=wally_base64_from_bytes(decoded.data(),compact_len,0,&encoded);
    const std::unique_ptr<char,decltype(&wally_free_string)> canonical(encoded,wally_free_string);
    if(encoding!=WALLY_OK || !canonical)return {MessageCheck::ProviderError,{}};
    if(signature!=canonical.get())return {MessageCheck::MalformedSignature,{}};
    if(decoded[0]<27 || decoded[0]>34)return {MessageCheck::UnsupportedHeader,{}};
    const auto hash=Digest(message);
    if(!hash)return {MessageCheck::ProviderError,{}};
    std::array<unsigned char,EC_PUBLIC_KEY_LEN> compressed{};
    if(wally_ec_sig_to_public_key(hash->data(),hash->size(),decoded.data(),compact_len,compressed.data(),compressed.size())!=WALLY_OK)
        return {MessageCheck::Mismatch,{}};
    std::array<unsigned char,EC_PUBLIC_KEY_UNCOMPRESSED_LEN> expanded{};
    std::span<const unsigned char> key=compressed;
    if(decoded[0]<31){
        if(wally_ec_public_key_decompress(compressed.data(),compressed.size(),expanded.data(),expanded.size())!=WALLY_OK)
            return {MessageCheck::ProviderError,{}};
        key=expanded;
    }
    if(!MessageKeyMatches(recipient.key_hash,key))return {MessageCheck::Mismatch,{}};
    return {MessageCheck::Valid,{key.begin(),key.end()}};
}
const char* MessageCheckText(MessageCheck check)
{
    switch(check){
    case MessageCheck::Valid:return "Valid legacy signature for this P2PKH address and exact message.";
    case MessageCheck::UnsupportedFormat:return "BIP322 requires a dedicated message proof implementation.";
    case MessageCheck::InvalidAddress:return "Enter a valid Bitcoin address.";
    case MessageCheck::WrongNetwork:return "This address belongs to a different network family.";
    case MessageCheck::UnsupportedAddress:return "Legacy signing requires a P2PKH address. Segwit, Taproot and P2SH require another message scheme.";
    case MessageCheck::InvalidMessage:return "Use valid UTF-8 without NUL bytes, up to 4,096 bytes.";
    case MessageCheck::MalformedSignature:return "Enter a canonical Base64 compact signature.";
    case MessageCheck::UnsupportedHeader:return "The compact header is outside the legacy P2PKH range 27–34.";
    case MessageCheck::Mismatch:return "The signature does not match this address and exact message.";
    case MessageCheck::ProviderError:return "The local message verification provider failed.";
    }
    return "Message verification unavailable.";
}
}
