// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "adapters/protocol/message.h"
#include "app/document_registry.h"
#include "services/capabilities.h"
#include <mutex>
#include <thread>

namespace roots::gui::app {
struct MessageWalletScope {
    uint64_t document{},session{},profile_revision{};
    LockEpoch epoch;
    WalletBinding binding;
    ServerCandidate profile;
    bool operator==(const MessageWalletScope&) const=default;
};
std::optional<MessageWalletScope> CurrentMessageWallet(const Document*,const ConnectionService*,LockEpoch);
struct MessageSignRequest {
    MessageWalletScope owner;
    uint64_t edit_revision{};
    protocol::MessageFormat format;
    protocol::MessageNetwork network;
    std::string address,message;
    bool operator==(const MessageSignRequest&) const=default;
};
enum class MessageSignStatus { Ready, Signed, NoWallet, Unsupported, WatchOnly, Locked, WrongNetwork, InvalidInput, NotOwned,
    Identity, Failed, Uncertain, BadReply, Cancelled, Busy };
const char* MessageSignText(MessageSignStatus);
struct MessageSignResult {
    MessageSignStatus status;
    std::string signature;
};

// Owns one explicitly requested message signature independently of PSBT and
// transaction signing. Request and reply bytes remain process-local and ephemeral.
class MessageSigning {
public:
    MessageSigning(rpc::Transport& transport,ConnectionService& connection):transport(transport),connection(connection){}
    ~MessageSigning();
    bool Busy() const{return worker.joinable();}
    MessageSignStatus Status(const MessageWalletScope&,protocol::MessageNetwork) const;
    MessageSignStatus Start(MessageSignRequest);
    std::optional<MessageSignResult> Poll(const MessageSignRequest* current);
    void Cancel();
private:
    const std::thread::id owner{std::this_thread::get_id()};
    rpc::Transport& transport;
    ConnectionService& connection;
    std::optional<MessageSignRequest> running;
    std::mutex mutex;
    std::optional<MessageSignResult> completion;
    std::jthread worker;
};
}
