// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "services/custody_extensions.h"
#include "app/history_workspace.h"
#include "ui/theme/theme.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <sodium.h>
#include <chrono>

namespace roots::gui::ui::shell {
namespace {
namespace c = components;
using c::ax::SemanticAction;
using c::ax::SemanticRole;
using protocol::PolicyChoice;
using protocol::PublicWalletPreview;
using protocol::ScriptTemplate;

constexpr std::array<const char*,4> SCRIPTS{"Native SegWit", "Nested SegWit", "Legacy", "Taproot key path"};

LockEpoch Epoch(const State& state)
{
    return state.recovery ? state.recovery->Gate().Epoch() : LockEpoch{};
}

const app::Document* Wallet(const State& state, uint64_t id)
{
    const auto* doc = state.documents.Find(id);
    return doc && doc->binding && doc->kind == app::DocumentKind::Wallet && doc->observation != app::ObservationState::Locked
        && (!state.recovery || !state.recovery->Locked()) ? doc : nullptr;
}

bool Viewing(const State& state, uint64_t id)
{
    const auto* doc = Wallet(state,id);
    return doc && state.documents.ActiveId() == id && doc->section == app::Section::Settings;
}

const BackendCapabilities* Current(app::Requests& host, const app::Document& doc)
{
    const auto* connection = host.Connection();
    return connection && !connection->Offline() && !connection->PrivacyLocked() && connection->Active()
        && app::VerifiedWalletBinding(*connection->Active()) == doc.binding ? &*connection->Active() : nullptr;
}

PolicySettings& Settings(State& state, const app::Document& doc)
{
    auto& settings = state.settings[doc.id];
    if (settings.binding != doc.binding) {
        settings = PolicySettings{};
        settings.binding = doc.binding;
    }
    return settings;
}

void Choice(PolicySettings& settings, const PolicyChoice& choice)
{
    settings.script = choice.script;
    settings.multisig = choice.multisig;
    settings.required = int(choice.required);
    settings.signers = int(choice.signers);
    settings.sorted = choice.sorted;
}

void Revert(PolicySettings& settings)
{
    settings.invalid.clear();
    settings.status.clear();
    settings.staged.reset();
    settings.material.reset();
    settings.signer_drafts.reset();
    settings.signer_clipboard.reset();
    settings.signer_source=SignerSource::None;
    settings.signer_error.clear();
    settings.clipboard_pending = false;
    settings.signer_tab = 0;
    settings.dirty = false;
    Choice(settings, {});
    if (settings.saved && settings.saved_branch < settings.saved->branches.size()) {
        const auto& branch = settings.saved->branches[settings.saved_branch];
        Choice(settings,branch.descriptor.choice);
        if (!branch.descriptor.internal && branch.classified)
            settings.material = PublicWalletPreview{settings.saved->chain,{branch.descriptor}};
    }
    ++settings.revision;
}

void Saved(PolicySettings& settings, SavedWalletPolicy saved, uint64_t session)
{
    const auto pick = std::ranges::find_if(saved.branches, [](const auto& branch) {
        return branch.active && branch.classified && !branch.descriptor.internal
            && branch.descriptor.choice.script == ScriptTemplate::NativeSegwit && !branch.descriptor.choice.multisig;
    });
    settings.saved_branch = pick == saved.branches.end() ? 0 : size_t(pick-saved.branches.begin());
    settings.saved = std::move(saved);
    settings.saved_session = session;
    settings.saved_observed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (!settings.dirty) Revert(settings);
    else ++settings.revision;
}

bool MatchesSaved(const PolicySettings& settings)
{
    if (!settings.saved || !settings.staged) return false;
    return std::ranges::all_of(settings.staged->descriptors, [&](const auto& draft) {
        return std::ranges::any_of(settings.saved->branches,[&](const auto& branch) {
            return branch.descriptor == draft && branch.classified;
        });
    });
}

PolicyChoice Picked(const PolicySettings& settings)
{
    return {settings.script,uint32_t(settings.required),uint32_t(settings.signers),settings.multisig,settings.sorted};
}

const protocol::PublicDescriptor* RepresentedBranch(const PolicySettings& settings)
{
    if (settings.material && !settings.material->descriptors.empty()) return &settings.material->descriptors.front();
    if (settings.saved && settings.saved_branch<settings.saved->branches.size())
        return &settings.saved->branches[settings.saved_branch].descriptor;
    return nullptr;
}

std::string_view Chain(const PolicySettings& settings)
{
    return settings.binding ? protocol::ReceiveChain(settings.binding->network.genesis.hex()) : std::string_view{};
}

void Stage(PolicySettings& settings)
{
    settings.dirty = true;
    settings.staged.reset();
    settings.invalid.clear();
    settings.status.clear();
    settings.signer_error.clear();
    ++settings.revision;
    const auto picked=Picked(settings);
    if (!protocol::SupportedPolicy(picked)) {
        settings.invalid = "Use 1-of-1 for single signature or 1 ≤ M ≤ N ≤ 15 with N at least 2 for multisig.";
        return;
    }
    if (settings.signer_drafts) {
        std::vector<protocol::PublicSigner> keys;
        for (const auto& [index, signer] : *settings.signer_drafts) {
            if (index!=keys.size()) break;
            keys.push_back(signer.key);
        }
        if (keys.size()!=picked.signers || keys.size()!=settings.signer_drafts->size()) {
            settings.invalid="Fill exactly the configured signer slots or clear the extra slots.";
            return;
        }
        auto built=protocol::StageSignerPolicy(picked,Chain(settings),keys);
        settings.staged=std::move(built.preview);
        settings.invalid=std::move(built.error);
        if (settings.staged) settings.material=settings.staged;
        return;
    }
    if (!settings.material) {
        settings.invalid = "Paste the public descriptor or fill the public signer slots.";
        return;
    }
    auto built = protocol::StageWalletPolicy(picked,*settings.material);
    settings.staged = std::move(built.preview);
    settings.invalid = std::move(built.error);
}

void AcceptPublicPaste(PolicySettings& settings, std::string& text)
{
    struct Wipe { std::string& value; ~Wipe() {sodium_memzero(value.data(),value.size());} } wipe{text};
    const auto chain = protocol::ReceiveChain(settings.binding->network.genesis.hex());
    auto validated = protocol::PreviewPublicWallet(protocol::PublicImportKind::Descriptor,text,chain);
    if (!validated.preview) {
        settings.dirty = true;
        settings.staged.reset();
        settings.invalid = std::move(validated.error);
        ++settings.revision;
        return;
    }
    if (settings.clipboard_change && settings.signer_drafts) {
        if (!settings.staged) {
            settings.invalid="Complete the receive signer policy before adding a change descriptor.";
            ++settings.revision;
            return;
        }
        settings.material=settings.staged;
    }
    settings.signer_drafts.reset();
    settings.signer_error.clear();
    if (settings.clipboard_change && settings.material) {
        auto branch = std::move(validated.preview->descriptors.front());
        branch.internal = true;
        auto& branches = settings.material->descriptors;
        if (branches.size() == 2) branches.pop_back();
        branches.push_back(std::move(branch));
    } else {
        settings.material = std::move(validated.preview);
        Choice(settings,settings.material->descriptors.front().choice);
    }
    Stage(settings);
}

void Display(State& state, const char* id, const char* name, const std::string& value, SemanticRole role = SemanticRole::Text)
{
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
    state.presentation.Annotate(id,name,value,role);
}

template<class Action>
void Button(State& state, Facts& facts, const imkit::Theme& theme, const char* id, const char* label,
    bool enabled, Action action, bool primary = false)
{
    const auto document = state.documents.ActiveId();
    ImGui::PushID(id);
    ImGui::BeginDisabled(!enabled);
    const bool clicked = theme::ActionButton(label,enabled && primary ? imkit::ActionVariant::Primary : imkit::ActionVariant::Secondary,{},theme);
    const auto lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
    facts.settings[id] = {{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),!enabled,clicked,clicked,ImGui::IsItemVisible()};
    const auto request = state.presentation.Interact(label,SemanticRole::Button,SemanticAction::Press|SemanticAction::Focus,!enabled);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool applied = enabled && clicked && Viewing(state,document) && action();
    if (request) state.presentation.Defer(*request,[&state,document,enabled,clicked,applied,action](const auto&) {
        return enabled && Viewing(state,document) && (clicked ? applied : action());
    });
}

bool Read(State& state, app::Requests& host, uint64_t id)
{
    const auto* doc = Wallet(state,id);
    if (!doc || !Viewing(state,id) || !host.Wallets() || host.Wallets()->Busy()) return false;
    const auto* caps = Current(host,*doc);
    if (!caps || !CanReadWalletPolicy(*caps)
        || !host.Wallets()->Start(WalletAction::ReadPolicy,doc->binding->DaemonWallet(),*host.Connection(),Epoch(state),id)) return false;
    state.policy_read = id;
    return true;
}

bool Review(State& state, app::Requests& host, uint64_t id, uint64_t revision, uint64_t session, uint64_t profile_revision, LockEpoch epoch)
{
    const auto* doc = Wallet(state,id);
    auto* connection = host.Connection();
    if (!doc || !Viewing(state,id) || state.onboarding.page != OnboardingPage::None || state.onboarding.close_pending
        || !connection || connection->Session() != session
        || connection->Revision() != profile_revision || Epoch(state) != epoch || !host.Wallets() || host.Wallets()->Busy()) return false;
    const auto* caps = Current(host,*doc);
    auto& settings = Settings(state,*doc);
    if (!caps || !CanImportWallet(*caps) || caps->pruned || state.documents.Documents().size() >= 32 || settings.revision != revision
        || !settings.dirty || !settings.staged || !settings.signer_error.empty() || MatchesSaved(settings) || !protocol::VerifyPublicPreview(*settings.staged,caps->chain)) return false;
    BeginOnboarding(state,OnboardingPage::Import,host);
    auto& panel = state.onboarding;
    if (!panel.dialog.open || panel.page != OnboardingPage::Import) return false;
    panel.import_method = WalletImportMethod::Descriptor;
    panel.policy_review = true;
    panel.preview = settings.staged;
    return true;
}

bool Export(State& state, app::Requests& host, uint64_t id, uint64_t revision)
{
    const auto* doc = Wallet(state,id);
    if (!doc || !Viewing(state,id)) return false;
    auto& settings = Settings(state,*doc);
    if (settings.revision != revision || !settings.saved) return false;
    std::string text;
    for (const auto& branch : settings.saved->branches) {
        if (text.size() + branch.descriptor.descriptor.size() + !text.empty() > 65536) {
            settings.status = "The saved policy exceeds the clipboard limit.";
            ++settings.revision;
            return false;
        }
        if (!text.empty()) text += '\n';
        text += branch.descriptor.descriptor;
    }
    if (text.empty()) return false;
    const app::Request copy{app::Command::Component,id,app::Section::Settings,c::CopyIdentifier{"saved-policy",std::move(text)}};
    const bool copied=host.CanSubmit(copy).available && host.Submit(copy).accepted;
    if (!copied) {
        settings.status="The native clipboard could not accept the saved descriptors.";
        ++settings.revision;
    }
    return copied;
}

void Selectors(State& state, Facts& facts, const imkit::Theme& theme, PolicySettings& settings, float scale, bool busy)
{
    const auto& style=ImGui::GetStyle();
    const auto policy_column=ImGui::CalcTextSize("Policy").x+theme::ActionWidth("Single signature")
        +theme::ActionWidth("Multisig")+2*(style.ItemSpacing.x+style.CellPadding.x);
    const bool split = ImGui::GetContentRegionAvail().x >= 2*policy_column
        && ImGui::BeginTable("policy-controls",2,ImGuiTableFlags_SizingStretchSame);
    if (split) ImGui::TableNextColumn();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{ImGui::GetStyle().FramePadding.x,2*scale});
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Policy");
    if (split) theme::SameLineForAction("Single signature");
    Button(state,facts,theme,"single","Single signature",!busy && settings.multisig,[&settings] {
        settings.multisig=false; settings.required=settings.signers=1; settings.sorted=true; Stage(settings); return true;
    });
    theme::SameLineForAction("Multisig");
    Button(state,facts,theme,"multi","Multisig",!busy && !settings.multisig,[&settings] {
        settings.multisig=true; settings.required=settings.signers=2; settings.sorted=true;
        if (settings.script == ScriptTemplate::Taproot) settings.script=ScriptTemplate::NativeSegwit;
        Stage(settings); return true;
    });
    ImGui::TextUnformatted("Script template");
    for (size_t i=0;i<SCRIPTS.size();++i) {
        const auto script=ScriptTemplate(i);
        if (settings.multisig && script==ScriptTemplate::Taproot) continue;
        if (i%2==1 && ImGui::GetContentRegionAvail().x >= 218*scale) ImGui::SameLine();
        const auto choice=std::string("script-")+std::to_string(i);
        ImGui::PushID(choice.c_str());
        ImGui::BeginDisabled(busy);
        const bool selected=settings.script==script;
        const bool pressed=ImGui::RadioButton(SCRIPTS[i],selected);
        const auto access=state.presentation.Interact(SCRIPTS[i],SemanticRole::Radio,SemanticAction::Select|SemanticAction::Focus,busy,selected);
        ImGui::EndDisabled();
        ImGui::PopID();
        const auto apply=[&settings,script] {
            if (settings.multisig && script==ScriptTemplate::Taproot) return false;
            if (settings.script!=script) {settings.script=script; Stage(settings);}
            return true;
        };
        if (pressed) apply();
        if (access) state.presentation.Defer(*access,[apply](const auto&) {return apply();});
    }
    if (split) ImGui::TableNextColumn();
    ImGui::BeginDisabled(busy);
    if (settings.multisig) {
        const auto threshold = [&](const char* id, const char* label, int& value, int minimum, int maximum) {
            ImGui::PushID(label);
            ImGui::TextUnformatted(label);
            ImGui::SetNextItemWidth(64*scale);
            const bool numeric = ImGui::InputInt("##number",&value,0);
            auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
            facts.settings[id]={{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),busy,numeric,numeric,ImGui::IsItemVisible()};
            const auto number=state.presentation.Interact(label,SemanticRole::TextField,SemanticAction::SetValue|SemanticAction::Focus,busy,false,std::to_string(value));
            if (number) state.presentation.Defer(*number,[&settings,&value](const c::SemanticRequest& request) {
                int parsed{};
                const auto end=request.value.data()+request.value.size();
                const auto result=std::from_chars(request.value.data(),end,parsed);
                if (result.ec!=std::errc{} || result.ptr!=end) return false;
                value=parsed; Stage(settings); return true;
            });
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##slider",&value,minimum,maximum,"%d",ImGuiSliderFlags_AlwaysClamp) || numeric) Stage(settings);
            const auto slider=state.presentation.Interact(label,SemanticRole::Slider,
                SemanticAction::Increment|SemanticAction::Decrement|SemanticAction::Focus,busy,false,std::to_string(value));
            if (value>=minimum && value<=maximum)
                state.presentation.RangeValue(ImGui::GetItemID(),value,minimum,maximum,1);
            if (slider) state.presentation.Defer(*slider,[&settings,&value,minimum,maximum](const c::SemanticRequest& request) {
                const auto step=request.action==SemanticAction::Increment?1:-1;
                value=std::clamp(std::clamp(value,minimum,maximum)+step,minimum,maximum);
                Stage(settings);
                return true;
            });
            ImGui::PopID();
        };
        threshold("required","Required signatures (M)",settings.required,1,std::clamp(settings.signers,2,15));
        threshold("signers","Signers (N)",settings.signers,2,15);
    } else Display(state,"single-count","Threshold","1 of 1");
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    if (split) ImGui::EndTable();
}

std::map<size_t,SignerDraft> ExistingSigners(const PolicySettings& settings)
{
    std::map<size_t,SignerDraft> result;
    const auto* branch=RepresentedBranch(settings);
    if (!branch) return result;
    for (size_t i=0;i<branch->signers.size();++i) {
        auto parsed=protocol::InspectPublicSigner(branch->signers[i],Chain(settings),
            branch->choice.script==ScriptTemplate::Taproot);
        if (parsed.signer) result.emplace(i,SignerDraft{std::move(*parsed.signer),SignerSource::Descriptor});
    }
    return result;
}

bool SetupSignerEdit(PolicySettings& settings)
{
    if (settings.material && settings.material->descriptors.size()>1)
        settings.signer_error="Edit receive and change descriptors together from Policy, or revert before replacing individual signers.";
    else if (const auto* branch=RepresentedBranch(settings); branch && branch->internal)
        settings.signer_error="Select a receive descriptor before replacing its signers.";
    else return true;
    ++settings.revision;
    return false;
}

void StageKey(PolicySettings& settings,const SignerClipboard& intent,std::string& text)
{
    struct Wipe { std::string& value; ~Wipe() { sodium_memzero(value.data(),value.size()); } } wipe{text};
    auto fail=[&settings](std::string reason) {
        settings.signer_error=std::move(reason);
        ++settings.revision;
    };
    const auto choice=Picked(settings);
    auto parsed=protocol::InspectPublicSigner(text,Chain(settings),choice.script==ScriptTemplate::Taproot);
    if (!parsed.signer) {fail(std::move(parsed.error)); return;}
    auto& key=*parsed.signer;
    if (!protocol::SupportedPolicy(choice) || intent.slot>=choice.signers) {
        fail("Set a valid policy and choose one of its signer slots.");
        return;
    }
    if (!protocol::CompatibleSignerEncoding(key.encoding,choice)) {
        fail("The key header is for a different script template or signer policy.");
        return;
    }
    if (key.extended && key.derivation_suffix.empty()) {
        key.derivation_suffix="/0/*";
        key.expression+=key.derivation_suffix;
    }
    auto signers=settings.signer_drafts ? *settings.signer_drafts : ExistingSigners(settings);
    if (std::ranges::any_of(signers,[&](const auto& entry){
            return entry.first!=intent.slot && entry.second.key.material_id==key.material_id;
        })) {fail("This key repeats public material and chain code in another signer slot."); return;}
    signers[intent.slot]={std::move(key),intent.source};
    settings.signer_drafts=std::move(signers);
    Stage(settings);
}

void SignerTabs(State& state, PolicySettings& settings,size_t count)
{
    settings.signer_tab=std::min(settings.signer_tab,count-1);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{ImGui::GetStyle().FramePadding.x,2});
    const float height=ImGui::GetFrameHeightWithSpacing()+ImGui::GetStyle().ScrollbarSize;
    if (ImGui::BeginChild("signer-tabs",{0,height},ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar)) {
        const auto pos=ImGui::GetWindowPos(), size=ImGui::GetWindowSize();
        state.presentation.Group("signer-selection","Keystore slots",SemanticRole::TabList,pos,{pos.x+size.x,pos.y+size.y});
        for (size_t i=0;i<count;++i) {
            if (i) ImGui::SameLine();
            const auto label="Signer "+std::to_string(i+1);
            const bool pressed=ImGui::Selectable(label.c_str(),i==settings.signer_tab,ImGuiSelectableFlags_None,
                {ImGui::CalcTextSize(label.c_str()).x+16,0});
            const auto select=state.presentation.Interact(label.c_str(),SemanticRole::Tab,SemanticAction::Select|SemanticAction::Focus,
                false,i==settings.signer_tab);
            const auto use=[&settings,i] {
                if (settings.signer_tab!=i) {
                    settings.signer_tab=i; settings.signer_clipboard.reset();
                    ++settings.revision;
                }
                return true;
            };
            if (pressed) use();
            if (select) state.presentation.Defer(*select,[use,pressed](const auto&) {return pressed || use();});
        }
        state.presentation.EndGroup();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void Keystores(State& state, app::Requests& host, Facts& facts, const imkit::Theme& theme,
    PolicySettings& settings, const BackendCapabilities* caps, bool busy, float scale)
{
    const auto id=state.documents.ActiveId();
    const auto* connection=host.Connection();
    const auto clipboard=ImGui::GetID("keystore-key-inspection");
    if (settings.signer_clipboard && c::native_input) {
        const auto intent=*settings.signer_clipboard;
        if (intent.slot!=settings.signer_tab || intent.source!=settings.signer_source || intent.revision!=settings.revision
            || intent.session!=(connection?connection->Session():0) || intent.profile_revision!=(connection?connection->Revision():0)
            || intent.epoch!=Epoch(state) || state.onboarding.dialog.open) settings.signer_clipboard.reset();
        else if (auto input=c::native_input->Paste(clipboard,"",0,0,0,1025)) {
            settings.signer_clipboard.reset();
            StageKey(settings,intent,*input);
        } else if (!c::native_input->InspectionPending(clipboard)) {
            settings.signer_clipboard.reset();
            settings.signer_error="Public-key clipboard inspection was cancelled.";
            ++settings.revision;
        }
    }

    const auto material=settings.signer_drafts ? *settings.signer_drafts : ExistingSigners(settings);
    const auto count=std::max(size_t(std::clamp(settings.signers,1,15)),material.empty()?size_t{1}:material.rbegin()->first+1);
    SignerTabs(state,settings,count);
    const size_t slot=settings.signer_tab;
    const auto key=material.find(slot);
    const bool configured=key!=material.end();
    const auto select=[&settings](SignerSource source) {
        settings.signer_source=source;
        settings.signer_clipboard.reset();
        if (!settings.dirty) settings.signer_error.clear();
        ++settings.revision;
        return true;
    };
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{ImGui::GetStyle().FramePadding.x,3*scale});
    Button(state,facts,theme,"source-connected","Connected signer",!busy,[select]{return select(SignerSource::Connected);});
    ImGui::SameLine();
    Button(state,facts,theme,"source-airgapped","Airgapped signer",!busy,[select]{return select(SignerSource::Airgapped);});
    ImGui::SameLine();
    Button(state,facts,theme,"source-daemon","Daemon software",!busy,[select]{return select(SignerSource::Daemon);});
    ImGui::SameLine();
    Button(state,facts,theme,"source-watch","Watch-only",!busy,[select]{return select(SignerSource::WatchOnly);});
    ImGui::PopStyleVar();

    const bool paste_source=settings.signer_source==SignerSource::WatchOnly || settings.signer_source==SignerSource::Airgapped;
    if (settings.signer_source==SignerSource::Connected) {
        Display(state,"discovery-service","Connected signer availability",caps && caps->external_signer==true
            ? "Open Receive or an approved signing review to rescan and select a matching daemon signer."
            : "Connected signer discovery requires a configured daemon external-signer service.");
        Display(state,"device-secret-owner","Device controls",DeviceTransportStatus(ShippedExtensions()).detail);
    } else if (settings.signer_source==SignerSource::Daemon)
        Display(state,"daemon-custody","Daemon software custody",caps && caps->private_keys==true
            ? "The daemon holds the private keys. Read its saved descriptors to inspect public keys."
            : "Select a verified software wallet with daemon custody to inspect its public descriptors.");
    else if (paste_source) {
        const auto import=[&state,&settings,connection,slot,clipboard,id] {
            if (!Viewing(state,id) || !settings.inspect_keystores || !SetupSignerEdit(settings) || !c::native_input) return false;
            const auto source=settings.signer_source;
            if (source!=SignerSource::WatchOnly && source!=SignerSource::Airgapped) return false;
            if (!c::native_input->RequestInspection(clipboard)) return false;
            settings.signer_clipboard=SignerClipboard{slot,settings.revision,connection?connection->Session():0,
                connection?connection->Revision():0,Epoch(state),source};
            return true;
        };
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{ImGui::GetStyle().FramePadding.x,3*scale});
        Button(state,facts,theme,"paste-signer","Paste public key",
            !busy && c::native_input && !settings.signer_clipboard,import);
        ImGui::SameLine();
        Button(state,facts,theme,"scan-signer","Scan public-key QR",false,[]{return false;});
        if (configured) {
            ImGui::SameLine();
            Button(state,facts,theme,"clear-signer","Clear signer",!busy,[&settings,slot] {
                if (!SetupSignerEdit(settings)) return false;
                if (!settings.signer_drafts) settings.signer_drafts=ExistingSigners(settings);
                settings.signer_drafts->erase(slot);
                Stage(settings);
                return true;
            });
        }
        ImGui::PopStyleVar();
        if (!configured)
            Display(state,"scan-dependency","QR import availability","QR import requires a decoder. Paste a public key or [origin]key from your signer.");
    }
    if (!settings.signer_error.empty()) Display(state,"signer-error","Signer validation",settings.signer_error,SemanticRole::Alert);
    if (!settings.invalid.empty() && settings.signer_error.empty())
        Display(state,"signer-policy","Policy validation",settings.invalid,SemanticRole::Alert);
    if (!configured) {
        Display(state,"signer-empty","Unconfigured signer","Choose a source to inspect public signer material.");
        return;
    }

