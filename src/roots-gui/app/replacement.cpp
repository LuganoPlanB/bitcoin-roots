// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/replacement.h"
#include "services/funding.h"
#include "services/wallet_lifecycle.h"

#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace roots::gui::app {
namespace {
using Clock = std::chrono::steady_clock;
using Json = rpc::Json;
using Content = std::shared_ptr<const protocol::TransactionDocument>;
constexpr int64_t MAX_MONEY=2100000000000000LL;
constexpr uint32_t MAX_RATE=100000; // sat/kvB, or 100 sat/vB
constexpr size_t MAX_INPUTS=16;
constexpr size_t MAX_OUTPUTS=64;
constexpr size_t MAX_PROOF_BYTES=256*1024;

void Require(bool ok,const char* reason)
{
    if (!ok) throw std::runtime_error(reason);
}
std::string_view Text(const Json* value)
{
    return value && value->kind==Json::Kind::String ? value->text : std::string_view{};
}
std::optional<bool> Flag(const Json* value)
{
    if (!value || value->kind!=Json::Kind::Boolean) return {};
    return value->text=="true";
}
std::optional<int64_t> Integer(const Json* value)
{
    if (!value || value->kind!=Json::Kind::Number) return {};
    int64_t result{};
    const auto parsed=std::from_chars(value->text.data(),value->text.data()+value->text.size(),result);
    return parsed.ec==std::errc{} && parsed.ptr==value->text.data()+value->text.size() ? std::optional(result) : std::nullopt;
}
std::optional<Money> Amount(const Json* value)
{
    if (!value || (value->kind!=Json::Kind::Number && value->kind!=Json::Kind::String)) return {};
    auto amount=Money::ParseJsonBtc(value->text);
    return amount && amount->sats()>=0 && amount->sats()<=MAX_MONEY ? amount : std::nullopt;
}
void Add(int64_t& sum,Money value)
{
    Require(value.sats()>=0 && value.sats()<=MAX_MONEY-sum,"Replacement input or output totals exceed the monetary bound.");
    sum+=value.sats();
}
bool Signal(const protocol::TransactionInspection& tx)
{
    return std::ranges::any_of(tx.inputs,[](const auto& input){ return input.sequence<0xfffffffeU; });
}
std::string_view Eligibility(const HistoryTransaction& row,FeeDraftKind kind)
{
    if (row.replaced_by || row.state==HistoryState::Replaced) return "This transaction has already been replaced.";
    if (row.confirmations>0 || row.state==HistoryState::Confirmed) return "This transaction is already confirmed.";
    if (row.state!=HistoryState::Pending || row.confirmations || row.generated || !row.mempool_conflicts.empty())
        return "This transaction is no longer an eligible pending payment.";
    if (kind!=FeeDraftKind::ChildPaysForParent
        && (!row.fee || std::ranges::none_of(row.details,[](const auto& detail){return detail.category=="send";})))
        return "Select an outgoing wallet payment with an observed fee.";
    return {};
}
bool SupportsReplacement(const BackendCapabilities& caps,FeeDraftKind kind)
{
    const auto supports=[&](std::string_view method,std::string_view argument={}) {
        return caps.Supports(method,argument)==SupportReason::Available;
    };
    const bool common=caps.descriptors==true && caps.external_signer==false && !caps.wallet_scanning
        && supports("getwalletinfo") && supports("gettransaction") && supports("listdescriptors","private")
        && supports("getaddressinfo") && supports("getblockhash") && supports("getbestblockhash") && supports("getmempoolentry");
    if (kind==FeeDraftKind::ChildPaysForParent)
        return common && supports("getmempoolancestors","verbose") && supports("getmempoolcluster")
            && supports("getmempoolinfo") && supports("getrawtransaction","verbosity")
            && supports("gettxout","include_mempool") && supports("listlockunspent");
    if (kind==FeeDraftKind::CancelPayment) return common && supports("getnetworkinfo");
    return common && kind==FeeDraftKind::ReplaceByFee && caps.version>=210000
        && supports("psbtbumpfee","txid") && supports("psbtbumpfee","options.fee_rate")
        && supports("psbtbumpfee","options.original_change_index") && supports("psbtbumpfee","options.psbt_version")
        && supports("gettxout");
}

class Probe {
    rpc::Transport& transport;
    const ServerCandidate& profile;
    std::string tip;
    const std::stop_token stop;
    const Clock::time_point deadline=Clock::now()+std::chrono::seconds(15);
    uint64_t id{};
public:
    const protocol::DisplayNetwork network;
    const std::string& Tip() const {return tip;}
    PackageRead PackageReader()
    {
        return [this](std::string method,std::string args,const char* reason) {
            return Read(std::move(method),std::move(args),false,reason);
        };
    }
    Probe(rpc::Transport& rpc,const ServerCandidate& target,NetworkIdentity identity,std::string expected_tip,std::stop_token token)
        : transport(rpc),profile(target),tip(std::move(expected_tip)),stop(token),network(protocol::DocumentNetwork(identity)) {}
    rpc::Reply Call(std::string method,std::string args,bool wallet,rpc::Effect effect=rpc::Effect::ReadOnly)
    {
        const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now());
        Require(!stop.stop_requested() && left>std::chrono::milliseconds(0),"Replacement preparation was cancelled or expired.");
        rpc::Limits limits;
        limits.total_timeout=left;
        limits.connect_timeout=std::min(left,std::chrono::milliseconds(3000));
        limits.response_bytes=1024*1024;
        return transport.Call(profile.connection,{++id,std::move(method),std::move(args),
            wallet ? profile.wallet : std::nullopt,effect},stop,limits);
    }
    Json Read(std::string method,std::string args,bool wallet,const char* reason)
    {
        auto reply=Call(std::move(method),std::move(args),wallet);
        Require(!stop.stop_requested() && reply.error==rpc::Error::None && reply.result.has_value(),reason);
        return std::move(*reply.result);
    }
    void CheckNode(bool require_unlocked)
    {
        const auto genesis=Read("getblockhash","[0]",false,"The authenticated genesis is unavailable.");
        const auto best=Read("getbestblockhash","[]",false,"The current chain tip is unavailable.");
        const auto current=Text(&best);
        Require(Text(&genesis)==profile.expected_genesis && Hash256::Parse(current) && (tip.empty() || tip==current),
            "The node or chain tip changed. Refresh the original transaction before trying again.");
        if (tip.empty()) tip=current;
        const auto wallet=Read("getwalletinfo","[]",true,"The connected wallet is unavailable.");
        Require(profile.wallet && Text(wallet.Find("walletname"))==*profile.wallet && Flag(wallet.Find("descriptors"))==true
            && Flag(wallet.Find("external_signer"))==false && Flag(wallet.Find("scanning"))==false,
            "The exact descriptor wallet changed or is scanning.");
        if (const auto* until=wallet.Find("unlocked_until");require_unlocked && until && Integer(until).value_or(0)<=0)
            throw std::runtime_error("The wallet is locked. Increase Fee does not unlock it.");
    }
    Json WalletTransaction(const Hash256& txid)
    {
        return Read("gettransaction","["+rpc::Quote(txid.hex())+"]",true,
            "The wallet cannot supply the exact original or previous transaction.");
    }
    Money CheckOriginal(const protocol::TransactionDocument& original)
    {
        const auto& tx=*original.transaction;
        const auto& txid=*tx.txid;
        const auto record=WalletTransaction(txid);
        const auto confirms=Integer(record.Find("confirmations"));
        Require(!record.Find("replaced_by_txid"),"This transaction has already been replaced.");
        Require(confirms && *confirms==0,"The original was confirmed or is no longer pending.");
        Require(Text(record.Find("txid"))==txid.hex()
            && Text(record.Find("hex"))==protocol::TransactionHex(original.wire),
            "The current wallet returned different original bytes.");
        const auto* conflicts=record.Find("mempoolconflicts");
        Require(conflicts && conflicts->kind==Json::Kind::Array && conflicts->items.empty(),
            "Wallet conflict state is unavailable or the original has a competing spend.");
        const auto entry=Read("getmempoolentry","["+rpc::Quote(txid.hex())+"]",false,
            "The exact original is no longer observed in this node's mempool.");
        const auto* fees=entry.Find("fees");
        const auto fee=Amount(fees ? fees->Find("base") : nullptr);
        Require(tx.wtxid && Text(entry.Find("wtxid"))==tx.wtxid->hex() && fee.has_value(),
            "The mempool witness identity or exact fee differs from the original.");
        Require(Integer(entry.Find("descendantcount"))==1,"No replacement draft is available while this payment has mempool descendants.");
        return *fee;
    }
    struct ScriptProof {
        protocol::OfflineChange branch;
        std::string concrete;
    };
    std::optional<ScriptProof> WalletScript(const SavedWalletPolicy& policy,
        const std::string& script,bool change)
    {
        const auto address=protocol::OutputAddress(script,network);
        Require(address.has_value(),"The original or replacement has an unsupported output script.");
        const auto info=Read("getaddressinfo","["+rpc::Quote(*address)+"]",true,
            "Independent wallet script classification is unavailable.");
        const auto mine=Flag(info.Find("ismine"));
        const auto internal=Flag(info.Find("ischange"));
        Require(Text(info.Find("scriptPubKey"))==script && mine && internal,
            "The wallet did not corroborate the exact script or its ownership classification.");
        if (change && !*internal) {
            Require(!std::ranges::any_of(policy.branches,[&](const auto& entry) {
                return entry.classified && entry.descriptor.internal && entry.descriptor.descriptor==Text(info.Find("parent_desc"));
            }),"Wallet change classification conflicts with the listed internal descriptor.");
            return {};
        }
        Require(*mine && Flag(info.Find("solvable"))==true,"No supported public wallet proof exists for this input or change.");
        const auto parent=Text(info.Find("parent_desc"));
        const auto branch=std::ranges::find_if(policy.branches,[&](const auto& entry) {
            return entry.descriptor.descriptor==parent && (!change || (entry.classified && entry.descriptor.internal));
        });
        Require(branch!=policy.branches.end(),"The script is outside supported listed public wallet policy.");
        uint32_t index{};
        if (branch->range) {
            auto path=Text(info.Find("hdkeypath"));
            const auto slash=path.rfind('/');
            Require(slash!=path.npos,"A bounded derivation index is unavailable for this wallet script.");
            path.remove_prefix(slash+1);
            const auto parsed=std::from_chars(path.data(),path.data()+path.size(),index);
            Require(parsed.ec==std::errc{} && parsed.ptr==path.data()+path.size()
                && index>=branch->range->first && index<=branch->range->last,"Wallet script derivation is out of range.");
        }
        const auto derived=protocol::DerivePublicDescriptor(branch->descriptor.descriptor,policy.chain,index);
        Require(derived && derived->script==script,"The script does not derive from the independently listed public wallet descriptor.");
        const auto concrete=protocol::DerivePublicDescriptor(Text(info.Find("desc")),policy.chain,0);
        Require(concrete && !concrete->ranged && concrete->script==script,
            "The exact concrete public spending descriptor could not be verified.");
        return ScriptProof{{branch->descriptor.descriptor,index,
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()},
            concrete->descriptor};
    }
};

