// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/coin_controls.h"
#include <sodium.h>
#include <algorithm>
#include <charconv>
namespace roots::gui::app {
namespace {
std::optional<std::string> Digest(std::string_view s){if(s.empty() || s.size()>10000 || s.size()%2 || sodium_init()<0)return {};std::string canonical(s);for(char& c:canonical){if(c>='A' && c<='F')c=char(c-'A'+'a');else if(!((c>='a' && c<='f') || (c>='0' && c<='9')))return {};}s=canonical;unsigned char b[32];char h[65];if(crypto_hash_sha256(b,reinterpret_cast<const unsigned char*>(s.data()),s.size()))return {};sodium_bin2hex(h,sizeof(h),b,sizeof(b));return std::string(h);}
struct AddressExclusion {std::string script,address;};
std::optional<AddressExclusion> ParseAddress(std::string_view key){if(!key.starts_with("a1:"))return {};key.remove_prefix(3);auto cut=key.find(':');if(cut!=64)return {};auto descriptor=Hash256::Parse(key.substr(0,cut));if(!descriptor || descriptor->hex()!=key.substr(0,cut))return {};key.remove_prefix(cut+1);cut=key.find(':');if(cut==std::string_view::npos)return {};uint32_t n{};auto parsed=std::from_chars(key.data(),key.data()+cut,n);if(parsed.ec!=std::errc{} || parsed.ptr!=key.data()+cut || n>0x7fffffff || std::to_string(n)!=key.substr(0,cut))return {};key.remove_prefix(cut+1);cut=key.find(':');if(cut!=64)return {};auto script=Hash256::Parse(key.substr(0,cut));if(!script || script->hex()!=key.substr(0,cut))return {};auto address=key.substr(cut+1);if(address.empty() || address.size()>128 || !std::all_of(address.begin(),address.end(),[](unsigned char c){return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9');}))return {};return AddressExclusion{script->hex(),std::string(address)};}
bool SameSet(const Document& d){if(!d.coin_draft || d.coin_draft->inputs.size()!=d.selected_coins.size())return false;std::set<std::string> a,b;for(const auto& p:d.coin_draft->inputs)a.insert(CoinKey(p.outpoint));for(const auto& p:d.selected_coins)b.insert(CoinKey(p));return a==b && a.size()==d.selected_coins.size();}
}
std::string CoinControls::ScopeKey(const storage::Scope& s){return rpc::Quote(s.backend)+","+rpc::Quote(s.network.genesis.hex())+","+rpc::Quote(s.daemon_wallet);}
bool CoinControls::Bound(const Document& d,const RefreshView& v,const RecoverySession& recovery) const {if(recovery.Locked() || !d.binding || !d.history.enabled || !v.identity || !v.ledger)return false;const auto& c=v.identity->context;auto origin=MetadataBackend(v.identity->profile);return origin && *origin==d.binding->backend && v.identity->profile.wallet==std::optional(d.binding->DaemonWallet()) && c.backend.value()=="connection-"+std::to_string(v.identity->connection_session) && c.network==d.binding->network && c.wallet==d.binding->wallet && c.document.value()=="wallet-document-"+std::to_string(d.id) && c.revision.value==d.history.query_revision && c.lock_epoch==recovery.Gate().Epoch() && v.ledger->Context()==c && v.ledger->DaemonWallet()==d.binding->DaemonWallet();}
CoinControls::MetadataState* CoinControls::StateFor(const Document& d,const RefreshView& v,RecoverySession& recovery,bool reload){if(!Bound(d,v,recovery))return nullptr;auto scope=HistoryMetadataScope(*d.binding);auto key=ScopeKey(scope);auto found=metadata.find(key);if(found==metadata.end()){if(metadata.size()==8)metadata.erase(metadata.begin());found=metadata.emplace(key,MetadataState{scope}).first;}auto& m=found->second;if(!m.attempted || reload){m.attempted=true;auto loaded=recovery.HistoryMetadata(scope);if(!loaded){m.valid=false;m.error=std::string("Local coin policy unavailable: ")+storage::ErrorName(loaded.error)+". Last acknowledged state retained; Reload policy to retry.";}else{m.acknowledged=std::move(*loaded.value);m.valid=true;m.error.clear();for(const auto& t:m.acknowledged.freezes)if((t.kind==storage::TargetKind::Address && !ParseAddress(t.key)) || (t.kind==storage::TargetKind::Output && !ParseCoinKey(t.key)) || t.kind==storage::TargetKind::Transaction){m.valid=false;m.error="Unrecognized saved freeze identity. Policy is Unknown; saved bytes are retained. Resolve the metadata before preflight.";break;}}++revision;}return &m;}
bool CoinControls::Reload(const Document& d,const RefreshView& v,RecoverySession& r){auto* m=StateFor(d,v,r,true);return m && m->valid;}
std::string CoinControls::Status(const Document& d,const RefreshView& v,RecoverySession& r){auto* m=StateFor(d,v,r);if(!m)return "Local coin policy Unknown: verified wallet scope unavailable";if(!m->valid)return m->error;return "Acknowledged local policy revision "+std::to_string(m->acknowledged.revision)+"; GUI reservations are temporary, separate from daemon locks. Preflight rereads policy and wallet.";}
Receipt CoinControls::Save(RecoverySession& r,MetadataState& m,std::vector<storage::Target> next){auto saved=r.SaveCoinFreezes(m.scope,next,m.acknowledged.revision);++revision;if(!saved){m.valid=false;m.error=std::string("Freeze was not acknowledged: ")+storage::ErrorName(saved.error)+". Previous saved state retained; Reload policy, then deliberately retry.";return {false,m.error};}m.acknowledged.freezes=std::move(next);m.acknowledged.revision=*saved.value;m.error.clear();return {true,"Local freeze policy saved. No daemon lock changed."};}
std::optional<CoinPolicy> CoinControls::Policy(const Document& d,const RefreshView& v,RecoverySession& r,uint64_t owner,std::span<const PinnedCoin> replaced_inputs){
    auto* m=StateFor(d,v,r);if(!m || !m->valid || !v.chain_current || !v.ledger->InventoryAvailable() || !v.ledger->LocksAvailable())return {};
    CoinPolicy p{v.identity->context,v.history_sequence,{}};std::set<std::string> frozen,scripts,reserved,locked;
    for(const auto& t:m->acknowledged.freezes){if(t.kind==storage::TargetKind::Output)frozen.insert(t.key);else if(t.kind==storage::TargetKind::Address)if(auto address=ParseAddress(t.key))scripts.insert(address->script);}
    for(const auto& [id,lease]:leases)if(id!=owner && lease.wallet==*d.binding)reserved.insert(lease.keys.begin(),lease.keys.end());
    for(const auto& point:v.ledger->DaemonLocked())locked.insert(CoinKey(point));
    const auto add=[&](const Outpoint& point,std::string_view locking_script) {
        const auto key=CoinKey(point);
        const auto script=Digest(locking_script);
        if(!script)return;
        const auto rule=frozen.contains(key) || scripts.contains(*script)?CoinRule::Frozen:reserved.contains(key)?CoinRule::Reserved:locked.contains(key)?CoinRule::Unavailable:CoinRule::Allowed;
        p.rules.emplace(key,rule);
    };
    for(const auto& coin:v.ledger->Coins())add(coin.outpoint,coin.script);
    for(const auto& coin:replaced_inputs)add(coin.outpoint,coin.script);
    return p;
}

Receipt CoinControls::FreezeOutpoints(const Document& d,const RefreshView& v,const CoinInventory& inventory,const std::set<std::string>& keys,bool freeze,RecoverySession& r){auto* m=StateFor(d,v,r);if(!m || !m->valid || !v.chain_current || inventory.Context()!=v.identity->context || inventory.Sequence()!=v.history_sequence || keys.empty() || keys.size()>4096)return {false,"Freeze unavailable: reload policy in the current verified wallet. Selection retained."};auto next=m->acknowledged.freezes;for(const auto& key:keys){if(!ParseCoinKey(key))return {false,"Invalid exact outpoint; nothing saved."};const auto row=inventory.Rows().find(key);if(freeze && (row==inventory.Rows().end() || !row->second.present || !row->second.current))return {false,"Freeze needs a current owned output observation; no partial selection was saved."};const storage::Target target{storage::TargetKind::Output,key};auto at=std::find(next.begin(),next.end(),target);if(freeze && at==next.end())next.push_back(target);if(!freeze && at!=next.end())next.erase(at);}return Save(r,*m,std::move(next));}
std::optional<std::string> CoinControls::AddressTarget(const DerivedAddress& a){auto digest=Digest(a.script);auto descriptor=Hash256::Parse(a.descriptor_id);if(!digest || !descriptor || descriptor->hex()!=a.descriptor_id || a.index>0x7fffffff || a.key!=a.descriptor_id+":"+std::to_string(a.index))return {};auto key="a1:"+a.descriptor_id+":"+std::to_string(a.index)+":"+*digest+":"+a.address;return ParseAddress(key)?std::optional(key):std::nullopt;}
Receipt CoinControls::FreezeAddress(const Document& d,const RefreshView& v,const AddressView& a,std::string_view key,bool freeze,RecoverySession& r){auto* m=StateFor(d,v,r);if(!m || !m->valid || !v.chain_current || !a.current || !a.snapshot || !a.snapshot->stable || !a.identity || *a.identity!=*v.identity || a.snapshot->context!=v.identity->context || a.snapshot->wallet!=d.binding->DaemonWallet() || a.snapshot->tip!=v.ledger->Tip())return {false,"Address freeze needs current verified public descriptor ownership; nothing saved."};auto row=std::find_if(a.snapshot->rows.begin(),a.snapshot->rows.end(),[&](const auto& row){return row.key==key;});if(row==a.snapshot->rows.end())return {false,"Exact public address identity disappeared; nothing saved."};auto target=AddressTarget(*row);if(!target)return {false,"Unsupported public address freeze identity; nothing saved."};auto next=m->acknowledged.freezes;if(freeze){storage::Target t{storage::TargetKind::Address,*target};if(std::find(next.begin(),next.end(),t)==next.end())next.push_back(std::move(t));}else{auto exact=ParseAddress(*target);std::erase_if(next,[&](const auto& t){auto a=t.kind==storage::TargetKind::Address?ParseAddress(t.key):std::nullopt;return a && exact && a->script==exact->script && a->address==exact->address;});}return Save(r,*m,std::move(next));}
bool CoinControls::AddressFrozen(const Document& d,const RefreshView& v,const DerivedAddress& address,RecoverySession& r){auto* m=StateFor(d,v,r);auto target=AddressTarget(address);if(!m || !m->valid || !target)return false;auto exact=ParseAddress(*target);return std::any_of(m->acknowledged.freezes.begin(),m->acknowledged.freezes.end(),[&](const auto& t){auto saved=t.kind==storage::TargetKind::Address?ParseAddress(t.key):std::nullopt;return saved && saved->script==exact->script && saved->address==exact->address;});}
CoinControls::PinResult CoinControls::Pin(DocumentRegistry& docs,uint64_t wallet_id,const std::set<std::string>& selected,
    const CoinInventory* inventory,const RefreshView* view,RecoverySession* recovery,std::optional<protocol::OfflineChange> change)
{
    auto* wallet=docs.Find(wallet_id);
    if (!wallet || !wallet->binding || selected.empty() || selected.size()>4096)
        return {0,"Choose exact outpoints; no draft created."};
    if (docs.Documents().size()>=32) return {0,"You have 32 open documents. Close a tab, then retry. Exact selection retained."};
    CoinDraft packet{*wallet->binding,{}};
    const auto network=protocol::DocumentNetwork(wallet->binding->network);
    const bool capture=inventory && recovery && view && Bound(*wallet,*view,*recovery) && view->chain_current
        && !view->offline && inventory->Context()==view->identity->context && inventory->Sequence()==view->history_sequence
        && selected.size()<=protocol::MAX_OFFLINE_INPUTS;
    if (capture) packet.offline_change=std::move(change);
    std::vector<Outpoint> pins;
    size_t remaining=protocol::MAX_OFFLINE_PREVIOUS*2;
    for (const auto& key:selected) {
        auto point=ParseCoinKey(key);
        if (!point) return {0,"Invalid selected identity; no partial draft created."};
        PinnedCoin input{*point,{},"","Unknown: no current owned inventory evidence","Unvalidated; explicit fresh preflight required"};
        if (inventory && inventory->Rows().contains(key)) {
            const auto& row=inventory->Rows().at(key);
            input.value=row.value;
            input.script=row.script;
            input.pinned_reason=row.reason;
            if (capture && row.eligible==true) {
                const auto tx=view->ledger->Rows().find(point->txid.hex());
                const auto coin=std::ranges::find(view->ledger->Coins(),*point,&HistoryCoin::outpoint);
                if (coin!=view->ledger->Coins().end() && tx!=view->ledger->Rows().end()
                    && tx->second.previous_hex.size()<=remaining) {
                    protocol::OfflineCoinProof proof{tx->second.previous_hex,coin->descriptor,coin->redeem_script,
                        coin->witness_script,view->ledger->Observed()};
                    if (protocol::ValidOfflineCoin({*point,input.value,input.script,proof},network)) {
                        remaining-=proof.previous_hex.size();
                        input.offline=std::move(proof);
                    }
                }
            }
        }
        packet.inputs.push_back(std::move(input));
        pins.push_back(*point);
    }
    const auto id=docs.OpenTransaction("Pinned coin draft");
    auto* draft=docs.Find(id);
    draft->coin_draft=std::move(packet);
    draft->selected_coins=std::move(pins);
    draft->draft_revision={1};
    draft->transaction.note=ui::components::Edit("");
    messages[id]="Every selected outpoint is pinned, including unavailable or Unknown inputs.";
    ++revision;
    return {id,messages[id]};
}

bool CoinControls::AllowsOfflineDraft(const Document& draft,const DocumentRegistry& docs,const RefreshView* view,RecoverySession* recovery)
{
    if (!SameSet(draft) || draft.coin_draft->observed_unavailable || !recovery || recovery->Locked()) return false;
    const auto& packet=*draft.coin_draft;
    auto local=recovery->HistoryMetadata(HistoryMetadataScope(packet.wallet));
    if (!local) return false;
    for (const auto& pin:packet.inputs) {
        auto script=Digest(pin.script);
        if (!pin.offline || !script) return false;
        for (const auto& target:local.value->freezes) {
            if (target.kind==storage::TargetKind::Output && target.key==CoinKey(pin.outpoint)) return false;
            if (target.kind==storage::TargetKind::Address) {
                const auto frozen=ParseAddress(target.key);
                if (!frozen || frozen->script==*script) return false;
            }
        }
        for (const auto& [id,lease]:leases)
            if (id!=draft.id && lease.wallet==packet.wallet && std::ranges::find(lease.keys,CoinKey(pin.outpoint))!=lease.keys.end())
                return false;
    }
    const auto* wallet=RegisteredWallet(docs,packet.wallet);
    if (!wallet || !view || view->offline || !view->chain_current || !Bound(*wallet,*view,*recovery)) return true;
    auto policy=Policy(*wallet,*view,*recovery,draft.id);
    CoinInventory inventory(view->identity->context,wallet->binding->DaemonWallet());
    if (!policy || !inventory.Apply(*view->ledger,view->history_sequence,view->chain_current,&*policy)) return false;
    for (const auto& pin:packet.inputs) {
        const auto row=inventory.Rows().find(CoinKey(pin.outpoint));
        if (row==inventory.Rows().end() || row->second.eligible!=true || pin.value!=row->second.value || pin.script!=row->second.script) return false;
    }
    return true;
}
std::optional<uint64_t> CoinControls::Owner(uint64_t id) const {auto i=leases.find(id);return i==leases.end()?std::nullopt:std::optional(i->second.token);}
std::string CoinControls::DraftStatus(uint64_t id) const {auto i=messages.find(id);return i==messages.end()?"Restored/public pinned draft: no reservation or validation authority. Explicit fresh validation required.":i->second;}
Receipt CoinControls::Release(uint64_t id,std::optional<uint64_t> expected,RefreshService* refresh){auto i=leases.find(id);if(expected && (i==leases.end() || i->second.token!=*expected))return {false,"Reservation owner mismatch; no owner released."};if(i!=leases.end())leases.erase(i);if(pending && pending->document==id){if(refresh && refresh->View().generation==pending->generation)refresh->Cancel(RefreshMonotonicMs());pending.reset();}messages[id]="Own GUI reservation/preflight released. Exact public pins retained; daemon locks untouched.";++revision;return {true,messages[id]};}
void CoinControls::PrivacyLock(){leases.clear();pending.reset();metadata.clear();messages.clear();++revision;}
void CoinControls::InvalidateMetadata(const storage::Scope& scope)
{
    metadata.erase(ScopeKey(scope));++revision;
}
Receipt CoinControls::RequestPreflight(DocumentRegistry& docs,uint64_t id,RecoverySession& recovery,RefreshService& refresh,int64_t now){auto* draft=docs.Find(id);if(recovery.Locked() || !draft || !SameSet(*draft))return {false,"Pinned draft changed or locked. No validation or reservation acquired."};const auto* wallet=RegisteredWallet(docs,draft->coin_draft->wallet);auto v=refresh.View();if(!wallet || !Bound(*wallet,v,recovery) || v.offline || v.busy)return {false,"Preflight needs this exact verified online wallet, idle read service and current draft. Pins retained."};if(pending){messages[pending->document]="Preflight superseded; no automatic retry or funding.";pending.reset();}if(!refresh.RequestNow(now))return {false,"Fresh read unavailable; pins retained, funding denied."};const auto generation=refresh.View().generation;pending=Pending{id,draft->draft_revision,*v.identity,generation,v.history_sequence,now>INT64_MAX-30000?INT64_MAX:now+30000};messages[id]="Validating exact pins with a fresh read-only wallet/daemon-lock observation; no funding or transaction RPC.";for(auto& input:draft->coin_draft->inputs)input.current_reason="Awaiting fresh preflight; exact original pin retained";refresh.Tick(now);++revision;return {true,messages[id]};}
void CoinControls::Complete(DocumentRegistry& docs,uint64_t id,const Document& wallet,const RefreshView& v,RecoverySession& recovery){auto* draft=docs.Find(id);if(!draft || !SameSet(*draft))return;Reload(wallet,v,recovery);auto policy=Policy(wallet,v,recovery,id);CoinInventory inventory(v.identity->context,wallet.binding->DaemonWallet());inventory.Apply(*v.ledger,v.history_sequence,v.chain_current,policy?&*policy:nullptr);bool eligible=true;std::vector<std::string> keys;for(auto& input:draft->coin_draft->inputs){const auto key=CoinKey(input.outpoint);keys.push_back(key);auto row=inventory.Rows().find(key);if(row==inventory.Rows().end()){input.current_reason=std::find(v.ledger->DaemonLocked().begin(),v.ledger->DaemonLocked().end(),input.outpoint)!=v.ledger->DaemonLocked().end()?"Unavailable: external daemon lock; this GUI does not own/release it":"Unavailable: absent from fresh wallet inventory (spent, missing or unknown). Exact pin retained";eligible=false;}else{input.current_reason=row->second.reason;if(!row->second.eligible || !*row->second.eligible)eligible=false;if((input.value && *input.value!=row->second.value) || (!input.script.empty() && input.script!=row->second.script)){input.current_reason="Unavailable: pinned value/script changed; no substitution";eligible=false;}}}if(!eligible){if(v.chain_current && v.ledger->InventoryAvailable())draft->coin_draft->observed_unavailable=true;Release(id);messages[id]="Preflight denied: one or more exact pins unavailable/Unknown. Every identity/reason retained; no funding.";return;}if(!next_owner){messages[id]="Reservation identity exhausted; pins retained, funding unavailable.";return;}leases.insert_or_assign(id,Lease{next_owner++,id,draft->draft_revision,v.identity->context,*wallet.binding,std::move(keys)});messages[id]="Exact inputs locally reserved by this draft after fresh preflight. Funding service unavailable; no funding/sign/broadcast action performed.";++revision;}
void CoinControls::Sync(DocumentRegistry& docs,RecoverySession* recovery,RefreshService* refresh,int64_t now){if(!recovery || recovery->Locked()){if(!leases.empty() || pending || !metadata.empty())PrivacyLock();return;}for(auto i=leases.begin();i!=leases.end();){auto* draft=docs.Find(i->first);const auto* wallet=RegisteredWallet(docs,i->second.wallet);bool keep=draft && SameSet(*draft) && draft->draft_revision==i->second.revision && wallet && i->second.context.lock_epoch==recovery->Gate().Epoch();if(keep && refresh){auto v=HistoryForDocument(*wallet,*refresh,recovery->Gate().Epoch());const auto selected=refresh->View();keep=v && v->identity && !v->offline && selected.identity && selected.identity->context==i->second.context && v->identity->context==i->second.context;}if(!keep){messages[i->first]="Own reservation retired after close/revision/wallet/epoch change; public pins need fresh validation.";i=leases.erase(i);++revision;}else ++i;}std::erase_if(messages,[&](const auto& m){return !docs.Find(m.first);});if(!pending || !refresh)return;const auto request=*pending;auto* draft=docs.Find(request.document);auto v=refresh->View();const Document* wallet=draft && draft->coin_draft?RegisteredWallet(docs,draft->coin_draft->wallet):nullptr;if(!draft || draft->draft_revision!=request.revision || !SameSet(*draft) || !wallet || !Bound(*wallet,v,*recovery) || !v.identity || *v.identity!=request.identity || v.generation!=request.generation || v.offline || now>request.deadline){pending.reset();if(v.generation==request.generation)refresh->Cancel(now);messages[request.document]="Preflight cancelled by document/profile/privacy/offline/deadline change; no reservation or funding.";if(draft && draft->coin_draft)for(auto& input:draft->coin_draft->inputs)input.current_reason="Unknown: preflight cancelled; explicit retry required";++revision;return;}if(v.busy)return;if(v.history_sequence<=request.sequence || !v.chain_current || v.errors[2]!=rpc::Error::None){pending.reset();for(auto& p:draft->coin_draft->inputs)p.current_reason="Unknown: fresh preflight read failed or did not advance; pin retained";messages[request.document]="Preflight denied: fresh read incomplete/failed. Explicit retry required; no funding.";++revision;return;}pending.reset();Complete(docs,request.document,*wallet,v,*recovery);}
}
