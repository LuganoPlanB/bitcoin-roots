// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"

#include <algorithm>
#include <charconv>
#include <imkit/charts.h>
#include <numeric>
#include <set>

namespace roots::gui::ui::shell {
namespace {
namespace c = components;
using c::ax::SemanticAction;
using c::ax::SemanticRole;

struct Access {
    FeeScope scope;
    FeeSupport support;
};
std::optional<Access> Current(const State& state, app::Requests& host)
{
    const auto* draft = ActiveSendDraft(state);
    const auto* connection = host.Connection();
    const auto* refresh = host.Refresh();
    if (!draft || !state.recovery || !connection || connection->Offline() || connection->PrivacyLocked()
        || !connection->Active() || !refresh) return {};
    const auto* wallet = app::RegisteredWallet(state.documents, *draft->SendWallet());
    if (!wallet) return {};
    const auto identity = app::WalletRefreshIdentity(*connection->Active(), connection->Session(), *wallet,
        state.recovery->Gate().Epoch());
    const auto view = refresh->View();
    if (!identity || !view.identity || *view.identity != *identity || !view.node || !view.chain_current
        || view.freshness[0] != Freshness::Live || view.offline || view.locked || view.node->initial_download) return {};
    std::set<std::string> unconfirmed;
    if (view.ledger && view.freshness[2] == Freshness::Live && draft->selected_coins.size() <= 64) {
        for (const auto& point : draft->selected_coins) {
            const auto coin = std::ranges::find(view.ledger->Coins(), point, &HistoryCoin::outpoint);
            if (coin == view.ledger->Coins().end() || coin->confirmations < 0) {
                unconfirmed.clear();
                break;
            }
            if (coin->confirmations == 0) unconfirmed.insert(point.txid.hex());
        }
    }
    return Access{{{*identity, connection->Revision(), draft->id}, draft->draft_revision, view.node->tip,
        draft->funding_target, {unconfirmed.begin(), unconfirmed.end()}}, SupportedFees(*connection->Active())};
}
const FeeObservation* Live(const std::optional<FeeObservation>& value, std::string_view tip, int64_t now)
{
    return value && value->tip == tip && FeeObservationLive(*value,now) ? &*value : nullptr;
}
const FeeObservation* LatestPolicy(const FeeView& view, std::string_view tip, int64_t now)
{
    const auto* target = Live(view.target,tip,now);
    const auto* mempool = Live(view.mempool,tip,now);
    if (target && !target->policy) target = nullptr;
    if (mempool && !mempool->policy) mempool = nullptr;
    return mempool && (!target || mempool->monotonic_ms >= target->monotonic_ms) ? mempool : target;
}
const FeeObservation* CurrentQuote(const FeeView& view, const app::Document& draft, const FeeContext& owner,
    std::string_view tip,int64_t now)
{
    return view.quoted && view.quoted->owner==owner && view.quoted->draft==draft.draft_revision
        && view.quoted->target==draft.funding_target ? Live(view.target,tip,now) : nullptr;
}
void Changed(State& state) { ++state.fees.revision; }

bool SetTarget(State& state, uint64_t id, uint16_t target)
{
    const auto* draft = ActiveSendDraft(state);
    if (!draft || draft->id != id || target > MAX_FEE_TARGET || draft->funding_target == target
        || draft->draft_revision.value == UINT64_MAX || state.actions.kind != DocumentDialog::None) return false;
    auto* edit = state.documents.Active();
    edit->funding_target = target;
    ++edit->draft_revision.value;
    Changed(state);
    return true;
}
bool Read(State& state, app::Requests& host, uint64_t id, FeeRead read, bool scheduled = false)
{
    auto* service = host.Fees();
    const auto access = Current(state,host);
    if (!service || service->Busy() || !access || access->scope.owner.document != id
        || !access->support.Allows(read) || !state.fees.open || state.actions.kind != DocumentDialog::None
        || state.recovery_dialog.open || state.local_document.dialog.open) return false;
    if (!service->Start({access->scope,read, access->support})) return false;
    state.fees.pending = access->scope;
    state.fees.scheduled = scheduled;
    state.fees.status = "Reading the connected node.";
    Changed(state);
    return true;
}
void Text(State& state, const char* key, const char* label, std::string_view text)
{
    ImGui::TextWrapped("%.*s", int(text.size()), text.data());
    state.presentation.Annotate(key,label,text);
}
void Button(State& state, Facts& facts, const imkit::Theme& theme, const char* key,
    const char* label, bool enabled, const std::function<bool()>& action)
{
    ImGui::PushID(key);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = theme::ActionButton(label,imkit::ActionVariant::Secondary,{},theme);
    const auto lo=ImGui::GetItemRectMin(), hi=ImGui::GetItemRectMax();
    facts.fee[key]={{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),!enabled,
        pressed,pressed,ImGui::IsItemVisible()};
    const auto request=state.presentation.Interact(label,SemanticRole::Button,
        SemanticAction::Press | SemanticAction::Focus,!enabled);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted=pressed && enabled && action();
    if (request) state.presentation.Defer(*request,[enabled,pressed,accepted,action](const auto&){
        return enabled && (pressed ? accepted : action());
    });
}
bool PanelFor(const State& state, uint64_t id)
{
    const auto* draft = ActiveSendDraft(state);
    return draft && draft->id == id && state.fees.open && state.actions.kind == DocumentDialog::None;
}
std::string RateLabel(const std::optional<uint32_t>& rate)
{
    return rate ? FeeRateText(*rate) + " sat/vB" : "Unknown";
}
void DrawPolicy(State& state, const FeeObservation* observed)
{
    if (!observed || !observed->policy) {
        Text(state,"fee-policy","Fee policy","Relay minimum and mempool minimum: Unknown");
        return;
    }
    const auto& policy = *observed->policy;
    Text(state,"fee-policy","Fee policy","Local relay minimum " + RateLabel(policy.relay)
        + " · Mempool minimum " + RateLabel(policy.mempool) + " · Replacement increment " + RateLabel(policy.incremental));
    Text(state,"fee-policy-source","Policy source","getmempoolinfo · " + ObservationDate(observed->observed_ms));
}
void TargetPanel(State& state, Facts& facts, const imkit::Theme& theme, const app::Document& draft,
    const std::optional<Access>& access, int64_t now)
{
    const auto id = draft.id;
    const bool wide=ImGui::GetContentRegionAvail().x>=ImGui::GetFontSize()*32;
    Button(state,facts,theme,"manual","Manual mode",draft.funding_target != 0,
        [&state,id]{ return SetTarget(state,id,0); });
    ImGui::SameLine();
    Button(state,facts,theme,"target","Target Blocks mode",draft.funding_target == 0,
        [&state,id]{ return SetTarget(state,id,6); });
    if (draft.funding_target) {
        if (wide) ImGui::SameLine();
        int target = draft.funding_target;
        ImGui::SetNextItemWidth(ImGui::GetFontSize()*5);
        if (ImGui::InputInt("Target blocks",&target,0) && target > 0 && target <= MAX_FEE_TARGET)
            SetTarget(state,id,uint16_t(target));
        const auto number = state.presentation.Interact("Target blocks",SemanticRole::TextField,
            SemanticAction::SetValue | SemanticAction::Focus,false,false,std::to_string(draft.funding_target));
        if (number) state.presentation.Defer(*number,[&state,id](const auto& action) {
            uint16_t value{};
            const auto* end = action.value.data()+action.value.size();
            const auto parsed=std::from_chars(action.value.data(),end,value);
            return parsed.ec==std::errc{} && parsed.ptr==end && value>0 && SetTarget(state,id,value);
        });
        const auto* quote = access ? CurrentQuote(state.fees,draft,access->scope.owner,access->scope.tip,now) : nullptr;
        const auto requested = std::to_string(draft.funding_target);
        if (quote) {
            const auto actual = !quote->quote.blocks ? "Unknown"
                : *quote->quote.blocks == 0 ? "0 (no usable target)" : std::to_string(*quote->quote.blocks);
            Text(state,"fee-quote","Target estimate","Requested " + requested + " blocks · Returned " + actual
                + " · Estimate " + RateLabel(quote->quote.rate));
            Text(state,"fee-quote-source","Estimate source","estimatesmartfee (BTC/kvB) · " + ObservationDate(quote->observed_ms));
            if (!quote->quote.estimator.empty()) Text(state,"fee-estimator","Node estimator",quote->quote.estimator);
            if (!quote->quote.error.empty()) Text(state,"fee-quote-error","Estimate error",quote->quote.error);
        } else {
            const bool stale=access && state.fees.target && state.fees.target->observed_ms>0 && state.fees.quoted
                && state.fees.quoted->owner==access->scope.owner && state.fees.quoted->target==draft.funding_target
                && !Live(state.fees.target,access->scope.tip,now);
            Text(state,"fee-quote","Target estimate","Requested " + requested + " blocks · Estimate Unknown. "
                + (stale ? "Previous estimatesmartfee observation is stale." : "Read current fee data."));
        }
    }
    const auto terms = access ? FeeTerms(state,draft,access->scope.owner,access->scope.tip)
        : DraftFeeTerms{EffectiveFundingRate(draft.funding_rate,draft.funding_target,nullptr)};
    if (!draft.funding_rate.empty()) {
        Text(state,"fee-effective","Effective draft rate",terms.rate ? "Manual override " + RateLabel(terms.rate)
            : "Manual rate is invalid. Enter an exact sat/vB rate in Funding preview.");
    } else Text(state,"fee-effective","Effective draft rate","Effective draft rate: " + RateLabel(terms.rate));
    if (terms.below_minimum) Text(state,"fee-below-minimum","Fee below policy","Draft rate is below the observed node minimum.");
    DrawPolicy(state,access ? LatestPolicy(state.fees,access->scope.tip,now) : nullptr);
    Text(state,"fee-next-block","Next-block estimate","Next-block estimate unavailable. No provider is configured.");
    if (draft.funding_target)
        Text(state,"fee-target-limits","Estimate limitations","The requested target is an estimate. Block inclusion is uncertain.");
}

constexpr std::array<const char*,6> BAND_LABELS{"<1","1–<2","2–<5","5–<10","10–<25","≥25"};
void DrawBands(State& state, const imkit::Theme& theme, const FeeBandSnapshot& bands)
{
    std::array<imkit::ChartDatum, BAND_LABELS.size()> data{};
    std::string text;
    for (size_t i=0; i<data.size(); ++i) {
        data[i] = {BAND_LABELS[i], float(bands.vbytes[i])};
        if (!text.empty()) text += " · ";
        text += BAND_LABELS[i];
        text += " sat/vB: " + std::to_string(bands.vbytes[i]) + " vB";
    }
    imkit::BarChart("mempool-bands",{data},{.size={0,ImGui::GetFrameHeight()*2.2f},.values=false},{.theme=&theme});
    Text(state,"fee-band-values","Mempool bands by virtual bytes",text);
}
void DrawTrend(const imkit::Theme& theme, std::span<const FeeSample> samples, int64_t now, int range)
{
    const auto start = now - int64_t(range)*3600000;
    uint64_t maximum{};
    for (const auto& sample : samples) {
        if (sample.monotonic_ms >= start && sample.monotonic_ms <= now && sample.bands)
            maximum = std::max(maximum,std::accumulate(sample.bands->vbytes.begin(),sample.bands->vbytes.end(),uint64_t{}));
    }
    const auto origin = ImGui::GetCursorScreenPos();
    const ImVec2 size{std::max(1.f,ImGui::GetContentRegionAvail().x),ImGui::GetFrameHeight()*2.6f};
    ImGui::Dummy(size);
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin,{origin.x+size.x,origin.y+size.y},ImGui::GetColorU32(ImGuiCol_FrameBg));
    if (!maximum) return;
    for (const auto& sample : samples) {
        if (sample.monotonic_ms < start || sample.monotonic_ms > now || !sample.bands) continue;
        const float x = origin.x + size.x*float(sample.monotonic_ms-start)/float(now-start);
        const float width = std::max(1.f,size.x*float(FEE_SAMPLE_INTERVAL)/float(now-start)*.72f);
        const float left=std::max(origin.x,x-width*.5f), right=std::min(origin.x+size.x,x+width*.5f);
        float y=origin.y+size.y;
        for (size_t i=0; i<sample.bands->vbytes.size(); ++i) {
            const float h = size.y*float(sample.bands->vbytes[i])/float(maximum);
            if (h > 0) draw->AddRectFilled({left,y-h},{right,y},
                ImGui::ColorConvertFloat4ToU32(imkit::ChartColor(&theme,i)));
            y-=h;
        }
    }
}
void MempoolPanel(State& state, app::Requests& host, Facts& facts, const imkit::Theme& theme, uint64_t id,
    const std::optional<Access>& access, int64_t now)
{
    auto& view=state.fees;
    const bool can_read=access && access->support.mempool;
    const bool wide=ImGui::GetContentRegionAvail().x>=ImGui::GetFontSize()*31;
    const auto* observation=access ? Live(view.mempool,access->scope.tip,now) : nullptr;
    Text(state,"fee-sample-coverage","Mempool sample coverage",
        "Retained samples record this node's observed fee conditions. Indexed address or output history requires a separate service.");
    Button(state,facts,theme,"record",view.recording ? "Stop recording" : "Record every 5 minutes",can_read || view.recording,
        [&state,&host,id] {
            if (!PanelFor(state,id)) return false;
            auto& model=state.fees;
            if (model.recording && model.scheduled) {
                host.Fees()->Cancel();
                model.pending.reset();
                model.scheduled=false;
                model.status.clear();
            } else if (!model.recording) {
                const auto current=Current(state,host);
                if (!current || !current->support.mempool) return false;
                model.next_sample=RefreshMonotonicMs();
            }
            model.recording=!model.recording;
            Changed(state);
            return true;
        });
    const auto select=[&state,id](int hours) {
        if (!PanelFor(state,id) || (hours && !state.fees.history.Coverage(RefreshMonotonicMs(),hours).sufficient)) return false;
        state.fees.range=hours;
        state.fees.sample=0;
        Changed(state);
        return true;
    };
    const auto short_range=view.history.Coverage(now,2), long_range=view.history.Coverage(now,24);
    const int range = !access || (view.range==2 && !short_range.sufficient) || (view.range==24 && !long_range.sufficient)
        ? 0 : view.range;
    if (wide) ImGui::SameLine();
    Button(state,facts,theme,"latest","Latest",range!=0,[select]{return select(0);});
    ImGui::SameLine();
    Button(state,facts,theme,"two-hours","2 hours",access && short_range.sufficient && range!=2,[select]{return select(2);});
    ImGui::SameLine();
    Button(state,facts,theme,"day","24 hours",access && long_range.sufficient && range!=24,[select]{return select(24);});
    const auto coverage=[](FeeCoverage value) {
        return std::to_string(value.samples) + (value.samples==1 ? " sample, " : " samples, ")
            + std::to_string(value.gaps) + (value.gaps==1 ? " gap" : " gaps")
            + (value.sufficient ? " · Available" : " · Insufficient");
    };
    Text(state,"fee-coverage","Recorded history coverage","2 h: " + coverage(short_range) + " / 24 h: " + coverage(long_range));
    if (!access) return;
    if (range) {
        const auto start=now-int64_t(range)*3600000;
        std::vector<const FeeSample*> usable;
        for (const auto& sample : view.history.Samples())
            if (sample.monotonic_ms>=start && sample.monotonic_ms<=now && sample.bands) usable.push_back(&sample);
        DrawTrend(theme,view.history.Samples(),now,range);
        if (!usable.empty()) {
            const auto offset=std::min(view.sample,usable.size()-1);
            const auto* sample=usable.at(usable.size()-1-offset);
            Button(state,facts,theme,"older","Earlier sample",offset+1<usable.size(),[&state,id,offset]{
                if (!PanelFor(state,id)) return false;
                state.fees.sample=offset+1; Changed(state); return true;
            });
            ImGui::SameLine();
            Button(state,facts,theme,"newer","Later sample",offset>0,[&state,id,offset]{
                if (!PanelFor(state,id) || !offset) return false;
                state.fees.sample=offset-1; Changed(state); return true;
            });
            Text(state,"fee-mempool-source","Selected mempool sample","getrawmempool · " + ObservationDate(sample->observed_ms));
            DrawBands(state,theme,*sample->bands);
        }
    } else if (observation && observation->mempool) {
        Text(state,"fee-mempool-source","Mempool snapshot source","getrawmempool · "
            + ObservationDate(observation->observed_ms) + " · Base fee bands by virtual bytes");
        DrawBands(state,theme,observation->mempool->bands);
    } else Text(state,"fee-bands-unknown","Mempool fee bands","Mempool bands: Unknown. Read the current node or start recording.");
    if (range) return;
    if (observation && observation->mempool && observation->mempool->ancestors && view.packaged == access->scope) {
        const auto& package=*observation->mempool->ancestors;
        Text(state,"fee-package","Known selected ancestor package","Selected ancestor package: " + std::to_string(package.sats)
            + " sats / " + std::to_string(package.vbytes) + " vB · " + std::to_string(package.transactions) + " unique transactions");
    } else Text(state,"fee-package","Selected ancestor package","Selected ancestor package rate: Unknown.");
}
void RecentPanel(State& state, Facts& facts, const imkit::Theme& theme, uint64_t id,
    const std::optional<Access>& access, int64_t now)
{
    const auto* current = access ? Live(state.fees.recent,access->scope.tip,now) : nullptr;
    if (!current || current->recent.empty()) {
        Text(state,"fee-recent-unknown","Recent blocks","Recent block summaries: Unknown. Read the active chain.");
        return;
    }
    Text(state,"fee-recent-source","Block source","getblockstats · " + ObservationDate(current->observed_ms)
        + " · Weight-based median sat/vB");
    std::vector<std::string> labels;
    labels.reserve(current->recent.size());
    for (const auto& row : current->recent) labels.push_back(std::to_string(row.height));
    std::vector<imkit::ChartDatum> rates;
    for (size_t i=0; i<labels.size(); ++i) if (const auto rate=current->recent[i].median_rate)
        rates.push_back({labels[i].c_str(),float(*rate)});
    if (!rates.empty()) imkit::BarChart("recent-block-fees",{rates},
        {.size={0,ImGui::GetFrameHeight()*2.3f},.values=false},{.theme=&theme});
    const auto selected = std::min(state.fees.block,current->recent.size()-1);
    const auto& block=current->recent[selected];
    Button(state,facts,theme,"older-block","Earlier block",selected+1<current->recent.size(),[&state,id,selected]{
        if (!PanelFor(state,id)) return false;
        state.fees.block=selected+1; Changed(state); return true;
    });
    ImGui::SameLine();
    Button(state,facts,theme,"newer-block","Later block",selected>0,[&state,id,selected]{
        if (!PanelFor(state,id) || !selected) return false;
        state.fees.block=selected-1; Changed(state); return true;
    });
    Text(state,"fee-block","Selected recent block","Block " + std::to_string(block.height) + " · "
        + ObservationDate(block.time*1000) + " · " + std::to_string(block.transactions) + " transactions including coinbase"
        + " · Total fees " + std::to_string(block.fees) + " sats · Median "
        + (block.median_rate ? std::to_string(*block.median_rate) + " sat/vB" : "Unknown"));
    Text(state,"fee-block-hash","Selected block hash",block.hash);
    if (!current->error.empty()) Text(state,"fee-block-gap","Block coverage",current->error);
}
}

