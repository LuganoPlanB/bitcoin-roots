// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "app/coin_inventory.h"
#include "app/recovery.h"
#include "app/commands.h"
#include "services/addresses.h"
namespace roots::gui::app {
// Durable exclusions and ephemeral GUI-owned draft leases have separate owners.
// This service owns NO financial mutation/funding/signing/broadcast/lock RPC port.
class CoinControls {
    struct MetadataState {storage::Scope scope;storage::Metadata acknowledged;bool attempted{},valid{};std::string error;};
    struct Lease {uint64_t token{},document{};DraftRevision revision;RequestContext context;WalletBinding wallet;std::vector<std::string> keys;};
    struct Pending {uint64_t document{};DraftRevision revision;RefreshIdentity identity;uint64_t generation{},sequence{};int64_t deadline{};};
    std::map<std::string,MetadataState> metadata;
    std::map<uint64_t,Lease> leases; // only this service instance's live draft owners
    std::optional<Pending> pending;
    std::map<uint64_t,std::string> messages;
    uint64_t next_owner{1},revision{};
    static std::string ScopeKey(const storage::Scope&);
    MetadataState* StateFor(const Document&,const RefreshView&,RecoverySession&,bool reload=false);
    bool Bound(const Document&,const RefreshView&,const RecoverySession&) const;
    Receipt Save(RecoverySession&,MetadataState&,std::vector<storage::Target>);
    void Complete(DocumentRegistry&,uint64_t,const Document&,const RefreshView&,RecoverySession&);
public:
    uint64_t Revision() const{return revision;}
    void Sync(DocumentRegistry&,RecoverySession*,RefreshService*,int64_t now);
    void PrivacyLock();
    void InvalidateMetadata(const storage::Scope&);
    bool Reload(const Document&,const RefreshView&,RecoverySession&);
    std::string Status(const Document&,const RefreshView&,RecoverySession&);
    std::optional<CoinPolicy> Policy(const Document&,const RefreshView&,RecoverySession&,uint64_t owner_document=0,
        std::span<const PinnedCoin> replaced_inputs={});
    Receipt FreezeOutpoints(const Document&,const RefreshView&,const CoinInventory&,const std::set<std::string>&,bool freeze,RecoverySession&);
    Receipt FreezeAddress(const Document&,const RefreshView&,const AddressView&,std::string_view key,bool freeze,RecoverySession&);
    bool AddressFrozen(const Document&,const RefreshView&,const DerivedAddress&,RecoverySession&);
    struct PinResult {uint64_t document{};std::string reason;};
    PinResult Pin(DocumentRegistry&,uint64_t wallet_document,const std::set<std::string>&,const CoinInventory*,
                  const RefreshView* = nullptr, RecoverySession* = nullptr, std::optional<protocol::OfflineChange> = {});
    bool AllowsOfflineDraft(const Document&, const DocumentRegistry&, const RefreshView*, RecoverySession*);

    Receipt RequestPreflight(DocumentRegistry&,uint64_t,RecoverySession&,RefreshService&,int64_t now);
    Receipt Release(uint64_t document,std::optional<uint64_t> expected_owner={},RefreshService* =nullptr);
    std::optional<uint64_t> Owner(uint64_t document) const;
    std::string DraftStatus(uint64_t document) const;
    static std::optional<std::string> AddressTarget(const DerivedAddress&);
};
}
