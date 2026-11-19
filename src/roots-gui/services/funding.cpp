// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/funding.h"
#include "services/fees.h"
#include "services/wallet_lifecycle.h"

#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace roots::gui {
namespace {
using Json = rpc::Json;
using Clock = std::chrono::steady_clock;
constexpr int64_t MAX_MONEY = 2100000000000000;
constexpr uint32_t SEQUENCE = 0xfffffffd;
constexpr size_t MAX_PROOF_BYTES = 512 * 1024;
using CoinKey = std::pair<std::string, uint32_t>;
CoinKey Key(const Outpoint& point) { return {point.txid.hex(), point.index}; }

void Need(bool ok, const char* error)
{
    if (!ok) throw std::runtime_error(error);
}

bool StringIs(const Json* value, std::string_view text)
{
    return value && value->kind == Json::Kind::String && value->text == text;
}
std::optional<int64_t> Integer(const Json* value)
{
    if (!value || value->kind != Json::Kind::Number) return {};
    int64_t n{};
    const auto [end, error] = std::from_chars(value->text.data(), value->text.data() + value->text.size(), n);
    return error == std::errc{} && end == value->text.data() + value->text.size() ? std::optional(n) : std::nullopt;
}
std::optional<Money> Amount(const Json* value)
{
    if (!value || (value->kind != Json::Kind::Number && value->kind != Json::Kind::String)) return {};
    const auto amount = Money::ParseJsonBtc(value->text);
    return amount && amount->sats() >= 0 && amount->sats() <= MAX_MONEY ? amount : std::nullopt;
}
void Add(int64_t& total, Money value)
{
    Need(value.sats() >= 0 && value.sats() <= MAX_MONEY - total, "Funding amounts exceed the Bitcoin monetary bound.");
    total += value.sats();
}
std::string QuoteFeeRate(uint32_t per_kvb)
{
    const auto fraction = std::to_string(1000 + per_kvb % 1000).substr(1);
    return std::to_string(per_kvb / 1000) + "." + fraction;
}
uint32_t OutputVsize(std::string_view script)
{
    const auto bytes = uint32_t(script.size() / 2);
    return bytes + 8 + (bytes < 253 ? 1 : 3);
}
int64_t RateFee(uint32_t rate, uint32_t size)
{
    return (int64_t(rate) * size + 999) / 1000;
}
std::vector<FundingChange> ChangesFrom(const Json& value, std::string_view wallet, protocol::DisplayNetwork network)
{
    const auto chain = protocol::DocumentChain(network);
    const auto policy = ValidateWalletPolicy(value, wallet, chain);
    if (!policy) return {};
    std::vector<FundingChange> native, other;
    for (const auto& branch : policy->branches) {
        if (!branch.classified || !branch.active || !branch.descriptor.internal || !branch.range
            || branch.range->next < branch.range->first || branch.range->next > branch.range->last) continue;
        auto derived=DeriveFundingChange(branch.descriptor.descriptor,branch.range->next,network);
        if (!derived) continue;
        auto& choices=branch.descriptor.choice.script==protocol::ScriptTemplate::NativeSegwit ? native : other;
        choices.push_back(std::move(*derived));
    }
    native.insert(native.end(),std::make_move_iterator(other.begin()),std::make_move_iterator(other.end()));
    std::set<std::string> scripts;
    std::erase_if(native,[&](const auto& choice){ return !scripts.insert(choice.script).second; });
    return native;
}

struct FundingPlan {
    std::map<CoinKey, const FundingCoin*> allowed;
    std::vector<const FundingCoin*> exact;
    std::optional<size_t> max;
    int64_t fixed{}, nominal_max{};
    uint32_t outputs_vsize{};
    bool add_inputs{};
};

FundingPlan Plan(const FundingRequest& request, protocol::DisplayNetwork network, const FundingChange& change,
    std::stop_token stop)
{
    FundingPlan plan;
    Need(!request.recipients.empty() && request.recipients.size() <= MAX_FUNDING_OUTPUTS,
        "Funding requires a bounded recipient set.");
    std::set<std::string> scripts;
    for (size_t n = 0; n < request.recipients.size(); ++n) {
        const auto& row = request.recipients[n];
        Need(protocol::AddressScript(row.address, network) == row.script && row.script != change.script,
            "A recipient conflicts with its expected script or the internal change address.");
        Need(scripts.insert(row.script).second, "This funding RPC requires distinct recipient destinations.");
        plan.outputs_vsize += OutputVsize(row.script);
        if (row.amount) {
            Need(row.amount->sats() > 0, "Recipient amounts must be positive.");
            Add(plan.fixed, *row.amount);
        } else {
            Need(!plan.max, "Only one recipient may use Max.");
            plan.max = n;
        }
    }
    Need(!request.coins.empty() && request.coins.size() <= MAX_FUNDING_INPUTS,
        "Funding requires a bounded set of eligible confirmed inputs.");
    int64_t available{};
    size_t bytes{};
    for (const auto& coin : request.coins) {
        Need(!stop.stop_requested(), "Funding preview cancelled.");
        Need(coin.confirmations > 0, "The exact input is unconfirmed; its ancestor fees are Unknown.");
        bytes += coin.proof.previous_hex.size();
        Need(bytes <= MAX_PROOF_BYTES && coin.amount.sats() > 0, "Previous-output evidence exceeds the funding limits.");
        Need(protocol::ValidOfflineCoin({coin.point, coin.amount, coin.script, coin.proof}, network),
            "An eligible input lacks independently matched previous-output or wallet policy evidence.");
        Need(plan.allowed.emplace(Key(coin.point), &coin).second, "Duplicate eligible funding input.");
        Add(available, coin.amount);
    }
    Need(available > plan.fixed, "Insufficient eligible confirmed funds for these recipients and their fee.");
    plan.add_inputs = request.selection == FundingSelection::Automatic && !plan.max;
    if (plan.add_inputs) return plan;
    if (plan.max || request.selection == FundingSelection::Exact) {
        for (const auto& coin : request.coins) plan.exact.push_back(&coin);
        if (plan.max) plan.nominal_max = available - plan.fixed;
        return plan;
    }

    // The daemon has no exclusion argument. Choose a deterministic eligible
    // subset when local constraints forbid unconstrained automatic selection.
    for (const auto& coin : request.coins) plan.exact.push_back(&coin);
    std::ranges::sort(plan.exact, [](const auto* a, const auto* b) {
        return a->amount != b->amount ? a->amount > b->amount : Key(a->point) < Key(b->point);
    });
    int64_t selected{};
    uint32_t vsize = 12 + plan.outputs_vsize + OutputVsize(change.script);
    for (size_t n = 0; n < plan.exact.size(); ++n) {
        const auto& coin = *plan.exact[n];
        Add(selected, coin.amount);
        vsize += FundingInputVsizeBound(coin.script,coin.proof.redeem_script);
        if (selected - plan.fixed >= RateFee(request.rate_per_kvb, vsize)) {
            plan.exact.resize(n + 1);
            break;
        }
    }
    return plan;
}

std::string FundingArgs(const FundingRequest& request, const FundingPlan& plan, const FundingChange& change)
{
    std::string inputs{"["}, outputs{"["};
    for (const auto* coin : plan.exact) {
        if (inputs.size() > 1) inputs += ',';
        inputs += "{\"txid\":" + rpc::Quote(coin->point.txid.hex()) + ",\"vout\":" + std::to_string(coin->point.index)
            + ",\"sequence\":" + std::to_string(SEQUENCE) + "}";
    }
    for (const auto& row : request.recipients) {
        if (outputs.size() > 1) outputs += ',';
        outputs += "{" + rpc::Quote(row.address) + ":" + row.amount.value_or(Money::Satoshis(plan.nominal_max)).btc() + "}";
    }
    return "{\"inputs\":" + inputs + "],\"outputs\":" + outputs + "],\"locktime\":0,\"bip32derivs\":false,\"version\":2,\"psbt_version\":2,"
        "\"options\":{\"add_inputs\":" + std::string(plan.add_inputs ? "true" : "false") + ",\"include_unsafe\":false,\"minconf\":1,"
        "\"replaceable\":true,\"lockUnspents\":false,\"changeAddress\":" + rpc::Quote(change.address)
        + ",\"changePosition\":" + std::to_string(request.recipients.size())
        + ",\"fee_rate\":" + QuoteFeeRate(request.rate_per_kvb)
        + ",\"subtractFeeFromOutputs\":[" + (plan.max ? std::to_string(*plan.max) : "") + "]}}";
}

FundingPreview Verify(std::string_view encoded, Money reported_fee, int64_t position,
    const FundingRequest& request, const FundingPlan& plan,
    const FundingChange& change, protocol::DisplayNetwork network, std::stop_token stop)
{
    auto document = protocol::ParseTransactionDocument(encoded, network, stop).document;
    Need(document && document->format == protocol::DocumentFormat::PsbtBase64
        && document->psbt_version == 2 && !document->modifiable.value_or(0) && document->transaction,
        "The daemon did not return a supported immutable PSBTv2.");
    const auto& tx = *document->transaction;
    Need(tx.version == 2 && tx.locktime == 0 && !tx.coinbase && !tx.inputs.empty()
        && tx.inputs.size() <= MAX_FUNDING_INPUTS && document->input_policy.size() == tx.inputs.size(),
        "The funded transaction violates the requested version, locktime, or input bounds.");
    const auto count = request.recipients.size();
    const bool has_change = position == int64_t(count);
    Need((position == -1 || (has_change && !plan.max)) && tx.outputs.size() == count + size_t(has_change),
        "The funded transaction added an unexpected output or misplaced change.");
    Need(plan.add_inputs || tx.inputs.size() == plan.exact.size(), "The daemon changed the exact input set.");
    std::set<CoinKey> seen;
    std::vector<protocol::OfflineReviewInput> proofs;
    std::vector<FundingCoin> verified_coins;
    int64_t total_in{}, total_out{};
    uint32_t minimum_vsize = 10 + plan.outputs_vsize, maximum_vsize = 12 + plan.outputs_vsize;
    for (size_t n = 0; n < tx.inputs.size(); ++n) {
        const auto& input = tx.inputs[n];
        const auto& policy = document->input_policy[n];
        const auto key = Key(input.outpoint);
        const auto found = plan.allowed.find(key);
        Need(found != plan.allowed.end() && seen.insert(key).second, "The daemon returned an input outside the eligible coin policy.");
        const auto& coin = *found->second;
        Need((plan.add_inputs || plan.exact[n]->point == input.outpoint) && input.sequence == SEQUENCE,
            "The daemon changed an exact pin, input order, or sequence.");
        Need(!policy.final_fields && policy.signature_modes.empty() && (!policy.sighash || *policy.sighash == 1
            || (*policy.sighash == 0 && coin.script.starts_with("5120"))), "The daemon returned signature data or unexpected sighash policy.");
        for (const auto& [field, value] : policy.scripts) {
            if (field == std::string_view("\x04", 1))
                Need(protocol::TransactionHex(value) == coin.proof.redeem_script, "The PSBT redeem script conflicts with wallet policy.");
            if (field == std::string_view("\x05", 1))
                Need(protocol::TransactionHex(value) == coin.proof.witness_script, "The PSBT witness script conflicts with wallet policy.");
            Need(field.empty() || (uint8_t(field.front()) != 0x15 && uint8_t(field.front()) != 0x18),
                "The PSBT supplied unsupported Taproot script-path policy.");
        }
        Need(input.evidence != protocol::PrevoutEvidence::Conflict && input.previous_value == coin.amount
            && input.previous_script == coin.script, "The PSBT previous output conflicts with independently observed wallet evidence.");
        Add(total_in, coin.amount);
        minimum_vsize += 41;
        maximum_vsize += FundingInputVsizeBound(coin.script,coin.proof.redeem_script);
        proofs.push_back({coin.point, coin.amount, coin.proof.descriptor});
        if (request.recheck) verified_coins.push_back(coin);
    }
    if (has_change) {
        const auto& output = tx.outputs.back();
        Need(output.value.sats() > 0 && output.script == change.script, "The change output does not match verified internal wallet policy.");
        minimum_vsize += OutputVsize(output.script);
        maximum_vsize += OutputVsize(output.script);
    }
    for (const auto& output : tx.outputs) Add(total_out, output.value);
    Need(total_in >= total_out && reported_fee.sats() == total_in - total_out && (!tx.fee || tx.fee == reported_fee),
        "Input and output values do not reconcile with the returned fee.");
    // The signed size is not known yet. Bound the fee by proved input script
    // satisfactions, with at most 1000 sat for a no-change fixed-payment excess.
    const auto ceiling = RateFee(request.rate_per_kvb, maximum_vsize) + (has_change || plan.max ? 0 : 1000);
    Need(reported_fee.sats() >= RateFee(request.rate_per_kvb, minimum_vsize) && reported_fee.sats() <= ceiling,
        "The funded fee exceeds the permitted size-based range for this rate.");
    std::optional<Money> maximum;
    for (size_t n = 0; n < request.recipients.size(); ++n) {
        const auto& recipient = request.recipients[n];
        auto amount = recipient.amount;
        if (!amount) {
            const int64_t max_sats = total_in - plan.fixed - reported_fee.sats();
            Need(max_sats > 0, "No eligible funds remain for Max after fees and fixed outputs.");
            maximum = amount = Money::Satoshis(max_sats);
        }
        Need(tx.outputs[n].script == recipient.script && tx.outputs[n].value == *amount,
            "The funded transaction changed a recipient script, order, or amount.");
    }
    return {std::move(document), std::move(proofs), has_change ? std::optional(change) : std::nullopt,
        Money::Satoshis(total_in), reported_fee, maximum, std::move(verified_coins)};
}
}

