// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "services/download_verification.h"
#include "adapters/protocol/qr.h"
#include "services/camera.h"
#include "app/replacement.h"
#include "app/signing_review.h"
#include "app/commands.h"
#include "app/wallet_tools.h"
#include "app/desktop.h"
#include "app/message_signing.h"
#include "services/addresses.h"
#include "services/funding.h"
#include "services/fees.h"
#include "services/send_batch.h"
#include "app/coin_inventory.h"
#include "app/coin_controls.h"
#include "ui/bootstrap.h"
#include "ui/server.h"
#include "app/recovery.h"
#include "services/refresh.h"
#include "services/wallet_lifecycle.h"
#include "services/receive.h"
#include "services/rescan.h"
#include "services/transaction_documents.h"
#include <map>
#include <utility>
namespace roots::gui::app { struct DaemonSignResult; }
namespace roots::gui::ui::shell {
struct Rect { float x{},y{},w{},h{}; };
struct Layout { Rect menus,tabs,rail,content,status,accounts;bool compact{}; };
Layout Measure(ImVec2 origin,ImVec2 size,float scale,float menu_height);
enum class DocumentDialog { None,Close,Rename,Delete,Import,ClearSend };
struct DocumentActions {
    DocumentDialog kind{DocumentDialog::None};
    imkit::DialogState dialog;
    uint64_t target{},return_focus{};
    LockEpoch metadata_epoch{};
    DraftRevision send_revision{};
    LockEpoch send_epoch{};
    std::optional<uint64_t> send_owner;
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
enum class WalletTool { None,CurrentSearch,AllSearch,Summary,Node };
struct ToolViewScope {
    uint64_t document{},connection{};
    LockEpoch epoch;
    bool hidden{},offline{},locked{};
    bool operator==(const ToolViewScope&) const=default;
};
struct ToolExport {uint64_t chooser{};ToolViewScope scope;std::string bytes;};
struct RetainedAddressNavigation {uint64_t document{},session{};};
struct WalletToolsPanel {
    WalletTool tool{WalletTool::None};
    imkit::DialogState dialog;
    bool close_pending{},show_wallet{};
    ToolViewScope scope;
    components::Edit search;
    app::SearchType search_type{app::SearchType::Any};
    std::optional<app::WalletSearchQuery> query;
    std::optional<app::WalletSearchResult> results;
    std::optional<size_t> selected;
    ToolExport export_file;
    uint64_t revision{};
    std::string notice;
};
enum class DesktopPage { None, Preferences, Camera, Documentation, Support, Report, About, Restart };
struct DesktopPanel {
    DesktopPage page{DesktopPage::None};
    imkit::DialogState dialog;
    bool close_pending{};
    uint64_t revision{};
    ToolViewScope scope;
    uint64_t profile_revision{};
    std::optional<app::HelpTarget> target;
    std::string confirmed_url, notice;
};
struct MessagePanel {
    imkit::DialogState dialog;
    bool close_pending{},pending{},status_ok{};
    uint64_t revision{},profile_revision{};
    ToolViewScope scope;
    std::optional<app::MessageWalletScope> wallet;
    protocol::MessageFormat format{protocol::MessageFormat::Legacy};
    protocol::MessageNetwork network{protocol::MessageNetwork::Main};
    std::array<char,protocol::MESSAGE_ADDRESS_BYTES+1> address{};
    std::array<char,protocol::MESSAGE_BYTES+1> text{};
    std::array<char,protocol::MESSAGE_SIGNATURE_BYTES+1> signature{};
    std::optional<std::string> external_address,external_text,external_signature;
    std::string status;
};
struct SweepNotice {
    imkit::DialogState dialog;
    bool close_pending{};
    ToolViewScope scope;
    uint64_t profile_revision{};
};
struct PaymentProtocolNotice {
    imkit::DialogState dialog;
    bool close_pending{};
    ToolViewScope scope;
    uint64_t profile_revision{};
};
struct DownloadPanel {
    imkit::DialogState dialog;
    bool close_pending{};
    ToolViewScope scope;
    uint64_t profile_revision{}, revision{}, chooser{};
    size_t choosing{};
    std::array<std::array<char,4097>, size_t(DownloadFile::Count)> paths{};
    std::array<std::optional<std::string>, size_t(DownloadFile::Count)> external_paths;
    std::array<char,41> fingerprint{};
    std::optional<std::string> external_fingerprint;
    std::optional<DownloadRequest> request;
    std::optional<DownloadResult> result;
    const char* notice{};
};
enum class MetadataBackupMode { None, Export, Restore };
struct MetadataBackupPanel {
    MetadataBackupMode mode{MetadataBackupMode::None};
    std::optional<app::WalletBinding> binding;
    ToolViewScope scope;
    uint64_t profile_revision{}, chooser{}, revision{};
    bool initial_focus{};
    std::array<char,4097> path{};
    std::array<char,256> password{}, confirmation{};
    std::optional<std::string> external_path, external_password, external_confirmation;
    std::optional<app::MetadataRestore> inspected;
    std::string notice, restored_name;
    size_t restored_labels{}, restored_freezes{};
};
struct Invocation {app::Command command;uint64_t document{};bool available{},dispatched{},accepted{};std::string reason;};
struct PrivacyPanel {bool setup{},reveal{},focus{true};std::array<char,256> password{},confirm{};std::optional<std::string> external_password,external_confirm;std::string error;};
struct HistoryLabelEditor {
    RefreshIdentity identity;
    std::string txid;
    uint64_t metadata_revision{};
    bool seen{},active{},finished{},attempted{};
};
struct HistoryPermission {RefreshIdentity identity;std::string txid;int64_t observed{};uint64_t generation{},sequence{};std::string url;};
struct GeneralPanel {
    imkit::DialogState dialog;
    bool close_pending{}, initialized{}, hide_not_saved{};
    app::RecoverySession* owner{};
    LockEpoch epoch;
    std::optional<storage::Workspace> saved;
    components::Edit explorer_template;
    std::string error;
};
struct HistoryProjectionKey {
    const HistoryLedger* ledger{};
    uint64_t generation{}, sequence{}, revision{};
    bool operator==(const HistoryProjectionKey&) const = default;
};
struct HistoryRowProjection {
    struct Row {
        const HistoryTransaction* tx;
        const HistoryDetail* detail{};
        const HistoryIO* io{};
    };
    HistoryProjectionKey source;
    uint64_t metadata_revision{};
    std::string filter;
    int sort{};
    bool descending{}, hidden{}, expanded_dirty{};
    std::vector<const HistoryTransaction*> transactions;
    std::vector<std::string> keys;
    std::vector<Row> rows;
    std::map<std::string, size_t> positions;
};
struct HistoryChartProjection {
    HistoryProjectionKey source;
    std::vector<const HistoryTransaction*> transactions;
    std::vector<size_t> geometry;
    std::map<std::string, size_t> positions;
    int64_t low{},high{};
};
struct HistoryRuntime {
    HistoryRowProjection projection;
    HistoryChartProjection chart;
    std::optional<HistoryLabelEditor> editor; // transient activation proof, never recovered
    std::optional<std::pair<std::string,std::string>> label_update; // native buffer sync, never a recovered action
    std::map<std::string,std::string> labels;
    std::string error,export_data,export_scope;
    uint64_t file_dialog{};
    std::optional<RefreshIdentity> export_identity;
    int64_t observed{};
    uint64_t generation{},sequence{};
    bool reveal_selected{},fee_actions{};
    std::string replacement_rate;
    std::optional<app::FeeDraftRequest> cancellation_prompt;
    std::optional<Hash256> child_parent;
    std::optional<uint32_t> child_output;
    std::optional<HistoryPermission> explorer_permission;
};
struct AddressRuntime {bool reveal_selected{},current{},reconciled{};uint64_t generation{},sequence{},history_sequence{};std::string error;bool open_actions{};std::set<std::string> frozen;std::map<std::string,app::CoinRule> policy;std::vector<AddressRow> rows;};
struct CoinRuntime {
    struct Projection {
        bool by_value{},descending{},hidden{};
        std::vector<const app::CoinRow*> rows;
        std::vector<std::string> keys;
        std::map<std::string,size_t> positions;
    };
    std::optional<Projection> projection;
    std::optional<app::CoinSummary> summary;
    std::shared_ptr<const std::vector<app::CoinGroup>> groups;
    std::vector<size_t> group_selection;
    uint64_t selection_revision{};
    bool groups_prepared{},policy_known{};
    std::optional<app::CoinInventory> inventory;std::optional<RefreshIdentity> identity;
    std::map<std::string,std::string> labels;std::optional<HistoryLabelEditor> editor;
    std::optional<std::pair<std::string,std::string>> label_update;
    uint64_t generation{},sequence{},file_dialog{};int64_t observed{};
    int chart_group{};bool reveal{},chain_current{},open_actions{};uint64_t policy_revision{};std::string error,export_data,export_scope;
};
enum class OnboardingPage { None, Introduction, Open, Create, Import, Backup };
enum class WalletImportMethod { Choose, Descriptor, ExtendedKey, Restore, RecoveryWords, Hardware, Foreign };
struct OnboardingPanel {
    OnboardingPage page{OnboardingPage::None};
    imkit::DialogState dialog;
    bool close_pending{}, listed{}, directory_verified{};
    uint64_t revision{}, session{}, profile_revision{}, document{};
    LockEpoch epoch;
    components::Edit name;
    std::vector<DaemonWalletEntry> wallets;
    std::vector<storage::Scope> recent;
    std::optional<DaemonWalletEntry> selected;
    std::string error;
    WalletImportMethod import_method{WalletImportMethod::Choose};
    components::Edit daemon_path;
    std::optional<protocol::PublicWalletPreview> preview;
    bool clipboard_pending{}, path_reviewed{}, policy_review{};
};
enum class ReceiveSource { Daemon, Reservation, Inventory };
struct ReceiveSelection {
    ReceiveTarget target;
    ReceiveSource source;
    bool operator==(const ReceiveSelection&) const = default;
};
struct ReceiveRuntime {
    std::optional<storage::Scope> scope;
    std::optional<ReceiveSelection> selection;
    components::Edit label;
    uint64_t label_revision{}, revision{};
    LockEpoch label_epoch;
    std::optional<ReceiveRequest> inspection;
    std::string status;
    bool label_loaded{}, label_active{}, label_seen_active{};
    bool reusable_view{}, reusable_focus{};
    uint64_t qr_session{},qr_profile{};
    LockEpoch qr_epoch;
    bool qr_uri{};
    std::string qr_payload;
    std::optional<protocol::QrMatrix> qr_matrix;
};
enum class SignerSource { None, Connected, Airgapped, Daemon, WatchOnly, Descriptor };
struct SignerDraft {
    protocol::PublicSigner key;
    SignerSource source;
};
struct SignerClipboard {
    size_t slot{};
    uint64_t revision{}, session{}, profile_revision{};
    LockEpoch epoch;
    SignerSource source;
};
struct PolicySettings {
    std::optional<app::WalletBinding> binding;
    std::optional<SavedWalletPolicy> saved;
    std::optional<protocol::PublicWalletPreview> material, staged;
    std::optional<std::map<size_t,SignerDraft>> signer_drafts;
    std::optional<SignerClipboard> signer_clipboard;
    SignerSource signer_source{SignerSource::None};
    bool inspect_keystores{}, inspect_advanced{}, account_service{};
    std::string signer_error;
    protocol::ScriptTemplate script{protocol::ScriptTemplate::NativeSegwit};
    int required{1}, signers{1};
    size_t saved_branch{}, signer_tab{};
    uint64_t revision{}, saved_session{};
    int64_t saved_observed_ms{};
    bool multisig{}, sorted{true}, dirty{}, clipboard_pending{}, clipboard_change{};
    std::string invalid, status;
};
struct AdvancedSettings {
    std::optional<RefreshIdentity> identity;
    uint64_t profile_revision{}, revision{};
    std::optional<ScanObservation> observation;
    std::optional<ScanReview> review;
    std::optional<PendingRescan> checked_pending;
    ScanRange range;
    bool show_ranges{};
    std::string status;
};
struct InspectionOwner {
    uint64_t id{},session{},profile{};
    DraftRevision draft;
    LockEpoch epoch;
    bool offline{};
    protocol::DisplayNetwork network;
    app::DocumentRepresentation representation;
    std::shared_ptr<const protocol::TransactionDocument> content;
    std::optional<app::WalletBinding> wallet;
    bool operator==(const InspectionOwner&) const=default;
};
struct InspectionExport {
    InspectionOwner owner;
    protocol::InspectionBytes representation;
    uint64_t view_revision{};
    std::optional<protocol::ByteSpan> selection;
};
enum class LocalDocumentMode { None, File, Text, Save, Merge, MergeText, SaveInspection, SaveSigningMaterial, SaveFinalized, BroadcastAttempts,
    Qr, MergeQr, PaymentQr, ShowQr, ShowFinalQr };
struct LocalDocumentPanel {
    LocalDocumentMode mode{LocalDocumentMode::None};
    imkit::DialogState dialog;
    LocalDocumentContext context;
    protocol::DisplayNetwork network{protocol::DisplayNetwork::Unknown};
    std::array<char,4097> path{};
    std::optional<std::string> edited_path;
    components::TextPosition cursor;
    uint64_t file_dialog{}, revision{};
    bool close_pending{},clipboard_pending{},signature_terminal{};
    std::string error;
    std::optional<InspectionOwner> owner;
    std::optional<InspectionExport> inspection;
    std::optional<app::SigningApproval> signature_approval;
    std::shared_ptr<const protocol::TransactionDocument> merge_base;
    std::vector<storage::BroadcastRecord> broadcast_attempts;
    protocol::QrAssembler qr_assembly;
    protocol::QrExport qr_export;
    protocol::QrDensity qr_density{protocol::QrDensity::Standard};
    protocol::QrAssembler::Clock::time_point qr_started{},qr_changed{};
    std::array<char,protocol::MAX_QR_TEXT+1> qr_text{};
    std::optional<std::string> qr_text_edit;
    components::TextPosition qr_cursor;
    std::string qr_payment;
    std::optional<app::WalletBinding> qr_wallet;
    uint64_t qr_session{},qr_profile{},qr_recipient{};
    size_t qr_position{},qr_omitted{};
    bool qr_omit_xpubs{true},qr_force_animation{},qr_paused{};
    uint64_t camera_generation{},camera_sample{};
    std::optional<CameraDevice> camera_device;
    std::optional<CameraMode> camera_mode;
    std::string camera_last_code;
    bool camera_validation{},camera_result_from_stream{};
    std::shared_ptr<const protocol::TransactionDocument> camera_result;
};
struct OfflineBuilder {
    components::Edit address{""}, amount{""}, fee{""};
    std::string error;
    bool open{}, use_change{};
    uint64_t revision{};
};
struct TransactionOverviewProjection {
    std::weak_ptr<const protocol::TransactionDocument> source;
    bool funded{};
    int64_t output_total{},input_total{};
    size_t known_inputs{};
    std::string locktime,input_mix,output_mix;
    std::optional<Hash256> commitment;
};
struct ImportedView {
    struct SpendingProjection {
        const HistoryLedger* ledger{};
        std::optional<RequestContext> context;
        uint64_t generation{},revision{};
        std::optional<Outpoint> point;
        std::optional<Hash256> current;
        std::string status;
    };
    uint64_t revision{};
    TransactionOverviewProjection overview;
    SpendingProjection spending;
    std::optional<app::WalletBinding> wallet;
    std::set<std::string> policy_scripts; // verified public cache only; never recovered as ownership
    std::optional<protocol::OfflineReviewEvidence> review_evidence;
    std::optional<app::ReplacementDraft> replacement;
    std::shared_ptr<const protocol::TransactionDocument> reviewed;
    std::optional<Hash256> review_commitment;
    std::optional<size_t> proved_change_output;
    std::string review_status;
    std::optional<std::pair<uint64_t,DraftRevision>> offline_review_origin;
    std::optional<InspectionOwner> inspection_owner;
    std::weak_ptr<const protocol::TransactionDocument> selected_document;
    protocol::InspectionNode node{protocol::InspectionNode::Overview};
    protocol::InspectionNode tree_group{protocol::InspectionNode::Input};
    protocol::InspectionBytes bytes{protocol::InspectionBytes::Source};
    size_t index{},tree_page{},field_page{},bytes_page{};
    bool hex{};
    std::optional<protocol::ByteSpan> selection;
    std::string selection_label;
    std::string inspection_status;
    struct SignatureView {
        bool open{};
        size_t page{};
        std::weak_ptr<const protocol::TransactionDocument> source;
        std::optional<protocol::SignatureProgress> progress;
    } signatures;
};
struct TransactionEditor {
    InspectionOwner owner;
    uint64_t generation{};
    protocol::InspectionNode node{protocol::InspectionNode::Overview};
    size_t index{};
    components::Edit version,locktime,outpoint,sequence,amount;
    std::vector<char> destination;
    std::string saved_destination;
    std::optional<std::string> external_destination;
    std::string status;
};
struct EditedOrigin {
    uint64_t id{};
    DraftRevision revision;
    std::weak_ptr<const protocol::TransactionDocument> content;
    std::string affected;
    bool fee_replacement{};
};
struct SigningView {
    bool open{},advanced{};
    size_t output_page{},input_page{};
    std::shared_ptr<const protocol::TransactionDocument> daemon_signed,daemon_enriched,external_signed;
    std::weak_ptr<const protocol::TransactionDocument> external_source;
    std::string daemon_status,cancellation_status;
    bool signing_uncertain{};
    std::optional<app::ApprovedFinalTransaction> finalized;
    std::string finalization_error;
};
struct DaemonUnlockView {
    std::optional<app::SigningBasis> owner;
    std::array<char,256> passphrase{};
    ImGuiID field{};
    bool focus{};
    ~DaemonUnlockView();
};
struct SendView {
    uint64_t document{}, recipient{};
    std::array<std::string, 3> edit_values;
    bool wallet_destinations{};
    struct Funds {
        RequestContext context;
        uint64_t generation{}, sequence{}, policy_revision{};
        DraftRevision draft;
        std::optional<Money> value;
    };
    std::optional<Funds> funds;
    struct Totals {
        DraftRevision revision;
        std::optional<Money> eligible;
        SendFundsCheck amounts;
    };
    std::optional<Totals> totals;
};
struct BatchBinding {
    uint64_t document,session,profile_revision;
    DraftRevision revision;
    LockEpoch epoch;
    app::WalletBinding wallet;
    bool offline;
    bool operator==(const BatchBinding&) const=default;
};
struct BatchEditor {
    BatchBinding binding;
    uint64_t generation{},revision{};
    SendRecipients work;
    std::optional<BatchValidation> checked;
    std::optional<std::pair<uint64_t,size_t>> selected_index;
    bool file_panel{};
    size_t page{};
    uint64_t file_dialog{};
    std::array<char,4097> path{};
    std::optional<std::string> path_edit;
    struct External { uint64_t row; RecipientField field; std::string text; };
    std::optional<External> external;
    std::string status;
};
struct FlowView {
    bool open{}, table{}, expanded{}, choose_change{};
    size_t input_first{}, output_first{}, change_index{};
    std::optional<FlowKey> selected;
};
struct FundingView {
    std::optional<FundingScope> scope;
    std::shared_ptr<const FundingPreview> preview;
    FlowView flow;
    std::string rate_shown, status;
    uint64_t document{}, revision{};
    bool expanded{}, creating{};
    std::optional<uint64_t> create_owner;
};
struct FeeView {
    std::optional<FeeContext> owner;
    std::optional<FeeScope> pending, quoted, packaged;
    std::optional<FeeObservation> target, mempool, recent;
    FeeHistory history;
    FeeRead tab{FeeRead::Target};
    int64_t next_sample{};
    size_t sample{}, block{};
    uint64_t revision{};
    int range{};
    bool open{}, recording{}, scheduled{};
    std::string status;
};
struct DraftFeeTerms {
    std::optional<uint32_t> rate;
    int64_t quote_observation{};
    bool below_minimum{};
};
struct State {
    app::CoinControls coin_controls;
    std::map<uint64_t,std::string> coin_draft_errors;
    std::string history_policy_scope;uint64_t history_policy_revision{},history_policy_generation{},history_policy_sequence{};
    uint64_t registered_session{};
    std::map<uint64_t,HistoryRuntime> histories;
    std::map<uint64_t,AddressRuntime> addresses;
    std::map<uint64_t,ReceiveRuntime> receives;
    std::map<uint64_t,PolicySettings> settings;
    std::optional<uint64_t> policy_read;
    std::map<uint64_t,AdvancedSettings> advanced;
    std::optional<std::pair<uint64_t,ScanAction>> pending_scan;
    std::map<uint64_t,CoinRuntime> coins;
    app::DocumentRegistry documents;
    components::AmountPrivacy privacy;
    bool semantic_hide_toggle{};
    components::PresentationFrame presentation;
    std::string notice;
    int unit{};
    uint64_t focus_tab{};
    uint64_t previous_document{};
    std::optional<app::Section> previous_device_section;
    bool previous_locked{};
    bool focus_empty{},reveal_empty_focus{};
    int reveal_navigation{-1};
    DocumentActions actions;
    Rect native_drop_target;
    app::HostObservations observations;
    std::optional<Invocation> invocation;
    Drawer drawer{Drawer::None};
    WalletToolsPanel wallet_tools;
    DesktopPanel desktop;
    MessagePanel message;
    SweepNotice sweep;
    PaymentProtocolNotice payment_protocol;
    DownloadPanel download;
    DownloadVerifier download_verifier;
    MetadataBackupPanel metadata_backup;
    app::ActivityJournal activity_log;
    std::optional<RetainedAddressNavigation> retained_address_navigation;
    std::optional<std::pair<RefreshIdentity,HistoryProjectionKey>> known_address_source;
    uint64_t activity_session{};
    LockEpoch activity_epoch;
    ToolExport activity_export;
    std::string activity_notice;
    bool drawer_focus{},restore_status{};
    uint64_t grid_exits{};
    uint64_t reveal_tab_focus{};
    bool semantic_menu_open{};
    int semantic_menu_request{-1};
    uint64_t close_tab_popup{};
    bool close_document_popup{};
    uint64_t semantic_context{},semantic_context_revision{};
    ServerPanel server;
    GeneralPanel general;
    OnboardingPanel onboarding;
    LocalDocumentPanel local_document;
    uint64_t local_document_generation{};
    std::map<uint64_t,ImportedView> imported_views;
    app::SigningReviews signing_reviews;
    std::map<uint64_t,SigningView> signing_views;
    DaemonUnlockView daemon_unlock;
    uint64_t daemon_operation{};
    std::map<uint64_t,TransactionEditor> transaction_edits;
    std::map<uint64_t,EditedOrigin> edited_origins;
    uint64_t transaction_edit_generation{}, transaction_edit_revision{};
    uint64_t signing_revision{};
    std::map<uint64_t,OfflineBuilder> offline_builders;
    SendView send_view;
    FundingView funding;
    FeeView fees;
    std::optional<BatchEditor> batch;
    std::optional<BatchBinding> send_analysis;
    std::unique_ptr<BatchFileRead> batch_read;
    uint64_t batch_generation{},batch_read_generation{},batch_read_revision{};
    app::RecoverySession* recovery{}; // borrowed owner-thread application service
    app::Broadcaster* broadcasts{}; // borrowed owner-thread application service
    imkit::DialogState recovery_dialog;
    bool recovery_prompted{},close_recovery_popup{};
    PrivacyPanel lock_panel;
};
struct FrameWork {
    size_t history_indexed{}, history_flattened{}, history_drawn{}, history_chart_indexed{}, history_chart_drawn{};
    size_t coins_adopted{}, coins_indexed{}, coins_drawn{}, coins_summarized{}, coin_chart_indexed{};
    size_t batch_validated{}, batch_drawn{}, send_counted{}, psbt_overview_items{}, psbt_hex_bytes{};
};
struct Facts {
    FrameWork work;
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
    bool preferences_visible{};
    std::map<std::string,components::ItemFacts> preferences;
    std::map<std::string,components::ItemFacts> tools;
    bool tools_visible{};
    bool desktop_visible{};
    std::map<std::string,components::ItemFacts> desktop;
    bool message_visible{};
    std::map<std::string,components::ItemFacts> message;
    bool sweep_visible{};
    components::ItemFacts sweep_close;
    bool payment_protocol_visible{};
    bool download_visible{};
    std::map<std::string,components::ItemFacts> download;
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
    std::map<std::string,components::ItemFacts> onboarding,local_document,transaction_document;
    std::map<std::string,components::ItemFacts> history;
    // Borrowed projection identifiers, valid until the next workspace draw.
    std::span<const std::string> history_order;
    std::map<std::string,components::ItemFacts> coin;std::span<const std::string> coin_order;
    std::map<std::string,components::ItemFacts> address,receive,settings,advanced;
    std::map<std::string,components::ItemFacts> send;
    std::map<std::string,components::ItemFacts> batch;
    std::map<std::string,components::ItemFacts> funding;
    std::map<std::string,components::ItemFacts> flow;
    std::map<std::string,components::ItemFacts> fee;
    std::array<Rect,2> address_tables;
    std::array<size_t,2> address_counts{};

};
ImGuiKeyChord CommandChord(std::string_view,bool macos);
std::string CommandShortcutLabel(std::string_view,bool macos);
app::Availability CommandAvailability(const State&,app::Command,app::Requests&);
void DrawStatus(State&,Facts&,const imkit::Theme&,float);
void DrawActivityDrawer(State&,Facts&,const imkit::Theme&,float,app::Requests* =nullptr);
void RestoreDocumentFocus(State&,uint64_t desired);
bool BeginClose(State&,uint64_t,bool others=false,bool all=false);
void BeginRename(State&,uint64_t);
void BeginDelete(State&,uint64_t);
void BeginImport(State&,std::string_view source={});
void ClosePendingDocumentPopup(State&);
void DrawDocumentDialog(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void DrawRecoveryDialog(State&,app::Requests&,Facts&,const imkit::Theme&,float);
bool OpenMetadataBackup(State&,app::Requests&,MetadataBackupMode);
void RetireMetadataBackup(State&);
bool CanOpenMetadataBackup(const State&);
bool DrawMetadataBackup(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void SyncGeneralPreferences(State&,app::RecoverySession*);
void ApplyGeneralPresentation(State&);
void OpenGeneralPreferences(State&);
void CloseGeneralPreferences(State&);
bool SetAmountVisibility(State&,bool hidden);
bool SetAmountUnit(State&,int unit);
bool SetDecimalPresentation(State&,bool trim);
bool SetAddressGroupingDefault(State&,bool group);
bool SetDesktopPreference(State&,bool PublicPreferences::*,bool value);
DesktopPage DesktopPageFor(app::Command);
bool OpenDesktop(State&,app::Requests&,DesktopPage);
void RetireDesktop(State&);
void DrawDesktop(State&,app::Requests&,Facts&,const imkit::Theme&,float);
bool OpenMessage(State&,app::Requests&);
void RetireMessage(State&,app::Requests&);
void DrawMessage(State&,app::Requests&,Facts&,const imkit::Theme&,float);
bool OpenSweepNotice(State&,app::Requests&);
void RetireSweepNotice(State&);
void DrawSweepNotice(State&,app::Requests&,Facts&,const imkit::Theme&,float);
bool OpenPaymentProtocolNotice(State&,app::Requests&);
void RetirePaymentProtocolNotice(State&);
void DrawPaymentProtocolNotice(State&,app::Requests&,Facts&,const imkit::Theme&,float);
bool OpenDownload(State&,app::Requests&);
void RetireDownload(State&);
void DrawDownload(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void DrawGeneralPreferences(State&,app::Requests&,Facts&,const imkit::Theme&,const AppearanceStatus&,float);
ToolViewScope CurrentToolScope(const State&,app::Requests&);
void OpenWalletTool(State&,app::Requests&,WalletTool);
void RetireWalletTool(State&);
void DrawWalletTools(State&,app::Requests&,Facts&,const imkit::Theme&,float);
bool BeginToolExport(ToolExport&,const ToolViewScope&,std::string bytes);
void PollToolExport(ToolExport&,const ToolViewScope&,const std::function<std::string()>& current,std::string& notice);
bool OpenLocalData(State&,std::shared_ptr<const protocol::TransactionDocument>,protocol::DisplayNetwork,std::string& error);
void DrawOfflineBuilder(State&,app::Requests&,Facts&,const imkit::Theme&);
void DrawSendRecipients(State&,app::Requests&,Facts&,const imkit::Theme&,float);
bool DrawBatchOrAnalysis(State&,app::Requests&,Facts&,const imkit::Theme&);
void PollBatch(State&,app::Requests&);
void RetireBatch(State&);
void RetireSendView(State&);
void DrawFunding(State&,app::Requests&,Facts&,const imkit::Theme&);
std::shared_ptr<const FundingPreview> CurrentFundingPreview(State&,app::Requests&);
void DrawTransactionFlow(State&,app::Requests&,Facts&,const imkit::Theme&,std::function<bool()> current);
void PollFunding(State&,app::Requests&);
void RetireFunding(State&);
const app::Document* ActiveSendDraft(const State&);
void DrawFeePanel(State&,app::Requests&,Facts&,const imkit::Theme&);
void PollFees(State&,app::Requests&);
void RetireFees(State&);
void SuspendFeeRecording(State&,app::Requests&);
DraftFeeTerms FeeTerms(const State&, const app::Document&, const FeeContext&, std::string_view tip);
bool BeginClearSend(State&,uint64_t);
bool CanClearSend(const State&,uint64_t);
bool ConfirmClearSend(State&,app::Requests&);
bool DrawQrMatrix(const protocol::QrMatrix&,float maximum);
void RetireLocalDocument(State&,app::Requests&);
bool BeginLocalDocument(State&,app::Requests&,LocalDocumentMode,std::string_view local_path={},bool collect=false);
void DrawLocalDocumentDialog(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void DrawImportedDocument(State&,app::Requests&,Facts&,const imkit::Theme&,float);
std::optional<InspectionOwner> CurrentInspectionOwner(const State&,app::Requests&);
bool ValidInspectionExport(const State&,app::Requests&,const InspectionExport&);
bool ToggleTransactionHex(State&,app::Requests&);
bool IsSigningCommand(app::Command);
bool BroadcastFreezes(const State&,uint64_t document);
void PollBroadcast(State&,app::Requests&);
app::Availability ReplacementAvailability(State&,app::Requests&,const app::FeeDraftRequest&);
app::Receipt BeginReplacement(State&,app::Requests&,const app::FeeDraftRequest&);
void PollReplacement(State&,app::Requests&);
void DrawBroadcastStatus(State&,app::Requests&,Facts&,const imkit::Theme&);
app::Availability SigningCommandAvailability(State&,app::Requests&,const app::Request&);
app::Receipt DispatchSigningCommand(State&,app::Requests&,const app::Request&);
const app::SigningApproval* CurrentSigningApproval(State&,app::Requests&,uint64_t,bool completing_exchange=false);
std::shared_ptr<const protocol::TransactionDocument> SigningMaterial(const State&,uint64_t);
const app::ApprovedFinalTransaction* FinalizedMaterial(const State&,uint64_t);
const protocol::SignatureProgress& SignatureProgressFor(State&,uint64_t,
    const std::shared_ptr<const protocol::TransactionDocument>&);
void DrawSignatureExchange(State&,app::Requests&,Facts&,const imkit::Theme&,const InspectionOwner&,bool collect);
void PollSigningReviews(State&,app::Requests&);
// Adopt a worker reply already matched to the open view and exact approval.
void CollectDaemonSigningResult(State&,app::Requests&,const app::SigningApproval&,app::DaemonSignResult);
void RetireDaemonSigning(State&,app::Requests&);
bool CloseSigningReview(State&,app::Requests&,uint64_t);
void RevokeSigningReview(State&,uint64_t,std::string);
bool DrawSigningReview(State&,app::Requests&,Facts&,const imkit::Theme&);
bool IsTransactionEditCommand(app::Command);
app::Availability TransactionEditAvailability(State&,app::Requests&,const app::Request&);
app::Receipt DispatchTransactionEdit(State&,app::Requests&,const app::Request&);
bool DrawTransactionEditor(State&,app::Requests&,Facts&,const imkit::Theme&);
void PollTransactionEditors(State&,app::Requests&);
void DrawTransactionWorkbench(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void BeginOnboarding(State&,OnboardingPage,app::Requests&);
void DrawOnboarding(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void DrawPolicySettings(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void PollPolicySettings(State&,app::Requests&);
void PollAdvancedSettings(State&,app::Requests&);
void DrawAdvancedSettings(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void DrawAccountService(State&,app::Requests&,Facts&,const imkit::Theme&);
void PolicyWalletApplied(State&,uint64_t previous,uint64_t created,const protocol::PublicWalletPreview&,uint64_t session);
bool AllowPolicyClose(State&,std::span<const uint64_t>);
void RetireOnboarding(State&,app::Requests&);
bool RequestPrivacyLock(State&,app::Requests&);
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
void PollReceive(State&,app::Requests&);
void DrawDeviceSigner(State&,app::Requests&,std::map<std::string,components::ItemFacts>&,const imkit::Theme&,bool blocked);
void BeginReceiveLabelFrame(State&);
void FinishReceiveLabelFrame(State&);
bool SaveReceiveLabelsBeforeClose(State&,std::span<const uint64_t>);
bool HasReceiveLabelError(const State&,uint64_t except);
void DrawReceive(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void DrawReceiveTo(State&,app::Requests&,Facts&,const imkit::Theme&,const AddressView&,std::string_view key);
void FinishHistoryLabelFrame(State&,app::Requests&);
void DrawHistory(State&,app::Requests&,Facts&,const imkit::Theme&,float);
void DrawObservedTransaction(State&,Facts&,const imkit::Theme&,float);
std::string SemanticScope(const State&);
Facts DrawWorkspace(State&,app::Requests&,const imkit::Theme&,const BootstrapAppearance&,ImVec2 work_size={});
}
