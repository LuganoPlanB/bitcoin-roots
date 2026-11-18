// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/public_wallet.h"
#include <wally_address.h>
#include <wally_bip32.h>
#include <wally_core.h>
#include <wally_crypto.h>
#include <wally_descriptor.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <memory>
#include <set>
#include <span>

namespace roots::gui::protocol {
namespace {
constexpr size_t MAX_DESCRIPTOR = 4096;
constexpr size_t MAX_SIGNERS = 15;
constexpr uint32_t PARSE_DEPTH = 8U << WALLY_MINISCRIPT_DEPTH_SHIFT;
constexpr std::string_view PUBLIC_ONLY = "Use only public keys (compressed hex, xpub or tpub). Private key and recovery material is rejected.";

uint32_t Network(std::string_view chain)
{
    if (chain == "main") return WALLY_NETWORK_BITCOIN_MAINNET;
    if (chain == "regtest") return WALLY_NETWORK_BITCOIN_REGTEST;
    if (chain == "test" || chain == "testnet4" || chain == "signet") return WALLY_NETWORK_BITCOIN_TESTNET;
    return 0;
}

std::string_view Trim(std::string_view text)
{
    constexpr auto space = " \t\r\n";
    const auto first = text.find_first_not_of(space);
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(space) - first + 1);
}

bool Hex(std::string_view text)
{
    return std::ranges::all_of(text, [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    });
}

bool Index(std::string_view text, uint32_t& value)
{
    if (text.empty() || (text.size() > 1 && text.front() == '0')) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value <= 0x7fffffffU;
}

bool Path(std::string_view path, bool origin, bool& ranged)
{
    size_t components = 0;
    while (!path.empty()) {
        if (path.front() != '/' || ++components > 16) return false;
        path.remove_prefix(1);
        const auto slash = path.find('/');
        auto element = path.substr(0, slash);
        if (element == "*" && !origin && slash == std::string_view::npos) {
            ranged = true;
            return true;
        }
        if (origin && (element.ends_with('h') || element.ends_with('\''))) element.remove_suffix(1);
        uint32_t index = 0;
        if (!Index(element, index)) return false;
        if (slash == std::string_view::npos) return true;
        path.remove_prefix(slash);
    }
    return true;
}

std::string HexBytes(std::span<const unsigned char> bytes)
{
    constexpr std::string_view digits = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 15]);
    }
    return text;
}