    const auto& item=key->second;
    auto text=[&state](const char* id,const char* label,const std::string& value) {
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        Display(state,id,label,value);
    };
    const bool saved=settings.saved && !settings.signer_drafts && !settings.dirty && caps;
    const char* type=item.source==SignerSource::WatchOnly?"Watch-only public key"
        :item.source==SignerSource::Airgapped?"Airgapped public key (supplied)"
        :saved && caps->external_signer==true?"Daemon-mediated signer"
        :saved && caps->private_keys==true?"Daemon software wallet"
        :saved && caps->private_keys==false?"Daemon watch-only descriptor"
        :"Public descriptor key";
    auto derivation=item.key.origin_path.empty()?std::string("Origin not provided"):item.key.origin_path;
    if (!item.key.derivation_suffix.empty()) derivation+=" → key"+item.key.derivation_suffix;
    if (ImGui::BeginTable("public-signer-fields",2,ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        text("signer-type","Type",type);
        ImGui::TableNextColumn();
        text("signer-label","Label","Signer "+std::to_string(slot+1));
        ImGui::TableNextColumn();
        text("signer-fingerprint","Fingerprint",item.key.fingerprint.empty()?"Not provided":item.key.fingerprint);
        ImGui::TableNextColumn();
        text("signer-derivation","Derivation",derivation);
        ImGui::EndTable();
    }
    const auto pub=item.key.public_key;
    text("signer-public-key",item.key.extended?"Xpub":"Public key",pub);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{ImGui::GetStyle().FramePadding.x,3*scale});
    Button(state,facts,theme,"copy-signer","Copy public key",true,[&state,&host,id,pub] {
        if (!Viewing(state,id)) return false;
        const app::Request copy{app::Command::Component,id,app::Section::Settings,c::CopyIdentifier{"public-signer",pub}};
        return host.CanSubmit(copy).available && host.Submit(copy).accepted;
    });
    ImGui::SameLine();
    Button(state,facts,theme,"show-signer-qr","Show public-key QR",false,[]{return false;});
    ImGui::PopStyleVar();
    Display(state,"show-qr-dependency","QR availability","Public-key QR needs a decoder for scanning and an encoder for display.");
}

