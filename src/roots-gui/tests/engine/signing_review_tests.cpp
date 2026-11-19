// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "tests/engine/recovery_fixture.h"
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include <imgui_test_engine/imgui_te_engine.h>
#include <imgui_test_engine/imgui_te_context.h>

#include <algorithm>
#include <stdexcept>

using namespace roots::gui;
namespace {
namespace a=app;
namespace sh=ui::shell;
namespace c=ui::components;
constexpr auto NETWORK=protocol::DisplayNetwork::Regtest;
constexpr auto GENESIS="0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
constexpr auto KEY="0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798";
constexpr auto PAYMENT="bcrt1qrk5wn7dxwy8l6ru39h6xvmdhxnvdh8p0kpjlxw";
constexpr auto WALLET_NAME="Exact workbench wallet";

void Require(bool condition,std::string_view reason)
{
    if (!condition) throw std::runtime_error(std::string(reason));
}
std::string Bytes(std::string_view hex)
{
    std::string bytes;
    for (size_t n=0;n<hex.size();n+=2) bytes+=char(std::stoul(std::string(hex.substr(n,2)),nullptr,16));
    return bytes;
}
void Field(std::string& wire,uint8_t type,std::string_view value)
{
    Require(value.size()<253,"short fixture field");
    wire+=char(1);
    wire+=char(type);
    wire+=char(value.size());
    wire+=value;
}
struct Fixture final : a::Requests {
    sh::State state;
    sh::Facts facts;
    mutable TransactionDocuments local;
    test::RecoveryFixtureDirectory scratch;
    a::RecoverySession recovery{scratch.Metadata()};
    ConnectionService connection{[](const ServerCandidate&,std::stop_token){return BackendCapabilities{};}};
    WalletLifecycle wallets{[this](const WalletOperation&,std::stop_token){++wallet_actions;return WalletResult{};}};
    unsigned external{};
    std::atomic_uint wallet_actions{};
    uint64_t origin{},created{},loose{};

