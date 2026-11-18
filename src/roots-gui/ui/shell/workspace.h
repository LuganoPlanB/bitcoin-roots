// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "app/commands.h"
#include "services/addresses.h"
#include "app/coin_inventory.h"
#include "app/coin_controls.h"
#include "ui/bootstrap.h"
#include "ui/server.h"
#include "app/recovery.h"
#include "services/refresh.h"
#include <map>
#include <utility>
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
struct PrivacyPanel {bool setup{},reveal{},focus{true};std::array<char,256> password{},confirm{};std::optional<std::string> external_password,external_confirm;std::string error;};
struct HistoryLabelEditor {
    RefreshIdentity identity;
    std::string txid;
    uint64_t metadata_revision{};
    bool seen{},active{},finished{},attempted{};
};
struct HistoryPermission {RefreshIdentity identity;std::string txid;int64_t observed{};uint64_t generation{},sequence{};};
struct HistoryRuntime {
    std::optional<HistoryLabelEditor> editor; // transient activation proof, never recovered
    std::optional<std::pair<std::string,std::string>> label_update; // native buffer sync, never a recovered action
    std::map<std::string,std::string> labels;
    std::string error,export_data,export_scope;
    uint64_t file_dialog{};
    std::optional<RefreshIdentity> export_identity;
    int64_t observed{};
    uint64_t generation{},sequence{};
    bool reveal_selected{},fee_actions{};
    std::optional<HistoryPermission> explorer_permission;
};
struct AddressRuntime {bool reveal_selected{},current{},reconciled{};uint64_t generation{},sequence{},history_sequence{};std::string error;bool open_actions{};std::set<std::string> frozen;std::map<std::string,app::CoinRule> policy;std::vector<AddressRow> rows;};
struct CoinRuntime {
    std::optional<app::CoinInventory> inventory;std::optional<RefreshIdentity> identity;
    std::map<std::string,std::string> labels;std::optional<HistoryLabelEditor> editor;
    std::optional<std::pair<std::string,std::string>> label_update;
    uint64_t generation{},sequence{},file_dialog{};int64_t observed{};
    int chart_group{};bool reveal{},chain_current{},open_actions{};uint64_t policy_revision{};std::string error,export_data,export_scope;
};
struct State {
    app::CoinControls coin_controls;
    std::map<uint64_t,std::string> coin_draft_errors;
    std::string history_policy_scope;uint64_t history_policy_revision{},history_policy_generation{},history_policy_sequence{};
    uint64_t registered_session{};
    std::map<uint64_t,HistoryRuntime> histories;
    std::map<uint64_t,AddressRuntime> addresses;
    std::map<uint64_t,CoinRuntime> coins;
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
    ServerPanel server;
    app::RecoverySession* recovery{}; // borrowed owner-thread application service
    imkit::DialogState recovery_dialog;
    bool recovery_prompted{},close_recovery_popup{};
    PrivacyPanel lock_panel;
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
    ServerFacts server;
    std::map<std::string,components::ItemFacts> history;
    std::vector<std::string> history_order;
    std::map<std::string,components::ItemFacts> coin;std::vector<std::string> coin_order;
    std::map<std::string,components::ItemFacts> address;
    std::array<Rect,2> address_tables;
    std::array<size_t,2> address_counts{};

};
ImGuiKeyChord CommandChord(std::string_view,bool macos);
std::string CommandShortcutLabel(std::string_view,bool macos);
app::Availability CommandAvailability(const State&,app::Command,const app::Requests&);
void DrawStatus(State&,Facts&,const imkit::Theme&,float);
void DrawActivityDrawer(State&,Facts&,const imkit::Theme&,float,app::Requests* =nullptr);
void RestoreDocumentFocus(State&,uint64_t desired);
void BeginClose(State&,uint64_t,bool others=false,bool all=false);
void BeginRename(State&,uint64_t);
void BeginDelete(State&,uint64_t);
void BeginImport(State&,std::string_view source={});
void ClosePendingDocumentPopup(State&);
void DrawDocumentDialog(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void DrawRecoveryDialog(State&,Facts&,const imkit::Theme&,float);
bool RequestPrivacyLock(State&);
void DrawPrivacyPanel(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void DrawTabActions(State&,app::Requests&,Facts&,uint64_t,const imkit::Theme&);
void BeginHistoryLabelFrame(State&);
void BeginCoinFrame(State&);
void FinishCoinFrame(State&,app::Requests&);
void DrawCoinDraft(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void DrawCoinContext(State&,app::Requests&,Facts&,const imkit::Theme&,const std::string&);
void DrawAddressContext(State&,app::Requests&,Facts&,const imkit::Theme&,const std::string&);
void DrawCoinActions(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void DrawAddressActions(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void DrawCoins(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void DrawAddresses(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void FinishHistoryLabelFrame(State&,app::Requests&);
void DrawHistory(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void DrawObservedTransaction(State&,Facts&,const imkit::Theme&,float);
std::string SemanticScope(const State&);
Facts DrawWorkspace(State&,app::Requests&,const imkit::Theme&,const BootstrapAppearance&,ImVec2 work_size={});
}