struct PreviousOutput {
    Content source;
    protocol::OfflineReviewInput proof;
};
PreviousOutput Previous(Probe& probe,const SavedWalletPolicy& policy,const Outpoint& point,bool added)
{
    const auto record=probe.WalletTransaction(point.txid);
    const auto previous=protocol::ParseTransactionDocument(Text(record.Find("hex")),probe.network,{},true).document;
    Require(previous && !previous->psbt_version && previous->transaction && previous->transaction->txid==point.txid
        && point.index<previous->transaction->outputs.size(),"A previous transaction is missing or does not match the exact outpoint.");
    const auto& output=previous->transaction->outputs[point.index];
    if (added) {
        const auto utxo=probe.Read("gettxout","["+rpc::Quote(point.txid.hex())+","+std::to_string(point.index)+",true]",false,
            "An additional input is no longer verified unspent.");
        const auto* script=utxo.Find("scriptPubKey");
        Require(Text(utxo.Find("bestblock"))==probe.Tip() && Integer(utxo.Find("confirmations")).value_or(0)>0
            && Amount(utxo.Find("value"))==output.value
            && script && Text(script->Find("hex"))==output.script,"An additional input is not an independently matched confirmed UTXO.");
    }
    const auto derivation=probe.WalletScript(policy,output.script,false);
    Require(derivation.has_value(),"The previous output is outside the current public wallet policy.");
    return {previous,{point,output.value,derivation->concrete}};
}

