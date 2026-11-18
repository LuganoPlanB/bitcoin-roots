// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <sodium.h>

namespace roots::gui::ui::shell {
namespace {
namespace a=app;
namespace c=components;
using c::ax::SemanticAction;
using c::ax::SemanticRole;
constexpr size_t PAGE=4;
constexpr int64_t MAX_MONEY=21000000LL*100000000;

bool Visible(const State& state)
{
    return !state.privacy.hidden && !state.semantic_hide_toggle && !state.lock_panel.setup
        && (!state.recovery || (!state.recovery->Locked() && !state.recovery->Pending()));
}
bool Unobstructed(const State& state)
{
    return Visible(state) && state.actions.kind==DocumentDialog::None && !state.local_document.dialog.open
        && !state.local_document.close_pending && !state.server.dialog.open && !state.onboarding.dialog.open
        && !state.recovery_dialog.open && !state.close_tab_popup && !state.close_document_popup
        && (!c::native_input || !c::native_input->NativeDialogActive());
}

class Provenance {
    crypto_hash_sha256_state state;
public:
    Provenance() {crypto_hash_sha256_init(&state);}
    void Add(std::string_view field)
    {
        const auto size=std::to_string(field.size())+":";
        crypto_hash_sha256_update(&state,reinterpret_cast<const unsigned char*>(size.data()),size.size());
        crypto_hash_sha256_update(&state,reinterpret_cast<const unsigned char*>(field.data()),field.size());
    }
    template <typename Integer> void Number(Integer value) {Add(std::to_string(value));}
    Hash256 Finish()
    {
        std::array<unsigned char,crypto_hash_sha256_BYTES> bytes{};
        crypto_hash_sha256_final(&state,bytes.data());
        return *Hash256::Parse(protocol::TransactionHex({reinterpret_cast<const char*>(bytes.data()),bytes.size()}));
    }
};

std::optional<Hash256> ProofDigest(const a::CoinDraft& coins)
{
    Provenance digest;
    if (coins.observed_unavailable) return {};
    digest.Add("Exact independent input and change evidence v1");
    digest.Number(coins.inputs.size());
    for (const auto& input:coins.inputs) {
        if (!input.value || !input.offline) return {};
        digest.Add(a::CoinKey(input.outpoint));
        digest.Number(input.value->sats());
        digest.Add(input.script);
        digest.Add(input.offline->previous_hex);
        digest.Add(input.offline->descriptor);
        digest.Add(input.offline->redeem_script);
        digest.Add(input.offline->witness_script);
        digest.Number(input.offline->observed_ms);
    }
    digest.Number(coins.offline_change.has_value());
    if (const auto& change=coins.offline_change) {
        digest.Add(change->descriptor);
        digest.Number(change->index);
        digest.Number(change->observed_ms);
    }
    return digest.Finish();
}

std::set<uint32_t> ProofIndices(const protocol::PublicDescriptor& descriptor)
{
    std::set<uint32_t> result;
    for (const auto& signer:descriptor.signers) {
        const auto end=signer.find(']');
        if (!signer.starts_with('[') || end==signer.npos) continue;
        const auto start=signer.rfind('/',end);
        if (start==signer.npos) continue;
        uint32_t index{};
        const auto parsed=std::from_chars(signer.data()+start+1,signer.data()+end,index);
        if (parsed.ec==std::errc{} && parsed.ptr==signer.data()+end && index<=0x7fffffffU) result.insert(index);
    }
    return result;
}

bool PolicyScript(const SavedWalletPolicy& policy,std::string_view script,const std::set<uint32_t>& indices,bool internal=false)
{
    for (const auto& branch:policy.branches) {
        if (!branch.active || !branch.classified || (internal && !branch.descriptor.internal)) continue;
        if (!branch.descriptor.ranged && branch.descriptor.script==script) return true;
        for (const auto index:indices) {
            if (!branch.range || index<branch.range->first || index>branch.range->last) continue;
            const auto derived=protocol::DerivePublicDescriptor(branch.descriptor.descriptor,policy.chain,index);
            if (derived && derived->script==script) return true;
        }
    }
    return false;
}

Hash256 PolicyDigest(const SavedWalletPolicy& policy)
{
    Provenance digest;
    digest.Add("Observed wallet policy v1");
    digest.Add(policy.chain);
    digest.Number(policy.unsupported);
    std::vector<std::string> branches;
    for (const auto& entry:policy.branches) {
        Provenance branch;
        branch.Add(entry.descriptor.descriptor);
        branch.Add(entry.descriptor.script);
        branch.Number(entry.descriptor.internal);
        branch.Number(entry.active);
        branch.Number(entry.classified);
        branch.Number(entry.range.has_value());
        if (entry.range) {
            branch.Number(entry.range->first);
            branch.Number(entry.range->last);
        }
        branches.push_back(branch.Finish().hex());
    }
    std::ranges::sort(branches);
    for (const auto& branch:branches) digest.Add(branch);
    return digest.Finish();
}

const SavedWalletPolicy* ObservedPolicy(const State& state,uint64_t wallet,const a::WalletBinding& binding,uint64_t session)
{
    const auto at=state.settings.find(wallet);
    if (at==state.settings.end() || at->second.binding!=binding || !at->second.saved
        || at->second.saved_session!=session || at->second.saved_observed_ms<=0) return nullptr;
    return &*at->second.saved;
}

struct Candidate {
    std::optional<a::SigningBasis> basis;
    std::string reason;
};
Candidate Current(State&,a::Requests&,uint64_t,bool validate=false,bool as_proof=false);
Candidate WorkingBasis(State& state,a::Requests& host,const a::Document& doc,const EditedOrigin& lineage,bool validate)
{
    const auto reject=[](std::string detail){return Candidate{{},std::move(detail)};};
    const auto* original=state.documents.Find(lineage.id);
    if (!lineage.id || lineage.id==doc.id || state.edited_origins.contains(lineage.id) || !original
        || !original->imported || original->draft_revision!=lineage.revision || lineage.content.lock()!=original->imported->parsed)
        return reject("The exact independent source for this working copy is unavailable.");
    auto known=Current(state,host,lineage.id,validate,true);
    if (!known.basis) return known;
    auto& facts=*known.basis;
    const auto& edited=*doc.imported->parsed;
    const auto& before=*facts.content->transaction;
    const auto& after=*edited.transaction;
    if (facts.proof.network!=doc.imported->network || edited.psbt_version!=facts.content->psbt_version
        || !protocol::TransactionEditBlock(edited).empty() || before.inputs.size()!=after.inputs.size()
        || before.outputs.size()!=after.outputs.size())
        return reject("This working copy differs from the independently proved wallet and payment plan.");
    std::vector<protocol::OfflineReviewInput> proof_inputs;
    std::vector<uint32_t> modes;
    for (size_t i=0;i<after.inputs.size();++i) {
        const auto& input=after.inputs[i];
        const auto original_input=std::ranges::find(before.inputs,input.outpoint,&protocol::TransactionInput::outpoint);
        if (original_input==before.inputs.end()) return reject("An edited outpoint needs independent proof from a new Send draft.");
        const auto index=size_t(original_input-before.inputs.begin());
        if (input.previous_value!=original_input->previous_value || input.previous_script!=original_input->previous_script
            || input.evidence!=original_input->evidence || protocol::ReviewSighash(edited,i)!=facts.sighash[index]
            || edited.input_policy[i].scripts!=facts.content->input_policy[index].scripts)
            return reject("Edited previous-output, sighash or script policy differs from the independent source.");
        proof_inputs.push_back(facts.proof.inputs[index]);
        modes.push_back(facts.sighash[index]);
    }
    if (facts.change_output && std::ranges::count(before.outputs,before.outputs[*facts.change_output])!=1)
        return reject("An identical recipient and change output cannot preserve independent change identity after editing.");
    std::vector<protocol::TransactionOutput> proof_outputs;
    std::vector<bool> used(before.outputs.size());
    std::optional<size_t> change;
    for (size_t i=0;i<after.outputs.size();++i) {
        const auto& item=after.outputs[i];
        bool found{};
        for (size_t j=0;j<before.outputs.size();++j) {
            if (used[j] || item!=before.outputs[j] || edited.output_policy[i].scripts!=facts.content->output_policy[j].scripts) continue;
            used[j]=found=true;
            if (facts.change_output==j) change=i;
            else proof_outputs.push_back(item);
            break;
        }
        if (!found) return reject("Edited outputs or change differ from independently proved payments. Prepare a new Send draft or export the working copy.");
    }
    facts.document=doc.id;
    facts.document_revision=doc.draft_revision;
    facts.content=doc.imported->parsed;
    facts.proof.inputs=std::move(proof_inputs);
    facts.proof.recipients=std::move(proof_outputs);
    facts.change_output=change;
    facts.sighash=std::move(modes);
    return known;
}

Candidate Current(State& state,a::Requests& host,uint64_t id,bool validate,bool as_proof)
{
    const auto reject=[](std::string reason){return Candidate{{},std::move(reason)};};
    const auto* doc=state.documents.Find(id);
    if (!Visible(state) || !doc || !doc->imported || doc->observation==a::ObservationState::Locked)
        return reject("The current transaction is unavailable.");
    if (!as_proof && state.transaction_edits.contains(id))
        return reject("Close the transaction editor and review the resulting revision before approving or signing.");
    const auto& imported=*doc->imported;
    const auto& content=imported.parsed;
    if (!content || !content->transaction || !content->psbt_version)
        return reject("An unsigned PSBT with independently established payment and wallet proofs is required.");
    if (const auto edited=state.edited_origins.find(id);edited!=state.edited_origins.end())
        return WorkingBasis(state,host,*doc,edited->second,validate);
    const auto& tx=*content->transaction;
    const auto view=state.imported_views.find(id);
    const auto* funding=imported.funding ? &*imported.funding : nullptr;
    const auto local=view!=state.imported_views.end() ? view->second.offline_review_origin : std::nullopt;
    if (!funding && (!local || !view->second.wallet || !view->second.review_evidence))
        return reject("Independent payment and wallet proofs are unavailable for this PSBT. Open the compatible wallet or export the original.");
    const auto origin=funding ? std::pair{funding->draft_document,funding->draft_revision} : *local;
    const auto* draft=state.documents.Find(origin.first);
    if (!draft || draft->draft_revision!=origin.second || !draft->complete || draft->observation==a::ObservationState::Locked)
        return reject("The independent originating draft is missing or its revision changed.");
    const auto& binding=funding ? funding->evidence.wallet : *view->second.wallet;
    const auto* wallet=a::RegisteredWallet(state.documents,binding);
    if (!wallet || wallet->observation==a::ObservationState::Locked || !draft->SendWallet() || *draft->SendWallet()!=binding
        || protocol::DocumentNetwork(binding.network)!=imported.network)
        return reject("The exact compatible signing wallet for this document and network is unavailable.");
    const auto* connection=host.Connection();
    const auto session=connection ? connection->Session() : 0;
    const auto profile=connection ? connection->Revision() : 0;
    const auto epoch=state.recovery ? state.recovery->Gate().Epoch() : LockEpoch{};
    const bool offline=!connection || connection->Offline() || !connection->Active();
    if (connection && connection->PrivacyLocked()) return reject("The connection is locked.");
    if (!offline && a::VerifiedWalletBinding(*connection->Active())!=binding)
        return reject("The verified connected wallet differs from the signing wallet.");
    const auto proof=funding ? a::FundingReviewEvidence(*draft,imported) : view->second.review_evidence;
    if (!proof || proof->network!=imported.network || proof->inputs.size()!=tx.inputs.size()
        || proof->recipients.empty() || proof->recipients.size()+size_t(proof->change.has_value())!=tx.outputs.size()
        || !std::ranges::equal(proof->recipients,std::span(tx.outputs).first(proof->recipients.size())))
        return reject("The ordered recipients, change, or independent input proof differs from the originating draft.");
    const auto* coins=funding ? &funding->evidence : draft->coin_draft ? &*draft->coin_draft : nullptr;
    const auto digest=coins && coins->wallet==binding ? ProofDigest(*coins) : std::nullopt;
    if (!digest) return reject("An independent previous-output or change proof is missing or unavailable.");
    if (!tx.locktime || content->modifiable.value_or(0) || tx.coinbase || tx.inputs.empty())
        return reject("A fixed non-coinbase PSBT with known locktime and immutable payment fields is required.");
    if (validate && funding && !a::ValidCreatedFunding(imported))
        return reject("The saved independent funding evidence no longer validates.");
    if (validate && !funding) {
        const auto checked=protocol::InspectOfflineReview(*content,*proof);
        if (!checked.commitment) return reject(checked.error);
    }
    const auto* observed=ObservedPolicy(state,wallet->id,binding,session);
    if (observed && observed->chain!=protocol::DocumentChain(imported.network)) return reject("Observed wallet policy is for a different network.");
    std::vector<uint32_t> sighash;
    bool acknowledgement{};
    int64_t total_input{},total_output{};
    for (const auto& output:tx.outputs) total_output+=output.value.sats();
    for (size_t i=0;i<tx.inputs.size();++i) {
        const auto& input=tx.inputs[i];
        const auto& pin=proof->inputs[i];
        const auto& policy=content->input_policy.at(i);
        if (std::ranges::find(draft->excluded_coins,input.outpoint)!=draft->excluded_coins.end())
            return reject("An exact input is now excluded by the originating draft.");
        if (pin.point!=input.outpoint || pin.value!=input.previous_value || input.evidence==protocol::PrevoutEvidence::Conflict
            || policy.final_fields)
            return reject("An input, previous amount or final script conflicts with the independent signing proof.");
        if (pin.value.sats()<0 || pin.value.sats()>MAX_MONEY-total_input)
            return reject("The independent input total is outside the Bitcoin monetary range.");
        total_input+=pin.value.sats();
        const auto script=protocol::DerivePublicDescriptor(pin.descriptor,protocol::DocumentChain(proof->network),0);
        if (!script || script->ranged || script->script!=input.previous_script)
            return reject("An input is outside the exact independent public wallet policy.");
        if (observed && !PolicyScript(*observed,input.previous_script,ProofIndices(*script)))
            return reject("An input is incompatible with the observed wallet policy.");
        const auto mode=protocol::ReviewSighash(*content,i);
        const bool taproot=script->choice.script==protocol::ScriptTemplate::Taproot;
        if (!mode || (*mode!=1 && !(taproot && *mode==0)))
            return reject("An input has conflicting, unknown, or unsupported sighash. No approval can be recorded.");
        sighash.push_back(*mode);
        acknowledgement |= taproot && *mode==1;
    }
    if (total_input<total_output || total_input-total_output!=proof->fee.sats())
        return reject("The independent input and output totals do not reconcile with the exact fee.");
    if (proof->change) {
        const auto derived=protocol::DerivePublicDescriptor(proof->change->descriptor,protocol::DocumentChain(proof->network),proof->change->index);
        if (!derived || proof->change->observed_ms<=0 || tx.outputs.back().script!=derived->script
            || (observed && !PolicyScript(*observed,derived->script,{proof->change->index},true)))
            return reject("Intended change no longer matches the independent internal wallet proof.");
    }
    std::optional<Hash256> coin_policy;
    if (!offline && host.Refresh() && state.recovery) {
        const auto scope=a::WalletRefreshIdentity(*connection->Active(),session,*wallet,epoch);
        const auto fresh=host.Refresh()->View();
        if (scope && fresh.identity && *scope==*fresh.identity && fresh.ledger && fresh.chain_current
            && !fresh.offline && !fresh.locked && fresh.freshness[2]==Freshness::Live && fresh.ledger->InventoryAvailable()) {
            const auto rules=state.coin_controls.Policy(*wallet,fresh,*state.recovery,draft->id);
            Provenance seen;
            seen.Add("Current exact coin policy v1");
            seen.Number(bool(rules));
            for (const auto& input:tx.inputs) {
                const auto id=a::CoinKey(input.outpoint);
                const auto exists=std::ranges::find(fresh.ledger->Coins(),input.outpoint,&HistoryCoin::outpoint);
                if (exists==fresh.ledger->Coins().end() || exists->amount!=input.previous_value
                    || exists->script!=input.previous_script || (fresh.ledger->LocksAvailable()
                    && std::ranges::find(fresh.ledger->DaemonLocked(),input.outpoint)!=fresh.ledger->DaemonLocked().end()))
                    return reject("An exact input changed, is unavailable or now has a daemon lock.");
                if (rules) {
                    const auto rule=rules->rules.find(id);
                    if (rule==rules->rules.end() || rule->second!=a::CoinRule::Allowed)
                        return reject("An exact input is frozen, reserved elsewhere, or unavailable under current coin policy.");
                }
                seen.Add(id);
            }
            coin_policy=seen.Finish();
        }
    }
    return {a::SigningBasis{id,origin.first,wallet->id,session,profile,doc->draft_revision,origin.second,epoch,
        offline,binding,content,*proof,proof->change ? std::optional(tx.outputs.size()-1) : std::nullopt,
        *digest,observed ? std::optional(PolicyDigest(*observed)) : std::nullopt,
        coin_policy,std::move(sighash),acknowledgement},{}};
}

a::Availability Can(State& state,const a::Request& request,const Candidate& current)
{
    if (!Unobstructed(state) || request.document!=state.documents.ActiveId() || !request.document)
        return {false,"The requested transaction view is no longer current."};
    if (!current.basis) return {false,current.reason};
    const auto& facts=*current.basis;
    if (request.command==a::Command::SignTransaction) {
        if (!state.signing_reviews.Authorize(facts)) return {false,"Signing requires a matching current review and explicit approval."};
        return {false,a::MissingDependency(a::Command::SignTransaction)};
    }
    const auto view=state.signing_views.find(request.document);
    if (view==state.signing_views.end() || !view->second.open) return {false,"Open the review for this exact transaction."};
    if (!state.signing_reviews.Acknowledged(facts)) return {false,"Read and acknowledge the advanced sighash commitments first."};
    if (request.command==a::Command::ReviewForSigning)
        return state.signing_reviews.Recorded(facts) ? a::Availability{false,"This exact transaction is already reviewed."}
            : a::Availability{true,{}};
    if (request.command==a::Command::ApproveSigning)
        return {state.signing_reviews.Recorded(facts) && !state.signing_reviews.Authorize(facts),
            "Review this exact transaction before approving it. An existing approval needs no repeat."};
    return {false,"Unrecognized signing review command."};
}

void Text(State& state,const char* key,const char* name,std::string_view value,SemanticRole role=SemanticRole::Text)
{
    ImGui::TextWrapped("%.*s",int(value.size()),value.data());
    state.presentation.Annotate(key,name,value,role);
}
bool Button(State& state,Facts& facts,const imkit::Theme& theme,const char* key,const char* label,
    bool enabled,std::function<bool()> action,bool primary=false,std::string_view detail={})
{
    ImGui::PushID(key);
    ImGui::BeginDisabled(!enabled);
    const auto pressed=theme::ActionButton(label,primary ? imkit::ActionVariant::Primary : imkit::ActionVariant::Secondary,{},theme);
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    facts.transaction_document[key]={{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),!enabled,
        pressed,pressed,ImGui::IsItemVisible()};
    const auto accessible=state.presentation.Interact(label,SemanticRole::Button,SemanticAction::Press|SemanticAction::Focus,
        !enabled,false,{},detail);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted=pressed && enabled && action();
    if (accessible) state.presentation.Defer(*accessible,[enabled,pressed,accepted,action=std::move(action)](const auto&){
        return enabled && (pressed ? accepted : action());
    });
    return accepted;
}

std::string Amount(Money value)
{
    return value.btc()+" BTC · "+std::to_string(value.sats())+(value.sats()==1 ? " sat" : " sats");
}
std::string SighashLabel(std::optional<uint32_t> mode)
{
    if (!mode) return "Unknown or conflicting";
    switch (*mode) {
    case 0: return "Taproot DEFAULT";
    case 1: return "ALL";
    case 2: return "NONE";
    case 3: return "SINGLE";
    case 0x81: return "ALL | ANYONECANPAY";
    case 0x82: return "NONE | ANYONECANPAY";
    case 0x83: return "SINGLE | ANYONECANPAY";
    default: return "Unsupported value "+std::to_string(*mode);
    }
}

void Overview(State& state,const a::ImportedDocument& imported,const Candidate& candidate)
{
    const auto& tx=*imported.parsed->transaction;
    const auto* facts=candidate.basis ? &*candidate.basis : nullptr;
    if (facts) {
        const auto label=facts->wallet.DaemonWallet().empty() ? "Default daemon wallet" : facts->wallet.DaemonWallet();
        Text(state,"review-wallet","Exact signing wallet","Signing wallet: "+label+" · "+std::string(protocol::DocumentNetworkName(imported.network)));
        Text(state,"review-origin","Signing wallet provenance",facts->observed_policy
            ? "Independent previous-output and observed public wallet policy proofs."
            : "Independent saved previous-output and public wallet policy proofs. Live signer readiness is Unknown.");
    } else Text(state,"review-wallet","Exact signing wallet","Signing wallet compatibility: Unknown");
    int64_t outputs{},inputs{};
    for (const auto& output:tx.outputs) outputs+=output.value.sats();
    if (facts) for (const auto& input:facts->proof.inputs) inputs+=input.value.sats();
    const auto fee=facts ? std::optional(facts->proof.fee) : tx.fee;
    Text(state,"review-values","Exact transaction totals",
        "Inputs "+(facts ? Amount(Money::Satoshis(inputs)) : std::string("Unknown"))+" · Outputs "+Amount(Money::Satoshis(outputs)));
    Text(state,"review-fee","Exact transaction fee","Fee: "+(fee ? Amount(*fee) : std::string("Unknown")));
    const auto* origin=facts && imported.funding ? state.documents.Find(facts->origin) : nullptr;
    const auto requested=origin ? ParseFundingRate(origin->funding_rate) : std::nullopt;
    if (requested) {
        const auto fraction=std::to_string(1000+*requested%1000).substr(1);
        Text(state,"review-rate-request","Requested funding rate","Requested funding: "+std::to_string(*requested/1000)+"."+fraction+" sat/vB");
    } else if (origin && origin->funding_target) Text(state,"review-rate-request","Requested funding target",
        "Funding target "+std::to_string(origin->funding_target)+" blocks. The exact requested rate is unavailable in this view.");
    Text(state,"review-rate","Actual final fee rate","Final fee rate: Unknown until the signed size is known.");
    const bool all_final=std::ranges::all_of(tx.inputs,[](const auto& input){return input.sequence==UINT32_MAX;});
    Text(state,"review-locktime","Absolute locktime and policy",tx.locktime
        ? "Locktime "+std::to_string(*tx.locktime)+(*tx.locktime==0 ? " · no absolute constraint" :
            *tx.locktime<500000000 ? " · block height" : " · Unix time")
            +(*tx.locktime==0 ? "" : all_final ? " · inactive because all sequences are final" : " · chain maturity Unknown")
            +(imported.parsed->modifiable.value_or(0) ? " · PSBT fields marked modifiable" : " · no PSBT modification flags set")
        : "Locktime: Unknown");
}

void Advanced(State& state)
{
    Text(state,"sighash-title","Advanced sighash commitments","Advanced sighash commitments");
    Text(state,"sighash-all","SIGHASH_ALL","ALL commits to every output. Without ANYONECANPAY it also commits to the set of inputs.");
    Text(state,"sighash-none","SIGHASH_NONE","NONE commits to no outputs. A recipient or amount could be changed while that signature remains valid.");
    Text(state,"sighash-single","SIGHASH_SINGLE","SINGLE commits only to the output at that input's index. Other outputs remain uncommitted. A missing corresponding output is unsupported here.");
    Text(state,"sighash-anyone","SIGHASH_ANYONECANPAY","ANYONECANPAY commits that signature to its own input only. Other inputs can change. The base mode still determines its output commitments.");
    Text(state,"sighash-default","Taproot SIGHASH_DEFAULT","Taproot DEFAULT commits like ALL without ANYONECANPAY. It is the implicit mode for a 64-byte Taproot signature.");
    Text(state,"sighash-support","Sighash compatibility","Approval supports ALL and Taproot DEFAULT with matching proofs. Explicit Taproot ALL needs acknowledgement. Weak, unknown or conflicting modes block approval.");
}
} // namespace

bool IsSigningCommand(a::Command command)
{
    return command==a::Command::ReviewForSigning || command==a::Command::ApproveSigning || command==a::Command::SignTransaction;
}

const a::SigningApproval* CurrentSigningApproval(State& state,a::Requests& host,uint64_t id)
{
    if (!id || state.documents.ActiveId()!=id || !Unobstructed(state)) return nullptr;
    const auto current=Current(state,host,id,true);
    return current.basis ? state.signing_reviews.Authorize(*current.basis) : nullptr;
}

static a::Availability ValidateSigningRequest(const State& state,const a::Request& request)
{
    if (!IsSigningCommand(request.command) || request.document!=state.documents.ActiveId() || !Unobstructed(state))
        return {false,"The requested transaction view is no longer current."};
    if (request.component || request.edit || !request.value.empty() || request.option || request.section!=a::Section::Transactions)
        return {false,"Signing review commands take their proof only from the current transaction context."};
    return {true,{}};
}

a::Availability SigningCommandAvailability(State& state,a::Requests& host,const a::Request& request)
{
    const auto valid=ValidateSigningRequest(state,request);
    if (!valid.available) return valid;
    return Can(state,request,Current(state,host,request.document,request.command==a::Command::SignTransaction));
}

a::Receipt DispatchSigningCommand(State& state,a::Requests& host,const a::Request& request)
{
    const auto valid=ValidateSigningRequest(state,request);
    if (!valid.available) return {false,valid.reason};
    auto current=Current(state,host,request.document,true);
    if (current.basis) {
        if (state.signing_reviews.Revalidate(*current.basis)) ++state.signing_revision;
    }
    else RevokeSigningReview(state,request.document,current.reason);
    const auto availability=Can(state,request,current);
    if (!availability.available) return {false,availability.reason};
    // Signing never routes to the fallback host in this milestone. Its
    // availability is intentionally denied even when the exact approval exists.
    if (request.command==a::Command::SignTransaction) return {false,a::MissingDependency(request.command)};
    const bool review=request.command==a::Command::ReviewForSigning;
    const bool accepted=review ? state.signing_reviews.Record(std::move(*current.basis)) : state.signing_reviews.Approve(*current.basis);
    if (accepted) ++state.signing_revision;
    return {accepted,accepted ? (review ? "Exact signing review recorded. Approve separately." :
        "Approval recorded for this exact transaction. No signing was requested.") : "The exact signing action could not be recorded."};
}

void RevokeSigningReview(State& state,uint64_t id,std::string reason)
{
    if (state.signing_reviews.Revoke(id,std::move(reason))) ++state.signing_revision;
}

void PollSigningReviews(State& state,a::Requests& host)
{
    state.signing_reviews.RemoveClosed(state.documents);
    std::erase_if(state.signing_views,[&](const auto& pair){return !state.documents.Find(pair.first);});
    if (!Visible(state)) {
        if (!state.signing_views.empty() || !state.signing_reviews.PendingDocuments().empty()) ++state.signing_revision;
        state.signing_views.clear();
        state.signing_reviews.RetireAll("Workspace details were hidden, locked or retired.");
        return;
    }
    for (const auto id:state.signing_reviews.PendingDocuments()) {
        const auto current=Current(state,host,id);
        if (!current.basis) RevokeSigningReview(state,id,current.reason);
        else if (state.signing_reviews.Revalidate(*current.basis)) ++state.signing_revision;
    }
}

bool DrawSigningReview(State& state,a::Requests& host,Facts& facts,const imkit::Theme& theme)
{
    const auto owner=CurrentInspectionOwner(state,host);
    const auto* document=state.documents.Active();
    if (!owner || !document || !owner->content->psbt_version || !Unobstructed(state)) return false;
    const auto id=owner->id, revision=state.signing_revision;
    auto& panel=state.signing_views[id];
    const auto permitted=[&state,&host,owner,revision]{
        return state.signing_revision==revision && Unobstructed(state) && CurrentInspectionOwner(state,host)==owner;
    };
    if (!panel.open) {
        Button(state,facts,theme,"review-open","Signing review",true,[&state,id,permitted]{
            if (!permitted()) return false;
            state.signing_views[id].open=true;
            ++state.signing_revision;
            return true;
        });
        return panel.open;
    }
    const auto current=Current(state,host,id);
    const auto& parsed=*owner->content;
    const auto& tx=*parsed.transaction;
    const auto candidate=current.basis ? &*current.basis : nullptr;
    const auto* review=candidate ? state.signing_reviews.Recorded(*candidate) : nullptr;
    const auto* approval=candidate ? state.signing_reviews.Authorize(*candidate) : nullptr;
    const auto change=[&state,&panel,permitted](auto update) {
        if (!permitted()) return false;
        update(panel);
        ++state.signing_revision;
        return true;
    };
    imkit::PushTextRole(theme,imkit::TextRole::Heading);
    Text(state,"signing-review","Transaction review for signing","Transaction review");
    imkit::PopTextRole();
    if (Button(state,facts,theme,"review-back","Back to inspection",true,[change]{
        return change([](SigningView& view){view.open=false;});
    })) return false;
    ImGui::SameLine();
    Button(state,facts,theme,"advanced-sighash",panel.advanced ? "Hide sighash details" : "Advanced sighash",true,[change]{
        return change([](SigningView& view){view.advanced=!view.advanced;});
    });
    if (panel.advanced) Advanced(state);
    if (const auto origin=state.edited_origins.find(id);origin!=state.edited_origins.end() && !review)
        Text(state,"edited-review-status","Working revision requires review",
            "Changed "+origin->second.affected+". This working revision needs its own review and approval.",SemanticRole::Alert);
    if (candidate && candidate->requires_ack && !state.signing_reviews.Acknowledged(*candidate) && panel.advanced) {
        Button(state,facts,theme,"ack-sighash","Acknowledge exact sighash commitments",true,[&state,&host,permitted,id]{
            if (!permitted()) return false;
            auto fresh=Current(state,host,id,true);
            if (!fresh.basis || !state.signing_reviews.Acknowledge(std::move(*fresh.basis))) return false;
            ++state.signing_revision;
            return true;
        });
    }
    if (!panel.advanced) {
        Overview(state,*document->imported,current);
        const auto first=panel.output_page*PAGE;
        for (size_t n=first;n<std::min(first+PAGE,tx.outputs.size());++n) {
            ImGui::PushID(int(n));
            const auto& output=tx.outputs[n];
            const bool is_change=candidate && candidate->change_output==n;
            const auto rank=n+1-size_t(candidate && candidate->change_output && *candidate->change_output<n);
            const auto title=std::string(is_change ? "Proved intended change" : candidate ? "Recipient "+std::to_string(rank)
                : "Output "+std::to_string(n+1)+" · intent Unknown");
            Text(state,"review-payee",title.c_str(),title+" · "+Amount(output.value));
            auto address=protocol::OutputAddress(output.script,document->imported->network);
            Text(state,"review-destination","Exact destination",address ? *address :
                output.script.size()<=256 ? "Script "+output.script : "Full script in the output inspector.");
            ImGui::PopID();
        }
        if (tx.outputs.size()>PAGE) {
            Text(state,"review-output-range","Visible approved payment outputs",std::to_string(first+1)+"–"+
                std::to_string(std::min(first+PAGE,tx.outputs.size()))+" of "+std::to_string(tx.outputs.size()));
            Button(state,facts,theme,"earlier-payments","Earlier outputs",panel.output_page>0,[change]{
                return change([](SigningView& view){--view.output_page;});
            });
            ImGui::SameLine();
            Button(state,facts,theme,"later-payments","Later outputs",first+PAGE<tx.outputs.size(),[change]{
                return change([](SigningView& view){++view.output_page;});
            });
        }
        if (candidate && candidate->proof.change) Text(state,"review-change-proof","Change proof qualification",
            "Intended change matches the separately derived internal descriptor. Allocation and live spendability are unconfirmed.");
    }
    const auto first=panel.input_page*PAGE;
    for (size_t n=first;n<std::min(first+PAGE,tx.inputs.size());++n) {
        ImGui::PushID(int(n));
        const auto word="Input "+std::to_string(n+1)+" · "+SighashLabel(protocol::ReviewSighash(parsed,n));
        Text(state,"review-sighash-input","Input commitment",word);
        if (candidate) {
            const auto& pin=candidate->proof.inputs[n];
            Text(state,"review-scope-input","Reviewed previous-output proof",a::CoinKey(pin.point)+" · "+Amount(pin.value));
            Text(state,"review-policy-input","Independent public descriptor",pin.descriptor);
        }
        ImGui::PopID();
    }
    if (tx.inputs.size()>PAGE) {
        Button(state,facts,theme,"earlier-proofs","Earlier inputs",panel.input_page>0,[change]{
            return change([](SigningView& view){--view.input_page;});
        });
        ImGui::SameLine();
        Button(state,facts,theme,"later-proofs","Later inputs",first+PAGE<tx.inputs.size(),[change]{
            return change([](SigningView& view){++view.input_page;});
        });
    }
    if (!current.reason.empty()) {
        Text(state,"review-error","Review unavailable",current.reason,SemanticRole::Alert);
        const auto* connection=host.Connection();
        Button(state,facts,theme,"review-open-wallet","Open Wallet",host.Wallets() && connection,[&state,&host,permitted]{
            if (!permitted() || !host.Wallets() || !host.Connection()) return false;
            BeginOnboarding(state,OnboardingPage::Open,host);
            return state.onboarding.dialog.open;
        },false,"Choose the exact wallet explicitly. Wallet discovery requires the connected daemon adapter.");
        ImGui::SameLine();
        Button(state,facts,theme,"review-export-original","Export original",host.LocalDocuments(),[&state,&host,permitted]{
            return permitted() && BeginLocalDocument(state,host,LocalDocumentMode::Save);
        });
    }
    if (const auto reason=state.signing_reviews.Reason(id);!reason.empty()) Text(state,"approval-revocation","Signing approval revocation",reason,SemanticRole::Alert);
    if (review) Text(state,"review-commitment","Reviewed transaction commitment","Reviewed commitment: "+review->commitment.hex());
    Text(state,"approval-stage","Signing review state",approval ? "Approved for this exact transaction. No signing was requested."
        : review ? "Review recorded. Approve separately." : "Review and approval are separate actions.");
    const auto command=[&state,&host,id,permitted](a::Command action) {
        if (!permitted()) return false;
        const auto receipt=DispatchSigningCommand(state,host,{action,id});
        state.notice=receipt.reason;
        return receipt.accepted;
    };
    const auto record=Can(state,{a::Command::ReviewForSigning,id},current);
    Button(state,facts,theme,"review-record","Record signing review",record.available,[command]{return command(a::Command::ReviewForSigning);},
        !review,record.available ? "" : record.reason);
    ImGui::SameLine();
    const auto approve=Can(state,{a::Command::ApproveSigning,id},current);
    Button(state,facts,theme,"review-approve","Approve for signing",approve.available,[command]{return command(a::Command::ApproveSigning);},
        bool(review) && !approval,approve.available ? "" : approve.reason);
    if (review || (candidate && candidate->requires_ack && state.signing_reviews.Acknowledged(*candidate))) {
        Button(state,facts,theme,"review-revoke","Revoke signing review",true,[&state,id,permitted]{
            if (!permitted()) return false;
            RevokeSigningReview(state,id,"Cancelled in the transaction review.");
            return true;
        });
    }
    Text(state,"signer-unavailable","Signer availability","A transaction signer is unavailable.");
    return true;
}
} // namespace roots::gui::ui::shell
