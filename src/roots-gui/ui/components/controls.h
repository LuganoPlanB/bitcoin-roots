// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "ui/components/native_input.h"
#include "domain/model.h"
#include "ui/components/presentation.h"
#include <imkit/imkit.h>
#include <array>
#include <set>
#include <span>
#include <variant>
namespace roots::gui::ui::components {
struct CopyIdentifier { std::string id, value; bool sensitive{}; };
struct CopyAmount { std::string id; Money value; };
struct CommitAmount { std::string id; Money value; };
struct CommitFee { std::string id; FeeRate value; };
struct CommitLabel { std::string id, value; };
struct SelectRows { std::string id; std::vector<uint64_t> rows; };
enum class ActionKind { Primary, Secondary, OpenDialog, AcceptDialog, CancelDialog, Retry };
struct Action { std::string id; ActionKind kind; };
using Command=std::variant<CopyIdentifier,CopyAmount,CommitAmount,CommitFee,CommitLabel,SelectRows,Action>;
// Host owns persistence, clipboard and effects. Acknowledge only actual accepted
// requests. A failed acknowledgement leaves the UI draft and saved value intact.
using LiveAvailability=std::function<bool()>;
using RowAvailability=std::function<bool(uint64_t)>;
using CommandEndpoint=std::function<bool(const Command&)>;
struct Commands {
    virtual ~Commands()=default;virtual bool Submit(const Command&)=0;
    virtual bool Available(const Command&) const {return true;}
    bool Request(const Command& c){return Available(c) && Submit(c);}
    // Stable hosts outlive the enclosing frame. Stack adapters must override
    // Capture and CaptureAvailability with owned endpoints, never adapter pointers.
    virtual CommandEndpoint CaptureAvailability() const {return [this](const Command& c){return Available(c);};}
    virtual CommandEndpoint Capture() {return [this](const Command& c){return Request(c);};}
};
struct EndpointCommands final:Commands {
    CommandEndpoint endpoint;explicit EndpointCommands(CommandEndpoint e):endpoint(std::move(e)){}
    bool Submit(const Command& c) override{return endpoint(c);}
};
struct Edit {
    std::array<char,256> text{};
    std::string saved, error;
    std::string last_attempt;
    std::optional<std::string> external_buffer;
    TextPosition cursor; // Actual public ImGui callback byte offsets, cleared when inactive.
    bool attempted{};
    explicit Edit(std::string_view initial="");
    bool SetDraft(std::string_view);
    bool SetExternalDraft(std::string_view);
    std::string_view draft() const { return text.data(); }
    bool dirty() const { return draft()!=saved; }
};
bool SetExternalBuffer(std::span<char>,std::optional<std::string>&,std::string_view);
bool TextInput(const char*,const char* hint,char*,size_t,std::optional<std::string>&,ImGuiInputTextFlags flags=0,
    TextPosition* byte_cursor=nullptr,bool sensitive=false,ImVec2 multiline={},bool protected_editor=false);
bool EditInput(const char*,Edit&,ImGuiInputTextFlags flags=0,bool sensitive=false);
bool CommitAmountEdit(const char*,Edit&,Commands&);
std::optional<Money> ParseAmount(std::string_view);
bool CommitFeeEdit(const char*,Edit&,Commands&);
bool CommitLabelEdit(const char*,Edit&,Commands&);
struct ItemFacts { std::array<float,4> rect{}; ImGuiID id{}; bool focused{}, disabled{}, activated{}, requested{}, visible{}; };
ItemFacts CopyableIdentifier(const char*,std::string_view,Commands&,const imkit::Theme&,bool enabled=true,ImFont* monospace=nullptr,PresentationFrame* =nullptr,LiveAvailability={});
ItemFacts AmountField(const char*,Edit&,Commands&,const imkit::Theme&,bool enabled=true,AmountPrivacy={},PresentationFrame* =nullptr,LiveAvailability={});
ItemFacts FeeField(const char*,Edit&,Commands&,const imkit::Theme&,bool enabled=true,AmountPrivacy={},PresentationFrame* =nullptr,LiveAvailability={});
ItemFacts LabelEditor(const char*,Edit&,Commands&,const imkit::Theme&,bool enabled=true,PresentationFrame* =nullptr,LiveAvailability={});
ItemFacts ActionButton(const char*,const char*,ActionKind,Commands&,const imkit::Theme&,bool enabled=true,const char* reason="Unavailable",PresentationFrame* =nullptr,LiveAvailability={});
struct FreshnessStatus { Freshness freshness{Freshness::Unknown}; bool failed{},incomplete{}; std::string detail, observed; bool busy{}; };
void StatusBanner(const char*,const FreshnessStatus&,const imkit::Theme&,PresentationFrame* =nullptr);
struct Row { uint64_t id; std::string label; Money amount; bool enabled{true}; std::string disabled_reason{"Unavailable"}; };
struct Selection { std::set<uint64_t> selected; std::optional<uint64_t> focused; std::string error; bool invalid_keys{}; };
struct RowFacts { uint64_t key; ImGuiID widget; bool focused,selected,disabled,visible; };
std::vector<RowFacts> SelectionTable(const char*,std::span<const Row>,Selection&,Commands&,const imkit::Theme&,float height=120,AmountPrivacy={},PresentationFrame* =nullptr,RowAvailability={});
ItemFacts CopyableAmount(const char*,Money,Commands&,const imkit::Theme&,AmountPrivacy={},PresentationFrame* =nullptr,bool enabled=true,LiveAvailability={});
void AmountSummary(const char*,std::span<const Row>,const imkit::Theme&,AmountPrivacy={},PresentationFrame* =nullptr);
void ReviewDetails(const char*,Money,std::string_view full_destination,AmountPrivacy,PresentationFrame* =nullptr);
struct Dialog { imkit::DialogState state; std::string error;bool close_pending{}; };
ItemFacts DialogLauncher(const char*,const char*,Dialog&,Commands&,const imkit::Theme&,PresentationFrame* =nullptr);
ItemFacts DialogShell(const char*,const char*,Dialog&,Commands&,const imkit::Theme&,bool valid=true,PresentationFrame* =nullptr,LiveAvailability={});
}
