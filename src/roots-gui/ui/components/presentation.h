// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "domain/model.h"
#include "ui/components/semantic_packet.h"
#include <functional>
#include <imkit/imkit.h>
#include <imkit/accessibility.h>
#include <deque>
#include <array>
#include <span>
namespace roots::gui::ui::components {
struct AmountPresentation {
    std::string text,tooltip,semantic;
    std::optional<std::string> copy;
};
struct ChartPresentation {
    std::vector<float> percentages; // Geometry only; never used for edits or approval.
    std::string alternative;
};
// Explicit per-view input. Domain amounts and retained edit drafts never change.
struct AmountPrivacy {
    bool hidden{};
    bool trim_decimals{};
    AmountPresentation Present(std::optional<Money>) const;
    AmountPresentation Draft(std::string_view) const;
    ChartPresentation Chart(std::span<const Money>) const;
};
// Review honors the same explicit policy. Full destinations remain available;
// exact financial review requires a deliberate show-amounts choice by the user.
// No approval/sign/broadcast capability.
struct ReviewPresentation { AmountPresentation amount;std::string destination;bool reveal_required; };
ReviewPresentation DeliberateReview(Money,std::string_view full_destination,AmountPrivacy);
// Own both annotation names and values through frame publication. This is a presentation
// annotation contract, not the native accessibility bridge or complete tree.
class PresentationFrame {
    std::deque<std::string> strings;
    std::array<imkit::accessibility::SemanticNode,256> nodes{};
    imkit::accessibility::AccessibilityFrame frame{nodes};
    uint64_t generation{};
    std::vector<float> chart_samples;
    std::vector<SemanticRequest> pending;
    struct Deferred {SemanticRequest request;std::function<bool(const SemanticRequest&)> apply;std::function<bool()> available;};
    std::vector<Deferred> deferred;
    struct FocusIntent {SemanticRequest request;std::string scope;uint64_t draw_generation;bool realizing{};};
    std::vector<FocusIntent> focus_intents;
    struct ValidationFocus {ax::StableId id;std::string scope;uint64_t draw_generation;};
    std::optional<ValidationFocus> error_focus;
    std::string scope;
    std::function<std::string()> current_scope;
    bool suppressed{},text_overflow{};
    size_t text_bytes{};
    std::string_view Own(std::string_view);
    size_t publication_start{},publication_end{};
    std::vector<ax::StableId> parents;
    std::vector<SemanticItemFacts> item_facts;
    SemanticItemFacts* ItemFacts(ax::StableId);
public:
    PresentationFrame()=default;
    PresentationFrame(const PresentationFrame&)=delete;
    PresentationFrame& operator=(const PresentationFrame&)=delete;
    SemanticExchange exchange;
    void Begin(std::string scope={},std::function<std::string()> current={});
    void Suppress(bool value) { suppressed=value; }
    void DialogOnly();
    void OnlyCurrentLayer() { publication_end=frame.Tree().nodes.size();suppressed=true; }
    void Group(const char* id,const char* name,ax::SemanticRole,ImVec2 minimum,ImVec2 maximum);
    void Parent(ax::StableId id) { parents.push_back(id); }
    void EndGroup() { if(!parents.empty())parents.pop_back(); }
    bool Seal();
    // Annotates the actual preceding item; consumes at most one activation per
    // target/frame. Pointer and external activation converge at the caller.
    std::optional<SemanticRequest> Interact(const char* name,ax::SemanticRole,ax::SemanticAction,bool disabled=false,bool selected=false,std::string_view value={},std::string_view description={},bool invalid=false,bool required=false);
    void Defer(const SemanticRequest&,std::function<bool(const SemanticRequest&)>,std::function<bool()> available={});
    void FocusOnNextDraw(ax::StableId id) { error_focus=ValidationFocus{id,scope,generation+1}; }
    void BindTextPosition(ax::StableId,std::string_view,TextPosition byte_offsets);
    void BindShortcut(ax::StableId,std::string_view);
    bool Annotate(const char* id,const char* name,std::string_view value,
                  imkit::accessibility::SemanticRole role=imkit::accessibility::SemanticRole::Text,bool disabled=false,bool read_only=true,bool busy=false);
    bool RangeValue(ax::StableId,double current,double minimum,double maximum,double step);
    void Progress(const char* id,const char* name,std::string_view value,std::optional<unsigned> per_mille,bool busy);
    ax::AccessibilityTree Tree() const { auto tree=frame.Tree();tree.overflow|=text_overflow;return tree; }
    void RecordChart(std::span<const float> samples) { chart_samples.insert(chart_samples.end(),samples.begin(),samples.end()); }
    const auto& ChartSamples() const { return chart_samples; }
    std::string Snapshot() const;
};
struct ViewPolish {
    imkit::AnimationState animation;
    bool accent_used{};
    void Begin() { accent_used=false; }
    bool ClaimIdentityAccent();
    float PrivacyFeedback(ImGuiID,bool hidden,bool reduced_motion,float delta,int frame);
};
}