ReplacementResult PrepareCancellation(Probe& probe,const ReplacementScope& scope,const WalletBinding& wallet,
    const Content& original,Money original_fee,int64_t total_in,const SavedWalletPolicy& policy,
    const std::map<std::string,PreviousOutput>& known,const std::optional<protocol::OfflineChange>& original_policy,
    Money original_change)
{
    auto destination=original_policy;
    if (!destination) {
        const auto& first=known.at(CoinKey(original->transaction->inputs.front().outpoint));
        const auto& output=first.source->transaction->outputs[first.proof.point.index];
        const auto previous=probe.WalletScript(policy,output.script,false);
        Require(previous.has_value(),"No existing owned destination could be independently proved.");
        destination=previous->branch;
    }
    const auto derived=protocol::DerivePublicDescriptor(destination->descriptor,policy.chain,destination->index);
    Require(derived && protocol::AddressScript(derived->address,probe.network)==derived->script,
        "The exact owned destination could not be independently derived.");

    CoinDraft coins{wallet};
    std::vector<protocol::OfflineInput> inputs;
    // Fixed v2 transaction header, one compact count of each kind and a
    // single output. Including witness framing here is conservative.
    int64_t maximum_vsize=12+9+derived->script.size()/2;
    for (const auto& spent:original->transaction->inputs) {
        const auto& previous=known.at(CoinKey(spent.outpoint));
        const auto& output=previous.source->transaction->outputs[spent.outpoint.index];
        protocol::OfflineCoinProof proof{previous.source->source,previous.proof.descriptor,{},{},destination->observed_ms};
        protocol::OfflineInput input{spent.outpoint,output.value,output.script,proof};
        Require(protocol::ValidOfflineCoin(input,probe.network),
            "This cancellation requires an independently proved input without a separate redeem or witness script.");
        maximum_vsize+=FundingInputVsizeBound(output.script,{});
        coins.inputs.push_back({spent.outpoint,output.value,output.script,{},{},std::move(proof)});
        inputs.push_back(std::move(input));
    }
    const auto fees=probe.Read("getnetworkinfo","[]",false,"Current incremental relay fee policy is unavailable.");
    const auto incremental=Amount(fees.Find("incrementalfee"));
    Require(incremental && incremental->sats()<=MAX_RATE,"Current incremental relay fee policy is outside supported bounds.");
    const int64_t sats=(scope.rate_per_kvb*maximum_vsize+999)/1000;
    const int64_t minimum=original_fee.sats()+(incremental->sats()*maximum_vsize+999)/1000;
    Require(sats>=minimum && sats>original_fee.sats(),"The requested fee rate does not cover the original and incremental relay fees.");
    Require(sats<total_in,"The requested fee leaves no positive self-payment. Choose a lower supported fee rate.");

    auto made=protocol::ConstructOfflineDraft({probe.network,std::move(inputs),
        {{derived->address,Money::Satoshis(total_in-sats)}},Money::Satoshis(sats),{}});
    Require(made.document && made.review_evidence && protocol::TransactionEditBlock(*made.document).empty()
        && protocol::InspectOfflineReview(*made.document,*made.review_evidence).commitment,
        "The local unsigned self-payment did not match the exact owned destination, inputs and fee.");
    probe.CheckNode(false);
    Require(probe.CheckOriginal(*original)==original_fee,"The exact original changed while preparing the self-payment.");
    probe.CheckNode(false);
    auto intent=CancellationIntent{original,original_fee,*destination};
    return {ReplacementDraft{scope,original,{},std::move(made.document),std::move(coins),std::move(*made.review_evidence),
        {},original_fee,original_change,Money::Satoshis(0),std::move(intent)},
        "Unsigned competing self-payment created. The original may still confirm first."};
}

bool ChildScript(std::string_view script)
{
    // These exact single-key forms have independent input proof and a bounded
    // child-size estimate without an additional redeem or witness script.
    return (script.size()==44 && script.starts_with("0014"))
        || (script.size()==68 && script.starts_with("5120"));
}

void CheckChildOutputLock(Probe& probe,const Outpoint& point)
{
    const auto result=probe.Read("listlockunspent","[]",true,"Current daemon locks on the selected output are Unknown.");
    Require(result.kind==Json::Kind::Array && result.items.size()<=HistoryLimits{}.coins,
        "Current daemon locks on the selected output are Unknown.");
    for (const auto& row:result.items) {
        const auto txid=Hash256::Parse(Text(row.Find("txid")));
        const auto index=Integer(row.Find("vout"));
        Require(txid && index && *index>=0 && uint64_t(*index)<=UINT32_MAX,
            "The daemon returned an unverified output lock.");
        Require(*txid!=point.txid || *index!=point.index,"The selected parent output is currently daemon-locked.");
    }
}

