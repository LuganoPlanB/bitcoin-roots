// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/localization.h"
#include "ui/components/presentation.h"
#include "ui/components/native_input.h"
#include <algorithm>
#include <cmath>
namespace roots::gui::ui::components {
namespace {
AmountPresentation Text(std::string value,bool copy) { return {value,value,value,copy?std::optional(value):std::nullopt}; }
void FocusLastItem() {
    if(!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup))ImGui::SetWindowFocus(nullptr);
    ImGui::SetWindowFocus();ImGui::SetKeyboardFocusHere(-1);ImGui::SetNavCursorVisible(true);
}
}
AmountPresentation AmountPrivacy::Present(std::optional<Money> amount) const
{
    if (hidden) return Text(locale::Text("Hidden"),false);
    if (!amount) return Text(locale::Text("Unavailable"),false);
    auto value = Text(locale::Btc(*amount, trim_decimals), false);
    value.copy = amount->btc();
    return value;
}
AmountPresentation AmountPrivacy::Draft(std::string_view draft) const
{
    if (hidden) return Text(locale::Text("Hidden"),false);
    return Text(std::string(draft),false);
}
ChartPresentation AmountPrivacy::Chart(std::span<const Money> values) const
{
    if (hidden) return {{},"Amount chart hidden"};
    Money total=Money::Satoshis(0);
    for (const auto value:values) {
        const auto next=total.Add(value);
        if (!next || value.sats()<0) return {{},"Amount chart unavailable"};
        total=*next;
    }
    ChartPresentation result{{},"Exact amounts (BTC):"};
    for (const auto value:values) {
        // Conversion is confined to non-authoritative chart geometry. Exact
        // labels/copy/semantic alternatives retain Money's integer formatting.
        result.percentages.push_back(total.sats()?float(100.L*value.sats()/total.sats()):0.F);
        result.alternative+=' ';result.alternative+=Present(value).semantic;
    }
    return result;
}
ReviewPresentation DeliberateReview(Money amount,std::string_view full_destination,AmountPrivacy privacy)
{
    privacy.trim_decimals=false;
    return {privacy.Present(amount),std::string(full_destination),privacy.hidden};
}
void PresentationFrame::Begin(std::string next,std::function<std::string()> current) {
    if(error_focus && (error_focus->scope!=next || error_focus->draw_generation<generation+1))error_focus.reset();
    if(native_input)native_input->BeginView(next);
    exchange.BindOwner();publication_start=publication_end=0;parents.clear();item_facts.clear();scope=std::move(next);current_scope=std::move(current);pending=exchange.Drain(scope);deferred.clear();
    focus_intents.erase(std::remove_if(focus_intents.begin(),focus_intents.end(),[&](const auto& f){return f.scope!=scope || f.draw_generation<generation+1;}),focus_intents.end());
    strings.clear();chart_samples.clear();suppressed=false;text_overflow=false;text_bytes=0;frame.Begin(++generation);
}
std::string_view PresentationFrame::Own(std::string_view text) {
    if(text_overflow || frame.Tree().nodes.size()==nodes.size() || text.size()>SemanticExchange::TextCapacity-text_bytes){text_overflow=true;return {};}
    text_bytes+=text.size();strings.emplace_back(text);return strings.back();
}
void PresentationFrame::DialogOnly() { publication_start=Tree().nodes.size();publication_end=0;parents.clear();suppressed=false; }
void PresentationFrame::Group(const char* id,const char* name,ax::SemanticRole role,ImVec2 minimum,ImVec2 maximum) {
    ax::SemanticNode n;n.id=ImGui::GetID(id);n.parent=parents.empty()?0:parents.back();n.role=role;n.minimum=minimum;n.maximum=maximum;
    n.name=Own(name);frame.Add(n);if(auto* f=ItemFacts(n.id))f->showing=ImGui::IsRectVisible(minimum,maximum);parents.push_back(n.id);
}
void PresentationFrame::Defer(const SemanticRequest& request,std::function<bool(const SemanticRequest&)> apply,std::function<bool()> available) {
    if(deferred.size()==SemanticExchange::QueueCapacity){exchange.Finish(request.sequence,ActionResult::Overflow);return;}
    if(std::any_of(deferred.begin(),deferred.end(),[&](const auto& d){return d.request.sequence==request.sequence;})){exchange.Finish(request.sequence,ActionResult::Duplicate);return;}
    deferred.push_back({request,std::move(apply),std::move(available)});
}
bool PresentationFrame::Seal() {
    const auto now=current_scope?current_scope():scope;
    auto tree=Tree();tree.nodes=tree.nodes.subspan(publication_start,(publication_end?publication_end:tree.nodes.size())-publication_start);
    const auto prior=exchange.Published();
    const auto packet=SemanticExchange::Prepare(tree,now,now==scope,item_facts);const bool published=packet->valid;
    // Retire the preceding public packet while admitted effects are applied.
    // Readers never observe a newly hidden/closed model through an old current
    // packet. Immutable packets already acquired by readers remain historical.
    exchange.Publish(tree,now,false);
    const auto dispatch_epoch=exchange.Published();
    const auto unavailable=[&]{return now!=scope || !prior || !prior->valid?ActionResult::Stale:Tree().overflow?ActionResult::Overflow:ActionResult::Unavailable;};
    for(const auto& r:pending)exchange.Finish(r.sequence,unavailable());pending.clear();
    // No external handler (including edit mutation/host calls) executes until
    // all annotations, active-layer filtering and packet validation finish.
    for(auto& d:deferred) {
        const auto& r=d.request;
        if(native_input && native_input->NativeDialogActive()){exchange.Finish(r.sequence,ActionResult::Unavailable);continue;}
        if(!published || !packet || !packet->valid){exchange.Finish(r.sequence,unavailable());continue;}
        const auto latest=exchange.Published();
        if(!latest || !dispatch_epoch || latest->generation!=dispatch_epoch->generation || (current_scope && current_scope()!=scope)){exchange.Finish(r.sequence,ActionResult::Stale);continue;}
        const auto* n=packet->Find(r.id);
        if(!n || n->state.disabled || !ax::Supports(n->actions,r.action)){exchange.Finish(r.sequence,ActionResult::Unavailable);continue;}
        if(r.action==ax::SemanticAction::Focus){focus_intents.push_back({r,scope,generation+1});exchange.Finish(r.sequence,ActionResult::Applied);continue;}
        if(d.available && !d.available()){exchange.Finish(r.sequence,ActionResult::Unavailable);continue;}
        const bool accepted=d.apply && d.apply(r);
        exchange.Finish(r.sequence,accepted?ActionResult::Applied:ActionResult::Rejected);
    }
    deferred.clear();
    // A validated action may legitimately close/switch/hide a view. Retire its
    // just-rendered packet before any external reader can use it as current.
    const auto after=current_scope?current_scope():scope;
    exchange.Publish(tree,after,published && after==now,item_facts);
    return published;
}
std::optional<SemanticRequest> PresentationFrame::Interact(const char* name,ax::SemanticRole role,ax::SemanticAction actions,bool disabled,bool selected,std::string_view value,std::string_view description,bool invalid,bool required) {
    disabled|=bool(ImGui::GetItemFlags()&ImGuiItemFlags_Disabled) || (native_input && native_input->NativeDialogActive());
    ax::SemanticNode n;n.id=ImGui::GetItemID();if(!n.id)return {};
    n.name=Own(name?name:"");n.value=Own(value);n.description=Own(description);
    n.parent=parents.empty()?0:parents.back();n.role=role;n.actions=actions;n.state.disabled=disabled;n.state.selected=selected;n.state.invalid=invalid;n.state.required=required;n.state.checked=(role==ax::SemanticRole::Toggle || role==ax::SemanticRole::Radio) && selected;n.state.readOnly=!ax::Supports(actions,ax::SemanticAction::SetValue);
    if(suppressed)n.actions=ax::SemanticAction::None;
    ax::AnnotateLastItem(frame,n);if(auto* f=ItemFacts(n.id))f->showing=ImGui::IsItemVisible();
    if(suppressed)return {};
    if(error_focus && error_focus->id==n.id){FocusLastItem();error_focus.reset();}
    for(auto it=focus_intents.begin();it!=focus_intents.end();) {
        if(it->scope!=scope){it=focus_intents.erase(it);continue;}
        if(it->request.id!=n.id){++it;continue;}
        if(!disabled && !Tree().overflow && !it->realizing){
            FocusLastItem();it->realizing=true;it->draw_generation=generation+1;++it;
        }else{
            // FocusApi applies on the following NewFrame and may clear the
            // cursor through SetFocusID. Restore it once on the real target;
            // otherwise native Enter/Space cannot activate a focused row.
            if(it->realizing && ImGui::IsItemFocused())ImGui::SetNavCursorVisible(true);
            it=focus_intents.erase(it);
        }
    }
    std::optional<SemanticRequest> result;bool focus_seen{};
    for(auto it=pending.begin();it!=pending.end();) {
        if(it->id!=n.id){++it;continue;}
        const auto r=std::move(*it);it=pending.erase(it);
        const auto published=exchange.Published();if(!published || !published->valid || published->generation!=r.generation){exchange.Finish(r.sequence,ActionResult::Stale);continue;}
        if((current_scope && current_scope()!=scope)){exchange.Finish(r.sequence,ActionResult::Stale);continue;}
        if(Tree().overflow){exchange.Finish(r.sequence,ActionResult::Overflow);continue;}
        if(disabled || (ImGui::GetItemFlags()&ImGuiItemFlags_Disabled) || !ax::Supports(actions,r.action)){exchange.Finish(r.sequence,ActionResult::Unavailable);continue;}
        if(r.action==ax::SemanticAction::Focus){if(focus_seen)exchange.Finish(r.sequence,ActionResult::Duplicate);else{Defer(r,{});focus_seen=true;}continue;}
        if(result){exchange.Finish(r.sequence,ActionResult::Duplicate);continue;}result=r;
    }return result;
}
SemanticItemFacts* PresentationFrame::ItemFacts(ax::StableId id) {
    for(auto& f:item_facts)if(f.id==id)return &f;if(item_facts.size()==SemanticExchange::NodeCapacity){text_overflow=true;return nullptr;}item_facts.push_back({id});return &item_facts.back();
}
void PresentationFrame::BindTextPosition(ax::StableId id,std::string_view text,TextPosition bytes) {
    if(bytes.caret<0)return;auto* f=ItemFacts(id);if(!f)return;
    const auto offset=[&](int at){if(at<0 || size_t(at)>text.size())return -1;return int(std::count_if(text.begin(),text.begin()+at,[](unsigned char c){return (c&0xc0)!=0x80;}));};
    f->position={offset(bytes.caret),offset(bytes.start),offset(bytes.end)};
}
void PresentationFrame::BindShortcut(ax::StableId id,std::string_view text) {
    const auto owned=Own(text);if(auto* f=ItemFacts(id))f->shortcut=owned;
}
bool PresentationFrame::Annotate(const char* id,const char* name,std::string_view value,imkit::accessibility::SemanticRole role,bool disabled,bool read_only,bool busy)
{
    const auto owned_name=Own(name?name:"");const auto owned_value=Own(value);imkit::accessibility::SemanticNode node;
    node.parent=parents.empty()?0:parents.back();node.id=ImGui::GetID(id);node.name=owned_name;node.value=owned_value;node.role=role;
    node.state.disabled=disabled;node.state.readOnly=read_only;node.state.busy=busy;
    // This semantic ID names static content, not the preceding widget's ID.
    // ImKit's AnnotateLastItem also inherits focus, which an EndGroup can leave
    // on a neighbouring active editor. Public geometry composition avoids
    // advertising that editor's focus on a validation/status text node.
    node.minimum=ImGui::GetItemRectMin();node.maximum=ImGui::GetItemRectMax();
    node.state.disabled|=bool(ImGui::GetItemFlags()&ImGuiItemFlags_Disabled);
    const bool result=frame.Add(node);if(auto* f=ItemFacts(node.id))f->showing=ImGui::IsItemVisible();return result;
}
bool PresentationFrame::RangeValue(ax::StableId id,double current,double minimum,double maximum,double step){
    if(!std::isfinite(current) || !std::isfinite(minimum) || !std::isfinite(maximum) || !std::isfinite(step) || minimum>current || current>maximum || step<=0)return false;
    const auto count=frame.Tree().nodes.size();for(size_t i=0;i<count;++i)if(nodes[i].id==id && nodes[i].role==ax::SemanticRole::Slider){auto& n=nodes[i];n.numericValue=current;n.minimumValue=minimum;n.maximumValue=maximum;n.smallChange=step;n.largeChange=step;n.state.readOnly=!(ax::Supports(n.actions,ax::SemanticAction::Increment) || ax::Supports(n.actions,ax::SemanticAction::Decrement) || ax::Supports(n.actions,ax::SemanticAction::SetValue));n.state.disabled|=minimum==maximum;return true;}return false;
}
void PresentationFrame::Progress(const char* id,const char* name,std::string_view value,std::optional<unsigned> per_mille,bool busy) {
    ax::SemanticNode n;n.id=ImGui::GetID(id);n.parent=parents.empty()?0:parents.back();n.name=Own(name);n.role=ax::SemanticRole::Progress;
    const bool known=per_mille && *per_mille<=1000;const std::string alternative=std::string(value)+(known?" / progress "+std::to_string(*per_mille)+" per mille":" / progress unavailable");n.value=Own(alternative);
    n.state.readOnly=true;n.state.busy=busy;n.maximumValue=known?1000:0;n.numericValue=known?*per_mille:0;n.largeChange=100;
    n.minimum=ImGui::GetItemRectMin();n.maximum=ImGui::GetItemRectMax();n.state.disabled=bool(ImGui::GetItemFlags()&ImGuiItemFlags_Disabled);
    frame.Add(n);if(auto* f=ItemFacts(n.id))f->showing=ImGui::IsItemVisible();
}
std::string PresentationFrame::Snapshot() const
{
    std::string text;
    for (const auto& node:Tree().nodes) {
        text+=std::string(node.name)+" | "+std::string(node.value)+" | "+(node.state.disabled?"disabled":"readable")+
            " | description="+std::string(node.description)+" | numericValue="+std::to_string(node.numericValue)+"\n";
    }
    return text;
}
bool ViewPolish::ClaimIdentityAccent() { if (accent_used) return false;accent_used=true;return true; }
float ViewPolish::PrivacyFeedback(ImGuiID id,bool hidden,bool reduced_motion,float delta,int frame)
{
    return animation.Update(id,hidden?1.F:0.F,delta,.12F,frame,!reduced_motion,imkit::Easing::EaseOut);
}
}

