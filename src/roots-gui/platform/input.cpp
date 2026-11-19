// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/input.h"
#include <algorithm>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <cmath>
#include <vector>
#include <utility>
#include <cstdlib>
#if defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <gio/gio.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <poll.h>
#include <xcb/xcb.h>
#undef Status
#endif
namespace roots::gui::platform {
namespace c=ui::components;
#if defined(__linux__)
namespace {
using Clock=std::chrono::steady_clock;
constexpr size_t TextLimit=65536;
bool Valid(std::string_view text){return text.size()<=TextLimit && text.find('\0')==text.npos && g_utf8_validate(text.data(),text.size(),nullptr);}
struct ReadResult {bool ok{};std::string text,detail;};
ReadResult ReadSelection(GCancellable* cancel) {
    // This worker owns its entire Xlib connection. No GLFW, ImGui, editor or
    // document state crosses the thread boundary. One read is admitted at once.
    Display* d=XOpenDisplay(nullptr);if(!d)return {false,{},"Clipboard display is unavailable."};
    const auto window=XCreateSimpleWindow(d,DefaultRootWindow(d),0,0,1,1,0,0,0);
    struct Cleanup {Display* d;Window window;~Cleanup(){XDestroyWindow(d,window);XCloseDisplay(d);}} cleanup{d,window};
    const Atom clip=XInternAtom(d,"CLIPBOARD",False),utf8=XInternAtom(d,"UTF8_STRING",False),property=XInternAtom(d,"ROOTS_GUI_CLIPBOARD",False),incr=XInternAtom(d,"INCR",False);
    const auto owner=XGetSelectionOwner(d,clip);if(!owner)return {false,{},"Clipboard has no text owner."};
    XSelectInput(d,window,PropertyChangeMask);bool latin{},incremental{};std::string text;
    const auto request=[&]{XConvertSelection(d,clip,latin?XA_STRING:utf8,property,window,CurrentTime);XFlush(d);};request();
    const auto deadline=Clock::now()+std::chrono::seconds(2);
    while(Clock::now()<deadline && !g_cancellable_is_cancelled(cancel)) {
        if(!XPending(d)){pollfd fd{ConnectionNumber(d),POLLIN,0};poll(&fd,1,20);continue;}
        XEvent event;XNextEvent(d,&event);
        if(event.type==SelectionNotify && event.xselection.property==None){if(!latin){latin=true;request();continue;}return {false,{},"Clipboard has no supported text format."};}
        if(event.type!=SelectionNotify && !(incremental && event.type==PropertyNotify && event.xproperty.atom==property && event.xproperty.state==PropertyNewValue))continue;
        Atom type{};int format{};unsigned long count{},after{};unsigned char* data{};
        const int result=XGetWindowProperty(d,window,property,0,TextLimit/4+1,True,AnyPropertyType,&type,&format,&count,&after,&data);
        if(result!=Success){if(data)XFree(data);return {false,{},"Clipboard transfer failed."};}
        if(type==incr){incremental=true;if(data)XFree(data);XDeleteProperty(d,window,property);XFlush(d);continue;}
        if(type!=(latin?XA_STRING:utf8) || format!=8 || after || count>TextLimit-text.size()){if(data)XFree(data);return {false,{},"Clipboard text exceeds the supported limit."};}
        if(count)text.append(reinterpret_cast<const char*>(data),count);if(data)XFree(data);
        if(incremental && count)continue;
        if(XGetSelectionOwner(d,clip)!=owner)return {false,{},"Clipboard owner changed; paste cancelled."};
        if(latin){GError* error{};gsize converted_bytes{};auto* converted=g_convert(text.data(),text.size(),"UTF-8","ISO-8859-1",nullptr,&converted_bytes,&error);if(!converted){if(error)g_error_free(error);return {false,{},"Clipboard text conversion failed."};}text.assign(converted,converted_bytes);g_free(converted);}
        if(!Valid(text))return {false,{},"Clipboard text is not valid bounded UTF-8."};return {true,std::move(text),{}};
    }
    return {false,{},g_cancellable_is_cancelled(cancel)?"Paste cancelled.":"Clipboard text request timed out."};
}
}
struct NativeInput::Impl:std::enable_shared_from_this<Impl> {
    const std::thread::id owner=std::this_thread::get_id();GLFWwindow* window{};xcb_connection_t* display{};xcb_window_t root{},selection{};xcb_atom_t clip{},utf8{},targets{},resource{};
    GMainContext* context=g_main_context_new();GCancellable* read_cancel=g_cancellable_new();GSettings* settings{};gulong setting_signal{};InputStatus status;unsigned jobs{};bool closing{},hidden{},copy_sensitive{};std::string copied,view;
    uint32_t editor{};bool editor_sensitive{},editor_protected{},paste_seen{},file_seen{};uint64_t sequence{},paste_epoch{};
    struct PasteRequest {uint64_t epoch{};uint32_t target{};bool sensitive{},protected_editor{},snapshot{},ready{};std::string scope,before,text;int cursor{},start{},end{};};std::optional<PasteRequest> paste;
    GSubprocess* process{};GCancellable* pipe_cancel{};uint64_t file_id{};bool waited{},out_done{},err_done{},file_cancelled{},file_failed{};std::string output,errors;int exit_status{};c::FileChoice choice;
    explicit Impl(GLFWwindow* value):window(value){}
    void Owner() const {if(owner!=std::this_thread::get_id())throw std::logic_error("native input requires host owner thread");}
    struct Context {GMainContext* p;explicit Context(GMainContext* value):p(value){g_main_context_push_thread_default(p);}~Context(){g_main_context_pop_thread_default(p);}};
    void* Hold(){++jobs;return new std::shared_ptr<Impl>(shared_from_this());}
    static std::shared_ptr<Impl> Take(void* data){std::unique_ptr<std::shared_ptr<Impl>> p(static_cast<std::shared_ptr<Impl>*>(data));auto self=*p;--self->jobs;return self;}
    xcb_atom_t AtomFor(const char* name){auto cookie=xcb_intern_atom(display,false,std::char_traits<char>::length(name),name);auto* reply=xcb_intern_atom_reply(display,cookie,nullptr);const auto atom=reply?reply->atom:0;std::free(reply);return atom;}
    bool Checked(xcb_void_cookie_t cookie){auto* error=xcb_request_check(display,cookie);const bool ok=!error;std::free(error);return ok;}
    xcb_window_t SelectionOwner(){auto* reply=xcb_get_selection_owner_reply(display,xcb_get_selection_owner(display,clip),nullptr);const auto result=reply?reply->owner:0;std::free(reply);return result;}
    void Start(){Owner();int screen{};display=xcb_connect(nullptr,&screen);if(display && !xcb_connection_has_error(display)){
            auto iterator=xcb_setup_roots_iterator(xcb_get_setup(display));while(screen-- && iterator.rem)xcb_screen_next(&iterator);if(iterator.rem){root=iterator.data->root;selection=xcb_generate_id(display);const uint32_t mask=XCB_EVENT_MASK_PROPERTY_CHANGE;
                status.clipboard=Checked(xcb_create_window_checked(display,0,selection,root,0,0,1,1,0,XCB_WINDOW_CLASS_INPUT_ONLY,XCB_COPY_FROM_PARENT,XCB_CW_EVENT_MASK,&mask));
                clip=AtomFor("CLIPBOARD");utf8=AtomFor("UTF8_STRING");targets=AtomFor("TARGETS");resource=AtomFor("RESOURCE_MANAGER");Checked(xcb_change_window_attributes_checked(display,root,XCB_CW_EVENT_MASK,&mask));ReadScale();}}
        auto* executable=g_find_program_in_path("zenity");status.file_dialog=executable!=nullptr;g_free(executable);
        auto* source=g_settings_schema_source_get_default();auto* schema=source?g_settings_schema_source_lookup(source,"org.gnome.desktop.interface",true):nullptr;
        if(schema){if(g_settings_schema_has_key(schema,"enable-animations")){Context push(context);settings=g_settings_new_full(schema,nullptr,nullptr);status.reduced_motion=!g_settings_get_boolean(settings,"enable-animations");setting_signal=g_signal_connect(settings,"changed::enable-animations",G_CALLBACK(SettingsChanged),this);}g_settings_schema_unref(schema);}
    }
    static void SettingsChanged(GSettings* settings,const gchar*,void* data){auto& self=*static_cast<Impl*>(data);self.Owner();if(!self.closing)self.status.reduced_motion=!g_settings_get_boolean(settings,"enable-animations");}
    void ReadScale(){auto* reply=xcb_get_property_reply(display,xcb_get_property(display,false,root,resource,XCB_ATOM_STRING,0,8192),nullptr);if(!reply)return;
        status.system_scale=1.F;
        if(reply->format==8 && !reply->bytes_after){std::string text(static_cast<const char*>(xcb_get_property_value(reply)),xcb_get_property_value_length(reply));auto db=XrmGetStringDatabase(text.c_str());if(db){XrmValue value{};char* kind{};if(XrmGetResource(db,"Xft.dpi","Xft.Dpi",&kind,&value) && value.addr){const auto dpi=g_ascii_strtod(value.addr,nullptr);if(std::isfinite(dpi) && dpi>=48 && dpi<=768)status.system_scale=float(std::clamp(dpi/96.0,.5,8.0));}XrmDestroyDatabase(db);}}std::free(reply);
    }
    void Events(){for(int i=0;display && i<16;++i){auto* event=xcb_poll_for_event(display);if(!event)break;const auto type=event->response_type&0x7f;
        if(type==XCB_PROPERTY_NOTIFY){const auto* property=reinterpret_cast<xcb_property_notify_event_t*>(event);if(property->window==root && property->atom==resource)ReadScale();}
        else if(type==XCB_SELECTION_CLEAR){if(SelectionOwner()!=selection){copied.clear();copy_sensitive=false;}}
        else if(type==XCB_SELECTION_REQUEST){const auto& request=*reinterpret_cast<xcb_selection_request_event_t*>(event);xcb_selection_notify_event_t response{};response.response_type=XCB_SELECTION_NOTIFY;response.time=request.time;response.requestor=request.requestor;response.selection=request.selection;response.target=request.target;const auto property=request.property?request.property:request.target;
            if(request.selection==clip && SelectionOwner()==selection && !(hidden && copy_sensitive)){
                bool ok{};if(request.target==targets){const xcb_atom_t supported[]{targets,utf8};ok=Checked(xcb_change_property_checked(display,XCB_PROP_MODE_REPLACE,request.requestor,property,XCB_ATOM_ATOM,32,2,supported));}
                else if(request.target==utf8)ok=Checked(xcb_change_property_checked(display,XCB_PROP_MODE_REPLACE,request.requestor,property,utf8,8,copied.size(),copied.data()));
                if(ok)response.property=property;}
            // Checked XCB requests keep a disappearing consumer from invoking
            // Xlib's process-global fatal error handler on the GUI thread.
            Checked(xcb_send_event_checked(display,false,request.requestor,0,reinterpret_cast<const char*>(&response)));xcb_flush(display);
        }
        std::free(event);
    }}
    bool Copy(std::string_view text,bool sensitive){Owner();if(closing || !status.clipboard || !display || !Valid(text) || (hidden && sensitive)){status.notice="Copy is unavailable for this value.";return false;}copied=text;copy_sensitive=sensitive;const bool accepted=Checked(xcb_set_selection_owner_checked(display,selection,clip,XCB_CURRENT_TIME)) && SelectionOwner()==selection;if(!accepted){copied.clear();status.notice="Clipboard ownership could not be acquired.";return false;}++status.clipboard_writes;status.notice.clear();return true;}
    void CancelPaste(){if(!paste)return;++paste_epoch;paste.reset();g_cancellable_cancel(read_cancel);++status.cancelled;}
    static void ReadWorker(GTask* task,gpointer,gpointer,GCancellable* cancel){auto* result=new ReadResult(ReadSelection(cancel));g_task_return_pointer(task,result,[](void* p){delete static_cast<ReadResult*>(p);});}
    static void ReadDone(GObject*,GAsyncResult* result,void* data){auto self=Take(data);self->Owner();GError* error{};std::unique_ptr<ReadResult> value(static_cast<ReadResult*>(g_task_propagate_pointer(G_TASK(result),&error)));const auto epoch=*static_cast<uint64_t*>(g_task_get_task_data(G_TASK(result)));
        if(!self->closing && self->paste && self->paste->epoch==epoch){if(value && value->ok){self->paste->text=std::move(value->text);self->paste->ready=true;}else{self->status.notice=value?value->detail:"Clipboard request was cancelled.";self->CancelPaste();}}if(error)g_error_free(error);
    }
    const char* RequestPaste(){Owner();if(closing || !display || !editor || (hidden && editor_sensitive && !editor_protected)){status.notice="Paste is unavailable in this control.";return "";}if(paste){status.notice="A clipboard request is already pending.";return "";}paste=PasteRequest{++paste_epoch,editor,editor_sensitive,editor_protected,false,false,view};paste_seen=true;
        if(SelectionOwner()==selection){paste->text=copied;paste->ready=true;return "";}
        if(jobs){status.notice="Native input is busy; retry paste.";CancelPaste();return "";}g_cancellable_reset(read_cancel);Context push(context);auto* task=g_task_new(nullptr,read_cancel,ReadDone,Hold());g_task_set_task_data(task,new uint64_t(paste_epoch),[](void* p){delete static_cast<uint64_t*>(p);});g_task_set_return_on_cancel(task,false);g_task_run_in_thread(task,ReadWorker);g_object_unref(task);++status.clipboard_reads;return "";
    }
    struct Pipe {std::shared_ptr<Impl> self;bool error;};
    void ReadPipe(bool error){Context push(context);++jobs;auto* stream=error?g_subprocess_get_stderr_pipe(process):g_subprocess_get_stdout_pipe(process);g_input_stream_read_bytes_async(stream,4096,G_PRIORITY_DEFAULT,pipe_cancel,PipeDone,new Pipe{shared_from_this(),error});}
    static void PipeDone(GObject* source,GAsyncResult* result,void* data){std::unique_ptr<Pipe> job(static_cast<Pipe*>(data));auto self=job->self;self->Owner();--self->jobs;GError* error{};auto* bytes=g_input_stream_read_bytes_finish(G_INPUT_STREAM(source),result,&error);gsize size{};auto* text=bytes?static_cast<const char*>(g_bytes_get_data(bytes,&size)):nullptr;auto& target=job->error?self->errors:self->output;const size_t limit=job->error?8192:4097;
        if(size>limit-target.size()){self->file_failed=true;self->status.notice="Native file chooser output exceeds its limit.";g_subprocess_force_exit(self->process);size=0;}else if(size)target.append(text,size);
        if(bytes)g_bytes_unref(bytes);if(error){if(!g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED))self->file_failed=true;g_error_free(error);}
        if(size && !self->closing && !self->file_failed)self->ReadPipe(job->error);else (job->error?self->err_done:self->out_done)=true;self->FinishFile();
    }
    static void WaitDone(GObject* source,GAsyncResult* result,void* data){auto self=Take(data);self->Owner();GError* error{};if(!g_subprocess_wait_finish(G_SUBPROCESS(source),result,&error))self->file_failed=true;else if(g_subprocess_get_if_exited(self->process))self->exit_status=g_subprocess_get_exit_status(self->process);else self->file_failed=true;if(error)g_error_free(error);self->waited=true;self->FinishFile();}
    void FinishFile(){if(!process || !waited || !out_done || !err_done)return;status.dialog_process.clear();if(file_cancelled || closing)choice={c::NativeOutcome::Cancelled,{},"File selection cancelled."};else if(!file_failed && exit_status==1)choice={c::NativeOutcome::Cancelled,{},"File selection cancelled."};else if(file_failed || exit_status!=0)choice={c::NativeOutcome::Failed,{},"Native file chooser failed; current path retained."};else{if(!output.empty() && output.back()=='\n')output.pop_back();if(output.empty() || output.size()>4096 || !Valid(output))choice={c::NativeOutcome::Failed,{},"Native file chooser returned an invalid path."};else choice={c::NativeOutcome::Chosen,output,{}};}}
    void CancelFile(){if(process && choice.outcome==c::NativeOutcome::Pending){file_cancelled=true;g_cancellable_cancel(pipe_cancel);g_subprocess_force_exit(process);++status.cancelled;}}
    uint64_t Choose(std::string_view initial,bool save,const char* title="--title=Choose public wallet or transaction file"){Owner();if(closing || !status.file_dialog || jobs || (process && !waited))return 0;if(initial.size()>4096 || !Valid(initial))return 0;if(process){g_object_unref(process);process=nullptr;}if(pipe_cancel)g_object_unref(pipe_cancel);pipe_cancel=g_cancellable_new();output.clear();errors.clear();waited=out_done=err_done=file_cancelled=file_failed=false;exit_status=0;file_id=++sequence;file_seen=true;
        std::vector<std::string> args{"zenity","--file-selection","--modal",title,"--attach="+std::to_string(glfwGetX11Window(window))};if(save){args.push_back("--save");args.push_back("--confirm-overwrite");}if(!initial.empty())args.push_back("--filename="+std::string(initial));std::vector<const char*> argv;for(auto& s:args)argv.push_back(s.c_str());argv.push_back(nullptr);GError* error{};Context push(context);process=g_subprocess_newv(argv.data(),GSubprocessFlags(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_PIPE),&error);
        if(!process){choice={c::NativeOutcome::Failed,{},"Native file chooser could not start."};if(error)g_error_free(error);return file_id;}choice={c::NativeOutcome::Pending,{},{}};status.notice.clear();status.dialog_process=g_subprocess_get_identifier(process);ReadPipe(false);ReadPipe(true);g_subprocess_wait_async(process,nullptr,WaitDone,Hold());return file_id;
    }
    void Pump(){Owner();Events();const auto until=Clock::now()+std::chrono::milliseconds(2);for(int i=0;i<16 && Clock::now()<until;++i)if(!g_main_context_iteration(context,false))break;}
    bool Close(){Owner();if(closing)return jobs==0;closing=true;status.closed=true;CancelPaste();CancelFile();g_cancellable_cancel(read_cancel);if(settings){if(setting_signal)g_signal_handler_disconnect(settings,setting_signal);g_object_unref(settings);settings=nullptr;}const auto until=Clock::now()+std::chrono::milliseconds(2500);while(jobs && Clock::now()<until){Pump();std::this_thread::sleep_for(std::chrono::milliseconds(1));}if(display){if(status.clipboard && SelectionOwner()==selection)Checked(xcb_set_selection_owner_checked(display,XCB_WINDOW_NONE,clip,XCB_CURRENT_TIME));if(selection)Checked(xcb_destroy_window_checked(display,selection));xcb_disconnect(display);display=nullptr;}status.clipboard=status.file_dialog=false;status.settled=jobs==0;return status.settled;}
    ~Impl(){if(process)g_object_unref(process);if(pipe_cancel)g_object_unref(pipe_cancel);g_object_unref(read_cancel);g_main_context_unref(context);}
};
NativeInput::NativeInput(GLFWwindow* window):impl(std::make_shared<Impl>(window)){impl->Start();}
NativeInput::~NativeInput(){impl->Close();}
void NativeInput::Pump(){impl->Pump();}
void NativeInput::BeginFrame(){impl->Owner();impl->paste_seen=false;impl->file_seen=false;}
void NativeInput::EndFrame(){impl->Owner();if(impl->paste && !impl->paste_seen)impl->CancelPaste();if(impl->process && !impl->file_seen)impl->CancelFile();}
void NativeInput::BeginView(std::string_view scope){impl->Owner();if(impl->view!=scope)impl->CancelPaste();impl->view=scope;}
void NativeInput::Privacy(bool hidden){impl->Owner();impl->hidden=hidden;if(hidden && impl->copy_sensitive){if(impl->display && impl->SelectionOwner()==impl->selection)impl->Checked(xcb_set_selection_owner_checked(impl->display,XCB_WINDOW_NONE,impl->clip,XCB_CURRENT_TIME));impl->copied.clear();impl->copy_sensitive=false;}if(hidden && impl->paste && impl->paste->sensitive && !impl->paste->protected_editor)impl->CancelPaste();}
void NativeInput::PrivacyLock(){impl->Owner();std::fill(impl->copied.begin(),impl->copied.end(),0);Privacy(true);impl->CancelPaste();impl->CancelFile();impl->choice={c::NativeOutcome::Cancelled,{},"File choice cancelled by privacy lock."};if(impl->display && impl->SelectionOwner()==impl->selection)impl->Checked(xcb_set_selection_owner_checked(impl->display,XCB_WINDOW_NONE,impl->clip,XCB_CURRENT_TIME));std::fill(impl->copied.begin(),impl->copied.end(),0);impl->copied.clear();impl->status.notice.clear();}
bool NativeInput::Copy(std::string_view text,bool sensitive){return !impl->editor_protected && impl->Copy(text,sensitive || impl->editor_sensitive);}
const char* NativeInput::ClipboardForPaste(){return impl->RequestPaste();}
void NativeInput::EnterEditor(uint32_t id,bool sensitive){impl->Owner();impl->editor=id;impl->editor_sensitive=sensitive;impl->editor_protected=false;}
void NativeInput::EnterProtectedEditor(uint32_t id){EnterEditor(id,true);impl->editor_protected=true;}
void NativeInput::LeaveEditor(){impl->Owner();impl->editor=0;impl->editor_sensitive=false;impl->editor_protected=false;}
std::optional<std::string> NativeInput::Paste(uint32_t id,std::string_view value,int cursor,int start,int end,size_t capacity){impl->Owner();auto& request=impl->paste;if(!request || request->target!=id)return {};impl->paste_seen=true;
    if(!request->snapshot){request->before=value;request->cursor=cursor;request->start=start;request->end=end;request->snapshot=true;}
    if(!request->ready)return {};if(request->scope!=impl->view || request->before!=value || request->cursor!=cursor || request->start!=start || request->end!=end || request->protected_editor!=impl->editor_protected || (request->sensitive && impl->hidden && !request->protected_editor)){impl->status.notice="Paste target changed; clipboard text was not inserted.";impl->CancelPaste();return {};}
    if(request->text.empty()){request.reset();return {};}
    if(start<0 || end<start || size_t(end)>value.size() || value.size()-size_t(end-start)+request->text.size()>=capacity){impl->status.notice="Clipboard text does not fit this field; draft retained.";impl->CancelPaste();return {};}
    auto result=std::move(request->text);request.reset();++impl->status.pastes;impl->status.notice.clear();return result;
}
bool NativeInput::RequestInspection(uint32_t id)
{
    impl->Owner();
    if (!id || !impl->status.clipboard || impl->paste) return false;
    EnterEditor(id, false);
    impl->RequestPaste();
    LeaveEditor();
    return InspectionPending(id);
}
bool NativeInput::InspectionPending(uint32_t id) const
{
    impl->Owner();
    return impl->paste && impl->paste->target == id;
}
bool NativeInput::FileChooserAvailable() const {impl->Owner();return impl->status.file_dialog && !impl->closing && !impl->jobs && (!impl->process || impl->waited);}
bool NativeInput::NativeDialogActive() const {impl->Owner();return impl->process && !impl->closing && impl->choice.outcome==c::NativeOutcome::Pending;}
uint64_t NativeInput::ChooseFile(std::string_view initial,bool save){return impl->Choose(initial,save);}
uint64_t NativeInput::ChooseRpcCookie(){return impl->Choose({},false,"--title=Choose the RPC cookie file");}
c::FileChoice NativeInput::PollFile(uint64_t id){impl->Owner();if(id!=impl->file_id)return {c::NativeOutcome::Stale,{},"File selection is no longer current."};impl->file_seen=true;return impl->choice;}
InputStatus NativeInput::Status() const {impl->Owner();auto status=impl->status;status.callbacks=impl->jobs;return status;}
bool NativeInput::Close(){return impl->Close();}
#else
struct NativeInput::Impl {InputStatus status;};
bool NativeInput::RequestInspection(uint32_t){return false;}
bool NativeInput::InspectionPending(uint32_t) const{return false;}
void NativeInput::PrivacyLock(){Privacy(true);}
void NativeInput::EnterProtectedEditor(uint32_t){}
uint64_t NativeInput::ChooseRpcCookie(){return 0;}
bool NativeInput::NativeDialogActive() const{return false;}
NativeInput::NativeInput(GLFWwindow*):impl(std::make_shared<Impl>()){}
NativeInput::~NativeInput()=default;void NativeInput::Pump(){}void NativeInput::BeginFrame(){}void NativeInput::EndFrame(){}void NativeInput::BeginView(std::string_view){}void NativeInput::Privacy(bool){}bool NativeInput::Copy(std::string_view,bool){return false;}const char* NativeInput::ClipboardForPaste(){return "";}void NativeInput::EnterEditor(uint32_t,bool){}void NativeInput::LeaveEditor(){}std::optional<std::string> NativeInput::Paste(uint32_t,std::string_view,int,int,int,size_t){return {};}bool NativeInput::FileChooserAvailable() const{return false;}uint64_t NativeInput::ChooseFile(std::string_view,bool){return 0;}c::FileChoice NativeInput::PollFile(uint64_t){return {c::NativeOutcome::Unavailable,{},"Native adapter requires platform implementation."};}InputStatus NativeInput::Status() const{return impl->status;}bool NativeInput::Close(){impl->status.closed=true;return true;}
#endif
}
