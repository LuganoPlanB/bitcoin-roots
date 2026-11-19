// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/extensions.h"

#include <algorithm>
#include <set>

namespace roots::gui {
namespace {
namespace p = protocol;
constexpr int64_t MAX_AGE_MS = 120'000;
constexpr int64_t MAX_SATS = 21'000'000LL * 100'000'000;
constexpr size_t MAX_INDEX_PROOFS = 256;
constexpr size_t MAX_INDEX_BYTES = 2 * 1024 * 1024;

bool Known(NetworkIdentity network)
{
    return p::DocumentNetwork(network)!=p::DisplayNetwork::Unknown;
}

bool Identifier(std::string_view id)
{
    return !id.empty() && id.size()<=64 && std::ranges::all_of(id,[](char c) {
        return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='-';
    });
}

bool Fresh(const ExtensionObservation& observation,std::string_view provider,NetworkIdentity network,Hash256 tip,int64_t now)
{
    return observation.provider==provider && observation.network==network && observation.tip==tip
        && observation.observed_ms>0 && now>=observation.observed_ms && now-observation.observed_ms<=MAX_AGE_MS;
}

bool Script(std::string_view script)
{
    return !script.empty() && script.size()<=20'000 && script.size()%2==0
        && std::ranges::all_of(script,[](char c) {return (c>='0' && c<='9') || (c>='a' && c<='f');});
}

bool IndexAdmissible(const ExtensionRegistry& registry,const HistoryIndexQuery& query)
{
    if (!query.permission || !query.session || !query.profile || !query.revision
        || !HistoryIndexStatus(registry,query.access,query.owner.network).available) return false;
    const auto* script=std::get_if<std::string>(&query.subject);
    return !script || Script(*script);
}

bool MatchIndexedOutput(const IndexedOutput& proof,const HistoryIndexQuery& query,size_t& bytes,std::stop_token stop)
{
    const auto network=p::DocumentNetwork(query.owner.network);
    const auto parse=[&](std::string_view source)->std::shared_ptr<const p::TransactionDocument> {
        if (source.empty() || source.size()>MAX_INDEX_BYTES-bytes) return {};
        bytes+=source.size();
        auto document=p::ParseTransactionDocument(source,network,stop).document;
        return document && !document->psbt_version && document->transaction ? document : nullptr;
    };
    const auto creation=parse(proof.creation_wire);
    if (!creation || creation->transaction->txid!=proof.point.txid || proof.point.index>=creation->transaction->outputs.size())
        return false;
    if (const auto* point=std::get_if<Outpoint>(&query.subject)) {
        if (*point!=proof.point) return false;
    } else if (std::get<std::string>(query.subject)!=creation->transaction->outputs[proof.point.index].script) return false;
    if (!proof.spending_wire) return true;
    const auto spend=parse(*proof.spending_wire);
    return spend && std::ranges::any_of(spend->transaction->inputs,[&](const auto& input) {
        return input.outpoint==proof.point;
    });
}

bool ValidIntent(const ConstructionIntent& intent,int64_t now,std::stop_token stop)
{
    const auto& scope=intent.scope;
    if (!scope.document || !scope.wallet.connection_session || !scope.profile_revision || !scope.generation
        || !scope.history_sequence || !scope.policy_revision || !scope.fee_rate || !Hash256::Parse(scope.tip)
        || scope.wallet.profile.expected_genesis!=scope.wallet.context.network.genesis.hex()
        || intent.coins.empty() || intent.coins.size()>MAX_FUNDING_INPUTS
        || intent.recipients.empty() || intent.recipients.size()>MAX_FUNDING_OUTPUTS
        || intent.maximum_fee.sats()<0 || intent.maximum_fee.sats()>MAX_SATS) return false;
    int64_t total{};
    for (const auto& output:intent.recipients) {
        if (output.value.sats()<=0 || output.value.sats()>MAX_SATS-total || !Script(output.script)) return false;
        total+=output.value.sats();
    }
    const auto network=p::DocumentNetwork(scope.wallet.context.network);
    std::set<std::pair<std::string,uint32_t>> points;
    for (const auto& coin:intent.coins) {
        if (stop.stop_requested() || coin.confirmations<=0 || coin.proof.observed_ms<=0
            || coin.proof.observed_ms>now || now-coin.proof.observed_ms>MAX_AGE_MS
            || !points.emplace(coin.point.txid.hex(),coin.point.index).second
            || !p::ValidOfflineCoin({coin.point,coin.amount,coin.script,coin.proof},network)) return false;
    }
    if (!intent.change) return true;
    return intent.change->observed_ms>0 && intent.change->observed_ms<=now && now-intent.change->observed_ms<=MAX_AGE_MS
        && p::DerivePublicDescriptor(intent.change->descriptor,p::DocumentChain(network),intent.change->index).has_value();
}

std::optional<p::OfflineReviewEvidence> ValidateConstruction(const ConstructionIntent& intent,const p::TransactionDocument& document)
{
    const auto network=p::DocumentNetwork(intent.scope.wallet.context.network);
    if (!document.psbt_version || !document.transaction || document.modifiable.value_or(0)) return {};
    const auto& tx=*document.transaction;
    if (tx.inputs.empty() || tx.inputs.size()>MAX_FUNDING_INPUTS || !tx.fee
        || tx.fee->sats()<0 || *tx.fee>intent.maximum_fee
        || tx.outputs.size()!=intent.recipients.size()+size_t(intent.change.has_value())
        || (intent.selection==FundingSelection::Exact && tx.inputs.size()!=intent.coins.size())
        || std::ranges::any_of(document.input_policy,[](const auto& input) {return input.signature_fields || input.final_fields;})) return {};
    p::OfflineReviewEvidence evidence{network,{},intent.recipients,*tx.fee,intent.change};
    std::set<std::string_view> scripts;
    for (const auto& input:tx.inputs) {
        const auto found=std::ranges::find(intent.coins,input.outpoint,&FundingCoin::point);
        if (found==intent.coins.end()
            || std::ranges::find(evidence.inputs,input.outpoint,&p::OfflineReviewInput::point)!=evidence.inputs.end()) return {};
        scripts.insert(found->script);
        evidence.inputs.push_back({found->point,found->amount,found->proof.descriptor});
    }
    if (intent.strategy==ConstructionStrategy::AvoidPartialSpends && std::ranges::any_of(intent.coins,[&](const auto& coin) {
        return scripts.contains(coin.script) && std::ranges::find(evidence.inputs,coin.point,&p::OfflineReviewInput::point)==evidence.inputs.end();
    })) return {};
    const auto change=intent.change ? std::optional(tx.outputs.size()-1) : std::nullopt;
    return p::InspectOfflineReview(document,evidence,change).commitment ? std::optional(std::move(evidence)) : std::nullopt;
}
} // namespace

const ExtensionRegistry& ShippedExtensions()
{
    static const ExtensionRegistry rpc_profile;
    return rpc_profile;
}

ExtensionStatus HistoryIndexStatus(const ExtensionRegistry& registry,IndexAccess access,std::optional<NetworkIdentity> network)
{
    if (registry.history && network && Known(*network) && Identifier(registry.history->Identity())
        && registry.history->Supports(access,*network))
        return {true,"History index client available for this network. Queries require explicit permission."};
    return {false,access==IndexAccess::Private
        ? "Private indexing is off. A HistoryIndexAdapter with endpoint and TLS support is required."
        : "Public indexing is off. A HistoryIndexAdapter and explicit service privacy permission are required."};
}

ExtensionStatus ConstructionStatus(const ExtensionRegistry& registry,ConstructionStrategy strategy,std::optional<NetworkIdentity> network)
{
    if (registry.construction && network && Known(*network) && Identifier(registry.construction->Identity())
        && registry.construction->Supports(strategy,*network))
        return {true,"Unsigned construction client available. Changes require a fresh review."};
    return {false,"Privacy selection requires a TransactionConstructionAdapter for Avoid partial spends."};
}

std::optional<HistoryIndexReply> ReadIndexedHistory(const ExtensionRegistry& registry,const HistoryIndexQuery& query,
    const HistoryIndexQuery& current,int64_t now,std::stop_token stop)
{
    if (stop.stop_requested() || query!=current || !IndexAdmissible(registry,query)) return {};
    std::optional<HistoryIndexReply> result;
    try {result=registry.history->Read(query,stop);}
    catch (...) {return {};}
    return result ? ValidateIndexedHistory(registry,current,std::move(*result),now,stop) : std::nullopt;
}

std::optional<HistoryIndexReply> ValidateIndexedHistory(const ExtensionRegistry& registry,const HistoryIndexQuery& current,
    HistoryIndexReply result,int64_t now,std::stop_token stop)
{
    if (stop.stop_requested() || result.query!=current || !IndexAdmissible(registry,current)
        || !Fresh(result.observation,registry.history->Identity(),current.owner.network,current.tip,now)
        || result.outputs.size()>MAX_INDEX_PROOFS) return {};
    size_t bytes{};
    std::set<std::pair<std::string,uint32_t>> observed;
    for (const auto& output:result.outputs) {
        if (stop.stop_requested() || !observed.emplace(output.point.txid.hex(),output.point.index).second
            || !MatchIndexedOutput(output,current,bytes,stop)) return {};
    }
    if (stop.stop_requested()) return {};
    if (result.first_height || result.has_more) result.coverage=IndexCoverage::Partial;
    return result;
}

std::optional<PreparedConstruction> PrepareUnsigned(const ExtensionRegistry& registry,const ConstructionIntent& intent,
    const FundingScope& current,int64_t now,std::stop_token stop)
{
    const auto network=current.wallet.context.network;
    if (stop.stop_requested() || current!=intent.scope || !ConstructionStatus(registry,intent.strategy,network).available
        || !ValidIntent(intent,now,stop)) return {};
    std::optional<ConstructionReply> reply;
    try {reply=registry.construction->Prepare(intent,stop);}
    catch (...) {return {};}
    if (!reply || stop.stop_requested() || reply->scope!=current || reply->strategy!=intent.strategy
        || !Fresh(reply->observation,registry.construction->Identity(),network,*Hash256::Parse(current.tip),now)) return {};
    auto document=p::ParseTransactionDocument(reply->psbt,p::DocumentNetwork(network),stop).document;
    if (!document || document->source!=reply->psbt || stop.stop_requested()) return {};
    auto proof=ValidateConstruction(intent,*document);
    if (!proof || stop.stop_requested()) return {};
    return PreparedConstruction(std::move(*reply),std::move(document),std::move(*proof));
}

bool PreparedConstruction::Current(const FundingScope& current,int64_t now) const
{
    if (!document || current!=reply.scope) return false;
    const auto tip=Hash256::Parse(current.tip);
    return tip && Fresh(reply.observation,reply.observation.provider,current.wallet.context.network,*tip,now);
}
} // namespace roots::gui
