// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <imkit/accessibility.h>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <deque>
#include <optional>
#include <utility>
namespace roots::gui::ui::components {
namespace ax=imkit::accessibility;
struct TextPosition {int caret{-1},start{-1},end{-1};}; // Unicode offsets in published packets; -1 means unavailable.
struct SemanticItemFacts {ax::StableId id{};TextPosition position;std::optional<bool> showing;std::string shortcut;};
// No view, span, ImGui object or application pointer crosses publication.
struct SemanticEntry {
    ax::StableId id{},parent{},labelled_by{};
    ax::SemanticRole role{};
    std::string name,description,value;
    ax::SemanticState state;
    ImVec2 minimum{},maximum{};
    std::vector<ax::StableId> children;
    ax::SemanticAction actions{};
    double numeric{},minimum_value{},maximum_value{},small_step{},large_step{};
    TextPosition text_position;bool showing{true};
    std::string shortcut;
};
struct SemanticPacket {
    uint64_t generation{};
    std::string scope;
    bool overflow{},valid{};
    std::vector<SemanticEntry> nodes;
    const SemanticEntry* Find(ax::StableId) const;
};
enum class ActionResult {Queued,Applied,Rejected,Stale,Unavailable,Unsupported,Overflow,Duplicate,WrongThread};
struct SemanticRequest {uint64_t sequence{},generation{};ax::StableId id{};ax::SemanticAction action{};std::string value;};
struct SemanticReceipt {uint64_t sequence{};ActionResult result{};};
// Only Queue/Published/Receipts are cross-thread. UI consumption and publication
// are owner-thread operations. Limits are fail-closed, never partial success.
class SemanticExchange {
    mutable std::mutex mutex;
    std::shared_ptr<const SemanticPacket> packet;
    std::deque<SemanticRequest> queued;
    std::vector<SemanticReceipt> receipts;
    uint64_t watermark{};
    bool closed{};
    std::thread::id owner{};
    ActionResult QueueLocked(SemanticRequest);
public:
    bool BindOwner();
    bool Close();
    static constexpr size_t QueueCapacity=32,NodeCapacity=256,TextCapacity=65536,ValueCapacity=4096;
    std::shared_ptr<const SemanticPacket> Published() const;
    std::vector<SemanticReceipt> Receipts() const;
    ActionResult Queue(SemanticRequest);
    // Native adapters share the same sequence allocator with explicit clients.
    std::pair<uint64_t,ActionResult> QueueNext(uint64_t generation,ax::StableId,ax::SemanticAction,std::string value={});
    bool CancelQueued(uint64_t sequence); // Owner only, before dispatch; terminal Stale.
    static std::shared_ptr<const SemanticPacket> Prepare(const ax::AccessibilityTree&,std::string scope,bool coherent=true,std::span<const SemanticItemFacts> positions={});
    bool Publish(const ax::AccessibilityTree&,std::string scope,bool coherent=true,std::span<const SemanticItemFacts> positions={});
    std::vector<SemanticRequest> Drain(std::string_view scope);
    void Finish(uint64_t,ActionResult);
};
}
