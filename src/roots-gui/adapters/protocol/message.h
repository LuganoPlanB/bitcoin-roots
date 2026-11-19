// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace roots::gui::protocol {
inline constexpr size_t MESSAGE_BYTES=4096, MESSAGE_ADDRESS_BYTES=128, MESSAGE_SIGNATURE_BYTES=128;
enum class MessageFormat { Legacy, Bip322 };
enum class MessageNetwork { Main, TestFamily };
enum class MessageCheck { Valid, UnsupportedFormat, InvalidAddress, WrongNetwork, UnsupportedAddress,
    InvalidMessage, MalformedSignature, UnsupportedHeader, Mismatch, ProviderError };
using MessageKeyHash=std::array<unsigned char,20>;
struct MessageAddress {
    MessageCheck check{MessageCheck::InvalidAddress};
    MessageKeyHash key_hash{};
};
struct MessageProof {
    MessageCheck check;
    std::vector<unsigned char> public_key;
};
const char* MessageCheckText(MessageCheck);
std::optional<MessageNetwork> MessageNetworkForGenesis(std::string_view);
bool ValidMessageBytes(std::string_view);
MessageAddress CheckMessageAddress(MessageNetwork,std::string_view);
bool MessageKeyMatches(const MessageKeyHash&,std::span<const unsigned char>);
MessageProof VerifyMessage(MessageFormat,MessageNetwork,std::string_view address,std::string_view message,std::string_view signature);
}