ReplacementResult PrepareChild(Probe& probe,const ReplacementScope& scope,const WalletBinding& wallet,
    const Content& parent,std::stop_token stop)
{
    Require(scope.parent_output.has_value(),"Select a specific wallet-owned unconfirmed outpoint.");
    const auto& selected=*scope.parent_output;
    const auto& outputs=parent->transaction->outputs;
    Require(selected.index<outputs.size() && outputs[selected.index]==protocol::TransactionOutput{selected.amount,selected.script}
        && ChildScript(selected.script),"The exact parent output or its spending policy is unsupported.");
    const auto descriptors=probe.Read("listdescriptors","{\"private\":false}",true,"The current public child-destination policy is unavailable.");
    const auto chain=protocol::DocumentChain(probe.network);
    const auto policy=ValidateWalletPolicy(descriptors,*scope.identity.profile.wallet,chain);
    Require(policy.has_value(),"The current public wallet policy could not be verified.");
    const auto owned=probe.WalletScript(*policy,selected.script,false);
    Require(owned.has_value(),"The selected parent output is outside proved wallet ownership.");
    const auto destination=protocol::OutputAddress(selected.script,probe.network);
    Require(destination && protocol::AddressScript(*destination,probe.network)==selected.script,
        "The existing wallet-owned child destination could not be independently verified.");
    const Outpoint point{scope.txid,selected.index};
    CheckChildOutputLock(probe,point);
    const auto read=probe.PackageReader();
    auto package=ReadChildPackage(read,*parent,selected.index,probe.network,probe.Tip(),stop);
    const auto fee=EstimateChildFee(package,scope.rate_per_kvb,selected.script,selected.amount);
    const auto payment=Money::Satoshis(selected.amount.sats()-fee.fee.sats());
    protocol::OfflineCoinProof proof{protocol::TransactionHex(parent->wire),owned->concrete,{},{},owned->branch.observed_ms};
    protocol::OfflineInput input{point,selected.amount,selected.script,proof};
    Require(protocol::ValidOfflineCoin(input,probe.network),"The exact owned parent output could not be proved independently.");
    auto made=protocol::ConstructOfflineDraft({probe.network,{std::move(input)},{{*destination,payment}},fee.fee,{}});
    Require(made.document && made.review_evidence && protocol::TransactionEditBlock(*made.document).empty()
        && protocol::InspectOfflineReview(*made.document,*made.review_evidence).commitment,
        "The unsigned child did not reconcile with the independently proved outpoint, payment and exact fee.");
    Require(ReadChildPackage(read,*parent,selected.index,probe.network,probe.Tip(),stop)==package,
        "The parent or package changed during preparation. No draft was adopted.");
    CheckChildOutputLock(probe,point);
    probe.CheckNode(false);
    CoinDraft coins{wallet,{{point,selected.amount,selected.script,{},{},std::move(proof)}}};
    const auto member=std::ranges::find(package.ancestors,scope.txid,&PackageTransaction::txid);
    Require(member!=package.ancestors.end(),"The exact parent fee is Unknown.");
    const auto original_fee=member->base_fee;
    auto child=ChildPackageIntent{parent,selected.index,owned->branch,std::move(package),scope.rate_per_kvb,fee.estimated_vsize};
    return {ReplacementDraft{scope,parent,{},std::move(made.document),std::move(coins),std::move(*made.review_evidence),
        {},original_fee,Money::Satoshis(0),Money::Satoshis(0),{},std::move(child)},
        "Unsigned child prepared with the verified package snapshot. Review and approve it separately."};
}