    void Create(bool taproot=false)
    {
        const a::WalletBinding wallet{*BackendSessionId::Make("test-regtest"),*WalletId::Make(WALLET_NAME),{*Hash256::Parse(GENESIS)},{}};
        origin=state.documents.OpenWallet(WALLET_NAME,wallet,true);
        auto* draft=state.documents.Active();
        draft->history.enabled=true;
        draft->section=a::Section::Send;
        draft->funding_rate="2.125";
        const auto chain=protocol::DocumentChain(NETWORK);
        const auto input_descriptor=protocol::PreviewPublicWallet(protocol::PublicImportKind::Descriptor,
            std::string(taproot ? "tr(" : "wpkh(")+KEY+")",chain);
        const auto change_descriptor=protocol::PreviewPublicWallet(protocol::PublicImportKind::Descriptor,
            "wpkh(02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5)",chain);
        Require(input_descriptor.preview.has_value(),input_descriptor.error);
        Require(change_descriptor.preview.has_value(),change_descriptor.error);
        const auto* p=&input_descriptor.preview->descriptors.front();
        const auto* ch=&change_descriptor.preview->descriptors.front();
        std::string previous="0200000001"+std::string(64,'1')+"0000000000feffffff0180c3c90100000000"
            +(taproot ? std::string("22") : "16")+p->script+"00000000";
        const auto parsed_previous=protocol::ParseTransactionDocument(previous,NETWORK).document;
        Require(parsed_previous && parsed_previous->transaction->txid,"fixture previous txid");
        const Outpoint point{*parsed_previous->transaction->txid,0};
        auto reversed=Bytes(point.txid.hex());
        std::ranges::reverse(reversed);
        auto wire=Bytes("70736274ff01fb040200000001020402000000010401010105010200");
        Field(wire,0,parsed_previous->wire);
        Field(wire,0x0e,reversed);
        Field(wire,0x0f,Bytes("00000000"));
        Field(wire,0x10,Bytes("fdffffff"));
        if (taproot) Field(wire,3,Bytes("01000000"));
        wire+='\0';
        const auto recipient=protocol::AddressScript(PAYMENT,NETWORK);
        Require(recipient.has_value(),"fixture public payment address");
        Field(wire,3,Bytes("8096980000000000"));
        Field(wire,4,Bytes(*recipient));
        wire+='\0';
        Field(wire,3,Bytes("d42b310100000000"));
        Field(wire,4,Bytes(ch->script));
        wire+='\0';
        const auto parsed=protocol::ParseTransactionDocument(protocol::TransactionBase64(wire),NETWORK).document;
        Require(bool(parsed),"fixture unsigned v2 PSBT");
        Require(draft->recipients.ApplyDestination(1,"bitcoin:"+std::string(PAYMENT)+"?amount=0.1",NETWORK,draft->draft_revision),
            "independent recipient draft");
        draft->selected_coins={point};
        a::CoinDraft evidence{wallet,{{point,Money::Satoshis(30000000),p->script,"","",
            protocol::OfflineCoinProof{previous,p->descriptor,"","",1700000000000}}}};
        evidence.offline_change=protocol::OfflineChange{ch->descriptor,0,1700000000000};
        a::ImportedDocument document{parsed,NETWORK,a::DocumentRepresentation::Source,
            a::CreatedFunding{origin,draft->draft_revision,Money::Satoshis(300),evidence}};
        std::string reason;
        created=recovery.CreateFunded(state.documents,std::move(document),reason);
        Require(created!=0,reason);
        loose=state.documents.OpenImportedDocument({parsed,NETWORK});
        Require(loose && state.documents.Activate(created),"activate exact created fixture");
    }
    a::Receipt Submit(const a::Request&) override {++external;return {true,"Unsafe mock fallback would accept"};}
    a::Availability CanSubmit(const a::Request&) const override {return {true,{}};}
    TransactionDocuments* LocalDocuments() const override {return &local;}
    a::RecoverySession* Recovery() override {return &recovery;}
    ConnectionService* Connection() override {return &connection;}
    WalletLifecycle* Wallets() override {return &wallets;}
};
void Draw(ImGuiTestContext* ctx)
{
    auto& owned=ctx->GetVars<std::unique_ptr<Fixture>>();
    if (!owned) owned=std::make_unique<Fixture>();
    const auto theme=ui::theme::Make(false);
    const ui::theme::Scope style(theme);
    owned->facts=sh::DrawWorkspace(owned->state,*owned,theme,{}, {1280,900});
}
a::Receipt Dispatch(Fixture& f,a::Command command,uint64_t id=0)
{
    return sh::DispatchSigningCommand(f.state,f,{command,id ? id : f.created});
}
bool Permits(Fixture& f,a::Command command,uint64_t id=0)
{
    return sh::SigningCommandAvailability(f.state,f,{command,id ? id : f.created}).available;
}
bool Queue(Fixture& f,std::string_view title)
{
    auto& exchange=f.state.presentation.exchange;
    const auto packet=exchange.Published();
    if (!packet || !packet->valid) return false;
    const auto found=std::ranges::find_if(packet->nodes,[title](const auto& node){
        return node.name==title && !node.state.disabled && c::ax::Supports(node.actions,c::ax::SemanticAction::Press);
    });
    return found!=packet->nodes.end() && exchange.QueueNext(packet->generation,found->id,c::ax::SemanticAction::Press).second==c::ActionResult::Queued;
}
std::string SignDenial(Fixture& f,uint64_t doc=0)
{
    const auto receipt=Dispatch(f,a::Command::SignTransaction,doc);
    Require(!receipt.accepted,"unavailable signer must never dispatch");
    return receipt.reason;
}
void Alter(Fixture& f,std::string_view field,protocol::InspectionNode group,size_t index,uint8_t value)
{
    auto& document=*f.state.documents.Find(f.created)->imported;
    const auto fields=protocol::InspectionFields(*document.parsed,protocol::InspectionBytes::Wire,group,index);
    const auto found=std::ranges::find_if(fields,[field](const auto& candidate){return candidate.name==field;});
    Require(found!=fields.end(),"fixture mapped mutation");
    auto bytes=document.parsed->wire;
    if (field=="Requested sighash") bytes.at(found->bytes.offset)=char(value);
    else bytes.at(found->bytes.offset)^=char(value);
    const auto result=protocol::ParseTransactionDocument(protocol::TransactionBase64(bytes),NETWORK);
    Require(bool(result.document),"mutated PSBT fixture still parses");
    document.parsed=result.document;
}
}
void RegisterSigningReviewTests(ImGuiTestEngine* engine)
{
    auto* test=IM_REGISTER_TEST(engine,"roots.signing-review","create_requires_separate_review_and_approval");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc=Draw;
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& f=*ctx->GetVars<std::unique_ptr<Fixture>>();
        f.Create();
        ctx->SetRef("###workspace");
        ctx->Yield(3);
        auto& doc=*f.state.documents.Find(f.created);
        f.state.imported_views[f.created].reviewed=doc.imported->parsed; // prior generic review grants no consent
        IM_CHECK(!Dispatch(f,a::Command::ApproveSigning).accepted);
        IM_CHECK(SignDenial(f).find("matching current review and explicit approval")!=std::string::npos);
        IM_CHECK(!Permits(f,a::Command::SignTransaction));
        ctx->ItemClick(f.facts.transaction_document.at("review-open").id);
        ctx->Yield(3);
        IM_CHECK(f.state.presentation.Snapshot().find("Signing wallet: Exact workbench wallet")!=std::string::npos);
        IM_CHECK(f.state.presentation.Snapshot().find("Proved intended change")!=std::string::npos);
        a::Request injected{a::Command::ApproveSigning,f.created};
        injected.value="claimed external approval";
        IM_CHECK(!sh::DispatchSigningCommand(f.state,f,injected).accepted);
        IM_CHECK(Queue(f,"Record signing review"));
        ctx->Yield(3);
        IM_CHECK(Permits(f,a::Command::ApproveSigning));
        IM_CHECK(SignDenial(f).find("matching current review and explicit approval")!=std::string::npos);
        IM_CHECK(Queue(f,"Approve for signing"));
        ctx->Yield(3);
        IM_CHECK(f.state.presentation.Snapshot().find("Approved for this exact transaction")!=std::string::npos);
        const auto* authorization=sh::CurrentSigningApproval(f.state,f,f.created);
        IM_CHECK(authorization && authorization->reviewed.wallet.DaemonWallet()==WALLET_NAME
            && authorization->reviewed.proof.recipients.size()==1 && authorization->reviewed.proof.change
            && authorization->original_source==a::SigningDigest(doc.imported->parsed->source)
            && authorization->commitment==protocol::TransactionCommitment(*doc.imported->parsed));
        IM_CHECK(!Permits(f,a::Command::SignTransaction) && SignDenial(f)==a::MissingDependency(a::Command::SignTransaction));
        IM_CHECK(f.external==0 && f.wallet_actions==0);
        const auto encoded=a::EncodePublicSession({f.state.documents,{}});
        const auto decoded=encoded ? a::DecodePublicSession(*encoded) : std::nullopt;
        IM_CHECK(decoded && decoded->documents.Active()->imported->parsed->source==doc.imported->parsed->source);
        sh::State fresh;
        fresh.documents=decoded->documents;
        auto receipt=sh::DispatchSigningCommand(fresh,f,{a::Command::SignTransaction,f.created});
        IM_CHECK(!receipt.accepted && receipt.reason.find("matching current review and explicit approval")!=std::string::npos);
        IM_CHECK(!Dispatch(f,a::Command::ApproveSigning,f.loose).accepted);
    };
    test=IM_REGISTER_TEST(engine,"roots.signing-review","proof_and_transaction_changes_revoke_current_approval");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc=Draw;
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& f=*ctx->GetVars<std::unique_ptr<Fixture>>();
        f.Create();
        ctx->SetRef("###workspace");
        ctx->Yield(3);
        IM_CHECK(Queue(f,"Signing review"));
        ctx->Yield(3);
        const auto approve=[&] {
            return Dispatch(f,a::Command::ReviewForSigning).accepted && Dispatch(f,a::Command::ApproveSigning).accepted
                && SignDenial(f)==a::MissingDependency(a::Command::SignTransaction);
        };
        IM_CHECK(approve());
        auto& imported=*f.state.documents.Active()->imported;
        const auto original=imported.parsed;
        Alter(f,"Output amount",protocol::InspectionNode::Output,0,1);
        IM_CHECK(SignDenial(f)!=a::MissingDependency(a::Command::SignTransaction));
        IM_CHECK(!f.state.signing_reviews.Reason(f.created).empty());
        imported.parsed=original;
        IM_CHECK(!Dispatch(f,a::Command::ApproveSigning).accepted && approve());
        ++imported.funding->evidence.offline_change->observed_ms;
        IM_CHECK(SignDenial(f)!=a::MissingDependency(a::Command::SignTransaction));
        IM_CHECK(f.state.signing_reviews.Reason(f.created).find("change")!=std::string_view::npos);
        IM_CHECK(approve());
        f.state.documents.Activate(f.loose);
        ctx->Yield(3);
        IM_CHECK(SignDenial(f,f.loose).find("Independent payment")!=std::string::npos);
        f.state.documents.Activate(f.created);
        ctx->Yield(3);
        IM_CHECK(SignDenial(f)==a::MissingDependency(a::Command::SignTransaction));
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiKey_H);
        ctx->Yield(3);
        IM_CHECK(!sh::CurrentSigningApproval(f.state,f,f.created) && f.facts.transaction_document.empty());
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiKey_H);
        ctx->Yield(3);
        IM_CHECK(Queue(f,"Signing review"));
        ctx->Yield(3);
        IM_CHECK(!Dispatch(f,a::Command::ApproveSigning).accepted && approve());
        ++f.state.documents.Find(f.origin)->draft_revision.value;
        IM_CHECK(SignDenial(f).find("revision")!=std::string::npos);
        IM_CHECK(f.external==0 && f.wallet_actions==0);
    };
    test=IM_REGISTER_TEST(engine,"roots.signing-review","advanced_acknowledgement_and_no_wallet_fallback");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc=Draw;
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& f=*ctx->GetVars<std::unique_ptr<Fixture>>();
        f.Create(true);
        ctx->SetRef("###workspace");
        ctx->Yield(3);
        IM_CHECK(Queue(f,"Signing review"));
        ctx->Yield(3);
        IM_CHECK(!Permits(f,a::Command::ReviewForSigning) && !Dispatch(f,a::Command::ApproveSigning).accepted);
        IM_CHECK(Queue(f,"Advanced sighash"));
        ctx->Yield(3);
        IM_CHECK(f.state.presentation.Snapshot().find("Taproot DEFAULT commits like ALL")!=std::string::npos);
        IM_CHECK(Queue(f,"Acknowledge exact sighash commitments"));
        ctx->Yield(3);
        IM_CHECK(Dispatch(f,a::Command::ReviewForSigning).accepted);
        IM_CHECK(Dispatch(f,a::Command::ApproveSigning).accepted);
        Alter(f,"Requested sighash",protocol::InspectionNode::Input,0,3);
        IM_CHECK(SignDenial(f).find("unsupported sighash")!=std::string::npos);
        IM_CHECK(!Dispatch(f,a::Command::ApproveSigning).accepted);
        f.state.documents.Activate(f.loose);
        ctx->Yield(3);
        IM_CHECK(Queue(f,"Signing review"));
        ctx->Yield(3);
        IM_CHECK(f.facts.transaction_document.contains("review-open-wallet") && f.facts.transaction_document.contains("review-export-original"));
        IM_CHECK(!Dispatch(f,a::Command::ReviewForSigning,f.loose).accepted);
        IM_CHECK(Queue(f,"Open Wallet"));
        ctx->Yield(3);
        IM_CHECK(f.state.onboarding.dialog.open && f.state.onboarding.page==sh::OnboardingPage::Open);
        IM_CHECK(f.external==0 && f.wallet_actions==0);
    };
}