namespace roots::gui::ui::components {
bool SemanticExchange::BindOwner() { std::lock_guard lock(mutex);if(owner==std::thread::id{})owner=std::this_thread::get_id();return owner==std::this_thread::get_id(); }
bool SemanticExchange::Close() {
    if(!BindOwner())return false;std::lock_guard lock(mutex);if(closed)return true;closed=true;
    while(!queued.empty()){if(receipts.size()==128)receipts.erase(receipts.begin());receipts.push_back({queued.front().sequence,ActionResult::Stale});queued.pop_front();}
    auto invalid=std::make_shared<SemanticPacket>();invalid->generation=packet?packet->generation+1:1;packet=std::move(invalid);return true;
}
const SemanticEntry* SemanticPacket::Find(ax::StableId id) const {for(const auto& n:nodes)if(n.id==id)return &n;return nullptr;}
std::shared_ptr<const SemanticPacket> SemanticExchange::Published() const {std::lock_guard lock(mutex);return packet;}
std::vector<SemanticReceipt> SemanticExchange::Receipts() const {std::lock_guard lock(mutex);return receipts;}
void SemanticExchange::Finish(uint64_t seq,ActionResult result) {
    std::lock_guard lock(mutex);if(receipts.size()==128)receipts.erase(receipts.begin());receipts.push_back({seq,owner==std::this_thread::get_id()?result:ActionResult::WrongThread});
}
ActionResult SemanticExchange::Queue(SemanticRequest request) {
    std::lock_guard lock(mutex);
    return QueueLocked(std::move(request));
}
std::pair<uint64_t,ActionResult> SemanticExchange::QueueNext(uint64_t generation,ax::StableId id,ax::SemanticAction action,std::string value) {
    std::lock_guard lock(mutex);if(watermark==UINT64_MAX)return {0,ActionResult::Overflow};
    const auto sequence=watermark+1;return {sequence,QueueLocked({sequence,generation,id,action,std::move(value)})};
}
bool SemanticExchange::CancelQueued(uint64_t sequence) {
    if(!BindOwner())return false;std::lock_guard lock(mutex);
    const auto it=std::find_if(queued.begin(),queued.end(),[&](const auto& r){return r.sequence==sequence;});if(it==queued.end())return false;
    queued.erase(it);if(receipts.size()==128)receipts.erase(receipts.begin());receipts.push_back({sequence,ActionResult::Stale});return true;
}
ActionResult SemanticExchange::QueueLocked(SemanticRequest request) {
    const auto reject=[&](ActionResult r){if(receipts.size()==128)receipts.erase(receipts.begin());receipts.push_back({request.sequence,r});return r;};
    if(!request.sequence || request.sequence<=watermark)return reject(ActionResult::Duplicate);
    watermark=request.sequence;
    if(closed)return reject(ActionResult::Stale);
    if(request.value.size()>ValueCapacity || queued.size()==QueueCapacity)return reject(ActionResult::Overflow);
    if(!packet || !packet->valid || request.generation!=packet->generation)return reject(ActionResult::Stale);
    const auto* node=packet->Find(request.id);if(!node || node->state.disabled)return reject(ActionResult::Unavailable);
    const auto bits=static_cast<unsigned>(request.action);
    if(!bits || (bits&(bits-1)) || !ax::Supports(node->actions,request.action))return reject(ActionResult::Unsupported);
    if(request.action!=ax::SemanticAction::SetValue && !request.value.empty())return reject(ActionResult::Unsupported);
    queued.push_back(std::move(request));return ActionResult::Queued;
}
std::shared_ptr<const SemanticPacket> SemanticExchange::Prepare(const ax::AccessibilityTree& tree,std::string scope,bool coherent,std::span<const SemanticItemFacts> positions) {
    auto next=std::make_shared<SemanticPacket>();next->generation=tree.generation;next->scope=std::move(scope);
    next->overflow=tree.overflow || tree.nodes.size()>NodeCapacity || positions.size()>NodeCapacity;
    size_t bytes{};
    if(!next->overflow)for(const auto& n:tree.nodes) {
        for(const auto text:{n.name,n.description,n.value}) {
            if(text.size()>TextCapacity-bytes){next->overflow=true;break;}bytes+=text.size();
        }
        if(next->overflow)break;
        if(n.children.size()>(TextCapacity-bytes)/sizeof(ax::StableId)){next->overflow=true;break;}bytes+=n.children.size()*sizeof(ax::StableId);
    }
    if(!next->overflow)for(const auto& p:positions){if(p.shortcut.size()>TextCapacity-bytes){next->overflow=true;break;}bytes+=p.shortcut.size();}
    // Check bounds before validation walks any borrowed child/text storage.
    next->valid=coherent && tree.generation && !next->overflow && tree.Validate();
    if(next->valid)for(const auto& n:tree.nodes) {
        if(!std::isfinite(n.minimum.x)||!std::isfinite(n.minimum.y)||!std::isfinite(n.maximum.x)||!std::isfinite(n.maximum.y)||
           n.maximum.x<n.minimum.x||n.maximum.y<n.minimum.y||!std::isfinite(n.numericValue)||!std::isfinite(n.minimumValue)||!std::isfinite(n.maximumValue)||!std::isfinite(n.smallChange)||!std::isfinite(n.largeChange)){next->valid=false;break;}
        next->nodes.push_back({n.id,n.parent,n.labelledBy,n.role,std::string(n.name),std::string(n.description),std::string(n.value),n.state,n.minimum,n.maximum,
            {n.children.begin(),n.children.end()},n.actions,n.numericValue,n.minimumValue,n.maximumValue,n.smallChange,n.largeChange});
    }
    if(next->valid) {
        // Derive all child lists from validated parents. The immutable packet
        // never retains a borrowed child span or an inconsistent second graph.
        for(auto& n:next->nodes)n.children.clear();
        for(const auto& n:next->nodes)if(n.parent)for(auto& p:next->nodes)if(p.id==n.parent)p.children.push_back(n.id);
    }
    if(next->valid)for(const auto& p:positions)for(auto& n:next->nodes)if(n.id==p.id){const auto count=std::count_if(n.value.begin(),n.value.end(),[](unsigned char c){return (c&0xc0)!=0x80;});const auto& v=p.position;if(v.caret < -1 || v.caret>count || v.start < -1 || v.end < -1 || v.start>v.end || v.end>count || ((v.start<0)!=(v.end<0))){next->valid=false;break;}n.text_position=v;if(p.showing)n.showing=*p.showing;n.shortcut=p.shortcut;}
    if(!next->valid)next->nodes.clear();
    return next;
}
bool SemanticExchange::Publish(const ax::AccessibilityTree& tree,std::string scope,bool coherent,std::span<const SemanticItemFacts> positions) {
    if(!BindOwner())return false;auto next=Prepare(tree,std::move(scope),coherent,positions);
    std::lock_guard lock(mutex);if(closed)return false;packet=std::move(next);return packet->valid;
}
std::vector<SemanticRequest> SemanticExchange::Drain(std::string_view scope) {
    std::vector<SemanticRequest> result;std::lock_guard lock(mutex);if(std::this_thread::get_id()!=owner)return {};
    while(!queued.empty()) {
        auto request=std::move(queued.front());queued.pop_front();
        if(!packet || !packet->valid || packet->scope!=scope || request.generation!=packet->generation) {
            if(receipts.size()==128)receipts.erase(receipts.begin());receipts.push_back({request.sequence,ActionResult::Stale});
        } else result.push_back(std::move(request));
    }return result;
}
}
