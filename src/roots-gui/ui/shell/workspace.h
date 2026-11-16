// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "app/commands.h"
#include "ui/bootstrap.h"
#include <map>
namespace roots::gui::ui::shell {
struct Rect { float x{},y{},w{},h{}; };
struct Layout { Rect menus,tabs,rail,content,status,accounts;bool compact{}; };
Layout Measure(ImVec2 origin,ImVec2 size,float scale,float menu_height);
enum class DocumentDialog { None,Close,Rename,Delete,Import };
struct DocumentActions {
    DocumentDialog kind{DocumentDialog::None};
    imkit::DialogState dialog;
    uint64_t target{},return_focus{};
    std::vector<uint64_t> close_ids;
    components::Edit name;
    int icon{},format{-1};
    std::array<char,4097> source{};
    std::optional<std::string> external_source;components::TextPosition source_cursor;
    app::ImportCandidate candidate;
    std::string error;
    uint64_t file_dialog{};std::string file_source;int file_format{-1};bool file_return_focus{};
};
enum class Drawer { None,Activity,Services };
struct Invocation {app::Command command;uint64_t document{};bool available{},dispatched{},accepted{};std::string reason;};
struct State {
    app::DocumentRegistry documents;
    components::AmountPrivacy privacy;
    bool semantic_hide_toggle{};
    components::PresentationFrame presentation;
    std::string notice;
    int unit{};
    uint64_t focus_tab{};
    uint64_t previous_document{};
    bool previous_locked{};
    bool focus_empty{},reveal_empty_focus{};
    int reveal_navigation{-1};
    DocumentActions actions;
    Rect native_drop_target;
    app::HostObservations observations;
    std::optional<Invocation> invocation;
    Drawer drawer{Drawer::None};
    bool drawer_focus{},restore_status{};
    uint64_t grid_exits{};
    uint64_t reveal_tab_focus{};
    bool semantic_menu_open{};
    int semantic_menu_request{-1};
    uint64_t close_tab_popup{};
    bool close_document_popup{};
    uint64_t semantic_context{},semantic_context_revision{};
};
struct Facts {
    Layout layout;
    uint64_t active_document{};
    int section{-1};
    std::array<components::ItemFacts,6> navigation{};
    components::ItemFacts primary;
    components::ItemFacts note;
    std::vector<components::ItemFacts> rows;
    std::map<uint64_t,components::ItemFacts> tabs;
    std::map<uint64_t,bool> tab_hovered,tab_held,tab_cursor_visible;
    std::array<ImGuiID,4> menus{};
    std::map<app::Command,ImGuiID> menu_items;
    std::map<app::Command,components::ItemFacts> menu_rows;
    float observed_scroll_x{},observed_scroll_y{};
    std::array<float,4> table_hover{};
    float table_scroll_x{},table_scroll_max_x{};
    bool horizontal_scroll{},accounts_submitted{};
    bool appearance_notice_visible{};
    int appearance_notice_count{};
    int appearance_requested{-1};
    Rect drop_target;
    std::map<std::string,components::ItemFacts> document_actions;
    bool document_dialog_visible{};
    std::vector<uint64_t> annotated_tabs;
    std::map<app::Command,app::Availability> availability;
    std::map<app::Command,std::string> shortcut_labels;
    components::ItemFacts activity_button,status_button,drawer_close;
    Rect drawer_rect,drawer_body;
    std::array<float,4> status_visual{};
    float drawer_scroll{},drawer_scroll_max{};
    bool drawer_visible{},grid_escape{},activity_hovered{},activity_held{};

};
ImGuiKeyChord CommandChord(std::string_view,bool macos);
std::string CommandShortcutLabel(std::string_view,bool macos);
app::Availability CommandAvailability(const State&,app::Command,const app::Requests&);
void DrawStatus(State&,Facts&,const imkit::Theme&,float);
void DrawActivityDrawer(State&,Facts&,const imkit::Theme&,float);
void RestoreDocumentFocus(State&,uint64_t desired);
void BeginClose(State&,uint64_t,bool others=false,bool all=false);
void BeginRename(State&,uint64_t);
void BeginDelete(State&,uint64_t);
void BeginImport(State&,std::string_view source={});
void ClosePendingDocumentPopup(State&);
void DrawDocumentDialog(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void DrawTabActions(State&,app::Requests&,Facts&,uint64_t,const imkit::Theme&);
std::string SemanticScope(const State&);
Facts DrawWorkspace(State&,app::Requests&,const imkit::Theme&,const BootstrapAppearance&,ImVec2 work_size={});
}
