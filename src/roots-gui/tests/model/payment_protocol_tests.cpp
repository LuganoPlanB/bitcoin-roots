// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/payment_review.h"
#include "services/send_recipients.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
namespace a=app;
namespace p=protocol;
namespace {
constexpr auto GENESIS="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
constexpr auto MAIN="000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";
constexpr auto SENDER="wpkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)";
constexpr auto RECEIVER="wpkh(02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5)";
constexpr auto PAYMENT="bcrt1qrk5wn7dxwy8l6ru39h6xvmdhxnvdh8p0kpjlxw";
constexpr auto NET=p::DisplayNetwork::Regtest;
constexpr int64_t NOW=200'000;
constexpr std::string_view OPAQUE="\x04\xfc\x01" "p\x01\x06" "opaque";
unsigned checks{};

template<typename T> void Check(const T& ok,const char* why)
{
    if (!static_cast<bool>(ok)) throw std::runtime_error(why);
    ++checks;
}
NetworkIdentity Network(std::string_view genesis=GENESIS) {return {*Hash256::Parse(genesis)};}
Hash256 Digest(std::string_view bytes) {return *PaymentSourceIdentity(bytes);}
Money Sats(int64_t amount) {return Money::Satoshis(amount);}
size_t InputMapStart(const p::TransactionDocument& document)
{
    const auto& fields=document.wire_layout.inputs.at(0);
    const auto entry=std::ranges::find_if(fields,[](const auto& field){return field.name.ends_with("(serialized PSBT entry)");});
    if (entry==fields.end()) throw std::runtime_error("fixture input map is unavailable");
    return entry->bytes.offset;
}
CustodyScope Scope()
{
    return {{*BackendSessionId::Make("fixture-backend"),Network(),*WalletId::Make("public-wallet"),
        *DocumentId::Make("original-payment"),{8},{2}},17,6,10,Digest("independently scoped policy")};
}

struct Payjoin final:PayjoinModule {
    bool qualified{true};
    mutable unsigned exchanges{};
    std::string_view Identity() const override {return "payjoin-fixture";}
    bool Qualified(const PayjoinTarget& target,NetworkIdentity network) const override {
        return qualified && target.endpoint=="https://wallet.invalid/receive" && target.version=="fixture-unsigned-v1" && network==Network();
    }
    std::optional<PayjoinProposal> Exchange(const PayjoinOffer&,std::stop_token) const override {++exchanges;return {};}
};
struct Contacts final:PaymentCodeContacts {
    mutable unsigned verifies{};
    std::string_view Identity() const override {return "contacts-fixture";}
    bool Qualified(uint8_t version,NetworkIdentity network) const override {return version==1 && network==Network();}
    bool Verify(const PaymentCodeContact& contact,std::stop_token) const override {
        ++verifies;
        return contact.code.code=="fixture-public-code";
    }
};
struct Fiat final:FiatRateProvider {
    std::string_view Identity() const override {return "fiat-fixture";}
    bool QualifiedCurrency(std::string_view currency) const override {return currency=="EUR";}
};
struct Releases final:ReleaseUpdateProvider {
    std::string_view Identity() const override {return "release-fixture";}
    bool QualifiedChannel(std::string_view channel) const override {return channel=="stable";}
};

p::OfflineInput Coin(char source,std::string_view public_descriptor)
{
    const auto descriptor=p::PreviewPublicWallet(p::PublicImportKind::Descriptor,public_descriptor,"regtest");
    Check(descriptor.preview && descriptor.preview->descriptors.size()==1,"derived independent public fixture policy");
    const auto& key=descriptor.preview->descriptors.front();
    const auto hex="0200000001"+std::string(64,source)+"0000000000feffffff0180c3c9010000000016"+key.script+"00000000";
    const auto raw=p::ParseTransactionDocument(hex,NET).document;
    Check(raw && raw->transaction->txid,"independent previous output bytes");
    return {{*raw->transaction->txid,0},Sats(30'000'000),key.script,p::OfflineCoinProof{hex,key.descriptor,{},{},NOW-25}};
}

struct Fixture {
    p::OfflineInput sender=Coin('1',SENDER),receiver=Coin('2',RECEIVER);
    p::OfflineChange change{sender.proof->descriptor,0,NOW-25};
    const p::OfflineResult original=p::ConstructOfflineDraft({NET,{sender},{{PAYMENT,Sats(10'000'000)}},Sats(300),change});
    const p::OfflineResult proposal=p::ConstructOfflineDraft({NET,{sender,receiver},{{PAYMENT,Sats(39'999'950)}},Sats(350),change});
    Payjoin provider;
    ExtensionRegistry registry{.payjoin=&provider};
    NegotiationIntent intent=MakeIntent();
    NegotiationPermission grant{intent.owner,NOW+30'000,true};
    CounterpartyEvidence independent{intent.owner.scope,intent.owner.tip,receiver};

    NegotiationIntent MakeIntent() const {
        Check(original.document && original.review_evidence && proposal.document,"real local unsigned construction proof");
        auto bytes=original.document->wire;
        // The fixed change output has an empty final output map. The first
        // sender input entry is available in the existing inspection layout.
        bytes.insert(bytes.size()-1,OPAQUE);
        bytes.insert(InputMapStart(*original.document),OPAQUE);
        bytes.insert(5,OPAQUE);
        PayjoinTarget target{"payjoin-fixture","https://wallet.invalid/receive","fixture-unsigned-v1",*p::AddressScript(PAYMENT,NET)};
        return {{Scope(),2,19,Digest(bytes),Digest("independent tip"),std::move(target),
            PayjoinDisclosure::UnsignedPaymentAndPreviousTransactions,*original.review_evidence,1,0,Sats(400)},std::move(bytes)};
    }
    std::optional<PayjoinOffer> Offer() const {
        return PreparePayjoinOffer(registry,intent,grant,intent.owner,NOW);
    }
    NegotiationReply Reply() const {
        const auto offer=Offer();
        const auto clean=p::PayjoinDisclosureCopy(*proposal.document);
        Check(offer && clean,"both unsigned wire variants are representable");
        return {intent.owner,{intent.owner.target,Digest(offer->unsigned_psbt),*clean}};
    }
    std::optional<NegotiatedDraft> Validate(const NegotiationReply& reply) const {
        return ValidatePayjoinReply(registry,intent,grant,intent.owner,reply,independent,NOW);
    }
};

void PermissionCases()
{
    Fixture f;
    const auto& shipped=ShippedExtensions();
    const auto& current=f.intent.owner;
    Check(!PayjoinStatus(shipped,&current.target,Network()).available && !PayjoinStatus(f.registry).available
        && !PreparePayjoinOffer(shipped,f.intent,f.grant,current,NOW),"no provider and no exact navigated context deny offers");
    const auto offer=f.Offer();
    Check(offer && offer->unsigned_psbt.find(OPAQUE)==std::string::npos
        && f.intent.original_source.find(OPAQUE)!=std::string::npos && !f.provider.exchanges,
        "exact permission creates a sanitized local offer without transporting it");
    const auto shared=p::ParseTransactionDocument(offer->unsigned_psbt,NET).document;
    const auto local=p::ParseTransactionDocument(f.intent.original_source,NET).document;
    Check(shared && local && shared->psbt_entries+3==local->psbt_entries,
        "outgoing copy omits opaque global, input and output metadata");
    f.grant.confirmed=false;
    Check(!f.Offer(),"navigation does not authorize disclosure");
    f.grant.confirmed=true;
    auto changed=current;
    changed.maximum_fee=Sats(401);
    Check(!PreparePayjoinOffer(f.registry,f.intent,f.grant,changed,NOW),"fee cap is part of immutable consent");
    changed=current;
    changed.target.endpoint="https://other.invalid/receive";
    Check(!PreparePayjoinOffer(f.registry,f.intent,f.grant,changed,NOW),"endpoint change retires permission");
    changed=current;
    ++changed.scope.owner.lock_epoch.value;
    Check(!PreparePayjoinOffer(f.registry,f.intent,f.grant,changed,NOW),"lock epoch retires permission");
    Check(!PreparePayjoinOffer(f.registry,f.intent,f.grant,current,NOW+30'001),"expired permission creates no offer");
    changed=current;
    changed.target.endpoint="https://wallet.invalid@other.invalid/receive";
    Check(!PayjoinStatus(f.registry,&changed.target,Network()).available
        && !PayjoinStatus(f.registry,&current.target,Network(MAIN)).available,"reject credential-bearing endpoint syntax and foreign network");
    const auto uri=std::string("bitcoin:")+PAYMENT;
    const auto request=ParsePaymentRequest(uri+"?amount=0.1&pj=https%3A%2F%2Fwallet.invalid%2Freceive",NET);
    Check(request.address.empty() && request.error.find("PayjoinModule")!=std::string::npos,
        "unsupported payment URI cannot silently fall back to ordinary payment");
    const auto ordinary=ParsePaymentRequest(uri+"?amount=0.1",NET);
    Check(ordinary.error.empty() && ordinary.amount==Sats(10'000'000),"a separate plain request remains an exact Bitcoin payment");
}

void ProposalCases()
{
    Fixture f;
    auto reply=f.Reply();
    const auto prepared=f.Validate(reply);
    Check(prepared && prepared->Original().source==f.intent.original_source
        && prepared->Proposal().source==reply.proposal.unsigned_psbt && prepared->Working()->wire.find(OPAQUE)!=std::string::npos
        && prepared->Proof().fee==Sats(350) && prepared->Proof().recipients[0].value==Sats(39'999'950),
        "independent proof and exact original, working, incoming and opaque state retained");
    auto change=p::EditTransaction(*prepared->Working(),NET,{.output=p::EditOutput{0,{Sats(39'999'950),f.receiver.script}}});
    Check(change.document,"construct parseable negotiated recipient substitution");
    reply.proposal.unsigned_psbt=*p::PayjoinDisclosureCopy(*change.document);
    Check(!f.Validate(reply),"recipient script substitution rejected");
    const auto original_change=prepared->Working()->transaction->outputs[1];
    change=p::EditTransaction(*prepared->Working(),NET,{.output=p::EditOutput{1,{Sats(original_change.value.sats()-100), original_change.script}}});
    Check(change.document,"construct parseable negotiated change reduction");
    reply.proposal.unsigned_psbt=*p::PayjoinDisclosureCopy(*change.document);
    Check(!f.Validate(reply),"sender change or fee contribution cannot be reduced");
    change=p::EditTransaction(*prepared->Working(),NET,{.output=p::EditOutput{0,{Sats(39'999'850),f.intent.owner.target.recipient_script}}});
    Check(change.document,"construct parseable fee-cap violation");
    reply.proposal.unsigned_psbt=*p::PayjoinDisclosureCopy(*change.document);
    Check(!f.Validate(reply),"extra receiver input does not waive the absolute fee cap");
    change=p::EditTransaction(*prepared->Working(),NET,{.input=p::EditInput{1,f.receiver.point,42}});
    Check(change.document,"construct parseable counterparty relative-lock mutation");
    reply.proposal.unsigned_psbt=*p::PayjoinDisclosureCopy(*change.document);
    Check(!f.Validate(reply),"counterparty cannot change effective relative-lock or RBF policy");
    reply=f.Reply();
    auto& altered=reply.proposal.unsigned_psbt;
    const std::string_view all("\x01\x03\x04\x01\0\0\0",7);
    const auto mode=altered.find(all);
    Check(mode!=altered.npos,"locate actual PSBT sighash field");
    altered[mode+3]=char(0x81);
    const auto downgraded=p::ParseTransactionDocument(altered,NET).document;
    Check(downgraded && p::ReviewSighash(*downgraded,0)==0x81 && !f.Validate(reply),"reject negotiated ANYONECANPAY mutation");
    reply=f.Reply();
    const auto input=InputMapStart(prepared->Proposal());
    reply.proposal.unsigned_psbt.insert(input,std::string_view("\x01\x07\x01\0",4));
    const auto final_field=p::ParseTransactionDocument(reply.proposal.unsigned_psbt,NET).document;
    Check(final_field && final_field->input_policy[0].final_fields && !f.Validate(reply),"pre-finalized field rejected");
    reply=f.Reply();
    reply.proposal.unsigned_psbt.insert(5,OPAQUE);
    Check(!f.Validate(reply),"unrecognized peer metadata cannot enter local maps");
    reply=f.Reply();
    reply.proposal.offer_digest=Digest("different sanitized offer");
    Check(!f.Validate(reply),"proposal receipt is bound to exact outgoing bytes");
    reply=f.Reply();
    f.independent.tip=Digest("stale chain tip");
    Check(!f.Validate(reply),"independently observed prevout must belong to the current tip");
    f.independent.tip=f.intent.owner.tip;
    f.independent.input.proof->observed_ms=NOW-121'000;
    Check(!f.Validate(reply),"stale previous-output proof rejected");
    f.independent.input.proof->observed_ms=NOW-25;
    f.independent.input.value=Sats(29'999'999);
    Check(!f.Validate(reply),"recipient provider cannot substitute an independent previous output value");
    f.independent.input.value=f.receiver.value;
    reply.owner.scope.owner.network=Network(MAIN);
    Check(!f.Validate(reply),"foreign reply owner cannot be relabelled");
    reply=f.Reply();
    std::stop_source stop;
    stop.request_stop();
    Check(!ValidatePayjoinReply(f.registry,f.intent,f.grant,f.intent.owner,reply,f.independent,NOW,stop.get_token())
        && !f.provider.exchanges,"validation settles locally on cancellation and never performs transport");
}

void FreshReviewCases()
{
    Fixture f;
    const auto ready=f.Validate(f.Reply());
    Check(ready,"validated unsigned fixture proposal");
    const auto& owner=f.intent.owner;
    const a::SigningBasis old{owner.document,1,1,17,6,{8},{8},{2},false,
        {owner.scope.owner.backend,owner.scope.owner.wallet,Network()},f.original.document,*f.original.review_evidence,1,
        owner.scope.policy,owner.scope.policy,{}, {1},false};
    a::SigningReviews reviews;
    std::optional<NegotiatedDraft> slot;
    Check(reviews.Record(old) && reviews.Approve(old) && reviews.Authorize(old),"independently approve original work");
    auto later=owner;
    ++later.ticket;
    Check(!a::StageNegotiatedDraft(f.registry,f.grant,later,NOW,*ready,slot,reviews) && reviews.Authorize(old),
        "a result from the retired ticket cannot mutate current review");
    std::stop_source cancelled;
    cancelled.request_stop();
    Check(!a::StageNegotiatedDraft(f.registry,f.grant,owner,NOW,*ready,slot,reviews,cancelled.get_token()) && !slot
        && reviews.Authorize(old),"cancellation after independent validation blocks adoption");
    f.provider.qualified=false;
    Check(!a::StageNegotiatedDraft(f.registry,f.grant,owner,NOW,*ready,slot,reviews) && !slot,"lost provider qualification blocks adoption");
    f.provider.qualified=true;
    Check(a::StageNegotiatedDraft(f.registry,f.grant,owner,NOW,*ready,slot,reviews)
        && !reviews.Authorize(old) && !reviews.HasReview(owner.document),"changed proposal retires original approval");
    auto fresh=old;
    ++fresh.document_revision.value;
    fresh.content=slot->Working();
    fresh.proof=slot->Proof();
    fresh.sighash={1,1};
    Check(!reviews.Approve(fresh) && !reviews.Finalize(old,f.original.document).artifact
        && reviews.Record(fresh) && reviews.Approve(fresh),"changed unsigned material requires separate new review and approval");
    a::RetireNegotiatedDraft(later,owner,slot,reviews);
    Check(slot && reviews.Authorize(fresh),"retired foreign ticket cannot revoke newer review");
    a::RetireNegotiatedDraft(owner,owner,slot,reviews);
    Check(!slot && !reviews.Authorize(fresh),"scope retirement discards negotiation and approval");
}

void ProviderCases()
{
    const auto& shipped=ShippedExtensions();
    const auto ports=OptionalPorts(shipped);
    Check(std::ranges::none_of(ports,&OptionalPort::linked),"test providers cannot enter compiled shipped registry");
    Contacts contacts;
    Fiat fiat;
    Releases release;
    const ExtensionRegistry test_ports{.payment_code_contacts=&contacts,.fiat_rates=&fiat,.release_updates=&release};
    const PaymentCodeContact contact{Scope(),"contacts-fixture",{"fixture-public-code",1},Digest("verified contact identity")};
    ContactPermission grant{contact.scope,contact.provider,"https://contacts.invalid/notify",contact.contact,Digest(contact.code.code),
        19,1,ContactDisclosure::PublicCodeAndNotificationLink,true};
    Check(!CurrentPaymentCodeContact(shipped,contact,grant,grant,{}) && !PaymentCodeContactStatus(shipped).available,
        "disabled contacts have no contact lookup or synthetic code");
    Check(CurrentPaymentCodeContact(test_ports,contact,grant,grant) && contacts.verifies==1,"fixture verifies current public code only");
    auto retired=grant;
    retired.endpoint="https://other.invalid/notify";
    Check(!CurrentPaymentCodeContact(test_ports,contact,grant,retired) && contacts.verifies==1,"contact permission binds exact endpoint");
    retired=grant;
    retired.disclosure=ContactDisclosure::None;
    Check(!CurrentPaymentCodeContact(test_ports,contact,retired,retired) && contacts.verifies==1,"no contact verification without scoped action");
    auto substituted=contact;
    substituted.code.code="substitute-fixture-code";
    Check(!CurrentPaymentCodeContact(test_ports,substituted,grant,grant) && contacts.verifies==1,"swapped code cannot reuse grant");
    Check(!FiatRateStatus(shipped,"EUR").available && !ReleaseUpdateStatus(shipped,"stable").available,
        "optional fiat and release services are off before user action");
    Check(FiatRateStatus(test_ports,"EUR").available && !FiatRateStatus(test_ports,"USD").available
        && ReleaseUpdateStatus(test_ports,"stable").available && !ReleaseUpdateStatus(test_ports,"test").available,
        "provider capability is scoped to currency or channel and does not make a request");
}
}

int main(int argc,char** argv)
{
    if (argc==2 && std::string_view(argv[1])=="--shipped-ports") {
        for (const auto& [id,linked]:OptionalPorts(ShippedExtensions())) std::cout<<id<<' '<<linked<<'\n';
        return 0;
    }
    if (argc!=1) return 2;
    try {
        PermissionCases();
        ProposalCases();
        FreshReviewCases();
        ProviderCases();
        std::cout<<"{\"selected\":4,\"executed\":4,\"passed\":4,\"failed\":0,\"skipped\":0,\"assertions\":"<<checks<<"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"Payment protocols failed after "<<checks<<" assertions: "<<error.what()<<'\n';
        return 1;
    }
}