ReplacementResult Prepare(rpc::Transport& transport,const ReplacementScope& scope,const WalletBinding& wallet,
    const std::set<std::string>& allowed,std::stop_token stop)
{
    const bool cancellation=scope.kind==FeeDraftKind::CancelPayment;
    const bool child=scope.kind==FeeDraftKind::ChildPaysForParent;
    Probe probe(transport,scope.identity.profile,scope.identity.context.network,scope.tip,stop);
    probe.CheckNode(!cancellation && !child);
    // History retains raw previous transactions preferentially for unspent
    // coins. Fetch the selected original on demand if it has no cached wire.
    const auto original_source=scope.original_hex.empty()
        ? std::string(Text(probe.WalletTransaction(scope.txid).Find("hex"))) : scope.original_hex;
    const auto original=protocol::ParseTransactionDocument(original_source,probe.network,stop,true).document;
    Require(original && original->transaction && !original->psbt_version && original->transaction->txid==scope.txid
        && (!scope.observed_witness || original->transaction->wtxid==scope.observed_witness)
        && !original->transaction->coinbase && original->transaction->inputs.size()<=MAX_INPUTS
        && original->transaction->outputs.size()<=MAX_OUTPUTS,"The original witness identity or transaction shape is unsupported.");
    if (child) return PrepareChild(probe,scope,wallet,original,stop);
    Require(Signal(*original->transaction),"This payment does not explicitly signal replaceability.");
    const auto mempool_fee=probe.CheckOriginal(*original);
    const auto descriptors=probe.Read("listdescriptors","{\"private\":false}",true,"The scoped public wallet policy is unavailable.");
    const auto policy=ValidateWalletPolicy(descriptors,*scope.identity.profile.wallet,protocol::DocumentChain(probe.network));
    Require(policy.has_value(),"The scoped descriptor wallet policy could not be verified.");

    std::map<std::string,PreviousOutput> known;
    int64_t original_in{},original_out{};
    size_t original_proof_bytes{};
    for (const auto& input:original->transaction->inputs) {
        auto bound=Previous(probe,*policy,input.outpoint,false);
        Add(original_in,bound.proof.value);
        original_proof_bytes+=bound.source->source.size();
        Require(original_proof_bytes<=MAX_PROOF_BYTES,"The original previous-transaction proof budget was exceeded.");
        Require(known.emplace(CoinKey(input.outpoint),std::move(bound)).second,"The original repeats an input.");
    }

    std::optional<protocol::OfflineChange> original_policy;
    std::optional<size_t> original_change;
    std::vector<protocol::TransactionOutput> recipients;
    for (size_t index=0;index<original->transaction->outputs.size();++index) {
        const auto& output=original->transaction->outputs[index];
        Add(original_out,output.value);
        if (const auto change=probe.WalletScript(*policy,output.script,true)) {
            Require(!original_change,"More than one original change output is unsupported.");
            original_policy=change->branch;
            original_change=index;
        } else recipients.push_back(output);
    }
    Require(!recipients.empty() && original_in>=original_out && original_in-original_out==mempool_fee.sats(),
        "The original payment recipients or independent fee could not be reconciled.");
    const auto old_change=original_change ? original->transaction->outputs[*original_change].value : Money::Satoshis(0);
    if (cancellation) return PrepareCancellation(probe,scope,wallet,original,mempool_fee,original_in,*policy,known,original_policy,old_change);

    probe.CheckNode(true);
    Require(probe.CheckOriginal(*original)==mempool_fee,"The original mempool fee changed.");

    auto rate=std::to_string(scope.rate_per_kvb/1000)+"."+std::to_string(1000+scope.rate_per_kvb%1000).substr(1);
    auto options="{\"fee_rate\":"+rate+",\"psbt_version\":2";
    if (original_change) options+=",\"original_change_index\":"+std::to_string(*original_change);
    const auto reply=probe.Call("psbtbumpfee","["+rpc::Quote(scope.txid.hex())+","+options+"}]",true,rpc::Effect::Mutation);
    if (reply.error!=rpc::Error::Rpc && reply.mutation_uncertain)
        return {{},"The wallet reply was lost. It may have allocated public change; no draft was adopted and no request was retried."};
    Require(reply.error==rpc::Error::None && reply.result.has_value(),
        "The wallet rejected the fee draft. Funds, the requested rate or current replacement policy may be insufficient.");
    const auto& response=*reply.result;
    const auto proposed=protocol::ParseTransactionDocument(Text(response.Find("psbt")),probe.network,stop,true).document;
    const auto* errors=response.Find("errors");
    Require(proposed && proposed->psbt_version==2 && proposed->transaction && !proposed->modifiable.value_or(0)
        && proposed->transaction->inputs.size()>=known.size() && proposed->transaction->inputs.size()<=MAX_INPUTS
        && proposed->transaction->outputs.size()<=MAX_OUTPUTS && !response.Find("txid")
        && errors && errors->kind==Json::Kind::Array && errors->items.empty()
        && protocol::TransactionEditBlock(*proposed).empty()
        && Amount(response.Find("origfee"))==mempool_fee,"The wallet reply is not the expected unsigned replacement PSBT.");
    Require(Signal(*proposed->transaction),"The proposed replacement did not preserve explicit replaceability.");
    Require(proposed->transaction->version==original->transaction->version,
        "The wallet changed the original transaction version.");

    protocol::OfflineReviewEvidence review{probe.network,{},recipients,Money::Satoshis(0),{}};
    CoinDraft coins{wallet};
    std::vector<Content> previous;
    std::set<std::string> seen;
    size_t proof_bytes{};
    const auto unsigned_size=proposed->unsigned_wire.size();
    auto maximum_size=unsigned_size+2; // Extend the unsigned framing by conservative input satisfaction bounds.
    int64_t total_in{},total_out{};
    const auto observed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (size_t index=0;index<proposed->transaction->inputs.size();++index) {
        const auto& point=proposed->transaction->inputs[index].outpoint;
        const auto key=CoinKey(point);
        Require(seen.insert(key).second,"The replacement repeats an input.");
        const auto existing=known.find(key);
        Require(existing!=known.end() || allowed.contains(key),"The replacement adds an input outside current eligible coin policy.");
        auto bound=existing!=known.end() ? existing->second : Previous(probe,*policy,point,true);
        const auto& output=bound.source->transaction->outputs[point.index];
        Add(total_in,output.value);
        proof_bytes+=bound.source->source.size();
        Require(proof_bytes<=MAX_PROOF_BYTES,"The previous-transaction proof budget was exceeded.");
        const auto& scripts=proposed->input_policy[index].scripts;
        const auto script=[&](std::string_view field) {
            const auto found=scripts.find(std::string(field));
            return found==scripts.end() ? std::string{} : protocol::TransactionHex(found->second);
        };
        const auto redeem=script(std::string_view("\x04",1));
        const auto witness=script(std::string_view("\x05",1));
        maximum_size+=FundingInputVsizeBound(output.script,redeem)-41; // The unsigned input already contributes 41 bytes.
        protocol::OfflineCoinProof proof{bound.source->source,bound.proof.descriptor,redeem,witness,observed};
        Require(protocol::ValidOfflineCoin({point,output.value,output.script,proof},probe.network),
            "A returned spending script or previous-output policy conflicts with independent wallet evidence.");
        coins.inputs.push_back({point,output.value,output.script,{},{},std::move(proof)});
        review.inputs.push_back(std::move(bound.proof));
        previous.push_back(std::move(bound.source));
    }
    Require(std::ranges::all_of(known,[&](const auto& item){return seen.contains(item.first);}),
        "The replacement removed an original input.");

    const auto& outputs=proposed->transaction->outputs;
    Require(outputs.size()==recipients.size() || outputs.size()==recipients.size()+1,
        "The replacement removed a recipient or added unexpected outputs.");
    std::optional<size_t> change_output;
    Money new_change=Money::Satoshis(0);
    if (outputs.size()>recipients.size()) {
        if (original_change) {
            const auto& original_script=original->transaction->outputs[*original_change].script;
            Require(std::ranges::count(outputs,original_script,&protocol::TransactionOutput::script)==1,
                "The proposal substituted or duplicated the original change script.");
            change_output=size_t(std::ranges::find(outputs,original_script,&protocol::TransactionOutput::script)-outputs.begin());
            review.change=original_policy;
        } else {
            change_output=size_t(std::ranges::mismatch(recipients,outputs).in2-outputs.begin());
            if (const auto change=probe.WalletScript(*policy,outputs[*change_output].script,true)) review.change=change->branch;
        }
        new_change=outputs[*change_output].value;
        Require(review.change && new_change.sats()>0,"An additional output could not be proved as internal wallet change.");
    }
    size_t next_recipient{};
    for (size_t index=0;index<outputs.size();++index) {
        const auto& output=outputs[index];
        Add(total_out,output.value);
        if (change_output==index) continue;
        Require(output==recipients[next_recipient++],"A recipient amount, script or order changed.");
    }
    Require(total_in>=total_out,"The replacement outputs exceed the independently known inputs.");
    review.fee=Money::Satoshis(total_in-total_out);
    coins.offline_change=review.change;
    Require(review.fee.sats()>mempool_fee.sats() && Amount(response.Find("fee"))==review.fee,
        "The fee did not increase or the exact reported fee does not reconcile.");
    const auto fee_floor=(int64_t(scope.rate_per_kvb)*int64_t(unsigned_size))/1000;
    const auto fee_ceiling=(int64_t(scope.rate_per_kvb)*int64_t(maximum_size)+999)/1000+(change_output ? 0 : 1000);
    Require(review.fee.sats()>=fee_floor && review.fee.sats()<=fee_ceiling,
        "The exact proposed fee falls outside the supported bounds for this rate and input policy.");
    const auto working=protocol::WithMatchedPreviousOutputs(*proposed,probe.network,previous);
    Require(working && protocol::InspectOfflineReview(*working,review,change_output).commitment.has_value(),
        "The proposed unsigned transaction could not be reconciled with the independent payment and wallet proofs.");
    for (size_t index=0;index<working->input_policy.size();++index) {
        const auto& scripts=working->input_policy[index].scripts;
        const auto mode=protocol::ReviewSighash(*working,index);
        Require(mode && (*mode==1 || (*mode==0 && working->transaction->inputs[index].previous_script.starts_with("5120")))
            && std::ranges::none_of(scripts,[](const auto& item) {
                return !item.first.empty() && (uint8_t(item.first.front())==0x15 || uint8_t(item.first.front())==0x18);
            }),"The replacement included an unsupported sighash or Taproot script path.");
    }
    probe.CheckNode(true);
    Require(probe.CheckOriginal(*original)==mempool_fee,"Original eligibility changed during replacement preparation.");
    probe.CheckNode(true);
    return {ReplacementDraft{scope,original,proposed,working,std::move(coins),std::move(review),change_output,
        mempool_fee,old_change,new_change},"Unsigned replacement draft created. Review and approve this draft separately."};
}
} // namespace

