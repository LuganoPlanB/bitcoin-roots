// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/transaction.h"
#include <wally_address.h>
#include <wally_core.h>
#include <wally_crypto.h>
#include <wally_psbt.h>
#include <wally_transaction.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <set>
#include <map>
#include <utility>

namespace roots::gui::protocol {
namespace {
using Error = DocumentError;
using Bytes = std::string_view;
constexpr Bytes PSBT_MAGIC{"psbt\xff",5};
constexpr uint64_t MAX_MONEY = 2100000000000000;
constexpr size_t MAX_SCRIPT = 10000, MAX_FIELDS = 4096, MAX_STACK = 1024, MAX_DEPTH = 128;
constexpr Bytes SPACE = " \t\r\n";
using Strings = std::unique_ptr<char, decltype(&wally_free_string)>;
using Tx = std::unique_ptr<wally_tx, decltype(&wally_tx_free)>;
using Psbt = std::unique_ptr<wally_psbt, decltype(&wally_psbt_free)>;

const unsigned char* Data(Bytes bytes)
{
    return reinterpret_cast<const unsigned char*>(bytes.data());
}
Bytes Data(const unsigned char* bytes, size_t length)
{
    return length ? Bytes(reinterpret_cast<const char*>(bytes),length) : Bytes{};
}
void Require(bool valid, Error error = Error::Malformed)
{
    if (!valid) throw error;
}
class Reader {
    Bytes bytes;
    size_t pos{};
public:
    explicit Reader(Bytes bytes) : bytes(bytes) {}
    bool Empty() const {return pos == bytes.size();}
    size_t Offset() const {return pos;}
    Bytes Range(size_t start) const {return bytes.substr(start,pos-start);}
    Bytes Take(size_t n)
    {
        Require(n <= bytes.size()-pos);
        const auto value = bytes.substr(pos,n);
        pos += n;
        return value;
    }
    uint64_t LE(size_t length)
    {
        uint64_t value{};
        unsigned shift{};
        for (unsigned char ch : Take(length)) {value |= uint64_t(ch)<<shift; shift+=8;}
        return value;
    }
    uint64_t Compact(uint64_t max = UINT64_MAX)
    {
        uint64_t first = LE(1);
        if (first>=253) {
            const auto n = LE(size_t{2}<<(first-253));
            const uint64_t min = first==253 ? 253 : first==254 ? 65536 : uint64_t{1}<<32;
            Require(n>=min);
            first=n;
        }
        Require(first<=max,Error::Limit);
        return first;
    }
    Bytes Vector(size_t max) {return Take(size_t(Compact(max)));}
};
class Budget {
    std::stop_token stop;
    std::chrono::steady_clock::time_point deadline;
public:
    explicit Budget(std::stop_token stop) : stop(stop), deadline(std::chrono::steady_clock::now()+std::chrono::seconds(2)) {}
    void Check() const
    {
        Require(!stop.stop_requested(), Error::Cancelled);
        Require(std::chrono::steady_clock::now()<deadline, Error::Limit);
    }
};
Bytes Trim(Bytes text)
{
    const auto start=text.find_first_not_of(SPACE);
    if (start==text.npos) return {};
    return text.substr(start,text.find_last_not_of(SPACE)-start+1);
}
bool IsHex(Bytes s)
{
    return !s.empty() && std::ranges::all_of(s, [](unsigned char c) {
        return (c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F');
    });
}
std::optional<std::string> Unhex(Bytes text,size_t max)
{
    if (text.empty() || text.size()%2 || text.size()/2>max || !IsHex(text)) return {};
    std::string result(text.size()/2,'\0');
    size_t written{};
    const std::string nul(text);
    if (wally_hex_to_bytes(nul.c_str(),reinterpret_cast<unsigned char*>(result.data()),result.size(),&written)!=WALLY_OK
        || written!=result.size()) return {};
    return result;
}
std::optional<Hash256> Txid(const wally_tx& tx)
{
    std::array<unsigned char,32> raw{};
    if (wally_tx_get_txid(&tx,raw.data(),raw.size())!=WALLY_OK) return {};
    std::ranges::reverse(raw);
    return Hash256::Parse(TransactionHex(Data(raw.data(),raw.size())));
}
Hash256 InputHash(const unsigned char* little)
{
    std::array<unsigned char,32> raw{};
    std::reverse_copy(little,little+raw.size(),raw.begin());
    return *Hash256::Parse(TransactionHex(Data(raw.data(),raw.size())));
}
bool NullHash(Bytes hash)
{
    return std::ranges::all_of(hash,[](char c){return c==0;});
}
void MoneyTotal(uint64_t& total, uint64_t amount)
{
    Require(amount<=MAX_MONEY-total);
    total+=amount;
}
void Mark(std::vector<InspectionField>* fields,std::string name, size_t start, const Reader& r,size_t base)
{
    if (fields) fields->push_back({std::move(name),{base+start,r.Offset()-start}});
}
bool WitnessStack(Reader& r,std::vector<InspectionField>* fields=nullptr,size_t base=0)
{
    const auto start=r.Offset();
    const auto items=r.Compact(MAX_STACK);
    Mark(fields,"Witness item count",start,r,base);
    for (uint64_t j=0;j<items;++j) {
        const auto length=r.Compact(MAX_TRANSACTION_WIRE);
        const auto item=r.Offset();
        r.Take(size_t(length));
        if (fields) Mark(fields,"Witness item "+std::to_string(j+1),item,r,base);
    }
    return items!=0;
}
struct TxShape {size_t inputs{},outputs{};bool witness{},coinbase{};};
TxShape CheckTx(Bytes bytes,const Budget& budget,bool unsigned_psbt,InspectionLayout* layout=nullptr,size_t base=0)
{
    Reader r(bytes);
    auto* header=layout ? &layout->header : nullptr;
    r.LE(4);
    Mark(header,"Version",0,r,base);
    auto count_start=r.Offset();
    auto inputs=r.Compact(MAX_TRANSACTION_IO);
    bool witness{};
    if (!unsigned_psbt && !inputs) {
        Require(r.LE(1)==1);
        Mark(header,"Witness marker and flag",count_start,r,base);
        count_start=r.Offset();
        inputs=r.Compact(MAX_TRANSACTION_IO);
        witness=true;
    }
    Mark(header,"Input count",count_start,r,base);
    if (layout) layout->inputs.resize(inputs);
    Require(unsigned_psbt || inputs>0);
    std::set<std::pair<Bytes,uint32_t>> seen;
    bool coinbase{};
    for (uint64_t i=0;i<inputs;++i) {
        budget.Check();
        auto* fields=layout ? &layout->inputs[i] : nullptr;
        auto start=r.Offset();
        const auto hash=r.Take(32);
        Mark(fields,"Previous transaction ID (wire order)",start,r,base);
        start=r.Offset();
        const auto index=uint32_t(r.LE(4));
        Mark(fields,"Previous output index",start,r,base);
        start=r.Offset();
        const auto length=r.Compact(MAX_SCRIPT);
        Mark(fields,"scriptSig length",start,r,base);
        start=r.Offset();
        const auto script=r.Take(size_t(length));
        Mark(fields,"scriptSig",start,r,base);
        start=r.Offset();
        r.LE(4);
        Mark(fields,"Sequence",start,r,base);
        Require(seen.emplace(hash,index).second,Error::Duplicate);
        if (unsigned_psbt) Require(script.empty());
        if (NullHash(hash) && index==UINT32_MAX) {
            Require(inputs==1);
            coinbase=true;
        }
    }
    count_start=r.Offset();
    const auto outputs=r.Compact(MAX_TRANSACTION_IO);
    Mark(header,"Output count",count_start,r,base);
    if (layout) layout->outputs.resize(outputs);
    Require(unsigned_psbt || outputs>0);
    uint64_t total{};
    for (uint64_t i=0;i<outputs;++i) {
        budget.Check();
        auto* fields=layout ? &layout->outputs[i] : nullptr;
        auto start=r.Offset();
        MoneyTotal(total,r.LE(8));
        Mark(fields,"Output amount",start,r,base);
        start=r.Offset();
        const auto length=r.Compact(MAX_SCRIPT);
        Mark(fields,"scriptPubKey length",start,r,base);
        start=r.Offset();
        r.Take(size_t(length));
        Mark(fields,"scriptPubKey",start,r,base);
    }
    if (witness) {
        bool nonempty{};
        for (uint64_t i=0;i<inputs;++i) {
            budget.Check();
            nonempty=WitnessStack(r,layout ? &layout->inputs[i] : nullptr,base) || nonempty;
        }
        Require(nonempty);
    }
    const auto lock_start=r.Offset();
    r.LE(4);
    Mark(header,"Locktime",lock_start,r,base);
    Require(r.Empty());
    return {size_t(inputs),size_t(outputs),witness,coinbase};
}
uint64_t OneLE(Bytes bytes, size_t width)
{
    Require(bytes.size()==width);
    return Reader(bytes).LE(width);
}
uint64_t OneCompact(Bytes bytes, uint64_t max)
{
    Reader reader(bytes);
    const auto value=reader.Compact(max);
    Require(reader.Empty());
    return value;
}
enum class MapKind { Global, Input, Output };
struct MapShape {
    std::optional<TxShape> transaction;
    std::optional<size_t> inputs, outputs;
    std::vector<PsbtPolicy> input_policy, output_policy;
    InspectionLayout layout;
    std::vector<std::optional<ByteSpan>> full_previous;
    std::optional<uint8_t> modifiable;
    uint32_t version{};
    size_t entries{};
};
std::string PsbtFieldName(MapKind kind,uint64_t type)
{
    using Names=std::initializer_list<std::pair<uint64_t,std::string_view>>;
    const auto name=[type](Names names) {
        for (const auto& [key,label]:names) if (type==key) return label;
        return std::string_view{};
    };
    const auto known=kind==MapKind::Global ? name({{0,"Embedded unsigned transaction"},{1,"Global extended public key"},
        {2,"Transaction version"},{3,"Fallback locktime"},{4,"Input count"},{5,"Output count"},
        {6,"Transaction modifiable flags"},{0xfb,"PSBT version"}})
        : kind==MapKind::Input ? name({{0,"Full previous transaction"},{1,"Witness previous-output data"},
            {2,"Partial signature"},{3,"Requested sighash"},{4,"Redeem script"},{5,"Witness script"},
            {6,"Public key derivation"},{7,"Final scriptSig"},{8,"Final serialized witness"},
            {0x0e,"Previous transaction ID (wire order)"},{0x0f,"Previous output index"},{0x10,"Sequence"},
            {0x11,"Required time locktime"},{0x12,"Required height locktime"},{0x13,"Taproot key-path signature"},
            {0x14,"Taproot script-path signature"},{0x15,"Taproot leaf script and version"},
            {0x16,"Taproot public key derivation"},{0x17,"Taproot internal key"},{0x18,"Taproot merkle root"}})
        : name({{0,"Redeem script"},{1,"Witness script"},{2,"Public key derivation"},{3,"Output amount"},
            {4,"scriptPubKey"},{5,"Taproot internal key"},{6,"Taproot tree"},{7,"Taproot public key derivation"}});
    if (!known.empty()) return std::string(known);
    if (type==0xfc) return "Proprietary field";
    return "Opaque field type "+std::to_string(type);
}
void CheckTapPath(Bytes value)
{
    Reader r(value);
    const auto hashes=r.Compact(MAX_STACK);
    r.Take(size_t(hashes)*32);
    r.Take(4);
    while (!r.Empty()) r.Take(4);
}
void CheckTapTree(Bytes value)
{
    Reader r(value);
    size_t leaves{}, pending{};
    std::array<uint8_t,MAX_DEPTH+2> depths{};
    while (!r.Empty()) {
        const auto depth=r.LE(1);
        Require(++leaves<=MAX_FIELDS && depth<=MAX_DEPTH,Error::Limit);
        Require(pending<depths.size() && (!pending || depths[0]!=0));
        r.LE(1);
        r.Vector(MAX_SCRIPT);
        depths[pending++]=uint8_t(depth);
        // Depth-first leaves must reduce to exactly one depth-zero root.
        while (pending>=2 && depths[pending-1]==depths[pending-2]) {
            --pending;
            Require(depths[pending-1]>0);
            --depths[pending-1];
        }
    }
    Require(pending==1 && depths[0]==0);
}
void CheckMap(Reader& r,MapShape& shape,MapKind map,const Budget& budget,
    std::vector<InspectionField>& fields,PsbtPolicy* policy=nullptr,std::optional<ByteSpan>* previous=nullptr)
{
    std::set<Bytes> seen;
    for (;;) {
        budget.Check();
        const auto start=r.Offset();
        const auto key=r.Vector(MAX_TRANSACTION_WIRE);
        if (key.empty()) return;
        const ByteSpan key_bytes{r.Offset()-key.size(),key.size()};
        Require(++shape.entries<=MAX_FIELDS,Error::Limit);
        Require(seen.insert(key).second,Error::Duplicate);
        Reader key_reader(key);
        const auto type=key_reader.Compact();
        if (type==0xfc) {
            key_reader.Vector(MAX_TRANSACTION_WIRE);
            key_reader.Compact(); // proprietary subtype; payload is deliberately opaque
        }
        const auto value=r.Vector(MAX_TRANSACTION_WIRE);
        const auto prefix=PsbtFieldName(map,type);
        const auto value_offset=r.Offset()-value.size();
        fields.push_back({prefix+" (serialized PSBT entry)",{start,r.Offset()-start}});
        fields.push_back({prefix+" (key)",key_bytes});
        fields.push_back({prefix,{value_offset,value.size()}});
        if (map==MapKind::Global) {
            if (type==0xfb && key_reader.Empty()) shape.version=uint32_t(OneLE(value,4));
            if (type==0 && key_reader.Empty()) shape.transaction=CheckTx(value,budget,true,&shape.layout,value_offset);
            if (type==1) {
                // PSBT global extended keys are public BIP32 serializations.
                Require(key.size()==79 && (key[46]==2 || key[46]==3));
            }
            if (type==4 && key_reader.Empty()) shape.inputs=size_t(OneCompact(value,MAX_TRANSACTION_IO));
            if (type==5 && key_reader.Empty()) shape.outputs=size_t(OneCompact(value,MAX_TRANSACTION_IO));
            if (type==6 && key_reader.Empty()) shape.modifiable=uint8_t(OneLE(value,1));
        } else if (map==MapKind::Input) {
            if ((type == 7 || type == 8) && key_reader.Empty()) policy->final_fields = true;
            if (type==2 || type==0x13 || type==0x14) policy->signature_fields=true;
            if (type==3 && key_reader.Empty()) policy->sighash=uint32_t(OneLE(value,4));
            if (type==2 && !value.empty()) policy->signature_modes.insert(uint8_t(value.back()));
            if ((type==0x13 || type==0x14) && (value.size()==64 || value.size()==65))
                policy->signature_modes.insert(value.size()==64 ? 0 : uint8_t(value.back()));
            if (type==4 || type==5 || type==0x15 || type==0x17 || type==0x18)
                policy->scripts.emplace(key,value);
            if (type==0 && key_reader.Empty()) {
                CheckTx(value,budget,false);
                *previous=ByteSpan{value_offset,value.size()};
            }
            if (type==1 && key_reader.Empty()) {
                Reader output(value);
                Require(output.LE(8)<=MAX_MONEY);
                Mark(&fields,"Supplied previous amount",0,output,value_offset);
                const auto length=output.Compact(MAX_SCRIPT);
                const auto output_script=output.Offset();
                output.Take(size_t(length));
                Mark(&fields,"Supplied previous scriptPubKey",output_script,output,value_offset);
                Require(output.Empty());
            }
            if (type==8 && key_reader.Empty()) {
                Reader witness(value);
                WitnessStack(witness,&fields,value_offset);
                Require(witness.Empty());
            }
            if (type==0x16) CheckTapPath(value);
        } else {
            if (type==0 || type==1 || type==5 || type==6)
                policy->scripts.emplace(key,value);
            if (type==0x04) Require(value.size()<=MAX_SCRIPT,Error::Limit);
            if (type==0x06) CheckTapTree(value);
            if (type==0x07) CheckTapPath(value);
        }
    }
}
MapShape CheckPsbt(Bytes wire,const Budget& budget)
{
    Reader r(wire);
    Require(r.Take(PSBT_MAGIC.size())==PSBT_MAGIC);
    MapShape shape;
    shape.layout.header.push_back({"PSBT magic",{0,PSBT_MAGIC.size()}});
    CheckMap(r,shape,MapKind::Global,budget,shape.layout.header);
    if (shape.version!=0 && shape.version!=2) return shape;
    if (shape.version==0) {
        Require(bool(shape.transaction));
        shape.inputs=shape.transaction->inputs;
        shape.outputs=shape.transaction->outputs;
    }
    Require(shape.inputs && shape.outputs);
    shape.input_policy.resize(*shape.inputs);
    shape.output_policy.resize(*shape.outputs);
    shape.layout.inputs.resize(*shape.inputs);
    shape.layout.outputs.resize(*shape.outputs);
    shape.full_previous.resize(*shape.inputs);
    for (size_t i=0;i<*shape.inputs;++i)
        CheckMap(r,shape,MapKind::Input,budget,shape.layout.inputs[i],&shape.input_policy[i],&shape.full_previous[i]);
    for (size_t i=0;i<*shape.outputs;++i)
        CheckMap(r,shape,MapKind::Output,budget,shape.layout.outputs[i],&shape.output_policy[i]);
    Require(r.Empty());
    return shape;
}
TransactionOutput Output(uint64_t amount,const unsigned char* script,size_t length)
{
    Require(amount<=MAX_MONEY && length<=MAX_SCRIPT);
    return {Money::Satoshis(int64_t(amount)),TransactionHex(Data(script,length))};
}
const wally_tx_output* CheckPrevout(const TransactionInput& in,const wally_psbt_input& psbt)
{
    if (!psbt.utxo || in.outpoint.index>=psbt.utxo->num_outputs || Txid(*psbt.utxo)!=in.outpoint.txid) return nullptr;
    return &psbt.utxo->outputs[in.outpoint.index];
}
void DetailPrevout(TransactionInput& in,const wally_psbt_input& data)
{
    const auto* full=CheckPrevout(in,data);
    const auto* witness=data.witness_utxo;
    if ((data.utxo && !full) || (full && witness
        && (full->satoshi!=witness->satoshi || Data(full->script,full->script_len)!=Data(witness->script,witness->script_len)))) {
        in.evidence=PrevoutEvidence::Conflict;
        return;
    }
    const auto* previous=full ? full : witness;
    if (!previous) return;
    auto detail=Output(previous->satoshi,previous->script,previous->script_len);
    in.previous_value=detail.value;
    in.previous_script=std::move(detail.script);
    in.evidence=full ? PrevoutEvidence::TxidMatched : PrevoutEvidence::Supplied;
}
void ReadTx(TransactionDocument& document,const Budget& budget)
{
    const auto shape=CheckTx(document.wire,budget,false,&document.wire_layout);
    wally_tx* raw{};
    const auto parsed=wally_tx_from_bytes(Data(document.wire),document.wire.size(),WALLY_TX_FLAG_USE_WITNESS,&raw);
    Tx tx(raw,&wally_tx_free);
    Require(parsed==WALLY_OK && tx && tx->num_inputs==shape.inputs && tx->num_outputs==shape.outputs);
    auto& data=document.transaction.emplace();
    data.version=tx->version;
    data.locktime=tx->locktime;
    data.txid=Txid(*tx);
    Require(data.txid.has_value());
    std::array<unsigned char,32> witness_id{};
    if (wally_sha256d(Data(document.wire),document.wire.size(),witness_id.data(),witness_id.size())==WALLY_OK) {
        std::ranges::reverse(witness_id);
        data.wtxid=Hash256::Parse(TransactionHex(Data(witness_id.data(),witness_id.size())));
    }
    size_t stripped{};
    if (wally_tx_get_length(tx.get(),0,&stripped)==WALLY_OK && stripped && stripped<=document.wire.size()
        && (shape.witness || stripped==document.wire.size())) {
        const size_t weight=3*stripped+document.wire.size();
        data.sizes=RawTransactionSizes{stripped,weight,(weight+3)/4};
    }
    data.witness=shape.witness;
    data.coinbase=shape.coinbase;
    for (size_t i=0;i<tx->num_inputs;++i) {
        budget.Check();
        const auto& input=tx->inputs[i];
        data.script_sig|=input.script_len!=0;
        data.inputs.push_back({{InputHash(input.txhash),input.index},input.sequence});
    }
    for (size_t i=0;i<tx->num_outputs;++i) {
        budget.Check();
        const auto& output=tx->outputs[i];
        data.outputs.push_back(Output(output.satoshi,output.script,output.script_len));
    }
}
void ReadPsbt(TransactionDocument& document, MapShape shape,const Budget& budget)
{
    wally_psbt* raw{};
    const auto parsed=wally_psbt_from_bytes(Data(document.wire),document.wire.size(),
        WALLY_PSBT_PARSE_FLAG_STRICT | WALLY_PSBT_PARSE_FLAG_COMPLETE,&raw);
    Psbt psbt(raw,&wally_psbt_free);
    Require(parsed==WALLY_OK && psbt && psbt->version==shape.version && psbt->num_inputs==shape.inputs && psbt->num_outputs==shape.outputs);
    auto& data=document.transaction.emplace();
    document.psbt_version=shape.version;
    document.psbt_entries=shape.entries;
    document.modifiable=shape.modifiable;
    document.wire_layout=std::move(shape.layout);
    document.input_policy=std::move(shape.input_policy);
    document.output_policy=std::move(shape.output_policy);
    Require(shape.version || psbt->tx);
    data.version=shape.version==0 ? psbt->tx->version : psbt->tx_version;
    if (!shape.version) data.locktime=psbt->tx->locktime;
    wally_tx* witness_free{};
    const auto extracted=wally_psbt_extract(psbt.get(),WALLY_PSBT_EXTRACT_NON_FINAL,&witness_free);
    Tx tx(witness_free,&wally_tx_free);
    if (extracted==WALLY_OK && tx) {
        if (shape.version) data.locktime=tx->locktime;
        size_t size{},written{};
        if (wally_tx_get_length(tx.get(),0,&size)==WALLY_OK && size>0 && size<=MAX_TRANSACTION_WIRE) {
            std::string unsigned_wire(size,'\0');
            if (wally_tx_to_bytes(tx.get(),0,reinterpret_cast<unsigned char*>(unsigned_wire.data()),size,&written)==WALLY_OK
                && written==size) {
                CheckTx(unsigned_wire,budget,true,&document.unsigned_layout);
                document.unsigned_wire=std::move(unsigned_wire);
            }
        }
    }
    std::set<std::pair<std::string,uint32_t>> distinct;
    uint64_t total_in{},total_out{};
    bool matched=psbt->num_inputs>0;
    for (size_t i=0;i<psbt->num_inputs;++i) {
        budget.Check();
        const auto& part=psbt->inputs[i];
        const auto* legacy=shape.version==0 ? &psbt->tx->inputs[i] : nullptr;
        TransactionInput input{{InputHash(legacy ? legacy->txhash : part.txhash),legacy ? legacy->index : part.index},
                               legacy ? legacy->sequence : part.sequence};
        Require(distinct.emplace(input.outpoint.txid.hex(),input.outpoint.index).second,Error::Duplicate);
        DetailPrevout(input,part);
        if (input.evidence==PrevoutEvidence::TxidMatched && shape.full_previous[i]) {
            const auto span=*shape.full_previous[i];
            InspectionLayout previous;
            CheckTx(Bytes(document.wire).substr(span.offset,span.length),budget,false,&previous,span.offset);
            for (const auto& field:previous.outputs.at(input.outpoint.index)) {
                if (field.name=="Output amount" || field.name=="scriptPubKey")
                    document.wire_layout.inputs[i].push_back({"Matched previous "+field.name,field.bytes});
            }
        }
        matched &= input.evidence==PrevoutEvidence::TxidMatched;
        if (input.evidence==PrevoutEvidence::TxidMatched) MoneyTotal(total_in,uint64_t(input.previous_value->sats()));
        data.inputs.push_back(std::move(input));
    }
    for (size_t i=0;i<psbt->num_outputs;++i) {
        budget.Check();
        const auto* legacy=shape.version==0 ? &psbt->tx->outputs[i] : nullptr;
        const auto& part=psbt->outputs[i];
        auto output=legacy ? Output(legacy->satoshi,legacy->script,legacy->script_len)
                           : Output(part.amount,part.script,part.script_len);
        MoneyTotal(total_out,uint64_t(output.value.sats()));
        data.outputs.push_back(std::move(output));
    }
    if (matched && total_in>=total_out) data.fee=Money::Satoshis(int64_t(total_in-total_out));
}
bool LooksLikeDescriptor(Bytes text)
{
    for (Bytes prefix:{"pkh(","wpkh(","sh(","wsh(","tr("}) if (text.starts_with(prefix)) return true;
    return false;
}
uint32_t AddressNetwork(DisplayNetwork network)
{
    if (network==DisplayNetwork::Mainnet) return WALLY_NETWORK_BITCOIN_MAINNET;
    if (network==DisplayNetwork::Regtest) return WALLY_NETWORK_BITCOIN_REGTEST;
    return DocumentChain(network).empty() ? 0 : WALLY_NETWORK_BITCOIN_TESTNET;
}
}

TransactionDocument::~TransactionDocument()
{
    if (!source.empty()) wally_bzero(source.data(),source.size());
    if (!wire.empty()) wally_bzero(wire.data(),wire.size());
    if (!unsigned_wire.empty()) wally_bzero(unsigned_wire.data(),unsigned_wire.size());
}
std::string_view InspectionBuffer(const TransactionDocument& doc,InspectionBytes representation)
{
    if (representation==InspectionBytes::Source) return doc.source;
    if (representation==InspectionBytes::Wire) return doc.wire;
    return doc.unsigned_wire;
}
std::string_view InspectionBufferName(const TransactionDocument& doc,InspectionBytes representation)
{
    if (representation==InspectionBytes::Source) {
        switch (doc.format) {
        case DocumentFormat::RawHex: return "Original source · transaction hex text";
        case DocumentFormat::RawBinary: return "Original source · raw transaction bytes";
        case DocumentFormat::PsbtBase64: return "Original source · PSBT Base64 text";
        case DocumentFormat::PsbtBinary: return "Original source · binary PSBT bytes";
        case DocumentFormat::PublicDescriptor: return "Original source · descriptor text";
        }
    }
    if (representation==InspectionBytes::Unsigned) return "Derived unsigned transaction bytes · no final scriptSig or witness";
    if (doc.psbt_version) return "Wire hex · decoded PSBT container bytes";
    return "Wire hex · supplied raw transaction bytes";
}
std::span<const InspectionField> InspectionFields(const TransactionDocument& doc,
    InspectionBytes representation,InspectionNode node,size_t index)
{
    const auto& layout=representation==InspectionBytes::Unsigned ? doc.unsigned_layout : doc.wire_layout;
    if (node==InspectionNode::Overview) return layout.header;
    const auto& group=node==InspectionNode::Input ? layout.inputs : layout.outputs;
    return index<group.size() ? std::span<const InspectionField>(group[index]) : std::span<const InspectionField>{};
}
std::optional<ByteSpan> InspectionSpan(const TransactionDocument& doc,InspectionBytes representation,ByteSpan range)
{
    if (representation==InspectionBytes::Source) {
        if (doc.format==DocumentFormat::PsbtBase64 || doc.format==DocumentFormat::PublicDescriptor) return {};
        if (doc.format==DocumentFormat::RawHex) {
            if (range.offset>doc.wire.size() || range.length>doc.wire.size()-range.offset) return {};
            const auto trimmed=Trim(doc.source);
            const auto leading=size_t(trimmed.data()-doc.source.data());
            range={leading+2*range.offset,2*range.length};
        }
    }
    const auto buffer=InspectionBuffer(doc,representation);
    if (range.offset>buffer.size() || range.length>buffer.size()-range.offset) return {};
    return range;
}
std::string TransactionHex(Bytes bytes)
{
    if (bytes.empty()) return {};
    char* raw{};
    const auto result=wally_hex_from_bytes(Data(bytes),bytes.size(),&raw);
    const Strings hex(raw,&wally_free_string);
    if (result!=WALLY_OK || !hex) throw std::bad_alloc();
    return hex.get();
}
std::optional<PsbtOutboundCopy> PsbtForQr(const TransactionDocument& source,DisplayNetwork network,bool omit)
{
    if (!source.psbt_version || !source.transaction || !source.wire.starts_with(PSBT_MAGIC)) return {};
    if (!omit) return PsbtOutboundCopy{source.wire,0};
    try {
        Reader reader(source.wire);
        Require(reader.Take(PSBT_MAGIC.size())==PSBT_MAGIC);
        PsbtOutboundCopy copy{std::string(PSBT_MAGIC),0};
        while (true) {
            const auto begin=reader.Offset();
            auto key=reader.Vector(MAX_TRANSACTION_WIRE);
            if (key.empty()) {
                copy.wire+=std::string_view(source.wire).substr(begin);
                break;
            }
            reader.Vector(MAX_TRANSACTION_WIRE);
            if (key.size()==79 && key.front()==1) ++copy.omitted_global_xpubs;
            else copy.wire+=reader.Range(begin);
        }
        const auto parsed=ParseTransactionDocument(copy.wire,network).document;
        Require(parsed && parsed->psbt_version==source.psbt_version && parsed->unsigned_wire==source.unsigned_wire);
        return copy;
    } catch (...) {return {};}
}

std::string TransactionBase64(Bytes bytes)
{
    if (bytes.empty()) return {};
    char* raw{};
    const auto result=wally_base64_from_bytes(Data(bytes),bytes.size(),0,&raw);
    const Strings encoded(raw,&wally_free_string);
    if (result!=WALLY_OK || !encoded) throw std::bad_alloc();
    return encoded.get();
}
std::optional<std::string> DecodeTransactionBase64(Bytes text,size_t maximum)
{
    if (text.empty() || text.size()%4 || text.size()>((maximum+2)/3)*4) return {};
    const auto padding=text.ends_with("==") ? 2U : text.ends_with('=') ? 1U : 0U;
    const auto payload=text.substr(0,text.size()-padding);
    if (!std::ranges::all_of(payload,[](unsigned char c){
        return (c>='A' && c<='Z') || (c>='a' && c<='z') || (c>='0' && c<='9') || c=='+' || c=='/';
    })) return {};
    const auto size=text.size()/4*3;
    if (size-padding>maximum) return {};
    std::string decoded(size,'\0');
    size_t written{};
    if (wally_base64_n_to_bytes(text.data(),text.size(),0,reinterpret_cast<unsigned char*>(decoded.data()),decoded.size(),&written)!=WALLY_OK
            || written!=size-padding) return {};
    decoded.resize(written);
    if (TransactionBase64(decoded)!=text) return {};
    return decoded;
}
DocumentResult ParseTransactionDocument(Bytes source, DisplayNetwork network, std::stop_token stop,bool text_only)
{
    try {
        const Budget budget(stop);
        budget.Check();
        Require(!source.empty(),Error::Empty);
        Require(source.size()<=MAX_TRANSACTION_SOURCE,Error::Limit);
        const auto text=Trim(source);
        Require(!text.empty(),Error::Empty);
        auto result=std::make_shared<TransactionDocument>();
        if (!text_only && source.starts_with(PSBT_MAGIC)) {
            result->format=DocumentFormat::PsbtBinary;
            result->wire=source;
        } else if (LooksLikeDescriptor(text)) {
            const auto chain=DocumentChain(network);
            Require(!chain.empty(),Error::NetworkRequired);
            auto descriptor=PreviewPublicWallet(PublicImportKind::Descriptor,text,chain);
            Require(bool(descriptor.preview),Error::PublicDescriptor);
            result->format=DocumentFormat::PublicDescriptor;
            result->descriptor=std::move(descriptor.preview);
        } else if (text.starts_with("cHNidP")) {
            std::string compact(text);
            std::erase_if(compact,[](char c){return SPACE.find(c)!=SPACE.npos;});
            auto decoded=DecodeTransactionBase64(compact,MAX_TRANSACTION_WIRE);
            Require(decoded && decoded->starts_with(PSBT_MAGIC),Error::Format);
            result->format=DocumentFormat::PsbtBase64;
            result->wire=std::move(*decoded);
        } else if (IsHex(text)) {
            Require(text.size()/2<=MAX_TRANSACTION_WIRE,Error::Limit);
            auto decoded=Unhex(text,MAX_TRANSACTION_WIRE);
            Require(bool(decoded),Error::Format);
            result->format=DocumentFormat::RawHex;
            result->wire=std::move(*decoded);
        } else if (!text_only) {
            result->format=DocumentFormat::RawBinary;
            result->wire=source;
        } else return {{},Error::Format};
        Require(result->wire.size()<=MAX_TRANSACTION_WIRE,Error::Limit);
        if (result->format==DocumentFormat::PsbtBinary || result->format==DocumentFormat::PsbtBase64) {
            auto shape=CheckPsbt(result->wire,budget);
            if (shape.version!=0 && shape.version!=2) return {{},Error::UnsupportedVersion,shape.version};
            ReadPsbt(*result,std::move(shape),budget);
        } else if (!result->descriptor) ReadTx(*result,budget);
        budget.Check();
        result->source=source;
        return {std::move(result)};
    } catch (Error error) {
        return {{},error};
    } catch (const std::bad_alloc&) {
        return {{},Error::Limit};
    }
}
std::string_view DocumentFormatName(DocumentFormat kind)
{
    switch (kind) {
    case DocumentFormat::RawBinary: return "Raw transaction · binary";
    case DocumentFormat::RawHex: return "Raw transaction · hex text";
    case DocumentFormat::PsbtBinary: return "PSBT · binary";
    case DocumentFormat::PsbtBase64: return "PSBT · Base64 text";
    case DocumentFormat::PublicDescriptor: return "Public descriptor · text";
    }
    return "Unknown format";
}
std::string DocumentErrorMessage(const DocumentResult& result)
{
    switch (result.error) {
    case Error::None: return {};
    case Error::Empty: return "The document is empty.";
    case Error::Limit: return "The document exceeds the offline parser's size, entry, depth, or processing limit.";
    case Error::Format: return "Use transaction hex, raw transaction bytes, binary or Base64 PSBT, or a public descriptor.";
    case Error::Malformed: return "Malformed transaction or PSBT. Check its framing, fields and encoding.";
    case Error::Duplicate: return "The document contains a duplicate PSBT key or transaction input.";
    case Error::UnsupportedVersion: return "Unsupported PSBT version " + std::to_string(result.unsupported_version.value_or(0)) + ". The source was left intact.";
    case Error::NetworkRequired: return "Choose the descriptor's network before inspecting it.";
    case Error::PublicDescriptor: return "Use a supported checksummed public descriptor on the selected network. Private keys and recovery material are rejected.";
    case Error::Cancelled: return "The local document read was cancelled.";
    }
    return "The document is unavailable.";
}
std::string_view DocumentNetworkName(DisplayNetwork network)
{
    switch (network) {
    case DisplayNetwork::Unknown: return "Unknown";
    case DisplayNetwork::Mainnet: return "Mainnet";
    case DisplayNetwork::Testnet: return "Testnet";
    case DisplayNetwork::Testnet4: return "Testnet4";
    case DisplayNetwork::Signet: return "Signet";
    case DisplayNetwork::Regtest: return "Regtest";
    }
    return "Unknown";
}
std::string_view DocumentChain(DisplayNetwork network)
{
    switch (network) {
    case DisplayNetwork::Mainnet: return "main";
    case DisplayNetwork::Testnet: return "test";
    case DisplayNetwork::Testnet4: return "testnet4";
    case DisplayNetwork::Signet: return "signet";
    case DisplayNetwork::Regtest: return "regtest";
    case DisplayNetwork::Unknown: return {};
    }
    return {};
}
DisplayNetwork DocumentNetwork(const NetworkIdentity& network)
{
    const auto chain=ReceiveChain(network.genesis.hex());
    for (auto candidate:{DisplayNetwork::Mainnet,DisplayNetwork::Testnet,DisplayNetwork::Testnet4,DisplayNetwork::Signet,DisplayNetwork::Regtest})
        if (chain==DocumentChain(candidate)) return candidate;
    return DisplayNetwork::Unknown;
}
std::optional<std::string> OutputAddress(Bytes script_hex, DisplayNetwork network)
{
    const auto encoded=AddressNetwork(network);
    const auto script=Unhex(script_hex,MAX_SCRIPT);
    if (!encoded || !script) return {};
    const char* family=network==DisplayNetwork::Mainnet ? "bc" : network==DisplayNetwork::Regtest ? "bcrt" : "tb";
    char* raw{};
    const auto witness=wally_addr_segwit_from_bytes(Data(*script),script->size(),family,0,&raw);
    const Strings segwit(raw,&wally_free_string);
    if (witness==WALLY_OK && segwit) return std::string(segwit.get());
    raw=nullptr;
    const auto result=wally_scriptpubkey_to_address(Data(*script),script->size(),encoded,&raw);
    const Strings address(raw,&wally_free_string);
    if (result==WALLY_OK && address) return std::string(address.get());
    return {};
}
std::optional<std::string> AddressScript(Bytes address, DisplayNetwork network)
{
    const auto encoded=AddressNetwork(network);
    if (!encoded || address.empty() || address.size()>128 || address.find('\0')!=Bytes::npos) return {};
    const std::string nul(address);
    const char* family=network==DisplayNetwork::Mainnet ? "bc" : network==DisplayNetwork::Regtest ? "bcrt" : "tb";
    std::array<unsigned char,WALLY_SEGWIT_ADDRESS_PUBKEY_MAX_LEN> out{};
    size_t n{};
    if (wally_addr_segwit_to_bytes(nul.c_str(),family,0,out.data(),out.size(),&n)!=WALLY_OK
        && wally_address_to_scriptpubkey(nul.c_str(),encoded,out.data(),out.size(),&n)!=WALLY_OK) return {};
    auto script=TransactionHex(Data(out.data(),n));
    if (OutputAddress(script,network)!=address) return {};
    return script;
}

std::optional<std::string> MergePsbtMaps(const TransactionDocument& target,const TransactionDocument& supplied)
{
    if (!target.psbt_version || target.psbt_version!=supplied.psbt_version
        || !target.transaction || !supplied.transaction) return {};
    const auto maps=1+target.transaction->inputs.size()+target.transaction->outputs.size();
    if (maps!=1+supplied.transaction->inputs.size()+supplied.transaction->outputs.size()) return {};
    try {
        Reader base(target.wire), extra(supplied.wire);
        Require(base.Take(5)==PSBT_MAGIC && extra.Take(5)==PSBT_MAGIC);
        std::string merged(PSBT_MAGIC);
        for (size_t i=0;i<maps;++i) {
            std::map<Bytes,Bytes> existing;
            const auto read=[&](Reader& reader,bool append_all) {
                auto remaining=[&] {return merged.size()<=MAX_TRANSACTION_WIRE;};
                for (;;) {
                    const auto start=reader.Offset();
                    const auto key=reader.Vector(MAX_TRANSACTION_WIRE);
                    if (key.empty()) break;
                    const auto value=reader.Vector(MAX_TRANSACTION_WIRE);
                    auto [at, inserted]=existing.emplace(key,value);
                    if (!inserted) Require(!append_all && at->second==value);
                    else {
                        merged+=reader.Range(start);
                        Require(remaining(),Error::Limit);
                    }
                }
            };
            read(base,true);
            read(extra,false);
            merged.push_back('\0');
        }
        Require(base.Empty() && extra.Empty() && merged.size()<=MAX_TRANSACTION_WIRE);
        return merged;
    } catch (...) {return {};}
}
namespace {
void AppendNumber(std::string& out,uint64_t number)
{
    for (unsigned byte=0;byte<8;++byte) out+=char(uint8_t(number>>(8*byte)));
}
void AppendText(std::string& out,Bytes text)
{
    AppendNumber(out,text.size());
    out+=text;
}
std::optional<Hash256> HashCommitment(Bytes data)
{
    std::array<unsigned char,32> hash{};
    if (wally_sha256(Data(data),data.size(),hash.data(),hash.size())!=WALLY_OK) return {};
    return Hash256::Parse(TransactionHex(Data(hash.data(),hash.size())));
}
const PsbtPolicy& PolicyAt(const std::vector<PsbtPolicy>& policies,size_t i)
{
    static const PsbtPolicy empty;
    return i<policies.size() ? policies[i] : empty;
}
bool SamePolicyScripts(const PsbtPolicy& current,const PsbtPolicy& proposed,bool allow_omissions)
{
    if (!allow_omissions) return current.scripts==proposed.scripts;
    return std::ranges::all_of(proposed.scripts,[&](const auto& item) {
        const auto field=current.scripts.find(item.first);
        return field!=current.scripts.end() && field->second==item.second;
    });
}
}

std::optional<Hash256> TransactionCommitment(const TransactionDocument& document)
{
    if (!document.transaction || !document.transaction->locktime) return {};
    const auto& tx=*document.transaction;
    std::string bytes="roots-gui transaction commitment v1";
    AppendNumber(bytes,tx.version);
    AppendNumber(bytes,*tx.locktime);
    AppendNumber(bytes,tx.inputs.size());
    for (const auto& input:tx.inputs) {
        AppendText(bytes,input.outpoint.txid.hex());
        AppendNumber(bytes,input.outpoint.index);
        AppendNumber(bytes,input.sequence);
    }
    AppendNumber(bytes,tx.outputs.size());
    for (const auto& output:tx.outputs) {
        AppendNumber(bytes,uint64_t(output.value.sats()));
        AppendText(bytes,output.script);
    }
    return HashCommitment(bytes);
}

std::optional<uint32_t> ReviewSighash(const TransactionDocument& document,size_t i)
{
    if (!document.transaction || i>=document.transaction->inputs.size()) return {};
    const auto& policy=PolicyAt(document.input_policy,i);
    if (policy.signature_modes.size()>1) return {};
    if (!policy.signature_modes.empty()) {
        const auto actual=*policy.signature_modes.begin();
        if (policy.sighash && actual!=*policy.sighash) return {};
        return actual;
    }
    if (policy.sighash) return policy.sighash;
    const auto& script=document.transaction->inputs[i].previous_script;
    if (script.empty()) return {};
    return script.size()==68 && script.starts_with("5120") ? 0u : 1u;
}

std::string CompareTransactionReview(const TransactionDocument& current,const TransactionDocument& proposed,bool allow_omissions)
{
    if (!current.transaction || !proposed.transaction) return "An exact transaction is required for comparison.";
    const auto& before=*current.transaction;
    const auto& after=*proposed.transaction;
    if (before.version!=after.version) return "Transaction version changed.";
    if (!before.locktime || !after.locktime) return "Transaction locktime is Unknown.";
    if (before.locktime!=after.locktime) return "Transaction locktime changed.";
    if (before.inputs.size()!=after.inputs.size()) return "The ordered input count changed.";
    if (before.outputs.size()!=after.outputs.size()) return "The ordered output count changed.";
    for (size_t i=0;i<before.inputs.size();++i) {
        const auto& input=before.inputs[i];
        const auto& next=after.inputs[i];
        const auto name="Input "+std::to_string(i+1);
        if (input.outpoint!=next.outpoint) return name+" outpoint or order changed.";
        if (input.sequence!=next.sequence) return name+" sequence changed.";
        if (input.evidence==PrevoutEvidence::Conflict || next.evidence==PrevoutEvidence::Conflict)
            return name+" previous output proofs conflict.";
        if (input.previous_value!=next.previous_value || input.previous_script!=next.previous_script
            || input.evidence!=next.evidence) {
            const bool omitted=next.evidence==PrevoutEvidence::Unknown
                || (input.evidence==PrevoutEvidence::TxidMatched && next.evidence==PrevoutEvidence::Supplied
                    && input.previous_value==next.previous_value && input.previous_script==next.previous_script);
            if (!allow_omissions || !omitted) return name+" previous output value, script or proof changed.";
        }
        const auto& policy=PolicyAt(current.input_policy,i);
        const auto& other=PolicyAt(proposed.input_policy,i);
        const auto mode=ReviewSighash(current,i), proposed_mode=ReviewSighash(proposed,i);
        if ((!mode && (policy.sighash || !policy.signature_modes.empty()))
            || (!proposed_mode && (other.sighash || !other.signature_modes.empty())))
            return name+" sighash declarations and signatures conflict.";
        if (mode!=proposed_mode && (!allow_omissions || other.sighash || !other.signature_modes.empty()))
            return name+" sighash commitment changed.";
        if (!SamePolicyScripts(policy,other,allow_omissions)) return name+" script policy changed.";
    }
    for (size_t i=0;i<before.outputs.size();++i) {
        const auto name="Output "+std::to_string(i+1);
        if (before.outputs[i].script!=after.outputs[i].script) return name+" destination script or order changed.";
        if (before.outputs[i].value!=after.outputs[i].value) return name+" amount or order changed.";
        if (!SamePolicyScripts(PolicyAt(current.output_policy,i),PolicyAt(proposed.output_policy,i),allow_omissions))
            return name+" script policy changed.";
    }
    if ((!allow_omissions || proposed.modifiable) && current.modifiable.value_or(0)!=proposed.modifiable.value_or(0))
        return "Transaction modification permissions changed.";
    return {};
}

bool VerifyOutputAddress(Bytes address,Bytes script_hex,DisplayNetwork network)
{
    return OutputAddress(script_hex,network)==address;
}
bool MatchesPublicPolicy(Bytes script_hex,DisplayNetwork network,const PublicWalletPreview& policy)
{
    return !script_hex.empty() && VerifyPublicPreview(policy,DocumentChain(network))
        && std::ranges::any_of(policy.descriptors,[&](const auto& branch){return branch.script==script_hex;});
}

std::string TransactionEditBlock(const TransactionDocument& document)
{
    if (!document.transaction || document.transaction->inputs.empty() || document.transaction->outputs.empty())
        return "Editing requires a complete unsigned transaction with inputs and outputs.";
    const auto& tx=*document.transaction;
    if (tx.coinbase || tx.witness || tx.script_sig || std::ranges::any_of(document.input_policy,[](const auto& policy){
        return policy.final_fields || policy.signature_fields;
    })) return "Transactions with scriptSig, witness, signatures, final fields or a coinbase input are read-only.";
    if (!tx.locktime || document.modifiable.value_or(0))
        return "A conflicting locktime or active PSBT negotiation flags prevent local editing. The original remains available.";
    if (std::ranges::any_of(tx.inputs,[](const auto& input){return input.evidence==PrevoutEvidence::Conflict;}))
        return "Conflicting previous-output proofs prevent local editing.";
    return {};
}

namespace {
void PutLE(std::string& out,uint64_t value,unsigned size)
{
    for (unsigned i=0;i<size;++i) out+=char(uint8_t(value>>(8*i)));
}
std::string LE(uint64_t value,unsigned size)
{
    std::string out;
    PutLE(out,value,size);
    return out;
}
void PutCompact(std::string& out,size_t size)
{
    if (size<253) out+=char(size);
    else if (size<=UINT16_MAX) {out+=char(253);PutLE(out,size,2);}
    else {out+=char(254);PutLE(out,size,4);}
}
void PutVector(std::string& out,Bytes bytes)
{
    PutCompact(out,bytes.size());
    out+=bytes;
}
std::string WireHash(const Outpoint& point)
{
    auto decoded=*Unhex(point.txid.hex(),32);
    std::ranges::reverse(decoded);
    return decoded;
}
std::string UnsignedWire(const TransactionInspection& tx)
{
    std::string result;
    PutLE(result,tx.version,4);
    PutCompact(result,tx.inputs.size());
    for (const auto& input:tx.inputs) {
        result+=WireHash(input.outpoint);
        PutLE(result,input.outpoint.index,4);
        result+='\0'; // unsigned scriptSig
        PutLE(result,input.sequence,4);
    }
    PutCompact(result,tx.outputs.size());
    for (const auto& output:tx.outputs) {
        PutLE(result,uint64_t(output.value.sats()),8);
        PutVector(result,output.script.empty() ? std::string{} : *Unhex(output.script,MAX_SCRIPT));
    }
    PutLE(result,*tx.locktime,4);
    return result;
}
struct EditMap {
    struct Entry {Bytes key,encoded;};
    std::vector<Entry> entries;
    std::map<uint8_t,std::string> changes;

