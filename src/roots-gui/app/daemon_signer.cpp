// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/daemon_signer.h"
#include "app/replacement.h"
#include "app/history_workspace.h"

#include <algorithm>
#include <charconv>
#include <sodium.h>

namespace roots::gui::app {
namespace {
struct Secret {
    std::array<char,255> bytes{};
    size_t length{};
    ~Secret() {sodium_memzero(bytes.data(),bytes.size());}
};

bool Is(const rpc::Json* value,rpc::Json::Kind kind,std::string_view text)
{
    return value && value->kind==kind && value->text==text;
}

std::optional<int64_t> Integer(const rpc::Json* value)
{
    if (!value || value->kind!=rpc::Json::Kind::Number) return {};
    int64_t number{};
    const auto parsed=std::from_chars(value->text.data(),value->text.data()+value->text.size(),number);
    if (parsed.ec!=std::errc{} || parsed.ptr!=value->text.data()+value->text.size() || number<0) return {};
    return number;
}

bool TaprootAll(const SigningBasis& basis,size_t i)
{
    const auto& script=basis.content->transaction->inputs[i].previous_script;
    return script.size()==68 && script.starts_with("5120") && basis.sighash[i]==1;
}

bool NeedsExplicitAll(const SigningBasis& basis)
{
    for (size_t i=0;i<basis.sighash.size();++i) {
        if (TaprootAll(basis,i)) return true;
    }
    return false;
}

DaemonSignResult Process(rpc::Transport& transport,const ServerCandidate& profile,const SigningApproval& approval,
    DaemonSignAction action,Secret& secret,const SelectedDevice& device,std::stop_token stop)
{
    const bool external=action==DaemonSignAction::ExternalSign;
    const auto failure=[action,external](std::string reason,bool uncertain=false){
        return DaemonSignResult{action,{},std::move(reason),false,uncertain,external};
    };
    const auto& basis=approval.reviewed;
    const auto& original=*basis.content;
    const auto verified=protocol::InspectOfflineReview(original,basis.proof,basis.change_output);
    if (verified.commitment!=approval.commitment || verified.change_output!=basis.change_output
        || SigningDigest(original.source)!=approval.original_source)
        return failure("The original no longer passes the independent payment and input policy checks.");
    for (size_t i=0;i<basis.sighash.size();++i) {
        if (protocol::ReviewSighash(original,i)!=basis.sighash[i])
            return failure("The original no longer matches its exact approved sighash commitments.");
    }
    std::optional<DeviceSelection::Operation> device_operation;
    if (external) {
        if (!device || !device->Matches(profile,basis.session,basis.profile))
            return failure("The matching signer selection is no longer current.");
        device_operation.emplace(*device);
        if (!*device_operation) return failure("The selected signer is already handling another request.");
        for (size_t i=0;i<basis.proof.inputs.size();++i) {
            if (stop.stop_requested() || !device->Live()) return failure("The device request was cancelled.");
            if (!device->Owns(basis.proof.inputs[i].descriptor,original.transaction->inputs[i].previous_script,false))
                return failure("An approved input is outside the selected signer's verified descriptor policy.");
        }
        if (basis.proof.change && (!basis.change_output || !device->Owns(basis.proof.change->descriptor,
            original.transaction->outputs[*basis.change_output].script,false,basis.proof.change->index)))
            return failure("The independently approved change is outside the selected signer's verified descriptor policy.");
    }

    const auto retired=[action](std::string reason) {
        return DaemonSignResult{action,{},std::move(reason),false,false,true};
    };
    if (basis.cancellation || basis.child_package) {
        auto reason=CheckFeeRelationship(transport,profile,basis.cancellation,basis.child_package,stop);
        if (!reason.empty()) return retired(std::move(reason));
    }

    uint64_t id{};
    const auto call=[&](std::string method,std::string args,bool wallet,rpc::Effect effect=rpc::Effect::ReadOnly) {
        rpc::Limits limits;
        if (external) {
            limits.response_bytes=256*1024;
            limits.total_timeout=effect==rpc::Effect::Mutation ? std::chrono::seconds(30) : std::chrono::seconds(4);
        }
        return transport.Call(profile.connection,{++id,std::move(method),std::move(args),wallet ? profile.wallet : std::nullopt,effect},stop,limits);
    };
    const auto genesis=call("getblockhash","[0]",false);
    if (genesis.error!=rpc::Error::None || !genesis.result
        || !Is(&*genesis.result,rpc::Json::Kind::String,basis.wallet.network.genesis.hex()))
        return failure("The current daemon network could not be verified. The original PSBT is retained.");
    const auto info=call("getwalletinfo","[]",true);
    if (info.error!=rpc::Error::None || !info.result
        || !Is(info.result->Find("walletname"),rpc::Json::Kind::String,basis.wallet.DaemonWallet())
        || !Is(info.result->Find("descriptors"),rpc::Json::Kind::Boolean,"true"))
        return failure("The exact current descriptor wallet could not be verified.");
    if (action!=DaemonSignAction::Enrich && !external
        && (!Is(info.result->Find("private_keys_enabled"),rpc::Json::Kind::Boolean,"true")
        || !Is(info.result->Find("external_signer"),rpc::Json::Kind::Boolean,"false")))
        return failure("The current wallet cannot sign using daemon-held private keys.");
    const auto* unlock_field=info.result->Find("unlocked_until");
    const auto until=Integer(unlock_field);
    if (unlock_field && !until) return failure("The current daemon wallet lock status is unavailable.");
    const auto now=std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const bool locked=until && *until<=now;
    if (stop.stop_requested()) return failure("Daemon request cancelled.");

    if (action==DaemonSignAction::Unlock) {
        if (!locked) return failure("The wallet is already unlocked or has no daemon passphrase. No unlock was sent.");
        const auto reply=transport.UnlockWallet(profile.connection,*profile.wallet,
            {secret.bytes.data(),secret.length},DAEMON_UNLOCK_SECONDS,stop);
        sodium_memzero(secret.bytes.data(),secret.bytes.size());
        if (reply.error==rpc::Error::None)
            return {action,{},"The daemon accepted a 30-second unlock. Choose Sign with daemon separately.",true};
        if (reply.rpc_code==-14) return failure("The daemon rejected the wallet passphrase.");
        if (reply.mutation_uncertain) return failure("The unlock outcome is uncertain. The daemon may have accepted its bounded timeout.",true);
        return failure("The daemon wallet unlock could not be sent.");
    }
    const bool signing=action==DaemonSignAction::Sign || external;
    if (signing && locked)
        return failure(external ? "The daemon will not use this external wallet while it is locked."
            : "The daemon wallet is locked. Choose Unlock daemon wallet explicitly.");
    const bool explicit_all=NeedsExplicitAll(basis);
    std::string params="{\"psbt\":"+rpc::Quote(protocol::TransactionBase64(original.wire))
        +",\"sign\":"+(signing ? "true" : "false")+",\"finalize\":false,\"bip32derivs\":"
        +(signing ? "false" : "true");
    if (explicit_all) params+=",\"sighashtype\":\"ALL\"";
    params+='}';
    if (external) {
        const auto state=device->Recheck(transport,stop);
        if (state!=DeviceState::Ready) {
            device->Retire(state);
            return failure(DeviceStateText(state));
        }
        if (!device->Live() || stop.stop_requested()) return failure("The device request was cancelled.");
    }
    const auto reply=call("walletprocesspsbt",std::move(params),true,
        signing ? rpc::Effect::Mutation : rpc::Effect::ReadOnly);
    if (reply.rpc_code==-13 && !external) return failure("The daemon wallet is locked. Choose Unlock daemon wallet explicitly.");
    if (reply.error!=rpc::Error::None) {
        if (external) {
            const auto state=SignerFailure(reply);
            device->Retire(state);
            return failure(DeviceStateText(state),reply.mutation_uncertain);
        }
        if (reply.error!=rpc::Error::Rpc && reply.mutation_uncertain)
            return failure("The signing outcome is uncertain. The approved original is retained. No retry was sent.",true);
        return failure("The daemon rejected or could not process the exact PSBT. The original is retained.");
    }
    const auto* psbt=reply.result ? reply.result->Find("psbt") : nullptr;
    if (!psbt || psbt->kind!=rpc::Json::Kind::String)
        return failure("The daemon returned no unfinalized PSBT.");
    const auto parsed=protocol::ParseTransactionDocument(psbt->text,basis.proof.network,stop,true);
    if (!parsed.document || !parsed.document->psbt_version) return failure("The daemon returned a malformed PSBT.");
    // RPC complete and hex describe daemon output only. They never enter the
    // public result or advance the local transaction workflow.
    auto merged=protocol::ApplyDaemonPsbt(original,*parsed.document,signing);
    if (!merged.document) return failure(std::move(merged.error));
    const auto proof=protocol::InspectOfflineReview(*merged.document,basis.proof,basis.change_output);
    if (proof.commitment!=approval.commitment || proof.change_output!=basis.change_output)
        return failure("The daemon PSBT no longer passes the exact approved input and payment proofs.");
    for (size_t i=0;i<basis.sighash.size();++i) {
        if (protocol::ReviewSighash(*merged.document,i)!=basis.sighash[i])
            return failure("The daemon PSBT changed an approved sighash commitment.");
    }
    if (basis.cancellation || basis.child_package) {
        auto reason=CheckFeeRelationship(transport,profile,basis.cancellation,basis.child_package,stop);
        if (!reason.empty()) return retired(std::move(reason));
    }
    if (external) {
        const auto state=device->Recheck(transport,stop);
        if (state!=DeviceState::Ready) {
            device->Retire(state);
            return failure(DeviceStateText(state));
        }
    }
    return {action,std::move(merged.document),external ? "Device signature material verified locally. The original PSBT is retained."
        : signing ? "Daemon signature material verified. Open the PSBT to inspect or export it."
        : "Enriched PSBT verified. The approved original remains unchanged.",true};
}
}

DaemonSigner::~DaemonSigner() {if (worker.joinable()) worker.request_stop();}

bool DaemonSigner::Current(const SigningBasis& basis) const
{
    const auto& caps=connection.Active();
    return std::this_thread::get_id()==owner && !connection.Offline() && !connection.PrivacyLocked()
        && !basis.offline && basis.session && connection.Session()==basis.session && connection.Revision()==basis.profile
        && caps && caps->descriptors==true && VerifiedWalletBinding(*caps)==basis.wallet;
}

std::string DaemonSigner::Unavailable(const SigningBasis& basis,DaemonSignAction action,const SelectedDevice& device) const
{
    const bool external=action==DaemonSignAction::ExternalSign;
    if (action!=DaemonSignAction::Sign && action!=DaemonSignAction::Enrich && action!=DaemonSignAction::Unlock && !external)
        return "Unrecognized daemon signing operation.";
    if (!Current(basis)) return "The approved wallet, network or connection is no longer current.";
    if (Busy()) return "Wait for the current daemon signing operation to settle.";
    const auto& caps=*connection.Active();
    if (external && (!device || !device->Current(connection) || device->Busy()))
        return "Rescan and select the matching signer for this external wallet, or use PSBT file or QR exchange.";
    if (!external && action!=DaemonSignAction::Enrich && (caps.private_keys!=true || caps.external_signer!=false))
        return "This wallet does not sign with daemon-held private keys.";
    if (!basis.content || !basis.content->psbt_version || !basis.content->transaction
        || basis.sighash.size()!=basis.content->transaction->inputs.size())
        return "Signing requires an exact supported PSBT.";
    if (external && basis.content->transaction->inputs.size()>64)
        return "Device signing supports up to 64 approved inputs. Use PSBT file or QR exchange.";
    if (NeedsExplicitAll(basis) && std::ranges::find(basis.sighash,0)!=basis.sighash.end())
        return "This daemon RPC cannot express mixed Taproot DEFAULT and ALL commitments.";
    if (action==DaemonSignAction::Unlock) {
        const auto& profile=caps.candidate.connection;
        if (!profile.tls && (profile.proxy!=ProxyKind::Direct || (profile.host!="127.0.0.1" && profile.host!="::1")))
            return "Daemon passphrases require TLS or a direct numeric loopback connection.";
        if (!caps.unlocked_until || caps.Supports("walletpassphrase","passphrase")!=SupportReason::Available
            || caps.Supports("walletpassphrase","timeout")!=SupportReason::Available)
            return "The exact wallet has no proven encrypted-wallet unlock route.";
    } else {
        for (const auto* arg:{"psbt","sign","finalize","bip32derivs"}) {
            if (caps.Supports("walletprocesspsbt",arg)!=SupportReason::Available)
                return "The daemon has not proven the named unfinalized PSBT arguments.";
        }
        if (NeedsExplicitAll(basis) && caps.Supports("walletprocesspsbt","sighashtype")!=SupportReason::Available)
            return "The daemon has not proven the exact reviewed sighash argument.";
    }
    return {};
}

bool DaemonSigner::Start(const SigningReviews& reviews,const SigningBasis& basis,DaemonSignAction action,
    std::string_view passphrase,SelectedDevice device)
{
    const auto* approval=reviews.Authorize(basis);
    if (!approval || !Unavailable(basis,action,device).empty() || passphrase.size()>255
        || (action!=DaemonSignAction::ExternalSign && device)
        || (action==DaemonSignAction::Unlock)!=!passphrase.empty()
        || !rpc::ValidUtf8(passphrase) || passphrase.find('\0')!=passphrase.npos) return false;
    auto secret=std::make_unique<Secret>();
    std::ranges::copy(passphrase,secret->bytes.begin());
    secret->length=passphrase.size();
    const auto profile=connection.Active()->candidate;
    try {
        running=*approval;
        running_device=device;
        worker=std::jthread([this,profile,approval=*approval,action,device=std::move(device),secret=std::move(secret)](std::stop_token stop) mutable {
            DaemonSignResult result{action,{},"The daemon signing request could not be processed."};
            try {result=Process(transport,profile,approval,action,*secret,device,stop);} catch (...) {result.original_retired=bool(device);}
            if (device && !result.success && device->Live()) device->Retire(DeviceState::Failure);
            secret.reset();
            std::lock_guard lock(mutex);
            completed=std::move(result);
        });
    } catch (...) {
        running.reset();
        running_device.reset();
        return false;
    }
    return true;
}

void DaemonSigner::Cancel()
{
    if (std::this_thread::get_id()!=owner) return;
    if (running_device) running_device->Retire();
    running.reset();
    if (Busy()) worker.request_stop();
}

std::optional<DaemonSignResult> DaemonSigner::Poll(const SigningApproval* current)
{
    if (std::this_thread::get_id()!=owner || !Busy()) return {};
    if (running && (!current || *current!=*running || !Current(current->reviewed)
        || (running_device && !running_device->Live() && running_device->RetiredState()==DeviceState::Idle))) Cancel();
    std::optional<DaemonSignResult> reply;
    {
        std::lock_guard lock(mutex);
        if (!completed) return {};
        reply=std::move(completed);
        completed.reset();
    }
    worker.join();
    const bool valid=running.has_value();
    running.reset();
    if (auto device=std::exchange(running_device,{}); device && (!valid || (reply->success && !device->Current(connection))))
        return DaemonSignResult{DaemonSignAction::ExternalSign,{},"The device request was retired. Existing PSBTs are retained.",false,true,true};
    return valid ? std::move(reply) : std::nullopt;
}
}
