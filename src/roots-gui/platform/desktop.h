// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "app/desktop.h"
#include <cstdint>
#include <functional>
#include <future>
#include <optional>
#include <stop_token>
#include <thread>

namespace roots::gui::platform {
enum class DesktopNotification { Test, CameraReady };
class SleepLease {
    int fd{-1};
public:
    explicit SleepLease(int value=-1):fd(value){}
    ~SleepLease();
    SleepLease(SleepLease&&) noexcept;
    SleepLease& operator=(SleepLease&&) noexcept;
    explicit operator bool() const{return fd>=0;}
};
struct DesktopPorts {
    std::function<SleepLease(std::stop_token)> inhibit;
    std::function<bool(DesktopNotification,std::stop_token)> notify;
    std::function<bool(app::HelpTarget,std::string_view)> browser;
};

// Reconciles one explicit camera generation. A late grant for a retired
// generation is closed locally and cannot carry over to another operation.
class DesktopRuntime {
    DesktopPorts ports;
    std::jthread power_worker,notification_worker;
    std::future<SleepLease> power_reply;
    std::future<bool> notification_reply;
    uint64_t generation{},requested{},camera_notification{};
    uint64_t feedback_scope{},worker_feedback_scope{};
    SleepLease lease;
    std::string notice;
public:
    explicit DesktopRuntime(DesktopPorts);
    ~DesktopRuntime();
    app::DesktopReceipt Available(app::DesktopCapability) const;
    app::DesktopReceipt Notify(DesktopNotification=DesktopNotification::Test);
    void CameraFeedbackScope(uint64_t);
    void CameraReady(uint64_t);
    void CancelNotifications();
    app::DesktopReceipt Open(app::HelpTarget,std::string_view);
    void CameraGeneration(uint64_t);
    void Suspend();
    void Poll();
    bool Busy() const{return power_worker.joinable() || notification_worker.joinable();}
    bool Holding() const{return bool(lease);}
    const std::string& Notice() const{return notice;}
};
DesktopPorts NativeDesktopPorts();
}
