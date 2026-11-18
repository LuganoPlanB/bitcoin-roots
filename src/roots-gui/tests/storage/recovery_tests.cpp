// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/recovery.h"
#include "adapters/rpc/json.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
using namespace roots::gui;
namespace a=app;
namespace {
int checks{};
void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
std::string Bytes(const std::filesystem::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
a::DocumentRegistry Documents(){a::DocumentRegistry d;const NetworkIdentity network{*Hash256::Parse(std::string(64,'1'))};d.OpenWallet("First public wallet",{*BackendSessionId::Make("profile-A"),*WalletId::Make("wallet/札幌"),network},true);d.OpenWallet("Second public wallet",{*BackendSessionId::Make("profile-B"),*WalletId::Make("wallet/札幌"),network},true);d.OpenTransaction("First transaction##literal");d.OpenTransaction("Second transaction");
    for(uint64_t id=1;id<=4;++id){auto& doc=*d.Find(id);doc.section=a::Section::Send;doc.draft_revision={9007199254740993ULL+id};doc.Current().note.SetDraft("Independent public draft "+std::to_string(id));doc.Current().selection.selected={id,id+20};doc.Current().selection.focused=id;doc.Current().scroll_x=3;doc.Current().scroll_y=float(id*31);doc.Current().table_x=float(id*7);doc.Current().table_y=float(id*11);doc.selected_coins={{*Hash256::Parse(std::string(64,char('a'+id-1))),uint32_t(id)}};doc.excluded_coins={{*Hash256::Parse(std::string(64,'e')),UINT32_MAX}};}
    d.Move(4,-1);d.Activate(2);return d;
}
void Same(const a::DocumentRegistry& actual,const a::DocumentRegistry& expected){auto x=a::EncodePublicSession({actual,{}}),y=a::EncodePublicSession({expected,{}});Check(x && y && *x==*y,"independent documents/revisions/coins/positions differ");for(const auto& d:actual.Documents())Check(d.observation==a::ObservationState::Offline,"restored document must be offline");}
void SendClearContracts(const std::filesystem::path& path)
{
    auto documents=Documents();
    const auto id=documents.ActiveId();
    documents.Active()->funding_rate="2.125";
    documents.Active()->funding_target=9;
    const auto policy=protocol::PreviewPublicWallet(protocol::PublicImportKind::Descriptor,
        "wpkh(0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798)","main");
    const auto change=policy.preview ? DeriveFundingChange(policy.preview->descriptors.front().descriptor,0,
        protocol::DisplayNetwork::Mainnet) : std::nullopt;
    Check(change.has_value(),"independently derived public change fixture");
    documents.Active()->binding->network={*Hash256::Parse("000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f")};
    documents.Active()->funding_change=change;
    const auto before=*documents.Active();
    a::RecoverySession writer(path);
    Check(writer.Enable(documents),"explicit Send recovery opt-in");
    a::RecoverySession stale(path);
    a::DocumentRegistry conflicted;
    Check(stale.Start() && stale.Restore(conflicted),"stale Send snapshot loaded deliberately");
    documents.Find(4)->transaction.note.SetDraft("Winning separate draft");
    Check(writer.Checkpoint(documents),"winning Send recovery checkpoint");
    const auto winning=Bytes(path);
    Check(!stale.ClearSend(conflicted,id,before.draft_revision)
        && conflicted.Active()->funding_rate=="2.125" && conflicted.Active()->funding_target==9
        && conflicted.Active()->funding_change==change && !stale.HasClearedSend() && Bytes(path)==winning,
        "Clear CAS failure keeps both current drafts and winning snapshot");
    Check(writer.ClearSend(documents,id,before.draft_revision),"durable Send Clear acknowledged");
    Check(documents.Active()->funding_rate.empty() && !documents.Active()->funding_target && !documents.Active()->funding_change
        && documents.Active()->selected_coins.empty()
        && documents.Active()->excluded_coins.empty() && documents.Active()->Current().note.draft()==before.Current().note.draft(),
        "Send fields cleared independently of unrelated edits");
    a::RecoverySession restarted(path);
    a::DocumentRegistry restored;
    Check(restarted.Start() && restarted.Pending() && restarted.Restore(restored) && restarted.HasClearedSend(),
        "Clear archive survives restart without replay");
    Check(restored.Active()->funding_rate.empty() && restored.Active()->selected_coins.empty() && !restored.Active()->funding_change,
        "Cleared Send remains empty until explicit recovery");
    restored.Find(4)->transaction.note.SetDraft("New separate edits after restart");
    Check(restarted.RecoverSend(restored),"explicit Send recovery");
    Check(restored.Active()->funding_rate==before.funding_rate && restored.Active()->funding_target==before.funding_target
        && restored.Active()->funding_change==change
        && restored.Active()->selected_coins==before.selected_coins
        && restored.Active()->excluded_coins==before.excluded_coins
        && restored.Active()->draft_revision.value>before.draft_revision.value
        && restored.Find(4)->transaction.note.draft()=="New separate edits after restart" && !restarted.HasClearedSend(),
        "Send recovery restores exact values and preserves other unsaved tabs");
}
void Contracts(const std::filesystem::path& p){
    SendClearContracts(p.parent_path()/"send-clear.sqlite");
    auto documents=Documents();a::RecoverySession disabled(p);Check(disabled.Start() && !disabled.Enabled(),"default recovery must be off");Check(disabled.Checkpoint(documents) && !std::filesystem::exists(p),"opt-out must not create store or persist positions");
    disabled.SetPresentation(true,2);Check(disabled.Enable(documents),"enable explicit persistence");const auto expected=documents;const auto bytes=Bytes(p);
    a::RecoverySession reopened(p);a::DocumentRegistry empty;Check(reopened.Start() && reopened.Pending() && empty.Documents().empty(),"startup must only offer recovery");Check(Bytes(p)==bytes,"loading must preserve database bytes");Check(reopened.Restore(empty),"deliberate restore");Check(reopened.Hidden() && reopened.Unit()==2,"privacy and units restore with the same snapshot");Same(empty,expected);
    Check(!reopened.Restore(empty),"duplicate restore rejected");
    empty.Find(1)->Current().note.SetDraft("New public edit");Check(reopened.Checkpoint(empty),"edit checkpoint");Check(empty.Find(1)->draft_revision.value==expected.Find(1)->draft_revision.value+1,"edited revision increments exactly");Check(empty.Find(2)->draft_revision==expected.Find(2)->draft_revision,"other document revision independent");
    const auto before_clear=*empty.Find(1);Check(reopened.Clear(empty,1),"recoverable clear");Check(empty.Find(1)->Current().note.draft().empty() && empty.Find(1)->selected_coins.empty() && empty.Find(1)->excluded_coins.empty(),"clear actually removes draft and coin policy");
    Check(reopened.RecoverLast(empty),"recover cleared public draft");Check(empty.Find(1)->Current().note.draft()==before_clear.Current().note.draft() && empty.Find(1)->selected_coins==before_clear.selected_coins && empty.Find(1)->excluded_coins==before_clear.excluded_coins,"Clear recovery exact coin policy");
    Check(reopened.Close(empty,std::array<uint64_t,1>{3}) && !empty.Find(3),"recoverable close");
    a::RecoverySession after_close(p);Check(after_close.Start() && after_close.Pending(),"recovery copy survives restart");a::DocumentRegistry restored;Check(after_close.Restore(restored) && !restored.Find(3),"closed view stays closed until requested");Check(after_close.RecoverLast(restored) && restored.Find(3),"deliberate reopen from recovery copy");Check(restored.Find(3)->transaction.note.draft()==expected.Find(3)->transaction.note.draft(),"transaction restored independently");
    a::RecoverySession stale(p);Check(stale.Start(),"stale instance load");a::DocumentRegistry stale_docs;Check(stale.Restore(stale_docs),"stale instance deliberate restore");restored.Find(4)->transaction.note.SetDraft("Winning instance");Check(after_close.Checkpoint(restored),"winning snapshot write");const auto winning=Bytes(p);stale_docs.Find(4)->transaction.note.SetDraft("Stale instance");Check(!stale.Checkpoint(stale_docs) && stale.Error()=="metadata-conflict","stale writer rejected");Check(Bytes(p)==winning,"conflict preserves winner bytes");
    Check(after_close.Clear(restored,2),"clear second wallet");Check(after_close.Disable(),"explicit disable forgets recovery");a::RecoverySession forgotten(p);Check(forgotten.Start() && !forgotten.Enabled() && !forgotten.Pending(),"opt-out leaves no recoverable drafts");
    auto json=rpc::Json::Parse(*a::EncodePublicSession({expected,{}}));Check(bool(json),"valid codec fixture");json->items.push_back({});Check(!a::DecodePublicSession(json->Encode()),"unknown executable field cannot enter codec");
    auto invalid=expected;invalid.Find(1)->excluded_coins=invalid.Find(1)->selected_coins;Check(!a::EncodePublicSession({invalid,{}}),"selected and excluded overlap rejected");invalid=expected;invalid.Find(1)->selected_coins.push_back(invalid.Find(1)->selected_coins[0]);Check(!a::EncodePublicSession({invalid,{}}),"duplicate coin rejected");invalid=expected;invalid.Find(1)->observation=a::ObservationState::Locked;Check(!a::EncodePublicSession({invalid,{}}),"locked state cannot be exposed to recovery writer");
    invalid=expected;invalid.Find(1)->Current().note.saved=std::string(256,'x');Check(!a::EncodePublicSession({invalid,{}}),"writer cannot save an unreadable editor buffer");invalid=expected;invalid.Find(1)->kind=a::DocumentKind(2);invalid.Find(1)->binding.reset();Check(!a::EncodePublicSession({invalid,{}}),"writer rejects unknown document kind");
    a::RecoverySession keep(p);Check(keep.Enable(documents),"enable current documents");const auto kept=Bytes(p);a::RecoverySession start_empty(p);Check(start_empty.Start() && start_empty.StartEmpty(),"explicit empty startup");a::DocumentRegistry blank;Check(start_empty.Checkpoint(blank) && Bytes(p)==kept,"start empty preserves original recovery and suspends autosave");
}
void Actor(const std::filesystem::path& path){a::RecoverySession session(path);Check(session.Start(),"actor startup");auto docs=Documents();if(session.Pending()){docs=a::DocumentRegistry();Check(session.Restore(docs),"actor explicit restore");}else Check(session.Enable(docs),"actor enable");std::cout<<"ready\n"<<std::flush;std::string command;while(std::getline(std::cin,command)){if(command=="edit"){docs.Find(4)->transaction.note.SetDraft("Interrupted edit after boundary");std::cout<<"edited\n";}else if(command=="save"){Check(session.Checkpoint(docs),"actor save");std::cout<<"saved\n";}else if(command=="inspect"){std::cout<<docs.Find(4)->transaction.note.draft()<<'\n';}else throw std::runtime_error("actor command");std::cout<<std::flush;}}
}
int main(int argc,char** argv){if(argc!=3)return 2;try{if(std::string(argv[1])=="actor"){Actor(argv[2]);return 0;}Contracts(argv[2]);std::cout<<"{\"passed\":true,\"checks\":"<<checks<<"}\n";return 0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<" after "<<checks<<" checks\n";return 1;}}