uint32_t FundingInputVsizeBound(std::string_view script,std::string_view redeem)
{
    if (script.starts_with("76a914")) return 182; // compressed or uncompressed public key
    if (script.starts_with("0014")) return 70;
    if (script.starts_with("5120")) return 60;
    // All remaining supported input policies were independently checked as
    // standard public single-key or up-to-15-key multisig. These bound the
    // serialized satisfaction, including redeem/witness scripts and framing.
    if (script.starts_with("0020")) return 460;
    if (redeem.starts_with("0014")) return 95;
    if (redeem.starts_with("0020")) return 500;
    return 1720;
}

std::optional<FundingChange> DeriveFundingChange(std::string_view descriptor,uint32_t index,protocol::DisplayNetwork network)
{
    if (index>=0x80000000U || descriptor.empty() || descriptor.size()>4096) return {};
    const auto derived=protocol::DerivePublicDescriptor(descriptor,protocol::DocumentChain(network),index);
    if (!derived || protocol::AddressScript(derived->address,network)!=derived->script) return {};
    return FundingChange{derived->address,derived->script,std::string(descriptor),index};
}

std::optional<uint32_t> ParseFundingRate(std::string_view text)
{
    const auto rate = FeeRate::Parse(text);
    if (!rate || !rate->numerator() || rate->numerator() > MAX_FUNDING_RATE * uint64_t(rate->denominator())
        || 1000 % rate->denominator()) return {};
    const auto scaled = rate->numerator() * (1000 / rate->denominator());
    if (scaled > MAX_FUNDING_RATE) return {};
    return uint32_t(scaled);
}
bool CanFund(const BackendCapabilities& caps)
{
    if (!CanReadWalletPolicy(caps) || caps.Supports("gettxout") != SupportReason::Available
        || caps.Supports("getbestblockhash") != SupportReason::Available
        || caps.Supports("listlockunspent") != SupportReason::Available) return false;
    constexpr std::string_view args[] = {"inputs", "outputs", "locktime", "bip32derivs", "version", "psbt_version",
        "options.add_inputs", "options.include_unsafe", "options.minconf", "options.replaceable",
        "options.lockUnspents", "options.changeAddress", "options.changePosition", "options.fee_rate",
        "options.subtractFeeFromOutputs"};
    return std::ranges::all_of(args, [&](auto arg) {
        return caps.Supports("walletcreatefundedpsbt", arg) == SupportReason::Available;
    });
}