enum class KeyStatus { Public, WrongNetwork, Invalid };
struct PublicHeader {
    uint32_t version;
    bool mainnet;
    SignerEncoding encoding;
};
constexpr PublicHeader STANDARD_HEADERS[]{
    {BIP32_VER_MAIN_PUBLIC,true,SignerEncoding::Standard},
    {BIP32_VER_TEST_PUBLIC,false,SignerEncoding::Standard},
};
// Public Bitcoin version bytes from SLIP-0132. Script hints are preserved
// separately and are never part of the public key/chain-code identity.
constexpr PublicHeader ALIAS_HEADERS[]{
    {0x049d7cb2,true,SignerEncoding::NestedSingle}, {0x04b24746,true,SignerEncoding::NativeSingle},
    {0x0295b43f,true,SignerEncoding::NestedMulti}, {0x02aa7ed3,true,SignerEncoding::NativeMulti},
    {0x044a5262,false,SignerEncoding::NestedSingle}, {0x045f1cf6,false,SignerEncoding::NativeSingle},
    {0x024289ef,false,SignerEncoding::NestedMulti}, {0x02575483,false,SignerEncoding::NativeMulti},
};
KeyStatus Extended(std::string_view text, uint32_t network, std::string& identity, PublicSigner* inspected = nullptr)
{
    if (text.size() != 111 || !std::ranges::all_of(text, [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '1' && c <= '9');
    })) return KeyStatus::Invalid;
    struct Scratch {
        std::array<char, 112> encoded{};
        std::array<unsigned char, BIP32_SERIALIZED_LEN + BASE58_CHECKSUM_LEN> bytes{};
        ~Scratch() { wally_bzero(encoded.data(), encoded.size()); wally_bzero(bytes.data(), bytes.size()); }
    } scratch;
    std::ranges::copy(text, scratch.encoded.begin());
    size_t length = 0;
    if (wally_base58_to_bytes(scratch.encoded.data(), BASE58_FLAG_CHECKSUM, scratch.bytes.data(), scratch.bytes.size(), &length) != WALLY_OK
        || length != BIP32_SERIALIZED_LEN || (scratch.bytes[45] != 2 && scratch.bytes[45] != 3)) return KeyStatus::Invalid;
    auto& bytes = scratch.bytes;
    const uint32_t version = (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) | bytes[3];
    const auto standard = std::ranges::find(STANDARD_HEADERS,version,&PublicHeader::version);
    const auto alias = std::ranges::find(ALIAS_HEADERS,version,&PublicHeader::version);
    const auto* header = standard != std::end(STANDARD_HEADERS) ? standard : inspected && alias != std::end(ALIAS_HEADERS) ? alias : nullptr;
    if (!header) return KeyStatus::Invalid;
    if ((network == WALLY_NETWORK_BITCOIN_MAINNET) != header->mainnet) return KeyStatus::WrongNetwork;
    if (wally_ec_public_key_verify(bytes.data()+45,33) != WALLY_OK) return KeyStatus::Invalid;
    if (inspected) {
        inspected->encoding = header->encoding;
        inspected->extended = true;
        inspected->public_key = text;
        const uint32_t canonical = header->mainnet ? BIP32_VER_MAIN_PUBLIC : BIP32_VER_TEST_PUBLIC;
        for (size_t i=0; i<4; ++i) bytes[i] = (canonical >> ((3-i)*8)) & 255;
        ext_key decoded{};
        if (bip32_key_unserialize(bytes.data(),length,&decoded) != WALLY_OK) return KeyStatus::Invalid;
        if (version != canonical) {
            char* raw = nullptr;
            const int converted = wally_base58_from_bytes(bytes.data(),length,BASE58_FLAG_CHECKSUM,&raw);
            const std::unique_ptr<char,decltype(&wally_free_string)> encoded(raw,&wally_free_string);
            if (converted != WALLY_OK || !encoded) return KeyStatus::Invalid;
            inspected->public_key = encoded.get();
        }
    }
    identity = HexBytes(std::span(bytes).subspan(13,BIP32_SERIALIZED_LEN-13));
    return KeyStatus::Public;
}

struct Shape {
    std::string_view arguments, type;
    bool multi{}, taproot{};
};

std::optional<Shape> Policy(std::string_view text)
{
    struct Form { std::string_view prefix, suffix, type; bool multi, taproot; };
    constexpr Form forms[]{
        {"pkh(", ")", "Legacy single signature (P2PKH)", false, false},
        {"wpkh(", ")", "Native SegWit single signature (P2WPKH)", false, false},
        {"sh(wpkh(", "))", "Nested SegWit single signature", false, false},
        {"tr(", ")", "Taproot key path", false, true},
        {"wsh(multi(", "))", "Native SegWit multisig", true, false},
        {"wsh(sortedmulti(", "))", "Native SegWit sorted multisig", true, false},
        {"sh(wsh(multi(", ")))", "Nested SegWit multisig", true, false},
        {"sh(wsh(sortedmulti(", ")))", "Nested SegWit sorted multisig", true, false},
        {"sh(multi(", "))", "Legacy multisig (P2SH)", true, false},
        {"sh(sortedmulti(", "))", "Legacy sorted multisig (P2SH)", true, false},
    };
    for (const auto& form : forms) if (text.starts_with(form.prefix) && text.ends_with(form.suffix)
        && text.size() > form.prefix.size() + form.suffix.size()) {
        return Shape{text.substr(form.prefix.size(), text.size() - form.prefix.size() - form.suffix.size()), form.type, form.multi, form.taproot};
    }
    return {};
}