static std::string CheckCancellationOriginal(rpc::Transport& transport,const ServerCandidate& profile,
    const CancellationIntent& intent,std::stop_token stop)
{
    try {
        const auto genesis=Hash256::Parse(profile.expected_genesis);
        Require(genesis && intent.original && !intent.original->psbt_version && intent.original->transaction
            && intent.original->transaction->txid && intent.original->transaction->wtxid
            && Signal(*intent.original->transaction),"The exact original required for cancellation is unavailable.");
        Probe probe(transport,profile,{*genesis},{},stop);
        probe.CheckNode(false);
        Require(probe.CheckOriginal(*intent.original)==intent.original_fee,"The original no longer has the expected fee.");
        const auto descriptors=probe.Read("listdescriptors","{\"private\":false}",true,"Current owned-destination policy is unavailable.");
        const auto chain=protocol::DocumentChain(probe.network);
        const auto policy=ValidateWalletPolicy(descriptors,*profile.wallet,chain);
        const auto& expected=intent.owned_destination;
        const auto script=protocol::DerivePublicDescriptor(expected.descriptor,chain,expected.index);
        Require(policy && script,"The exact public self-payment policy is unavailable.");
        const auto owned=probe.WalletScript(*policy,script->script,false);
        Require(owned && owned->branch.descriptor==expected.descriptor && owned->branch.index==expected.index,
            "The self-payment destination differs from current verified wallet policy.");
        probe.CheckNode(false);
        return {};
    } catch (const std::runtime_error& error) {return error.what();}
    catch (...) {return "The current original payment could not be verified.";}
}

std::string CheckFeeRelationship(rpc::Transport& transport,const ServerCandidate& profile,
    const std::optional<CancellationIntent>& cancellation,const std::optional<ChildPackageIntent>& child,std::stop_token stop)
{
    if (cancellation && child) return "Conflicting fee relationships have no authority.";
    if (cancellation) return CheckCancellationOriginal(transport,profile,*cancellation,stop);
    if (!child) return {};
    try {
        const auto genesis=Hash256::Parse(profile.expected_genesis);
        Require(genesis && child->parent && child->parent->transaction
            && child->parent->transaction->txid && child->output<child->parent->transaction->outputs.size(),
            "The exact child relationship is Unknown.");
        Probe probe(transport,profile,{*genesis},{},stop);
        probe.CheckNode(false);
        const auto descriptors=probe.Read("listdescriptors","{\"private\":false}",true,"Current child-destination policy is Unknown.");
        const auto chain=protocol::DocumentChain(probe.network);
        const auto policy=ValidateWalletPolicy(descriptors,*profile.wallet,chain);
        const auto& script=child->parent->transaction->outputs[child->output].script;
        const auto& expected=child->owned_destination;
        const auto derived=protocol::DerivePublicDescriptor(expected.descriptor,chain,expected.index);
        Require(policy && derived && derived->script==script,"The exact child destination is outside current public wallet proof.");
        const auto owned=probe.WalletScript(*policy,script,false);
        Require(owned && owned->branch.descriptor==expected.descriptor && owned->branch.index==expected.index,
            "The current wallet no longer proves this child destination.");
        const Outpoint point{*child->parent->transaction->txid,child->output};
        CheckChildOutputLock(probe,point);
        Require(ReadChildPackage(probe.PackageReader(),*child->parent,child->output,probe.network,probe.Tip(),stop)==child->package,
            "The ancestor set, fees, sizes or policy changed. Prepare and review a new package draft.");
        CheckChildOutputLock(probe,point);
        probe.CheckNode(false);
        return {};
    } catch (const std::runtime_error& error) {return error.what();}
    catch (...) {return "The current child package is Unknown.";}
}

bool ReplacementInputsAllowed(const ReplacementDraft& draft,const Document& wallet,const RefreshView& view,
    RecoverySession& recovery,CoinControls& controls)
{
    if (!view.ledger || !view.chain_current || view.offline || view.locked || !draft.original || !draft.original->transaction)
        return false;
    std::vector<PinnedCoin> original_inputs;
    for (const auto& input:draft.coins.inputs) {
        if (!draft.child_package && std::ranges::find(draft.original->transaction->inputs,input.outpoint,&protocol::TransactionInput::outpoint)
                !=draft.original->transaction->inputs.end()) {
            original_inputs.push_back(input);
            continue;
        }
        const auto coin=std::ranges::find(view.ledger->Coins(),input.outpoint,&HistoryCoin::outpoint);
        if (coin==view.ledger->Coins().end() || !coin->solvable || coin->amount!=input.value || coin->script!=input.script
            || (draft.child_package ? coin->confirmations!=0 : (!coin->safe || coin->confirmations<=0))) return false;
    }
    const auto policy=controls.Policy(wallet,view,recovery,0,original_inputs);
    return policy && std::ranges::all_of(draft.coins.inputs,[&](const auto& input) {
        const auto rule=policy->rules.find(CoinKey(input.outpoint));
        return rule!=policy->rules.end() && rule->second==CoinRule::Allowed;
    });
}