FundingReply FetchFunding(rpc::Transport& transport, const FundingRequest& request, std::stop_token stop)
{
    try {
        const auto& profile = request.scope.wallet.profile;
        const auto& context = request.scope.wallet.context;
        const auto network = protocol::DocumentNetwork(context.network);
        const auto& genesis = context.network.genesis.hex();
        const auto tip = Hash256::Parse(request.scope.tip);
        const auto current_rate = [&] {
            const auto observed = request.scope.fee_observation;
            const auto now = RefreshMonotonicMs();
            return request.rate_per_kvb && request.rate_per_kvb <= MAX_FUNDING_RATE
                && request.scope.fee_rate == request.rate_per_kvb
                && (!observed || (observed > 0 && observed <= now && now - observed <= FEE_SAMPLE_INTERVAL));
        };
        Need(profile.wallet && ValidServerCandidate(profile) && profile.expected_genesis == genesis
            && tip && tip->hex() == request.scope.tip && network != protocol::DisplayNetwork::Unknown
            && current_rate(), "Funding request lacks exact scoped fee or wallet evidence.");
        const auto deadline = Clock::now() + std::chrono::seconds(12);
        uint64_t id{};
        const auto call = [&](std::string method, std::string args, bool wallet = false) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
            Need(!stop.stop_requested() && left > std::chrono::milliseconds(0), "Funding preview cancelled or expired.");
            rpc::Limits limits;
            limits.total_timeout = left;
            limits.connect_timeout = std::min(left, std::chrono::milliseconds(3000));
            limits.response_bytes = 1024 * 1024;
            // An explicit independently derived change address suppresses key
            // allocation; lockUnspents is always false. This is a preview.
            return transport.Call(profile.connection, {++id, std::move(method), std::move(args),
                wallet ? profile.wallet : std::nullopt, rpc::Effect::ReadOnly}, stop, limits);
        };
        const auto checked = [&](std::string method, std::string args, bool wallet, const char* error) {
            auto reply = call(std::move(method), std::move(args), wallet);
            Need(reply.error == rpc::Error::None && reply.result && !stop.stop_requested(), error);
            return std::move(*reply.result);
        };
        const auto scope = [&] {
            const auto g = checked("getblockhash", "[0]", false, "Authenticated genesis is unavailable.");
            const auto best = checked("getbestblockhash", "[]", false, "Verified chain tip is unavailable.");
            const auto info = checked("getwalletinfo", "[]", true, "Current wallet identity is unavailable.");
            const auto* descriptors = info.Find("descriptors");
            Need(StringIs(&g, genesis) && StringIs(&best, request.scope.tip) && StringIs(info.Find("walletname"), *profile.wallet)
                && descriptors && descriptors->kind == Json::Kind::Boolean && descriptors->text == "true",
                "Chain or actual daemon wallet changed. Refresh before reviewing funding.");
        };
        const auto next_changes = [&] {
            return ChangesFrom(checked("listdescriptors", "{\"private\":false}", true,
                "Verified public internal change policy is unavailable."), *profile.wallet, network);
        };
        scope();
        auto choices=next_changes();
        Need(!choices.empty(), "No supported active internal descriptor can prove a preview change address.");
        const auto selected=request.requested_change
            ? std::ranges::find(choices,*request.requested_change) : choices.begin();
        Need(selected!=choices.end(), "The selected internal change proof changed. Choose default or refresh your preference.");
        const auto& change=*selected;
        const auto plan = Plan(request, network, change, stop);
        Need(current_rate(), "The fee observation expired. Read current fee data before funding.");
        auto preview = [&] {
            if (const auto& prior = request.recheck) {
                Need(request.selection == FundingSelection::Exact && prior->psbt
                    && (!prior->change || prior->change == change), "The accepted inputs or internal change policy changed.");
                auto rechecked = Verify(prior->psbt->source, prior->fee,
                    prior->change ? int64_t(request.recipients.size()) : -1, request, plan, change, network, stop);
                Need(rechecked.maximum == prior->maximum && rechecked.total_inputs == prior->total_inputs,
                    "The accepted funding totals changed.");
                return rechecked;
            }
            const auto result = checked("walletcreatefundedpsbt", FundingArgs(request, plan, change), true,
                "Wallet declined the unsigned funding preview. Check amount, dust, confirmed inputs and fee rate.");
            const auto* encoded = result.Find("psbt");
            const auto fee = Amount(result.Find("fee"));
            const auto position = Integer(result.Find("changepos"));
            Need(encoded && encoded->kind == Json::Kind::String && fee && position, "Malformed funding response.");
            return Verify(encoded->text, *fee, *position, request, plan, change, network, stop);
        }();
        const auto locks = checked("listlockunspent", "[]", true, "Current daemon coin lock evidence is unavailable.");
        Need(locks.kind == Json::Kind::Array && locks.items.size() <= 8192, "Malformed current daemon coin lock evidence.");
        std::set<CoinKey> locked;
        for (const auto& lock : locks.items) {
            const auto* txid = lock.Find("txid");
            const auto index = Integer(lock.Find("vout"));
            Need(txid && txid->kind == Json::Kind::String && index && *index >= 0 && *index <= UINT32_MAX,
                "Malformed current daemon coin lock evidence.");
            const auto hash = Hash256::Parse(txid->text);
            Need(hash && hash->hex() == txid->text, "Malformed current daemon coin lock evidence.");
            locked.emplace(txid->text, uint32_t(*index));
        }
        for (const auto& input : preview.inputs) {
            const auto key = Key(input.point);
            Need(!locked.contains(key), "A selected coin has a new daemon lock.");
            const auto current = checked("gettxout", "[" + rpc::Quote(key.first) + "," + std::to_string(key.second) + ",true]",
                false, "Current selected output evidence is unavailable.");
            const auto* script = current.Find("scriptPubKey");
            const auto& coin = *plan.allowed.at(key);
            Need(StringIs(current.Find("bestblock"), request.scope.tip) && Integer(current.Find("confirmations")).value_or(0) > 0
                && Amount(current.Find("value")) == coin.amount && script && StringIs(script->Find("hex"), coin.script),
                "A selected output is spent, has unknown ancestors, or changed value or script.");
        }
        Need(next_changes() == choices, "Internal change policy changed during the preview. Request a fresh preview.");
        scope();
        Need(current_rate(), "The fee observation expired. Read current fee data before funding.");
        std::erase_if(choices,[&](const auto& candidate) {
            return std::ranges::any_of(request.recipients,[&](const auto& row){return row.script==candidate.script;});
        });
        preview.change_choices=std::move(choices);
        preview.flow=DescribeFunding(preview,network);
        Need(preview.flow.has_value(),"Verified funding could not produce a complete transaction flow.");
        return {std::move(preview), {}};
    } catch (const std::runtime_error& error) {
        return {{}, error.what()};
    }
}

FundingService::~FundingService() { Cancel(); }
bool FundingService::Start(FundingRequest request)
{
    if (std::this_thread::get_id() != owner || Busy() || !fetch) return false;
    running = request.scope;
    worker = std::jthread([this, request = std::move(request)](std::stop_token stop) {
        FundingReply result;
        try { result = fetch(request, stop); } catch (...) { result.error = "Funding preview is unavailable."; }
        std::lock_guard lock(mutex);
        completed = std::move(result);
    });
    return true;
}
void FundingService::Cancel()
{
    if (std::this_thread::get_id() != owner) return;
    running.reset();
    if (Busy()) worker.request_stop();
}
std::optional<FundingReply> FundingService::Poll(const std::optional<FundingScope>& scope)
{
    if (std::this_thread::get_id() != owner || !Busy()) return {};
    if (running != scope) Cancel();
    std::optional<FundingReply> result;
    {
        std::lock_guard lock(mutex);
        if (!completed) return {};
        result = std::move(completed);
        completed.reset();
    }
    worker.join();
    const bool current = running && running == scope;
    running.reset();
    return current ? std::move(result) : std::nullopt;
}
}