namespace {
bool SetField(Fixture& f,std::string_view title,std::string value)
{
    auto& exchange=f.state.presentation.exchange;
    const auto packet=exchange.Published();
    if (!packet || !packet->valid) return false;
    const auto found=std::ranges::find_if(packet->nodes,[title](const auto& node){
        return node.name==title && !node.state.disabled && c::ax::Supports(node.actions,c::ax::SemanticAction::SetValue);
    });
    return found!=packet->nodes.end()
        && exchange.QueueNext(packet->generation,found->id,c::ax::SemanticAction::SetValue,std::move(value)).second==c::ActionResult::Queued;
}
a::Request ApplyFields(const Fixture& f,uint64_t id,protocol::TransactionEdit fields)
{
    const auto& editor=f.state.transaction_edits.at(id);
    a::Request request{a::Command::ApplyTransactionEdit,id};
    request.edit=a::TransactionEditRequest{editor.generation,editor.owner.draft,std::move(fields)};
    return request;
}
}

void RegisterTransactionEditTests(ImGuiTestEngine* engine)
{
    auto* test=IM_REGISTER_TEST(engine,"roots.transaction-edit","approval_retires_then_future_locktime_keyboard_order_reapprove");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc=Draw;
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& f=*ctx->GetVars<std::unique_ptr<Fixture>>();
        auto& state=f.state;
        f.Create();
        const auto prior=state.documents.Active()->imported->parsed;
        ctx->SetRef("###workspace");
        ctx->Yield(3);
        IM_CHECK(Queue(f,"Signing review"));
        ctx->Yield(3);
        IM_CHECK(Dispatch(f,a::Command::ReviewForSigning).accepted && Dispatch(f,a::Command::ApproveSigning).accepted);
        ctx->Yield(3);
        const auto* approved=sh::CurrentSigningApproval(state,f,f.created);
        IM_CHECK(approved);
        const auto old_commitment=approved->commitment;
        a::Request unauthorized{a::Command::ApplyTransactionEdit,f.created};
        unauthorized.edit=a::TransactionEditRequest{1,state.documents.Active()->draft_revision,{.locktime=900000}};
        IM_CHECK(!sh::TransactionEditAvailability(state,f,unauthorized).available
            && !sh::DispatchTransactionEdit(state,f,unauthorized).accepted);
        IM_CHECK(state.documents.Active()->imported->parsed==prior);
        IM_CHECK(Queue(f,"Return to editing"));
        ctx->Yield(3);
        IM_CHECK(!sh::CurrentSigningApproval(state,f,f.created) && SignDenial(f).find("editor")!=std::string::npos);
        IM_CHECK(SetField(f,"Transaction version","3"));
        ctx->Yield(3);
        IM_CHECK(SetField(f,"Transaction locktime","900000"));
        ctx->Yield(3);
        IM_CHECK(state.presentation.Snapshot().find("Block height 900000")!=std::string::npos);
        IM_CHECK(Queue(f,"Apply fields"));
        ctx->Yield(3);
        const auto working=state.documents.ActiveId();
        IM_CHECK(working!=f.created && working!=f.origin && state.documents.Find(f.created)->imported->parsed==prior);
        IM_CHECK(state.documents.Active()->imported->parsed->transaction->version==3
            && state.documents.Active()->imported->parsed->transaction->locktime==900000);
        IM_CHECK(!state.documents.Active()->imported->funding && state.edited_origins.at(working).id==f.created);
        ctx->ItemClick(f.facts.transaction_document.at("edit-outputs").id);
        ctx->Yield(2);
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiKey_DownArrow);
        ctx->Yield(3);
        IM_CHECK(state.documents.ActiveId()==working && state.transaction_edits.at(working).index==1);
        const auto& tx=*state.documents.Active()->imported->parsed->transaction;
        IM_CHECK(tx.outputs.front()==prior->transaction->outputs.back() && tx.outputs.back()==prior->transaction->outputs.front());
        IM_CHECK(SetField(f,"Output amount (sats)","10000001"));
        ctx->Yield(3);
        state.documents.Activate(f.loose);
        ctx->Yield(3);
        state.documents.Activate(working);
        ctx->Yield(3);
        IM_CHECK(state.transaction_edits.at(working).amount.draft()=="10000001" && state.transaction_edits.at(working).index==1);
        IM_CHECK(Queue(f,"Discard pending fields"));
        ctx->Yield(3);
        IM_CHECK(Queue(f,"Back to inspection"));
        ctx->Yield(3);
        IM_CHECK(!state.transaction_edits.contains(working) && state.imported_views.at(working).index==1);
        IM_CHECK(SignDenial(f,working).find("matching current review and explicit approval")!=std::string::npos);
        IM_CHECK(Queue(f,"Signing review"));
        ctx->Yield(3);
        IM_CHECK(state.presentation.Snapshot().find("Proved intended change")!=std::string::npos
            && state.presentation.Snapshot().find("chain maturity Unknown")!=std::string::npos);
        IM_CHECK(!Dispatch(f,a::Command::ApproveSigning,working).accepted);
        IM_CHECK(Dispatch(f,a::Command::ReviewForSigning,working).accepted && Dispatch(f,a::Command::ApproveSigning,working).accepted);
        const auto* renewed=sh::CurrentSigningApproval(state,f,working);
        IM_CHECK(renewed && renewed->commitment!=old_commitment && renewed->reviewed.change_output==0
            && renewed->reviewed.proof.recipients==std::vector<protocol::TransactionOutput>{prior->transaction->outputs[0]});
        IM_CHECK(f.external==0 && f.wallet_actions==0);
    };
    test=IM_REGISTER_TEST(engine,"roots.transaction-edit","imported_negotiation_never_manufactures_payment_proof");
    test->SetVarsDataType<std::unique_ptr<Fixture>>();
    test->GuiFunc=Draw;
    test->TestFunc=[](ImGuiTestContext* ctx) {
        auto& f=*ctx->GetVars<std::unique_ptr<Fixture>>();
        auto& state=f.state;
        f.Create();
        state.documents.Activate(f.loose);
        const auto original=state.documents.Active()->imported->parsed;
        ctx->SetRef("###workspace");
        ctx->Yield(3);
        IM_CHECK(sh::DispatchTransactionEdit(state,f,{a::Command::BeginTransactionEdit,f.loose}).accepted);
        auto change=original->transaction->outputs.back();
        change.value=Money::Satoshis(change.value.sats()-1);
        const auto plan=protocol::TransactionEdit{.output=protocol::EditOutput{1,change}};
        const auto stale=ApplyFields(f,f.loose,plan);
        ++state.documents.Active()->draft_revision.value;
        IM_CHECK(!sh::DispatchTransactionEdit(state,f,stale).accepted && state.documents.Active()->imported->parsed==original);
        ctx->Yield(3);
        IM_CHECK(!state.transaction_edits.contains(f.loose));
        uint64_t vacancy{};
        while (state.documents.Documents().size()<32) vacancy=state.documents.OpenTransaction("Capacity fixture");
        state.documents.Activate(f.loose);
        ctx->Yield(3);
        IM_CHECK(sh::DispatchTransactionEdit(state,f,{a::Command::BeginTransactionEdit,f.loose}).accepted);
        const auto request=ApplyFields(f,f.loose,plan);
        const auto refused=sh::DispatchTransactionEdit(state,f,request);
        IM_CHECK(!refused.accepted && refused.reason.find("Close a document")!=std::string::npos
            && state.documents.Active()->imported->parsed==original && state.transaction_edits.contains(f.loose));
        IM_CHECK(state.documents.Close(std::array{vacancy}));
        IM_CHECK(sh::DispatchTransactionEdit(state,f,request).accepted);
        const auto working=state.documents.ActiveId();
        IM_CHECK(state.documents.Active()->imported->parsed->transaction->outputs.back()==change
            && state.documents.Find(f.loose)->imported->parsed==original);
        ctx->Yield(3);
        IM_CHECK(Queue(f,"Back to inspection"));
        ctx->Yield(3);
        IM_CHECK(Queue(f,"Signing review"));
        ctx->Yield(3);
        IM_CHECK(state.presentation.Snapshot().find("intent Unknown")!=std::string::npos
            && state.presentation.Snapshot().find("Proved intended change")==std::string::npos);
        IM_CHECK(!Dispatch(f,a::Command::ReviewForSigning,working).accepted
            && !Dispatch(f,a::Command::ApproveSigning,working).accepted
            && SignDenial(f,working).find("Independent payment")!=std::string::npos);
        const auto persisted=a::EncodePublicSession({state.documents,{}});
        const auto recovered=persisted ? a::DecodePublicSession(*persisted) : std::nullopt;
        IM_CHECK(recovered && recovered->documents.Find(f.loose)->imported->parsed->source==original->source
            && recovered->documents.Find(working)->imported->parsed->source==state.documents.Active()->imported->parsed->source);
        IM_CHECK(f.external==0 && f.wallet_actions==0);
    };
}
