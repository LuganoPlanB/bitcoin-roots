// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/foundation.h"
#include "platform/native_host.h"
#include "platform/linux/camera.h"
#include "ui/shell/workspace.h"
#include "platform/shell_composition.h"
#include "app/connection_observations.h"
#include "app/daemon_signer.h"
#include "app/broadcast.h"
#include "app/history_workspace.h"
#include "services/addresses.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#if !defined(__APPLE__)
#include <imgui_impl_opengl3.h>
#endif
#include <cstdio>
#include <algorithm>
#include <cstdlib>
namespace roots::gui::platform {
app::Availability GuiRestartAvailability(const ui::shell::State& workspace,app::Requests& requests)
{
    if(!RestartSupported())return {false,"Restart requires the Linux GUI process replacement adapter."};
    auto* recovery=requests.Recovery();
    if(workspace.privacy.hidden || workspace.semantic_hide_toggle || (recovery && (recovery->Locked() || recovery->Pending())))
        return {false,"Finish recovery and reveal the workspace before restarting."};
    if(!workspace.documents.Documents().empty() && (!recovery || !recovery->Enabled()))
        return {false,"Enable public workspace recovery or close the open documents before restarting."};
    if(std::ranges::any_of(workspace.documents.Documents(),&app::Document::Dirty)
        || std::ranges::any_of(workspace.settings,[](const auto& item){return item.second.dirty;})
        || std::ranges::any_of(workspace.receives,[](const auto& item){return item.second.label.dirty();})
        || !workspace.transaction_edits.empty() || !workspace.signing_views.empty() || !workspace.offline_builders.empty()
        || workspace.batch || workspace.batch_read)
        return {false,"Save or close the current edits and transaction work before restarting."};
    const auto busy=[](const auto* service){return service && service->Busy();};
    if(workspace.local_document.dialog.open || workspace.local_document.camera_generation
        || busy(requests.Connection()) || busy(requests.Wallets()) || busy(requests.Rescanning())
        || busy(requests.Receiving()) || busy(requests.Funding()) || busy(requests.Fees()) || busy(requests.Refresh())
        || busy(requests.Addresses()) || busy(requests.Devices()) || busy(requests.LocalDocuments()) || busy(requests.Signer())
        || busy(requests.Broadcasts()) || busy(requests.ReplacementDrafts()) || busy(requests.Messages()))
        return {false,"Wait for in-flight GUI operations to settle before restarting."};
    if(auto* camera=requests.Camera()){
        const auto camera_state=camera->View();
        if(camera_state.retiring)return {false,"Wait for camera resources to be released."};
    }
    return {true,{}};
}
std::optional<RestartContext> PrepareGuiRestart(ui::shell::State& workspace,app::Requests& requests,std::string& error)
{
    const auto available=GuiRestartAvailability(workspace,requests);
    error=available.reason;
    if(!available.available)return {};
    RestartContext next;
    if(auto* connection=requests.Connection()){
        next.offline=connection->Offline();
        if(connection->Active())next.profile=connection->Active()->candidate;
        else if(ValidServerCandidate(connection->Draft()))next.profile=connection->Draft();
    }
    if(!EncodeRestartContext(next)){error="The current profile references could not be retained safely.";return {};}
    if(auto* recovery=requests.Recovery();recovery && recovery->Enabled() && !recovery->Checkpoint(workspace.documents)){
        error="The public workspace checkpoint failed. The current window stays open.";return {};
    }
    return next;
}
bool AdoptGuiRestart(ui::shell::State& workspace,app::Requests& requests,const RestartContext& resumed)
{
    if(auto* recovery=requests.Recovery();recovery && (recovery->Locked() || recovery->Pending()))return false;
    auto* connection=requests.Connection();
    if(!connection || connection->Active() || connection->Busy())return false;
    if(resumed.profile){
        if(!connection->SetDraft(*resumed.profile))return false;
        workspace.server.Load(*resumed.profile);
        workspace.server.initialized=true;
    }
    if(connection->Offline()!=resumed.offline && !connection->SetOffline(resumed.offline))return false;
    workspace.notice=resumed.profile?"Restarted GUI. Open Switch Server to review, test and apply the retained RPC fields."
                                    :"Restarted GUI.";
    return true;
}

ui::shell::Facts DrawShellFrame(NativeHost& host,ui::shell::State& workspace,app::Requests& requests,const imkit::Theme& theme,const ui::BootstrapAppearance& appearance)
{
    ui::shell::SyncGeneralPreferences(workspace,requests.Recovery());
    if (workspace.local_document.camera_generation && !glfwGetWindowAttrib(host.Window(),GLFW_FOCUSED)) {
        workspace.notice="Camera closed when the window lost focus.";
        ui::shell::RetireLocalDocument(workspace,requests);
    }
    const bool privacy_locked=requests.Recovery() && requests.Recovery()->Locked();
    if(auto* connection=requests.Connection()) {
        connection->SetPrivacyLocked(privacy_locked);
        connection->Poll();
        if(auto& retained=workspace.retained_address_navigation;retained){
            const auto* document=workspace.documents.Active();
            if(privacy_locked || retained->session!=connection->Session() || !document || document->id!=retained->document
                || document->section!=app::Section::Addresses || document->observation==app::ObservationState::Locked)retained.reset();
        }
        if(auto* refresh=requests.Refresh()) {
            const auto now=RefreshMonotonicMs();refresh->SetOffline(connection->Offline(),now);
            if(privacy_locked){refresh->PrivacyLock();refresh->Tick(now);}
            else if(const auto& active=connection->Active()) {
                std::optional<RefreshIdentity> identity;
                auto* recovery=requests.Recovery();const auto epoch=recovery?recovery->Gate().Epoch():LockEpoch{};
                if(auto binding=app::VerifiedWalletBinding(*active);binding && (!recovery || !recovery->Pending())){
                    const auto* registered=app::RegisteredWallet(workspace.documents,*binding);
                    if(workspace.registered_session!=connection->Session()){
                        if(!registered && workspace.documents.Documents().size()<32){
                            const auto& name=binding->DaemonWallet();
                            const auto id=workspace.documents.OpenWallet(name.empty()?"Default daemon wallet":name.size()>255?"Daemon wallet "+binding->wallet.value().substr(0,24):name,*binding,true);
                            auto* document=workspace.documents.Find(id);
                            if(recovery)recovery->RestoreWalletPresentation(*document);
                            document->history.enabled=true;
                            for(auto& section:document->sections)section.note=ui::components::Edit("");
                            registered=document;
                        }
                        workspace.registered_session=connection->Session();
                    }
                    if(registered)identity=app::WalletRefreshIdentity(*active,connection->Session(),*registered,epoch);
                }else if(active->wallet!=WalletAvailability::Available)identity=app::ConnectionRefreshIdentity(*active,connection->Session());
                if(identity){identity->context.lock_epoch=epoch;refresh->Bind(std::move(*identity),*active,false,now);}
                else refresh->Unbind();
            } else refresh->Unbind();
            if(!privacy_locked)refresh->Tick(now);
            if(auto* addresses=requests.Addresses()){
                addresses->SetOffline(connection->Offline());
                if(privacy_locked){
                    workspace.known_address_source.reset();
                    addresses->PrivacyLock();
                }else if(const auto& active=connection->Active();active && !workspace.retained_address_navigation && workspace.documents.Active() && workspace.documents.Active()->section==app::Section::Addresses){
                    const auto epoch=requests.Recovery()?requests.Recovery()->Gate().Epoch():LockEpoch{};
                    const auto identity=app::WalletRefreshIdentity(*active,connection->Session(),*workspace.documents.Active(),epoch);
                    if(identity && addresses->Bind(*identity,*active,now)){
                        const auto view=refresh->View();
                        const auto* ledger=view.identity && *view.identity==*identity ? view.ledger : nullptr;
                        const auto source=std::pair{*identity,ui::shell::HistoryProjectionKey{
                            ledger,view.generation,view.history_sequence,ledger?ledger->Revision():0}};
                        if (workspace.known_address_source!=source) {
                            std::vector<std::string> known;
                            if(ledger)for(const auto& [txid,tx]:ledger->Rows())for(const auto& io:tx.io)if(io.kind=="Output" && !io.destination.empty() && io.destination.size()<=128 && std::all_of(io.destination.begin(),io.destination.end(),[](unsigned char c){return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9');}))known.push_back(io.destination);
                            addresses->SetKnownAddresses(*identity,std::move(known),now);
                            workspace.known_address_source=source;
                        }
                    }else{
                        workspace.known_address_source.reset();
                        addresses->Unbind();
                    }
                }else{
                    workspace.known_address_source.reset();
                    addresses->Unbind();
                }
                addresses->Tick(now);
            }
        }
    }
    for(auto& drop:host.TakeDroppedPaths()) {
        const auto& area=workspace.native_drop_target;
        if(workspace.actions.kind==ui::shell::DocumentDialog::None && workspace.documents.Documents().empty() && area.w>0 && drop.x>=area.x && drop.x<area.x+area.w && drop.y>=area.y && drop.y<area.y+area.h) {
            if(drop.paths.size()==1) {
                const auto hint=app::ClassifyImportPath(drop.paths.front());
                const bool transaction=hint.formats.size()==1 && (hint.formats.front()==app::ImportFormat::PSBT
                    || hint.formats.front()==app::ImportFormat::RawTransaction);
                if (requests.LocalDocuments() && transaction) {
                    if (!ui::shell::BeginLocalDocument(workspace,requests,ui::shell::LocalDocumentMode::File,drop.paths.front()))
                        workspace.notice="The local document view is busy, or the workspace is hidden.";
                } else ui::shell::BeginImport(workspace,drop.paths.front());
            }
            else workspace.notice="Import one public file at a time; no files were read.";
        }
    }
    // The native facade lives through DrawWorkspace's complete-frame Seal.
    // Native callbacks retain only operation IDs/data, never this facade.
    struct NativeRequests final:app::Requests,app::DesktopActions {
        NativeHost& host;ui::shell::State& state;app::Requests& fallback;
        NativeRequests(NativeHost& h,ui::shell::State& s,app::Requests& f):host(h),state(s),fallback(f){}
        app::DesktopActions* Desktop() override{return host.Desktop()?this:fallback.Desktop();}
        app::DesktopReceipt Available(app::DesktopCapability capability) const override {
            if(capability==app::DesktopCapability::Minimize)return {true,{}};
            if(capability==app::DesktopCapability::Restart){
                if(host.Desktop()->Busy() || host.InputState().dialog_process.size())
                    return {false,"Wait for the current native desktop operation to settle."};
                auto result=fallback.CanSubmit({app::Command::Restart});
                return {result.available,std::move(result.reason)};
            }
            return host.Desktop()->Available(capability);
        }
        app::DesktopReceipt Minimize() override {
            const bool requested=host.Minimize();
            return {requested,requested?"Minimize requested.":"The window could not be minimized."};
        }
        app::DesktopReceipt TestNotification() override{return host.Desktop()->Notify();}
        app::DesktopReceipt OpenHelp(app::HelpTarget target,std::string_view url) override{return host.Desktop()->Open(target,url);}
        app::DesktopReceipt Restart() override {
            const auto available=Available(app::DesktopCapability::Restart);
            if(!available.accepted)return available;
            auto result=fallback.Submit({app::Command::Restart});return {result.accepted,std::move(result.reason)};
        }
        std::string Status() const override{return host.Desktop()->Notice();}
        bool CanRefreshRetainedAddress() const {
            auto* connection=fallback.Connection();
            const auto* document=state.documents.Active();
            if(!connection || !fallback.Addresses() || !connection->Active() || connection->Offline()
                || state.privacy.hidden || state.semantic_hide_toggle || !document || !document->binding
                || document->observation==app::ObservationState::Locked || document->section!=app::Section::Addresses)return false;
            const auto& retained=state.retained_address_navigation;
            return retained && retained->document==document->id && retained->session==connection->Session()
                && app::VerifiedWalletBinding(*connection->Active())==document->binding;
        }
        bool CopyRequest(const app::Request& request) const {return request.component && (std::holds_alternative<ui::components::CopyIdentifier>(*request.component) || std::holds_alternative<ui::components::CopyAmount>(*request.component));}
        bool MetadataRequest(const app::Request& request) const {
            return fallback.Recovery() && (request.command==app::Command::DocumentMetadata || request.command==app::Command::DeleteWallet);
        }
        app::Availability CanChangeMetadata(const app::Request& request) const {
            namespace shell=ui::shell;
            const auto* recovery=fallback.Recovery();
            const auto* wallet=state.documents.Find(request.document);
            if(!recovery || recovery->Locked() || recovery->Pending() || state.privacy.hidden || state.semantic_hide_toggle
                || !wallet || wallet->kind!=app::DocumentKind::Wallet || !wallet->binding
                || wallet->observation==app::ObservationState::Locked || wallet->section!=request.section
                || request.component || request.edit)
                return {false,"Select an unlocked wallet with local metadata."};
            const auto& action=state.actions;
            const bool current_dialog=action.dialog.open && action.target==wallet->id
                && action.metadata_epoch==recovery->Gate().Epoch();
            if(request.command==app::Command::DeleteWallet){
                const bool allowed=current_dialog && action.kind==shell::DocumentDialog::Delete
                    && request.value.empty() && request.option==0 && !shell::BroadcastFreezes(state,wallet->id);
                return {allowed,allowed?"":"Confirm the current wallet's local metadata removal outside an active submission."};
            }
            const bool rename=current_dialog && action.kind==shell::DocumentDialog::Rename
                && request.value==action.name.draft() && request.option==action.icon;
            const bool reset=action.kind==shell::DocumentDialog::None && request.value==wallet->original_name && request.option==0;
            const bool allowed=(rename || reset) && !request.value.empty() && request.value.size()<=255
                && request.option>=0 && request.option<=2;
            return {allowed,allowed?"":"Specify a valid local display name and icon."};
        }
        app::Receipt ChangeMetadata(const app::Request& request) {
            const auto allowed=CanChangeMetadata(request);
            if(!allowed.available)return {false,allowed.reason};
            auto* recovery=fallback.Recovery();
            auto* wallet=state.documents.Find(request.document);
            const bool deleting=request.command==app::Command::DeleteWallet;
            auto default_name=deleting?wallet->original_name:std::string{};
            const auto saved=deleting?recovery->DeleteWalletMetadata(*wallet->binding)
                :recovery->SaveWalletPresentation(*wallet->binding,request.value,request.option);
            if(!saved)return {false,std::string("Local wallet metadata could not be saved: ")+storage::ErrorName(saved.error)+". Previous data retained."};
            if(!deleting)return {true,"Saved the local wallet display name and icon."};
            wallet->name=std::move(default_name);
            wallet->icon=0;
            wallet->history.metadata_loaded=false;
            wallet->coins.metadata_loaded=false;
            if(auto receive=state.receives.find(wallet->id);receive!=state.receives.end() && !receive->second.label.dirty())
                receive->second.label_loaded=false;
            state.coin_controls.InvalidateMetadata(app::HistoryMetadataScope(*wallet->binding));
            ++state.semantic_context_revision;
            return {true,"Local wallet metadata removed. The wallet tab, drafts and daemon wallet were retained."};
        }
        app::Availability CanSubmit(const app::Request& request) const override {
            if(ui::shell::IsSigningCommand(request.command))return ui::shell::SigningCommandAvailability(state,fallback,request);
            if(ui::shell::IsTransactionEditCommand(request.command))return ui::shell::TransactionEditAvailability(state,fallback,request);
            if(MetadataRequest(request))return CanChangeMetadata(request);
            if(request.command==app::Command::Refresh && state.retained_address_navigation)
                return {CanRefreshRetainedAddress(),"Apply this wallet's verified RPC profile to refresh its retained inventory."};
            if(CopyRequest(request) && fallback.Recovery() && fallback.Recovery()->Locked())return {false,"Workspace is locked."};
            if(!CopyRequest(request))return fallback.CanSubmit(request);
            const auto* identifier=std::get_if<ui::components::CopyIdentifier>(&*request.component);
            const bool sensitive=!identifier || identifier->sensitive;
            const auto* doc=state.documents.Find(request.document);
            const bool available=host.InputState().clipboard && (!request.document || (doc && state.documents.ActiveId()==request.document && doc->observation!=app::ObservationState::Locked)) && (!sensitive || (!state.privacy.hidden && !state.semantic_hide_toggle));
            return {available,available?"":"Native clipboard is unavailable for this view/value."};
        }
        app::Receipt Submit(const app::Request& request) override {
            if(ui::shell::IsSigningCommand(request.command))return ui::shell::DispatchSigningCommand(state,fallback,request);
            if(ui::shell::IsTransactionEditCommand(request.command))return ui::shell::DispatchTransactionEdit(state,fallback,request);
            if(MetadataRequest(request))return ChangeMetadata(request);
            if(fallback.Recovery() && fallback.Recovery()->Locked() && request.command!=app::Command::Quit)return {false,"Workspace is locked; request discarded."};
            if(request.command==app::Command::Refresh && state.retained_address_navigation){
                const auto availability=CanSubmit(request);
                if(!availability.available)return {false,availability.reason};
                state.retained_address_navigation.reset();
                return {true,"Read-only address inventory refresh requested."};
            }
            if(!CopyRequest(request))return fallback.Submit(request);
            if(!CanSubmit(request).available)return {false,"Copy unavailable; clipboard unchanged."};
            const auto* identifier=std::get_if<ui::components::CopyIdentifier>(&*request.component);
            const bool copied=identifier?host.Input().Copy(identifier->value,identifier->sensitive):host.Input().Copy(std::get<ui::components::CopyAmount>(*request.component).value.btc(),true);
            return {copied,copied?"Copied to the native clipboard.":host.InputState().notice};
        }
        bool CurrentHistoryAction(const RequestContext& context,const Hash256& txid,bool current,uint64_t generation,uint64_t sequence) const {
            if(state.privacy.hidden || state.semantic_hide_toggle || (fallback.Recovery() && fallback.Recovery()->Locked()) || !fallback.Refresh())return false;
            const auto* doc=state.documents.Active();if(!doc || doc->history.selected!=txid.hex())return false;
            const auto epoch=fallback.Recovery()?fallback.Recovery()->Gate().Epoch():LockEpoch{};
            const auto view=app::HistoryForDocument(*doc,*fallback.Refresh(),epoch);
            return view && view->identity && view->identity->context==context && view->generation==generation && view->history_sequence==sequence && !view->offline && (!current || view->chain_current) && view->ledger->Rows().contains(txid.hex());
        }
        app::Receipt FeeDraft(const app::FeeDraftRequest& request) override {
            if(!CurrentHistoryAction(request.context,request.txid,true,request.generation,request.sequence))return {false,"Fee draft request retired: wallet, chain or privacy state changed."};
            return ui::shell::BeginReplacement(state,fallback,request);
        }
        app::Receipt OpenExplorer(const app::ExplorerRequest& request) override {
            if(!request.permission || !CurrentHistoryAction(request.context,request.txid,false,request.generation,request.sequence))return {false,"Explorer request retired; permission and current public selection are required."};
            return fallback.OpenExplorer(request);
        }
        app::HostObservations Observe() const override{return fallback.Observe();}
        ConnectionService* Connection() override{return fallback.Connection();}
        WalletLifecycle* Wallets() override{return fallback.Wallets();}
        TransactionDocuments* LocalDocuments() const override{return fallback.LocalDocuments();}
        CameraService* Camera() override{return fallback.Camera();}
        DeviceSigners* Devices() override{return fallback.Devices();}
        RescanService* Rescanning() override{return fallback.Rescanning();}
        ReceiveService* Receiving() override{return fallback.Receiving();}
        FundingService* Funding() override{return fallback.Funding();}
        app::DaemonSigner* Signer() override{return fallback.Signer();}
        app::Broadcaster* Broadcasts() override{return fallback.Broadcasts();}
        app::Replacements* ReplacementDrafts() override{return fallback.ReplacementDrafts();}
        FeeService* Fees() override{return fallback.Fees();}
        AddressService* Addresses() override{return fallback.Addresses();}
        RefreshService* Refresh() override{return fallback.Refresh();}
        app::RecoverySession* Recovery() override{return fallback.Recovery();}
        app::MessageSigning* Messages() override{return fallback.Messages();}
    } native(host,workspace,requests);
    auto facts=ui::shell::DrawWorkspace(workspace,native,theme,appearance);
    const bool locked_now=requests.Recovery() && requests.Recovery()->Locked();
    if (privacy_locked || locked_now) ui::shell::RetireDaemonSigning(workspace,requests);
    if (workspace.privacy.hidden || workspace.semantic_hide_toggle || locked_now) {
        if(workspace.message.dialog.open)ui::shell::RetireMessage(workspace,requests);
        if(workspace.download.dialog.open)ui::shell::RetireDownload(workspace);
        if (auto* devices=requests.Devices()) devices->Cancel();
        if (workspace.local_document.camera_generation) ui::shell::RetireLocalDocument(workspace,requests);
        if (auto* submission=requests.Broadcasts()) submission->Cancel();
    }
    if(requests.Recovery() && requests.Recovery()->Locked()){if(auto* connection=requests.Connection())connection->SetPrivacyLocked(true);if(auto* refresh=requests.Refresh())refresh->PrivacyLock();if(auto* addresses=requests.Addresses())addresses->PrivacyLock();if(auto* wallets=requests.Wallets())wallets->Cancel();if(auto* receive=requests.Receiving())receive->Cancel();if(auto* scan=requests.Rescanning())scan->Cancel();if(auto* docs=requests.LocalDocuments())docs->Cancel();if(auto* funding=requests.Funding())funding->Cancel();if(auto* fees=requests.Fees())fees->Cancel();}
    if(auto* desktop=host.Desktop()){
        if(workspace.privacy.hidden || workspace.semantic_hide_toggle || locked_now)desktop->Suspend();
        else{
            uint64_t capture{};
            bool qr_ready{};
            const auto* preferences=workspace.general.saved?&workspace.general.saved->preferences:nullptr;
            if(!preferences || !preferences->desktop_notifications)desktop->CancelNotifications();
            if(preferences && workspace.local_document.camera_generation)if(auto* camera=requests.Camera()){
                const auto view=camera->View();
                if(view.generation==workspace.local_document.camera_generation
                    && (view.stage==CameraStage::Starting || view.stage==CameraStage::Streaming)){
                    capture=view.generation;
                    qr_ready=view.stage==CameraStage::Streaming
                        && (workspace.local_document.camera_result || !workspace.local_document.qr_payment.empty());
                }
            }
            desktop->CameraFeedbackScope(preferences && preferences->desktop_notifications?capture:0);
            desktop->CameraGeneration(preferences && preferences->camera_keep_awake?capture:0);
            if(qr_ready)desktop->CameraReady(capture);
        }
    }
    const auto native_notice=host.InputState().notice;if(!native_notice.empty())workspace.notice=native_notice;
    if(facts.appearance_requested>=0)host.SetAppearance(facts.appearance_requested);
    return facts;
}
std::string BuildInfo()
{
    auto* volatile platform_entry = &ImGui_ImplGlfw_InitForOther;
#if defined(__APPLE__)
    const bool renderer_linked = false; // Metal native host remains follow-on work.
#else
    auto* volatile renderer_entry = &ImGui_ImplOpenGL3_Init;
    const bool renderer_linked = renderer_entry != nullptr;
#endif
    return "\"glfw\":\"" + std::string(glfwGetVersionString()) + "\",\"backend_symbols_linked\":" +
           (platform_entry && renderer_linked ? "true" : "false") + ",\"native_initialized\":false";
}
int Run(std::optional<RestartContext> resumed)
{
    NativeOptions options;
    options.desktop_ports=NativeDesktopPorts();
    if (const auto* config=std::getenv("XDG_CONFIG_HOME"); config && std::filesystem::path(config).is_absolute())
        options.appearance_file=std::filesystem::path(config)/"roots-gui/appearance";
    else if (const auto* home=std::getenv("HOME"); home && std::filesystem::path(home).is_absolute())
        options.appearance_file=std::filesystem::path(home)/".config/roots-gui/appearance";
    struct ShellRequests final:app::Requests {
        NativeHost* host{};ui::components::SemanticExchange* semantics{};
        app::RecoverySession* recovery{};app::DocumentRegistry* documents{};
        ui::shell::State* workspace{};
        std::optional<RestartContext> restart;
        std::function<app::Availability()> restart_available;
        mutable TransactionDocuments local_documents;
        CameraService camera{NativeCamera()};
        CameraService* Camera() override{return &camera;}
        TransactionDocuments* LocalDocuments() const override{return &local_documents;}
        rpc::CookieCredentials credentials;
        rpc::Transport transport{credentials};
        DeviceSigners devices{transport};
        DeviceSigners* Devices() override{return &devices;}
        AddressService addresses{[this](const RefreshIdentity& id,const std::vector<std::string>& known,std::stop_token stop){return FetchAddresses(transport,id,stop,{},known);}};
        AddressService* Addresses() override{return &addresses;}
        RefreshService refresh{[this](const RefreshRequest& value,std::stop_token stop){return FetchRefresh(transport,value,stop);}};
        ConnectionService connection{[this](const ServerCandidate& value,std::stop_token stop){return ProbeCapabilities(transport,value,stop);},[this](bool value){refresh.SetOffline(value,RefreshMonotonicMs());addresses.SetOffline(value);}};
        WalletLifecycle wallets{[this](const WalletOperation& operation,std::stop_token stop){return RunWalletOperation(transport,operation,stop);}};
        ConnectionService* Connection() override{return &connection;}
        WalletLifecycle* Wallets() override{return &wallets;}
        RescanService rescan{[this](const ScanRequest& request,std::stop_token stop){return FetchScan(transport,request,stop);}};
        RescanService* Rescanning() override{return &rescan;}
        ReceiveService receive{[this](const ReceiveRequest& request,std::stop_token stop){return FetchReceive(transport,request,stop);}};
        ReceiveService* Receiving() override{return &receive;}
        FundingService funding{[this](const FundingRequest& request,std::stop_token stop){return FetchFunding(transport,request,stop);}};
        FundingService* Funding() override{return &funding;}
        app::DaemonSigner signer{transport,connection};
        app::DaemonSigner* Signer() override{return &signer;}
        app::MessageSigning messages{transport,connection};
        app::MessageSigning* Messages() override{return &messages;}
        app::Broadcaster broadcast{transport,connection};
        app::Broadcaster* Broadcasts() override{return &broadcast;}
        app::Replacements replacements{transport,connection};
        app::Replacements* ReplacementDrafts() override{return &replacements;}
        FeeService fees{[this](const FeeRequest& request,std::stop_token stop){return FetchFees(transport,request,stop);}};
        FeeService* Fees() override{return &fees;}
        RefreshService* Refresh() override{return &refresh;}
        app::RecoverySession* Recovery() override{return recovery;}
        app::Receipt OpenExplorer(const app::ExplorerRequest& request) override {
            const auto& active=connection.Active();
            if(!request.permission || !documents || !recovery || recovery->Locked() || connection.Offline() || !active
                || active->candidate.connection.proxy!=ProxyKind::Direct || active->genesis!=request.context.network.genesis.hex()
                || request.context.backend.value()!="connection-"+std::to_string(connection.Session()))
                return {false,"Explorer permission requires the current unlocked direct RPC profile."};
            const auto preferences=recovery->LoadWorkspacePreferences();
            const auto url=preferences?app::ExplorerUrl(preferences.value->preferences,request.context.network,request.txid):std::nullopt;
            if(!url || *url!=request.confirmed_url)return {false,"The explorer setting or URL changed. No browser was opened."};
            const auto open=ImGui::GetPlatformIO().Platform_OpenInShellFn;
            const bool accepted=open && open(ImGui::GetCurrentContext(),url->c_str());
            return {accepted,accepted?"Browser open requested once; the explorer receives the selected public transaction and browser/network identity.":"Browser open failed. No automatic retry; the selected transaction is retained."};
        }
        app::HostObservations Observe() const override {if(recovery && recovery->Locked()){app::HostObservations value;value.connection={app::ServiceState::Locked,"GUI privacy lock; daemon work already accepted cannot be cancelled remotely."};return value;}return app::ConnectionObservations(connection,refresh);}
        app::Availability CanSubmit(const app::Request& request) const override {
            if(request.command==app::Command::Restart && restart_available)return restart_available();
            if(request.command==app::Command::Refresh && documents && documents->Active() && documents->Active()->section==app::Section::Addresses && !addresses.Available())return {false,"Read-only descriptor inventory is unavailable for this wallet/profile or offline."};
            return {request.command==app::Command::Quit || request.command==app::Command::Server || request.command==app::Command::Preferences || request.command==app::Command::Introduction || request.command==app::Command::NewWallet || request.command==app::Command::OpenWallet || request.command==app::Command::ExportWallet || (request.command==app::Command::SaveDraft && recovery && recovery->Enabled() && !recovery->Pending() && documents && documents->Find(request.document)) || (request.command==app::Command::Refresh && connection.Active() && !connection.Offline()),app::MissingDependency(request.command)};
        }
        app::Receipt Submit(const app::Request& request) override {
            if(request.command==app::Command::Restart){
                if(!workspace || !host)return {false,"GUI restart is unavailable."};
                std::string error;
                auto next=PrepareGuiRestart(*workspace,*this,error);
                if(!next)return {false,std::move(error)};
                restart=std::move(next);host->RequestClose();
                return {true,"GUI restart requested."};
            }
            if(request.command==app::Command::SaveDraft){const bool ok=CanSubmit(request).available && recovery->Checkpoint(*documents);return {ok,ok?"Public draft saved locally; no transaction action was performed.":"Recovery save failed; document retained."};}
            if(request.command==app::Command::Quit){if(recovery && recovery->Enabled() && !recovery->Locked() && !recovery->Pending() && !recovery->Checkpoint(*documents))return {false,"Recovery save failed; keep this window open and retry."};semantics->Close();host->RequestClose();return {true,{}};}
            if(request.command==app::Command::Refresh)return {documents && documents->Active() && documents->Active()->section==app::Section::Addresses?addresses.RequestNow(RefreshMonotonicMs()):refresh.RequestNow(RefreshMonotonicMs()),connection.Offline()?"Offline mode: refresh is disabled.":"Read-only refresh requested."};
            return {false,app::MissingDependency(request.command)};
        }
    } requests;
    ui::shell::State workspace;
    std::filesystem::path recovery_file;
    if(const auto* data=std::getenv("XDG_DATA_HOME");data && std::filesystem::path(data).is_absolute())recovery_file=std::filesystem::path(data)/"roots-gui/metadata.sqlite";
    else if(const auto* home=std::getenv("HOME");home && std::filesystem::path(home).is_absolute())recovery_file=std::filesystem::path(home)/".local/share/roots-gui/metadata.sqlite";
    app::RecoverySession recovery(recovery_file);recovery.Start();
    requests.recovery=&recovery;requests.documents=&workspace.documents;requests.workspace=&workspace;
    requests.restart_available=[&workspace,&requests]{return GuiRestartAvailability(workspace,requests);};
    if(!resumed && !recovery.Pending() && !recovery.Locked())ui::shell::BeginOnboarding(workspace,ui::shell::OnboardingPage::Introduction,requests);
    struct SemanticLifetime {ui::components::SemanticExchange& exchange;~SemanticLifetime(){exchange.Close();}} lifetime{workspace.presentation.exchange};
    requests.semantics=&workspace.presentation.exchange;
    options.accessibility=&workspace.presentation.exchange;
    options.on_draw_suspended=[&workspace,&requests] {
        if(workspace.message.dialog.open)ui::shell::RetireMessage(workspace,requests);
        if(workspace.sweep.dialog.open)ui::shell::RetireSweepNotice(workspace);
        if(workspace.download.dialog.open)ui::shell::RetireDownload(workspace);
        ui::shell::RetireMetadataBackup(workspace);
        workspace.download_verifier.Poll(nullptr);
        requests.messages.Poll(nullptr);
        if (auto* devices=requests.Devices()) devices->Cancel();
        if (workspace.local_document.camera_generation) ui::shell::RetireLocalDocument(workspace,requests);
    };
    options.before_close=[&workspace,&requests] {
        if(requests.restart){
            const auto available=GuiRestartAvailability(workspace,requests);
            if(!available.available){workspace.desktop.notice=available.reason;requests.restart.reset();return false;}
        }
        const auto ids=workspace.documents.CloseTargets(workspace.documents.ActiveId(),false,true);
        if (!ui::shell::AllowPolicyClose(workspace,ids) || !ui::shell::SaveReceiveLabelsBeforeClose(workspace,ids)){
            if(requests.restart){
                requests.restart.reset();
                workspace.desktop.notice="Current edits could not be acknowledged. Choose restart again after saving them.";
            }
            return false;
        }
        ui::shell::RetireDaemonSigning(workspace,requests);
        if(workspace.message.dialog.open)ui::shell::RetireMessage(workspace,requests);
        if(workspace.sweep.dialog.open)ui::shell::RetireSweepNotice(workspace);
        if(workspace.download.dialog.open)ui::shell::RetireDownload(workspace);
        ui::shell::RetireMetadataBackup(workspace);
        if (workspace.local_document.camera_generation) ui::shell::RetireLocalDocument(workspace,requests);
        if (auto* submission=requests.Broadcasts()) submission->Cancel();
        return true;
    };
    for(;;){
        int result{};
        {
            NativeHost host(options,nullptr,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance) {
                if(resumed && AdoptGuiRestart(workspace,requests,*resumed))resumed.reset();
                DrawShellFrame(*requests.host,workspace,requests,theme,appearance);
            });
            requests.host=&host;
            result=host.Run();
        }
        if(!requests.restart)return result;
        auto restart=std::exchange(requests.restart,std::nullopt);
        const auto failure=ReplaceGui(*restart);
        ui::shell::RetireDesktop(workspace);
        workspace.notice=failure.detail;
    }
}
}
