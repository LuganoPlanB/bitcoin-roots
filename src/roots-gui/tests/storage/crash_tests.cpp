// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/qr.h"
#include "adapters/storage/protected_file.h"
#include "adapters/storage/schema.h"
#include "app/broadcast.h"
#include "app/daemon_signer.h"
#include "app/history_workspace.h"
#include "app/recovery.h"
#include "services/camera.h"
#include "services/download_verification.h"
#include "services/transaction_documents.h"
#include "ui/shell/workspace.h"
#include <sqlite3.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <semaphore>
#include <stdexcept>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace roots::gui;
namespace a = app;
namespace s = storage;
namespace p = protocol;
namespace sh = ui::shell;
namespace {
constexpr char GENESIS[] = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
constexpr char WALLET[] = "crash-fixture";
constexpr char INITIAL[] = "Committed public draft";
constexpr char CHANGED[] = "Revised public draft";
constexpr char DISPLAY[] = "Restored local wallet";
unsigned checks{};

void Check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
    ++checks;
}

template<typename T>
T Take(s::Result<T> result)
{
    if (!result) throw std::runtime_error(std::string("storage result: ")+s::ErrorName(result.error));
    return std::move(*result.value);
}

std::string Bytes(const fs::path& file)
{
    std::ifstream input(file, std::ios::binary);
    Check(bool(input), "fixture file exists");
    return {std::istreambuf_iterator<char>(input), {}};
}

void Write(const fs::path& file, std::string_view bytes)
{
    std::ofstream target(file, std::ios::binary | std::ios::trunc);
    target.write(bytes.data(), bytes.size());
    Check(bool(target), "write owned fixture");
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write);
}

