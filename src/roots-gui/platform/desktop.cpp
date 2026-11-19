// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/desktop.h"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <utility>
#if !defined(_WIN32)
#include <unistd.h>
#endif
#if defined(__linux__)
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#endif

namespace roots::gui::platform {
SleepLease::~SleepLease()
{
#if !defined(_WIN32)
    if(fd>=0)::close(fd);
#endif
}
SleepLease::SleepLease(SleepLease&& other) noexcept:fd(std::exchange(other.fd,-1)){}
SleepLease& SleepLease::operator=(SleepLease&& other) noexcept
{
    SleepLease previous(std::exchange(fd,std::exchange(other.fd,-1)));
    return *this;
}
DesktopRuntime::DesktopRuntime(DesktopPorts value):ports(std::move(value)){}
DesktopRuntime::~DesktopRuntime()
{
    Suspend();
    if(power_worker.joinable())power_worker.join();
    if(notification_worker.joinable())notification_worker.join();
}
app::DesktopReceipt DesktopRuntime::Available(app::DesktopCapability capability) const
{
    switch(capability){
    case app::DesktopCapability::Sleep:return {bool(ports.inhibit),ports.inhibit?"Requires permission from the system sleep-inhibition service.":"Requires the Linux logind sleep-inhibition adapter."};
    case app::DesktopCapability::Notifications:return {bool(ports.notify),ports.notify?"Requires an active desktop notification service.":"Requires the freedesktop desktop notification adapter."};
    case app::DesktopCapability::Browser:return {bool(ports.browser),ports.browser?"":"The native browser opener is unavailable."};
    default:return {false,"The desktop operation is unavailable."};
    }
}
app::DesktopReceipt DesktopRuntime::Notify(DesktopNotification kind)
{
    if(!ports.notify || notification_worker.joinable())return {false,"Desktop notification unavailable or still pending."};
    std::promise<bool> promise;
    notification_reply=promise.get_future();
    try{
        notification_worker=std::jthread([notify=ports.notify,kind,p=std::move(promise)](std::stop_token stop) mutable {
            try{p.set_value(notify(kind,stop));}catch(...){p.set_value(false);}
        });
    }catch(...){notification_reply={};return {false,"The notification request could not start."};}
    worker_feedback_scope=kind==DesktopNotification::CameraReady?feedback_scope:0;
    notice.clear();
    return {true,"Requesting a desktop notification…"};
}
void DesktopRuntime::CameraFeedbackScope(uint64_t next)
{
    feedback_scope=next;
    if(worker_feedback_scope && worker_feedback_scope!=next)CancelNotifications();
}
void DesktopRuntime::CameraReady(uint64_t camera_generation)
{
    if(!camera_generation || camera_generation!=feedback_scope || camera_notification==camera_generation)return;
    camera_notification=camera_generation;
    const auto result=Notify(DesktopNotification::CameraReady);
    if(!result.accepted)notice=result.detail;
}
void DesktopRuntime::CancelNotifications()
{
    if(notification_worker.joinable())notification_worker.request_stop();
}
app::DesktopReceipt DesktopRuntime::Open(app::HelpTarget target,std::string_view confirmed)
{
    if(!app::ConfirmedHelpUrl(target,confirmed))return {false,"The confirmed Help link changed."};
    const bool opened=ports.browser && ports.browser(target,confirmed);
    return {opened,opened?"Open requested from the system browser.":"The system browser could not open this link."};
}
void DesktopRuntime::CameraGeneration(uint64_t next)
{
    if(generation!=next){
        generation=next;
        lease=SleepLease{};
        if(power_worker.joinable())power_worker.request_stop();
        if(!next)notice.clear();
    }
    Poll();
    if(!generation || generation==requested || !ports.inhibit || power_worker.joinable())return;
    requested=generation;
    std::promise<SleepLease> promise;
    power_reply=promise.get_future();
    try{
        power_worker=std::jthread([acquire=ports.inhibit,p=std::move(promise)](std::stop_token stop) mutable {
            try{p.set_value(acquire(stop));}catch(...){p.set_value(SleepLease());}
        });
    }catch(...){power_reply={};notice="Sleep inhibition could not start. Camera capture continues.";}
}
void DesktopRuntime::Suspend()
{
    CameraFeedbackScope(0);
    CameraGeneration(0);
    requested=0;
    CancelNotifications();
}
void DesktopRuntime::Poll()
{
    using namespace std::chrono_literals;
    if(power_reply.valid() && power_reply.wait_for(0ms)==std::future_status::ready){
        auto granted=power_reply.get();
        power_worker.join();
        if(generation && generation==requested){
            lease=std::move(granted);
            notice=lease?"Sleep inhibition is held for this camera capture.":"Sleep inhibition was denied or unavailable. Camera capture continues.";
        }
    }
    if(notification_reply.valid() && notification_reply.wait_for(0ms)==std::future_status::ready){
        const bool accepted=notification_reply.get();
        const bool cancelled=notification_worker.get_stop_token().stop_requested();
        notification_worker.join();
        worker_feedback_scope=0;
        if(!cancelled)notice=accepted?"The desktop accepted the generic notification.":"Desktop notification unavailable or rejected.";
    }
}

#if defined(__linux__)
namespace {
struct Cancellable {
    std::unique_ptr<GCancellable, decltype(&g_object_unref)> owner{g_cancellable_new(),g_object_unref};
    GCancellable* value{owner.get()};
    std::stop_callback<std::function<void()>> cancel;
    explicit Cancellable(std::stop_token stop):cancel(stop,[this]{g_cancellable_cancel(value);}){}
};
GDBusConnection* LocalBus(const char* address,GCancellable* cancel)
{
    if(!address)return nullptr;
    const std::string_view value(address);
    if(!value.starts_with("unix:") || value.size()>4096 || value.find(';')!=std::string_view::npos)return nullptr;
    return g_dbus_connection_new_for_address_sync(address,
        GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT|G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr,cancel,nullptr);
}
SleepLease Logind(std::stop_token stop)
{
    Cancellable cancel(stop);
    auto* bus=LocalBus("unix:path=/run/dbus/system_bus_socket",cancel.value);
    if(!bus)return SleepLease();
    GUnixFDList* received{};
    auto* reply=g_dbus_connection_call_with_unix_fd_list_sync(bus,"org.freedesktop.login1","/org/freedesktop/login1",
        "org.freedesktop.login1.Manager","Inhibit",g_variant_new("(ssss)","sleep","Bitcoin Roots","Active camera QR capture","block"),
        G_VARIANT_TYPE("(h)"),G_DBUS_CALL_FLAGS_NO_AUTO_START,1000,nullptr,&received,cancel.value,nullptr);
    int fd=-1;
    if(reply){
        int index=-1;g_variant_get(reply,"(h)",&index);
        if(received && index>=0 && index<g_unix_fd_list_get_length(received) && !stop.stop_requested())
            fd=g_unix_fd_list_get(received,index,nullptr);
        g_variant_unref(reply);
    }
    if(received)g_object_unref(received);
    g_object_unref(bus);
    return SleepLease(fd);
}
bool GenericNotification(DesktopNotification kind,std::stop_token stop)
{
    Cancellable cancel(stop);
    auto* bus=LocalBus(std::getenv("DBUS_SESSION_BUS_ADDRESS"),cancel.value);
    if(!bus)return false;
    GVariantBuilder actions,hints;
    g_variant_builder_init(&actions,G_VARIANT_TYPE("as"));
    g_variant_builder_init(&hints,G_VARIANT_TYPE("a{sv}"));
    auto* reply=g_dbus_connection_call_sync(bus,"org.freedesktop.Notifications","/org/freedesktop/Notifications",
        "org.freedesktop.Notifications","Notify",g_variant_new("(susssasa{sv}i)","Bitcoin Roots",guint32(0),"",
        "Bitcoin Roots",kind==DesktopNotification::CameraReady?"Camera QR input is ready for review.":"Desktop notifications are enabled.",
        &actions,&hints,gint32(3500)),
        G_VARIANT_TYPE("(u)"),G_DBUS_CALL_FLAGS_NO_AUTO_START,1000,cancel.value,nullptr);
    guint32 id{};
    if(reply){g_variant_get(reply,"(u)",&id);g_variant_unref(reply);}
    const bool accepted=id!=0 && !stop.stop_requested();
    g_object_unref(bus);
    return accepted;
}
}
#endif
DesktopPorts NativeDesktopPorts()
{
    DesktopPorts ports;
#if defined(__linux__)
    ports.inhibit=Logind;
    ports.notify=GenericNotification;
    ports.browser=[](app::HelpTarget target,std::string_view url){
        return app::ConfirmedHelpUrl(target,url) && g_app_info_launch_default_for_uri(std::string(url).c_str(),nullptr,nullptr);
    };
#endif
    return ports;
}
}
