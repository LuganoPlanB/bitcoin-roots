// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/localization.h"
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <sodium.h>

namespace roots::gui::ui::shell {
namespace {
namespace c=components;
using c::ax::SemanticAction;
using c::ax::SemanticRole;
using protocol::ByteSpan;
using protocol::InspectionBytes;
using protocol::InspectionNode;
constexpr size_t ROW_PAGE=8, FIELD_PAGE=8, HEX_PAGE=96;
constexpr int64_t MAX_MONEY=21000000LL*100000000;

bool Obscured(const State& state)
{
    return state.local_document.dialog.open || state.recovery_dialog.open || state.server.dialog.open
        || state.onboarding.dialog.open || state.actions.kind!=DocumentDialog::None
        || state.close_tab_popup || state.close_document_popup
        || (c::native_input && c::native_input->NativeDialogActive());
}

void ResetBytes(ImportedView& view)
{
    view.selection.reset();
    view.selection_label.clear();
    view.bytes_page=0;
    view.field_page=0;
    view.inspection_status.clear();
}
void ActivateBytes(State& state,ImportedView& view,InspectionBytes bytes)
{
    auto& imported=*state.documents.Active()->imported;
    imported.representation=bytes==InspectionBytes::Wire ? app::DocumentRepresentation::WireHex : app::DocumentRepresentation::Source;
    view.inspection_owner->representation=imported.representation;
    view.bytes=bytes;
    ResetBytes(view);
    ++view.revision;
}

struct Guard {
    State& state;
    app::Requests& host;
    InspectionOwner owner;
    uint64_t revision;

