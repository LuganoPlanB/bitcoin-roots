// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "ui/components/controls.h"
#include "adapters/protocol/transaction.h"
#include "adapters/protocol/offline_draft.h"
#include "services/funding_change.h"
#include "services/send_recipients.h"
#include <map>
namespace roots::gui::app {
enum class Section { Transactions,Send,Receive,Addresses,UTXOs,Settings };
inline constexpr std::array<const char*,6> SectionNames{"Transactions","Send","Receive","Addresses","UTXOs","Settings"};
struct SectionState {
    ui::components::Edit note{"Public fixture note"};
    ui::components::Selection selection;
    float scroll_x{},scroll_y{},table_x{},table_y{};
    bool restore_scroll{true},restore_table{true};
};
struct WalletBinding { BackendSessionId backend;WalletId wallet;NetworkIdentity network;std::optional<std::string> daemon_wallet; // exact RPC name when an opaque ID is needed (empty or long name)
    const std::string& DaemonWallet() const{return daemon_wallet?*daemon_wallet:wallet.value();}
    bool operator==(const WalletBinding&) const=default; };
struct HistoryLabelDraft {std::string saved,draft,error;uint64_t base_revision{};bool dirty() const{return saved!=draft;}};
struct HistoryDocumentState {
    bool enabled{};
    std::string selected,filter;
    std::set<std::string> expanded;
    int sort{};bool descending{true};
    std::map<std::string,HistoryLabelDraft> drafts;
    uint64_t query_revision{1},metadata_revision{};
    bool metadata_loaded{}; // fresh storage acknowledgement, never restored
};
struct AddressPresentation {
    std::string selected;std::set<std::string> expanded;
    bool group{true},hide_empty_used{},transactions{},restore{true};
    float split{0.5F},receive_x{},receive_y{},change_x{},change_y{};
};
struct CoinPresentation {
    std::string focused;bool by_value{true},descending{true};
    std::map<std::string,HistoryLabelDraft> drafts;
    uint64_t metadata_revision{};bool metadata_loaded{}; // acknowledgement never recovered

    const auto& Selection() const { return selected; }
    uint64_t SelectionRevision() const { return selection_revision; }
    bool ReplaceSelection(std::set<std::string> next)
    {
        if (next==selected) return true;
        if (next.size()>4096 || selection_revision==UINT64_MAX) return false;
        selected=std::move(next);
        ++selection_revision;
        return true;
    }
private:
    std::set<std::string> selected;
    uint64_t selection_revision{};
};
struct ObservedTransaction {WalletBinding wallet;Hash256 txid;bool operator==(const ObservedTransaction&) const=default;};
struct PinnedCoin {
    Outpoint outpoint;
    std::optional<Money> value;
    std::string script,pinned_reason,current_reason;
    std::optional<protocol::OfflineCoinProof> offline;
};
struct CoinDraft {
    WalletBinding wallet;
    std::vector<PinnedCoin> inputs;
    std::optional<protocol::OfflineChange> offline_change;
    bool observed_unavailable{}; // last explicit exact wallet revalidation denied this pinned set
}; // public data; no reservation/validation authority
struct CreatedFunding {
    uint64_t draft_document;
    DraftRevision draft_revision;
    Money fee;
    CoinDraft evidence;
};
enum class DocumentRepresentation { Source, WireHex };
struct ImportedDocument {
    std::shared_ptr<const protocol::TransactionDocument> parsed;
    protocol::DisplayNetwork network;
    DocumentRepresentation representation{DocumentRepresentation::Source};
    std::optional<CreatedFunding> funding;
};
bool ValidCreatedFunding(const ImportedDocument&);
enum class DocumentKind { Wallet,Transaction };
enum class ObservationState { Offline,Loading,Empty,Error,Stale,Locked };
struct Document {
    uint64_t id;DocumentKind kind;std::string name;
    std::optional<WalletBinding> binding;
    bool complete{true};ObservationState observation{ObservationState::Offline};
    Section section{Section::Transactions};std::array<SectionState,6> sections;
    SectionState transaction;
    std::string original_name;
    int icon{}; // Session metadata, applied only after the host acknowledgement.
    DraftRevision draft_revision;
    std::vector<Outpoint> selected_coins,excluded_coins;
    HistoryDocumentState history;
    AddressPresentation addresses;
    CoinPresentation coins;
    std::optional<CoinDraft> coin_draft;
    std::optional<ObservedTransaction> observed_transaction;
    std::optional<ImportedDocument> imported;
    SendRecipients recipients;
    std::string funding_rate;
    uint16_t funding_target{}; // zero selects Manual; any entered rate overrides a target
    std::optional<FundingChange> funding_change;
    const WalletBinding* SendWallet() const
    {
        if (binding) return &*binding;
        return coin_draft ? &coin_draft->wallet : nullptr;
    }
    bool Dirty() const;
    SectionState& Current() { return kind==DocumentKind::Wallet?sections.at(size_t(section)):transaction; }
    const SectionState& Current() const { return kind==DocumentKind::Wallet?sections.at(size_t(section)):transaction; }
};
class DocumentRegistry {
    std::vector<Document> documents;
    uint64_t active{},next{1};
    bool group_address_outputs{true};
public:
    void SetAddressGroupingDefault(bool value) { group_address_outputs=value; }
    uint64_t OpenWallet(std::string,WalletBinding,bool complete);
    uint64_t OpenTransaction(std::string);
    uint64_t OpenImportedDocument(ImportedDocument);
    // Existing references can focus at capacity; 0 reports the ordinary32-view limit.
    uint64_t OpenObservedTransaction(ObservedTransaction);
    bool Activate(uint64_t);
    bool Navigate(Section);
    Document* Find(uint64_t);
    const Document* Find(uint64_t) const;
    Document* Active() { return Find(active); }
    const Document* Active() const { return Find(active); }
    const auto& Documents() const { return documents; }
    uint64_t ActiveId() const { return active; }
    bool Move(uint64_t,int direction);
    bool Rename(uint64_t,std::string);
    bool SetIcon(uint64_t,int);
    bool Close(std::span<const uint64_t>); // Explicitly confirmed view closure, never wallet deletion.
    std::vector<uint64_t> CloseTargets(uint64_t,bool others,bool all) const;
    // Public recovery codec validates every field before atomically replacing
    // the registry. No host requests, capabilities or approvals are restored.
    bool RestorePublic(std::vector<Document>,uint64_t active_id,uint64_t reserved_through=0);
    void Conceal(); // erase owned editor buffers before dropping locked views
};
std::string Dependency(const Document&,Section);
DocumentRegistry PublicFixtureDocuments(); // Explicit effect-free test/gallery input only.
}
