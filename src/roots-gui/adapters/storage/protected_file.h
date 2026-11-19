// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "adapters/storage/metadata.h"
#include <span>
namespace roots::gui::storage {
// Public metadata becomes sensitive while the local store is protected.
struct CleansedBytes {
    std::vector<unsigned char> bytes;
    ~CleansedBytes();
    CleansedBytes()=default;
    CleansedBytes(CleansedBytes&&)=default;
    CleansedBytes& operator=(CleansedBytes&&) noexcept;
    CleansedBytes(const CleansedBytes&)=delete;
};
class FileGuard {
    int fd{-1};
public:
    static Result<std::unique_ptr<FileGuard>> Open(const std::filesystem::path&,bool exclusive);
    ~FileGuard();
    bool Exclusive();
    void Shared();
};
// Uses libsodium's Argon2id13 and secretstream API unchanged. This class owns
// only bounded framing and atomic private-file replacement, never a cipher.
class ProtectedFile {
    std::filesystem::path path;
    unsigned char* key{};
    std::vector<unsigned char> prefix,accepted;
    bool existed{};
    ProtectedFile()=default;
public:
    static constexpr size_t MaxBytes=32*1024*1024;
    static Result<std::unique_ptr<ProtectedFile>> Open(const std::filesystem::path&,std::string_view password,
                                                     size_t max_plain = MaxBytes,bool existing_only = false);
    ~ProtectedFile();
    Result<CleansedBytes> Read();
    Result<Done> Save(std::span<const unsigned char>);
    Result<Done> SaveNew(std::span<const unsigned char>); // atomic failure if a file already exists
    // Caller owns the exclusive metadata guard. Authenticate before inspecting
    // a surviving plaintext copy; never discard different bytes silently.
    Result<Done> FinishConversion(const std::filesystem::path& plain_path,ConversionResolution);
    Result<std::vector<unsigned char>> Seal(std::span<const unsigned char>);
    Result<CleansedBytes> Unseal(std::span<const unsigned char>);
};
}
