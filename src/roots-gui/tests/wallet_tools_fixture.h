// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "ui/shell/workspace.h"
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace roots::gui::test::wallet_tools {
constexpr auto REGTEST = "0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206";
constexpr auto MAIN = "000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f";
constexpr auto WHEN = 1700000002000LL;
constexpr auto RAW_DETAIL = "=sensitive-fixture-raw 9.87654321";
inline Hash256 Hash(char c){return *Hash256::Parse(std::string(64,c));}

template<class Service>
void Settle(Service& service,int64_t now)
{
    for(int attempt=0;attempt<1000;++attempt){
        service.Tick(now);
        if(!service.Busy())return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("wallet tools observation worker did not settle");
}

struct Wallets {
    std::array<uint64_t,3> ids{};
    std::atomic<unsigned> fetches{},address_fetches{};
    std::atomic<bool> fail{};
    AddressService addresses{[this](const RefreshIdentity& identity,const auto&,std::stop_token){
        ++address_fetches;
        AddressSnapshot snapshot{identity.context,*identity.profile.wallet,Hash('b').hex()};
        snapshot.rows.push_back({"address-key-"+*identity.profile.wallet,"","bcrt1fixtureaddress","", "Address label"});
        snapshot.stable=true;
        return AddressFetch{std::move(snapshot)};
    }};

    RefreshReply Fetch(const RefreshRequest& request)
    {
        ++fetches;
        const auto& id=request.identity;
        RefreshReply reply{id,request.generation,request.sequence};
        if(fail){reply.errors.fill(rpc::Error::Connection);return reply;}
        const auto& wallet=*id.profile.wallet;
        const bool archived=wallet=="wallet-a";
        const bool locked=wallet=="wallet-locked";
        const char unique=archived?'1':locked?'3':'2';
        const auto sats=archived?100000001:locked?990000009:200000003;
        HistoryBatch batch{id.context,wallet,Hash('b').hex()};
        batch.balances.trusted=Money::Satoshis(sats);
        batch.balances.pending=Money::Satoshis(17);
        batch.full_query=batch.stable=batch.inventory_available=true;
        batch.height=19;
        HistoryTransaction transaction{Hash('a')}; // Same txid observed in distinct wallets.
        transaction.time=1700000000;transaction.confirmations=2;transaction.state=HistoryState::Confirmed;
        transaction.wallet_delta=Money::Satoshis(sats);
        HistoryDetail primary;
        primary.category="receive";primary.label=locked?"Locked-only label":"Shared coffee";primary.amount.value=transaction.wallet_delta;
        transaction.details.push_back(primary);
        primary.label=archived?"Archive detail":"=formula-sheet";
        transaction.details.push_back(primary);
        batch.transactions.push_back(std::move(transaction));
        batch.coins.push_back({{Hash(unique),0},Money::Satoshis(17),0,true,false,false,"0014","bcrt1fixtureaddress","Output label"});
        reply.observed_unix_ms=WHEN+fetches.load();
        reply.node=NodeObservation{19,batch.lastblock,false,false};
        reply.wallet=WalletObservation{batch.balances.trusted,batch.balances.pending,batch.balances.immature};
        reply.wallet->scanning=archived;
        reply.history=HistoryDelta{{},{},batch.lastblock,std::move(batch)};
        return reply;
    }

    void Seed(ui::shell::State& state,RefreshService& refresh,LockEpoch epoch={})
    {
        constexpr const char* names[]{"Archived wallet","Current wallet","Locked wallet canary"};
        constexpr const char* wallet_names[]{"wallet-a","wallet-b","wallet-locked"};
        for(size_t index:{0,2,1}){
            ServerCandidate profile;
            profile.connection.credential.reference="/explicit/typed-fixture";
            profile.wallet=wallet_names[index];
            profile.expected_genesis=index==0?MAIN:REGTEST;
            BackendCapabilities caps;
            caps.candidate=profile;caps.genesis=profile.expected_genesis;caps.wallet=WalletAvailability::Available;
            caps.authenticated=caps.identity_verified=true;caps.descriptors=true;
            for(const char* method:{"getwalletinfo","getbalances","listsinceblock","gettransaction","listdescriptors","deriveaddresses","getdescriptorinfo","getaddressinfo"})
                caps.methods[method]={true,true,true,false,{"blockhash","include_removed","txid","private","range","descriptor","address"}};
            const auto binding=app::VerifiedWalletBinding(caps);
            if(!binding)throw std::runtime_error("typed wallet fixture binding required");
            const auto id=state.documents.OpenWallet(names[index],*binding,true);ids[index]=id;
            auto* document=state.documents.Find(id);
            document->history.enabled=true;
            const auto identity=app::WalletRefreshIdentity(caps,101+index,*document,epoch);
            if(!identity || !refresh.Bind(*identity,caps,false,0) || !addresses.Bind(*identity,caps,0))
                throw std::runtime_error("typed wallet fixture observation scope");
            Settle(refresh,0);Settle(addresses,0);
            if(index==2)document->observation=app::ObservationState::Locked;
        }
        if(!state.documents.Activate(ids[1]))throw std::runtime_error("typed wallet fixture selection");
    }
};
}
