// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/download_format.h"
#include "services/download_verification.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace roots::gui;
using I = DownloadIntegrity;
using S = DownloadSignature;
using T = DownloadTrust;
using E = DownloadIssue;
namespace fs = std::filesystem;

namespace {
unsigned checks{};
void Check(bool ok, const char* reason)
{
    if (!ok) throw std::runtime_error(reason);
    ++checks;
}

void Put(const fs::path& path, std::string_view data)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(data.data(), data.size());
    if (!stream) throw std::runtime_error("fixture file write failed");
}

std::string Get(const fs::path& path)
{
    std::ifstream data(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(data), {}};
}

void Formats()
{
    const std::string word(64, 'a');
    const auto one = word + "  release.tar.gz\n";
    Check(protocol::FindManifestEntry(one, "release.tar.gz").hash == std::string(64, 'A'), "exact flat manifest entry");
    Check(protocol::FindManifestEntry(word + " *release.tar.gz\r\n", "release.tar.gz").hash.size() == 64, "binary GNU marker");
    Check(protocol::FindManifestEntry(one, "Release.tar.gz").outcome == I::MissingEntry, "exact case-sensitive filename");
    Check(protocol::FindManifestEntry(one + one, "release.tar.gz").outcome == I::AmbiguousEntry, "duplicate entry rejected");
    Check(protocol::FindManifestEntry(word + "  ../release.tar.gz\n", "release.tar.gz").outcome == I::UnsupportedManifest, "traversal refused");
    Check(protocol::FindManifestEntry(word + "  /release.tar.gz\n", "release.tar.gz").outcome == I::UnsupportedManifest, "absolute entry refused");
    Check(protocol::FindManifestEntry(word + "  sub/release.tar.gz\n", "release.tar.gz").outcome == I::UnsupportedManifest, "directory entry refused");
    Check(protocol::FindManifestEntry(word + ' ' + "release.tar.gz\n", "release.tar.gz").outcome == I::UnsupportedManifest, "invalid GNU marker");
    Check(!protocol::PublicOpenPgpKey("\x95\1\4"), "secret primary packet refused");
    Check(!protocol::DetachedOpenPgpSignature("-----BEGIN PGP SIGNED MESSAGE-----\n"), "cleartext signature refused");
    Check(!DownloadFingerprint("1130abcd") && DownloadFingerprint(std::string(40, 'a')) == std::string(40, 'A'), "full fingerprint only");
    const auto fingerprint = std::string(40, 'A');
    const std::string valid = "[GNUPG:] VALIDSIG " + fingerprint + " 2020-01-01 1577836800 0 4 0 22 8 00 " + fingerprint + "\n";
    DownloadResult result;
    protocol::InterpretOpenPgpStatus(valid, 0, fingerprint, result);
    Check(result.signature == S::VerifierError && result.trust == T::Unestablished, "exit zero cannot replace signature status");
    protocol::InterpretOpenPgpStatus("[GNUPG:] NEWSIG\n[GNUPG:] REVKEYSIG key\n" + valid, 0, fingerprint, result);
    Check(result.signature == S::Valid && result.trust == T::RevokedKey, "revoked condition stays distinct from validity");
    result = {};
    protocol::InterpretOpenPgpStatus("[GNUPG:] NEWSIG\n[GNUPG:] EXPSIG key\n" + valid, 0, fingerprint, result);
    Check(result.signature == S::Valid && result.trust == T::ExpiredSignature, "expired signature stays distinct from key expiry");
}

