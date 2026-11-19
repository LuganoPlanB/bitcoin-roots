// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "adapters/protocol/transaction.h"
#include <mutex>
#include <thread>

namespace roots::gui {
struct LocalDocumentContext {
    uint64_t generation{}, document{};
    DraftRevision revision;
    LockEpoch epoch;
    bool operator==(const LocalDocumentContext&) const = default;
};
struct LocalDocumentReply {
    protocol::DocumentResult parsed;
    std::string file_error;
    std::optional<std::string> qr_text;
};

// One owner and one bounded, cancellable local read/parse. No server port.
class TransactionDocuments {
public:
    ~TransactionDocuments();
    bool ReadFile(LocalDocumentContext, std::string path, protocol::DisplayNetwork);
    bool ReadText(LocalDocumentContext, std::string text, protocol::DisplayNetwork);
    bool ReadQrImage(LocalDocumentContext,std::string path);
    bool ReadBinary(LocalDocumentContext,std::string payload,protocol::DisplayNetwork);
    bool Busy() const {return worker.joinable();}
    void Cancel();
    std::optional<LocalDocumentReply> Poll(const std::optional<LocalDocumentContext>&);
private:
    enum class Source { Text, File, Binary, QrImage };
    bool Start(LocalDocumentContext,std::string,protocol::DisplayNetwork,Source);
    static LocalDocumentReply Read(std::string,protocol::DisplayNetwork,Source,std::stop_token);
    const std::thread::id owner{std::this_thread::get_id()};
    std::optional<LocalDocumentContext> active;
    bool retired{};
    std::mutex mutex;
    std::optional<LocalDocumentReply> completed;
    std::jthread worker;
};
bool ValidLocalDocumentPath(std::string_view);
struct PublicFileRead {
    std::string bytes,error;
    bool limited{},cancelled{};
};
// CSV requires a symlink-free path from the root. Legacy document routes keep
// their established parent-directory handling and always refuse a symlink leaf.
PublicFileRead ReadPublicFile(std::string_view,size_t,std::stop_token,bool strict_directories);
std::string SavePublicFile(std::string_view,std::string_view,size_t,bool strict_directories);
std::string SaveOriginalDocument(std::string_view path, const protocol::TransactionDocument&); // empty means durable success
}
