// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "adapters/protocol/public_wallet.h"
#include "domain/model.h"
#include <map>
#include <memory>
#include <set>
#include <stop_token>
#include <span>

namespace roots::gui::protocol {
inline constexpr size_t MAX_TRANSACTION_WIRE = 65536;
inline constexpr size_t MAX_TRANSACTION_SOURCE = MAX_TRANSACTION_WIRE*2+64;
inline constexpr size_t MAX_TRANSACTION_IO = 1024;
enum class DocumentFormat { RawBinary, RawHex, PsbtBinary, PsbtBase64, PublicDescriptor };
enum class DisplayNetwork { Unknown, Mainnet, Testnet, Testnet4, Signet, Regtest };
enum class PrevoutEvidence { Unknown, Supplied, TxidMatched, Conflict };
enum class DocumentError { None, Empty, Limit, Format, Malformed, Duplicate, UnsupportedVersion, NetworkRequired, PublicDescriptor, Cancelled };

struct TransactionInput {
    Outpoint outpoint;
    uint32_t sequence{};
    std::optional<Money> previous_value;
    std::string previous_script;
    PrevoutEvidence evidence{PrevoutEvidence::Unknown};
};
struct TransactionOutput {
    Money value;
    std::string script;
    bool operator==(const TransactionOutput&) const = default;
};
struct PsbtPolicy {
    std::optional<uint32_t> sighash;
    std::set<uint32_t> signature_modes;
    std::map<std::string,std::string> scripts; // standardized script commitments, never key-origin claims
    bool final_fields{};
    bool signature_fields{};
};
struct RawTransactionSizes {
    size_t stripped{},weight{},vsize{};
};
struct TransactionInspection {
    uint32_t version{};
    std::optional<uint32_t> locktime;
    std::optional<Hash256> txid; // actual raw transaction only
    std::optional<Hash256> wtxid; // hash of supplied raw wire, including any witness
    std::vector<TransactionInput> inputs;
    std::vector<TransactionOutput> outputs;
    std::optional<Money> fee;
    bool witness{}, coinbase{};
    bool script_sig{};
    std::optional<RawTransactionSizes> sizes; // exact raw serialization only
};
struct ByteSpan {
    size_t offset{},length{};
    bool operator==(const ByteSpan&) const=default;
};
struct InspectionField {
    std::string name;
    ByteSpan bytes;
};
struct InspectionLayout {
    std::vector<InspectionField> header;
    std::vector<std::vector<InspectionField>> inputs,outputs;
};
enum class InspectionBytes { Source, Wire, Unsigned };
enum class InspectionNode { Overview, Input, Output };
struct TransactionDocument {
    DocumentFormat format{};
    std::string source; // original bytes, including any text whitespace or container field ordering
    std::string wire;   // decoded binary for transactions; never a provider reserialization
    std::string unsigned_wire; // derived inspection serialization without final scripts/witness
    InspectionLayout wire_layout,unsigned_layout;
    std::optional<uint32_t> psbt_version;
    std::optional<TransactionInspection> transaction;
    std::optional<PublicWalletPreview> descriptor;
    std::vector<PsbtPolicy> input_policy, output_policy;
    std::optional<uint8_t> modifiable;
    size_t psbt_entries{};
    ~TransactionDocument();
};
std::string_view InspectionBuffer(const TransactionDocument&,InspectionBytes);
std::string_view InspectionBufferName(const TransactionDocument&,InspectionBytes);
std::span<const InspectionField> InspectionFields(const TransactionDocument&,InspectionBytes,InspectionNode,size_t index);
std::optional<ByteSpan> InspectionSpan(const TransactionDocument&,InspectionBytes,ByteSpan);
struct EditInput {
    size_t index;
    Outpoint previous;
    uint32_t sequence;
};
struct EditOutput {
    size_t index;
    TransactionOutput replacement;
};
struct ReorderTransactionItem {
    InspectionNode group;
    size_t index;
    size_t destination;
};
struct TransactionEdit {
    std::optional<uint32_t> version, locktime;
    std::optional<EditInput> input;
    std::optional<EditOutput> output;
    std::optional<ReorderTransactionItem> reorder;
};
struct EditedTransaction {
    std::shared_ptr<const TransactionDocument> document;
    std::string affected,error;
};
std::string TransactionEditBlock(const TransactionDocument&);
EditedTransaction EditTransaction(const TransactionDocument&,DisplayNetwork,const TransactionEdit&);
struct DocumentResult {
    std::shared_ptr<const TransactionDocument> document;
    DocumentError error{DocumentError::None};
    std::optional<uint32_t> unsupported_version;
};
DocumentResult ParseTransactionDocument(std::string_view, DisplayNetwork, std::stop_token = {}, bool text_only = false);
std::string_view DocumentFormatName(DocumentFormat);
std::string DocumentErrorMessage(const DocumentResult&);
std::string_view DocumentNetworkName(DisplayNetwork);
std::string_view DocumentChain(DisplayNetwork);
DisplayNetwork DocumentNetwork(const NetworkIdentity&);
std::string TransactionHex(std::string_view);
struct PsbtOutboundCopy {
    std::string wire;
    size_t omitted_global_xpubs{};
};
std::optional<PsbtOutboundCopy> PsbtForQr(const TransactionDocument&,DisplayNetwork,bool omit_global_xpubs);
std::string TransactionBase64(std::string_view);
std::optional<std::string> DecodeTransactionBase64(std::string_view, size_t maximum);
std::optional<std::string> OutputAddress(std::string_view script_hex, DisplayNetwork);
std::optional<std::string> AddressScript(std::string_view address, DisplayNetwork);
// Add missing non-witness UTXOs from independently read, exact previous transactions.
std::shared_ptr<const TransactionDocument> WithMatchedPreviousOutputs(const TransactionDocument&,
    DisplayNetwork, std::span<const std::shared_ptr<const TransactionDocument>>);
std::optional<std::string> MergePsbtMaps(const TransactionDocument& target, const TransactionDocument& supplied);
// Narrow unsigned negotiation boundary. The outbound copy contains only the
// unsigned transaction and per-input previous-transaction/UTXO/sighash keys.
std::optional<std::string> PayjoinDisclosureCopy(const TransactionDocument&);
// Keep all local maps and graft only the allowed unsigned transaction and one
// new input map from an independently validated proposal. No remote metadata
// is imported into local maps. These helpers perform no transport or approval.
std::shared_ptr<const TransactionDocument> PreservePayjoinMaps(const TransactionDocument& original,
    const TransactionDocument& proposal, DisplayNetwork, std::stop_token = {});
std::optional<Hash256> TransactionCommitment(const TransactionDocument&);
// Returns the first policy or transaction change. Independent of format and map order.
// For an external signature subset, omitted fields can be supplied by the fixed working document.
std::string CompareTransactionReview(const TransactionDocument&, const TransactionDocument&, bool allow_omissions = false);
std::optional<uint32_t> ReviewSighash(const TransactionDocument&, size_t input);
bool VerifyOutputAddress(std::string_view address, std::string_view script_hex, DisplayNetwork);

// An exact match to a proved script says only that this script is in the
// supplied wallet's public policy. It does not establish the output's intent.
bool MatchesPublicPolicy(std::string_view script_hex, DisplayNetwork, const PublicWalletPreview&);
}
