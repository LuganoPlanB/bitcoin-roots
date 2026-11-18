// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/public_wallet.h"
#include "services/wallet_lifecycle.h"
#include <wally_bip32.h>
#include <wally_core.h>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace roots::gui;
namespace {
unsigned checks{};
void Check(bool valid, const char* label)
{
    if (!valid) throw std::runtime_error(label);
    ++checks;
}
std::string EncodedKey(bool mainnet, bool private_key)
{
    // Disposable deterministic material exists only for the rejection and
    // address vectors in this process. Never print or retain a private fixture.
    std::array<unsigned char, 32> seed{};
    seed[0] = 7;
    ext_key* raw = nullptr;
    const int status = bip32_key_from_seed_alloc(seed.data(), seed.size(), mainnet ? BIP32_VER_MAIN_PRIVATE : BIP32_VER_TEST_PRIVATE, 0, &raw);
    wally_bzero(seed.data(), seed.size());
    const std::unique_ptr<ext_key, decltype(&bip32_key_free)> key(raw, &bip32_key_free);
    Check(status == WALLY_OK && key, "fixture key construction");
    char* encoded = nullptr;
    Check(bip32_key_to_base58(key.get(), private_key ? BIP32_FLAG_KEY_PRIVATE : BIP32_FLAG_KEY_PUBLIC, &encoded) == WALLY_OK && encoded, "fixture key encoding");
    std::string result(encoded);
    wally_free_string(encoded);
    return result;
}
}

int main()
{
    using namespace protocol;
    try {
        const auto key = EncodedKey(false, false);
        const auto main_key = EncodedKey(true, false);
        std::string private_descriptor = "wpkh(";
        {
            auto secret = EncodedKey(false, true);
            private_descriptor += secret + "/0/*)";
            const auto rejected_key = PreviewPublicWallet(PublicImportKind::ExtendedKey, secret, "regtest");
            wally_bzero(secret.data(), secret.size());
            Check(!rejected_key.preview && rejected_key.error.find("Private key") != std::string::npos, "private extended key rejected");
        }
        const auto rejected_descriptor = PreviewPublicWallet(PublicImportKind::Descriptor, private_descriptor, "regtest");
        wally_bzero(private_descriptor.data(), private_descriptor.size());
        Check(!rejected_descriptor.preview && rejected_descriptor.error.find("Private key") != std::string::npos, "descriptor with a private key rejected");
        auto wrong = PreviewPublicWallet(PublicImportKind::ExtendedKey, main_key, "regtest");
        Check(!wrong.preview && wrong.error.find("network") != std::string::npos, "mainnet key rejected for regtest");
        Check(!PreviewPublicWallet(PublicImportKind::Descriptor, "wpkh(" + key + "/0/*)", "main").preview, "testnet descriptor rejected on mainnet");
        Check(!PreviewPublicWallet(PublicImportKind::ExtendedKey, key, "unvalidated").preview, "unknown target network rejected");

        auto xpub = PreviewPublicWallet(PublicImportKind::ExtendedKey, key, "regtest");
        Check(xpub.preview && xpub.preview->descriptors.size() == 2, "two explicit xpub branches");
        const auto& receive = xpub.preview->descriptors[0];
        const auto& change = xpub.preview->descriptors[1];
        Check(!receive.internal && receive.ranged && receive.address.starts_with("bcrt1") && receive.script.starts_with("0014"), "regtest native segwit receive");
        Check(change.internal && change.ranged && change.address != receive.address && change.script != receive.script, "independent change");
        Check(receive.signers == std::vector<std::string>{key + "/0/*"} && change.signers == std::vector<std::string>{key + "/1/*"}, "literal public derivations");
        Check(VerifyPublicPreview(*xpub.preview, "regtest") && !VerifyPublicPreview(*xpub.preview, "test"), "preview bound to selected chain");
        auto altered = *xpub.preview;
        altered.descriptors[0].script = change.script;
        Check(!VerifyPublicPreview(altered, "regtest"), "forged preview script rejected");

        auto single = PreviewPublicWallet(PublicImportKind::Descriptor, receive.descriptor, "regtest");
        Check(single.preview && single.preview->descriptors == std::vector<PublicDescriptor>{receive}, "descriptor round trip");
        auto invalid_checksum = receive.descriptor;
        invalid_checksum.back() = invalid_checksum.back() == 'q' ? 'p' : 'q';
        Check(!PreviewPublicWallet(PublicImportKind::Descriptor, invalid_checksum, "regtest").preview, "wrong checksum rejected");
        Check(!PreviewPublicWallet(PublicImportKind::Descriptor, "wpkh(" + key + "/0h/*)", "regtest").preview, "hardened child from xpub rejected");
        Check(!PreviewPublicWallet(PublicImportKind::Descriptor, "wsh(sortedmulti(1,[00000000]" + key + "/0/*,[11111111]" + key + "/0/*))", "regtest").preview,
            "duplicate key under different origins rejected");
        const std::string compressed = "0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";
        auto fixed = PreviewPublicWallet(PublicImportKind::Descriptor, "wpkh(" + compressed + ")", "regtest");
        Check(fixed.preview && !fixed.preview->descriptors[0].ranged, "fixed public descriptor");
        auto multi = PreviewPublicWallet(PublicImportKind::Descriptor, "wsh(sortedmulti(2," + compressed + "," + key + "/0/*))", "regtest");
        Check(multi.preview && multi.preview->descriptors[0].policy.starts_with("2 of 2"), "supported standard multisig preview");
        Check(!PreviewPublicWallet(PublicImportKind::Descriptor, "wpkh(" + std::string(4096, 'x') + ")", "regtest").preview, "bounded descriptor text");
        Check(ValidDaemonWalletPath("/var/lib/bitcoin/recovery.dat") && ValidDaemonWalletPath("D:\\vault\\recovery.dat"), "absolute daemon paths");
        for (const auto* invalid : {"relative.dat", "//server/file", "\\\\server\\file", "/path/../file", "/path/", "file:///path", "D:file"}) {
            Check(!ValidDaemonWalletPath(invalid), "ambiguous host path rejected");
        }
        std::cout << "{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":" << checks << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
