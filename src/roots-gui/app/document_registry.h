// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "ui/components/controls.h"
namespace roots::gui::app {
enum class Section { Transactions,Send,Receive,Addresses,UTXOs,Settings };
inline constexpr std::array<const char*,6> SectionNames{"Transactions","Send","Receive","Addresses","UTXOs","Settings"};
struct SectionState {
    ui::components::Edit note{"Public fixture note"};
    ui::components::Selection selection;
    float scroll_x{},scroll_y{},table_x{},table_y{};
    bool restore_scroll{true},restore_table{true};
};
struct WalletBinding { BackendSessionId backend;WalletId wallet;NetworkIdentity network;bool operator==(const WalletBinding&) const=default; };
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
    bool Dirty() const;
    SectionState& Current() { return kind==DocumentKind::Wallet?sections.at(size_t(section)):transaction; }
};
class DocumentRegistry {
    std::vector<Document> documents;
    uint64_t active{},next{1};
public:
    uint64_t OpenWallet(std::string,WalletBinding,bool complete);
    uint64_t OpenTransaction(std::string);
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
};
std::string Dependency(const Document&,Section);
DocumentRegistry PublicFixtureDocuments(); // Explicit effect-free test/gallery input only.
}