template<typename Ready>
void Await(Ready ready)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(9);
    while (!ready()) {
        Check(std::chrono::steady_clock::now() < deadline, "current fixture operation settled before deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

NetworkIdentity Network() { return {*Hash256::Parse(GENESIS)}; }
s::Scope Scope() { return {"persisted-fixture", Network(), WALLET}; }

struct Raw {
    sqlite3* db{};
    explicit Raw(const fs::path& file)
    {
        if (!fs::exists(file)) Write(file, "");
        Check(sqlite3_open_v2(file.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr)==SQLITE_OK, "open owned raw schema fixture");
    }
    ~Raw() { sqlite3_close_v2(db); }
    void Exec(std::string_view sql)
    {
        Check(sqlite3_exec(db,std::string(sql).c_str(),nullptr,nullptr,nullptr)==SQLITE_OK, "fixture SQL executes");
    }
};

void Legacy(const fs::path& path)
{
    Raw raw(path);
    raw.Exec("BEGIN IMMEDIATE");
    for (const auto& sql : s::schema::V1) raw.Exec(sql);
    raw.Exec("PRAGMA application_id="+std::to_string(s::schema::ApplicationId));
    raw.Exec("PRAGMA user_version=1");
    raw.Exec("INSERT INTO scopes VALUES(1,'persisted-fixture','"+std::string(GENESIS)+"','crash-fixture',7,'Legacy display')");
    raw.Exec("INSERT INTO labels VALUES(1,0,'fixture-public-address','Legacy label')");
    raw.Exec("COMMIT");
}

void CheckLegacy(s::MetadataStore& store)
{
    const auto saved=Take(store.Load(Scope()));
    Check(saved.revision==7 && saved.display_name=="Legacy display"
        && saved.labels==std::vector<s::Label>{{{s::TargetKind::Address,"fixture-public-address"},"Legacy label"}},
        "legacy revision and exact metadata survive migration");
}

int ProtectedSchema(const fs::path& root)
{
    const auto file=Take(s::ProtectedFile::Open(root/"metadata.sqlite.protected",Bytes(root/"archive-password"),
                                              s::ProtectedFile::MaxBytes,true));
    const auto plaintext=Take(file->Read());
    sqlite3* db{};
    Check(sqlite3_open(":memory:",&db)==SQLITE_OK,"open readonly protected schema inspection");
    std::unique_ptr<sqlite3,decltype(&sqlite3_close_v2)> owned(db,sqlite3_close_v2);
    auto* contents=const_cast<unsigned char*>(plaintext.bytes.data());
    Check(sqlite3_deserialize(db,"main",contents,plaintext.bytes.size(),plaintext.bytes.size(),
          SQLITE_DESERIALIZE_READONLY)==SQLITE_OK,"inspect decrypted schema without publication");
    sqlite3_stmt* statement{};
    Check(sqlite3_prepare_v2(db,"PRAGMA user_version",-1,&statement,nullptr)==SQLITE_OK,"read authenticated schema version");
    std::unique_ptr<sqlite3_stmt,decltype(&sqlite3_finalize)> query(statement,sqlite3_finalize);
    Check(sqlite3_step(statement)==SQLITE_ROW,"authenticated version exists");
    return sqlite3_column_int(statement,0);
}

std::shared_ptr<const p::TransactionDocument> Parse(std::string_view source)
{
    auto parsed=p::ParseTransactionDocument(source,p::DisplayNetwork::Regtest);
    Check(parsed.document && parsed.document->transaction, "fixture transaction decoded");
    return parsed.document;
}

void Boundary(const fs::path& root, const char* label)
{
    Write(root/"boundary",label);
    // The controller verifies this marker, then sends SIGKILL. This point is
    // outside production code and has no mutable recovery effects.
    ::pause();
    throw std::runtime_error("owned interruption was not delivered");
}

void Paint(const fs::path& source)
{
    const auto qr=p::EncodeQr(Bytes(source));
    Check(qr.has_value(),"encode actual QR matrix");
    constexpr int quiet=4,pitch=4;
    const int side=(qr->size+quiet*2)*pitch;
    std::cout<<"P5\n"<<side<<' '<<side<<"\n255\n";
    for (int y=0;y<side;++y) for (int x=0;x<side;++x) {
        const int i=x/pitch-quiet,j=y/pitch-quiet;
        const bool black=i>=0 && i<qr->size && j>=0 && j<qr->size && qr->modules[size_t(j)*qr->size+i];
        std::cout.put(black?'\0':char(255));
    }
}

void PublicCrash(const fs::path& root,std::string_view mode)
{
    a::RecoverySession recovery(root/"metadata.sqlite");
    Check(recovery.Start() && !recovery.Pending(),"initialize owned GUI metadata");
    a::DocumentRegistry docs;
    const auto id=docs.OpenTransaction("Public draft");
    docs.Find(id)->transaction.note.SetDraft(INITIAL);
    Check(recovery.Enable(docs),"durable baseline draft");
    if (mode.starts_with("draft-")) {
        docs.Find(id)->transaction.note.SetDraft(CHANGED);
        if (mode=="draft-after-save") Check(recovery.Checkpoint(docs),"acknowledged revision after save");
        Boundary(root,mode=="draft-after-save"?"saved":"edited");
    }
    TransactionDocuments reader;
    const LocalDocumentContext context{1,id,docs.Find(id)->draft_revision,recovery.Gate().Epoch()};
    std::optional<LocalDocumentReply> reply;
    Check(reader.ReadQrImage(context,(root/"source.png").string()),"open actual local PNG QR");
    Await([&]{reply=reader.Poll(context);return !reader.Busy();});
    Check(reply && reply->qr_text && reply->file_error.empty(),"independent local QR decode acknowledged");
    Check(reader.ReadText(context,std::move(*reply->qr_text),p::DisplayNetwork::Regtest),"parse decoded transaction under same context");
    Await([&]{reply=reader.Poll(context);return !reader.Busy();});
    Check(reply && reply->parsed.document,"actual transaction import validation");
    Check(reply->parsed.document->source==Bytes(root/"qr-source.txt")
        && reply->parsed.document->wire==Parse(Bytes(root/"original.psbt"))->wire,
        "QR preserves its exact source representation and decoded opaque PSBT");
    if (mode=="qr-after-save") {
        Check(docs.OpenImportedDocument({reply->parsed.document,p::DisplayNetwork::Regtest,a::DocumentRepresentation::Source})==2,
              "open verified QR document");
        Check(recovery.Checkpoint(docs),"durable QR import checkpoint");
    }
    Boundary(root,mode=="qr-after-save"?"qr-saved":"qr-decoded");
}

void RecoverPublic(const fs::path& root,std::string_view mode)
{
    a::RecoverySession recovery(root/"metadata.sqlite");
    a::DocumentRegistry docs;
    Check(recovery.Start() && recovery.Pending() && docs.Documents().empty(),"restart prompts without adopting authority");
    Check(recovery.Restore(docs),"user explicitly restores last committed public revision");
    const auto* draft=docs.Find(1);
    Check(draft && draft->transaction.note.draft()==(mode=="draft-after-save"?CHANGED:INITIAL)
        && draft->observation==a::ObservationState::Offline,"exact public draft revision");
    const bool has_qr=mode=="qr-after-save";
    Check(docs.Documents().size()==(has_qr?2:1),"only durably committed QR import is reopened");
    if (has_qr) Check(docs.Find(2)->imported->parsed->source==Bytes(root/"qr-source.txt")
        && docs.Find(2)->imported->parsed->wire==Parse(Bytes(root/"original.psbt"))->wire,
        "recovered QR source mode and opaque PSBT bytes match");
}

struct OnlineFixture {
    ServerCandidate profile;
    rpc::CookieCredentials credentials;
    rpc::Transport transport{credentials};
    ConnectionService connection{[this](const ServerCandidate& candidate,std::stop_token) {
        BackendCapabilities caps;
        caps.candidate=candidate;caps.genesis=GENESIS;caps.chain="regtest";
        caps.identity_verified=caps.authenticated=true;
        caps.wallet=WalletAvailability::Available;caps.descriptors=true;caps.private_keys=true;caps.external_signer=false;
        caps.methods["walletprocesspsbt"]={true,true,true,false,{"psbt","sign","finalize","bip32derivs","sighashtype"}};
        caps.methods["testmempoolaccept"]={true,true,false,false,{"rawtxs","maxfeerate"}};
        caps.methods["sendrawtransaction"]={true,true,false,false,{"hexstring","maxfeerate","maxburnamount"}};
        caps.methods["listwallets"]={true};
        caps.methods["listwalletdir"]={true};
        caps.methods["loadwallet"]={true,true,false,false,{"filename"}};
        caps.methods["getwalletinfo"]={true};
        return caps;
    }};

    OnlineFixture(const fs::path& root,unsigned port)
    {
        profile.connection.port=uint16_t(port);
        profile.connection.credential.reference=(root/"fixture-cookie").string();
        profile.expected_genesis=GENESIS;
        profile.wallet=WALLET;
        Check(connection.SetDraft(profile) && connection.Test(),"test declared synthetic connection state");
        Await([&]{connection.Poll();return !connection.Busy();});
        Check(connection.Apply(),"apply exact synthetic connection for real loopback requests");
    }
};

struct ApprovalFixture {
    std::shared_ptr<const p::TransactionDocument> original, collected;
    a::SigningBasis basis;
    a::SigningReviews reviews;

    ApprovalFixture(const fs::path& root,const OnlineFixture& online,LockEpoch epoch)
        : original(Parse(Bytes(root/"original.psbt"))), collected(Parse(Bytes(root/"collected.psbt"))),
          basis(Make(root,online,epoch,original))
    {}

    static a::SigningBasis Make(const fs::path& root,const OnlineFixture& online,LockEpoch epoch,
                                const std::shared_ptr<const p::TransactionDocument>& original)
    {
        const auto& tx=*original->transaction;
        Check(tx.inputs.size()==1 && tx.inputs[0].previous_value && tx.fee,"fixture independent prevout and fee");
        const auto preview=p::PreviewPublicWallet(p::PublicImportKind::Descriptor,Bytes(root/"descriptor"),"regtest");
        Check(preview.preview && preview.preview->descriptors.size()==1,"fixture derives independent public descriptor");
        const auto wallet=a::VerifiedWalletBinding(*online.connection.Active());
        Check(wallet.has_value(),"exact wallet and network binding");
        const p::OfflineReviewEvidence proof{p::DisplayNetwork::Regtest,
            {{tx.inputs[0].outpoint,*tx.inputs[0].previous_value,preview.preview->descriptors[0].descriptor}},tx.outputs,*tx.fee,{}};
        Check(p::InspectOfflineReview(*original,proof).commitment.has_value(),"funded review proof");
        const auto digest=*a::SigningDigest("independent public crash fixture");
        return {2,2,1,online.connection.Session(),online.connection.Revision(),{1},{1},epoch,false,
                *wallet,original,proof,{},digest,{},digest,{1},false};
    }

    void Approve()
    {
        Check(reviews.Record(basis) && reviews.Approve(basis),"separate review and explicit approval");
    }
};

a::ApprovedFinalTransaction Finalize(ApprovalFixture& fixture)
{
    auto final=fixture.reviews.Finalize(fixture.basis,fixture.collected);
    Check(final.artifact.has_value(),"local signer proof authorizes exact finalization");
    return std::move(*final.artifact);
}

void Facts(const fs::path& root)
{
    OnlineFixture online(root,1);
    // This mode derives public expectations without a transport call.
    ApprovalFixture fixture(root,online,{0});
    fixture.Approve();
    const auto artifact=Finalize(fixture);
    const auto& tx=*artifact.final.document->transaction;
    std::cout<<"{\"txid\":"<<rpc::Quote(tx.txid->hex())<<",\"wtxid\":"<<rpc::Quote(tx.wtxid->hex())
             <<",\"wireDigest\":"<<rpc::Quote(artifact.final_source.hex())<<",\"fee\":"<<rpc::Quote(artifact.final.fee.btc())<<"}\n";
}

void TransactionCrash(const fs::path& root,std::string_view mode,unsigned port)
{
    OnlineFixture online(root,port);
    a::RecoverySession recovery(root/"metadata.sqlite");
    Check(recovery.Start(),"start owned transaction workspace");
    ApprovalFixture fixture(root,online,recovery.Gate().Epoch());
    a::DocumentRegistry docs;
    if (mode.starts_with("reopen-")) {
        Check(recovery.Pending() && recovery.Restore(docs),"restart only restores public workspace by choice");
        const auto* transaction=docs.Find(2);
        Check(transaction && transaction->observation==a::ObservationState::Offline && transaction->imported
            && transaction->imported->parsed->source==fixture.original->source,"exact restored original opaque PSBT");
        a::DaemonSigner signer(online.transport,online.connection);
        Check(!signer.Start(fixture.reviews,fixture.basis,a::DaemonSignAction::Sign,""),"recovered draft has no signing admission");
        Check(!fixture.reviews.Finalize(fixture.basis,fixture.collected).artifact,"recovered collection has no finalization authority");
        if (mode=="reopen-submission") {
            const auto txid=*Hash256::Parse(Bytes(root/"expected-txid"));
            const auto saved=Take(recovery.LoadBroadcastAttempt(Network(),txid));
            Check(saved && saved->outcome==s::BroadcastOutcome::Pending && saved->serial==1
                && saved->identity.wire_digest==*Hash256::Parse(Bytes(root/"expected-digest")),"exact durable pending journal before lost reply");
            a::Broadcaster broadcaster(online.transport,online.connection);
            Check(!broadcaster.Can(a::BroadcastAction::Submit,nullptr,nullptr,&recovery).available,
                  "restart grants no send or rebroadcast authority");
        }
        return;
    }
    Check(docs.OpenWallet("Signing fixture",fixture.basis.wallet,true)==1,"fixture wallet");
    Check(docs.OpenImportedDocument({fixture.original,p::DisplayNetwork::Regtest,a::DocumentRepresentation::Source})==2,
          "retain exact original transaction source");
    Check(recovery.Enable(docs),"commit original before requested transaction operation");
    fixture.Approve();
    if (mode=="sign-pending") {
        a::DaemonSigner signer(online.transport,online.connection);
        Check(signer.Start(fixture.reviews,fixture.basis,a::DaemonSignAction::Sign,""),"explicit current signing starts");
        Boundary(root,"sign-dispatched");
    }
    const auto artifact=Finalize(fixture);
    if (mode=="finalized-before-save") Boundary(root,"finalized-local");
    a::Broadcaster broadcaster(online.transport,online.connection);
    Check(broadcaster.Start(a::BroadcastAction::Check,fixture.reviews,fixture.basis,artifact,recovery).accepted,
          "explicit node policy check");
    const auto poll=[&]{broadcaster.Poll(fixture.reviews,fixture.reviews.Authorize(fixture.basis),&artifact,&recovery);};
    Await([&]{poll();return !broadcaster.Busy();});
    Check(broadcaster.View().stage==a::BroadcastStage::Ready,"exact bounded policy response");
    Check(broadcaster.Start(a::BroadcastAction::Submit,fixture.reviews,fixture.basis,artifact,recovery).accepted,
          "distinct explicit Submit");
    Await([&]{poll();return broadcaster.View().stage==a::BroadcastStage::Sending;});
    Boundary(root,"durable-submission");
}

void ArchiveContracts(const fs::path& root)
{
    const auto password=Bytes(root/"archive-password");
    const auto path=root/"wallet.rmeta";
    const auto source_path=root/"source.sqlite";
    const auto dest_path=root/"metadata.sqlite";
    const auto scope=Scope();
    s::Metadata source;
    source.display_name=DISPLAY;
    source.labels={{{s::TargetKind::Address,"fixture-address"},"Restored receiving label"},
                   {{s::TargetKind::Transaction,std::string(64,'a')},"Restored transaction label"}};
    source.freezes={{s::TargetKind::Output,std::string(64,'b')+":2"}};
    source.drafts={{"private-to-source-store",{1},"Do not copy source drafts",{}}};
    {
        auto store=Take(s::MetadataStore::Open(source_path));
        Check(Take(store->Save(scope,source))==1,"seed source metadata");
        s::Profile profile;
        profile.id="source-profile";
        profile.candidate.expected_genesis=GENESIS;
        profile.candidate.connection.credential={Authentication::SecureStore,"fixture-reference-not-for-backup"};
        Check(Take(store->SaveProfile(profile))==1,"source profile is separate");
    }
    {
        a::RecoverySession recovery(source_path);
        a::DocumentRegistry docs;
        Check(recovery.Start() && recovery.Protect(docs,password),"protected metadata source");
        Check(bool(recovery.ExportWalletMetadata(scope,DISPLAY,path,password)),"export independent authenticated metadata archive");
        auto encrypted=Bytes(path);
        Check(encrypted.find(DISPLAY)==std::string::npos && encrypted.find("Restored receiving label")==std::string::npos,
              "stored file keeps metadata private");
        Check(recovery.ExportWalletMetadata(scope,DISPLAY,path,password).error==s::Error::Conflict && Bytes(path)==encrypted,
              "refuse any backup overwrite");
    }
    const auto archive=Take(s::ReadWalletMetadataArchive(path,password));
    Check(archive==s::WalletMetadataArchive{scope,DISPLAY,source.labels,source.freezes},"only exact scope and allowed local metadata round trip");

    s::Metadata initial;
    initial.display_name="Destination display";
    initial.drafts={{"destination-draft",{3},"Independent destination draft",{}}};
    {
        auto store=Take(s::MetadataStore::Open(dest_path));
        Check(Take(store->Save(scope,initial))==1,"seed distinct destination metadata");
        Check(Take(store->Save({scope.backend,scope.network,"different-wallet"},initial))==1,"isolate a separate wallet");
    }
    a::RecoverySession destination(dest_path);
    Check(destination.Start(),"read current destination");
    auto wrong=scope;wrong.daemon_wallet="different-wallet";
    Check(destination.InspectWalletMetadata(wrong,path,password).error==s::Error::Conflict,"wrong wallet refused");
    wrong=scope;wrong.backend="different-origin";
    Check(destination.InspectWalletMetadata(wrong,path,password).error==s::Error::Conflict,"wrong backend refused");
    wrong=scope;wrong.network={*Hash256::Parse(std::string(64,'3'))};
    Check(destination.InspectWalletMetadata(wrong,path,password).error==s::Error::Conflict,"wrong network refused");
    Check(destination.InspectWalletMetadata(scope,path,"independent wrong password").error==s::Error::Authentication,"wrong archive password");
    auto tampered=Bytes(path);
    tampered.back()^=1;
    Write(root/"tampered.rmeta",tampered);
    Check(destination.InspectWalletMetadata(scope,root/"tampered.rmeta",password).error==s::Error::Authentication,"corruption fails closed");
    Write(root/"oversized.rmeta","");
    fs::resize_file(root/"oversized.rmeta",s::WALLET_METADATA_ARCHIVE_MAX+4097);
    Check(destination.InspectWalletMetadata(scope,root/"oversized.rmeta",password).error==s::Error::Resource,
          "oversized input fails before authentication");
    {
        auto protected_file=Take(s::ProtectedFile::Open(root/"foreign.rmeta",password));
        const std::string_view foreign="{\"account\":\"foreign schema\"}";
        Take(protected_file->Save({reinterpret_cast<const unsigned char*>(foreign.data()),foreign.size()}));
        Check(destination.InspectWalletMetadata(scope,root/"foreign.rmeta",password).error==s::Error::Corrupt,
              "authenticated foreign payload never becomes wallet metadata");
    }
    const auto staged=Take(destination.InspectWalletMetadata(scope,path,password));
    Check(Take(destination.HistoryMetadata(scope)).revision==1 && staged.destination_revision==1,"inspection writes nothing");
    auto retired=staged;++retired.epoch.value;
    Check(destination.ApplyWalletMetadata(scope,retired).error==s::Error::Authentication,"different epoch cannot apply staged metadata");
    Check(Take(destination.SaveHistoryLabel(scope,*Hash256::Parse(std::string(64,'a')),"New destination label",1))==2,"intervening metadata revision");
    Check(destination.ApplyWalletMetadata(scope,staged).error==s::Error::Conflict,"stale restore refuses to overwrite user work");
    const auto current=Take(destination.InspectWalletMetadata(scope,path,password));
    Check(Take(destination.ApplyWalletMetadata(scope,current))==3,"explicit restore commits with exact CAS");
    const auto restored=Take(destination.HistoryMetadata(scope));
    Check(restored.display_name==DISPLAY && restored.labels==source.labels && restored.freezes==source.freezes
        && restored.drafts==initial.drafts,"restore exactly scoped local fields and preserve destination drafts");
    Check(Take(destination.HistoryMetadata({scope.backend,scope.network,"different-wallet"})).display_name==initial.display_name,
          "other wallet unchanged");
    Check(!Take(destination.HistoryMetadata(scope)).labels.empty(),"durable restored scope remains readable");

    const auto corrupt=root/"corrupt.sqlite";
    Write(corrupt,"retained corrupt metadata");
    a::RecoverySession damaged(corrupt);
    Check(!damaged.Start() && Bytes(corrupt)=="retained corrupt metadata","corrupt store fails without silent reset");
    Check(!damaged.InspectWalletMetadata(scope,path,password),"damaged store cannot be overwritten by archive");
    Check(Bytes(corrupt)=="retained corrupt metadata","failed restore leaves corrupt source available for recovery");
    {
        const auto base=root/"empty.sqlite";
        const auto encrypted=root/"empty.sqlite.protected";
        auto empty=Take(s::ProtectedFile::Open(encrypted,password));
        Take(empty->Save({}));
        const auto saved=Bytes(encrypted);
        Check(s::MetadataStore::OpenProtected(base,password).error==s::Error::Corrupt
              && Bytes(encrypted)==saved && !fs::exists(base),"authenticated empty database fails without resetting");
    }

    const auto readonly=root/"readonly";
    fs::create_directory(readonly);
    fs::permissions(readonly,fs::perms::owner_all);
    const auto readonly_store=readonly/"metadata.sqlite";
    {
        auto store=Take(s::MetadataStore::Open(readonly_store));
        Check(Take(store->Save(scope,initial))==1,"seed readonly store before switching permissions");
    }
    const auto saved_bytes=Bytes(readonly_store);
    fs::permissions(readonly,fs::perms::owner_read | fs::perms::owner_exec);
    fs::permissions(readonly_store,fs::perms::owner_read);
    try {
        a::RecoverySession denied(readonly_store);
        const bool opened=denied.Start();
        const auto preview=opened?denied.InspectWalletMetadata(scope,path,password):s::Result<a::MetadataRestore>{};
        Check(!preview || !denied.ApplyWalletMetadata(scope,*preview.value),"readonly store refuses import writes");
        Check(Bytes(readonly_store)==saved_bytes,"readonly store remains byte-identical");
    } catch (...) {
        fs::permissions(readonly,fs::perms::owner_all);
        throw;
    }
    fs::permissions(readonly,fs::perms::owner_all);
}

void Continue(const fs::path& root, std::string_view stage)
{
    Write(root/"boundary",stage);
    std::string received;
    Check(bool(std::getline(std::cin,received)) && received==stage,"owned controller released exact barrier");
}

struct SigningHost final:a::Requests {
    ConnectionService& connection;
    a::DaemonSigner& signer;
    SigningHost(ConnectionService& connection,a::DaemonSigner& signer) : connection(connection),signer(signer) {}
    ConnectionService* Connection() override{return &connection;}
    a::DaemonSigner* Signer() override{return &signer;}
    a::Availability CanSubmit(const a::Request&) const override{return {false,{}};}
    a::Receipt Submit(const a::Request&) override{return {false,{}};}
};

void SignerRace(const fs::path& root,std::string_view transition,unsigned port)
{
    OnlineFixture online(root,port);
    a::RecoverySession recovery(root/"metadata.sqlite");
    sh::State state;
    state.recovery=&recovery;
    auto& docs=state.documents;
    const auto password=Bytes(root/"archive-password");
    Check(recovery.Start(),"open owned workspace");
    if (transition=="lock") Check(recovery.Protect(docs,password),"initialize protected workspace");
    ApprovalFixture fixture(root,online,recovery.Gate().Epoch());
    Check(docs.OpenWallet("Signing fixture",fixture.basis.wallet,true)==1,"open exact signing wallet");
    Check(docs.OpenImportedDocument({fixture.original,p::DisplayNetwork::Regtest,a::DocumentRepresentation::Source})==2,
          "open invariant source PSBT");
    docs.Find(2)->transaction.note.SetDraft(INITIAL);
    Check(recovery.Enable(docs),"save workspace before signing");
    fixture.Approve();
    state.signing_reviews=std::move(fixture.reviews);
    state.signing_views[2].open=true;
    state.daemon_operation=2;
    const auto original_approval=*state.signing_reviews.Authorize(fixture.basis);
    a::DaemonSigner signer(online.transport,online.connection);
    SigningHost host(online.connection,signer);
    Check(signer.Start(state.signing_reviews,fixture.basis,a::DaemonSignAction::Sign,""),"dispatch explicit signing");
    Continue(root,"signing-dispatched");
    if (transition=="shutdown") {
        sh::RetireDaemonSigning(state,host);
        return;
    }
    if (transition=="lock") {
        Check(sh::RequestPrivacyLock(state,host) && docs.Documents().empty() && recovery.Gate().Epoch()!=fixture.basis.epoch,
              "conceal documents and retire epoch while signer reply is held");
        online.connection.SetPrivacyLocked(true);
    } else if (transition=="clear") {
        const auto revision=docs.Find(2)->draft_revision;
        Check(recovery.Clear(docs,2) && docs.Find(2)->draft_revision!=revision && docs.Find(2)->transaction.note.draft().empty(),
              "acknowledged Clear retires the exact draft revision during signing");
    } else if (transition=="review") {
        Check(sh::CloseSigningReview(state,host,2) && !state.signing_views.at(2).open,"actual Back to inspection action");
        // Reopening before delivery must not reinstate the cancelled operation.
        state.signing_views.at(2).open=true;
    } else if (transition=="close") {
        const uint64_t id=2;
        Check(recovery.Close(docs,{&id,1}) && !docs.Find(id),"actual acknowledged document close");
    } else throw std::runtime_error("unknown signing transition");
    Continue(root,"view-retired");
    if (transition=="review") {
        // Even an otherwise unchanged approval cannot admit this operation
        // once the user leaves its review, including a rapid reopen.
        Await([&]{
            Check(!signer.Poll(&original_approval),"reopened review cannot admit the retired operation");
            return !signer.Busy();
        });
    } else {
        Await([&]{sh::PollSigningReviews(state,host);return !signer.Busy();});
        Check(state.daemon_operation==0,"shell releases the completed operation's owner");
    }
    const auto view=state.signing_views.find(2);
    Check(view==state.signing_views.end() || (!view->second.daemon_signed && !view->second.daemon_enriched
          && !view->second.external_signed),"late signer cannot publish material into any current view");
    if (transition=="lock") {
        Check(recovery.Locked() && docs.Documents().empty(),"held reply cannot expose protected documents");
        Check(recovery.Unlock(password,docs),"explicit unlock restores public workspace");
    } else if (transition=="clear" || transition=="close") {
        Check(recovery.RecoverLast(docs),"explicit recovery restores public draft after settlement");
    }
    const auto* source=docs.Find(2);
    Check(source && source->imported && source->imported->parsed->source==fixture.original->source
        && source->imported->parsed->wire==fixture.original->wire && source->transaction.note.draft()==INITIAL,
        "retain exact original opaque bytes and public draft");
    const auto* approval=state.signing_reviews.Authorize(fixture.basis);
    if (transition=="review") {
        Check(!approval || *approval==original_approval,"closing and reopening cannot create or change approval");
    } else {
        Check(!approval && !state.signing_reviews.Finalize(fixture.basis,fixture.collected).artifact
            && !signer.Start(state.signing_reviews,fixture.basis,a::DaemonSignAction::Sign,""),
            "no recovered approval or automatic finalization or signing");
    }
}

void BroadcastRace(const fs::path& root,std::string_view role,unsigned port)
{
    OnlineFixture online(root,port);
    a::RecoverySession recovery(root/"metadata.sqlite");
    Check(recovery.Start(),"open shared owned broadcast store");
    const bool competing=role=="broadcast-contender";
    if (competing) Check(recovery.Pending() && recovery.StartEmpty(),"explicitly decline restoring the first actor's documents");
    ApprovalFixture fixture(root,online,recovery.Gate().Epoch());
    a::DocumentRegistry docs;
    Check(docs.OpenWallet("Signing fixture",fixture.basis.wallet,true)==1,"open exact signing wallet");
    const auto first=docs.OpenImportedDocument({fixture.original,p::DisplayNetwork::Regtest,a::DocumentRepresentation::Source});
    Check(first==2,"open initial transaction document");
    if (!competing) {
        Check(recovery.Enable(docs),"create shared durable journal");
    } else {
        const auto second=docs.OpenImportedDocument({fixture.original,p::DisplayNetwork::Regtest,a::DocumentRepresentation::Source});
        Check(second==3 && second!=first,"open distinct contender document containing same PSBT");
        fixture.basis.document=second;
    }
    fixture.Approve();
    const auto artifact=Finalize(fixture);
    a::Broadcaster broadcaster(online.transport,online.connection);
    Check(broadcaster.Start(a::BroadcastAction::Check,fixture.reviews,fixture.basis,artifact,recovery).accepted,
          "explicit policy check");
    const auto poll=[&]{broadcaster.Poll(fixture.reviews,fixture.reviews.Authorize(fixture.basis),&artifact,&recovery);};
    Await([&]{poll();return !broadcaster.Busy();});
    Check(broadcaster.View().stage==a::BroadcastStage::Ready,"exact preflight succeeds");
    if (role=="broadcast-first") Continue(root,"first-ready");
    Check(broadcaster.Start(a::BroadcastAction::Submit,fixture.reviews,fixture.basis,artifact,recovery).accepted,
          "one explicit submit per distinct current actor");
    if (role=="broadcast-retired") {
        Continue(root,"policy-recheck-started");
        const auto revision=docs.Find(first)->draft_revision;
        Check(recovery.Clear(docs,first) && docs.Find(first)->draft_revision!=revision,
              "Clear invalidates the approved draft during the final policy recheck");
        fixture.reviews.Revoke(first,"Draft cleared during policy recheck.");
        Continue(root,"policy-recheck-retired");
        Await([&]{poll();return !broadcaster.Busy();});
        Check(broadcaster.View().stage==a::BroadcastStage::Cancelled
            && !Take(recovery.LoadBroadcastAttempt(Network(),*artifact.final.document->transaction->txid)),
            "late policy reply cannot journal or send an obsolete draft");
        return;
    }
    if (role=="broadcast-first") {
        Await([&]{poll();return broadcaster.View().stage==a::BroadcastStage::Sending;});
        Await([&]{poll();return !broadcaster.Busy();});
        Check(broadcaster.View().stage==a::BroadcastStage::Uncertain,"lost acknowledgment remains uncertain");
    } else {
        Await([&]{poll();return !broadcaster.Busy();});
        Check(broadcaster.View().stage==a::BroadcastStage::Refused
            && broadcaster.View().message.find("submission lease")!=std::string::npos,
            "overlapping submit is refused by the shared durable lease");
    }
    const auto txid=*artifact.final.document->transaction->txid;
    const auto saved=Take(recovery.LoadBroadcastAttempt(Network(),txid));
    Check(saved && saved->serial==1 && saved->identity.wire_digest==artifact.final_source
        && saved->outcome==(role=="broadcast-first" ? s::BroadcastOutcome::Uncertain : s::BroadcastOutcome::Pending),
        "one durable serial retains exact transaction identity and outcome");
    Check(!broadcaster.Start(a::BroadcastAction::Submit,fixture.reviews,fixture.basis,artifact,recovery).accepted,
          "stale or repeated submit cannot replay the recorded attempt");
    if (role=="broadcast-first") {
        Check(broadcaster.Start(a::BroadcastAction::Check,fixture.reviews,fixture.basis,artifact,recovery).accepted,
              "explicit fresh policy check is read-only");
        Await([&]{poll();return !broadcaster.Busy();});
        Check(broadcaster.View().stage==a::BroadcastStage::Ready
            && !broadcaster.Start(a::BroadcastAction::Submit,fixture.reviews,fixture.basis,artifact,recovery).accepted,
            "even fresh preflight cannot repeat a recorded first Submit");
    }
}

void MetadataRace(const fs::path& root,std::string_view role)
{
    const auto password=Bytes(root/"archive-password");
    const auto scope=Scope();
    const auto other=s::Scope{scope.backend,scope.network,"other-wallet"};
    const auto txid=*Hash256::Parse(std::string(64,'a'));
    const auto path=root/"metadata.sqlite",archive=root/"backup.rmeta";
    a::RecoverySession owner(path);
    a::DocumentRegistry docs;
    if (role=="metadata-protected-contender") {
        Check(owner.Start() && owner.Locked(),"different process detects protected workspace");
        Check(!owner.Unlock(password,docs) && owner.Locked() && owner.Error().find("another instance")!=std::string::npos,
              "second owner is refused while protected workspace has an exclusive owner");
        return;
    }
    if (role=="metadata-writer") {
        Check(owner.Start(),"different process opens the same plain store");
        Check(Take(owner.SaveHistoryLabel(scope,txid,"Concurrent owner label",1))==2,"other owner advances exact scope");
        return;
    }
    {
        auto store=Take(s::MetadataStore::Open(path));
        s::Metadata initial;
        initial.display_name="Original display";
        initial.drafts={{"local-draft",{3},"Independent saved draft",{}}};
        Check(Take(store->Save(scope,initial))==1 && Take(store->Save(other,initial))==1,"seed independent wallet scopes");
    }
    Check(owner.Start(),"open first owner of plain store");
    Take(s::WriteWalletMetadataArchive(archive,password,{scope,"Backup display",{}, {}}));
    const auto staged=Take(owner.InspectWalletMetadata(scope,archive,password));
    Check(staged.destination_revision==1,"stage against exact revision before other owner writes");
    Continue(root,"archive-staged");
    Check(owner.ApplyWalletMetadata(scope,staged).error==s::Error::Conflict,"stale restore cannot overwrite other owner");
    const auto refreshed=Take(owner.InspectWalletMetadata(scope,archive,password));
    Check(refreshed.destination_revision==2,"fresh stage reads other process's revision");
    Check(owner.Protect(docs,password),"protect exact owned store after other plain owner settles");
    Continue(root,"protected-owned");
    Check(owner.Lock(docs) && owner.Unlock(password,docs),"actual protected lock and unlock between inspect and apply");
    Check(owner.ApplyWalletMetadata(scope,refreshed).error==s::Error::Authentication,"old epoch cannot apply after unlock");
    const auto live=Take(owner.HistoryMetadata(scope));
    Check(live.revision==2 && live.labels==std::vector<s::Label>{{{s::TargetKind::Transaction,txid.hex()},"Concurrent owner label"}},
          "previous attempts retained independently acknowledged update");
    Check(Take(owner.ApplyWalletMetadata(scope,Take(owner.InspectWalletMetadata(scope,archive,password))))==3,
          "only a newly inspected, explicitly applied archive commits");
    const auto restored=Take(owner.HistoryMetadata(scope)),unrelated=Take(owner.HistoryMetadata(other));
    Check(restored.display_name=="Backup display" && restored.drafts==live.drafts
        && unrelated.revision==1 && unrelated.display_name=="Original display" && unrelated.drafts==live.drafts,
        "restore preserves unrelated wallet scope and saved drafts");
}

struct HeldCompletion {
    std::binary_semaphore entered{0}, release{0};
    void Hold()
    {
        entered.release();
        if (!release.try_acquire_for(std::chrono::seconds(5))) throw std::runtime_error("fixture completion was not released");
    }
    void Wait()
    {
        Check(entered.try_acquire_for(std::chrono::seconds(4)),"worker entered owned completion gate");
    }
};

void WalletRetirement(const fs::path& root)
{
    OnlineFixture online(root,1);
    HeldCompletion gate, mutation;
    unsigned calls{};
    WalletLifecycle lifecycle([&](const WalletOperation& request,std::stop_token) {
        if (++calls==1) gate.Hold();
        if (request.action==WalletAction::Load) mutation.Hold();
        WalletResult result;
        result.wallet=request.backend;
        result.directory_verified=true;
        result.mutation_dispatched=request.action==WalletAction::Load;
        return result;
    });
    Check(lifecycle.Start(WalletAction::Open,WALLET,online.connection,{2},9),"start exact wallet observation");
    gate.Wait();
    const auto previous=online.connection.Session();
    auto replacement=online.profile;
    ++replacement.connection.port;
    Check(online.connection.SetDraft(replacement) && online.connection.Test(),"explicit test of another backend");
    Await([&]{online.connection.Poll();return !online.connection.Busy();});
    Check(online.connection.Apply() && online.connection.Session()!=previous,"replace the applied backend while wallet result is held");
    gate.release.release();
    Await([&]{
        Check(!lifecycle.Poll(online.connection,{3},10),"obsolete wallet reply cannot adopt after backend, document and epoch change");
        return !lifecycle.Busy();
    });
    Check(calls==1,"no automatic wallet operation replay");
    Check(lifecycle.Start(WalletAction::Open,WALLET,online.connection,{3},10),"fresh explicit wallet observation");
    std::optional<std::pair<WalletAction,WalletResult>> result;
    Await([&]{result=lifecycle.Poll(online.connection,{3},10);return !lifecycle.Busy();});
    Check(result && result->second.wallet && result->second.wallet->candidate==online.connection.Active()->candidate && calls==2,
          "the current result publishes once for its exact owner");
    Check(lifecycle.Start(WalletAction::Load,WALLET,online.connection,{3},10),"hold synthetic wallet mutation acknowledgment");
    mutation.Wait();
    lifecycle.Cancel();
    mutation.release.release();
    Await([&]{Check(!lifecycle.Poll(online.connection,{3},10),"retired wallet mutation cannot update the view");return !lifecycle.Busy();});
    Check(lifecycle.Uncertain(*online.connection.Active(),WALLET)
        && !lifecycle.Start(WalletAction::Load,WALLET,online.connection,{3},10) && calls==3,
        "remote mutation uncertainty is retained and prevents automatic or repeated load");
}

void CameraRetirement()
{
    struct Driver final:CameraDriver {
        HeldCompletion discovery, modes, sample;
        unsigned finds{}, opens{};
        std::vector<CameraDevice> Find(std::stop_token) override
        {
            if (++finds==1) discovery.Hold();
            return {{.name="Synthetic camera"}};
        }
        std::string Capture(const CameraDevice&,const CameraCallbacks& cb,std::stop_token) override
        {
            if (++opens==1) modes.Hold();
            if (!cb.choose({{2,2,CameraPixels::Gray}},"Synthetic camera")) return "retired open completion";
            cb.started();
            sample.Hold();
            cb.frame({2,2,{0,1,2,3},"Synthetic QR"});
            return "retired terminal callback";
        }
    };
    auto fixture=std::make_shared<Driver>();
    CameraService camera(fixture);
    const auto settle=[&] {
        Await([&]{return !camera.View().retiring;});
        const auto view=camera.View();
        Check(view.stage==CameraStage::Idle && !view.frame && view.status.empty() && view.sample==0 && view.modes.empty(),
              "retired camera callbacks cannot publish or change status");
    };
    Check(camera.Find(),"explicit discovery");
    fixture->discovery.Wait();
    camera.Stop();
    fixture->discovery.release.release();
    settle();
    Check(camera.View().devices.empty() && camera.Find(),"late discovery discarded, explicit fresh discovery allowed");
    Await([&]{return camera.View().stage==CameraStage::Devices;});
    auto view=camera.View();
    const auto device=view.devices.at(0);
    Check(camera.Open(view.generation,device),"explicit open");
    fixture->modes.Wait();
    camera.Stop();
    fixture->modes.release.release();
    settle();
    Check(camera.Open(camera.View().generation,device),"fresh open after close");
    Await([&]{return camera.View().stage==CameraStage::ChooseMode;});
    view=camera.View();
    Check(camera.Start(view.generation,view.modes.at(0)),"explicit streaming start");
    fixture->sample.Wait();
    camera.Stop();
    fixture->sample.release.release();
    settle();
    Check(fixture->finds==2 && fixture->opens==2,"no implicit reopen or rediscovery");
}

void FileRetirement(const fs::path& root,std::string_view kind)
{
    if (kind=="download") {
        DownloadVerifier verifier;
        DownloadRequest current{1,{{(root/"release.bin").string(),(root/"manifest").string(),
            (root/"signature").string(),(root/"public").string()}, {}}};
        Check(verifier.Start(current),"begin exact local proof");
        Continue(root,"file-dispatched");
        ++current.revision;
        Continue(root,"file-retired");
        Await([&]{Check(!verifier.Poll(&current),"changed selection cannot adopt the held verifier result");return !verifier.Busy();});
        Check(verifier.Start(current),"fresh explicit verification after settlement");
        std::optional<DownloadResult> result;
        Await([&]{result=verifier.Poll(&current);return !verifier.Busy();});
        Check(result && result->integrity==DownloadIntegrity::Match && result->signature==DownloadSignature::Unsupported
            && result->trust==DownloadTrust::Unestablished,"fresh exact hash remains distinct from unsupported signature and trust");
        return;
    }
    if (kind=="batch") {
        OnlineFixture online(root,1);
        a::DaemonSigner signer(online.transport,online.connection);
        SigningHost host(online.connection,signer);
        sh::State state;
        const auto wallet=*a::VerifiedWalletBinding(*online.connection.Active());
        const auto id=state.documents.OpenWallet("Batch owner",wallet,true);
        state.documents.Find(id)->history.enabled=true;
        Check(state.documents.Navigate(a::Section::Send),"open batch parent Send");
        const auto* draft=state.documents.Find(id);
        const sh::BatchBinding binding{id,online.connection.Session(),online.connection.Revision(),draft->draft_revision,{},wallet,false};
        state.batch=sh::BatchEditor{binding,1,1,StageRecipients(draft->recipients)};
        state.batch_read_generation=1;
        state.batch_read_revision=1;
        state.batch_read=std::make_unique<BatchFileRead>((root/"batch.csv").string(),state.batch->work.next);
        Continue(root,"file-dispatched");
        state.batch->work.rows[0].label="Concurrent public edit";
        ++state.batch->revision;
        Continue(root,"file-retired");
        Await([&]{sh::PollBatch(state,host);return !state.batch_read;});
        Check(state.batch && state.batch->work.rows[0].label=="Concurrent public edit" && state.batch->status.empty()
            && !state.documents.Find(id)->recipients.Dirty(),"late parsed CSV cannot replace staged work or apply itself");
        state.batch_read_revision=state.batch->revision;
        state.batch_read=std::make_unique<BatchFileRead>((root/"batch.csv").string(),state.batch->work.next);
        Await([&]{sh::PollBatch(state,host);return !state.batch_read;});
        Check(state.batch->work.rows[0].label=="CSV fixture" && !state.documents.Find(id)->recipients.Dirty(),"fresh CSV stages only");
        return;
    }

    TransactionDocuments documents;
    LocalDocumentContext current{1,2,{1},{1}};
    const bool qr=kind=="qr";
    const auto read=[&](auto owner) {
        return qr ? documents.ReadQrImage(owner,(root/"source.png").string())
                  : documents.ReadFile(owner,(root/"original.psbt").string(),p::DisplayNetwork::Regtest);
    };
    Check(read(current),"start real local file decode");
    Continue(root,"file-dispatched");
    ++current.generation;
    ++current.revision.value;
    ++current.epoch.value;
    Continue(root,"file-retired");
    Await([&]{Check(!documents.Poll(current),"late local parse cannot publish to a different generation, draft or epoch");return !documents.Busy();});
    Check(read(current),"fresh explicit local read");
    std::optional<LocalDocumentReply> result;
    Await([&]{result=documents.Poll(current);return !documents.Busy();});
    Check(result && (qr ? result->qr_text==Bytes(root/"qr-source.txt") : bool(result->parsed.document)),
          "current file and QR parsers publish the exact supported result");
}
}

int main(int argc,char** argv)
{
    if (argc<3) return 2;
    try {
        const std::string_view mode=argv[1];
        const fs::path root=argv[2];
        const auto storage=root/"metadata.sqlite";
        if (mode=="paint") { Paint(root/"qr-source.txt"); return 0; }
        if (mode=="facts") { Facts(root); return 0; }
        if (mode=="protected-schema") {
            std::cout<<"{\"schema\":"<<ProtectedSchema(root)<<",\"current\":"<<s::schema::Version<<"}\n";
            return 0;
        }
        if (mode=="legacy" || mode=="protected-legacy") {
            Legacy(storage);
            if (mode=="protected-legacy") {
                const auto bytes=Bytes(storage);
                const auto file=Take(s::ProtectedFile::Open(storage.string()+".protected",Bytes(root/"archive-password")));
                Take(file->Save({reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size()}));
                Check(fs::remove(storage),"keep only the owned protected legacy file");
            }
        } else if (mode=="migrate") {
            auto opened=Take(s::MetadataStore::Open(storage));
            CheckLegacy(*opened);
        } else if (mode=="migrate-protected") {
            auto opened=Take(s::MetadataStore::OpenProtected(storage,Bytes(root/"archive-password")));
            CheckLegacy(*opened);
        } else if (mode=="archive-contracts") ArchiveContracts(root);
        else if (mode.starts_with("metadata-")) MetadataRace(root,mode);
        else if (mode=="wallet-retirement") WalletRetirement(root);
        else if (mode=="camera-retirement") CameraRetirement();
        else if (mode.starts_with("file-")) FileRetirement(root,mode.substr(5));
        else if (argc==4 && mode.starts_with("signer-")) SignerRace(root,mode.substr(7),unsigned(std::stoi(argv[3])));
        else if (argc==4 && mode.starts_with("broadcast-")) BroadcastRace(root,mode,unsigned(std::stoi(argv[3])));
        else if (mode=="recover-public" && argc==4) RecoverPublic(root,argv[3]);
        else if ((mode.starts_with("draft-") || mode.starts_with("qr-")) && argc==3) PublicCrash(root,mode);
        else if (argc==4) TransactionCrash(root,mode,unsigned(std::stoi(argv[3])));
        else return 2;
        std::cout<<"{\"passed\":true,\"checks\":"<<checks<<"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
