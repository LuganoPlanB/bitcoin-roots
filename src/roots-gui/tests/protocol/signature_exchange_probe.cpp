// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/offline_draft.h"
#include "app/signing_review.h"
#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace p=roots::gui::protocol;
namespace a=roots::gui::app;
using namespace roots::gui;
namespace {
int checks{};
void Check(bool ok,const char* reason)
{
    if (!ok) throw std::runtime_error(reason);
    ++checks;
}
std::string Bytes(const std::filesystem::path& file)
{
    std::ifstream stream(file,std::ios::binary);
    return {std::istreambuf_iterator<char>(stream),{}};
}
p::DocumentResult Read(const std::filesystem::path& file)
{
    return p::ParseTransactionDocument(Bytes(file),p::DisplayNetwork::Regtest);
}
std::shared_ptr<const p::TransactionDocument> Parsed(const std::filesystem::path& file)
{
    const auto result=Read(file);
    if (!result.document) throw std::runtime_error(file.filename().string()+": "+p::DocumentErrorMessage(result));
    return result.document;
}
void Finalization(const std::filesystem::path& folder,const std::shared_ptr<const p::TransactionDocument>& original,
    const std::shared_ptr<const p::TransactionDocument>& partial,const std::shared_ptr<const p::TransactionDocument>& complete,
    const std::shared_ptr<const p::TransactionDocument>& invalid)
{
    const auto& tx=*original->transaction;
    p::OfflineReviewEvidence proof{p::DisplayNetwork::Regtest,{},tx.outputs,Money::Satoshis(500),{}};
    const auto public_policy=p::PreviewPublicWallet(p::PublicImportKind::Descriptor,Bytes(folder/"descriptor.txt"),"regtest");
    Check(public_policy.preview && public_policy.preview->descriptors.size()==1,"independent public fixture descriptor");
    const auto& descriptor=public_policy.preview->descriptors.front().descriptor;
    for (const auto& input:tx.inputs) proof.inputs.push_back({input.outpoint,*input.previous_value,descriptor});
    const auto original_digest=*a::SigningDigest(original->source);
    const a::WalletBinding wallet{*BackendSessionId::Make("offline-test"),*WalletId::Make("offline-multisig"),
        {*Hash256::Parse("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")},{}};
    const a::SigningBasis basis{1,2,3,4,5,{6},{7},{8},true,wallet,original,proof,{},original_digest,{},{},{1,1},false};
    a::SigningReviews reviews;
    Check(!reviews.Finalize(basis,complete).artifact && reviews.Record(basis)
        && !reviews.Finalize(basis,complete).artifact,"complete material and a recorded review alone provide no approval");
    Check(reviews.Approve(basis),"separate approval");
    Check(!reviews.Finalize(basis,partial).artifact && !reviews.Finalize(basis,invalid).artifact,
        "partial and invalid signatures cannot produce final bytes");
    Check(!reviews.Finalize(basis,Parsed(folder/"wrong.psbt")).artifact,"wrong transaction cannot produce final bytes");
    auto wrong_proof=proof;
    ++wrong_proof.inputs.front().point.index;
    Check(!p::FinalizeReviewedDraft(*original,*complete,wrong_proof,{}).final,"independent outpoint proof cannot be substituted");
    const auto other=folder.parent_path()/(original->psbt_version==0 ? "v2" : "v0")/"complete.psbt";
    Check(!reviews.Finalize(basis,Parsed(other)).artifact,"equivalent cross-version collection still requires exact approved format");
    auto stale=basis;
    ++stale.session;
    Check(!reviews.Finalize(stale,complete).artifact,"stale session is fenced");
    stale=basis;
    ++stale.profile;
    Check(!reviews.Finalize(stale,complete).artifact,"stale node profile is fenced");
    stale=basis;
    ++stale.epoch.value;
    Check(!reviews.Finalize(stale,complete).artifact,"stale lock epoch is fenced");
    stale=basis;
    ++stale.document_revision.value;
    Check(!reviews.Finalize(stale,complete).artifact,"stale document revision is fenced");
    stale=basis;
    stale.proof.network=p::DisplayNetwork::Mainnet;
    Check(!reviews.Finalize(stale,complete).artifact,"changed chain is fenced");
    const auto final=reviews.Finalize(basis,complete);
    if (!final.artifact) throw std::runtime_error(final.error);
    const auto independent=Bytes(folder/"expected.tx");
    Check(final.artifact->final.document->wire==independent
        && final.artifact->final.fee==Money::Satoshis(500),"exact assembly agrees with independent legacy multisig serialization");
    const auto& artifact=*final.artifact;
    Check(artifact.final.size==independent.size() && artifact.final.stripped_size==independent.size()
        && artifact.final.weight==independent.size()*4 && artifact.final.vsize==independent.size()
        && artifact.final.document->transaction->txid==artifact.final.document->transaction->wtxid,
        "legacy measurements are from exact signed scriptSigs");
    Check(artifact.approval.reviewed.content==original && artifact.collected==complete
        && artifact.collected_source==a::SigningDigest(complete->source) && artifact.final_source==a::SigningDigest(independent),
        "source, collection and transaction remain independently bound");
    Check(reviews.Revoke(basis.document,"test context retired") && !reviews.Finalize(basis,complete).artifact,
        "retired approval cannot finalize again");
    Check(reviews.Record(basis) && reviews.Approve(basis)
        && reviews.Authorize(basis)->generation!=artifact.approval.generation,
        "fresh approval of identical bytes has a distinct identity");
}
int Verify(const std::filesystem::path& folder,uint32_t version)
{
    checks=0;
    const auto original=Parsed(folder/"original.psbt");
    const auto before=original->source;
    Check(original->psbt_version==version,"exact PSBT version");
    const auto bare=p::InspectSignatures(*original);
    Check(!bare.Complete() && bare.error.empty() && bare.inputs.size()==2,"unsigned two-input progress");
    for (const auto& input:bare.inputs)
        Check(input.unknown.empty() && input.threshold==2 && input.signers.size()==3 && input.Verified()==0,"authentic unsigned 2-of-3 policy");
    const auto external=Parsed(folder/"partial.psbt");
    const auto partial=p::MergeOfflineSignatures(*original,*external);
    if (!partial.document) throw std::runtime_error("First merge: "+partial.error);
    Check(partial.document && partial.satisfied==1 && partial.required==2,"first merge cannot complete the second input");
    const auto progress=p::InspectSignatures(*partial.document);
    Check(!progress.Complete() && progress.error.empty(),"partial despite two unique signers");
    Check(progress.inputs[0].Satisfied() && progress.inputs[0].Verified()==2,"first exact input has its two signatures");
    Check(!progress.inputs[1].Satisfied() && progress.inputs[1].Verified()==1,"second exact input needs another key");
    Check(progress.inputs[0].signers.size()==3 && progress.inputs[1].signers.size()==3
        && progress.inputs[0].signers[2].key==progress.inputs[1].signers[2].key,"keys came from the committed input scripts");
    const auto yes=Parsed(folder/"complete.psbt");
    const auto complete=p::MergeOfflineSignatures(*partial.document,*yes);
    Check(complete.document && complete.satisfied==2 && complete.required==2
        && p::InspectSignatures(*complete.document).Complete(),"fresh key material satisfies every input");
    Check(complete.document->psbt_version==version
        && p::TransactionCommitment(*complete.document)==p::TransactionCommitment(*original)
        && complete.document->wire.find("original input A")!=std::string::npos
        && complete.document->wire.find("original output B")!=std::string::npos
        && complete.document->wire.find("external signer opaque material")!=std::string::npos,"same transaction and opaque maps survive");
    const auto no_new=p::MergeOfflineSignatures(*partial.document,*external);
    Check(!no_new.document && no_new.error.find("no new verified signature")!=std::string::npos,"signature replay rejected");
    const auto invalid=Parsed(folder/"invalid.psbt");
    const auto unverified=p::InspectSignatures(*invalid);
    Check(!unverified.Complete() && unverified.inputs[0].Satisfied() && !unverified.inputs[1].unknown.empty()
        && unverified.inputs[1].Verified()==0,"a signature copied across inputs never receives verified status");
    const auto rejected=p::MergeOfflineSignatures(*original,*invalid);
    Check(!rejected.document && rejected.review_conflict.empty(),"cryptographic failure is atomic without declaring a material change");
    const auto ambiguous=p::MergeOfflineSignatures(*partial.document,*Parsed(folder/"conflicting.psbt"));
    Check(!ambiguous.document && ambiguous.review_conflict.empty() && ambiguous.error.find("Conflicting PSBT")!=std::string::npos,"opaque record collision rejected");
    const auto wrong=p::MergeOfflineSignatures(*partial.document,*Parsed(folder/"wrong.psbt"));
    Check(!wrong.document && wrong.review_conflict.find("amount")!=std::string::npos,"changed recipient requires review");
    Check(Read(folder/"duplicate.psbt").error==p::DocumentError::Duplicate,"duplicate serialized map key rejected");
    const auto unknown=p::InspectSignatures(*Parsed(folder/"unknown.psbt"));
    Check(!unknown.Complete() && !unknown.inputs[1].unknown.empty() && unknown.inputs[1].Verified()==0,"missing full proof is Unknown");
    Finalization(folder,original,partial.document,complete.document,invalid);
    Check(original->source==before,"exact original bytes retained");
    return checks;
}
}

int main(int argc,char** argv)
{
    if (argc!=2) return 2;
    try {
        for (const auto family:{AF_INET,AF_INET6}) {
            const int fd=socket(family,SOCK_STREAM,0);
            const bool denied=fd<0 && errno==EPERM;
            if (fd>=0) close(fd);
            Check(denied,"network fence required");
        }
        const std::filesystem::path directory=argv[1];
        const auto v0=Verify(directory/"v0",0);
        const auto v2=Verify(directory/"v2",2);
        const auto first=Parsed(directory/"v0/partial.psbt");
        const auto other=Parsed(directory/"v2/complete.psbt");
        Check(p::CompareTransactionReview(*first,*other).empty(),"cross-version unsigned identity comparison");
        const auto refused=p::MergeOfflineSignatures(*first,*other);
        Check(!refused.document && refused.review_conflict.empty() && refused.error.find("version")!=std::string::npos,
            "no silent or lossy cross-version merge");
        std::cout<<"{\"v0\":"<<v0<<",\"v2\":"<<v2<<",\"crossVersionChecks\":2,\"networkDenied\":true}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
