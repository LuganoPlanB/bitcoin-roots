// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/history_workspace.h"
#include "app/message_signing.h"
#include "tests/message_fixture.h"
#include <cstdio>
#include <fstream>
#include <stdexcept>

using namespace roots::gui;
namespace a=app;
namespace p=protocol;
namespace fixture=test::message_fixture;
namespace {
unsigned checks{};
void Check(bool condition,const char* message)
{
    if(!condition)throw std::runtime_error(message);
    ++checks;
}
template<class Update,class Done> void Await(Update update,Done done)
{
    using namespace std::chrono_literals;
    for(int count=0;count<2500;++count){
        update();
        if(done())return;
        std::this_thread::sleep_for(2ms);
    }
    throw std::runtime_error("message fixture did not settle");
}
void Vectors()
{
    using F=p::MessageFormat;
    using N=p::MessageNetwork;
    using C=p::MessageCheck;
    constexpr char core_signature[]="IPojfrX2dfPnH26UegfbGQQLrdK844DlHq5157/P6h57WyuS/Qsl+h/WSVGDF4MUi4rWSswW38oimDYfNNUBUOk=";
    Check(p::VerifyMessage(F::Legacy,N::Main,"15CRxFdyRpGZLW9w8HnHvVduizdL5jKNbs","Trust no one",core_signature).check==C::Valid,"Core legacy vector verifies");
    const auto exact=p::VerifyMessage(F::Legacy,N::TestFamily,fixture::ADDRESS,fixture::MESSAGE,fixture::SIGNATURE);
    Check(exact.check==C::Valid && exact.public_key.size()==33,"literal Unicode and whitespace vector");
    Check(p::VerifyMessage(F::Legacy,N::TestFamily,fixture::ADDRESS,"  Café\nCafé\t ",fixture::SIGNATURE).check==C::Mismatch,"normalization is a byte change");
    Check(p::VerifyMessage(F::Legacy,N::TestFamily,fixture::ADDRESS,std::string_view(fixture::MESSAGE).substr(2),fixture::SIGNATURE).check==C::Mismatch,"trimming is a byte change");
    Check(p::VerifyMessage(F::Legacy,N::TestFamily,"mipcBbFg9gMiCh81Kj8tqqdgoZub1ZJRfn",fixture::MESSAGE,fixture::SIGNATURE).check==C::Mismatch,"wrong valid address");
    const auto uncompressed=p::VerifyMessage(F::Legacy,N::Main,"1EHNa6Q4Jz2uvNExL497mE43ikXhwF6kZm","",fixture::EMPTY_UNCOMPRESSED);
    Check(uncompressed.check==C::Valid && uncompressed.public_key.size()==65,"empty message and uncompressed P2PKH header");
    Check(p::VerifyMessage(F::Legacy,N::Main,fixture::ADDRESS,fixture::MESSAGE,fixture::SIGNATURE).check==C::WrongNetwork,"wrong network family");
    Check(p::VerifyMessage(F::Legacy,N::TestFamily,fixture::ADDRESS,"bad",fixture::SIGNATURE).check==C::Mismatch,"wrong message");
    Check(p::VerifyMessage(F::Bip322,N::TestFamily,fixture::ADDRESS,fixture::MESSAGE,fixture::SIGNATURE).check==C::UnsupportedFormat,"no relabeling legacy signatures");
    Check(p::CheckMessageAddress(N::Main,"3B5fQsEXEaV8v6U3ejYc8XaKXAkyQj2MjV").check==C::UnsupportedAddress,"P2SH unsupported");
    Check(p::CheckMessageAddress(N::Main,"bc1p83n3au0rjylefxq2nc2xh2y4jzz4pm6zxj4mw5pagdjjr2a9f36s6jjnnu").check==C::UnsupportedAddress,"Taproot unsupported");
    Check(p::CheckMessageAddress(N::Main,"bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4").check==C::UnsupportedAddress,"Segwit unsupported");
    Check(p::CheckMessageAddress(N::TestFamily,std::string(" ")+fixture::ADDRESS).check==C::InvalidAddress,"address whitespace is never trimmed");
    Check(p::CheckMessageAddress(N::TestFamily,"invalid").check==C::InvalidAddress,"invalid address");
    Check(p::VerifyMessage(F::Legacy,N::TestFamily,fixture::ADDRESS,fixture::MESSAGE,"invalid").check==C::MalformedSignature,"invalid Base64");
    auto header=std::string(fixture::SIGNATURE);header[0]='J';
    Check(p::VerifyMessage(F::Legacy,N::TestFamily,fixture::ADDRESS,fixture::MESSAGE,header).check==C::UnsupportedHeader,"other compact header rejected");
    Check(!p::ValidMessageBytes(std::string("a\0b",3)) && !p::ValidMessageBytes(std::string(1,char(0xc3))),"invalid literal UTF-8 input");
    Check(p::ValidMessageBytes(std::string(p::MESSAGE_BYTES,'a')) && !p::ValidMessageBytes(std::string(p::MESSAGE_BYTES+1,'a')),"exact message bound");
    Check(!p::MessageNetworkForGenesis(std::string(64,'f')),"unknown genesis cannot authorize signing");
}
void Rpc(unsigned port,const char* cookie,std::string_view mode,const std::filesystem::path& markers)
{
    using S=a::MessageSignStatus;
    rpc::CookieCredentials credentials;
    rpc::Transport transport(credentials);
    ConnectionService connection([&](const auto& c,std::stop_token stop){return ProbeCapabilities(transport,c,stop);});
    ServerCandidate candidate;
    candidate.connection.port=uint16_t(port);candidate.connection.credential.reference=cookie;
    candidate.expected_genesis=fixture::GENESIS;candidate.wallet=fixture::WALLET;
    Check(connection.SetDraft(candidate) && connection.Test(),"explicit fixture connection test");
    Await([&]{connection.Poll();},[&]{return !connection.Busy();});
    Check(connection.CanApply() && connection.Apply(),"explicit fixture profile Apply");
    const auto binding=a::VerifiedWalletBinding(*connection.Active());
    Check(binding.has_value(),"fixture identity verified");
    a::DocumentRegistry docs;
    const auto id=docs.OpenWallet("Message fixture",*binding,true);
    auto* document=docs.Find(id);
    Check(document!=nullptr,"exact wallet document");
    document->observation=a::ObservationState::Locked;
    Check(!a::CurrentMessageWallet(document,&connection,LockEpoch{1}),"locked document cannot authorize a message");
    document->observation=a::ObservationState::Offline;
    const auto scope=a::CurrentMessageWallet(document,&connection,LockEpoch{1});
    Check(scope && scope->document==id && scope->binding==*binding,"exact message wallet scope");
    a::MessageSigning service(transport,connection);
    a::MessageSignRequest request{*scope,1,p::MessageFormat::Legacy,p::MessageNetwork::TestFamily,fixture::ADDRESS,fixture::MESSAGE};
    auto unbound=request;++unbound.owner.profile_revision;
    Check(service.Start(unbound)==S::NoWallet,"stale profile revision never dispatches");
    if(mode=="watch-only" || mode=="schema" || mode=="schema-type"){
        const auto expected=mode=="watch-only"?S::WatchOnly:S::Unsupported;
        Check(service.Start(request)==expected && !service.Busy(),"disabled signer never dispatches");
        return;
    }
    Check(service.Start(request)==S::Ready,"explicit message request starts once");
    Check(service.Start(request)==S::Busy,"single pending request");
    std::optional<a::MessageSignResult> result;
    if(mode=="late"){
        Await([]{},[&]{return std::filesystem::exists(markers/"entered");});
        auto edited=request;++edited.edit_revision;edited.owner.epoch=LockEpoch{2};edited.message+=" changed";
        Check(!service.Poll(&edited),"changed context retires in-flight response");
        std::ofstream(markers/"retired")<<"retired\n";
        Await([&]{result=service.Poll(&edited);},[&]{return !service.Busy();});
        Check(!result && !service.Poll(&request),"late result cannot be accepted or replayed");
        return;
    }
    Await([&]{result=service.Poll(&request);},[&]{return !service.Busy();});
    Check(result.has_value(),"current fixture request settles");
    const auto expected=mode=="success"?S::Signed:mode=="fresh-watch"?S::WatchOnly:mode=="locked"?S::Locked:
        mode=="not-owned"?S::NotOwned:mode=="bad-reply"?S::BadReply:mode=="changed-genesis"?S::Identity:
        mode=="uncertain"?S::Uncertain:S::Failed;
    Check(result->status==expected,"exact typed signing outcome");
    Check(mode=="success"?result->signature==fixture::SIGNATURE:result->signature.empty(),"only verified exact signature published");
    Check(!service.Poll(&request),"completion is consumed once");
}
}
int main(int argc,char** argv)
{
    try{
        if(argc==1)Vectors();
        else if(argc==6 && std::string_view(argv[1])=="--rpc")
            Rpc(unsigned(std::stoi(argv[2])),argv[3],argv[4],argv[5]);
        else return 2;
        std::printf("{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":%u}\n",checks);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}
}