DraftFeeTerms FeeTerms(const State& state, const app::Document& draft, const FeeContext& owner,std::string_view tip)
{
    const auto now=RefreshMonotonicMs();
    const auto& view=state.fees;
    const auto* quote = CurrentQuote(view,draft,owner,tip,now);
    const auto rate=EffectiveFundingRate(draft.funding_rate,draft.funding_target,quote ? &quote->quote : nullptr);
    const auto* policy = view.owner==owner ? LatestPolicy(view,tip,now) : nullptr;
    return {rate, draft.funding_rate.empty() && quote ? quote->monotonic_ms : 0,
        rate && policy && !MeetsFeeMinimum(*rate,*policy->policy)};
}
void RetireFees(State& state)
{
    const auto revision=state.fees.revision;
    state.fees={};
    state.fees.revision=revision+1;
}
void SuspendFeeRecording(State& state,app::Requests& host)
{
    auto& view=state.fees;
    if (!view.open) return;
    if (auto* service=host.Fees(); service && view.scheduled) {
        service->Cancel();
        view.pending.reset();
        view.scheduled=false;
        view.status.clear();
    }
    view.open=false;
    view.recording=false;
    Changed(state);
}
void PollFees(State& state, app::Requests& host)
{
    auto* service=host.Fees();
    if (!service) return;
    auto& view=state.fees;
    const auto access=Current(state,host);
    const auto scope=access ? std::optional(access->scope) : std::nullopt;
    const auto now=RefreshMonotonicMs();
    const auto wall_now=[] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    };
    if (scope && view.owner && view.owner!=scope->owner) {
        const bool open=view.open;
        RetireFees(state);
        view.open=open;
    }
    if (scope) view.owner=scope->owner;
    if (!scope && view.recording) {
        view.recording=false;
        Changed(state);
    }
    if (view.pending && view.pending!=scope) {
        if (view.scheduled && scope && view.pending->owner==scope->owner) {
            view.history.Append({wall_now(),now,{}});
        }
        view.pending.reset();
        view.scheduled=false;
        view.next_sample=now+FEE_SAMPLE_INTERVAL;
        view.status="Observation retired. The draft, wallet or chain changed.";
        Changed(state);
    }
    if (auto done=service->Poll(scope); done && scope && view.pending==scope) {
        view.status=done->error;
        switch (done->read) {
        case FeeRead::Target:
            view.quoted=scope;
            view.target=std::move(done);
            break;
        case FeeRead::Mempool: {
            view.history.Append({done->observed_ms > 0 ? done->observed_ms : wall_now(),
                done->monotonic_ms > 0 ? done->monotonic_ms : now,
                done->mempool ? std::optional(done->mempool->bands) : std::nullopt});
            view.packaged=scope;
            view.next_sample=std::max(now,done->monotonic_ms)+FEE_SAMPLE_INTERVAL;
            view.mempool=std::move(done);
            break;
        }
        case FeeRead::Recent:
            view.block=0;
            view.recent=std::move(done);
            break;
        }
        view.pending.reset();
        view.scheduled=false;
        Changed(state);
    }
    if (scope && view.recording && view.open && now>=view.next_sample && !service->Busy())
        Read(state,host,scope->owner.document,FeeRead::Mempool,true);
}
void DrawFeePanel(State& state, app::Requests& host, Facts& facts, const imkit::Theme& theme)
{
    const auto* draft=ActiveSendDraft(state);
    auto* service=host.Fees();
    if (!draft || !service) return;
    const auto id=draft->id;
    auto& view=state.fees;
    ImGui::PushID("fees");
    Button(state,facts,theme,"open",view.open ? "Hide fees" : "Fee observations",true,[&state,&host,id]{
        const auto* current=ActiveSendDraft(state);
        if (!current || current->id!=id || state.actions.kind!=DocumentDialog::None) return false;
        auto& model=state.fees;
        model.open=!model.open;
        if (!model.open) {
            model.recording=false;
            if (model.pending) host.Fees()->Cancel();
            model.pending.reset();
            model.scheduled=false;
            model.status.clear();
        }
        Changed(state);
        return true;
    });
    if (view.open) {
        const auto access=Current(state,host);
        const bool wide=ImGui::GetContentRegionAvail().x>=ImGui::GetFontSize()*32;
        const auto tab=[&](const char* key,const char* label,FeeRead read){
            Button(state,facts,theme,key,label,view.tab!=read,[&state,id,read]{
                if (!PanelFor(state,id)) return false;
                state.fees.tab=read; Changed(state); return true;
            });
        };
        tab("mode-tab","Fee target",FeeRead::Target);
        ImGui::SameLine();
        tab("mempool-tab","Mempool samples",FeeRead::Mempool);
        ImGui::SameLine();
        tab("blocks-tab","Recent blocks",FeeRead::Recent);
        if (wide) ImGui::SameLine();
        Button(state,facts,theme,"read","Read fee data",access && access->support.Allows(view.tab) && !service->Busy(),
            [&state,&host,id,read=view.tab]{ return Read(state,host,id,read); });
        if (view.pending) {
            if (!wide) ImGui::SameLine();
            Button(state,facts,theme,"cancel","Cancel fee read",true,[&state,&host,id]{
                if (!PanelFor(state,id) || !state.fees.pending) return false;
                host.Fees()->Cancel();
                state.fees.pending.reset();
                state.fees.recording=false;
                state.fees.scheduled=false;
                state.fees.status="Fee read cancelled.";
                Changed(state);
                return true;
            });
        }
        if (!access) Text(state,"fee-unavailable","Connected fee source","Fee data: Unknown. A current verified wallet and chain are required.");
        else if (!access->support.Allows(view.tab))
            Text(state,"fee-unavailable","Connected fee source","The required fee observation RPC is unavailable on this node.");
        if (!view.status.empty()) Text(state,"fee-status","Fee observation status",view.status);
        const auto now=RefreshMonotonicMs();
        switch (view.tab) {
        case FeeRead::Target: TargetPanel(state,facts,theme,*draft,access,now); break;
        case FeeRead::Mempool: MempoolPanel(state,host,facts,theme,id,access,now); break;
        case FeeRead::Recent: RecentPanel(state,facts,theme,id,access,now); break;
        }
    }
    ImGui::PopID();
}
}