std::optional<ReplacementScope> Replacements::Scope(const DocumentRegistry& docs,const RefreshService& refresh,
    const RecoverySession& recovery,const FeeDraftRequest& action,uint64_t policy_revision) const
{
    if (std::this_thread::get_id()!=owner || !action.rate_per_kvb
        || action.rate_per_kvb>MAX_RATE || recovery.Locked() || recovery.Pending() || docs.Documents().size()>=32
        || connection.PrivacyLocked() || connection.Offline() || !connection.Active()
        || !SupportsReplacement(*connection.Active(),action.kind)) return {};
    const auto* doc=docs.Active();
    const auto& active=*connection.Active();
    if (!doc || doc->section!=Section::Transactions || doc->history.selected!=action.txid.hex() || !doc->binding
        || VerifiedWalletBinding(active)!=doc->binding || doc->observation==ObservationState::Locked) return {};
    const auto expected=WalletRefreshIdentity(active,connection.Session(),*doc,recovery.Gate().Epoch());
    const auto view=refresh.View();
    if (!expected || !view.identity || *view.identity!=*expected || action.context!=expected->context
        || view.generation!=action.generation || view.history_sequence!=action.sequence || !view.chain_current
        || view.offline || view.locked || !view.node || !view.ledger || view.node->tip!=view.ledger->Tip()
        || !Hash256::Parse(view.node->tip) || !view.ledger->InventoryAvailable() || !view.ledger->LocksAvailable()) return {};
    const auto row=view.ledger->Rows().find(action.txid.hex());
    if (row==view.ledger->Rows().end() || !Eligibility(row->second,action.kind).empty()) return {};
    std::optional<SelectedParentOutput> output;
    if (action.kind==FeeDraftKind::ChildPaysForParent) {
        if (!action.parent_output) return {};
        const auto coin=std::ranges::find(view.ledger->Coins(),Outpoint{action.txid,*action.parent_output},&HistoryCoin::outpoint);
        if (coin==view.ledger->Coins().end() || coin->confirmations!=0 || !coin->solvable || !ChildScript(coin->script)) return {};
        output=SelectedParentOutput{*action.parent_output,coin->amount,coin->script};
    } else if (action.parent_output) return {};
    return ReplacementScope{*view.identity,doc->id,connection.Revision(),action.generation,action.sequence,
        doc->history.query_revision,policy_revision,doc->draft_revision,action.txid,row->second.wire_witness,view.node->tip,
        row->second.previous_hex,action.rate_per_kvb,action.kind,std::move(output)};
}

std::string Replacements::Unavailable(const DocumentRegistry& docs,const RefreshService& refresh,
    const RecoverySession& recovery,const FeeDraftRequest& action) const
{
    if (Busy()) return "Wait for the current fee request to finish or cancel the wait.";
    if (action.kind==FeeDraftKind::ChildPaysForParent && !action.parent_output)
        return "Select a specific current wallet-owned parent output.";
    if (action.kind!=FeeDraftKind::ReplaceByFee && action.kind!=FeeDraftKind::CancelPayment && action.kind!=FeeDraftKind::ChildPaysForParent)
        return "This fee action is unavailable.";
    if (!connection.Active() || !SupportsReplacement(*connection.Active(),action.kind)) {
        if (action.kind==FeeDraftKind::ChildPaysForParent)
            return "Package ancestry, exact node bytes, public wallet policy or adjusted cluster limits are Unknown.";
        return action.kind==FeeDraftKind::CancelPayment
            ? "Attempt cancellation needs a compatible descriptor wallet, public policy and current original-payment observations."
            : "Increase Fee requires a compatible descriptor wallet and verified psbtbumpfee arguments.";
    }
    if (!action.rate_per_kvb || action.rate_per_kvb>MAX_RATE) return "Enter a fee rate from 0.001 to 100 sat/vB.";
    if (!recovery.BroadcastJournalAvailable()) return "The local transaction operation lock is unavailable.";
    if (const auto* doc=docs.Active();doc && doc->history.selected==action.txid.hex()) {
        const auto view=HistoryForDocument(*doc,refresh,recovery.Gate().Epoch());
        if (view && view->chain_current && view->ledger) {
            const auto row=view->ledger->Rows().find(action.txid.hex());
            if (row!=view->ledger->Rows().end()) {
                if (const auto reason=Eligibility(row->second,action.kind);!reason.empty()) return std::string(reason);
            }
        }
    }
    if (!Scope(docs,refresh,recovery,action,0))
        return action.kind==FeeDraftKind::ChildPaysForParent ? "The selected unconfirmed output or its current wallet policy is unavailable."
            : "Select an outgoing transaction in current verified history with current coin and lock observations.";
    return {};
}