void BranchDetails(State& state, PolicySettings& settings, bool busy)
{
    const auto* branch = settings.staged ? &settings.staged->descriptors.front()
        : settings.saved && !settings.saved->branches.empty() ? &settings.saved->branches[settings.saved_branch].descriptor : nullptr;
    if (!branch) return;
    ImGui::TextUnformatted(settings.staged ? "Staged descriptor" : "Saved descriptor");
    const float descriptor_height=2.35F*ImGui::GetTextLineHeightWithSpacing();
    if (ImGui::BeginChild("policy-descriptor",{0,descriptor_height})) {
        Display(state,"checksummed-policy",settings.staged ? "Staged public descriptor" : "Saved public descriptor",branch->descriptor);
        if (settings.staged && settings.staged->descriptors.size()==2)
            Display(state,"change-policy","Staged change descriptor",settings.staged->descriptors[1].descriptor);
    }
    ImGui::EndChild();
    if (!settings.staged && settings.saved) {
        const auto& record=settings.saved->branches[settings.saved_branch];
        if (record.range) Display(state,"saved-range","Saved daemon range","Daemon range "+std::to_string(record.range->first)+"–"+std::to_string(record.range->last));
        if (settings.saved->branches.size()>1) {
            if (record.range) ImGui::SameLine();
            ImGui::BeginDisabled(settings.dirty || busy);
            const bool next=ImGui::SmallButton("Next saved descriptor");
            const auto access=state.presentation.Interact("Next saved descriptor",SemanticRole::Button,SemanticAction::Press|SemanticAction::Focus,settings.dirty||busy);
            ImGui::EndDisabled();
            const auto select=[&settings] {
                settings.saved_branch=(settings.saved_branch+1)%settings.saved->branches.size();
                Revert(settings);
                return true;
            };
            if (next) select();
            if (access) state.presentation.Defer(*access,[select,next](const auto&) {return next || select();});
        }
    }
    if (branch->signers.empty()) return;
    const size_t selected=std::min(settings.signer_tab,branch->signers.size()-1);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{ImGui::GetStyle().FramePadding.x,2});
    const float tab_height=ImGui::GetFrameHeightWithSpacing()+ImGui::GetStyle().ScrollbarSize;
    if (ImGui::BeginChild("policy-keystores",{0,tab_height},ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar)) {
        const auto pos=ImGui::GetWindowPos(), size=ImGui::GetWindowSize();
        state.presentation.Group("keystore-heading","Keystores",SemanticRole::TabList,pos,{pos.x+size.x,pos.y+size.y});
        for (size_t i=0;i<branch->signers.size();++i) {
            if (i) ImGui::SameLine();
            const auto label="Signer "+std::to_string(i+1);
            const bool pressed=ImGui::Selectable(label.c_str(),i==selected,ImGuiSelectableFlags_None,{ImGui::CalcTextSize(label.c_str()).x+16,0});
            const auto access=state.presentation.Interact(label.c_str(),SemanticRole::Tab,SemanticAction::Select|SemanticAction::Focus,false,i==selected);
            const auto choose=[&settings,i] {settings.signer_tab=i; ++settings.revision; return true;};
            if (pressed && i!=selected) choose();
            if (access) state.presentation.Defer(*access,[choose,pressed](const auto&) {return pressed || choose();});
        }
        state.presentation.EndGroup();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    Display(state,"signer-expression","Public signer expression",branch->signers[selected]);
}
}