KeyStatus InspectKey(std::string_view text, const Shape& shape, uint32_t network, bool& ranged,
    std::string& identity, PublicSigner* inspected = nullptr)
{
    if (text.starts_with('[')) {
        const auto end = text.find(']');
        if (end == std::string_view::npos || end < 9 || !Hex(text.substr(1,8)) || !Path(text.substr(9,end-9),true,ranged)) return KeyStatus::Invalid;
        if (inspected) {
            inspected->fingerprint=text.substr(1,8);
            inspected->origin_path="m"+std::string(text.substr(9,end-9));
        }
        text.remove_prefix(end+1);
    }
    const auto slash = text.find('/');
    const auto key = text.substr(0,slash);
    const bool compressed = key.size()==66 && (key.starts_with("02") || key.starts_with("03"));
    if ((compressed || (shape.taproot && key.size()==64)) && Hex(key)) {
        if (slash != std::string_view::npos) return KeyStatus::Invalid;
        identity.assign(key);
        for (char& c : identity) if (c>='A' && c<='F') c+='a'-'A';
        if (inspected) inspected->public_key=identity;
        return KeyStatus::Public;
    }
    if (slash != std::string_view::npos && !Path(text.substr(slash),false,ranged)) return KeyStatus::Invalid;
    if (inspected && slash != std::string_view::npos) inspected->derivation_suffix=text.substr(slash);
    return Extended(key,network,identity,inspected);
}

std::optional<PublicDescriptor> InspectDescriptor(std::string_view text, uint32_t network, bool internal, std::string& error, uint32_t index = 0)
{
    error = "Descriptor is invalid or outside the supported public single-signature, standard multisig and Taproot key-path policies.";
    const auto checksum = text.find('#');
    if (text.empty() || text.size() > MAX_DESCRIPTOR || (checksum != std::string_view::npos && text.size() - checksum != 9)) return {};
    auto shape = Policy(text.substr(0, checksum));
    if (!shape) return {};
    auto keys = shape->arguments;
    uint32_t threshold = 1;
    if (shape->multi) {
        const auto comma = keys.find(',');
        if (comma == std::string_view::npos || !Index(keys.substr(0, comma), threshold) || !threshold) return {};
        keys.remove_prefix(comma + 1);
    }
    PublicDescriptor result;
    result.internal = internal;
    std::set<std::string> identities;
    do {
        std::string identity;
        const auto comma = keys.find(',');
        const auto key = keys.substr(0, comma);
        bool ranged = false;
        if (result.signers.size() >= MAX_SIGNERS || (!shape->multi && comma != std::string_view::npos)) return {};
        switch (InspectKey(key, *shape, network, ranged, identity)) {
        case KeyStatus::Invalid: error = PUBLIC_ONLY; return {};
        case KeyStatus::WrongNetwork: error = "Public key network encoding does not match the selected daemon."; return {};
        case KeyStatus::Public: break;
        }
        if (!identities.insert(identity).second) { error = "Descriptor contains a duplicate public signer key."; return {}; }
        result.signers.emplace_back(key);
        result.ranged = result.ranged || ranged;
        if (comma == std::string_view::npos) break;
        keys.remove_prefix(comma + 1);
    } while (true);
    if (threshold > result.signers.size()) return {};
    if (index && !result.ranged) return {};

    wally_descriptor* raw = nullptr;
    const std::string public_input(text);
    const auto parsed = wally_descriptor_parse(public_input.c_str(), nullptr, network,
        PARSE_DEPTH | (checksum == std::string_view::npos ? 0 : WALLY_MINISCRIPT_REQUIRE_CHECKSUM), &raw);
    const std::unique_ptr<wally_descriptor, decltype(&wally_descriptor_free)> descriptor(raw, &wally_descriptor_free);
    if (parsed != WALLY_OK || !descriptor) return {};
    uint32_t features = 0, key_count = 0;
    if (wally_descriptor_get_features(descriptor.get(), &features) != WALLY_OK
        || (features & (WALLY_MS_IS_PRIVATE | WALLY_MS_IS_MULTIPATH | WALLY_MS_IS_ELEMENTS)) != 0
        || bool(features & WALLY_MS_IS_RANGED) != result.ranged
        || wally_descriptor_get_num_keys(descriptor.get(), &key_count) != WALLY_OK || key_count != result.signers.size()) return {};
    char* canonical_raw = nullptr;
    const auto canonicalized = wally_descriptor_canonicalize(descriptor.get(), 0, &canonical_raw);
    const std::unique_ptr<char, decltype(&wally_free_string)> canonical(canonical_raw, &wally_free_string);
    if (canonicalized != WALLY_OK || !canonical) return {};
    char* address_raw = nullptr;
    const auto derived = wally_descriptor_to_address(descriptor.get(), 0, 0, index, 0, &address_raw);
    const std::unique_ptr<char, decltype(&wally_free_string)> address(address_raw, &wally_free_string);
    std::array<unsigned char, 128> script{};
    size_t length = 0;
    if (derived != WALLY_OK || !address
        || wally_descriptor_to_script(descriptor.get(), 0, 0, 0, 0, index, 0, script.data(), script.size(), &length) != WALLY_OK
        || !length || length > script.size()) return {};
    result.descriptor = canonical.get();
    result.address = address.get();
    result.script = HexBytes(std::span(script).first(length));
    result.choice.script = text.starts_with("tr(") ? ScriptTemplate::Taproot
        : text.starts_with("sh(w") ? ScriptTemplate::NestedSegwit
        : text.starts_with("w") ? ScriptTemplate::NativeSegwit : ScriptTemplate::Legacy;
    result.choice.required = threshold;
    result.choice.signers = key_count;
    result.choice.multisig = shape->multi;
    result.choice.sorted = !shape->multi || text.find("sortedmulti(") != std::string_view::npos;
    result.policy = shape->multi ? std::to_string(threshold) + " of " + std::to_string(key_count) + " · " + std::string(shape->type) : std::string(shape->type);
    error.clear();
    return result;
}
}