Receipt Replacements::Start(const DocumentRegistry& docs,const RefreshService& refresh,RecoverySession& recovery,
    CoinControls& controls,const FeeDraftRequest& action)
{
    if (auto reason=Unavailable(docs,refresh,recovery,action);!reason.empty()) return {false,std::move(reason)};
    const auto& doc=*docs.Active();
    const auto view=refresh.View();
    const auto coins=controls.Policy(doc,view,recovery);
    if (!coins) return {false,"Local coin eligibility is Unknown. Refresh the wallet and load local coin policy."};
    const auto scope=Scope(docs,refresh,recovery,action,controls.Revision());
    if (!scope) return {false,"The original transaction or wallet observation changed."};
    if (scope->parent_output) {
        const auto rule=coins->rules.find(CoinKey({scope->txid,scope->parent_output->index}));
        if (rule==coins->rules.end() || rule->second!=CoinRule::Allowed)
            return {false,"The selected parent output is frozen, reserved, daemon-locked or Unknown."};
    }
    std::set<std::string> allowed;
    for (const auto& coin:view.ledger->Coins()) {
        auto key=CoinKey(coin.outpoint);
        const auto rule=coins->rules.find(key);
        if (coin.confirmations>0 && coin.safe && coin.solvable && rule!=coins->rules.end() && rule->second==CoinRule::Allowed)
            allowed.insert(std::move(key));
    }
    auto lease=recovery.BroadcastLease();
    if (!lease) return {false,"Another transaction mutation is active or the local operation lock is unavailable."};
    try {
        running=*scope;
        local_job=scope->kind;
        worker=std::jthread([this,scope=*scope,wallet=*doc.binding,allowed=std::move(allowed),lease=std::move(*lease.value)](std::stop_token stop) {
            ReplacementResult result;
            try {
                result=Prepare(transport,scope,wallet,allowed,stop);
            } catch (const std::runtime_error& error) {
                result.reason=scope.kind==FeeDraftKind::ChildPaysForParent
                    ? std::string("Child package unavailable. ")+error.what() : error.what();
            } catch (...) {
                result.reason="The fee draft exceeded local processing resources. No replacement was adopted.";
            }
            std::lock_guard lock(mutex);
            completed=std::move(result);
        });
    } catch (...) {
        running.reset();
        return {false,"The fee draft worker could not start."};
    }
    if (scope->kind==FeeDraftKind::ChildPaysForParent)
        return {true,"Verifying the selected output and its complete unconfirmed package. Preparation makes no wallet changes."};
    return {true,local_job==FeeDraftKind::CancelPayment ? "Checking the original and preparing an unsigned self-payment to an existing wallet address."
        : "Checking eligibility and preparing an unsigned replacement. The wallet may allocate public change."};
}

bool Replacements::Cancel()
{
    if (std::this_thread::get_id()!=owner || !running) return false;
    running.reset();
    worker.request_stop();
    return true;
}

std::optional<ReplacementResult> Replacements::Poll(const DocumentRegistry& docs,const RefreshService& refresh,
    RecoverySession& recovery,CoinControls& controls,bool visible)
{
    if (std::this_thread::get_id()!=owner || !Busy() || approval) return {};
    if (running) {
        const auto& target=*running;
        const auto now=Scope(docs,refresh,recovery,
            {target.identity.context,target.txid,target.kind,target.generation,target.sequence,target.rate_per_kvb,
                target.parent_output ? std::optional(target.parent_output->index) : std::nullopt},
            controls.Revision());
        if (!visible || now!=running) Cancel();
    }
    auto result=TakeCompletion();
    if (!result) return {};
    if (!running) return ReplacementResult{{},local_job==FeeDraftKind::ChildPaysForParent
        ? "The child draft request was retired. The parent is retained."
        : local_job==FeeDraftKind::CancelPayment ? "The cancellation draft request was retired. The original is retained."
        : "The fee request was retired. Public change may have been allocated if the wallet received it. No draft was adopted or request retried."};
    running.reset();
    if (result->draft && !ReplacementInputsAllowed(*result->draft,*docs.Active(),refresh.View(),recovery,controls))
        return ReplacementResult{{},"A replacement input is frozen, reserved, daemon-locked or no longer independently eligible."};
    return result;
}

std::optional<ReplacementResult> Replacements::TakeCompletion()
{
    std::optional<ReplacementResult> result;
    {
        std::lock_guard lock(mutex);
        if (!completed) return {};
        result=std::move(completed);
        completed.reset();
    }
    worker.join();
    return result;
}

bool Replacements::ApprovalCurrent(const SigningBasis& basis) const
{
    return (basis.cancellation || basis.child_package) && !basis.offline && basis.coin_policy && !connection.PrivacyLocked()
        && !connection.Offline() && connection.Active() && connection.Session()==basis.session
        && connection.Revision()==basis.profile && VerifiedWalletBinding(*connection.Active())==basis.wallet;
}

Receipt Replacements::StartApproval(const SigningReviews& reviews,const SigningBasis& basis)
{
    if (std::this_thread::get_id()!=owner || Busy() || !ApprovalCurrent(basis))
        return {false,"A current online wallet and original-payment check are required to approve this self-payment."};
    const auto* review=reviews.Recorded(basis);
    if (!review || reviews.Authorize(basis)) return {false,"Review this exact self-payment before requesting approval."};
    const auto profile=connection.Active()->candidate;
    try {
        approval=ApprovalCheck{basis,review->generation};
        worker=std::jthread([this,profile,cancellation=basis.cancellation,child=basis.child_package](std::stop_token stop) {
            auto reason=CheckFeeRelationship(transport,profile,cancellation,child,stop);
            std::lock_guard lock(mutex);
            completed=ReplacementResult{{},std::move(reason)};
        });
    } catch (...) {
        approval.reset();
        return {false,"The original-payment eligibility check could not start."};
    }
    return {true,basis.child_package ? "Checking the current parent and full package before recording your approval."
        : "Checking that the exact original can still be replaced before recording your approval."};
}

std::optional<Receipt> Replacements::PollApproval(SigningReviews& reviews,const SigningBasis* current)
{
    if (std::this_thread::get_id()!=owner || !approval || !Busy()) return {};
    const auto* record=current ? reviews.Recorded(*current) : nullptr;
    if (!current || *current!=approval->basis || !record || record->generation!=approval->generation
        || !ApprovalCurrent(*current)) {
        approval->retired=true;
        worker.request_stop();
    }
    auto result=TakeCompletion();
    if (!result) return {};
    const bool live=!approval->retired;
    const bool child=approval->basis.child_package.has_value();
    approval.reset();
    if (!live) return Receipt{false,"Approval was retired because the original or its view changed. The draft is retained."};
    if (!result->reason.empty()) return Receipt{false,std::move(result->reason)};
    if (!reviews.ApproveCurrent(*current)) return Receipt{false,"Approval could not be recorded for this exact self-payment."};
    return Receipt{true,child ? "Approval recorded for the exact child. Signing is a separate action."
        : "Approval recorded for the competing self-payment. Signing is a separate action."};
}

Replacements::~Replacements()
{
    if (Busy()) {
        worker.request_stop();
        worker.join();
    }
}
} // namespace roots::gui::app