void PollPolicySettings(State& state, app::Requests& host)
{
    std::erase_if(state.settings,[&](const auto& item){return !state.documents.Find(item.first);});
    // Clipboard reads never restore a staged action when returning to a tab.
    for (auto& [id, settings] : state.settings) if (!Viewing(state,id)) {
        settings.clipboard_pending=false;
        settings.signer_clipboard.reset();
    }
    if (!state.policy_read || !host.Wallets() || !host.Connection()) return;
    const auto id=*state.policy_read;
    const auto* document=Wallet(state,id);
    const bool current = document && Viewing(state,id) && Current(host,*document) && !state.onboarding.dialog.open;
    if (!current) host.Wallets()->Cancel();
    auto reply=host.Wallets()->Poll(*host.Connection(),Epoch(state),state.documents.ActiveId());
    if (!host.Wallets()->Busy()) state.policy_read.reset();
    if (!current || !reply || reply->first!=WalletAction::ReadPolicy) return;
    auto& settings=Settings(state,*document);
    auto& result=reply->second;
    if (result.policy && (!result.policy->unsupported || !result.policy->branches.empty()))
        Saved(settings,std::move(*result.policy),host.Connection()->Session());
    else if (result.policy) result.error="The daemon reports only unsupported public descriptor forms. Prior saved policy retained.";
    settings.status=std::move(result.error);
    ++settings.revision;
}