    explicit EditMap(Reader& reader)
    {
        for (;;) {
            const auto start=reader.Offset();
            const auto key=reader.Vector(MAX_TRANSACTION_WIRE);
            if (key.empty()) break;
            reader.Vector(MAX_TRANSACTION_WIRE);
            entries.push_back({key,reader.Range(start)});
        }
    }
    bool Contains(uint8_t type) const
    {
        return std::ranges::any_of(entries,[type](const auto& field){return field.key.size()==1 && uint8_t(field.key.front())==type;});
    }
    bool Only(std::initializer_list<uint8_t> allowed) const
    {
        return std::ranges::all_of(entries,[allowed](const auto& field) {
            return field.key.size()==1 && std::ranges::find(allowed,uint8_t(field.key.front()))!=allowed.end();
        });
    }
    void Write(std::string& out) const
    {
        std::set<uint8_t> written;
        const auto write=[&](uint8_t type,const std::string& value) {
            out+=char(1);
            out+=char(type);
            PutVector(out,value);
            written.insert(type);
        };
        for (const auto& field:entries) {
            const auto change=field.key.size()==1 ? changes.find(uint8_t(field.key.front())) : changes.end();
            if (change==changes.end()) out+=field.encoded;
            else write(change->first,change->second);
        }
        for (const auto& [type,value]:changes) if (!written.contains(type)) write(type,value);
        out+='\0';
    }
};
template<class T> void MoveItem(std::vector<T>& items,size_t source,size_t destination)
{
    if (source<destination) std::rotate(items.begin()+source,items.begin()+source+1,items.begin()+destination+1);
    else if (source>destination) std::rotate(items.begin()+destination,items.begin()+source,items.begin()+source+1);
}
std::string EditedSource(const TransactionDocument& original,std::string wire)
{
    switch (original.format) {
    case DocumentFormat::RawHex: return TransactionHex(wire);
    case DocumentFormat::PsbtBase64: return TransactionBase64(wire);
    default: return wire;
    }
}
}

std::optional<std::string> PayjoinDisclosureCopy(const TransactionDocument& original)
{
    if (original.psbt_version!=0 || !TransactionEditBlock(original).empty()) return {};
    try {
        Reader source(original.wire);
        Require(source.Take(5)==PSBT_MAGIC);
        std::string wire(PSBT_MAGIC);
        EditMap global(source);
        Require(global.Contains(0));
        std::erase_if(global.entries,[](const auto& entry){return entry.key!=Bytes("\0",1);});
        global.Write(wire);
        for (const auto& input:original.transaction->inputs) {
            Require(input.evidence==PrevoutEvidence::TxidMatched);
            EditMap map(source);
            std::erase_if(map.entries,[](const auto& entry){
                return entry.key.size()!=1
                    || (entry.key.front()!=0 && entry.key.front()!=1 && entry.key.front()!=3);
            });
            map.Write(wire);
        }
        wire.append(original.transaction->outputs.size(),'\0');
        return wire;
    } catch (...) {return {};}
}

std::shared_ptr<const TransactionDocument> PreservePayjoinMaps(const TransactionDocument& original,
    const TransactionDocument& proposal, DisplayNetwork network, std::stop_token stop)
{
    if (stop.stop_requested() || original.psbt_version!=0 || proposal.psbt_version!=0
        || !TransactionEditBlock(original).empty() || !TransactionEditBlock(proposal).empty()
        || proposal.transaction->inputs.size()!=original.transaction->inputs.size()+1
        || proposal.transaction->outputs.size()!=original.transaction->outputs.size()) return {};
    try {
        Reader previous(original.wire), received(proposal.wire);
        Require(previous.Take(5)==PSBT_MAGIC && received.Take(5)==PSBT_MAGIC);
        EditMap local(previous), remote(received);
        Require(local.Contains(0) && remote.Contains(0) && remote.Only({0}));
        local.changes.emplace(0,proposal.unsigned_wire);
        std::string wire(PSBT_MAGIC);
        local.Write(wire);
        for (size_t i=0;i<original.transaction->inputs.size();++i) {
            if (stop.stop_requested()) return {};
            Require(EditMap(received).Only({0,1,3}));
            EditMap(previous).Write(wire);
        }
        const EditMap added(received);
        Require(added.Contains(0) && added.Only({0,1,3}));
        added.Write(wire);
        for (size_t i=0;i<original.transaction->outputs.size();++i) {
            Require(EditMap(received).Only({}));
            EditMap(previous).Write(wire);
        }
        Require(previous.Empty() && received.Empty() && wire.size()<=MAX_TRANSACTION_WIRE,Error::Limit);
        return ParseTransactionDocument(EditedSource(original,std::move(wire)),network,stop).document;
    } catch (...) {return {};}
}

std::shared_ptr<const TransactionDocument> WithMatchedPreviousOutputs(const TransactionDocument& target,
    DisplayNetwork network,std::span<const std::shared_ptr<const TransactionDocument>> previous)
{
    if (!target.psbt_version || !target.transaction || previous.size()!=target.transaction->inputs.size()) return {};
    try {
        Reader reader(target.wire);
        Require(reader.Take(5)==PSBT_MAGIC);
        std::string wire(PSBT_MAGIC);
        EditMap(reader).Write(wire);
        for (size_t i=0;i<previous.size();++i) {
            EditMap map(reader);
            const auto& input=target.transaction->inputs[i];
            const auto& source=previous[i];
            Require(source && !source->psbt_version && source->transaction && source->transaction->txid==input.outpoint.txid
                && input.outpoint.index<source->transaction->outputs.size() && input.evidence!=PrevoutEvidence::Conflict);
            const auto& output=source->transaction->outputs[input.outpoint.index];
            Require((!input.previous_value || input.previous_value==output.value)
                && (input.previous_script.empty() || input.previous_script==output.script));
            if (!map.Contains(0)) map.changes.emplace(0,source->wire);
            map.Write(wire);
            Require(wire.size()<=MAX_TRANSACTION_WIRE,Error::Limit);
        }
        for (const auto& output:target.transaction->outputs) {
            (void)output;
            EditMap(reader).Write(wire);
        }
        Require(reader.Empty() && wire.size()<=MAX_TRANSACTION_WIRE);
        auto result=ParseTransactionDocument(wire,network).document;
        Require(result && result->unsigned_wire==target.unsigned_wire);
        return result;
    } catch (...) {return {};}
}

EditedTransaction EditTransaction(const TransactionDocument& original,DisplayNetwork network,const TransactionEdit& edit)
{
    const auto fail=[](std::string error){return EditedTransaction{{},{},std::move(error)};};
    if (auto reason=TransactionEditBlock(original);!reason.empty()) return fail(std::move(reason));
    try {
        auto planned=*original.transaction;
        const bool version2=original.psbt_version==2;
        std::optional<EditMap> header;
        std::vector<EditMap> inputs,outputs;
        if (original.psbt_version) {
            Reader reader(original.wire);
            Require(reader.Take(PSBT_MAGIC.size())==PSBT_MAGIC);
            header.emplace(reader);
            for (size_t i=0;i<planned.inputs.size();++i) inputs.emplace_back(reader);
            for (size_t i=0;i<planned.outputs.size();++i) outputs.emplace_back(reader);
            Require(reader.Empty());
        }
        std::string changed;
        const auto mark=[&](std::string_view field){if (!changed.empty()) changed+=", ";changed+=field;};
        if (edit.version && *edit.version!=planned.version) {
            if (*edit.version<(version2 ? 2U : 1U) || *edit.version>3)
                return fail(version2 ? "This PSBTv2 provider supports editing to transaction version 2 or 3."
                    : "Editable transaction versions are 1, 2 and 3.");
            planned.version=*edit.version;
            if (version2) header->changes[2]=LE(planned.version,4);
            mark("transaction version");
        }
        if (edit.locktime && edit.locktime!=planned.locktime) {
            if (version2 && std::ranges::any_of(inputs,[](const auto& map){return map.Contains(0x11) || map.Contains(0x12);}))
                return fail("An input-required PSBT locktime cannot be replaced by changing the fallback locktime.");
            planned.locktime=*edit.locktime;
            if (version2) header->changes[3]=LE(*planned.locktime,4);
            mark("locktime");
        }
        if (const auto& input=edit.input) {
            if (input->index>=planned.inputs.size()) return fail("The selected input no longer exists.");
            auto& changed_input=planned.inputs[input->index];
            if (input->previous!=changed_input.outpoint) {
                if (original.psbt_version && !(version2 ? inputs[input->index].Only({0x0e,0x0f,0x10}) : inputs[input->index].entries.empty()))
                    return fail("This input has attached metadata or proofs. Replacing its outpoint would detach them.");
                if (input->previous.index==UINT32_MAX && input->previous.txid.hex()==std::string(64,'0'))
                    return fail("A coinbase outpoint cannot be assigned here.");
                changed_input.outpoint=input->previous;
                if (version2) {
                    inputs[input->index].changes[0x0e]=WireHash(input->previous);
                    inputs[input->index].changes[0x0f]=LE(input->previous.index,4);
                }
                mark("input "+std::to_string(input->index+1)+" outpoint");
            }
            if (input->sequence!=changed_input.sequence) {
                changed_input.sequence=input->sequence;
                if (version2) inputs[input->index].changes[0x10]=LE(input->sequence,4);
                mark("input "+std::to_string(input->index+1)+" sequence");
            }
        }
        if (const auto& output=edit.output) {
            if (output->index>=planned.outputs.size()) return fail("The selected output no longer exists.");
            auto& item=planned.outputs[output->index];
            const auto& replacement=output->replacement;
            if (replacement.value.sats()<0 || uint64_t(replacement.value.sats())>MAX_MONEY)
                return fail("Use whole satoshis within the Bitcoin monetary range.");
            if (replacement.value!=item.value) {
                if (version2) outputs[output->index].changes[3]=LE(uint64_t(replacement.value.sats()),8);
                mark("output "+std::to_string(output->index+1)+" amount");
            }
            if (replacement.script!=item.script) {
                if (original.psbt_version && !(version2 ? outputs[output->index].Only({3,4}) : outputs[output->index].entries.empty()))
                    return fail("This output has attached metadata. Its script cannot be replaced safely.");
                const auto script=Unhex(replacement.script,MAX_SCRIPT);
                if (!script || TransactionHex(*script)!=replacement.script)
                    return fail("Provide a supported address or exact lowercase script hex.");
                if (version2) outputs[output->index].changes[4]=*script;
                mark("output "+std::to_string(output->index+1)+" script");
            }
            item=replacement;
        }
        if (const auto& order=edit.reorder) {
            const auto [group,from,to]=*order;
            const bool in=group==InspectionNode::Input;
            const auto count=in ? planned.inputs.size() : planned.outputs.size();
            if (group==InspectionNode::Overview || from>=count || to>=count || from==to)
                return fail("Select a current input or output and a different position.");
            if (in) {
                MoveItem(planned.inputs,from,to);
                if (header) MoveItem(inputs,from,to);
            } else {
                MoveItem(planned.outputs,from,to);
                if (header) MoveItem(outputs,from,to);
            }
            mark(in ? "input order" : "output order");
        }
        if (changed.empty()) return fail("No material transaction fields changed.");
        uint64_t total{};
        for (const auto& output:planned.outputs) MoneyTotal(total,uint64_t(output.value.sats()));
        std::string wire;
        if (header) {
            if (!version2) header->changes[0]=UnsignedWire(planned);
            wire=PSBT_MAGIC;
            header->Write(wire);
            for (const auto& map:inputs) map.Write(wire);
            for (const auto& map:outputs) map.Write(wire);
        } else wire=UnsignedWire(planned);
        if (wire.size()>MAX_TRANSACTION_WIRE) return fail("The edited transaction exceeds the size limit.");
        auto checked=ParseTransactionDocument(EditedSource(original,std::move(wire)),network);
        if (!checked.document) return fail(DocumentErrorMessage(checked));
        const auto& result=*checked.document->transaction;
        if (result.version!=planned.version || result.locktime!=planned.locktime || result.outputs!=planned.outputs
            || !std::ranges::equal(result.inputs,planned.inputs,[](const auto& a,const auto& b){
                return a.outpoint==b.outpoint && a.sequence==b.sequence;
            })) return fail("The edited bytes do not match the requested fields.");
        uint64_t known{};
        const bool matched=std::ranges::all_of(result.inputs,[&](const auto& input) {
            if (input.evidence!=PrevoutEvidence::TxidMatched || !input.previous_value) return false;
            MoneyTotal(known,uint64_t(input.previous_value->sats()));
            return true;
        });
        if (matched && total>known) return fail("The outputs exceed the matched previous-output total.");
        return {std::move(checked.document),std::move(changed),{}};
    } catch (Error) {
        return fail("The edit exceeds transaction bounds or the monetary limit.");
    } catch (const std::bad_alloc&) {
        return fail("The edit could not fit in available resources.");
    }
}
}
