// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/coin_controls.h"
#include "app/history_workspace.h"
#include "services/wallet_lifecycle.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
using namespace std::chrono_literals;
namespace {
namespace p=protocol;
int checks{};
void Check(bool ok,const char* message)
{
    if (!ok) throw std::runtime_error(message);
    ++checks;
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream file(path,std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file),{});
}
auto Parsed(const std::filesystem::path& path)
{
    auto document=p::ParseTransactionDocument(Read(path),p::DisplayNetwork::Regtest).document;
    Check(bool(document),"unable to open public test document");
    return document;
}
struct Online {
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    ConnectionService connection{[this](const auto& candidate,std::stop_token stop){return ProbeCapabilities(transport,candidate,stop);}};
    RefreshService refresh{[this](const auto& request,std::stop_token stop){return FetchRefresh(transport,request,stop);}};
    Online(const char* port,const char* cookie,const char* genesis,const char* name)
    {
        ServerCandidate profile;
        profile.connection.port=uint16_t(std::stoul(port));
        profile.connection.credential.reference=cookie;
        profile.expected_genesis=genesis;
        profile.wallet=name;
        Check(connection.SetDraft(profile) && connection.Test(),"begin explicit authenticated capability check");
        const auto deadline=std::chrono::steady_clock::now()+10s;
        while (connection.Busy() && std::chrono::steady_clock::now()<deadline) {
            connection.Poll();
            std::this_thread::sleep_for(2ms);
        }
        Check(connection.Apply(),"apply exact scoped capabilities");
    }
    RefreshView ReadWallet(app::DocumentRegistry& docs,app::RecoverySession& recovery)
    {
        const auto& caps=*connection.Active();
        const auto binding=app::VerifiedWalletBinding(caps);
        Check(bool(binding),"verified exact wallet binding");
        const auto id=docs.OpenWallet(binding->DaemonWallet(),*binding,true);
        auto* document=docs.Find(id);
        document->history.enabled=true;
        const auto key=app::WalletRefreshIdentity(caps,connection.Session(),*document,recovery.Gate().Epoch());
        Check(key && refresh.Bind(*key,caps,false,RefreshMonotonicMs()),"bind exact history request");
        const auto deadline=std::chrono::steady_clock::now()+15s;
        RefreshView view;
        do {
            refresh.Tick(RefreshMonotonicMs());
            view=refresh.View();
            if (!view.busy && view.chain_current && view.ledger && view.ledger->InventoryAvailable() && view.ledger->LocksAvailable())
                return view;
            std::this_thread::sleep_for(2ms);
        } while (std::chrono::steady_clock::now()<deadline);
        throw std::runtime_error("usable exact wallet inventory deadline");
    }
};
const app::Document* Pinned(const app::DocumentRegistry& docs)
{
    const auto found=std::ranges::find_if(docs.Documents(),[](const auto& doc){return doc.coin_draft.has_value();});
    return found==docs.Documents().end()?nullptr:&*found;
}
void Cache(Online& online,const std::filesystem::path& metadata)
{
    app::RecoverySession recovery(metadata);
    app::DocumentRegistry docs;
    Check(recovery.Start(),"start private metadata session");
    const auto view=online.ReadWallet(docs,recovery);
    const auto* wallet=docs.Active();
    app::CoinControls controls;
    auto policy=controls.Policy(*wallet,view,recovery);
    app::CoinInventory inventory(view.identity->context,wallet->binding->DaemonWallet());
    Check(policy && inventory.Apply(*view.ledger,view.history_sequence,view.chain_current,&*policy),"acknowledged coin inventory and freeze scope");
    std::set<std::string> selected;
    for (const auto& [id,row]:inventory.Rows()) if (row.eligible==true) selected.insert(id);
    Check(selected.size()==1,"one exact eligible fixture coin");
    const auto& caps=*online.connection.Active();
    const auto saved=RunWalletOperation(online.transport,{WalletAction::ReadPolicy,wallet->binding->DaemonWallet(),caps,
        online.connection.Session(),online.connection.Revision(),wallet->id,recovery.Gate().Epoch()},{}).policy;
    Check(bool(saved),"actual daemon public descriptor inventory");
    const auto change=std::ranges::find_if(saved->branches,[](const auto& branch) {
        return branch.classified && branch.active && branch.descriptor.internal && branch.range
            && branch.descriptor.choice.script==p::ScriptTemplate::NativeSegwit && !branch.descriptor.choice.multisig;
    });
    Check(change!=saved->branches.end(),"independent native SegWit change descriptor");
    const auto result=controls.Pin(docs,wallet->id,selected,&inventory,&view,&recovery,
        p::OfflineChange{change->descriptor.descriptor,change->range->next,view.ledger->Observed()});
    const auto* pin=docs.Find(result.document);
    Check(pin && pin->coin_draft && pin->coin_draft->inputs.front().offline &&
          pin->coin_draft->offline_change,"production exact authenticated cache");
    const auto& input=pin->coin_draft->inputs.front();
    Check(controls.AllowsOfflineDraft(*pin,docs,&view,&recovery),"complete original cache");
    const auto point=app::CoinKey(input.outpoint);
    online.connection.SetOffline(true);
    online.refresh.SetOffline(true,RefreshMonotonicMs());
    Check(online.connection.Offline() && online.refresh.Offline(),"explicit local transport offline gate");
    Check(recovery.Enable(docs),"persist exact public observation before restart");
    std::cout<<"{\"cached\":1,\"checks\":"<<checks<<",\"exactOutpoint\":"<<rpc::Quote(point)
        <<",\"cacheTime\":"<<input.offline->observed_ms<<"}\n";
}
void Verify(const std::filesystem::path& directory,const std::filesystem::path& metadata)
{
    app::RecoverySession recovery(metadata);
    app::DocumentRegistry docs;
    Check(recovery.Start() && recovery.Pending() && recovery.Restore(docs),"restore independently cached public draft offline");
    const auto* draft=Pinned(docs);
    Check(draft && draft->coin_draft && draft->coin_draft->offline_change,"exact local draft restored");
    const auto& cached=*draft->coin_draft;
    const auto& coin=cached.inputs.front();
    Check(coin.offline && coin.value,"cached full prevout proof restored");
    p::OfflineDraft request{p::DisplayNetwork::Regtest,{{coin.outpoint,coin.value,coin.script,coin.offline}},
        {{p::OutputAddress(coin.script,p::DisplayNetwork::Regtest).value(),Money::Satoshis(12345)}},Money::Satoshis(1200),cached.offline_change};
    auto proposed=p::ConstructOfflineDraft(request);
    Check(proposed.document && proposed.document->transaction->fee==Money::Satoshis(1200)
         && proposed.document->transaction->outputs.front().value.sats()==12345,"exact fee, independent change and amount");
    auto incomplete=request;
    incomplete.inputs.front().proof.reset();
    Check(!p::ConstructOfflineDraft(incomplete).document,"missing prevout fails closed");
    incomplete=request;
    incomplete.change.reset();
    Check(!p::ConstructOfflineDraft(incomplete).document,"missing change proof fails closed");
    incomplete=request;
    incomplete.recipients.front().address=p::OutputAddress(coin.script,p::DisplayNetwork::Mainnet).value();
    Check(!p::ConstructOfflineDraft(incomplete).document,"wrong network address fails");
    incomplete=request;
    incomplete.inputs.push_back(incomplete.inputs.front());
    Check(!p::ConstructOfflineDraft(incomplete).document,"duplicate exact pin fails");
    incomplete=request;
    incomplete.inputs.front().value=Money::Satoshis(1);
    Check(!p::ConstructOfflineDraft(incomplete).document,"forged amount fails");
    const auto original=Parsed(directory/"unsigned.psbt");
    {
        std::ifstream destination(directory/"recipient.txt");
        std::string address;
        destination>>address;
        request.recipients={{address,Money::Satoshis(123456)}};
    }
    const auto exact=p::ConstructOfflineDraft(request);
    Check(exact.document && exact.review_evidence,"independent expected payment and wallet review evidence");
    const auto& expected=*exact.review_evidence;
    const auto reviewed=p::InspectOfflineReview(*original,expected);
    Check(reviewed.commitment && reviewed.change_output==1,"local review proves amount, fee and independently derived change");
    const auto equivalent=Parsed(directory/"equivalent-v2.psbt");
    Check(equivalent->psbt_version==2 && p::TransactionCommitment(*equivalent)==reviewed.commitment
          && p::CompareTransactionReview(*original,*equivalent).empty(),
        "equivalent v0 and v2 transaction and policy");
    const auto across_formats=p::MergeOfflineSignatures(*original,*equivalent);
    Check(!across_formats.document && across_formats.review_conflict.empty() && across_formats.error.find("working PSBT version")!=std::string::npos,
        "same commitment across formats retains review and refuses unsupported cross-version merge");
    const auto map_order=Parsed(directory/"reordered-maps.psbt");
    Check(original->wire!=map_order->wire && p::TransactionCommitment(*map_order)==reviewed.commitment
          && p::CompareTransactionReview(*original,*map_order).empty(),
        "opaque metadata and map ordering leave the commitment and review unchanged");
    for (const auto& [fixture,reason]:std::array<std::pair<const char*,const char*>,9>{{
        {"reordered-outputs.psbt","order changed"},
        {"wrong-version.psbt","version changed"},
        {"wrong-locktime.psbt","locktime changed"},
        {"wrong-sequence.psbt","sequence changed"},
        {"wrong-sighash.psbt","sighash commitment changed"},
        {"policy-conflict.psbt","script policy changed"},
        {"wrong-prevout.psbt","previous output proofs conflict"},
        {"prevout-conflict.psbt","previous output proofs conflict"},
        {"modifiable.psbt","modification permissions changed"},
    }}) {
        const auto variant=Parsed(directory/fixture);
        const auto diff=p::CompareTransactionReview(*original,*variant);
        if (diff.find(reason)==std::string::npos) throw std::runtime_error(std::string(fixture)+": "+diff);
        Check(p::MergeOfflineSignatures(*original,*variant).review_conflict==diff,
            "review mutation is rejected with the exact revocation reason");
        Check((p::TransactionCommitment(*variant)!=reviewed.commitment)==
              (diff.find("order")!=std::string::npos || diff.find("version")!=std::string::npos
               || diff.find("locktime")!=std::string::npos || diff.find("sequence")!=std::string::npos),
              "unsigned identity separates transaction body from review policy facts");
    }
    const auto forged=Parsed(directory/"forged-change.psbt");
    Check(p::InspectOfflineReview(*forged,expected).error.find("internal descriptor")!=std::string::npos
          && !p::InspectOfflineReview(*forged,expected).commitment,
        "copied output origins cannot prove a different change script");
    auto unknown_change=expected;
    unknown_change.change.reset();
    Check(!p::InspectOfflineReview(*original,unknown_change).commitment,"no review without independent change proof");
    Check(!p::FinalizeOfflineDraft(*original).document,"unsigned cannot finalize");
    const auto signed_source=Parsed(directory/"signed.psbt");
    const auto merged=p::MergeOfflineSignatures(*original,*signed_source);
    Check(merged.document && merged.satisfied==1 && merged.required==1,"externally signed exact transaction verifies");
    Check(merged.document->wire.find("offline-proprietary-example")!=std::string::npos,"opaque external map retained");
    const auto wrong=Parsed(directory/"wrong-transaction.psbt");
    Check(p::MergeOfflineSignatures(*original,*wrong).review_conflict.find("amount")!=std::string::npos,
        "the amount change rejects the merge and revokes review");
    const auto stale=Parsed(directory/"wrong-signature.psbt");
    const auto unverified=p::MergeOfflineSignatures(*original,*stale);
    Check(!unverified.document && unverified.error.find("Signature does not validate")!=std::string::npos,
        "same unsigned transaction with a signature from another transaction is rejected cryptographically");
    const auto conflict=Parsed(directory/"conflict.psbt");
    const auto opaque_conflict=p::MergeOfflineSignatures(*merged.document,*conflict);
    Check(!opaque_conflict.document && opaque_conflict.review_conflict.empty(),
        "contradictory opaque fields reject merge without changing the review identity");
    const auto final=Parsed(directory/"final.tx");
    const auto check=p::FinalizeOfflineDraft(*merged.document);
    Check(check.document && check.document->wire==final->wire,"deterministic provider assembly matches native export");
    Check(p::TransactionCommitment(*final)==reviewed.commitment,"raw finalized transaction preserves the unsigned commitment");
    std::cout<<"{\"checks\":"<<checks<<",\"noRpc\":true,\"reviewIdentityChecked\":true,\"commitment\":"
             <<rpc::Quote(reviewed.commitment->hex())<<"}\n";
}
void PolicyVector(const std::filesystem::path& path,bool rejected)
{
    std::optional<p::FinalTransaction> reviewed_final;
    const auto base=Parsed(path/"base.psbt");
    const auto signed_psbt=Parsed(path/"signature.psbt");
    const auto merged=p::MergeOfflineSignatures(*base,*signed_psbt);
    if (rejected) {
        Check(!merged.document,"unsupported vector must reject");
    } else {
        Check(!p::FinalizeOfflineDraft(*base).document,"unsigned policy cannot finalize");
        if (!merged.document) throw std::runtime_error(merged.error);
        Check(bool(merged.document),"signed policy merge must pass");
        if (std::filesystem::exists(path/"partial.psbt")) {
            const auto one=Parsed(path/"partial.psbt");
            const auto partial=p::MergeOfflineSignatures(*base,*one);
            Check(partial.document && partial.satisfied==0,"one signature does not satisfy the multisig threshold");
            Check(!p::FinalizeOfflineDraft(*partial.document).document,"partial threshold cannot finalize");
            Check(p::MergeOfflineSignatures(*partial.document,*signed_psbt).satisfied==1,"remaining valid signature satisfies threshold");
        }
        Check(merged.satisfied==base->transaction->inputs.size() && merged.document->psbt_version==base->psbt_version,
            "every exact input policy satisfied in the original PSBT version");
        const auto final=p::FinalizeOfflineDraft(*merged.document);
        if (!final.document) throw std::runtime_error(final.error);
        Check(final.document && final.document->transaction->txid && final.document->transaction->outputs.size()==base->transaction->outputs.size(),
            "locally finalized exact public bytes");
        const auto& actual=*final.document->transaction;
        const auto& expected=*base->transaction;
        Check(actual.version==expected.version && actual.locktime==expected.locktime &&
              actual.inputs.size()==expected.inputs.size(),"unsigned transaction header not substituted");
        for(size_t i=0;i<actual.inputs.size();++i)
            Check(actual.inputs[i].outpoint==expected.inputs[i].outpoint && actual.inputs[i].sequence==expected.inputs[i].sequence,
                "exact input identity and sequence survive finalization");
        for(size_t i=0;i<actual.outputs.size();++i)
            Check(actual.outputs[i].value==expected.outputs[i].value && actual.outputs[i].script==expected.outputs[i].script,
                "exact ordered output survives finalization");
        if (std::filesystem::exists(path/"descriptor.txt")) {
            const auto& input=expected.inputs.front();
            Check(input.previous_value && expected.fee,"known independent previous value and fee");
            const auto descriptor=p::PreviewPublicWallet(p::PublicImportKind::Descriptor,Read(path/"descriptor.txt"),"regtest");
            Check(descriptor.preview && descriptor.preview->descriptors.size()==1,"independent public vector descriptor");
            const p::OfflineReviewEvidence proof{p::DisplayNetwork::Regtest,
                {{input.outpoint,*input.previous_value,descriptor.preview->descriptors.front().descriptor}},
                expected.outputs,*expected.fee,{}};
            const auto approved=p::FinalizeReviewedDraft(*base,*merged.document,proof,{});
            if (!approved.final) throw std::runtime_error(approved.error);
            Check(approved.final->document->wire==final.document->wire && approved.final->fee==*expected.fee,
                "approved final bytes are identical to strict assembly");
            Check(!p::FinalizeReviewedDraft(*base,*base,proof,{}).final,"incomplete input cannot finalize under approval");
            reviewed_final=approved.final;
        }
    }
    std::cout<<"{\"checks\":"<<checks<<",\"expectedRejection\":"<<(rejected?"true":"false");
    if (reviewed_final) {
        const auto& facts=*reviewed_final;
        const auto& tx=*facts.document->transaction;
        std::cout<<",\"final\":{\"txid\":"<<rpc::Quote(tx.txid->hex())<<",\"wtxid\":"<<rpc::Quote(tx.wtxid->hex())
            <<",\"size\":"<<facts.size<<",\"stripped\":"<<facts.stripped_size<<",\"weight\":"<<facts.weight
            <<",\"vsize\":"<<facts.vsize<<",\"wire\":"<<rpc::Quote(p::TransactionHex(facts.document->wire))<<"}";
    }
    std::cout<<"}\n";
}
void CheckSpent(Online& online,const std::filesystem::path& metadata)
{
    app::RecoverySession recovery(metadata);
    app::DocumentRegistry docs;
    Check(recovery.Start() && recovery.Pending() && recovery.Restore(docs),"restore original older pin after controller spend");
    const auto view=online.ReadWallet(docs,recovery);
    const auto* draft=Pinned(docs);
    Check(draft && draft->coin_draft && draft->coin_draft->inputs.front().offline,"older cache remains inspectable");
    app::CoinControls controls;
    Check(!controls.AllowsOfflineDraft(*draft,docs,&view,&recovery),"fresh absence blocks entire stale spent set");
    std::cout<<"{\"checks\":"<<checks<<",\"spentPinRejected\":true}\n";
}
}
int main(int argc,char** argv)
{
    try {
        const std::string_view mode=argc>1?argv[1]:"";
        if (argc==4 && mode=="verify") Verify(argv[2],argv[3]);
        else if (argc==4 && mode=="policy") PolicyVector(argv[2],std::string_view(argv[3])=="reject");
        else if (argc==7 && (mode=="cache" || mode=="spent")) {
            Online online(argv[2],argv[3],argv[4],argv[5]);
            if (mode=="cache") Cache(online,argv[6]); else CheckSpent(online,argv[6]);
        } else return 2;
        return 0;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