PublicPreviewResult PreviewPublicWallet(PublicImportKind kind, std::string_view text, std::string_view chain)
{
    const uint32_t network = Network(chain);
    if (!network) return {{}, "The connected daemon's network has no supported public descriptor encoding."};
    text = Trim(text);
    if (text.empty() || text.size() > MAX_DESCRIPTOR || !std::ranges::all_of(text, [](unsigned char c) { return c >= 33 && c <= 126; })) {
        return {{}, "Provide one bounded line of public descriptor or extended-key text."};
    }
    PublicWalletPreview result{std::string(chain)};
    std::string error;
    if (kind == PublicImportKind::ExtendedKey) {
        std::string identity;
        switch (Extended(text, network, identity)) {
        case KeyStatus::WrongNetwork: return {{}, "Public key network encoding does not match the selected daemon."};
        case KeyStatus::Invalid: return {{}, std::string(PUBLIC_ONLY)};
        case KeyStatus::Public: break;
        }
        for (const bool internal : {false, true}) {
            const auto expression = "wpkh(" + std::string(text) + (internal ? "/1/*)" : "/0/*)");
            auto branch = InspectDescriptor(expression, network, internal, error);
            if (!branch) return {{}, std::move(error)};
            result.descriptors.push_back(std::move(*branch));
        }
    } else {
        auto descriptor = InspectDescriptor(text, network, false, error);
        if (!descriptor) return {{}, std::move(error)};
        result.descriptors.push_back(std::move(*descriptor));
    }
    return {std::move(result), {}};
}