    ImportedView* Current() const
    {
        if (Obscured(state) || CurrentInspectionOwner(state,host)!=owner) return nullptr;
        const auto at=state.imported_views.find(owner.id);
        return at!=state.imported_views.end() && at->second.revision==revision ? &at->second : nullptr;
    }
};

void Text(State& state,const char* key,const char* label,std::string_view value)
{
    ImGui::TextWrapped("%.*s",int(value.size()),value.data());
    state.presentation.Annotate(key,label,value);
}

bool Button(State& state,Facts& facts,const imkit::Theme& theme,const std::string& key,const std::string& label,
    bool enabled,std::function<bool()> operation,bool selected=false,const char* semantic_label=nullptr,
    std::string_view description={})
{
    ImGui::PushID(key.c_str());
    ImGui::BeginDisabled(!enabled);
    const bool pressed=theme::ActionButton(label.c_str(),selected ? imkit::ActionVariant::Primary
        : imkit::ActionVariant::Secondary,{},theme);
    const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();
    facts.transaction_document[key]={{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),!enabled,
        pressed,pressed,ImGui::IsItemVisible()};
    const auto request=state.presentation.Interact(semantic_label ? semantic_label : label.c_str(),SemanticRole::Button,
        SemanticAction::Press|SemanticAction::Focus,!enabled,selected,{},description);
    ImGui::EndDisabled();
    ImGui::PopID();
    const bool accepted=enabled && pressed && operation();
    if (request) state.presentation.Defer(*request,[enabled,pressed,accepted,operation=std::move(operation)](const auto&) {
        return enabled && (pressed ? accepted : operation());
    });
    return accepted;
}

std::string_view ScriptKind(std::string_view script)
{
    if (script.size()==50 && script.starts_with("76a914") && script.ends_with("88ac")) return "P2PKH";
    if (script.size()==46 && script.starts_with("a914") && script.ends_with("87")) return "P2SH";
    if (script.size()==44 && script.starts_with("0014")) return "P2WPKH";
    if (script.size()==68 && script.starts_with("0020")) return "P2WSH";
    if (script.size()==68 && script.starts_with("5120")) return "P2TR";
    if (script.starts_with("6a")) return "OP_RETURN";
    return script.empty() ? "Empty script" : "Other script";
}

std::string Amount(Money value)
{
    return locale::BothUnits(value);
}

const HistoryLedger* LiveLedger(const Guard& guard)
{
    if (!guard.owner.wallet || !guard.host.Recovery() || !guard.host.Refresh() || !guard.host.Connection()) return nullptr;
    const auto& connection=*guard.host.Connection();
    const auto* wallet=app::RegisteredWallet(guard.state.documents,*guard.owner.wallet);
    if (!wallet || connection.Offline() || connection.PrivacyLocked() || !connection.Active()) return nullptr;
    const auto identity=app::WalletRefreshIdentity(*connection.Active(),connection.Session(),*wallet,guard.owner.epoch);
    const auto current=guard.host.Refresh()->View();
    if (!identity || !current.identity || *identity!=*current.identity || !current.ledger || !current.node
        || !current.chain_current || current.offline || current.locked || current.freshness[2]!=Freshness::Live
        || !current.history_sequence || current.ledger->Tip()!=current.node->tip
        || !current.ledger->Stable() || current.ledger->Scanning()) return nullptr;
    return current.ledger;
}

std::string Spending(const HistoryLedger* ledger,const Outpoint& point,const std::optional<Hash256>& current={})
{
    if (!ledger) return "Spending history: Unknown";
    const auto identifier=point.txid.hex()+":"+std::to_string(point.index);
    for (const auto& [id,tx]:ledger->Rows()) {
        if (!tx.decoded_available || (tx.state!=HistoryState::Confirmed && tx.state!=HistoryState::Pending)) continue;
        if (std::ranges::any_of(tx.io,[&](const auto& io){return io.kind=="Input" && io.reference==identifier;})) {
            if (current && tx.txid==*current) return "Spent by this transaction in current wallet history.";
            if (current) return "Conflicting spend in current wallet history: "+id;
            return std::string(tx.state==HistoryState::Confirmed ? "Confirmed spend in wallet history: "
                : "Pending spend in wallet history: ")+id;
        }
    }
    if (ledger->InventoryAvailable() && std::ranges::any_of(ledger->Coins(),[&](const auto& coin){return coin.outpoint==point;}))
        return "Observed in the current wallet's unspent output inventory. Spendability is Unknown.";
    return "Spending history: Unknown";
}

std::string CurrentSpending(const Guard& guard,const Outpoint& point,const std::optional<Hash256>& transaction={})
{
    auto& cache=guard.state.imported_views.at(guard.owner.id).spending;
    const auto* ledger=LiveLedger(guard);
    if (!ledger) {
        cache={};
        return "Spending history: Unknown";
    }
    const auto generation=guard.host.Refresh()->View().generation;
    if (cache.ledger!=ledger || cache.context!=ledger->Context() || cache.generation!=generation || cache.revision!=ledger->Revision()
        || cache.point!=point || cache.current!=transaction) {
        cache={ledger,ledger->Context(),generation,ledger->Revision(),point,transaction,Spending(ledger,point,transaction)};
    }
    return cache.status;
}

std::string Locktime(const protocol::TransactionInspection& tx)
{
    if (!tx.locktime) return "Locktime: Unknown";
    const auto value=*tx.locktime;
    if (!value) return "Locktime 0 · no absolute constraint.";
    const auto measure=value<500000000 ? "block height" : "Unix timestamp (seconds)";
    const bool final=std::ranges::all_of(tx.inputs,[](const auto& input){return input.sequence==UINT32_MAX;});
    return "Locktime "+std::to_string(value)+" · "+measure+(final
        ? ". All sequences are final, so this constraint is inactive."
        : ". Whether the chain satisfies it is Unknown.");
}

std::string Sequence(const protocol::TransactionInspection& tx,uint32_t sequence)
{
    std::string result=std::to_string(sequence);
    if (sequence==UINT32_MAX) return result+" · Final sequence.";
    if (sequence<0xfffffffeU) result+=" · Signals opt-in replacement.";
    if (tx.coinbase || std::bit_cast<int32_t>(tx.version)<2 || (sequence&(1U<<31)))
        return result+" Relative lock inactive.";
    const auto interval=sequence&0xffffU;
    result+=" Relative lock "+std::to_string(sequence&(1U<<22) ? interval*512U : interval)
        +(sequence&(1U<<22) ? " seconds (median-time-past)." : " blocks.");
    return result+" Maturity is Unknown.";
}

std::string SighashName(uint32_t mode)
{
    switch (mode) {
    case 0: return "DEFAULT (Taproot)";
    case 1: return "ALL";
    case 2: return "NONE";
    case 3: return "SINGLE";
    case 0x81: return "ALL | ANYONECANPAY";
    case 0x82: return "NONE | ANYONECANPAY";
    case 0x83: return "SINGLE | ANYONECANPAY";
    default: return "Unsupported value "+std::to_string(mode);
    }
}

std::string Sighash(const protocol::TransactionDocument& doc,size_t index)
{
    if (index>=doc.input_policy.size()) return "Unknown. Raw scripts and witness are shown without interpreting signatures.";
    const auto& policy=doc.input_policy[index];
    if (policy.signature_modes.size()>1 || (policy.sighash && !policy.signature_modes.empty()
        && *policy.sighash!=*policy.signature_modes.begin())) return "Conflicting sighash metadata. Effective mode is Unknown.";
    if (!policy.signature_modes.empty()) return SighashName(*policy.signature_modes.begin())+" · supplied signature encoding.";
    if (policy.sighash) return SighashName(*policy.sighash)+" · requested in PSBT.";
    if (policy.final_fields) return "Unknown from the finalized PSBT fields.";
    const auto& prev=doc.transaction->inputs[index];
    const auto kind=ScriptKind(prev.previous_script);
    if (!prev.previous_value || kind=="Other script" || kind=="Empty script" || kind=="OP_RETURN") return "Unknown";
    return SighashName(kind=="P2TR" ? 0 : 1)+" · default for the supplied previous-output script type.";
}

app::Request CopyRequest(uint64_t id,std::string name,std::string value)
{
    return {app::Command::Component,id,app::Section::Transactions,c::CopyIdentifier{std::move(name),std::move(value),true}};
}

void CopyValue(const Guard& guard,Facts& facts,const imkit::Theme& theme,const char* key,const char* label,std::string value)
{
    auto request=CopyRequest(guard.owner.id,key,std::move(value));
    Button(guard.state,facts,theme,key,label,guard.host.CanSubmit(request).available,
        [guard,request=std::move(request)] {
            auto* view=guard.Current();
            if (!view || !guard.host.CanSubmit(request).available) return false;
            const auto receipt=guard.host.Submit(request);
            view->inspection_status=receipt.reason;
            ++view->revision;
            return receipt.accepted;
        });
}

const TransactionOverviewProjection& PrepareOverview(ImportedView& view,const app::ImportedDocument& imported,FrameWork& work)
{
    auto& overview=view.overview;
    if (overview.source.lock()==imported.parsed && overview.funded==bool(imported.funding)) return overview;
    overview={};
    overview.source=imported.parsed;
    overview.funded=bool(imported.funding);
    const auto& parsed=*imported.parsed;
    const auto& tx=*parsed.transaction;
    work.psbt_overview_items=tx.inputs.size()+tx.outputs.size();
    overview.locktime=Locktime(tx);
    std::map<std::string_view,size_t> output_scripts,input_scripts;
    const auto mix=[](const auto& scripts) {
        std::string text;
        for (const auto& [kind,count]:scripts) {
            if (!text.empty()) text+=" · ";
            text+=std::to_string(count)+" "+std::string(kind);
        }
        return text;
    };
    for (const auto& output:tx.outputs) {
        ++output_scripts[ScriptKind(output.script)];
        overview.output_total+=output.value.sats();
    }
    for (const auto& input:tx.inputs) {
        const bool matched=input.evidence==protocol::PrevoutEvidence::TxidMatched || overview.funded;
        ++input_scripts[tx.coinbase ? "Coinbase" : matched && input.previous_value ? ScriptKind(input.previous_script) : "Unknown script"];
        if (input.evidence!=protocol::PrevoutEvidence::TxidMatched || !input.previous_value
            || input.previous_value->sats()>MAX_MONEY-overview.input_total) continue;
        ++overview.known_inputs;
        overview.input_total+=input.previous_value->sats();
    }
    overview.output_mix=mix(output_scripts);
    overview.input_mix=mix(input_scripts);
    if (parsed.psbt_version) overview.commitment=protocol::TransactionCommitment(parsed);
    return overview;
}

void Overview(const Guard& guard,Facts& facts,const imkit::Theme& theme,const app::ImportedDocument& imported)
{
    auto& state=guard.state;
    const auto& decoded=*imported.parsed;
    const auto& tx=*decoded.transaction;
    Text(state,"overview","Transaction overview","Transaction overview");
    Text(state,"transaction-header","Transaction header",std::to_string(tx.inputs.size())+(tx.inputs.size()==1 ? " input · " : " inputs · ")+
        std::to_string(tx.outputs.size())+(tx.outputs.size()==1 ? " output · Version " : " outputs · Version ")
        +std::to_string(std::bit_cast<int32_t>(tx.version)));
    const auto& overview=PrepareOverview(state.imported_views.at(guard.owner.id),imported,facts.work);
    Text(state,"locktime","Absolute locktime",overview.locktime);
    Text(state,"output-total","Total output amount","Outputs "+Amount(Money::Satoshis(overview.output_total))+
        (overview.output_mix.empty() ? "" : " · "+overview.output_mix));
    const auto& funding=imported.funding;
    if (funding) {
        const auto total=Money::Satoshis(overview.output_total+funding->fee.sats());
        Text(state,"input-total","Total input amount","Inputs "+Amount(total)+" · matched saved prevouts.");
    } else Text(state,"input-total","Total input amount",overview.known_inputs && overview.known_inputs==tx.inputs.size()
        ? "Inputs "+Amount(Money::Satoshis(overview.input_total))
        : "Inputs Unknown · "+std::to_string(overview.known_inputs)+" of "+std::to_string(tx.inputs.size())+" previous amounts matched.");
    if (!overview.input_mix.empty()) Text(state,"input-mix","Input script distribution","Input scripts · "+overview.input_mix);
    if (funding) Text(state,"transaction-fee","Verified funding fee","Fee: "+locale::Btc(funding->fee)+" BTC · matched saved prevouts");
    else if (tx.fee) Text(state,"transaction-fee","Offline calculated fee","Fee: "+tx.fee->btc()+" BTC · matched supplied prevouts");
    else Text(state,"transaction-fee","Offline calculated fee",tx.coinbase ? "Fee: not applicable to a coinbase transaction." : "Fee: Unknown");
    if (!guard.owner.wallet) Text(state,"overview-ownership","Wallet evidence","Wallet ownership: Unknown");
    if (decoded.psbt_version) {
        Text(state,"psbt-status","PSBT container status","PSBT v"+std::to_string(*decoded.psbt_version)+
            " · "+std::to_string(decoded.psbt_entries)+" serialized map entries. Signed transaction ID: Unknown.");
        if (const auto& commitment=overview.commitment) {
            Text(state,"unsigned-commitment","Unsigned transaction commitment","Unsigned commitment: "+commitment->hex());
            CopyValue(guard,facts,theme,"copy-commitment","Copy unsigned commitment",commitment->hex());
        } else Text(state,"unsigned-commitment","Unsigned transaction commitment","Unsigned commitment: Unknown");
        return;
    }
    if (tx.txid) Text(state,"raw-txid","Raw transaction ID","Transaction ID: "+tx.txid->hex());
    if (tx.wtxid) Text(state,"raw-wtxid","Raw witness transaction ID","Witness transaction ID: "+tx.wtxid->hex());
    if (tx.txid) CopyValue(guard,facts,theme,"copy-txid","Copy transaction ID",tx.txid->hex());
    if (tx.wtxid) {
        if (tx.txid) ImGui::SameLine();
        CopyValue(guard,facts,theme,"copy-wtxid","Copy witness transaction ID",tx.wtxid->hex());
    }
}

void Input(const Guard& guard,Facts& facts,const imkit::Theme& theme,const ImportedView& view,const app::ImportedDocument& imported)
{
    auto& state=guard.state;
    const auto& tx=*imported.parsed->transaction;
    const auto& input=tx.inputs.at(view.index);
    const auto identifier=input.outpoint.txid.hex()+":"+std::to_string(input.outpoint.index);
    Text(state,"input-detail","Input detail","Input "+std::to_string(view.index+1));
    Text(state,"input-outpoint","Selected input outpoint",tx.coinbase ? "Coinbase input · no previous output." : identifier);
    const auto* proof=imported.funding ? &imported.funding->evidence.inputs.at(view.index) : nullptr;
    const bool matched=input.evidence==protocol::PrevoutEvidence::TxidMatched;
    const bool cached=matched && view.wallet && view.wallet==guard.owner.wallet && view.policy_scripts.contains(input.previous_script);
    if (proof) Text(state,"prevout-evidence","Previous output evidence","Previous output matches the saved wallet proof for "+proof->value->btc()+" BTC.");
    else if (input.evidence==protocol::PrevoutEvidence::Conflict)
        Text(state,"prevout-evidence","Previous output evidence","Previous output: conflicting metadata. Amount and script are Unknown.");
    else if (input.previous_value) Text(state,"prevout-evidence","Previous output evidence",matched
        ? Amount(*input.previous_value)+" · matches supplied previous transaction ID."
        : Amount(*input.previous_value)+" · supplied amount. Independently matched previous amount is Unknown.");
    else Text(state,"prevout-evidence","Previous output evidence",tx.coinbase ? "No previous output." : "Previous amount and script: Unknown");
    if (input.previous_value) Text(state,"prevout-script","Previous scriptPubKey",
        std::string(ScriptKind(input.previous_script))+(matched || proof ? " · " : " · supplied · ")
        +(input.previous_script.size()<=96 ? input.previous_script : input.previous_script.substr(0,96)+"… Open byte fields for the full script."));
    if (!tx.coinbase) {
        Text(state,"input-ownership","Input wallet evidence",proof ? "Matches independently saved wallet policy evidence."
            : cached ? "Matched previous script is in the cached public wallet policy." : "Wallet ownership: Unknown");
        Text(state,"input-spent","Previous output spending history",CurrentSpending(guard,input.outpoint,tx.txid));
    }
    Text(state,"sequence","Input sequence","Sequence "+Sequence(tx,input.sequence));
    Text(state,"sighash","Input sighash","Sighash: "+Sighash(*imported.parsed,view.index));
    if (!tx.coinbase) CopyValue(guard,facts,theme,"copy-outpoint","Copy outpoint",identifier);
}

void Output(const Guard& guard,Facts& facts,const imkit::Theme& theme,const ImportedView& view,const app::ImportedDocument& imported)
{
    auto& state=guard.state;
    const auto& tx=*imported.parsed->transaction;
    const auto& output=tx.outputs.at(view.index);
    Text(state,"output-detail","Output detail","Output "+std::to_string(view.index+1));
    Text(state,"output-amount","Selected output amount",Amount(output.value));
    const auto address=protocol::OutputAddress(output.script,imported.network);
    Text(state,"output-address","Selected output address",address.value_or("Address unavailable for this script or display network."));
    if (tx.txid) Text(state,"output-outpoint","Output outpoint",tx.txid->hex()+":"+std::to_string(view.index));
    else Text(state,"output-outpoint","Output outpoint","Signed transaction ID: Unknown. Output index "+std::to_string(view.index)+".");
    const bool saved_change=imported.funding && imported.funding->evidence.offline_change && view.index==tx.outputs.size()-1;
    const bool reviewed_change=view.proved_change_output==view.index && view.reviewed
        && protocol::CompareTransactionReview(*view.reviewed,*imported.parsed).empty();
    const bool known=view.wallet && view.wallet==guard.owner.wallet && view.policy_scripts.contains(output.script);
    Text(state,"output-ownership","Wallet script evidence",saved_change
        ? "This output matches the saved internal change descriptor. Allocation is unconfirmed."
        : reviewed_change ? "Intended change matches the separately read internal wallet descriptor."
        : known ? "Exact match to previously read wallet script. Change intent: Unknown." : "Wallet ownership and change intent: Unknown");
    Text(state,"output-spent","Output spending history",tx.txid
        ? CurrentSpending(guard,{*tx.txid,uint32_t(view.index)}) : "Spending history: Unknown");
    Text(state,"output-script","Selected output script",std::string(ScriptKind(output.script))+" · "+
        (output.script.size()<=96 ? output.script : output.script.substr(0,96)+"… Open byte fields for the full script."));
    if (address) CopyValue(guard,facts,theme,"copy-output-address","Copy output address",*address);
}

void Tree(const Guard& guard,Facts& facts,const imkit::Theme& theme,const ImportedView& view)
{
    const auto& tx=*guard.owner.content->transaction;
    auto& state=guard.state;
    const auto select=[guard](InspectionNode group,size_t index) {
        auto* current=guard.Current();
        if (!current) return false;
        current->node=group;
        current->index=index;
        if (group!=InspectionNode::Overview) current->tree_group=group;
        ResetBytes(*current);
        ++current->revision;
        return true;
    };
    Button(state,facts,theme,"overview","Overview",true,[select]{return select(InspectionNode::Overview,0);},view.node==InspectionNode::Overview);
    const auto branch=[guard](InspectionNode group) {
        auto* current=guard.Current();
        if (!current || current->tree_group==group) return false;
        current->tree_group=group;
        current->tree_page=0;
        ++current->revision;
        return true;
    };
    Button(state,facts,theme,"tree-inputs","Inputs",!tx.inputs.empty(),[branch]{return branch(InspectionNode::Input);},
        view.tree_group==InspectionNode::Input,"Transaction inputs",std::to_string(tx.inputs.size()));
    ImGui::SameLine();
    Button(state,facts,theme,"tree-outputs","Outputs",!tx.outputs.empty(),[branch]{return branch(InspectionNode::Output);},
        view.tree_group==InspectionNode::Output,"Transaction outputs",std::to_string(tx.outputs.size()));
    const auto group=view.tree_group;
    const auto count=group==InspectionNode::Input ? tx.inputs.size() : tx.outputs.size();
    const auto first=view.tree_page*ROW_PAGE;
    for (size_t i=first;i<std::min(count,first+ROW_PAGE);++i) {
        const std::string type=group==InspectionNode::Input ? "Input" : "Output";
        const auto identity=group==InspectionNode::Input
            ? tx.inputs[i].outpoint.txid.hex()+":"+std::to_string(tx.inputs[i].outpoint.index)
            : "Output index "+std::to_string(i);
        Button(state,facts,theme,(group==InspectionNode::Input ? "input-" : "output-")+std::to_string(i),
            type+" "+std::to_string(i+1),true,[select,group,i]{return select(group,i);},group==view.node && view.index==i,nullptr,identity);
    }
    if (count<=ROW_PAGE) return;
    Text(state,"tree-range","Visible transaction items",std::to_string(first+1)+"–"+std::to_string(std::min(count,first+ROW_PAGE))+" of "+std::to_string(count));
    const auto page=[guard,count](size_t index){
        auto* current=guard.Current();
        if (!current || index>(count-1)/ROW_PAGE) return false;
        current->tree_page=index;
        ++current->revision;
        return true;
    };
    Button(state,facts,theme,"tree-prev","Earlier rows",view.tree_page>0,[page,index=view.tree_page]{return index && page(index-1);});
    ImGui::SameLine();
    Button(state,facts,theme,"tree-next","Later rows",first+ROW_PAGE<count,[page,index=view.tree_page]{return page(index+1);});
    Button(state,facts,theme,"tree-first","First page",view.tree_page>0,[page]{return page(0);});
    ImGui::SameLine();
    Button(state,facts,theme,"tree-last","Last page",first+ROW_PAGE<count,[page,count]{return page((count-1)/ROW_PAGE);});
}

void Hex(const Guard& guard,Facts& facts,const imkit::Theme& theme,const ImportedView& view)
{
    const auto& doc=*guard.owner.content;
    auto& state=guard.state;
    const auto data=protocol::InspectionBuffer(doc,view.bytes);
    Text(state,"shown-representation","Active representation",protocol::InspectionBufferName(doc,view.bytes));
    const auto repr=[guard](InspectionBytes bytes) {
        auto* current=guard.Current();
        if (!current || protocol::InspectionBuffer(*guard.owner.content,bytes).empty()) return false;
        ActivateBytes(guard.state,*current,bytes);
        return true;
    };
    Button(state,facts,theme,"hex-source","Original source",true,[repr]{return repr(InspectionBytes::Source);},view.bytes==InspectionBytes::Source);
    if (!doc.wire.empty()) {
        theme::SameLineForAction(doc.psbt_version ? "Decoded PSBT" : "Raw wire");
        Button(state,facts,theme,"hex-wire",doc.psbt_version ? "Decoded PSBT" : "Raw wire",true,
            [repr]{return repr(InspectionBytes::Wire);},view.bytes==InspectionBytes::Wire);
    }
    if (!doc.unsigned_wire.empty()) {
        theme::SameLineForAction("Derived unsigned");
        Button(state,facts,theme,"hex-unsigned","Derived unsigned",true,[repr]{return repr(InspectionBytes::Unsigned);},view.bytes==InspectionBytes::Unsigned);
    }
    const auto pages=(data.size()+HEX_PAGE-1)/HEX_PAGE;
    const auto move=[guard,pages](size_t page){
        auto* current=guard.Current();
        if (!current || page>=pages) return false;
        current->bytes_page=page;
        ++current->revision;
        return true;
    };
    if (pages>1) {
        Button(state,facts,theme,"bytes-back","Previous bytes",view.bytes_page>0,[move,page=view.bytes_page]{return page && move(page-1);});
        theme::SameLineForAction("Next bytes");
        Button(state,facts,theme,"bytes-next","Next bytes",view.bytes_page+1<pages,[move,page=view.bytes_page]{return move(page+1);});
    }
    const auto selection=view.selection;
    if (selection) Text(state,"selected-range","Selected byte range",view.selection_label+" · Offset "+std::to_string(selection->offset)+
        " · "+std::to_string(selection->length)+(selection->length==1 ? " byte" : " bytes"));
    else Text(state,"selected-range","Selected byte range","Select a field to map its exact bytes.");
    const auto start=view.bytes_page*HEX_PAGE;
    const auto page=data.substr(std::min(start,data.size()),HEX_PAGE);
    facts.work.psbt_hex_bytes=page.size();
    Text(state,"page-summary","Visible byte page","Page "+std::to_string(view.bytes_page+1)+" of "+std::to_string(pages)+
        " · "+std::to_string(data.size())+" bytes");
    std::string accessible;
    const auto page_hex=protocol::TransactionHex(page);
    for (size_t row=0;row<page.size();row+=16) {
        char offset[24];
        std::snprintf(offset,sizeof(offset),"%06zx  ",start+row);
        ImGui::TextUnformatted(offset);
        accessible+=offset;
        for (size_t col=row;col<std::min(row+16,page.size());++col) {
            const auto encoded=page_hex.substr(col*2,2);
            const bool marked=selection && start+col>=selection->offset && start+col-selection->offset<selection->length;
            ImGui::SameLine(0,ImGui::GetFontSize()*0.24f);
            if (marked) {
                const auto cursor=ImGui::GetCursorScreenPos();
                const auto size=ImGui::CalcTextSize(encoded.c_str());
                ImGui::GetWindowDrawList()->AddRectFilled({cursor.x-2,cursor.y-1},{cursor.x+size.x+2,cursor.y+size.y+1},
                    ImGui::GetColorU32(theme.colors.accent),2.f);
                ImGui::PushStyleColor(ImGuiCol_Text,theme.colors.onAccent);
            }
            ImGui::TextUnformatted(encoded.c_str());
            if (marked) ImGui::PopStyleColor();
            accessible+=encoded;
            accessible+=' ';
        }
        accessible+='\n';
    }
    state.presentation.Annotate("document-bytes","Document bytes (hex)",accessible);
    if (selection && selection->length && selection->length<=HEX_PAGE)
        Text(state,"selected-hex","Selected bytes (hex)",protocol::TransactionHex(data.substr(selection->offset,selection->length)));
    const auto selected=selection ? data.substr(selection->offset,selection->length) : data;
    const char* copied=selection ? "Copy selected bytes as hex" : "Copy representation as hex";
    constexpr size_t max_clipboard_bytes=32768; // native clipboard accepts up to 65536 text bytes
    const auto copy_request=CopyRequest(guard.owner.id,"copy-bytes",{});
    Button(state,facts,theme,"copy-bytes",copied,!selected.empty() && selected.size()<=max_clipboard_bytes
        && guard.host.CanSubmit(copy_request).available,[guard,bytes=view.bytes,selection]{
        auto* current=guard.Current();
        if (!current || !current->hex || current->bytes!=bytes || current->selection!=selection) return false;
        auto data=protocol::InspectionBuffer(*guard.owner.content,bytes);
        if (selection) data=data.substr(selection->offset,selection->length);
        if (data.empty() || data.size()>max_clipboard_bytes) return false;
        auto request=CopyRequest(guard.owner.id,"copy-bytes",protocol::TransactionHex(data));
        if (!guard.host.CanSubmit(request).available) return false;
        const auto receipt=guard.host.Submit(request);
        auto& text=std::get<c::CopyIdentifier>(*request.component).value;
        sodium_memzero(text.data(),text.size());
        current->inspection_status=receipt.reason;
        ++current->revision;
        return receipt.accepted;
    },false,nullptr,selected.size()>max_clipboard_bytes ? "Save the bytes to export beyond the clipboard limit." : "");
    theme::SameLineForAction(selection ? "Save selected bytes" : "Save representation");
    Button(state,facts,theme,"save-representation",selection ? "Save selected bytes" : "Save representation",
        !selected.empty() && guard.host.LocalDocuments(),[guard,representation=view.bytes,selection]{
            if (!guard.Current() || !BeginLocalDocument(guard.state,guard.host,LocalDocumentMode::SaveInspection)) return false;
            guard.state.local_document.inspection=InspectionExport{guard.owner,representation,guard.revision,selection};
            return true;
        });
    if (selection) Button(state,facts,theme,"clear-field","Clear byte selection",true,[guard]{
        auto* current=guard.Current();
        if (!current) return false;
        current->selection.reset();
        current->selection_label.clear();
        ++current->revision;
        return true;
    });
    if (!view.inspection_status.empty()) Text(state,"workbench-status","Workbench action status",view.inspection_status);
}

void FieldList(const Guard& guard,Facts& facts,const imkit::Theme& theme,const ImportedView& view)
{
    auto& state=guard.state;
    const auto& document=*guard.owner.content;
    const auto layout=view.hex ? view.bytes : InspectionBytes::Wire;
    const auto fields=protocol::InspectionFields(document,layout,view.node,view.index);
    if (fields.empty()) return;
    if (view.hex && layout==InspectionBytes::Source && document.format==protocol::DocumentFormat::PsbtBase64) {
        Text(state,"field-unmapped","Byte mapping","Base64 source has no direct byte offsets for PSBT fields. Choose Decoded PSBT to map fields.");
        return;
    }
    Text(state,"field-heading","Field inspection","Byte fields");
    const auto first=view.field_page*FIELD_PAGE;
    for (size_t i=first;i<std::min(fields.size(),first+FIELD_PAGE);++i) {
        const auto& field=fields[i];
        const auto mapped=protocol::InspectionSpan(document,layout,field.bytes);
        auto key="field-"+std::to_string(i);
        auto text=field.name;
        if (!mapped || !mapped->length) text+=" · empty";
        Button(state,facts,theme,key,text,bool(mapped),[guard,mapped,layout,name=field.name]{
            auto* current=guard.Current();
            if (!current || !mapped) return false;
            if (!current->hex) {
                current->hex=true;
                const auto page=current->field_page;
                ActivateBytes(guard.state,*current,layout);
                current->field_page=page;
            }
            current->selection=mapped;
            current->selection_label=name;
            current->bytes_page=mapped->offset/HEX_PAGE;
            ++current->revision;
            return true;
        },mapped && mapped==view.selection,nullptr,mapped
            ? "Offset "+std::to_string(mapped->offset)+", "+std::to_string(mapped->length)+" bytes" : "Offset unavailable");
    }
    if (fields.size()<=FIELD_PAGE) return;
    const auto turn=[guard,count=fields.size()](size_t page){
        auto* current=guard.Current();
        if (!current || page>(count-1)/FIELD_PAGE) return false;
        current->field_page=page;
        ++current->revision;
        return true;
    };
    Text(state,"field-range","Visible byte fields",std::to_string(first+1)+"–"+std::to_string(std::min(fields.size(),first+FIELD_PAGE))+" of "+std::to_string(fields.size()));
    Button(state,facts,theme,"fields-back","Previous fields",view.field_page>0,[turn,page=view.field_page]{return page && turn(page-1);});
    ImGui::SameLine();
    Button(state,facts,theme,"fields-next","Next fields",first+FIELD_PAGE<fields.size(),[turn,page=view.field_page]{return turn(page+1);});
}

} // namespace

std::optional<InspectionOwner> CurrentInspectionOwner(const State& state,app::Requests& host)
{
    const auto* doc=state.documents.Active();
    if (!doc || !doc->imported || !doc->imported->parsed || state.semantic_hide_toggle || state.privacy.hidden
        || state.lock_panel.setup || doc->observation==app::ObservationState::Locked
        || (state.recovery && (state.recovery->Locked() || state.recovery->Pending()))) return {};
    const auto& imported=*doc->imported;
    const auto at=state.imported_views.find(doc->id);
    auto wallet=imported.funding ? std::optional(imported.funding->evidence.wallet)
        : at!=state.imported_views.end() ? at->second.wallet : std::nullopt;
    if (wallet && protocol::DocumentNetwork(wallet->network)!=imported.network) wallet.reset();
    const auto* connection=host.Connection();
    return InspectionOwner{doc->id,connection ? connection->Session() : 0,connection ? connection->Revision() : 0,
        doc->draft_revision,state.recovery ? state.recovery->Gate().Epoch() : LockEpoch{},
        connection && connection->Offline(),imported.network,imported.representation,imported.parsed,std::move(wallet)};
}

bool ValidInspectionExport(const State& state,app::Requests& host,const InspectionExport& requested)
{
    const auto at=state.imported_views.find(requested.owner.id);
    if (CurrentInspectionOwner(state,host)!=requested.owner || at==state.imported_views.end()) return false;
    const auto& view=at->second;
    if (!view.hex || view.revision!=requested.view_revision || view.bytes!=requested.representation
        || view.selection!=requested.selection) return false;
    const auto buffer=protocol::InspectionBuffer(*requested.owner.content,requested.representation);
    const auto span=requested.selection;
    return span ? span->length && span->offset<=buffer.size() && span->length<=buffer.size()-span->offset : !buffer.empty();
}

bool ToggleTransactionHex(State& state,app::Requests& host)
{
    const auto owner=CurrentInspectionOwner(state,host);
    if (!owner || Obscured(state)) return false;
    const auto at=state.imported_views.find(owner->id);
    if (at==state.imported_views.end() || at->second.inspection_owner!=owner) return false;
    auto& view=at->second;
    view.hex=!view.hex;
    const auto bytes=view.hex && !owner->content->wire.empty() ? InspectionBytes::Wire : InspectionBytes::Source;
    ActivateBytes(state,view,bytes);
    return true;
}

void DrawTransactionWorkbench(State& state,app::Requests& host,Facts& facts,const imkit::Theme& theme,float scale)
{
    const auto owner=CurrentInspectionOwner(state,host);
    if (!owner || Obscured(state)) return;
    const auto& imported=*state.documents.Active()->imported;
    auto& view=state.imported_views[owner->id];
    if (view.inspection_owner!=owner) {
        view.inspection_owner=owner;
        if (view.selected_document.lock()!=owner->content) {
            view.node=InspectionNode::Overview;
            view.tree_group=InspectionNode::Input;
            view.index=0;
            view.tree_page=0;
            view.selected_document=owner->content;
        }
        view.hex=owner->representation==app::DocumentRepresentation::WireHex;
        view.bytes=view.hex ? InspectionBytes::Wire : InspectionBytes::Source;
        ResetBytes(view);
        ++view.revision;
    }
    const Guard guard{state,host,*owner,view.revision};
    if (Button(state,facts,theme,"representation",view.hex ? "Hide hex" : "Show wire hex",true,[guard]{
        return guard.Current() && ToggleTransactionHex(guard.state,guard.host);
    })) return;
    if (view.hex) Hex(guard,facts,theme,view);
    if (!owner->content->transaction) return;
    if (ImGui::BeginTable("transaction-inspection",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV|
        ImGuiTableFlags_SizingStretchProp,{ImGui::GetContentRegionAvail().x,0})) {
        ImGui::TableSetupColumn("Structure",ImGuiTableColumnFlags_WidthFixed,146*scale);
        ImGui::TableSetupColumn("Details",ImGuiTableColumnFlags_WidthStretch,1);
        ImGui::TableNextColumn();
        Tree(guard,facts,theme,view);
        ImGui::TableNextColumn();
        if (!view.hex) {
            switch (view.node) {
            case InspectionNode::Overview: Overview(guard,facts,theme,imported); break;
            case InspectionNode::Input: Input(guard,facts,theme,view,imported); break;
            case InspectionNode::Output: Output(guard,facts,theme,view,imported); break;
            }
        }
        FieldList(guard,facts,theme,view);
        ImGui::EndTable();
    }
    if (!view.hex && !view.inspection_status.empty()) Text(state,"workbench-status","Workbench action status",view.inspection_status);
}
} // namespace roots::gui::ui::shell