template<typename Test>
void Await(Test ready)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(12);
    while (!ready()) {
        if (std::chrono::steady_clock::now() >= until) throw std::runtime_error("download fixture deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

DownloadResult CheckFiles(DownloadVerifier& service, DownloadRequest request)
{
    Check(service.Start(request), "explicit verify request starts");
    std::optional<DownloadResult> result;
    Await([&] { result = service.Poll(&request); return !service.Busy(); });
    Check(result.has_value() && !result->Changed(), "current verification settles once");
    return std::move(*result);
}

void Verify(const fs::path& root, std::string current, std::string expired)
{
    Check(DownloadVerifierConfigured(), "build-pinned verifier configured");
    const auto artifact = root / "fixture-release.bin", manifest = root / "SHA256SUMS";
    DownloadRequest request{1, {{artifact.string(), manifest.string(), (root / "current.asc").string(),
                               (root / "current.key").string()}, current}};
    const auto good_artifact = Get(artifact), good_manifest = Get(manifest);
    DownloadVerifier service;

    auto result = CheckFiles(service, request);
    Check(result.integrity == I::Match && result.signature == S::Valid && result.trust == T::ExplicitFingerprint
          && result.primary_fingerprint == current && result.artifact_hash == result.manifest_hash, "independent integrity, signature and trust all match");
    auto binary = request;
    binary.selections.files[2] = (root / "current.sig").string();
    binary.selections.files[3] = (root / "current.gpg").string();
    auto binary_result = CheckFiles(service, binary);
    Check(binary_result.integrity == I::Match && binary_result.signature == S::Valid && binary_result.trust == T::ExplicitFingerprint,
          "accepted binary signature and public key");
    auto no_trust = request;
    no_trust.selections.expected_fingerprint.clear();
    auto untrusted = CheckFiles(service, no_trust);
    Check(untrusted.integrity == I::Match && untrusted.signature == S::Valid && untrusted.trust == T::Unestablished,
          "correct hash and signature cannot infer trust from the selected key");
    auto mismatch = request;
    mismatch.selections.expected_fingerprint = expired;
    Check(CheckFiles(service, mismatch).trust == T::FingerprintMismatch, "independent fingerprint mismatch");

    Put(artifact, good_artifact + "tampered");
    Check(result.Changed(), "completed result retires when input bytes change in place");
    auto tampered = CheckFiles(service, request);
    Check(tampered.integrity == I::Mismatch && tampered.signature == S::Valid && tampered.trust == T::ExplicitFingerprint,
          "valid trusted signature does not conceal a mismatched artifact");
    Put(artifact, good_artifact);
    Put(manifest, good_manifest + std::string(64, 'a') + "  other.bin\n");
    auto substituted = CheckFiles(service, request);
    Check(substituted.integrity == I::Match && substituted.signature == S::Invalid && substituted.trust == T::Unestablished,
          "substituted manifest with matching hash fails signature");
    Put(manifest, good_manifest);

    auto wrong = request;
    wrong.selections.files[3] = (root / "expired.gpg").string();
    auto unknown = CheckFiles(service, wrong);
    Check(unknown.integrity == I::Match && unknown.signature == S::UnknownSigner && unknown.primary_fingerprint.empty()
          && unknown.trust == T::Unestablished, "wrong selected key does not identify or trust an unknown signer");

    auto old = request;
    old.selections = {{artifact.string(), manifest.string(), (root / "expired.asc").string(),
                       (root / "expired.key").string()}, expired};
    auto dated = CheckFiles(service, old);
    Check(dated.integrity == I::Match && dated.signature == S::Valid && dated.trust == T::ExpiredKey
          && dated.primary_fingerprint == expired, "expired signer has valid signature without current trust");

    const auto malformed = root / "bad.sig";
    Put(malformed, "not a detached OpenPGP signature");
    auto unsupported = request;
    unsupported.selections.files[2] = malformed.string();
    Check(CheckFiles(service, unsupported).signature == S::Unsupported, "unsupported format");
    Put(malformed, Get(root / "current.sig") + Get(root / "current.sig"));
    Check(CheckFiles(service, unsupported).signature == S::Unsupported, "multi-signature stream refused");
    Put(malformed, Get(root / "current.asc") + Get(root / "current.asc"));
    Check(CheckFiles(service, unsupported).signature == S::Unsupported, "combined armor refused");
    Put(manifest, good_manifest + good_manifest);
    auto ambiguous = CheckFiles(service, request);
    Check(ambiguous.integrity == I::AmbiguousEntry && ambiguous.signature == S::Invalid, "ambiguous manifest stays distinct");
    Put(manifest, std::string(1024 * 1024 + 1, 'x'));
    Check(CheckFiles(service, request).issue == E::Limit, "oversized manifest refused");
    Put(manifest, good_manifest);

    fs::create_symlink(artifact, root / "link.bin");
    auto link = request;
    link.selections.files[0] = (root / "link.bin").string();
    Check(CheckFiles(service, link).issue == E::Unreadable, "artifact symlink refused");
    auto before_rename = CheckFiles(service, request);
    Put(root / "replacement", good_artifact);
    fs::rename(root / "replacement", artifact);
    Check(before_rename.Changed(), "identical bytes with a replacement inode retire prior result");

    const auto pseudo_manifest = root / "pseudo-sha256";
    Put(pseudo_manifest, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855  version\n");
    auto pseudo = request;
    pseudo.selections.files[0] = "/proc/version";
    pseudo.selections.files[1] = pseudo_manifest.string();
    const auto pseudo_result = CheckFiles(service, pseudo);
    Check((pseudo_result.issue == E::Changed || pseudo_result.issue == E::Unreadable) && pseudo_result.integrity == I::Unchecked,
          "zero stat size cannot authenticate a pseudo-file with readable contents");

    fs::resize_file(artifact, 512 * 1024 * 1024);
    Check(service.Start(request), "streaming read begins");
    Await([&] { return service.Stage() == DownloadStage::HashArtifact; });
    Put(root / "replacement", good_manifest);
    fs::rename(root / "replacement", manifest);
    std::optional<DownloadResult> raced;
    Await([&] { raced = service.Poll(&request); return !service.Busy(); });
    Check(raced && raced->issue == E::Changed && raced->integrity == I::Unchecked && raced->signature == S::Unchecked,
          "manifest replacement during streaming suppresses both prior outcomes");

    Check(service.Start(request), "begin cancellable stream");
    Await([&] { return service.Stage() == DownloadStage::HashArtifact; });
    service.Cancel();
    bool published{};
    Await([&] { published |= service.Poll(nullptr).has_value(); return !service.Busy(); });
    Check(!published, "cancelled stream publishes no proof");
    Check(service.Start(request), "start after cancellation settles");
    auto later = request;
    ++later.revision;
    Await([&] { published |= service.Poll(&later).has_value(); return !service.Busy(); });
    Check(!published, "stale request never publishes");
    Put(artifact, good_artifact);
    Check(CheckFiles(service, request).signature == S::Valid, "fresh explicit Verify works after retirement");
}
}

int main(int argc, char** argv)
{
    try {
        if (argc == 1) Formats();
        else if (argc == 4) Verify(argv[1], argv[2], argv[3]);
        else return 2;
        std::cout << "{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":" << checks << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
