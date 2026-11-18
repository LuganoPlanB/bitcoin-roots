// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/foundation.h"
#include "platform/native_host.h"
#include "ui/shell/workspace.h"
#include "platform/shell_composition.h"
#include "app/connection_observations.h"
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
ui::shell::Facts DrawShellFrame(NativeHost& host,ui::shell::State& workspace,app::Requests& requests,const imkit::Theme& theme,const ui::BootstrapAppearance& appearance)
{
    const bool privacy_locked=requests.Recovery() && requests.Recovery()->Locked();
    if(auto* connection=requests.Connection()) {
        connection->SetPrivacyLocked(privacy_locked);
        connection->Poll();
        if(auto* refresh=requests.Refresh()) {
            const auto now=RefreshMonotonicMs();refresh->SetOffline(connection->Offline(),now);
            if(privacy_locked){refresh->PrivacyLock();refresh->Tick(now);}
            else if(const auto& active=connection->Active()) {
                std::optional<RefreshIdentity> identity;
                auto* recovery=requests.Recovery();const auto epoch=recovery?recovery->Gate().Epoch():LockEpoch{};
                if(auto binding=app::VerifiedWalletBinding(*active);binding && (!recovery || !recovery->Pending())){
                    const auto* registered=app::RegisteredWallet(workspace.documents,*binding);
                    if(workspace.registered_session!=connection->Session()){
                        if(!registered && workspace.documents.Documents().size()<32){const auto id=workspace.documents.OpenWallet(binding->DaemonWallet().empty()?"Default daemon wallet":binding->DaemonWallet().size()>255?"Daemon wallet "+binding->wallet.value().substr(0,24):binding->DaemonWallet(),*binding,true);auto* document=workspace.documents.Find(id);document->history.enabled=true;for(auto& section:document->sections)section.note=ui::components::Edit("");registered=document;}
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
                if(privacy_locked)addresses->PrivacyLock();
                else if(const auto& active=connection->Active();active && workspace.documents.Active() && workspace.documents.Active()->section==app::Section::Addresses){
                    const auto epoch=requests.Recovery()?requests.Recovery()->Gate().Epoch():LockEpoch{};
                    const auto identity=app::WalletRefreshIdentity(*active,connection->Session(),*workspace.documents.Active(),epoch);
                    if(identity && addresses->Bind(*identity,*active,now)){std::vector<std::string> known;const auto view=refresh->View();if(view.identity && *view.identity==*identity && view.ledger)for(const auto& [txid,tx]:view.ledger->Rows())for(const auto& io:tx.io)if(io.kind=="Output" && !io.destination.empty() && io.destination.size()<=128 && std::all_of(io.destination.begin(),io.destination.end(),[](unsigned char c){return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9');}))known.push_back(io.destination);addresses->SetKnownAddresses(*identity,std::move(known),now);}else addresses->Unbind();
                }else addresses->Unbind();
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
    struct NativeRequests final:app::Requests {
        NativeHost& host;ui::shell::State& state;app::Requests& fallback;
        NativeRequests(NativeHost& h,ui::shell::State& s,app::Requests& f):host(h),state(s),fallback(f){}
        bool CopyRequest(const app::Request& request) const {return request.component && (std::holds_alternative<ui::components::CopyIdentifier>(*request.component) || std::holds_alternative<ui::components::CopyAmount>(*request.component));}
        app::Availability CanSubmit(const app::Request& request) const override {
            if(CopyRequest(request) && fallback.Recovery() && fallback.Recovery()->Locked())return {false,"Workspace is locked."};
            if(!CopyRequest(request))return fallback.CanSubmit(request);
            const auto* identifier=std::get_if<ui::components::CopyIdentifier>(&*request.component);
            const bool sensitive=!identifier || identifier->sensitive;
            const auto* doc=state.documents.Find(request.document);
            const bool available=host.InputState().clipboard && (!request.document || (doc && state.documents.ActiveId()==request.document && doc->observation!=app::ObservationState::Locked)) && (!sensitive || (!state.privacy.hidden && !state.semantic_hide_toggle));
            return {available,available?"":"Native clipboard is unavailable for this view/value."};
        }
        app::Receipt Submit(const app::Request& request) override {
            if(fallback.Recovery() && fallback.Recovery()->Locked() && request.command!=app::Command::Quit)return {false,"Workspace is locked; request discarded."};
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
            return fallback.FeeDraft(request);
        }
        app::Receipt OpenExplorer(const app::ExplorerRequest& request) override {
            if(!request.permission || !CurrentHistoryAction(request.context,request.txid,false,request.generation,request.sequence))return {false,"Explorer request retired; permission and current public selection are required."};
            return fallback.OpenExplorer(request);
        }
        app::HostObservations Observe() const override{return fallback.Observe();}
        ConnectionService* Connection() override{return fallback.Connection();}
        WalletLifecycle* Wallets() override{return fallback.Wallets();}
        TransactionDocuments* LocalDocuments() const override{return fallback.LocalDocuments();}
        RescanService* Rescanning() override{return fallback.Rescanning();}
        ReceiveService* Receiving() override{return fallback.Receiving();}
        AddressService* Addresses() override{return fallback.Addresses();}
        RefreshService* Refresh() override{return fallback.Refresh();}
        app::RecoverySession* Recovery() override{return fallback.Recovery();}
    } native(host,workspace,requests);
    auto facts=ui::shell::DrawWorkspace(workspace,native,theme,appearance);
    if(requests.Recovery() && requests.Recovery()->Locked()){if(auto* connection=requests.Connection())connection->SetPrivacyLocked(true);if(auto* refresh=requests.Refresh())refresh->PrivacyLock();if(auto* addresses=requests.Addresses())addresses->PrivacyLock();if(auto* wallets=requests.Wallets())wallets->Cancel();if(auto* receive=requests.Receiving())receive->Cancel();if(auto* scan=requests.Rescanning())scan->Cancel();if(auto* docs=requests.LocalDocuments())docs->Cancel();}
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
int Run()
{
    NativeOptions options;
    if (const auto* config=std::getenv("XDG_CONFIG_HOME"); config && std::filesystem::path(config).is_absolute())
        options.appearance_file=std::filesystem::path(config)/"roots-gui/appearance";
    else if (const auto* home=std::getenv("HOME"); home && std::filesystem::path(home).is_absolute())
        options.appearance_file=std::filesystem::path(home)/".config/roots-gui/appearance";
    struct ShellRequests final:app::Requests {
        NativeHost* host{};ui::components::SemanticExchange* semantics{};
        app::RecoverySession* recovery{};app::DocumentRegistry* documents{};
        mutable TransactionDocuments local_documents;
        TransactionDocuments* LocalDocuments() const override{return &local_documents;}
        rpc::CookieCredentials credentials;
        rpc::Transport transport{credentials};
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
        RefreshService* Refresh() override{return &refresh;}
        app::RecoverySession* Recovery() override{return recovery;}
        app::Receipt OpenExplorer(const app::ExplorerRequest& request) override {
            if(!request.permission || !documents || (recovery && recovery->Locked()) || connection.Offline())return {false,"External explorer permission is required for this unlocked online view."};
            const auto url=app::ExplorerUrl(request.context.network,request.txid);
            if(!url)return {false,"No external explorer default exists for this exact network. No external request was made."};
            const auto open=ImGui::GetPlatformIO().Platform_OpenInShellFn;
            const bool accepted=open && open(ImGui::GetCurrentContext(),url->c_str());
            return {accepted,accepted?"Browser open requested once; the explorer receives the selected public transaction and browser/network identity.":"Browser open failed. No automatic retry; the selected transaction is retained."};
        }
        app::HostObservations Observe() const override {if(recovery && recovery->Locked()){app::HostObservations value;value.connection={app::ServiceState::Locked,"GUI privacy lock; daemon work already accepted cannot be cancelled remotely."};return value;}return app::ConnectionObservations(connection,refresh);}
        app::Availability CanSubmit(const app::Request& request) const override {if(request.command==app::Command::Refresh && documents && documents->Active() && documents->Active()->section==app::Section::Addresses && !addresses.Available())return {false,"Read-only descriptor inventory is unavailable for this wallet/profile or offline."};return {request.command==app::Command::Quit || request.command==app::Command::Server || request.command==app::Command::Preferences || request.command==app::Command::Introduction || request.command==app::Command::NewWallet || request.command==app::Command::OpenWallet || request.command==app::Command::ExportWallet || (request.command==app::Command::SaveDraft && recovery && recovery->Enabled() && !recovery->Pending() && documents && documents->Find(request.document)) || (request.command==app::Command::Refresh && connection.Active() && !connection.Offline()),app::MissingDependency(request.command)};}
        app::Receipt Submit(const app::Request& request) override {
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
    requests.recovery=&recovery;requests.documents=&workspace.documents;
    if(!recovery.Pending() && !recovery.Locked())ui::shell::BeginOnboarding(workspace,ui::shell::OnboardingPage::Introduction,requests);
    struct SemanticLifetime {ui::components::SemanticExchange& exchange;~SemanticLifetime(){exchange.Close();}} lifetime{workspace.presentation.exchange};
    requests.semantics=&workspace.presentation.exchange;
    options.accessibility=&workspace.presentation.exchange;
    options.before_close=[&workspace] {
        const auto ids=workspace.documents.CloseTargets(workspace.documents.ActiveId(),false,true);
        return ui::shell::AllowPolicyClose(workspace,ids) && ui::shell::SaveReceiveLabelsBeforeClose(workspace,ids);
    };
    NativeHost host(options,nullptr,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance) {
        DrawShellFrame(*requests.host,workspace,requests,theme,appearance);
    });
    requests.host=&host;
    return host.Run();
}
}
