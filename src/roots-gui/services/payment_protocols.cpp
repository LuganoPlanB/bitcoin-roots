// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/payment_protocols.h"

#include <algorithm>
#include <sodium.h>

namespace roots::gui {
namespace {
namespace p=protocol;
constexpr int64_t MAX_AGE_MS=120'000;
constexpr int64_t MAX_SATS=21'000'000LL*100'000'000;

bool Identifier(std::string_view id)
{
    return !id.empty() && id.size()<=64 && std::ranges::all_of(id,[](char c){
        return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='-';
    });
}

// Admit only a small canonical subset. Provider qualification must also pin
// peer identity, DNS resolution, connection policy and transport provenance.
// Parsing this value does not resolve it, start a browser or open a socket.
bool Endpoint(std::string_view url)
{
    if (!url.starts_with("https://") || url.size()>512) return false;
    url.remove_prefix(8);
    const auto slash=url.find('/');
    const auto host=url.substr(0,slash);
    if (slash==url.npos || host.find('.')==host.npos || host.size()>253) return false;
    for (size_t at=0;at<=host.size();) {
        const auto dot=host.find('.',at);
        const auto label=host.substr(at,dot==host.npos ? dot : dot-at);
        if (!Identifier(label) || label.size()>63 || label.front()=='-' || label.back()=='-') return false;
        if (dot==host.npos) break;
        at=dot+1;
    }
    const auto path=url.substr(slash);
    return path.find("//")==path.npos && path.find("..") == path.npos && std::ranges::all_of(path,[](char c){
        return c=='/' || c=='_' || c=='.' || c=='-' || (c>='a' && c<='z') || (c>='0' && c<='9');
    });
}

bool ValidScope(const CustodyScope& scope)
{
    return scope.session && scope.profile && scope.generation && p::DocumentNetwork(scope.owner.network)!=p::DisplayNetwork::Unknown;
}

bool Fresh(int64_t observation,int64_t now)
{
    return observation>0 && now>=observation && now-observation<=MAX_AGE_MS;
}

bool Permitted(const NegotiationPermission& permission,const NegotiationOwner& current,int64_t now)
{
    return permission.confirmed && permission.owner==current && current.document && current.ticket && ValidScope(current.scope)
        && now>0 && permission.valid_until_ms>=now && permission.valid_until_ms-now<=MAX_AGE_MS
        && current.disclosure==PayjoinDisclosure::UnsignedPaymentAndPreviousTransactions;
}

bool Unsigned(const p::TransactionDocument& document)
{
    if (document.psbt_version!=0 || !p::TransactionEditBlock(document).empty()) return false;
    const auto& tx=*document.transaction;
    if (tx.inputs.size()>p::MAX_OFFLINE_INPUTS || tx.outputs.size()>MAX_FUNDING_OUTPUTS) return false;
    for (size_t i=0;i<tx.inputs.size();++i) {
        const auto& input=tx.inputs[i];
        if (p::ReviewSighash(document,i)!=1 || input.previous_script.size()!=44 || !input.previous_script.starts_with("0014")
            || input.evidence!=p::PrevoutEvidence::TxidMatched || !input.previous_value || input.previous_value->sats()<=0) return false;
    }
    return true;
}

std::shared_ptr<const p::TransactionDocument> ReadOriginal(const ExtensionRegistry& registry,const NegotiationIntent& intent,
    const NegotiationPermission& permission,const NegotiationOwner& current,int64_t now,std::stop_token stop)
{
    const auto& network=current.scope.owner.network;
    const auto& proof=intent.owner.original_proof;
    if (stop.stop_requested() || intent.owner!=current || !Permitted(permission,current,now)
        || !PayjoinStatus(registry,&current.target,network).available
        || PaymentSourceIdentity(intent.original_source)!=current.source
        || proof.network!=p::DocumentNetwork(network) || intent.owner.maximum_fee.sats()>MAX_SATS
        || (proof.change && !Fresh(proof.change->observed_ms,now))) return {};
    const auto document=p::ParseTransactionDocument(intent.original_source,proof.network,stop).document;
    if (!document || !Unsigned(*document) || proof.fee.sats()<0 || proof.fee>intent.owner.maximum_fee
        || document->transaction->outputs.size()<=intent.owner.receiver_output
        || document->transaction->outputs[intent.owner.receiver_output].value.sats()<=0
        || document->transaction->outputs[intent.owner.receiver_output].script!=current.target.recipient_script) return {};
    const auto review=p::InspectOfflineReview(*document,proof,intent.owner.change_output);
    if (!review.commitment || review.change_output!=intent.owner.change_output || review.change_output==intent.owner.receiver_output) return {};
    return document;
}

bool SameSenderInput(const p::TransactionInput& original,const p::TransactionInput& reply)
{
    return original.outpoint==reply.outpoint && original.sequence==reply.sequence && original.previous_value==reply.previous_value
        && original.previous_script==reply.previous_script;
}

std::optional<p::OfflineReviewEvidence> CheckProposal(const NegotiationIntent& intent,const p::TransactionDocument& original,
    const p::TransactionDocument& proposal,const CounterpartyEvidence& evidence,int64_t now,std::stop_token stop)
{
    if (!Unsigned(proposal)) return {};
    const auto& before=*original.transaction;
    const auto& next=*proposal.transaction;
    const auto& extra=evidence.input;
    const auto minimum_sequence=std::ranges::min_element(before.inputs,{},&p::TransactionInput::sequence)->sequence;
    const uint32_t join_sequence=minimum_sequence<0xfffffffeU ? 0xfffffffdU : minimum_sequence;
    if (before.version!=next.version || before.locktime!=next.locktime || before.inputs.size()+1!=next.inputs.size()
        || before.outputs.size()!=next.outputs.size() || !next.fee || *next.fee<*before.fee || *next.fee>intent.owner.maximum_fee
        || evidence.scope!=intent.owner.scope || evidence.tip!=intent.owner.tip || !extra.proof
        || !Fresh(extra.proof->observed_ms,now) || !p::ValidOfflineCoin(extra,intent.owner.original_proof.network)
        || next.inputs.back().sequence!=join_sequence || next.inputs.back().outpoint!=extra.point
        || next.inputs.back().previous_value!=extra.value
        || next.inputs.back().previous_script!=extra.script
        || std::ranges::any_of(before.inputs,[&](const auto& input){return input.previous_script==extra.script;})
        || (intent.owner.change_output && before.outputs[*intent.owner.change_output].script==extra.script)
        || !std::equal(before.inputs.begin(),before.inputs.end(),next.inputs.begin(),SameSenderInput)) return {};
    for (size_t i=0;i<next.outputs.size();++i) {
        if (stop.stop_requested()) return {};
        if (i==intent.owner.receiver_output) {
            if (next.outputs[i].script!=before.outputs[i].script || next.outputs[i].value<before.outputs[i].value
                || next.outputs[i].value.sats()-before.outputs[i].value.sats()>extra.value->sats()) return {};
        } else if (next.outputs[i]!=before.outputs[i]) return {};
    }
    auto proof=intent.owner.original_proof;
    proof.fee=*next.fee;
    proof.inputs.push_back({extra.point,*extra.value,extra.proof->descriptor});
    const auto recipient=intent.owner.receiver_output-size_t(intent.owner.change_output && *intent.owner.change_output<intent.owner.receiver_output);
    proof.recipients[recipient]=next.outputs[intent.owner.receiver_output];
    return proof;
}
} // namespace

ExtensionStatus PayjoinStatus(const ExtensionRegistry& registry,const PayjoinTarget* target,std::optional<NetworkIdentity> network)
{
    const auto* port=registry.payjoin;
    if (!port || !target || !network || p::DocumentNetwork(*network)==p::DisplayNetwork::Unknown
        || !Identifier(target->provider) || target->provider!=port->Identity() || !Identifier(target->version) || !Endpoint(target->endpoint)
        || !p::OutputAddress(target->recipient_script,p::DocumentNetwork(*network)) || !port->Qualified(*target,*network))
        return {false,"Payjoin requires a qualified PayjoinModule for the recipient, endpoint, network and protocol version."};
    return {true,"Payjoin is eligible for this request and requires separate disclosure permission."};
}

std::optional<Hash256> PaymentSourceIdentity(std::string_view bytes)
{
    if (bytes.empty() || bytes.size()>p::MAX_TRANSACTION_SOURCE) return {};
    std::array<unsigned char,crypto_hash_sha256_BYTES> hash{};
    if (crypto_hash_sha256(hash.data(),reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size())!=0) return {};
    return Hash256::Parse(p::TransactionHex({reinterpret_cast<const char*>(hash.data()),hash.size()}));
}

std::optional<PayjoinOffer> PreparePayjoinOffer(const ExtensionRegistry& registry,const NegotiationIntent& intent,
    const NegotiationPermission& permission,const NegotiationOwner& current,int64_t now,std::stop_token stop)
{
    const auto original=ReadOriginal(registry,intent,permission,current,now,stop);
    if (!original) return {};
    auto disclosure=p::PayjoinDisclosureCopy(*original);
    if (!disclosure || stop.stop_requested()) return {};
    return PayjoinOffer{current.target,std::move(*disclosure)};
}

NegotiatedDraft::NegotiatedDraft(NegotiationOwner owner,std::shared_ptr<const p::TransactionDocument> original,
    std::shared_ptr<const p::TransactionDocument> proposal,std::shared_ptr<const p::TransactionDocument> working,
    p::OfflineReviewEvidence proof,int64_t observed)
    : owner(std::move(owner)),original(std::move(original)),proposal(std::move(proposal)),working(std::move(working)),
      proof(std::move(proof)),observed_ms(observed) {}

bool NegotiatedDraft::Current(const ExtensionRegistry& registry,const NegotiationPermission& permission,
    const NegotiationOwner& current,int64_t now) const
{
    return owner==current && Permitted(permission,current,now) && Fresh(observed_ms,now)
        && PayjoinStatus(registry,&current.target,current.scope.owner.network).available;
}

std::optional<NegotiatedDraft> ValidatePayjoinReply(const ExtensionRegistry& registry,const NegotiationIntent& intent,
    const NegotiationPermission& permission,const NegotiationOwner& current,const NegotiationReply& reply,
    const CounterpartyEvidence& evidence,int64_t now,std::stop_token stop)
{
    const auto original=ReadOriginal(registry,intent,permission,current,now,stop);
    if (!original || reply.owner!=current || reply.proposal.target!=current.target) return {};
    const auto disclosure=p::PayjoinDisclosureCopy(*original);
    if (!disclosure || PaymentSourceIdentity(*disclosure)!=reply.proposal.offer_digest) return {};
    const auto network=intent.owner.original_proof.network;
    const auto proposal=p::ParseTransactionDocument(reply.proposal.unsigned_psbt,network,stop).document;
    if (!proposal) return {};
    auto proof=CheckProposal(intent,*original,*proposal,evidence,now,stop);
    if (!proof) return {};
    auto working=p::PreservePayjoinMaps(*original,*proposal,network,stop);
    if (!working || !p::InspectOfflineReview(*working,*proof,intent.owner.change_output).commitment || stop.stop_requested()) return {};
    return NegotiatedDraft(current,original,proposal,std::move(working),std::move(*proof),evidence.input.proof->observed_ms);
}

ExtensionStatus PaymentCodeContactStatus(const ExtensionRegistry& registry,const PaymentCodeContact* contact)
{
    const auto* port=registry.payment_code_contacts;
    if (!port || !contact || !ValidScope(contact->scope) || !contact->code.version || !Identifier(contact->provider)
        || contact->provider!=port->Identity() || !port->Qualified(contact->code.version,contact->scope.owner.network))
        return {false,"Payment-code contacts require a qualified PaymentCodeContacts provider."};
    return {true,"The exact public contact code can be verified for this network and version."};
}

bool CurrentPaymentCodeContact(const ExtensionRegistry& registry,const PaymentCodeContact& contact,const ContactPermission& grant,
    const ContactPermission& current,std::stop_token stop)
{
    if (stop.stop_requested() || grant!=current || !grant.confirmed || !grant.ticket || grant.scope!=contact.scope
        || grant.provider!=contact.provider || grant.contact!=contact.contact || grant.version!=contact.code.version
        || grant.disclosure!=ContactDisclosure::PublicCodeAndNotificationLink || !Endpoint(grant.endpoint)
        || contact.code.code.size()>256 || PaymentSourceIdentity(contact.code.code)!=grant.code
        || !PaymentCodeContactStatus(registry,&contact).available) return false;
    try { return registry.payment_code_contacts->Verify(contact,stop) && !stop.stop_requested(); }
    catch (...) { return false; }
}

ExtensionStatus FiatRateStatus(const ExtensionRegistry& registry,std::string_view currency)
{
    const auto* provider=registry.fiat_rates;
    if (!provider || !Identifier(provider->Identity()) || currency.size()!=3
        || !std::ranges::all_of(currency,[](char c){return c>='A' && c<='Z';}) || !provider->QualifiedCurrency(currency))
        return {false,"Off. No fiat exchange-rate provider is available. Bitcoin amounts and operation remain available."};
    return {true,"A fiat rate provider is available for this currency. Network access requires separate opt-in."};
}

ExtensionStatus ReleaseUpdateStatus(const ExtensionRegistry& registry,std::string_view channel)
{
    const auto* provider=registry.release_updates;
    if (!provider || !Identifier(provider->Identity()) || !Identifier(channel) || !provider->QualifiedChannel(channel))
        return {false,"Update checks are off. No release update provider is available."};
    return {true,"A release update provider is available for this channel. Checks require separate opt-in."};
}

std::array<OptionalPort,9> OptionalPorts(const ExtensionRegistry& registry)
{
    return {{{"indexed-history",registry.history!=nullptr},{"privacy-construction",registry.construction!=nullptr},
        {"local-signer",registry.local_signer!=nullptr},{"direct-device-transport",registry.device_transport!=nullptr},
        {"reusable-receive",registry.reusable_receive!=nullptr},{"payjoin",registry.payjoin!=nullptr},
        {"payment-code-contacts",registry.payment_code_contacts!=nullptr},{"fiat-rates",registry.fiat_rates!=nullptr},
        {"release-updates",registry.release_updates!=nullptr}}};
}
} // namespace roots::gui
