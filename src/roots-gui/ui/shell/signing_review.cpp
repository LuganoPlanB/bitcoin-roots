// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/localization.h"
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include "app/daemon_signer.h"
#include "app/broadcast.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <imgui_internal.h>
#include <sodium.h>

namespace roots::gui::ui::shell {
DaemonUnlockView::~DaemonUnlockView() {sodium_memzero(passphrase.data(),passphrase.size());}

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
bool Unobstructed(const State& state,bool completing_exchange=false)
{
    const auto& local=state.local_document;
    const bool exchange=completing_exchange && local.signature_approval
        && (local.mode==LocalDocumentMode::Merge || local.mode==LocalDocumentMode::MergeText || local.mode==LocalDocumentMode::MergeQr);
    return Visible(state) && state.actions.kind==DocumentDialog::None && (!local.dialog.open || exchange)
        && !state.local_document.close_pending && !state.server.dialog.open && !state.onboarding.dialog.open
        && !state.recovery_dialog.open && !state.close_tab_popup && !state.close_document_popup
        && (!c::native_input || !c::native_input->NativeDialogActive() || (exchange && local.file_dialog));
}

void ClearUnlockInputEvents(ImGuiContext& context,bool retire)
{
    for (auto& event:context.InputEventsTrail) {
        if (event.Type==ImGuiInputEventType_Text) sodium_memzero(&event.Text,sizeof(event.Text));
    }
    auto& characters=context.IO.InputQueueCharacters;
    if (retire) characters.resize(0);
    if (characters.empty() && characters.Data)
        sodium_memzero(characters.Data,size_t(characters.Capacity)*sizeof(ImWchar));
    if (retire) {
        auto& pending=context.InputEventsQueue;
        auto* end=std::remove_if(pending.begin(),pending.end(),[](const auto& event){return event.Type==ImGuiInputEventType_Text;});
        if (end!=pending.end()) {
            sodium_memzero(end,size_t(pending.end()-end)*sizeof(ImGuiInputEvent));
            pending.resize(int(end-pending.begin()));
        }
    }
}

void ClearUnlockEditor(ImGuiID field)
{
    if (field && ImGui::GetCurrentContext()) {
        const auto wipe=[](ImVector<char>& bytes) {
            if (bytes.Data) sodium_memzero(bytes.Data,size_t(bytes.Capacity));
        };
        auto& ctx=*ImGui::GetCurrentContext();
        if (auto* edit=ImGui::GetInputTextState(field)) {
            ClearUnlockInputEvents(ctx,true);
            if (ctx.ActiveId==field) ImGui::ClearActiveID();
            wipe(edit->TextA);
            wipe(edit->TextToRevertTo);
            wipe(edit->CallbackTextBackup);
            if (edit->TextA.Size) edit->ClearText();
            edit->ID=0;
        }
        if (ctx.InputTextDeactivatedState.ID==field) {
            wipe(ctx.InputTextDeactivatedState.TextA);
            ctx.InputTextDeactivatedState.ClearFreeMemory();
        }
    }
}
void ClearUnlock(DaemonUnlockView& view)
{
    sodium_memzero(view.passphrase.data(),view.passphrase.size());
    ClearUnlockEditor(view.field);
    view.owner.reset();
    view.field=0;
    view.focus=false;
}

std::optional<a::DaemonSignAction> DaemonAction(a::Command command)
{
    switch (command) {
    case a::Command::SignTransaction: return a::DaemonSignAction::Sign;
    case a::Command::EnrichTransaction: return a::DaemonSignAction::Enrich;
    case a::Command::OpenDaemonUnlock: return a::DaemonSignAction::Unlock;
    default: return {};
    }
}
std::optional<a::BroadcastAction> BroadcastAction(a::Command command)
{
    switch (command) {
    case a::Command::CheckBroadcast: return a::BroadcastAction::Check;
    case a::Command::BroadcastTransaction: return a::BroadcastAction::Submit;
    case a::Command::RebroadcastTransaction: return a::BroadcastAction::Rebroadcast;
    default: return {};
    }
}
std::optional<a::BroadcastStatusContext> StatusContext(State& state,a::Requests& host,uint64_t id)
{
    if (!Unobstructed(state) || id!=state.documents.ActiveId()) return {};
    const auto owner=CurrentInspectionOwner(state,host);
    const auto* connection=host.Connection();
    if (!owner || !connection || !connection->Active()) return {};
    const auto genesis=Hash256::Parse(connection->Active()->genesis);
    if (!genesis || protocol::DocumentNetwork(NetworkIdentity{*genesis})!=owner->network) return {};
    auto raw=owner->content;
    if (const auto* artifact=FinalizedMaterial(state,id)) raw=artifact->final.document;
    if (!raw || raw->psbt_version || !raw->transaction || !raw->transaction->txid) return {};
    return a::BroadcastStatusContext{id,owner->session,owner->profile,owner->draft,owner->epoch,{*genesis},std::move(raw)};
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
    const auto original_view=state.imported_views.find(lineage.id);
    if (original_view!=state.imported_views.end() && original_view->second.replacement && proof_outputs!=facts.proof.recipients)
        return reject("A replacement must preserve the original recipient order. The unedited draft is retained.");
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
    const auto* replacement=view!=state.imported_views.end() && view->second.replacement ? &*view->second.replacement : nullptr;
    const auto local=view!=state.imported_views.end() ? view->second.offline_review_origin : std::nullopt;
    if (!funding && !replacement && (!local || !view->second.wallet || !view->second.review_evidence))
        return reject("Independent payment and wallet proofs are unavailable for this PSBT. Open the compatible wallet or export the original.");
    const auto origin=funding ? std::pair{funding->draft_document,funding->draft_revision}
        : replacement ? std::pair{replacement->scope.document,replacement->scope.revision} : *local;
    const auto* draft=state.documents.Find(origin.first);
    if (!draft || draft->draft_revision!=origin.second || !draft->complete || draft->observation==a::ObservationState::Locked)
        return reject("The independent originating draft is missing or its revision changed.");
    const auto& binding=funding ? funding->evidence.wallet : replacement ? replacement->coins.wallet : *view->second.wallet;
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
    if (replacement) {
        const auto& scope=replacement->scope;
        if (content!=replacement->working || epoch!=scope.identity.context.lock_epoch
            || session!=scope.identity.connection_session || profile!=scope.profile || draft->history.query_revision!=scope.query)
            return reject("The exact replacement relationship or its wallet context changed. Prepare a new fee draft.");
        if (!offline) {
            const auto history=host.Refresh() ? a::HistoryForDocument(*wallet,*host.Refresh(),epoch) : std::nullopt;
            if (!history || !history->identity || *history->identity!=scope.identity || !history->chain_current || !history->ledger)
                return reject("Refresh the original in the verified wallet before reviewing this fee draft.");
            const auto row=history->ledger->Rows().find(scope.txid.hex());
            if (row==history->ledger->Rows().end() || row->second.state!=HistoryState::Pending || row->second.confirmations
                || row->second.replaced_by || !row->second.mempool_conflicts.empty()
                || (row->second.wire_witness && row->second.wire_witness!=replacement->original->transaction->wtxid)
                || (!row->second.previous_hex.empty() && row->second.previous_hex!=replacement->original->source))
                return reject("The original payment was confirmed, replaced or changed. Its fee draft has no approval.");
        }
    }
    const auto proof=funding ? a::FundingReviewEvidence(*draft,imported) : replacement
        ? std::optional(replacement->proof) : view->second.review_evidence;
    if (!proof || proof->network!=imported.network || proof->inputs.size()!=tx.inputs.size()
        || proof->recipients.empty() || proof->recipients.size()+size_t(proof->change.has_value())!=tx.outputs.size()
        || (!replacement && !std::ranges::equal(proof->recipients,std::span(tx.outputs).first(proof->recipients.size()))))
        return reject("The ordered recipients, change, or independent input proof differs from the originating draft.");
    const auto change_output=replacement ? replacement->change_output
        : proof->change ? std::optional(tx.outputs.size()-1) : std::nullopt;
    const auto* coins=funding ? &funding->evidence : replacement ? &replacement->coins : draft->coin_draft ? &*draft->coin_draft : nullptr;
    auto digest=coins && coins->wallet==binding ? ProofDigest(*coins) : std::nullopt;
    if (!digest) return reject("An independent previous-output or change proof is missing or unavailable.");
    if (replacement) {
        Provenance relationship;
        relationship.Add("Exact replacement source and original wire v1");
        relationship.Add(digest->hex());
        relationship.Add(replacement->original->wire);
        relationship.Add(replacement->returned ? replacement->returned->source : replacement->working->source);
        relationship.Number(replacement->scope.rate_per_kvb);
        relationship.Number(replacement->cancellation.has_value());
        relationship.Number(replacement->child_package.has_value());
        if (const auto& package=replacement->child_package) {
            relationship.Add(package->owned_destination.descriptor);
            relationship.Number(package->owned_destination.index);
            relationship.Number(package->output);
            for (const auto& entry:package->package.ancestors) relationship.Add(entry.wtxid.hex());
        }
        if (const auto& intent=replacement->cancellation) {
            relationship.Add(intent->owned_destination.descriptor);
            relationship.Number(intent->owned_destination.index);
            relationship.Number(intent->original_fee.sats());
        }
        digest=relationship.Finish();
    }
    if (!tx.locktime || content->modifiable.value_or(0) || tx.coinbase || tx.inputs.empty())
        return reject("A fixed non-coinbase PSBT with known locktime and immutable payment fields is required.");
    if (validate && funding && !a::ValidCreatedFunding(imported))
        return reject("The saved independent funding evidence no longer validates.");
    if (validate && !funding) {
        const auto checked=protocol::InspectOfflineReview(*content,*proof,change_output);
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
        if (!derived || proof->change->observed_ms<=0 || !change_output || tx.outputs.at(*change_output).script!=derived->script
            || (observed && !PolicyScript(*observed,derived->script,{proof->change->index},true)))
            return reject("Intended change no longer matches the independent internal wallet proof.");
    }
    std::optional<Hash256> coin_policy;
    if (!offline && host.Refresh() && state.recovery) {
        const auto scope=a::WalletRefreshIdentity(*connection->Active(),session,*wallet,epoch);
        const auto fresh=host.Refresh()->View();
        if (scope && fresh.identity && *scope==*fresh.identity && fresh.ledger && fresh.chain_current
            && !fresh.offline && !fresh.locked && fresh.freshness[2]==Freshness::Live && fresh.ledger->InventoryAvailable()) {
            if (replacement) {
                if (!a::ReplacementInputsAllowed(*replacement,*wallet,fresh,*state.recovery,state.coin_controls))
                    return reject("A replacement input is frozen, reserved, daemon-locked or no longer independently eligible.");
                Provenance seen;
                seen.Add("Current exact replacement coin policy v1");
                seen.Add(replacement->scope.txid.hex());
                for (const auto& input:tx.inputs) seen.Add(a::CoinKey(input.outpoint));
                coin_policy=seen.Finish();
            } else {
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
                if (rules) coin_policy=seen.Finish();
            }
        }
    }
    return {a::SigningBasis{id,origin.first,wallet->id,session,profile,doc->draft_revision,origin.second,epoch,
        offline,binding,content,*proof,change_output,
        *digest,observed ? std::optional(PolicyDigest(*observed)) : std::nullopt,
        coin_policy,std::move(sighash),acknowledgement,replacement ? replacement->cancellation : std::nullopt,
        replacement ? replacement->child_package : std::nullopt},{}};
}

a::Availability Can(State& state,a::Requests& host,const a::Request& request,const Candidate& current)
{
    if (!Unobstructed(state) || request.document!=state.documents.ActiveId() || !request.document)
        return {false,"The requested transaction view is no longer current."};
    if (request.command==a::Command::CancelBroadcast) {
        const auto* submission=host.Broadcasts();
        return {submission && submission->Busy() && submission->View().document==request.document,
            "No submission work is pending in this view."};
    }
    if (request.command==a::Command::CheckBroadcastStatus || request.command==a::Command::MonitorBroadcast) {
        const auto scope=StatusContext(state,host,request.document);
        const auto* submission=host.Broadcasts();
        if (request.command==a::Command::MonitorBroadcast && submission && submission->Monitoring(request.document))
            return {true,{}};
        if (!scope || !submission) return {false,"Status Unknown without the exact raw transaction and a verified current node."};
        return submission->CanCheckStatus(*scope,state.recovery);
    }
    if (BroadcastFreezes(state,request.document))
        return {false,"Wait for the pending submission before changing this transaction."};
    if (!current.basis) return {false,current.reason};
    const auto& facts=*current.basis;
    if (const auto action=BroadcastAction(request.command)) {
        const auto* submission=host.Broadcasts();
        const auto* artifact=FinalizedMaterial(state,request.document);
        if (!submission) return {false,"The node submission service is unavailable."};
        if (const auto* replacement=host.ReplacementDrafts();replacement && replacement->Busy())
            return {false,"Wait for the current fee draft request to settle before submission."};
        if (!artifact || artifact->collected!=SigningMaterial(state,request.document))
            return {false,"Finalize the current approved collection first."};
        const auto view=state.signing_views.find(request.document);
        if (view==state.signing_views.end() || !view->second.open || state.daemon_operation || state.daemon_unlock.owner
            || !state.transaction_edits.empty())
            return {false,"Open the exact signing review and close any pending editor or daemon action."};
        return submission->Can(*action,state.signing_reviews.Authorize(facts),artifact,state.recovery);
    }
    if (const auto action=DaemonAction(request.command)) {
        if (!state.signing_reviews.Authorize(facts)) return {false,"Signing requires a matching current review and explicit approval."};
        const auto* signer=host.Signer();
        if (!signer) return {false,a::MissingDependency(a::Command::SignTransaction)};
        if (state.daemon_unlock.owner) return {false,"Finish or cancel the daemon unlock prompt first."};
        const auto view=state.signing_views.find(request.document);
        if (view==state.signing_views.end() || !view->second.open)
            return {false,"Open the signing review for this exact transaction."};
        if (*action==a::DaemonSignAction::Sign && view->second.daemon_signed)
            return {false,"Verified signature material has already been collected for this approval."};
        if (*action==a::DaemonSignAction::Sign && view->second.signing_uncertain)
            return {false,"Review and approve the retained original again before deliberately requesting a new signature."};
        const bool external=*action==a::DaemonSignAction::Sign && host.Connection()
            && host.Connection()->Active() && host.Connection()->Active()->external_signer==true;
        if (external && SignatureProgressFor(state,request.document,SigningMaterial(state,request.document)).Complete())
            return {false,"Every input already has the required verified signatures."};
        const auto device=external && host.Devices() ? host.Devices()->Current(host.Connection()) : SelectedDevice{};
        const auto unavailable=signer->Unavailable(facts,external ? a::DaemonSignAction::ExternalSign : *action,device);
        return {unavailable.empty(),unavailable};
    }
    const auto view=state.signing_views.find(request.document);
    if (view==state.signing_views.end() || !view->second.open) return {false,"Open the review for this exact transaction."};
    if (request.command==a::Command::FinalizeSigning) {
        const auto* approval=state.signing_reviews.Authorize(facts);
        if (!approval) return {false,"Review and approve this exact transaction before finalization."};
        if (state.daemon_operation || state.daemon_unlock.owner)
            return {false,"Finish or cancel the current daemon operation first."};
        const auto material=SigningMaterial(state,request.document);
        if (!material || !SignatureProgressFor(state,request.document,material).Complete())
            return {false,"Every input needs its required verified signatures before finalization."};
        const auto& previous=view->second.finalized;
        if (previous && previous->approval==*approval && previous->collected==material)
            return {false,"This exact collection is already finalized."};
        return {true,{}};
    }
    if (!state.signing_reviews.Acknowledged(facts)) return {false,"Read and acknowledge the advanced sighash commitments first."};
    if (request.command==a::Command::ReviewForSigning)
        return state.signing_reviews.Recorded(facts) ? a::Availability{false,"This exact transaction is already reviewed."}
            : a::Availability{true,{}};
    if (request.command==a::Command::ApproveSigning) {
        if (facts.cancellation || facts.child_package) {
            const auto* replacement=host.ReplacementDrafts();
            if (!replacement || facts.offline || !facts.coin_policy)
                return {false,facts.child_package ? "Approval needs a current online check of the parent, ancestors and wallet output policy."
                    : "Approval needs a current online check of the original payment and wallet input policy."};
            if (replacement->Busy()) return {false,"Wait for the current original-payment check."};
        }
        return {state.signing_reviews.Recorded(facts) && !state.signing_reviews.Authorize(facts),
            "Review this exact transaction before approving it. An existing approval needs no repeat."};
    }
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
    const auto pressed=theme::ActionButton(locale::Text(label),primary ? imkit::ActionVariant::Primary : imkit::ActionVariant::Secondary,{},theme);
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    facts.transaction_document[key]={{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),!enabled,
        pressed,pressed,ImGui::IsItemVisible()};
    const auto accessible=state.presentation.Interact(locale::Text(label),SemanticRole::Button,SemanticAction::Press|SemanticAction::Focus,
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
    return locale::BothUnits(value);
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

void Overview(State& state,const a::ImportedDocument& imported,const Candidate& candidate,bool finalized)
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
    if (!finalized) Text(state,"review-rate","Actual final fee rate","Final fee rate: Unknown until the signed size is known.");
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
    return command==a::Command::ReviewForSigning || command==a::Command::ApproveSigning
        || command==a::Command::FinalizeSigning || command==a::Command::CancelBroadcast || command==a::Command::CheckBroadcastStatus
        || command==a::Command::MonitorBroadcast
        || DaemonAction(command).has_value() || BroadcastAction(command).has_value();
}

const a::SigningApproval* CurrentSigningApproval(State& state,a::Requests& host,uint64_t id,bool completing_exchange)
{
    if (!id || state.documents.ActiveId()!=id || !Unobstructed(state,completing_exchange)) return nullptr;
    const auto current=Current(state,host,id,true);
    return current.basis ? state.signing_reviews.Authorize(*current.basis) : nullptr;
}

std::shared_ptr<const protocol::TransactionDocument> SigningMaterial(const State& state,uint64_t id)
{
    const auto* document=state.documents.Find(id);
    if (!document || !document->imported) return {};
    const auto original=document->imported->parsed;
    if (const auto at=state.signing_views.find(id);at!=state.signing_views.end()) {
        const auto& view=at->second;
        if (view.external_signed && view.external_source.lock()==original) return view.external_signed;
        if (view.daemon_signed) return view.daemon_signed;
    }
    if (const auto* artifact=FinalizedMaterial(state,id)) return artifact->collected;
    return original;
}

const a::ApprovedFinalTransaction* FinalizedMaterial(const State& state,uint64_t id)
{
    if (!Visible(state)) return nullptr;
    const auto* document=state.documents.Find(id);
    const auto at=state.signing_views.find(id);
    if (!document || !document->imported || document->observation==a::ObservationState::Locked
        || at==state.signing_views.end() || !at->second.finalized)
        return nullptr;
    const auto& artifact=*at->second.finalized;
    const auto& basis=artifact.approval.reviewed;
    return basis.content==document->imported->parsed && basis.document_revision==document->draft_revision
        && basis.proof.network==document->imported->network ? &artifact : nullptr;
}

bool BroadcastFreezes(const State& state,uint64_t id)
{
    if (!state.broadcasts || !state.broadcasts->Busy()) return false;
    const auto* document=state.documents.Find(id);
    return state.broadcasts->Freezes(id,document && document->imported ? document->imported->parsed.get() : nullptr);
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
    if (request.command==a::Command::CheckBroadcastStatus || request.command==a::Command::CancelBroadcast
        || request.command==a::Command::MonitorBroadcast)
        return Can(state,host,request,{});
    return Can(state,host,request,Current(state,host,request.document,
        DaemonAction(request.command).has_value() || request.command==a::Command::FinalizeSigning
        || BroadcastAction(request.command).has_value()));
}

a::Receipt DispatchSigningCommand(State& state,a::Requests& host,const a::Request& request)
{
    const auto valid=ValidateSigningRequest(state,request);
    if (!valid.available) return {false,valid.reason};
    if (request.command==a::Command::CheckBroadcastStatus || request.command==a::Command::MonitorBroadcast) {
        const auto available=Can(state,host,request,{});
        if (!available.available) return {false,available.reason};
        const bool monitor=request.command==a::Command::MonitorBroadcast;
        auto* submission=host.Broadcasts();
        if (monitor && submission->Monitoring(request.document)) {
            const auto stopped=submission->StopMonitoring();
            if (stopped) ++state.signing_revision;
            return {stopped,"Monitoring paused."};
        }
        auto result=submission->StartStatus(*StatusContext(state,host,request.document),*state.recovery,monitor);
        ++state.signing_revision;
        return result;
    }
    if (request.command==a::Command::CancelBroadcast) {
        const auto available=Can(state,host,request,{});
        if (!available.available) return {false,available.reason};
        const auto accepted=host.Broadcasts()->Cancel();
        if (accepted) ++state.signing_revision;
        return {accepted,accepted ? "Stopping the local wait. Any possible submission remains recorded." : "The wait is already ending."};
    }
    auto current=Current(state,host,request.document,true);
    if (current.basis) {
        if (state.signing_reviews.Revalidate(*current.basis)) ++state.signing_revision;
    }
    else RevokeSigningReview(state,request.document,current.reason);
    const auto availability=Can(state,host,request,current);
    if (!availability.available) return {false,availability.reason};
    if (const auto action=BroadcastAction(request.command)) {
        auto result=host.Broadcasts()->Start(*action,state.signing_reviews,*current.basis,
            *FinalizedMaterial(state,request.document),*state.recovery);
        ++state.signing_revision;
        return result;
    }
    if (request.command==a::Command::FinalizeSigning) {
        const auto material=SigningMaterial(state,request.document);
        auto result=state.signing_reviews.Finalize(*current.basis,material);
        auto& view=state.signing_views[request.document];
        if (!result.artifact) {
            view.finalization_error=std::move(result.error);
            ++state.signing_revision;
            return {false,view.finalization_error};
        }
        const auto* approval=CurrentSigningApproval(state,host,request.document);
        if (!approval || *approval!=result.artifact->approval || SigningMaterial(state,request.document)!=material)
            return {false,"The signing context changed. The collected PSBT is retained."};
        view.finalized=std::move(result.artifact);
        view.finalization_error.clear();
        ++state.signing_revision;
        return {true,"Exact final transaction created locally. The original and collected PSBTs are retained."};
    }
    if (const auto action=DaemonAction(request.command)) {
        if (*action==a::DaemonSignAction::Unlock) {
            state.daemon_unlock.owner=*current.basis;
            state.daemon_unlock.focus=true;
            ++state.signing_revision;
            return {true,"Enter the passphrase for the current daemon wallet."};
        }
        const bool external=*action==a::DaemonSignAction::Sign && host.Connection()
            && host.Connection()->Active() && host.Connection()->Active()->external_signer==true;
        const auto device=external && host.Devices() ? host.Devices()->Current(host.Connection()) : SelectedDevice{};
        if (!host.Signer()->Start(state.signing_reviews,*current.basis,
                external ? a::DaemonSignAction::ExternalSign : *action,{},device))
            return {false,"The scoped daemon request could not start. The approved PSBT is retained."};
        state.daemon_operation=request.document;
        state.signing_views[request.document].daemon_status=external ? DeviceStateText(DeviceState::Confirm)
            : "Waiting for the current daemon wallet.";
        ++state.signing_revision;
        return {true,"Requested once. The original PSBT remains available."};
    }
    if (request.command==a::Command::ApproveSigning && (current.basis->cancellation || current.basis->child_package)) {
        auto result=host.ReplacementDrafts()->StartApproval(state.signing_reviews,*current.basis);
        state.signing_views[request.document].cancellation_status=result.reason;
        ++state.signing_revision;
        return result;
    }
    const bool review=request.command==a::Command::ReviewForSigning;
    const bool accepted=review ? state.signing_reviews.Record(std::move(*current.basis)) : state.signing_reviews.Approve(*current.basis);
    if (accepted) ++state.signing_revision;
    return {accepted,accepted ? (review ? "Exact signing review recorded. Approve separately." :
        "Approval recorded for this exact transaction. No signing was requested.") : "The exact signing action could not be recorded."};
}

void RevokeSigningReview(State& state,uint64_t id,std::string reason)
{
    if (state.signing_reviews.Revoke(id,std::move(reason))) {
        ++state.signing_revision;
        if (const auto view=state.signing_views.find(id);view!=state.signing_views.end()) {
            view->second.daemon_signed.reset();
            view->second.daemon_enriched.reset();
            view->second.daemon_status.clear();
            view->second.cancellation_status.clear();
            view->second.signing_uncertain=false;
        }
    }
}

void RetireDaemonSigning(State& state,a::Requests& host)
{
    ClearUnlock(state.daemon_unlock);
    if (auto* devices=host.Devices()) devices->Cancel();
    if (auto* signer=host.Signer()) signer->Cancel();
}

bool CloseSigningReview(State& state,a::Requests& host,uint64_t id)
{
    const auto view=state.signing_views.find(id);
    if (state.documents.ActiveId()!=id || view==state.signing_views.end() || !view->second.open
        || state.daemon_unlock.owner) return false;
    view->second.open=false;
    if (state.daemon_operation==id) RetireDaemonSigning(state,host);
    ++state.signing_revision;
    return true;
}

void CollectDaemonSigningResult(State& state,a::Requests& host,const a::SigningApproval& approval,
    a::DaemonSignResult result)
{
    const auto id=approval.reviewed.document;
    auto& view=state.signing_views.at(id);
    view.daemon_status=std::move(result.status);
    const bool external=result.action==a::DaemonSignAction::ExternalSign;
    const bool signed_reply=result.action==a::DaemonSignAction::Sign || external;
    if (signed_reply && result.success && result.psbt && !result.original_retired && view.external_signed
        && view.external_source.lock()==approval.reviewed.content) {
        const auto combined=protocol::CombineVerifiedCollections(*view.external_signed,*result.psbt);
        const auto& basis=approval.reviewed;
        const auto checked=combined.document
            ? protocol::InspectOfflineReview(*combined.document,basis.proof,basis.change_output) : protocol::OfflineReview{};
        if (checked.commitment==approval.commitment && checked.change_output==basis.change_output) {
            if (combined.document->wire!=view.external_signed->wire) view.external_signed=combined.document;
        } else {
            view.daemon_status="The signing result conflicts with the verified collection. Both the existing collection and original are retained.";
            state.signing_reviews.Revoke(id,view.daemon_status);
            if (external && host.Devices()) host.Devices()->Cancel();
            result.success=false;
            result.psbt.reset();
        }
    }
    if (result.action==a::DaemonSignAction::Sign || (external && result.success)) {
        view.daemon_signed=std::move(result.psbt);
        view.signing_uncertain=result.uncertain;
    }
    if (result.action==a::DaemonSignAction::Enrich) view.daemon_enriched=std::move(result.psbt);
}

void PollSigningReviews(State& state,a::Requests& host)
{
    state.signing_reviews.RemoveClosed(state.documents);
    std::erase_if(state.signing_views,[&](const auto& pair){return !state.documents.Find(pair.first);});
    const bool visible=Visible(state);
    const auto current_approval=[&](uint64_t id)->const a::SigningApproval* {
        if (!id || state.documents.ActiveId()!=id || !Unobstructed(state)) return nullptr;
        const auto view=state.signing_views.find(id);
        if (view==state.signing_views.end() || !view->second.open) return nullptr;
        const auto current=Current(state,host,id);
        return current.basis ? state.signing_reviews.Authorize(*current.basis) : nullptr;
    };
    if (auto* service=host.ReplacementDrafts();service && service->ApprovalBasis()) {
        const auto id=service->ApprovalBasis()->document;
        const auto view=state.signing_views.find(id);
        const bool current_view=visible && Unobstructed(state) && state.documents.ActiveId()==id
            && view!=state.signing_views.end() && view->second.open;
        const auto current=current_view ? Current(state,host,id,true) : Candidate{};
        if (auto result=service->PollApproval(state.signing_reviews,current.basis ? &*current.basis : nullptr)) {
            if (!result->accepted) RevokeSigningReview(state,id,result->reason);
            if (visible && state.signing_views.contains(id))
                state.signing_views[id].cancellation_status=std::move(result->reason);
            ++state.signing_revision;
        }
    }
    if (auto& unlock=state.daemon_unlock;unlock.owner) {
        const auto* approval=current_approval(unlock.owner->document);
        if (!approval || approval->reviewed!=*unlock.owner || !state.signing_views[unlock.owner->document].open) {
            ClearUnlock(unlock);
            ++state.signing_revision;
        }
    }
    if (auto* signer=host.Signer();signer && signer->Busy()) {
        const auto id=state.daemon_operation;
        const auto* approval=current_approval(id);
        const bool review_current=approval!=nullptr;
        if (auto result=signer->Poll(approval)) {
            const bool external=result->action==a::DaemonSignAction::ExternalSign;
            if (result->original_retired) {
                if (external) state.signing_reviews.Revoke(id,result->status);
                else RevokeSigningReview(state,id,result->status);
            }
            else if (review_current) CollectDaemonSigningResult(state,host,*approval,std::move(*result));
            ++state.signing_revision;
        }
        if (!signer->Busy() && std::exchange(state.daemon_operation,0)) {
            if (visible && !review_current && state.signing_views.contains(id))
                state.signing_views[id].daemon_status="The operation was retired. The original PSBT remains available.";
        }
    }
    if (!visible) {
        if (!state.signing_views.empty() || !state.signing_reviews.PendingDocuments().empty()) ++state.signing_revision;
        state.signing_views.clear();
        state.signing_reviews.RetireAll("Workspace details were hidden, locked or retired.");
        return;
    }
    for (const auto id:state.signing_reviews.PendingDocuments()) {
        const auto current=Current(state,host,id);
        if (!current.basis) RevokeSigningReview(state,id,current.reason);
        else if (state.signing_reviews.Revalidate(*current.basis)) {
            ++state.signing_revision;
            if (auto view=state.signing_views.find(id);view!=state.signing_views.end()) {
                view->second.daemon_signed.reset();
                view->second.daemon_enriched.reset();
                view->second.daemon_status.clear();
                view->second.cancellation_status.clear();
                view->second.signing_uncertain=false;
            }
        }
    }
}

void PollBroadcast(State& state,a::Requests& host)
{
    auto* submission=host.Broadcasts();
    if (!submission) return;
    const auto phase=submission->View().stage;
    const auto id=submission->View().document;
    if (submission->StatusActive()) {
        const auto scope=StatusContext(state,host,id);
        std::optional<std::string> tip;
        const auto* connection=host.Connection();
        if (scope && connection && connection->Active() && host.Refresh()) {
            const auto refresh=host.Refresh()->View();
            if (refresh.identity && refresh.node && !refresh.offline && !refresh.locked
                && refresh.freshness[0]==Freshness::Live && refresh.identity->connection_session==scope->session
                && refresh.identity->profile==connection->Active()->candidate
                && refresh.identity->context.network==scope->network && refresh.identity->context.lock_epoch==scope->epoch
                && Hash256::Parse(refresh.node->tip))
                tip=refresh.node->tip;
        }
        submission->PollStatus(scope,state.recovery,tip);
        if (phase!=submission->View().stage) ++state.signing_revision;
        return;
    }
    if (!submission->Busy() && phase!=a::BroadcastStage::Ready) return;
    const auto view=state.signing_views.find(id);
    const auto* approval=view!=state.signing_views.end() && view->second.open ? CurrentSigningApproval(state,host,id) : nullptr;
    const auto* artifact=FinalizedMaterial(state,id);
    if (artifact && artifact->collected!=SigningMaterial(state,id)) artifact=nullptr;
    submission->Poll(state.signing_reviews,approval,artifact,state.recovery);
    if (phase!=submission->View().stage) {
        if (submission->View().original_retired) RevokeSigningReview(state,id,submission->View().message);
        ++state.signing_revision;
    }
}

void DrawBroadcastStatus(State& state,a::Requests& host,Facts& facts,const imkit::Theme& theme)
{
    const auto* submission=host.Broadcasts();
    if (!submission) return;
    const auto owner=CurrentInspectionOwner(state,host);
    if (!owner || !Unobstructed(state)) return;
    const auto revision=state.signing_revision;
    const auto action=[&](a::Command cmd,const char* key,const char* label){
        const a::Request request{cmd,owner->id};
        const auto available=SigningCommandAvailability(state,host,request);
        Button(state,facts,theme,key,label,available.available,[&state,&host,owner,revision,request] {
            if (state.signing_revision!=revision || CurrentInspectionOwner(state,host)!=owner) return false;
            const auto result=DispatchSigningCommand(state,host,request);
            state.notice=result.reason;
            return result.accepted;
        },false,available.available ? "" : available.reason);
    };
    action(a::Command::CheckBroadcastStatus,"check-broadcast-status","Check Status");
    theme::SameLineForAction(locale::Text(submission->Monitoring(owner->id) ? "Stop monitoring" : "Monitor transaction"));
    action(a::Command::MonitorBroadcast,"monitor-broadcast",submission->Monitoring(owner->id) ? "Stop monitoring" : "Monitor transaction");
    const auto& result=submission->View();
    if (result.document!=owner->id) return;
    if (submission->Busy()) {
        theme::SameLineForAction(locale::Text("Cancel wait"));
        action(a::Command::CancelBroadcast,"broadcast-cancel","Cancel wait");
    }
    if (!result.message.empty()) {
        const bool alert=result.stage==a::BroadcastStage::Rejected || result.stage==a::BroadcastStage::Uncertain
            || result.stage==a::BroadcastStage::Refused || result.stage==a::BroadcastStage::Recorded
            || result.stage==a::BroadcastStage::StatusUnknown;
        Text(state,"broadcast-status","Transaction submission",result.message,alert ? SemanticRole::Alert : SemanticRole::Text);
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
    if (candidate && candidate->cancellation) {
        Text(state,"review-cancellation-intent","Competing self-payment",
            "This draft redirects the original inputs to a verified address in your wallet. The original may confirm first; cancellation is never guaranteed.");
    }
    if (candidate && candidate->child_package) {
        Text(state,"review-child-intent","Child pays for parent",
            "This draft spends the selected unconfirmed parent output to a verified address in your wallet. Approval requires the exact current parent and package.");
    }
    if (!panel.cancellation_status.empty())
        Text(state,"review-cancellation-check","Fee-action eligibility check",panel.cancellation_status);
    const auto change=[&state,&panel,permitted](auto update) {
        if (!permitted()) return false;
        update(panel);
        ++state.signing_revision;
        return true;
    };
    imkit::PushTextRole(theme,imkit::TextRole::Heading);
    Text(state,"signing-review","Transaction review for signing","Transaction review");
    imkit::PopTextRole();
    if (Button(state,facts,theme,"review-back","Back to inspection",!state.daemon_unlock.owner,[&state,&host,id,permitted]{
        return permitted() && CloseSigningReview(state,host,id);
    })) return false;
    theme::SameLineForAction(panel.advanced ? "Hide sighash details" : "Advanced sighash");
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
        Overview(state,*document->imported,current,FinalizedMaterial(state,id)!=nullptr);
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
    Text(state,"approval-stage","Signing review state",approval ? "Approved for this exact transaction. Signing is a separate action."
        : review ? "Review recorded. Approve separately." : "Review and approval are separate actions.");
    const auto command=[&state,&host,id,permitted](a::Command action) {
        if (!permitted()) return false;
        const auto receipt=DispatchSigningCommand(state,host,{action,id});
        state.notice=receipt.reason;
        return receipt.accepted;
    };
    const auto record=Can(state,host,{a::Command::ReviewForSigning,id},current);
    Button(state,facts,theme,"review-record","Record signing review",record.available,[command]{return command(a::Command::ReviewForSigning);},
        !review,record.available ? "" : record.reason);
    theme::SameLineForAction(locale::Text("Approve for signing"));
    const auto approve=Can(state,host,{a::Command::ApproveSigning,id},current);
    Button(state,facts,theme,"review-approve","Approve for signing",approve.available,[command]{return command(a::Command::ApproveSigning);},
        bool(review) && !approval,approve.available ? "" : approve.reason);
    if (review || (candidate && candidate->requires_ack && state.signing_reviews.Acknowledged(*candidate))) {
        Button(state,facts,theme,"review-revoke","Revoke signing review",true,[&state,id,permitted]{
            if (!permitted()) return false;
            RevokeSigningReview(state,id,"Cancelled in the transaction review.");
            return true;
        });
    }
    const auto draw_finalization=[&] {
        const auto available=Can(state,host,{a::Command::FinalizeSigning,id},current);
        Button(state,facts,theme,"finalize-signing","Finalize transaction",available.available,
            [command]{return command(a::Command::FinalizeSigning);},available.available,available.reason);
        if (!panel.finalization_error.empty())
            Text(state,"finalization-error","Finalization status",panel.finalization_error,SemanticRole::Alert);
        const auto* artifact=FinalizedMaterial(state,id);
        if (!artifact) return;
        const auto& final=artifact->final;
        const auto data=final.document;
        const auto& actual=*data->transaction;
        const auto vsize=std::to_string(final.vsize);
        const auto fee=std::to_string(final.fee.sats());
        Text(state,"final-transaction-title","Finalized transaction","Finalized transaction");
        const auto* current_approval=candidate ? state.signing_reviews.Authorize(*candidate) : nullptr;
        if (!current_approval || *current_approval!=artifact->approval || SigningMaterial(state,id)!=artifact->collected)
            Text(state,"final-previous-approval","Final transaction provenance",
                "These bytes came from an earlier approved collection. You can still save or inspect them.");
        Text(state,"final-txid","Final transaction ID","Transaction ID: "+actual.txid->hex());
        Text(state,"final-wtxid","Final witness transaction ID","Witness transaction ID: "+actual.wtxid->hex());
        Text(state,"final-size","Final transaction size and weight",std::to_string(final.size)+" bytes · "+
            std::to_string(final.weight)+" WU · "+vsize+" vB");
        Text(state,"final-rate","Actual final fee rate","Final fee rate: "+fee+" sats / "+vsize+" vB");
        const auto artifact_current=[&state,id,permitted,data] {
            if (!permitted()) return false;
            const auto* latest=FinalizedMaterial(state,id);
            return latest && latest->final.document==data;
        };
        Button(state,facts,theme,"save-final-transaction","Save final transaction",host.LocalDocuments(),
            [&state,&host,artifact_current] {
                return artifact_current() && BeginLocalDocument(state,host,LocalDocumentMode::SaveFinalized);
            });
        theme::SameLineForAction("View final transaction");
        if (Button(state,facts,theme,"view-final-transaction","View final transaction",state.documents.Documents().size()<32,
            [&state,artifact_current,data,network=owner->network] {
                if (!artifact_current()) return false;
                std::string error;
                if (OpenLocalData(state,data,network,error)) return true;
                state.notice=std::move(error);
                return false;
            })) return;
        theme::SameLineForAction("Show final transaction QR");
        Button(state,facts,theme,"final-transaction-qr","Show final transaction QR",host.LocalDocuments(),[&state,&host,id]{
            return state.documents.ActiveId()==id && BeginLocalDocument(state,host,LocalDocumentMode::ShowFinalQr);
        });
        if (host.Broadcasts()) {
            const auto action=[&](a::Command cmd,const char* key,const char* label,bool primary=false) {
                const auto status=Can(state,host,{cmd,id},current);
                Button(state,facts,theme,key,label,status.available,[command,cmd]{return command(cmd);},
                    primary && status.available,status.available ? "" : status.reason);
            };
            Text(state,"broadcast-fee-limit","Broadcast fee policy","Maximum submission fee rate: 100 sat/vB.");
            action(a::Command::CheckBroadcast,"broadcast-check","Check node policy");
            theme::SameLineForAction(locale::Text("Broadcast"));
            action(a::Command::BroadcastTransaction,"broadcast-submit","Broadcast",true);
            theme::SameLineForAction("Rebroadcast same bytes");
            action(a::Command::RebroadcastTransaction,"broadcast-retry","Rebroadcast same bytes");
            DrawBroadcastStatus(state,host,facts,theme);
        }
    };
    auto* signer=host.Signer();
    if (!signer) {
        Text(state,"signer-unavailable","Signer availability","A transaction signer is unavailable.");
        DrawSignatureExchange(state,host,facts,theme,*owner,approval!=nullptr);
        draw_finalization();
        return true;
    }
    if (!panel.daemon_status.empty())
        Text(state,"daemon-signing-status","Daemon signing status",panel.daemon_status);
    if (state.daemon_operation==id && signer->Busy()) {
        Button(state,facts,theme,"cancel-daemon-signing","Cancel waiting",true,[&state,&panel,signer,permitted]{
            if (!permitted()) return false;
            signer->Cancel();
            panel.daemon_status="Waiting cancelled. Work already received by the daemon may finish. The original PSBT is retained.";
            ++state.signing_revision;
            return true;
        });
    }
    auto& unlock=state.daemon_unlock;
    if (unlock.owner && unlock.owner->document==id) {
        Text(state,"unlock-daemon-boundary","Daemon unlock timeout",
            "Unlock for 30 seconds. This changes only the daemon wallet lock. Signing requires a separate action.");
        ImGui::TextUnformatted("Wallet passphrase");
        const bool opening=std::exchange(unlock.focus,false);
        if (opening) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        const bool entered=ImGui::InputText("##daemon-wallet-passphrase",unlock.passphrase.data(),unlock.passphrase.size(),
            ImGuiInputTextFlags_Password|ImGuiInputTextFlags_NoUndoRedo|ImGuiInputTextFlags_EnterReturnsTrue);
        unlock.field=ImGui::GetItemID();
        const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
        const bool typing=ImGui::IsItemActive();
        facts.transaction_document["daemon-wallet-passphrase"]={{lo.x,lo.y,hi.x,hi.y},unlock.field,ImGui::IsItemFocused(),
            false,typing,false,ImGui::IsItemVisible()};
        state.presentation.Interact("Wallet passphrase",SemanticRole::TextField,SemanticAction::None,
            false,false,{},"Protected keyboard input. The value is never published.");
        if (typing) ClearUnlockInputEvents(*ImGui::GetCurrentContext(),false);
        if (ImGui::IsItemDeactivated()) ClearUnlockEditor(unlock.field);
        const auto submit=[&state,&host,&unlock,id,permitted] {
            if (!permitted() || !unlock.owner || !unlock.passphrase[0]) return false;
            const auto fresh=Current(state,host,id,true);
            const bool accepted=fresh.basis && *fresh.basis==*unlock.owner && host.Signer()->Start(state.signing_reviews,
                *fresh.basis,a::DaemonSignAction::Unlock,unlock.passphrase.data());
            ClearUnlock(unlock);
            ++state.signing_revision;
            if (accepted) state.daemon_operation=id;
            state.signing_views[id].daemon_status=accepted ? "Waiting for the daemon unlock response."
                : "The unlock request was retired. The original PSBT remains available.";
            return accepted;
        };
        Button(state,facts,theme,"submit-daemon-unlock","Unlock for 30 seconds",unlock.passphrase[0],submit,true);
        if (entered && unlock.owner) submit();
        ImGui::SameLine();
        Button(state,facts,theme,"cancel-daemon-unlock","Cancel",bool(unlock.owner),[&state,&unlock,permitted] {
            if (!permitted()) return false;
            ClearUnlock(unlock);
            ++state.signing_revision;
            return true;
        });
        if ((opening || typing) && ImGui::GetItemRectMax().y>ImGui::GetCurrentWindow()->InnerClipRect.Max.y)
            ImGui::SetScrollHereY(1.0f);
        return true;
    }
    const bool external=host.Connection() && host.Connection()->Active() && host.Connection()->Active()->external_signer==true;
    if (external) DrawDeviceSigner(state,host,facts.transaction_document,theme,signer->Busy() && state.daemon_operation!=id);
    if (approval) {
        const auto operation=[&](a::Command cmd,const char* key,const char* label,bool primary=false) {
            const auto available=Can(state,host,{cmd,id},current);
            Button(state,facts,theme,key,label,available.available,[command,cmd]{return command(cmd);},primary,available.reason);
        };
        operation(a::Command::SignTransaction,external ? "sign-on-device" : "sign-with-daemon",
            external ? "Sign on device" : "Sign with daemon",true);
        if (!external) {
            theme::SameLineForAction(locale::Text("Unlock daemon wallet"));
            operation(a::Command::OpenDaemonUnlock,"open-daemon-unlock","Unlock daemon wallet");
        }
        theme::SameLineForAction(locale::Text("Enrich PSBT"));
        operation(a::Command::EnrichTransaction,"enrich-psbt","Enrich PSBT");
        const auto open=[&](const auto& data,const char* key,const char* label) {
            if (!data) return;
            Button(state,facts,theme,key,label,true,[&state,permitted,data,network=document->imported->network] {
                if (!permitted()) return false;
                std::string error;
                if (OpenLocalData(state,data,network,error)) return true;
                state.notice=std::move(error);
                return false;
            });
        };
        open(panel.daemon_signed,"open-daemon-signed","Open verified signed PSBT");
        open(panel.daemon_enriched,"open-daemon-enriched","Open enriched PSBT");
    }
    DrawSignatureExchange(state,host,facts,theme,*owner,approval!=nullptr);
    draw_finalization();
    return true;
}
} // namespace roots::gui::ui::shell
