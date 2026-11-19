// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/localization.h"
#include "ui/shell/workspace.h"
#include "app/history_workspace.h"
#include "ui/theme/theme.h"
#include <algorithm>
#include <charconv>

namespace roots::gui::ui::shell {
namespace {
namespace c = components;
using c::ax::SemanticAction;
using c::ax::SemanticRole;

const app::Document* ActiveWallet(const State& state)
{
    const auto* doc = state.documents.Active();
    return doc && doc->binding && doc->kind == app::DocumentKind::Wallet && doc->section == app::Section::Settings
        && doc->observation != app::ObservationState::Locked && (!state.recovery || !state.recovery->Locked()) ? doc : nullptr;
}
bool Viewing(const State& state, const app::Document& doc)
{
    const auto view = state.settings.find(doc.id);
    return view != state.settings.end() && view->second.inspect_advanced && !state.onboarding.dialog.open
        && !state.server.dialog.open && !state.privacy.hidden && !state.semantic_hide_toggle;
}
std::optional<RefreshIdentity> Identity(const State& state, app::Requests& host, const app::Document& doc)
{
    const auto* connection = host.Connection();
    if (!Viewing(state,doc) || !connection || connection->Offline() || connection->PrivacyLocked()
        || !connection->Active() || (state.recovery && state.recovery->Pending())) return {};
    return app::WalletRefreshIdentity(*connection->Active(),connection->Session(),doc,
        state.recovery ? state.recovery->Gate().Epoch() : LockEpoch{});
}
void SetStatus(AdvancedSettings& runtime, std::string status)
{
    runtime.status = std::move(status);
    ++runtime.revision;
}
std::string BlockRange(ScanRange range)
{
    return std::to_string(range.first) + " to " + std::to_string(range.last);
}
template <class Action>
void Button(State& state, Facts& facts, const imkit::Theme& theme, const char* id, const char* title,
    bool enabled, Action action, bool primary = false)
{
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    const bool pressed=theme::ActionButton(title,primary ? imkit::ActionVariant::Primary : imkit::ActionVariant::Secondary,{},theme);
    const auto lo=ImGui::GetItemRectMin(), hi=ImGui::GetItemRectMax();
    facts.advanced[id]={{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),!enabled,pressed,pressed,ImGui::IsItemVisible()};
    const auto external=state.presentation.Interact(title,SemanticRole::Button,SemanticAction::Press|SemanticAction::Focus,!enabled);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted = pressed && action();
    if (external) state.presentation.Defer(*external,[action,enabled,pressed,accepted](const auto&){
        return enabled && (pressed ? accepted : action());
    });
}
void Text(State& state, const char* id, const char* name, const std::string& value, SemanticRole role = SemanticRole::Text)
{
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
    state.presentation.Annotate(id,name,value,role);
}
void Height(State& state, AdvancedSettings& runtime, const char* label, int64_t ScanRange::*member, bool enabled, float scale)
{
    auto& height=runtime.range.*member;
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(132*scale);
    ImGui::BeginDisabled(!enabled);
    if (ImGui::InputScalar("##height",ImGuiDataType_S64,&height)) {
        runtime.review.reset();
        ++runtime.revision;
    }
    const auto external=state.presentation.Interact(label,SemanticRole::TextField,SemanticAction::SetValue|SemanticAction::Focus,
        !enabled,false,std::to_string(height));
    ImGui::EndDisabled();
    ImGui::PopID();
    if (external) state.presentation.Defer(*external,[&runtime,member,enabled](const auto& request){
        if (!enabled) return false;
        if (request.action == SemanticAction::Focus) return true;
        const auto& text=request.value;
        int64_t value{};
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
        if (parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size()) return false;
        runtime.range.*member=value;
        runtime.review.reset();
        ++runtime.revision;
        return true;
    });
}

bool Start(State& state, app::Requests& host, uint64_t id, ScanAction action)
{
    const auto* doc=ActiveWallet(state);
    auto* scans=host.Rescanning();
    auto* connection=host.Connection();
    if (!doc || doc->id!=id || !scans || scans->Busy() || (host.Wallets() && host.Wallets()->Busy()) || !connection) return false;
    auto& advanced=state.advanced.at(id);
    const auto identity=Identity(state,host,*doc);
    if (!identity || identity!=advanced.identity || connection->Revision()!=advanced.profile_revision) return false;
    const auto& caps=*connection->Active();
    if (!CanObserveScan(caps)) return false;
    const auto scope=app::HistoryMetadataScope(*doc->binding);
    std::optional<PendingRescan> pending;
    if (state.recovery) {
        const auto stored=state.recovery->PendingScan(scope);
        if (!stored) {
            SetStatus(advanced,std::string("Local scan state unavailable: ")+storage::ErrorName(stored.error)+'.');
            if (action!=ScanAction::Check) return false;
        } else pending=*stored.value;
    }
    if ((action==ScanAction::Review || action==ScanAction::Start)
        && (!CanStartScan(caps) || !advanced.observation || !state.recovery || pending
            || !ValidateScanRange(*advanced.observation,advanced.range).empty())) return false;
    ScanRequest request{action,*identity,connection->Revision(),pending ? pending->ticket : 0,advanced.range,{},
        CanReadWalletPolicy(caps)};
    RescanService::Lease lease;
    if (action==ScanAction::Start || action==ScanAction::Acknowledge) {
        if (!state.recovery) return false;
        auto guard=state.recovery->RescanLease();
        if (!guard) {
            SetStatus(advanced,std::string("Could not acquire the local scan guard: ")+storage::ErrorName(guard.error)+'.');
            return false;
        }
        lease=std::move(*guard.value);
        if (action==ScanAction::Start) {
            auto* starting=state.documents.Find(id);
            if (!advanced.review || advanced.review->range!=advanced.range
                || starting->history.query_revision==std::numeric_limits<uint64_t>::max()) return false;
            request.reviewed_block=advanced.review->last_block;
            const auto saved=state.recovery->BeginScan(scope,advanced.range);
            if (!saved) {
                SetStatus(advanced,std::string("Scan was not submitted: ")+storage::ErrorName(saved.error)+". Check scan state.");
                return false;
            }
            request.ticket=*saved.value;
            // The daemon may discover older transactions. Retire the existing
            // history cursor before dispatch, including for uncertain outcomes.
            request.identity.context.revision.value=++starting->history.query_revision;
            advanced.identity=request.identity;
        } else {
            const auto latest=state.recovery->PendingScan(scope);
            if (!pending || !latest || *latest.value!=pending || pending!=advanced.checked_pending
                || !advanced.observation || advanced.observation->scanning) return false;
        }
    }
    const auto ticket=request.ticket;
    if (!scans->Start(std::move(request),*connection,lease)) {
        const bool cleared=action!=ScanAction::Start || bool(state.recovery->FinishScan(scope,ticket));
        SetStatus(advanced,cleared?"No request was dispatched.":"No request was dispatched. Check the retained scan record.");
        return false;
    }
    advanced.checked_pending.reset();
    advanced.review.reset();
    state.pending_scan=std::pair{id,action};
    SetStatus(advanced,action==ScanAction::Start ? "Waiting for the daemon's rescan result…" : "Checking the authenticated wallet…");
    return true;
}
void DrawObservations(State& state, Facts& facts, const imkit::Theme& theme, AdvancedSettings& runtime)
{
    const auto& info=*runtime.observation;
    Text(state,"scan-birth","Daemon wallet birth time","Wallet birth time   "+(info.birthtime ? locale::LocalDate(*info.birthtime,true) : std::string("Unknown")));
    const auto count=[](auto n){return n ? std::to_string(*n) : std::string("Unknown");};
    Text(state,"scan-keypool","Daemon keypool counts",
        "Pregenerated keys   Receive "+count(info.receive_keys)+"  ·  Change "+count(info.change_keys));
    if (!info.active_ranges) Text(state,"range-unknown","Active descriptor ranges","Active descriptor ranges are unavailable.");
    else if (info.active_ranges->empty()) Text(state,"range-empty","Active descriptor ranges","No active ranged descriptors reported.");
    else {
        Button(state,facts,theme,"descriptor-ranges",runtime.show_ranges ? "Hide descriptor ranges" : "Show descriptor ranges",
            true,[&runtime] {runtime.show_ranges=!runtime.show_ranges;++runtime.revision;return true;});
        if (runtime.show_ranges) {
            for (size_t index=0;index<info.active_ranges->size();++index) {
                const auto& range=info.active_ranges->at(index);
                const auto name="Active descriptor "+std::to_string(index+1);
                Text(state,("descriptor-range-"+std::to_string(index)).c_str(),name.c_str(),name+"   "+std::to_string(range.first)
                    +" to "+std::to_string(range.last)+"  ·  next index "+std::to_string(range.next));
            }
        }
    }
    Text(state,"wallet-height","Wallet block height","Wallet processed through block "+std::to_string(info.wallet_height)
        +"  ·  Node tip "+std::to_string(info.chain_height));
    if (info.pruned)
        Text(state,"prune-height","Pruned history", "This node retains blocks from "+std::to_string(info.first_available)
            +". Use a backend that retains any earlier blocks you need.",SemanticRole::Alert);
    if (info.scanning) Text(state,"scan-running","Daemon scan activity","The daemon reports a scan in progress.");
}
}

void PollAdvancedSettings(State& state, app::Requests& host)
{
    std::erase_if(state.advanced,[&](const auto& item){return !state.documents.Find(item.first);});
    const auto* doc=ActiveWallet(state);
    const auto identity=doc ? Identity(state,host,*doc) : std::nullopt;
    for (auto& [id,runtime]:state.advanced) {
        const auto current=doc && id==doc->id ? identity : std::nullopt;
        if (current!=runtime.identity || (current && runtime.profile_revision!=host.Connection()->Revision())) {
            const auto revision=runtime.revision+1;
            runtime=AdvancedSettings{current,current ? host.Connection()->Revision() : 0,revision};
        }
    }
    auto* scans=host.Rescanning();
    if (!scans || !state.pending_scan) return;
    const auto [id,action]=*state.pending_scan;
    const auto current=identity && doc && doc->id==id;
    if (!current) scans->Cancel();
    const auto completion=scans->Poll(host.Connection(),current ? identity : std::nullopt);
    if (!scans->Busy()) state.pending_scan.reset();
    if (!current || !completion) return;
    auto& runtime=state.advanced.at(id);
    const auto& reply=completion->reply;
    const auto& request=completion->request;
    const auto scope=app::HistoryMetadataScope(*doc->binding);
    if (reply.observation) {
        const bool new_range=!runtime.observation;
        // Review and start omit descriptor inventory. Keep the last explicit
        // public inventory read under this same connection identity.
        auto ranges=runtime.observation && action!=ScanAction::Check ? std::move(runtime.observation->active_ranges) : std::nullopt;
        runtime.observation=reply.observation;
        if (ranges) runtime.observation->active_ranges=std::move(ranges);
        if (new_range && action==ScanAction::Check)
            runtime.range={reply.observation->first_available,reply.observation->wallet_height};
    } else runtime.observation.reset();
    runtime.review=reply.review;
    std::string message=reply.error;
    if (action==ScanAction::Check && reply.observation) {
        const auto saved=state.recovery ? state.recovery->PendingScan(scope) : storage::Result<std::optional<PendingRescan>>{};
        if (saved && *saved.value && (*saved.value)->ticket==request.ticket) runtime.checked_pending=*saved.value;
        message=reply.observation->scanning ? "Checked now. The wallet reports a scan in progress."
                                           : "Checked now. The wallet reports no scan in progress.";
    }
    if (action==ScanAction::Start) {
        if (reply.complete && message.empty()) {
            message="Daemon confirmed blocks "+BlockRange(request.range)+". Coverage applies to this range only.";
            // Rebind bounded observations to the same wallet and a fresh
            // query identity. The old cursor cannot imply coverage of new hits.
            auto* target=state.documents.Find(id);
            if (target->history.query_revision<std::numeric_limits<uint64_t>::max()) {
                ++target->history.query_revision;
                runtime.identity=Identity(state,host,*target);
            }
        } else if (reply.last_scanned)
            message+=" Last confirmed height: "+std::to_string(*reply.last_scanned)+'.';
    }
    const bool settle=(action==ScanAction::Start && (reply.complete || !reply.dispatched))
        || (action==ScanAction::Acknowledge && reply.observation && message.empty());
    if (settle && !state.recovery->FinishScan(scope,request.ticket)) {
        if (!message.empty()) message+=' ';
        message+="Scan record could not be cleared. Check scan state before proceeding.";
    } else if (settle && action==ScanAction::Acknowledge)
        message="Previous scan outcome remains unknown. The pending record was acknowledged; no scan was sent.";
    SetStatus(runtime,std::move(message));
}

void DrawAdvancedSettings(State& state, app::Requests& host, Facts& facts, const imkit::Theme& theme, float scale)
{
    const auto* doc=ActiveWallet(state);
    if (!doc) return;
    const auto id=doc->id;
    auto& settings=state.settings.at(id);
    auto& runtime=state.advanced[id];
    const auto identity=Identity(state,host,*doc);
    const auto* caps=identity && host.Connection()->Active() ? &*host.Connection()->Active() : nullptr;
    const bool busy=host.Rescanning()->Busy();
    const bool belongs=busy && state.pending_scan && state.pending_scan->first==id;
    const bool readable=caps && runtime.identity==identity && runtime.profile_revision==host.Connection()->Revision()
        && CanObserveScan(*caps) && !busy && !host.Wallets()->Busy();
    const auto scope=app::HistoryMetadataScope(*doc->binding);
    const auto saved=state.recovery && !state.privacy.hidden ? state.recovery->PendingScan(scope)
        : storage::Result<std::optional<PendingRescan>>{};
    const bool pending=saved && saved.value->has_value();
    const bool startable=readable && saved && !pending && CanStartScan(*caps)
        && runtime.observation && ValidateScanRange(*runtime.observation,runtime.range).empty();
    Button(state,facts,theme,"back","Settings",true,[&settings]{settings.inspect_advanced=false;++settings.revision;return true;});
    ImGui::SameLine();
    Button(state,facts,theme,"check","Check scan state",readable,[&state,&host,id]{return Start(state,host,id,ScanAction::Check);});
    if (belongs) {
        ImGui::SameLine();
        Button(state,facts,theme,"cancel","Stop waiting",true,[&state,&host,&runtime,id]{
            if (!state.pending_scan || state.pending_scan->first!=id) return false;
            const bool sent=state.pending_scan->second==ScanAction::Start;
            host.Rescanning()->Cancel();
            SetStatus(runtime,sent?"Stopped waiting. The daemon may still run or have partially scanned. Check scan state."
                                 :"Stopped waiting for the check.");
            return true;
        });
    }
    const auto wallet=doc->binding->DaemonWallet();
    Text(state,"advanced-wallet","Advanced wallet settings",state.privacy.hidden ? "Activity hidden"
        : "Advanced  ·  "+(wallet.empty() ? std::string("Default daemon wallet") : wallet));
    if (!runtime.status.empty() && !state.privacy.hidden)
        Text(state,"scan-status","Rescan status",runtime.status,SemanticRole::Status);
    if (!caps) Text(state,"scan-connection","Connection requirement","Connect to this daemon wallet to check or rescan it.");
    else if (!CanStartScan(*caps)) Text(state,"scan-capability","Rescan capability","This connection does not prove both rescan height arguments.");
    if (!saved && !state.privacy.hidden) Text(state,"scan-journal","Local scan record","A writable local metadata store is required to start a rescan.");
    if (pending && !belongs) {
        Text(state,"scan-pending","Pending rescan","Previous request for blocks "+BlockRange((**saved.value).range)
            +" is unconfirmed. Check scan state before proceeding.",SemanticRole::Alert);
        if (runtime.checked_pending==*saved.value && runtime.observation && !runtime.observation->scanning) {
            Text(state,"scan-pending-consequence","Acknowledgment consequence",
                 "The wallet is idle now. The previous scan may still start or have left partial history.");
            Button(state,facts,theme,"acknowledge","Acknowledge unknown result",readable,[&state,&host,id] {
                return Start(state,host,id,ScanAction::Acknowledge);
            });
        }
    }
    if (!runtime.observation || state.privacy.hidden) return;
    if (!pending) {
        if (!runtime.observation->scanning && ImGui::BeginTable("scan-bounds",2,ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Start block");
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("End block");
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            Height(state,runtime,"Start block",&ScanRange::first,readable,scale);
            ImGui::TableNextColumn();
            Height(state,runtime,"End block",&ScanRange::last,readable,scale);
            ImGui::EndTable();
        }
        if (runtime.review) {
            Text(state,"scan-review","Reviewed rescan range","Scan blocks "+BlockRange(runtime.review->range)
                +" for this wallet. The daemon may add matching transactions and take time to finish.");
            Button(state,facts,theme,"confirm","Start wallet rescan",startable,[&state,&host,id]{
                return Start(state,host,id,ScanAction::Start);
            },true);
        } else {
            Button(state,facts,theme,"review","Review rescan",startable,[&state,&host,id]{
                return Start(state,host,id,ScanAction::Review);
            });
            const auto invalid=ValidateScanRange(*runtime.observation,runtime.range);
            if (!invalid.empty()) Text(state,"scan-range-error","Scan range validation",invalid,SemanticRole::Alert);
        }
    }
    ImGui::Separator();
    DrawObservations(state,facts,theme,runtime);
    Text(state,"advanced-daemon-policy","Birth, gap and range ownership",
        "This rescan takes block heights. The daemon reports keypool counts but exposes no gap limit here. "
        "Set timestamps and derivation ranges when importing a supported public policy.");
}

void DrawAccountService(State& state, app::Requests& host, Facts& facts, const imkit::Theme& theme)
{
    const auto* doc=ActiveWallet(state);
    if (!doc) return;
    auto& settings=state.settings.at(doc->id);
    Button(state,facts,theme,"account-back","Settings",true,[&settings]{settings.account_service=false;++settings.revision;return true;});
    Text(state,"account-service","Add Account","A real account service is required to add or manage accounts.");
    Text(state,"wallet-model","Wallet workspace model","Each tab opens one daemon wallet.");
    const bool wallet_routes=host.Connection() && host.Wallets() && !host.Wallets()->Busy() && !state.onboarding.dialog.open;
    Button(state,facts,theme,"account-new","New Wallet",wallet_routes,[&state,&host]{
        BeginOnboarding(state,OnboardingPage::Create,host);return state.onboarding.page==OnboardingPage::Create;
    });
    ImGui::SameLine();
    Button(state,facts,theme,"account-open","Open Wallet",wallet_routes,[&state,&host]{
        BeginOnboarding(state,OnboardingPage::Open,host);return state.onboarding.page==OnboardingPage::Open;
    });
}
}
