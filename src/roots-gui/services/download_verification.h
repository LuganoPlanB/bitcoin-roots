// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace roots::gui {

enum class DownloadFile : size_t { Artifact, Manifest, Signature, PublicKey, Count };
enum class DownloadIntegrity { Unchecked, Match, Mismatch, MissingEntry, AmbiguousEntry, UnsupportedManifest };
enum class DownloadSignature { Unchecked, Valid, Invalid, UnknownSigner, Unsupported, VerifierError };
enum class DownloadTrust { Unestablished, ExplicitFingerprint, FingerprintMismatch, ExpiredKey, ExpiredSignature, RevokedKey };
enum class DownloadIssue { None, Unavailable, Unreadable, Limit, Changed, Cancelled };
enum class DownloadStage { Idle, ReadInputs, HashArtifact, VerifyManifest };

struct DownloadSelections {
    std::array<std::string, size_t(DownloadFile::Count)> files;
    std::string expected_fingerprint;
    bool operator==(const DownloadSelections&) const = default;
};

class DownloadSnapshot {
public:
    virtual ~DownloadSnapshot() = default;
    virtual bool Changed() = 0;
};

struct DownloadResult {
    DownloadIntegrity integrity{DownloadIntegrity::Unchecked};
    DownloadSignature signature{DownloadSignature::Unchecked};
    DownloadTrust trust{DownloadTrust::Unestablished};
    DownloadIssue issue{DownloadIssue::None};
    std::string signing_fingerprint, primary_fingerprint, artifact_hash, manifest_hash;
    std::unique_ptr<DownloadSnapshot> files;
    bool Changed() { return files && files->Changed(); }
};

struct DownloadRequest {
    uint64_t revision{};
    DownloadSelections selections;
    bool operator==(const DownloadRequest&) const = default;
};

bool DownloadVerifierConfigured();
std::optional<std::string> DownloadFingerprint(std::string_view);
const char* DownloadText(DownloadIntegrity);
const char* DownloadText(DownloadSignature);
const char* DownloadText(DownloadTrust);
const char* DownloadText(DownloadIssue);
const char* DownloadText(DownloadStage);

// One worker owned by the shell. Replacing a view retires its request, even
// when the verifier is already producing a response. Poll consumes once.
class DownloadVerifier {
public:
    DownloadVerifier() = default;
    ~DownloadVerifier();
    bool Start(DownloadRequest);
    std::optional<DownloadResult> Poll(const DownloadRequest*);
    void Cancel();
    bool Busy() const { return worker.joinable(); }
    DownloadStage Stage() const { return stage.load(std::memory_order_relaxed); }

private:
    const std::thread::id owner = std::this_thread::get_id();
    std::optional<DownloadRequest> active;
    std::mutex mutex;
    std::atomic<DownloadStage> stage{DownloadStage::Idle};
    std::optional<DownloadResult> completed;
    std::jthread worker;
};

}
