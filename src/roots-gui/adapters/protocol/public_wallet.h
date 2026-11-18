// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "domain/receive.h"
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace roots::gui::protocol {
enum class PublicImportKind { Descriptor, ExtendedKey };

enum class ScriptTemplate { NativeSegwit, NestedSegwit, Legacy, Taproot };
struct PolicyChoice {
    ScriptTemplate script{ScriptTemplate::NativeSegwit};
    uint32_t required{1}, signers{1};
    bool multisig{}, sorted{true};
    bool operator==(const PolicyChoice&) const = default;
};
bool SupportedPolicy(const PolicyChoice&);

enum class SignerEncoding { Standard, NestedSingle, NativeSingle, NestedMulti, NativeMulti };
struct PublicSigner {
    std::string expression, material_id, public_key, fingerprint, origin_path, derivation_suffix;
    SignerEncoding encoding{SignerEncoding::Standard};
    bool extended{};
};
struct PublicSignerResult {
    std::optional<PublicSigner> signer;
    std::string error;
};
PublicSignerResult InspectPublicSigner(std::string_view, std::string_view chain, bool taproot = false);
bool CompatibleSignerEncoding(SignerEncoding, const PolicyChoice&);

struct PublicDescriptor {
    std::string descriptor, policy, script, address;
    std::vector<std::string> signers;
    bool ranged{}, internal{};
    PolicyChoice choice;
    bool operator==(const PublicDescriptor&) const = default;
};

struct PublicWalletPreview {
    std::string chain;
    std::vector<PublicDescriptor> descriptors;
    bool operator==(const PublicWalletPreview&) const = default;
};

struct PublicPreviewResult {
    std::optional<PublicWalletPreview> preview;
    std::string error;
};

// Reject unsupported/private inputs before they reach a descriptor parser,
// editor state, diagnostic or RPC. The caller supplies the verified target chain.
PublicPreviewResult PreviewPublicWallet(PublicImportKind, std::string_view, std::string_view chain);
PublicPreviewResult StageSignerPolicy(const PolicyChoice&, std::string_view chain, std::span<const PublicSigner>);
PublicPreviewResult StageWalletPolicy(const PolicyChoice&, const PublicWalletPreview&);
bool VerifyPublicPreview(const PublicWalletPreview&, std::string_view chain);
std::optional<PublicDescriptor> DerivePublicDescriptor(std::string_view descriptor, std::string_view chain, uint32_t index);
bool VerifyReceiveTarget(const ReceiveTarget&);
std::string_view ReceiveChain(std::string_view genesis);
}
