// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/download_format.h"

#include <algorithm>
#include <sodium.h>
#include <unordered_set>
#include <vector>

namespace roots::gui {
namespace {

bool HexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

char UpperHex(char c)
{
    return c >= 'a' && c <= 'f' ? c - 'a' + 'A' : c;
}

std::string_view Line(std::string_view& input)
{
    const auto end = input.find('\n');
    auto line = input.substr(0, end);
    input.remove_prefix(end == std::string_view::npos ? input.size() : end + 1);
    if (line.ends_with('\r')) line.remove_suffix(1);
    return line;
}

bool ArtifactName(std::string_view name)
{
    const auto ordinary = [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    };
    return !name.empty() && name.size() <= 255 && ordinary(name.front())
        && std::ranges::all_of(name, [&](char c) { return ordinary(c) || c == '.' || c == '-' || c == '_' || c == '+'; });
}

std::optional<std::string> Decode64(std::string_view text)
{
    std::string result(text.size(), '\0');
    size_t decoded{};
    if (sodium_base642bin(reinterpret_cast<unsigned char*>(result.data()), result.size(), text.data(), text.size(),
                         nullptr, &decoded, nullptr, sodium_base64_VARIANT_ORIGINAL) != 0) return {};
    result.resize(decoded);
    return result;
}

uint32_t ArmorChecksum(std::string_view bytes)
{
    // OpenPGP's CRC-24 transport checksum. GnuPG verifies the actual signature.
    uint32_t crc = 0xb704ce;
    for (unsigned char b : bytes) {
        crc ^= uint32_t(b) << 16;
        for (int bit = 0; bit < 8; ++bit) {
            crc <<= 1;
            if (crc & 0x1000000) crc ^= 0x1864cfb;
        }
    }
    return crc;
}

std::optional<std::string> OpenPgpBytes(std::string_view bytes, std::string_view begin, std::string_view end)
{
    if (bytes.empty()) return {};
    if (static_cast<unsigned char>(bytes.front()) & 0x80) return std::string(bytes);

    if (Line(bytes) != begin) return {};
    bool separator{};
    for (int i = 0; i < 8 && !bytes.empty(); ++i) {
        const auto header = Line(bytes);
        if (header.empty()) { separator = true; break; }
        if (header.size() > 256 || !(header.starts_with("Version: ") || header.starts_with("Comment: "))
            || !std::ranges::all_of(header, [](char c) { return c >= ' ' && c <= '~'; })) return {};
    }
    if (!separator) return {};

    std::string encoded;
    std::string_view checksum;
    while (!bytes.empty()) {
        const auto line = Line(bytes);
        if (line == end) {
            if (!bytes.empty() || encoded.empty()) return {};
            auto decoded = Decode64(encoded);
            if (!decoded) return {};
            if (!checksum.empty()) {
                const auto check = Decode64(checksum);
                if (!check || check->size() != 3) return {};
                const auto* digest = reinterpret_cast<const unsigned char*>(check->data());
                const uint32_t expected = uint32_t(digest[0]) << 16 | uint32_t(digest[1]) << 8 | digest[2];
                if (ArmorChecksum(*decoded) != expected) return {};
            }
            return decoded;
        }
        if (line.empty() || line.size() > 128 || !checksum.empty()) return {};
        if (line.starts_with('=') && line.size() == 5) checksum = line.substr(1);
        else encoded += line;
    }
    return {};
}

struct Packet { unsigned tag{}; std::string_view body; };
std::optional<Packet> TakePacket(std::string_view& bytes)
{
    if (bytes.size() < 2) return {};
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    if (!(p[0] & 0x80)) return {};
    unsigned tag{};
    uint64_t size{};
    size_t header{};
    if (p[0] & 0x40) {
        tag = p[0] & 63;
        if (p[1] < 192) { header = 2; size = p[1]; }
        else if (p[1] <= 223 && bytes.size() >= 3) { header = 3; size = (p[1] - 192) * 256 + p[2] + 192; }
        else if (p[1] == 255 && bytes.size() >= 6) header = 6;
        else return {}; // partial lengths are outside this bounded subset
    } else {
        tag = (p[0] >> 2) & 15;
        if ((p[0] & 3) == 3) return {};
        header = 1 + (1U << (p[0] & 3));
    }
    if (bytes.size() < header) return {};
    if (!(p[0] & 0x40) || header == 6) {
        const size_t first = (p[0] & 0x40) ? 2 : 1;
        for (size_t i = first; i < header; ++i) size = (size << 8) | p[i];
    }
    if (!size || size > bytes.size() - header) return {};
    const Packet packet{tag, bytes.substr(header, size_t(size))};
    bytes.remove_prefix(header + size_t(size));
    return packet;
}

bool StrongHash(unsigned algo) { return algo == 8 || algo == 9 || algo == 10; }
bool SigningAlgorithm(unsigned algo) { return algo == 1 || algo == 3 || algo == 19 || algo == 22; }

std::vector<std::string_view> Words(std::string_view text)
{
    std::vector<std::string_view> words;
    while (!text.empty() && words.size() <= 11) {
        const auto space = text.find(' ');
        words.push_back(text.substr(0, space));
        if (space == std::string_view::npos) break;
        text.remove_prefix(space + 1);
    }
    return words;
}

} // namespace

std::optional<std::string> DownloadFingerprint(std::string_view text)
{
    if (text.size() != 40 || !std::ranges::all_of(text, HexDigit)) return {};
    std::string result(text);
    std::ranges::transform(result, result.begin(), UpperHex);
    return result;
}

namespace protocol {

ManifestEntry FindManifestEntry(std::string_view manifest, std::string_view artifact)
{
    if (!ArtifactName(artifact) || manifest.empty()) return {};
    std::unordered_set<std::string_view> seen;
    std::string_view digest;
    while (!manifest.empty()) {
        const auto line = Line(manifest);
        if (seen.size() >= 8192 || line.size() <= 66 || line[64] != ' '
            || (line[65] != ' ' && line[65] != '*')) return {};
        const auto name = line.substr(66), hash = line.substr(0, 64);
        if (!ArtifactName(name) || !std::ranges::all_of(hash, HexDigit)) return {};
        if (!seen.insert(name).second) return {DownloadIntegrity::AmbiguousEntry, {}};
        if (name == artifact) digest = hash;
    }
    if (digest.empty()) return {DownloadIntegrity::MissingEntry, {}};
    std::string canonical(digest);
    std::ranges::transform(canonical, canonical.begin(), UpperHex);
    return {DownloadIntegrity::Unchecked, canonical};
}

std::optional<std::string> PublicOpenPgpKey(std::string_view source)
{
    auto key = OpenPgpBytes(source, "-----BEGIN PGP PUBLIC KEY BLOCK-----", "-----END PGP PUBLIC KEY BLOCK-----");
    if (!key) return {};
    std::string_view bytes = *key;
    const auto root = TakePacket(bytes);
    if (!root || root->tag != 6 || root->body.front() != 4) return {};
    size_t packets{1};
    while (!bytes.empty()) {
        const auto packet = TakePacket(bytes);
        if (!packet || ++packets > 1024 || (packet->tag != 2 && packet->tag != 13 && packet->tag != 14
            && packet->tag != 17)) return {};
    }
    return key;
}

std::optional<std::string> DetachedOpenPgpSignature(std::string_view source)
{
    auto signature = OpenPgpBytes(source, "-----BEGIN PGP SIGNATURE-----", "-----END PGP SIGNATURE-----");
    if (!signature) return {};
    std::string_view bytes = *signature;
    const auto packet = TakePacket(bytes);
    if (!packet || packet->tag != 2 || !bytes.empty() || packet->body.size() < 4) return {};
    const auto* p = reinterpret_cast<const unsigned char*>(packet->body.data());
    if (p[0] != 4 || p[1] != 0 || !SigningAlgorithm(p[2]) || !StrongHash(p[3])) return {};
    return signature;
}

void InterpretOpenPgpStatus(std::string_view status, int exit_code, std::string_view expected, DownloadResult& result)
{
    using S = DownloadSignature;
    using T = DownloadTrust;
    result.signature = S::VerifierError;
    std::string_view outcome;
    std::vector<std::string_view> valid;
    bool unknown{}, failure{};
    unsigned signatures{};
    while (!status.empty()) {
        auto line = Line(status);
        if (!line.starts_with("[GNUPG:] ")) return;
        line.remove_prefix(9);
        const auto split = line.find(' ');
        const auto tag = line.substr(0, split);
        const auto data = split == std::string_view::npos ? std::string_view{} : line.substr(split + 1);
        if (tag == "NEWSIG") ++signatures;
        else if (tag == "NO_PUBKEY") unknown = true;
        else if (tag == "FAILURE" || tag == "ERROR" || tag == "NODATA" || tag == "UNEXPECTED") failure = true;
        else if (tag == "VALIDSIG") {
            if (!valid.empty()) return;
            valid = Words(data);
        } else if (tag == "GOODSIG" || tag == "BADSIG" || tag == "EXPSIG" || tag == "EXPKEYSIG"
                   || tag == "REVKEYSIG" || tag == "ERRSIG") {
            if (!outcome.empty()) return;
            outcome = tag;
        }
    }
    if (signatures != 1) return;
    if (outcome == "BADSIG" && valid.empty() && exit_code != 0) { result.signature = S::Invalid; return; }
    if (outcome == "ERRSIG" && valid.empty() && unknown) { result.signature = S::UnknownSigner; return; }
    if (outcome != "GOODSIG" && outcome != "EXPSIG" && outcome != "EXPKEYSIG" && outcome != "REVKEYSIG") return;
    if (failure || unknown || (exit_code != 0 && outcome == "GOODSIG")) return;
    if (valid.size() != 10 || valid[8] != "00" || (valid[7] != "8" && valid[7] != "9" && valid[7] != "10")
        || valid[4] != "4") {
        result.signature = S::Unsupported;
        return;
    }
    auto signing = DownloadFingerprint(valid[0]), primary = DownloadFingerprint(valid[9]);
    if (!signing || !primary) return;
    result.signature = S::Valid;
    result.signing_fingerprint = std::move(*signing);
    result.primary_fingerprint = std::move(*primary);
    if (outcome == "EXPSIG") result.trust = T::ExpiredSignature;
    else if (outcome == "EXPKEYSIG") result.trust = T::ExpiredKey;
    else if (outcome == "REVKEYSIG") result.trust = T::RevokedKey;
    else if (!expected.empty()) result.trust = expected == result.primary_fingerprint ? T::ExplicitFingerprint : T::FingerprintMismatch;
}

} // namespace protocol
}