PublicSignerResult InspectPublicSigner(std::string_view text, std::string_view chain, bool taproot)
{
    text=Trim(text);
    const auto network=Network(chain);
    if (!network || text.empty() || text.size()>512
        || !std::ranges::all_of(text,[](unsigned char c){return c>=33 && c<=126;}))
        return {{}, "Provide one bounded public-key expression for this wallet network."};
    PublicSigner signer;
    bool ranged=false;
    switch (InspectKey(text,{{},{},false,taproot},network,ranged,signer.material_id,&signer)) {
    case KeyStatus::WrongNetwork: return {{}, "The public key encoding does not match the wallet network."};
    case KeyStatus::Invalid: return {{}, "Invalid public key or derivation. Private keys and recovery words are rejected."};
    case KeyStatus::Public: break;
    }
    if (!signer.extended) {
        // Borrow the checked descriptor route to prove a fixed curve key;
        // it accepts public text only, and Taproot may use x-only keys.
        std::string error;
        const auto parsed=InspectDescriptor(std::string(taproot?"tr(":"wpkh(")+std::string(text)+')',network,false,error);
        if (!parsed) return {{}, "The public curve key is invalid for this script."};
    }
    if (!signer.fingerprint.empty()) {
        for (char& c : signer.fingerprint) if (c>='A' && c<='F') c+='a'-'A';
        std::ranges::replace(signer.origin_path,'\'','h');
        signer.expression='['+signer.fingerprint+signer.origin_path.substr(1)+']';
    }
    signer.expression+=signer.public_key+signer.derivation_suffix;
    return {std::move(signer),{}};
}

bool CompatibleSignerEncoding(SignerEncoding encoding,const PolicyChoice& choice)
{
    switch (encoding) {
    case SignerEncoding::Standard: return true;
    case SignerEncoding::NestedSingle: return choice.script==ScriptTemplate::NestedSegwit && !choice.multisig;
    case SignerEncoding::NativeSingle: return choice.script==ScriptTemplate::NativeSegwit && !choice.multisig;
    case SignerEncoding::NestedMulti: return choice.script==ScriptTemplate::NestedSegwit && choice.multisig;
    case SignerEncoding::NativeMulti: return choice.script==ScriptTemplate::NativeSegwit && choice.multisig;
    }
    return false;
}

bool SupportedPolicy(const PolicyChoice& choice)
{
    switch (choice.script) {
    case ScriptTemplate::Taproot:
        if (choice.multisig) return false;
        break;
    case ScriptTemplate::NativeSegwit:
    case ScriptTemplate::NestedSegwit:
    case ScriptTemplate::Legacy: break;
    default: return false;
    }
    return choice.multisig
        ? choice.signers >= 2 && choice.signers <= MAX_SIGNERS && choice.required >= 1 && choice.required <= choice.signers
        : choice.required == 1 && choice.signers == 1;
}

namespace {
std::string PolicyExpression(const PolicyChoice& choice, std::span<const std::string> signers)
{
    std::string expression;
    if (choice.multisig) {
        expression = choice.sorted ? "sortedmulti(" : "multi(";
        expression += std::to_string(choice.required);
        for (const auto& signer : signers) expression += ',' + signer;
        expression += ')';
        if (choice.script == ScriptTemplate::Legacy) expression = "sh(" + expression + ')';
        else {
            expression = "wsh(" + expression + ')';
            if (choice.script == ScriptTemplate::NestedSegwit) expression = "sh(" + expression + ')';
        }
    } else {
        const auto& signer = signers.front();
        switch (choice.script) {
        case ScriptTemplate::NativeSegwit: expression = "wpkh(" + signer + ')'; break;
        case ScriptTemplate::NestedSegwit: expression = "sh(wpkh(" + signer + "))"; break;
        case ScriptTemplate::Legacy: expression = "pkh(" + signer + ')'; break;
        case ScriptTemplate::Taproot: expression = "tr(" + signer + ')'; break;
        }
    }
    return expression;
}
}

PublicPreviewResult StageSignerPolicy(const PolicyChoice& choice,std::string_view chain,std::span<const PublicSigner> signers)
{
    if (!SupportedPolicy(choice) || signers.size()!=choice.signers || Network(chain)==0)
        return {{}, "Fill exactly the supported number of public signer slots."};
    std::vector<std::string> expressions;
    std::set<std::string> assessed;
    for (const auto& signer : signers) {
        auto verified=InspectPublicSigner(signer.expression,chain,choice.script==ScriptTemplate::Taproot);
        if (!verified.signer || verified.signer->expression!=signer.expression || verified.signer->material_id!=signer.material_id)
            return {{}, "A staged public signer could not be verified for this script and network."};
        if (!CompatibleSignerEncoding(signer.encoding,choice)) return {{}, "The key header is for a different script template or signer policy."};
        if (!assessed.insert(signer.material_id).second) return {{}, "Duplicate public key material and chain code."};
        expressions.push_back(signer.expression);
    }
    return PreviewPublicWallet(PublicImportKind::Descriptor,PolicyExpression(choice,expressions),chain);
}