void PolicyWalletApplied(State& state,uint64_t previous,uint64_t created,const PublicWalletPreview& preview,uint64_t session)
{
    const auto* doc=Wallet(state,created);
    if (!doc || previous==created) return;
    SavedWalletPolicy saved{preview.chain};
    for (const auto& descriptor : preview.descriptors)
        saved.branches.push_back({descriptor,descriptor.ranged,true,{}});
    Saved(Settings(state,*doc),std::move(saved),session);
    if (auto older=state.settings.find(previous); older!=state.settings.end()) Revert(older->second);
}

bool AllowPolicyClose(State& state,std::span<const uint64_t> documents)
{
    for (const auto id:documents) {
        const auto found=state.settings.find(id);
        if (found==state.settings.end() || !found->second.dirty) continue;
        state.documents.Activate(id);
        state.documents.Navigate(app::Section::Settings);
        found->second.inspect_advanced=false;
        found->second.account_service=false;
        found->second.status="Staged policy retained. Apply or Revert before closing.";
        ++found->second.revision;
        return false;
    }
    return true;
}

void DrawPolicySettings(State& state, app::Requests& host, Facts& facts, const imkit::Theme& theme, float scale)
{
    const auto id=state.documents.ActiveId();
    const auto* doc=Wallet(state,id);
    if (!doc) return;
    auto& settings=Settings(state,*doc);
    const auto* caps=Current(host,*doc);
    const bool busy=host.Wallets()->Busy();
    if (settings.inspect_advanced) {
        DrawAdvancedSettings(state,host,facts,theme,scale);
        return;
    }
    if (settings.account_service) {
        DrawAccountService(state,host,facts,theme);
        return;
    }
    const bool readable=caps && CanReadWalletPolicy(*caps);
    imkit::PushTextRole(theme,imkit::TextRole::Heading);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Settings");
    imkit::PopTextRole();
    state.presentation.Annotate("settings-title","Wallet Settings","Wallet Settings");
    theme::SameLineForAction("Read saved descriptors");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{ImGui::GetStyle().FramePadding.x,3*scale});
    Button(state,facts,theme,"read","Read saved descriptors",readable && !busy,[&state,&host,id]{return Read(state,host,id);});
    if (state.policy_read==id) {
        theme::SameLineForAction("Cancel read");
        Button(state,facts,theme,"cancel-read","Cancel read",true,[&state,&host,id] {
            if (state.policy_read!=id) return false;
            host.Wallets()->Cancel();
            return true;
        });
    }
    theme::SameLineForAction(settings.inspect_keystores?"Policy":"Keystores");
    Button(state,facts,theme,"toggle-keystores",settings.inspect_keystores?"Policy":"Keystores",!busy,[&settings] {
        settings.inspect_keystores=!settings.inspect_keystores;
        settings.signer_clipboard.reset();
        settings.clipboard_pending=false;
        ++settings.revision;
        return true;
    });
    if (host.Rescanning()) {
        theme::SameLineForAction("Advanced");
        Button(state,facts,theme,"advanced","Advanced",!busy,[&settings] {
            settings.inspect_advanced=true;
            settings.signer_clipboard.reset();
            settings.clipboard_pending=false;
            ++settings.revision;
            return true;
        });
    }
    ImGui::PopStyleVar();
    const char* custody=!caps ? "Wallet custody requires a current connection."
        : caps->external_signer==true ? "Daemon-mediated signer." : caps->private_keys==true
        ? "Private keys are held by the daemon." : caps->private_keys==false ? "Watch-only; public descriptors." : "Wallet custody is unknown.";
    Display(state,"wallet-custody","Wallet type and custody",custody);
    const auto clipboard = ImGui::GetID("settings-public-descriptor");
    if (settings.clipboard_pending && c::native_input) {
        if (auto input=c::native_input->Paste(clipboard,"",0,0,0,4097)) {
            settings.clipboard_pending=false;
            AcceptPublicPaste(settings,*input);
        } else if (!c::native_input->InspectionPending(clipboard)) {
            settings.clipboard_pending=false;
            settings.status="Public clipboard inspection was cancelled.";
            ++settings.revision;
        }
    }
    const auto paste=[&settings,clipboard](bool change) {
        if (!c::native_input) return false;
        settings.clipboard_change=change;
        settings.clipboard_pending=c::native_input->RequestInspection(clipboard);
        return settings.clipboard_pending;
    };
    const float spacing=ImGui::GetStyle().ItemSpacing.x;
    const float actions_left=theme::ActionWidth("Export saved")+theme::ActionWidth("Add Account")+spacing;
    const float actions_right=theme::ActionWidth("Revert")+theme::ActionWidth("Review Apply")+spacing;
    const bool single_row=actions_left+spacing+actions_right<=ImGui::GetContentRegionAvail().x;
    const float line=ImGui::GetFrameHeightWithSpacing();
    const float footer=line*(single_row?1.F:2.F);
    if (ImGui::BeginChild("policy-details",{0,std::max(line*2,ImGui::GetContentRegionAvail().y-footer)})) {
        if (!settings.saved && !readable)
            Display(state,"saved-unavailable","Saved policy availability","Saved descriptors require the daemon's public listdescriptors capability.");
        if (settings.saved && settings.saved->unsupported)
            Display(state,"saved-partial","Saved policy coverage",std::to_string(settings.saved->unsupported)+" saved descriptors use unsupported forms.");
        if (settings.saved && (!caps || settings.saved_session!=host.Connection()->Session()))
            Display(state,"saved-dated","Saved policy provenance","Previously verified wallet policy.");
        if (!settings.status.empty()) Display(state,"settings-status","Settings status",settings.status);
        if (settings.inspect_keystores) Keystores(state,host,facts,theme,settings,caps,busy,scale);
        else {
            Selectors(state,facts,theme,settings,scale,busy);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{ImGui::GetStyle().FramePadding.x,3*scale});
            Button(state,facts,theme,"paste","Paste public descriptor",c::native_input && !busy && !settings.clipboard_pending,[paste]{return paste(false);});
            if (settings.material) {
                theme::SameLineForAction("Paste change descriptor");
                Button(state,facts,theme,"paste-change","Paste change descriptor",c::native_input && !busy && !settings.clipboard_pending,[paste]{return paste(true);});
            }
            ImGui::PopStyleVar();
            if (!settings.signer_error.empty()) Display(state,"source-error","Signer validation",settings.signer_error,SemanticRole::Alert);
            if (!settings.invalid.empty()) Display(state,"policy-error","Policy validation",settings.invalid,SemanticRole::Alert);
            BranchDetails(state,settings,busy);
        }
    }
    ImGui::EndChild();

    const auto revision=settings.revision;
    Button(state,facts,theme,"export","Export saved",settings.saved && !settings.saved->branches.empty(),
        [&state,&host,id,revision]{return Export(state,host,id,revision);});
    theme::SameLineForAction("Add Account");
    Button(state,facts,theme,"add-account","Add Account",!busy,[&settings] {
        settings.account_service=true;
        settings.signer_clipboard.reset();
        settings.clipboard_pending=false;
        ++settings.revision;
        return true;
    });
    if (single_row) ImGui::SameLine();
    const auto remaining=ImGui::GetContentRegionAvail().x;
    if (remaining>actions_right) ImGui::SetCursorPosX(ImGui::GetCursorPosX()+remaining-actions_right);
    Button(state,facts,theme,"revert","Revert",settings.dirty && !busy,[&settings]{Revert(settings);return true;});
    theme::SameLineForAction("Review Apply");
    const bool apply=caps && CanImportWallet(*caps) && !caps->pruned && !busy && state.documents.Documents().size()<32
        && settings.dirty && settings.staged && settings.signer_error.empty() && !MatchesSaved(settings);
    Button(state,facts,theme,"apply","Review Apply",apply,
        [&state,&host,id,revision,session=caps?host.Connection()->Session():0,profile=caps?host.Connection()->Revision():0,epoch=Epoch(state)]{
            return Review(state,host,id,revision,session,profile,epoch);
        },true);
    if (apply && ImGui::IsItemHovered()) ImGui::SetTooltip("Review creation of a separate watch-only wallet");
}
}
