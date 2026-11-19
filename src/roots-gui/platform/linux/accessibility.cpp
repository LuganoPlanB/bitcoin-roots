// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/localization.h"
#include "platform/linux/accessibility.h"
#include "ui/components/semantic_packet.h"
#if defined(__linux__)
#include "platform/linux/atspi_interfaces.h"
#include <gio/gio.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <stdexcept>
#include <thread>
#include <utility>
namespace roots::gui::platform {
namespace c=ui::components;namespace ax=c::ax;
namespace {
constexpr auto Root="/org/a11y/atspi/accessible/root",Window="/org/a11y/atspi/accessible/window",Null="/org/a11y/atspi/null",Cache="/org/a11y/atspi/cache";
constexpr auto Prefix="org.a11y.atspi.";
using Clock=std::chrono::steady_clock;
struct Role {unsigned id;const char* name;};
Role NativeRole(ax::SemanticRole r) {
    using R=ax::SemanticRole;
    switch(r){
    case R::Button:return {43,"push button"};case R::Toggle:return {62,"toggle button"};case R::Radio:return {44,"radio button"};
    case R::ComboBox:return {11,"combo box"};case R::TextField:return {79,"entry"};case R::Dialog:return {16,"dialog"};case R::Toolbar:return {63,"tool bar"};
    case R::TabList:return {38,"page tab list"};case R::Tab:return {37,"page tab"};case R::Tree:return {65,"tree"};case R::TreeItem:return {91,"tree item"};
    case R::Grid:return {55,"table"};case R::Row:return {90,"table row"};case R::Cell:return {56,"table cell"};case R::Status:return {54,"status bar"};
    case R::Alert:return {2,"alert"};case R::Progress:return {42,"progress bar"};case R::Menu:return {33,"menu"};case R::MenuItem:return {35,"menu item"};
    case R::Text:return {61,"text"};case R::Slider:return {51,"slider"};case R::Meter:return {103,"level bar"};default:return {39,"panel"};}
}
std::string ValidError(GError* error){std::string result=error?error->message:"Accessibility transport failed.";if(error)g_error_free(error);return result;}
GVariant* Empty(const char* type){return g_variant_new_array(G_VARIANT_TYPE(type),nullptr,0);}
bool Utf8(const std::string& s){return s.find('\0')==std::string::npos && g_utf8_validate(s.data(),s.size(),nullptr);}
int Length(const std::string& s){return int(g_utf8_strlen(s.c_str(),s.size()));}
std::string Slice(const std::string& s,int start,int end){return std::string(g_utf8_offset_to_pointer(s.c_str(),start),g_utf8_offset_to_pointer(s.c_str(),end));}
std::string KeyBinding(std::string_view label) {
    if(label.empty())return {};std::string out;
    for(;;){const auto plus=label.find('+');if(plus==std::string_view::npos)break;const auto part=label.substr(0,plus);if(part=="Ctrl")out+="<Control>";else if(part=="Shift")out+="<Shift>";else if(part=="Alt")out+="<Alt>";else return {};label.remove_prefix(plus+1);}
    if(label.size()!=1)return {};out+=label==","?"comma":std::string(1,g_ascii_tolower(label[0]));return ";;"+out;
}
std::string NativeName(const c::SemanticEntry& n) {
    // Status bars are containers to native readers; their Text/value is not
    // necessarily presented by object navigation. Keep the status role and
    // include only the current, already privacy-projected visible content.
    if((n.role==ax::SemanticRole::Status || n.role==ax::SemanticRole::Alert) && !n.value.empty())return n.value.starts_with(n.name)?n.value:n.name+": "+n.value;
    return n.name;
}
std::vector<std::pair<ax::SemanticAction,const char*>> Actions(const c::SemanticEntry& n){
    std::vector<std::pair<ax::SemanticAction,const char*>> result;
    for(const auto [action,name]:{std::pair{ax::SemanticAction::Press,"press"},{ax::SemanticAction::Toggle,"toggle"},{ax::SemanticAction::Select,"select"},{ax::SemanticAction::Expand,"expand"},{ax::SemanticAction::Collapse,"collapse"},{ax::SemanticAction::Dismiss,"dismiss"},{ax::SemanticAction::Increment,"increment"},{ax::SemanticAction::Decrement,"decrement"}})
        if(ax::Supports(n.actions,action))result.push_back({action,name});return result;
}
}
struct LinuxAccessibility::Impl:std::enable_shared_from_this<Impl> {
    c::SemanticExchange& exchange;const std::thread::id owner=std::this_thread::get_id();
    GMainContext* context=g_main_context_new();GCancellable* cancel=g_cancellable_new();GDBusConnection* session{},*bus{};GDBusNodeInfo* interfaces{};
    AccessibilityStatus status;unsigned jobs{},registrations_live{};uint64_t serial{};int app_id{},x{},y{},width{},height{};bool active{},last_active{},have_active{},closing{},failed{};std::atomic_bool disconnected{false};gulong closed_handler{};
    std::string parent_bus,parent_path=Null;std::shared_ptr<const c::SemanticPacket> packet;
    struct Object {ax::StableId id{};int kind{};std::vector<unsigned> registrations;std::vector<std::string> interfaces;};
    std::map<std::string,Object> objects;
    struct Pending {uint64_t sequence;GDBusMethodInvocation* invocation;Clock::time_point deadline;};std::vector<Pending> pending;
    explicit Impl(c::SemanticExchange& value):exchange(value){}
    void Owner() const {if(owner!=std::this_thread::get_id())throw std::logic_error("AT-SPI adapter requires native owner thread");}
    struct Context {GMainContext* value;explicit Context(GMainContext* c):value(c){g_main_context_push_thread_default(c);}~Context(){g_main_context_pop_thread_default(value);}};
    void* Hold(){++jobs;status.settled=false;return new std::shared_ptr<Impl>(shared_from_this());}
    static std::shared_ptr<Impl> Take(void* raw){std::unique_ptr<std::shared_ptr<Impl>> p(static_cast<std::shared_ptr<Impl>*>(raw));auto self=*p;--self->jobs;return self;}
    void Fail(std::string error){failed=true;status.error=std::move(error);status.registered=false;status.connected=false;g_cancellable_cancel(cancel);CancelPending();Unregister();packet.reset();}
    void Start(){Owner();Context push(context);GError* error{};interfaces=g_dbus_node_info_new_for_xml(AtspiInterfaces,&error);if(!interfaces){Fail(ValidError(error));return;}g_bus_get(G_BUS_TYPE_SESSION,cancel,SessionReady,Hold());}
    static void SessionReady(GObject*,GAsyncResult* result,void* data){auto s=Take(data);GError* error{};auto* connection=g_bus_get_finish(result,&error);if(connection)g_dbus_connection_set_exit_on_close(connection,false);if(s->closing || s->failed){if(connection)g_object_unref(connection);if(error)g_error_free(error);return;}if(!connection){s->Fail(ValidError(error));return;}s->session=connection;g_dbus_connection_set_exit_on_close(connection,false);Context push(s->context);g_dbus_connection_call(connection,"org.a11y.Bus","/org/a11y/bus","org.a11y.Bus","GetAddress",nullptr,G_VARIANT_TYPE("(s)"),G_DBUS_CALL_FLAGS_NONE,500,s->cancel,AddressReady,s->Hold());}
    static void AddressReady(GObject* source,GAsyncResult* result,void* data){auto s=Take(data);GError* error{};auto* reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),result,&error);if(s->closing || s->failed){if(reply)g_variant_unref(reply);if(error)g_error_free(error);return;}if(!reply){s->Fail(ValidError(error));return;}const char* address{};g_variant_get(reply,"(&s)",&address);Context push(s->context);g_dbus_connection_new_for_address(address,GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),nullptr,s->cancel,BusReady,s->Hold());g_variant_unref(reply);}
    static void BusReady(GObject*,GAsyncResult* result,void* data){auto s=Take(data);GError* error{};auto* connection=g_dbus_connection_new_for_address_finish(result,&error);if(connection)g_dbus_connection_set_exit_on_close(connection,false);if(s->closing || s->failed){if(connection){g_dbus_connection_close_sync(connection,nullptr,nullptr);g_object_unref(connection);}if(error)g_error_free(error);return;}if(!connection){s->Fail(ValidError(error));return;}s->bus=connection;g_dbus_connection_set_exit_on_close(connection,false);s->status.connected=true;s->status.bus_name=g_dbus_connection_get_unique_name(connection);s->closed_handler=g_signal_connect_data(connection,"closed",G_CALLBACK(BusClosed),new std::shared_ptr<Impl>(s),SignalFreed,GConnectFlags(0));Context push(s->context);if(!s->Add(Root,0,1)||!s->Add(Window,0,2)||!s->Add(Cache,0,3))return;
        g_dbus_connection_call(connection,"org.a11y.atspi.Registry",Root,"org.a11y.atspi.Socket","Embed",g_variant_new("((so))",s->status.bus_name.c_str(),Root),G_VARIANT_TYPE("((so))"),G_DBUS_CALL_FLAGS_NONE,1000,s->cancel,Embedded,s->Hold());}
    static void Embedded(GObject* source,GAsyncResult* result,void* data){auto s=Take(data);GError* error{};auto* reply=g_dbus_connection_call_finish(G_DBUS_CONNECTION(source),result,&error);if(s->closing || s->failed){if(reply)g_variant_unref(reply);if(error)g_error_free(error);return;}if(!reply){s->Fail(ValidError(error));return;}const char* name{},*path{};g_variant_get(reply,"((&s&o))",&name,&path);s->parent_bus=name;s->parent_path=path;s->status.registered=true;g_variant_unref(reply);}
    static void SignalFreed(void* data,GClosure*){delete static_cast<std::shared_ptr<Impl>*>(data);}
    static void BusClosed(GDBusConnection*,gboolean,GError*,void* data){const auto keep=*static_cast<std::shared_ptr<Impl>*>(data);keep->disconnected=true;}
    const c::SemanticEntry* Node(const Object& o) const {return !o.kind && packet && packet->valid?packet->Find(o.id):nullptr;}
    std::string Path(ax::StableId id) const {for(const auto& [path,o]:objects)if(!o.kind && o.id==id)return path;return Null;}
    std::string Parent(const Object& o) const {if(o.kind==1)return parent_path;if(o.kind==2)return Root;const auto* n=Node(o);return n && n->parent?Path(n->parent):Window;}
    std::vector<std::string> Children(const Object& o) const {if(o.kind==1)return {Window};std::vector<std::string> out;if(!packet || !packet->valid)return out;if(o.kind==2){for(const auto& n:packet->nodes)if(!n.parent)out.push_back(Path(n.id));}else if(const auto* n=Node(o))for(auto key:n->children)out.push_back(Path(key));return out;}
    std::vector<std::string> Interfaces(const Object& o) const {
        if(o.kind==3)return {std::string(Prefix)+"Cache"};std::vector<std::string> out{std::string(Prefix)+"Accessible"};
        const auto add=[&](const char* name){out.push_back(std::string(Prefix)+name);};if(o.kind==1){add("Application");return out;}add("Component");if(o.kind==2)return out;
        const auto* n=Node(o);if(!n)return out;using R=ax::SemanticRole;
        if(!Actions(*n).empty())add("Action");
        if(n->role==R::TextField || n->role==R::Text || n->role==R::Cell || n->role==R::Row || n->role==R::Status || n->role==R::Alert)add("Text");
        if(ax::Supports(n->actions,ax::SemanticAction::SetValue) && !n->state.readOnly)add("EditableText");
        if(n->role==R::TabList || n->role==R::Grid || n->role==R::Tree)add("Selection");
        if(n->role==R::Grid)add("Table");if(n->role==R::Cell)add("TableCell");
        if(n->role==R::Progress || n->role==R::Meter || n->role==R::Slider)add("Value");return out;
    }
    static void Call(GDBusConnection*,const char*,const char* path,const char* interface,const char* method,GVariant* args,GDBusMethodInvocation* invocation,void* user){auto keep=*static_cast<std::shared_ptr<Impl>*>(user);auto& s=*keep;s.Owner();const auto it=s.objects.find(path);if(s.closing || it==s.objects.end()){Error(invocation,"Defunct","Accessible object has been retired.");return;}s.Method(path,it->second,interface,method,args,invocation);}
    static GVariant* Get(GDBusConnection*,const char*,const char* path,const char* interface,const char* property,GError** error,void* user){auto keep=*static_cast<std::shared_ptr<Impl>*>(user);auto& s=*keep;s.Owner();const auto it=s.objects.find(path);if(it==s.objects.end() || s.closing){g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_CLOSED,"Accessible object is defunct.");return nullptr;}return s.Property(it->second,interface,property,error);}
    static gboolean Set(GDBusConnection*,const char*,const char*,const char* interface,const char* property,GVariant* value,GError** error,void* user){auto keep=*static_cast<std::shared_ptr<Impl>*>(user);auto& s=*keep;s.Owner();if(s.closing){g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_CLOSED,"Accessible object is closed.");return false;}if(std::string_view(interface)=="org.a11y.atspi.Application" && std::string_view(property)=="Id"){s.app_id=g_variant_get_int32(value);return true;}g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_SUPPORTED,"Property is read-only.");return false;}
    bool Add(const std::string& path,ax::StableId id,int kind){Object o{id,kind,{},{}};objects.emplace(path,std::move(o));return Register(path,objects.at(path));}
    bool Register(const std::string& path,Object& o){Context push(context);for(auto key:o.registrations)g_dbus_connection_unregister_object(bus,key);o.registrations.clear();o.interfaces=Interfaces(o);static const GDBusInterfaceVTable table{Call,Get,Set,{nullptr}};
        for(const auto& name:o.interfaces){GError* error{};auto* info=g_dbus_node_info_lookup_interface(interfaces,name.c_str());if(registrations_live==4096){Fail("Accessibility registration retirement capacity exhausted.");return false;}auto* lifetime=new std::shared_ptr<Impl>(shared_from_this());++registrations_live;const auto id=g_dbus_connection_register_object(bus,path.c_str(),info,&table,lifetime,RegistrationFreed,&error);if(!id){--registrations_live;delete lifetime;Fail(ValidError(error));return false;}o.registrations.push_back(id);}return true;}
    static void RegistrationFreed(void* raw){std::unique_ptr<std::shared_ptr<Impl>> lifetime(static_cast<std::shared_ptr<Impl>*>(raw));auto& s=**lifetime;s.Owner();--s.registrations_live;}
    void Unregister(){if(bus)for(auto& [_,o]:objects)for(auto id:o.registrations)g_dbus_connection_unregister_object(bus,id);objects.clear();}
    GVariant* Reference(const std::string& path) const {return g_variant_new("(so)",path==Null?"":status.bus_name.c_str(),path.c_str());}
    GVariant* Names(const std::vector<std::string>& values) const {GVariantBuilder b;g_variant_builder_init(&b,G_VARIANT_TYPE("as"));for(const auto& v:values)g_variant_builder_add(&b,"s",v.c_str());return g_variant_builder_end(&b);}
    GVariant* References(const std::vector<std::string>& values) const {GVariantBuilder b;g_variant_builder_init(&b,G_VARIANT_TYPE("a(so)"));for(const auto& v:values)g_variant_builder_add_value(&b,Reference(v));return g_variant_builder_end(&b);}
    Role RoleOf(const Object& o) const {if(o.kind==1)return {75,"application"};if(o.kind==2)return {23,"frame"};const auto* n=Node(o);return n?NativeRole(n->role):Role{0,"invalid"};}
    std::string Name(const Object& o) const {const auto* n=Node(o);return n?NativeName(*n):o.kind==1 || o.kind==2?"Bitcoin Roots":"";}
    std::string Text(const Object& o) const {const auto* n=Node(o);return n?(!n->value.empty() || n->role==ax::SemanticRole::TextField?n->value:n->name):Name(o);}
    std::vector<std::string> Selected(const Object& o) const {std::vector<std::string> out;for(const auto& path:Children(o)){const auto it=objects.find(path);if(it!=objects.end())if(const auto* n=Node(it->second);n && n->state.selected)out.push_back(path);}return out;}
    GVariant* States(const Object& o) const {uint64_t bits{};const auto set=[&](int bit,bool yes=true){if(yes)bits|=uint64_t(1)<<bit;};const auto* n=Node(o);set(30);set(25,o.kind!=2 || (width>0 && height>0));set(15,o.kind==2 && (width<=0 || height<=0));set(8,!n || !n->state.disabled);set(24,!n || !n->state.disabled);
        if(o.kind==2)set(1,active);if(n){set(3,n->state.busy);set(4,n->state.checked);set(7,!n->state.readOnly && ax::Supports(n->actions,ax::SemanticAction::SetValue));set(9,n->state.expandable);set(10,n->state.expanded);set(11,ax::Supports(n->actions,ax::SemanticAction::Focus));set(12,n->state.focused && active);set(16,n->role==ax::SemanticRole::Dialog);set(22,ax::Supports(n->actions,ax::SemanticAction::Select));set(23,n->state.selected);set(26,n->role==ax::SemanticRole::TextField);set(33,n->state.required);set(36,n->state.invalid);set(41,n->role==ax::SemanticRole::Toggle || n->role==ax::SemanticRole::Radio);set(43,n->state.readOnly);if(!n->showing || n->maximum.x<=0 || n->maximum.y<=0 || n->minimum.x>=width || n->minimum.y>=height)bits&=~(uint64_t(1)<<25);}
        GVariantBuilder b;g_variant_builder_init(&b,G_VARIANT_TYPE("au"));g_variant_builder_add(&b,"u",unsigned(bits));g_variant_builder_add(&b,"u",unsigned(bits>>32));return g_variant_builder_end(&b);}
    int Index(const std::string& path,const Object& o) const {const auto parent=Parent(o);const auto it=objects.find(parent);if(it==objects.end())return -1;const auto children=Children(it->second);const auto pos=std::find(children.begin(),children.end(),path);return pos==children.end()?-1:int(pos-children.begin());}
    std::vector<std::string> Rows(const Object& o) const {std::vector<std::string> out;for(const auto& path:Children(o)){const auto it=objects.find(path);if(it!=objects.end())if(const auto* n=Node(it->second);n && n->role==ax::SemanticRole::Row)out.push_back(path);}return out;}
    int Columns(const Object& o) const {size_t cols=1;for(const auto& path:Rows(o))cols=std::max(cols,Children(objects.at(path)).size());return int(cols);}
    std::string Cell(const Object& o,int row,int col) const {const auto rows=Rows(o);if(row<0 || size_t(row)>=rows.size() || col<0 || col>=Columns(o))return Null;const auto cells=Children(objects.at(rows[row]));return size_t(col)<cells.size()?cells[col]:Null;}
    GVariant* Property(const Object& o,std::string_view interface,std::string_view property,GError** error){const auto* n=Node(o);
        if(interface=="org.a11y.atspi.Accessible"){
            if(property=="Name")return g_variant_new_string(Name(o).c_str());if(property=="Description" || property=="HelpText")return g_variant_new_string(n?n->description.c_str():"");
            if(property=="Parent")return o.kind==1?g_variant_new("(so)",parent_bus.c_str(),parent_path.c_str()):Reference(Parent(o));
            if(property=="ChildCount")return g_variant_new_int32(Children(o).size());if(property=="Locale")return g_variant_new_string(ui::locale::LanguageTag());if(property=="AccessibleId")return g_variant_new_string(o.kind?o.kind==1?"roots-gui":"native-window":std::to_string(o.id).c_str());
        }else if(interface=="org.a11y.atspi.Application"){
            if(property=="ToolkitName")return g_variant_new_string("Roots ImGui");if(property=="Version")return g_variant_new_string("19297 / ImKit 4.1.0");if(property=="AtspiVersion")return g_variant_new_string("2.1");if(property=="Id")return g_variant_new_int32(app_id);
        }else if(interface=="org.a11y.atspi.Action" && n && property=="NActions")return g_variant_new_int32(Actions(*n).size());
        else if(interface=="org.a11y.atspi.Text"){if(property=="CharacterCount")return g_variant_new_int32(Length(Text(o)));if(property=="CaretOffset")return g_variant_new_int32(n?n->text_position.caret:-1);}
        else if(interface=="org.a11y.atspi.Selection" && property=="NSelectedChildren")return g_variant_new_int32(Selected(o).size());
        else if(interface=="org.a11y.atspi.Table"){
            if(property=="NRows")return g_variant_new_int32(Rows(o).size());if(property=="NColumns")return g_variant_new_int32(Columns(o));if(property=="Caption" || property=="Summary")return Reference(Null);if(property=="NSelectedRows")return g_variant_new_int32(Selected(o).size());if(property=="NSelectedColumns")return g_variant_new_int32(0);
        }else if(interface=="org.a11y.atspi.TableCell" && n){if(property=="ColumnSpan" || property=="RowSpan")return g_variant_new_int32(1);const auto row=objects.find(Path(n->parent));const auto* r=row==objects.end()?nullptr:Node(row->second);if(property=="Table")return Reference(r?Path(r->parent):Null);if(property=="Position"){int ri=-1,ci=-1;if(r){const auto table=objects.find(Path(r->parent));if(table!=objects.end()){const auto rows=Rows(table->second);const auto pos=std::find(rows.begin(),rows.end(),Path(n->parent));ri=int(pos-rows.begin());const auto cells=Children(row->second);ci=int(std::find(cells.begin(),cells.end(),Path(n->id))-cells.begin());}}return g_variant_new("(ii)",ri,ci);}}
        else if(interface=="org.a11y.atspi.Value" && n){if(property=="CurrentValue")return g_variant_new_double(n->numeric);if(property=="MinimumValue")return g_variant_new_double(n->minimum_value);if(property=="MaximumValue")return g_variant_new_double(n->maximum_value);if(property=="MinimumIncrement")return g_variant_new_double(n->small_step);if(property=="Text")return g_variant_new_string(n->value.c_str());}
        g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_SUPPORTED,"Property is not supported.");return nullptr;
    }
    static void Error(GDBusMethodInvocation* call,const char* name,const char* text){g_dbus_method_invocation_return_dbus_error(call,(std::string(Prefix)+"Error."+name).c_str(),text);}
    static void Reply(GDBusMethodInvocation* call,GVariant* value){g_dbus_method_invocation_return_value(call,value);}
    static int Int(GVariant* args,int index){auto* v=g_variant_get_child_value(args,index);const int result=g_variant_get_int32(v);g_variant_unref(v);return result;}
    static unsigned Unsigned(GVariant* args,int index){auto* v=g_variant_get_child_value(args,index);const auto result=g_variant_get_uint32(v);g_variant_unref(v);return result;}
    void Queue(const Object& o,ax::SemanticAction action,std::string value,GDBusMethodInvocation* call){const auto* n=Node(o);if(!n || !packet || pending.size()>=c::SemanticExchange::QueueCapacity || n->state.disabled){Reply(call,g_variant_new("(b)",false));return;}const auto [sequence,result]=exchange.QueueNext(packet->generation,o.id,action,std::move(value));if(result!=c::ActionResult::Queued){Reply(call,g_variant_new("(b)",false));return;}pending.push_back({sequence,G_DBUS_METHOD_INVOCATION(g_object_ref(call)),Clock::now()+std::chrono::seconds(2)});}
    void Resolve(){const auto receipts=exchange.Receipts();for(auto it=pending.begin();it!=pending.end();){const auto receipt=std::find_if(receipts.rbegin(),receipts.rend(),[&](const auto& r){return r.sequence==it->sequence;});if(receipt!=receipts.rend() || Clock::now()>=it->deadline){const bool applied=receipt!=receipts.rend() && receipt->result==c::ActionResult::Applied;if(receipt==receipts.rend())exchange.CancelQueued(it->sequence);Reply(it->invocation,g_variant_new("(b)",applied));g_object_unref(it->invocation);it=pending.erase(it);}else ++it;}}
    void CancelPending(){for(auto& p:pending){exchange.CancelQueued(p.sequence);Reply(p.invocation,g_variant_new("(b)",false));g_object_unref(p.invocation);}pending.clear();}
    GVariant* CacheItem(const std::string& path,const Object& o){const auto* n=Node(o);return g_variant_new("(@(so)@(so)@(so)ii@assus@au)",Reference(path),Reference(Root),o.kind==1?g_variant_new("(so)",parent_bus.c_str(),parent_path.c_str()):Reference(Parent(o)),Index(path,o),int(Children(o).size()),Names(o.interfaces),Name(o).c_str(),RoleOf(o).id,n?n->description.c_str():"",States(o));}
    void Event(const std::string& path,const char* signal,const char* detail,int first=0,int second=0,GVariant* value=nullptr,const char* interface="org.a11y.atspi.Event.Object"){
        if(!bus || g_dbus_connection_is_closed(bus))return;g_dbus_connection_emit_signal(bus,nullptr,path.c_str(),interface,signal,g_variant_new("(siiv@a{sv})",detail,first,second,value?value:g_variant_new_int32(0),Empty("{sv}")),nullptr);
    }
    void Bounds(const Object& o,unsigned coords,int& px,int& py,int& w,int& h){const auto* n=Node(o);px=n?int(std::floor(n->minimum.x)):0;py=n?int(std::floor(n->minimum.y)):0;w=n?int(std::ceil(n->maximum.x))-px:width;h=n?int(std::ceil(n->maximum.y))-py:height;if(coords==0){px+=x;py+=y;}else if(coords==2 && n && n->parent){if(const auto* parent=packet->Find(n->parent)){px-=int(parent->minimum.x);py-=int(parent->minimum.y);}}}
    void Method(const std::string& path,const Object& o,std::string_view interface,std::string_view method,GVariant* args,GDBusMethodInvocation* call){const auto* n=Node(o);
        if(interface=="org.a11y.atspi.Accessible"){
            if(method=="GetChildren"){Reply(call,g_variant_new("(@a(so))",References(Children(o))));return;}
            if(method=="GetChildAtIndex"){const auto children=Children(o);const int index=Int(args,0);if(index<0 || size_t(index)>=children.size())Error(call,"InvalidArgs","Child index is outside the current tree.");else Reply(call,g_variant_new("(@(so))",Reference(children[index])));return;}
            if(method=="GetIndexInParent"){Reply(call,g_variant_new("(i)",Index(path,o)));return;}
            if(method=="GetRole"){Reply(call,g_variant_new("(u)",RoleOf(o).id));return;}
            if(method=="GetRoleName" || method=="GetLocalizedRoleName"){Reply(call,g_variant_new("(s)",RoleOf(o).name));return;}
            if(method=="GetState"){Reply(call,g_variant_new("(@au)",States(o)));return;}
            if(method=="GetApplication"){Reply(call,g_variant_new("(@(so))",Reference(Root)));return;}
            if(method=="GetInterfaces"){Reply(call,g_variant_new("(@as)",Names(o.interfaces)));return;}
            if(method=="GetRelationSet"){Reply(call,g_variant_new("(@a(ua(so)))",Empty("(ua(so))")));return;}
            if(method=="GetAttributes"){GVariantBuilder b;g_variant_builder_init(&b,G_VARIANT_TYPE("a{ss}"));g_variant_builder_add(&b,"{ss}","model-id",std::to_string(o.id).c_str());g_variant_builder_add(&b,"{ss}","snapshot-generation",std::to_string(packet?packet->generation:0).c_str());Reply(call,g_variant_new("(@a{ss})",g_variant_builder_end(&b)));return;}
        }else if(interface=="org.a11y.atspi.Application" && method=="GetLocale"){Reply(call,g_variant_new("(s)",ui::locale::LanguageTag()));return;}
        else if(interface=="org.a11y.atspi.Cache" && method=="GetItems"){GVariantBuilder b;g_variant_builder_init(&b,G_VARIANT_TYPE("a((so)(so)(so)iiassusau)"));for(const auto& [key,obj]:objects)if(obj.kind!=3)g_variant_builder_add_value(&b,CacheItem(key,obj));Reply(call,g_variant_new("(@a((so)(so)(so)iiassusau))",g_variant_builder_end(&b)));return;}
        else if(interface=="org.a11y.atspi.Action" && n){const auto actions=Actions(*n);if(method=="GetActions"){GVariantBuilder b;g_variant_builder_init(&b,G_VARIANT_TYPE("a(sss)"));for(const auto& [_,name]:actions)g_variant_builder_add(&b,"(sss)",name,n->description.c_str(),KeyBinding(n->shortcut).c_str());Reply(call,g_variant_new("(@a(sss))",g_variant_builder_end(&b)));return;}const int index=Int(args,0);if(index<0 || size_t(index)>=actions.size()){Error(call,"InvalidArgs","Action index is outside the current action list.");return;}if(method=="DoAction"){Queue(o,actions[index].first,{},call);return;}if(method=="GetName" || method=="GetLocalizedName"){Reply(call,g_variant_new("(s)",actions[index].second));return;}if(method=="GetDescription"){Reply(call,g_variant_new("(s)",n->description.c_str()));return;}if(method=="GetKeyBinding"){Reply(call,g_variant_new("(s)",KeyBinding(n->shortcut).c_str()));return;}}
        else if(interface=="org.a11y.atspi.Component"){
            if(method=="GrabFocus"){Queue(o,ax::SemanticAction::Focus,{},call);return;}
            if(method=="GetLayer"){Reply(call,g_variant_new("(u)",o.kind==2?7U:n && n->role==ax::SemanticRole::Dialog?5U:3U));return;}if(method=="GetMDIZOrder"){Reply(call,g_variant_new("(n)",o.kind==2?0:-1));return;}if(method=="GetAlpha"){Reply(call,g_variant_new("(d)",1.));return;}
            if(method=="GetExtents" || method=="GetPosition" || method=="GetSize"){const unsigned coords=method=="GetSize"?1:Unsigned(args,0);if(coords>2){Error(call,"InvalidArgs","Unknown coordinate type.");return;}int px,py,w,h;Bounds(o,coords,px,py,w,h);Reply(call,method=="GetExtents"?g_variant_new("((iiii))",px,py,w,h):method=="GetPosition"?g_variant_new("(ii)",px,py):g_variant_new("(ii)",w,h));return;}
            if(method=="Contains" || method=="GetAccessibleAtPoint"){const auto coords=Unsigned(args,2);if(coords>2){Error(call,"InvalidArgs","Unknown coordinate type.");return;}int px,py,w,h;Bounds(o,coords,px,py,w,h);const auto qx=Int(args,0),qy=Int(args,1);const bool contains=qx>=px && qy>=py && qx<px+w && qy<py+h;if(method=="Contains")Reply(call,g_variant_new("(b)",contains));else{std::string hit=Null;if(contains){const int reference_x=px,reference_y=py;Bounds(o,0,px,py,w,h);const int sx=qx+px-reference_x,sy=qy+py-reference_y;auto children=Children(o);for(auto it=children.rbegin();it!=children.rend();++it){const auto child=objects.find(*it);if(child==objects.end())continue;if(const auto* node=Node(child->second);node && !node->showing)continue;Bounds(child->second,0,px,py,w,h);if(sx>=px && sy>=py && sx<px+w && sy<py+h){hit=*it;break;}}}Reply(call,g_variant_new("(@(so))",Reference(hit)));}return;}
        }else if(interface=="org.a11y.atspi.EditableText" && n){
            auto value=Text(o);const int count=Length(value);
            if(method=="SetTextContents" || method=="InsertText"){auto* text=g_variant_get_child_value(args,method=="SetTextContents"?0:1);gsize size{};const char* raw=g_variant_get_string(text,&size);if(size>c::SemanticExchange::ValueCapacity){g_variant_unref(text);Reply(call,g_variant_new("(b)",false));return;}std::string insert(raw,size);g_variant_unref(text);if(method=="SetTextContents")value=std::move(insert);else{const int at=Int(args,0),length=Int(args,2);if(at<0 || at>count || length<0){Error(call,"InvalidArgs","Text offset is outside the current Unicode value.");return;}insert.resize(std::min(insert.size(),size_t(length)));if(!Utf8(insert)){Error(call,"InvalidArgs","Byte length splits a UTF-8 character.");return;}value=Slice(value,0,at)+insert+Slice(value,at,count);}Queue(o,ax::SemanticAction::SetValue,std::move(value),call);return;}
            if(method=="DeleteText"){const int a=Int(args,0),b=Int(args,1);if(a<0 || b<a || b>count){Error(call,"InvalidArgs","Text range is outside the current Unicode value.");return;}Queue(o,ax::SemanticAction::SetValue,Slice(value,0,a)+Slice(value,b,count),call);return;}
        }else if(interface=="org.a11y.atspi.Text"){
            const auto text=Text(o);const int count=Length(text);
            if(method=="GetText"){const auto a=Int(args,0);auto b=Int(args,1);if(b==-1)b=count;if(a<0 || b<a || b>count){Error(call,"InvalidArgs","Text range is outside the current Unicode value.");return;}Reply(call,g_variant_new("(s)",Slice(text,a,b).c_str()));return;}
            if(method=="GetCharacterAtOffset"){const int at=Int(args,0);if(at<0 || at>=count){Error(call,"InvalidArgs","Text offset is outside the current Unicode value.");return;}Reply(call,g_variant_new("(i)",int(g_utf8_get_char(g_utf8_offset_to_pointer(text.c_str(),at)))));return;}
            if(method=="GetStringAtOffset" || method=="GetTextAtOffset" || method=="GetTextBeforeOffset" || method=="GetTextAfterOffset"){
                int at=Int(args,0);const auto unit=Unsigned(args,1);if(at<0 || at>count || unit>(method=="GetStringAtOffset"?4U:6U)){Error(call,"InvalidArgs","Unknown text offset or granularity.");return;}int start=0,end=count;const bool character=unit==0,word=unit==1 || (method!="GetStringAtOffset" && unit==2);
                const auto range=[&](int offset){if(character){start=offset;end=std::min(count,offset+1);}else if(word){start=std::min(offset,count);end=start;while(start>0 && !g_unichar_isspace(g_utf8_get_char(g_utf8_offset_to_pointer(text.c_str(),start-1))))--start;while(end<count && !g_unichar_isspace(g_utf8_get_char(g_utf8_offset_to_pointer(text.c_str(),end))))++end;}else{start=0;end=count;}};range(at);if(method=="GetTextBeforeOffset"){if(start)range(start-1);else end=start=0;}if(method=="GetTextAfterOffset"){if(end<count){int next=end;if(!character)while(next<count && g_unichar_isspace(g_utf8_get_char(g_utf8_offset_to_pointer(text.c_str(),next))))++next;range(next);}else start=end=count;}Reply(call,g_variant_new("(sii)",Slice(text,start,end).c_str(),start,end));return;
            }
            if(method=="GetNSelections"){Reply(call,g_variant_new("(i)",n && n->text_position.start>=0 && n->text_position.end>n->text_position.start?1:0));return;}
            if(method=="GetSelection"){if(Int(args,0)!=0 || !n || n->text_position.start<0 || n->text_position.end<=n->text_position.start){Error(call,"InvalidArgs","No such native text selection.");return;}Reply(call,g_variant_new("(ii)",n->text_position.start,n->text_position.end));return;}
            if(method=="GetDefaultAttributes" || method=="GetDefaultAttributeSet"){Reply(call,g_variant_new("(@a{ss})",Empty("{ss}")));return;}
            if(method=="GetAttributes" || method=="GetAttributeRun"){const int at=Int(args,0);if(at<0 || at>count){Error(call,"InvalidArgs","Text offset is outside the current Unicode value.");return;}Reply(call,g_variant_new("(@a{ss}ii)",Empty("{ss}"),0,count));return;}
            if(method=="GetAttributeValue"){Reply(call,g_variant_new("(s)",""));return;}
        }else if(interface=="org.a11y.atspi.Selection"){
            if(method=="GetSelectedChild"){const int index=Int(args,0);const auto selected=Selected(o);if(index<0 || size_t(index)>=selected.size())Error(call,"InvalidArgs","Selected child index is outside the current selection.");else Reply(call,g_variant_new("(@(so))",Reference(selected[index])));return;}
            if(method=="SelectChild" || method=="IsChildSelected"){const auto children=Children(o);const int index=Int(args,0);if(index<0 || size_t(index)>=children.size()){Error(call,"InvalidArgs","Child index is outside the current tree.");return;}const auto& child=objects.at(children[index]);if(method=="SelectChild")Queue(child,ax::SemanticAction::Select,{},call);else Reply(call,g_variant_new("(b)",Node(child) && Node(child)->state.selected));return;}
        }else if(interface=="org.a11y.atspi.Table"){
            const auto rows=Rows(o);const int cols=Columns(o);
            if(method=="GetAccessibleAt"){const auto cell=Cell(o,Int(args,0),Int(args,1));if(cell==Null)Error(call,"InvalidArgs","Cell is outside the current grid.");else Reply(call,g_variant_new("(@(so))",Reference(cell)));return;}
            if(method=="GetIndexAt"){const int row=Int(args,0),col=Int(args,1);Reply(call,g_variant_new("(i)",Cell(o,row,col)==Null?-1:row*cols+col));return;}
            if(method=="GetRowAtIndex" || method=="GetColumnAtIndex"){const int index=Int(args,0);Reply(call,g_variant_new("(i)",index<0 || index>=int(rows.size())*cols?-1:method=="GetRowAtIndex"?index/cols:index%cols));return;}
            if(method=="GetRowDescription" || method=="GetColumnDescription"){const int index=Int(args,0);if(index<0 || (method=="GetRowDescription"?index>=int(rows.size()):index>=cols)){Error(call,"InvalidArgs","Grid index is outside the current grid.");return;}Reply(call,g_variant_new("(s)",method=="GetRowDescription"?Name(objects.at(rows[index])).c_str():index==0?"Public label":!rows.empty() && Cell(o,0,index)!=Null?Name(objects.at(Cell(o,0,index))).c_str():""));return;}
            if(method=="GetRowExtentAt" || method=="GetColumnExtentAt"){Reply(call,g_variant_new("(i)",Cell(o,Int(args,0),Int(args,1))==Null?0:1));return;}
            if(method=="GetRowHeader" || method=="GetColumnHeader"){Reply(call,g_variant_new("(@(so))",Reference(Null)));return;}
            if(method=="GetSelectedRows" || method=="GetSelectedColumns"){GVariantBuilder b;g_variant_builder_init(&b,G_VARIANT_TYPE("ai"));if(method=="GetSelectedRows")for(size_t i=0;i<rows.size();++i)if(Node(objects.at(rows[i]))->state.selected)g_variant_builder_add(&b,"i",int(i));Reply(call,g_variant_new("(@ai)",g_variant_builder_end(&b)));return;}
            if(method=="AddRowSelection"){const int index=Int(args,0);if(index<0 || size_t(index)>=rows.size())Error(call,"InvalidArgs","Row is outside the current grid.");else Queue(objects.at(rows[index]),ax::SemanticAction::Select,{},call);return;}
            if(method=="IsRowSelected" || method=="IsSelected" || method=="IsColumnSelected"){const int index=Int(args,0);const bool selected=method!="IsColumnSelected" && index>=0 && size_t(index)<rows.size() && Node(objects.at(rows[index]))->state.selected;Reply(call,g_variant_new("(b)",selected));return;}
        }else if(interface=="org.a11y.atspi.TableCell"){
            if(method=="GetColumnHeaderCells" || method=="GetRowHeaderCells"){Reply(call,g_variant_new("(@a(so))",Empty("(so)")));return;}
            if(method=="GetRowColumnSpan"){GError* error{};auto* pos=Property(o,interface,"Position",&error);int row=-1,col=-1;if(pos){g_variant_get(pos,"(ii)",&row,&col);g_variant_ref_sink(pos);g_variant_unref(pos);}if(error)g_error_free(error);Reply(call,g_variant_new("(biiii)",row>=0 && col>=0,row,col,1,1));return;}
        }
        g_dbus_method_invocation_return_dbus_error(call,"org.freedesktop.DBus.Error.NotSupported","This operation has no matching current native contract.");
    }
    void Update(int px,int py,int w,int h,bool focused){Owner();x=px;y=py;width=w;height=h;active=focused;if(!status.registered)return;status.error.clear();auto next=exchange.Published();
        if(next && next->valid)for(const auto& n:next->nodes)if(!Utf8(n.name)||!Utf8(n.value)||!Utf8(n.description)||!Utf8(n.shortcut)||std::abs(n.minimum.x)>10000000 || std::abs(n.minimum.y)>10000000 || std::abs(n.maximum.x)>10000000 || std::abs(n.maximum.y)>10000000){status.error="Semantic packet cannot be represented by bounded D-Bus text/coordinates.";next.reset();break;}
        if(w<=0 || h<=0){next.reset();CancelPending();}
        if(next && !next->valid)status.error=next->overflow?"Semantic frame overflow; native content retired.":"Semantic frame is incoherent; native content retired.";
        auto old=packet;const bool scope_changed=old && next && old->scope!=next->scope;packet=next && next->valid?next:nullptr;
        std::vector<std::string> remove;
        for(const auto& [path,o]:objects)if(!o.kind){const auto* now=Node(o);const auto* before=old?old->Find(o.id):nullptr;if(scope_changed || !now || !before || now->role!=before->role)remove.push_back(path);}
        const auto depth=[&](const std::string& path){int d{};const auto* n=old?old->Find(objects.at(path).id):nullptr;while(n && n->parent){++d;n=old->Find(n->parent);}return d;};
        const auto old_index=[&](ax::StableId id){const auto* n=old?old->Find(id):nullptr;std::vector<ax::StableId> children;if(n && n->parent){if(const auto* parent=old->Find(n->parent))children=parent->children;}else if(old)for(const auto& item:old->nodes)if(!item.parent)children.push_back(item.id);const auto it=std::find(children.begin(),children.end(),id);return it==children.end()?-1:int(it-children.begin());};
        std::stable_sort(remove.begin(),remove.end(),[&](const auto& a,const auto& b){const auto da=depth(a),db=depth(b);if(da!=db)return da>db;const auto* na=old->Find(objects.at(a).id);const auto* nb=old->Find(objects.at(b).id);if(na->parent!=nb->parent)return na->parent<nb->parent;return old_index(na->id)>old_index(nb->id);});
        for(const auto& path:remove){const auto o=objects.at(path);const auto* before=old?old->Find(o.id):nullptr;const auto parent=before && before->parent?Path(before->parent):Window;Event(path,"StateChanged","defunct",1);Event(parent,"ChildrenChanged","remove",old_index(o.id),0,Reference(path));g_dbus_connection_emit_signal(bus,nullptr,Cache,"org.a11y.atspi.Cache","RemoveAccessible",g_variant_new("(@(so))",Reference(path)),nullptr);for(auto key:o.registrations)g_dbus_connection_unregister_object(bus,key);objects.erase(path);}
        std::vector<std::string> added;
        if(packet)for(const auto& n:packet->nodes)if(Path(n.id)==Null){if(serial==UINT64_MAX){Fail("Accessible incarnation capacity exhausted.");return;}const auto path=std::string("/org/a11y/atspi/accessible/n")+std::to_string(++serial);if(!Add(path,n.id,0))return;added.push_back(path);}
        bool refresh_cache=!remove.empty() || !added.empty();
        if(packet)for(const auto& n:packet->nodes){auto& obj=objects.at(Path(n.id));if(Interfaces(obj)!=obj.interfaces){if(!Register(Path(n.id),obj))return;refresh_cache=true;}const auto* before=old?old->Find(n.id):nullptr;if(before && (before->parent!=n.parent || before->children!=n.children))refresh_cache=true;}
        if(refresh_cache){
            const auto cache_add=[&](const std::string& path){g_dbus_connection_emit_signal(bus,nullptr,Cache,"org.a11y.atspi.Cache","AddAccessible",g_variant_new("(@((so)(so)(so)iiassusau))",CacheItem(path,objects.at(path))),nullptr);};
            cache_add(Root);cache_add(Window);
            std::vector<std::pair<int,std::string>> order;if(packet)for(const auto& n:packet->nodes){int depth{};for(auto parent=n.parent;parent;parent=packet->Find(parent)->parent)++depth;order.push_back({depth,Path(n.id)});}
            std::stable_sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.first<b.first;});for(const auto& entry:order)cache_add(entry.second);
        }
        for(const auto& path:added){const auto& obj=objects.at(path);Event(Parent(obj),"ChildrenChanged","add",Index(path,obj),0,Reference(path));}
        if(packet)for(const auto& n:packet->nodes){const auto path=Path(n.id);auto& obj=objects.at(path);const auto* before=std::find(added.begin(),added.end(),path)==added.end() && old?old->Find(n.id):nullptr;
            if(!before || NativeName(*before)!=NativeName(n))Event(path,"PropertyChange","accessible-name",0,0,g_variant_new_string(NativeName(n).c_str()));
            if(before && before->description!=n.description)Event(path,"PropertyChange","accessible-description",0,0,g_variant_new_string(n.description.c_str()));
            if(before && before->value!=n.value){if(!before->value.empty())Event(path,"TextChanged","delete",0,Length(before->value),g_variant_new_string(""));if(!n.value.empty())Event(path,"TextChanged","insert",0,Length(n.value),g_variant_new_string(n.value.c_str()));}
            if((n.role==ax::SemanticRole::Progress || n.role==ax::SemanticRole::Meter || n.role==ax::SemanticRole::Slider) && (!before || before->numeric!=n.numeric || before->minimum_value!=n.minimum_value || before->maximum_value!=n.maximum_value))Event(path,"PropertyChange","accessible-value",0,0,g_variant_new_double(n.numeric));
            if((n.role==ax::SemanticRole::Status || n.role==ax::SemanticRole::Alert) && (!before || before->value!=n.value) && !n.value.empty())Event(path,"Announcement","",n.role==ax::SemanticRole::Alert?2:1,0,g_variant_new_string(n.value.c_str()));
            if(before && (before->minimum.x!=n.minimum.x || before->minimum.y!=n.minimum.y || before->maximum.x!=n.maximum.x || before->maximum.y!=n.maximum.y)){int bx,by,bw,bh;Bounds(obj,0,bx,by,bw,bh);Event(path,"BoundsChanged","",0,0,g_variant_new("(iiii)",bx,by,bw,bh));}
            if(before && before->children!=n.children)Event(path,"ModelChanged","");
            if(before && before->parent!=n.parent)Event(path,"PropertyChange","accessible-parent",0,0,Reference(Parent(obj)));

            const auto state=[&](const char* name,bool now,bool was){if(now!=was)Event(path,"StateChanged",name,now);};state("focused",n.state.focused && active,before && before->state.focused && last_active);state("selected",n.state.selected,before && before->state.selected);state("checked",n.state.checked,before && before->state.checked);state("enabled",!n.state.disabled,!before || !before->state.disabled);state("sensitive",!n.state.disabled,!before || !before->state.disabled);state("showing",n.showing,before && before->showing);state("read-only",n.state.readOnly,before && before->state.readOnly);state("required",n.state.required,before && before->state.required);state("editable",!n.state.readOnly && ax::Supports(n.actions,ax::SemanticAction::SetValue),before && !before->state.readOnly && ax::Supports(before->actions,ax::SemanticAction::SetValue));state("busy",n.state.busy,before && before->state.busy);state("invalid-entry",n.state.invalid,before && before->state.invalid);state("expanded",n.state.expanded,before && before->state.expanded);
            if(n.text_position.caret>=0 && (!before || before->text_position.caret!=n.text_position.caret))Event(path,"TextCaretMoved","",n.text_position.caret);
            if(before && (before->text_position.start!=n.text_position.start || before->text_position.end!=n.text_position.end))Event(path,"TextSelectionChanged","");
            if(before && before->state.selected!=n.state.selected)Event(Parent(obj),"SelectionChanged","");
        }
        if(status.registered && (!have_active || active!=last_active)){Event(Window,active?"Activate":"Deactivate","",0,0,g_variant_new_string("Bitcoin Roots"),"org.a11y.atspi.Event.Window");Event(Window,"StateChanged","active",active);last_active=active;have_active=true;}Resolve();
    }
    void Pump(){Owner();const auto deadline=Clock::now()+std::chrono::milliseconds(2);for(unsigned i=0;i<16 && Clock::now()<deadline;++i)if(!g_main_context_iteration(context,false))break;if(disconnected.exchange(false) && !closing)Fail("Accessibility bus disconnected; native workspace remains available.");Resolve();}
    bool Close(){Owner();if(closing)return jobs==0 && registrations_live==0;closing=true;status.closed=true;g_cancellable_cancel(cancel);CancelPending();Unregister();packet.reset();
        if(bus){if(closed_handler){g_signal_handler_disconnect(bus,closed_handler);closed_handler=0;}g_dbus_connection_close_sync(bus,nullptr,nullptr);}
        const auto deadline=Clock::now()+std::chrono::seconds(1);while((jobs || registrations_live) && Clock::now()<deadline){while(g_main_context_iteration(context,false)){}if(jobs || registrations_live)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        status.connected=status.registered=false;status.settled=jobs==0 && registrations_live==0;return status.settled;
    }
    ~Impl(){if(bus)g_object_unref(bus);if(session)g_object_unref(session);if(interfaces)g_dbus_node_info_unref(interfaces);g_object_unref(cancel);g_main_context_unref(context);}
};
LinuxAccessibility::LinuxAccessibility(c::SemanticExchange& exchange):m_impl(std::make_shared<Impl>(exchange)){m_impl->Start();}
LinuxAccessibility::~LinuxAccessibility(){m_impl->Close();}
void LinuxAccessibility::Pump(){m_impl->Pump();}
void LinuxAccessibility::Update(int x,int y,int w,int h,bool active){m_impl->Update(x,y,w,h,active);}
bool LinuxAccessibility::Close(){return m_impl->Close();}
AccessibilityStatus LinuxAccessibility::Status() const {m_impl->Owner();auto s=m_impl->status;s.objects=m_impl->objects.size();s.pending=m_impl->pending.size();s.callbacks=m_impl->jobs+(m_impl->closing?m_impl->registrations_live:0);return s;}
}
#else
namespace roots::gui::platform {
struct LinuxAccessibility::Impl {};
LinuxAccessibility::LinuxAccessibility(ui::components::SemanticExchange&):m_impl(std::make_shared<Impl>()){}
LinuxAccessibility::~LinuxAccessibility()=default;
void LinuxAccessibility::Pump(){}void LinuxAccessibility::Update(int,int,int,int,bool){}
bool LinuxAccessibility::Close(){return true;}AccessibilityStatus LinuxAccessibility::Status() const {return {false,false,true,true,0,0,0,"","Linux AT-SPI is unavailable on this platform."};}
}
#endif