PublicPreviewResult StageWalletPolicy(const PolicyChoice& choice, const PublicWalletPreview& source)
{
    if (!SupportedPolicy(choice)) return {{}, "Choose a supported script and threshold."};
    if (!VerifyPublicPreview(source, source.chain)) return {{}, "Provide validated public descriptor material."};

    PublicWalletPreview result{source.chain};
    for (const auto& branch : source.descriptors) {
        if (branch.signers.size() != choice.signers) return {{}, "The descriptor must supply exactly the selected number of public signer expressions."};
        std::string error;
        auto validated = InspectDescriptor(PolicyExpression(choice,branch.signers), Network(source.chain), branch.internal, error);
        if (!validated || validated->choice != choice) return {{}, error.empty() ? "The requested script does not match its public policy." : std::move(error)};
        result.descriptors.push_back(std::move(*validated));
    }
    if (!VerifyPublicPreview(result, source.chain)) return {{}, "The receive and change descriptors must remain distinct."};
    return {std::move(result), {}};
}

bool VerifyPublicPreview(const PublicWalletPreview& preview, std::string_view chain)
{
    const uint32_t network = Network(chain);
    if (!network || chain != preview.chain || preview.descriptors.empty() || preview.descriptors.size() > 2
        || preview.descriptors.front().internal) return false;
    for (size_t i = 0; i < preview.descriptors.size(); ++i) {
        const auto& descriptor = preview.descriptors[i];
        std::string error;
        auto checked = InspectDescriptor(descriptor.descriptor, network, descriptor.internal, error);
        if (descriptor.internal != (i == 1) || !checked || descriptor != *checked) return false;
    }
    return preview.descriptors.size() < 2 || preview.descriptors[0].address != preview.descriptors[1].address;
}

std::optional<PublicDescriptor> DerivePublicDescriptor(std::string_view descriptor, std::string_view chain,uint32_t index)
{
    const auto network=Network(chain);
    if (!network || index>=0x80000000U || descriptor.find('#')==std::string_view::npos) return {};
    std::string error;
    return InspectDescriptor(descriptor,network,false,error,index);
}

bool VerifyReceiveTarget(const ReceiveTarget& target)
{
    const auto network = Network(target.chain);
    if (!network || target.observed_ms <= 0 || target.address.empty() || target.address.size() > 128
        || target.script.empty() || target.script.size() > 256 || target.path.size() > 256
        || target.descriptor.find('#') == std::string::npos || (target.index && *target.index >= 0x80000000U)) return false;
    bool ranged = false;
    if (!target.path.empty() && (!target.path.starts_with('m') || !Path(std::string_view(target.path).substr(1), true, ranged))) return false;
    std::string error;
    const auto checked = InspectDescriptor(target.descriptor, network, false, error, target.index.value_or(0));
    return checked && checked->ranged == target.index.has_value() && checked->descriptor == target.descriptor
        && checked->address == target.address && checked->script == target.script;
}

std::string_view ReceiveChain(std::string_view genesis)
{
    if (genesis == "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f") return "main";
    if (genesis == "000000000933ea01ad0ee984209779baaec3ced90fa3f408719526f8d77f4943") return "test";
    if (genesis == "00000000da84f2bafbbc53dee25a72ae507ff4914b867c565be350b0da8bf043") return "testnet4";
    if (genesis == "00000008819873e925422c1ff0f99f7cc9bbb232af63a077a480a3633bee1ef6") return "signet";
    if (genesis == "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206") return "regtest";
    return {};
}
}
