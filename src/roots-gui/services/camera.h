// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace roots::gui {
inline constexpr unsigned CAMERA_MAX_WIDTH=1280,CAMERA_MAX_HEIGHT=960;
inline constexpr size_t CAMERA_MAX_DEVICES=16,CAMERA_MAX_MODES=24;

struct CameraDevice {
    unsigned slot{};
    uint64_t filesystem{},inode{},device{};
    int64_t created_seconds{},created_nanos{};
    std::string name;
    bool operator==(const CameraDevice&) const=default;
};
enum class CameraPixels { Yuyv, Gray };
struct CameraMode {
    unsigned width{},height{};
    CameraPixels pixels{};
    bool operator==(const CameraMode&) const=default;
};
struct CameraFrame {
    unsigned width{},height{};
    std::vector<uint8_t> luma;
    std::optional<std::string> qr;
};
struct CameraCallbacks {
    std::function<std::optional<CameraMode>(std::vector<CameraMode>,std::string)> choose;
    std::function<void()> started;
    std::function<void(CameraFrame)> frame;
};
class CameraDriver {
public:
    virtual ~CameraDriver()=default;
    virtual std::vector<CameraDevice> Find(std::stop_token)=0;
    virtual std::string Capture(const CameraDevice&,const CameraCallbacks&,std::stop_token)=0;
};
enum class CameraStage { Idle,Finding,Devices,Opening,ChooseMode,Starting,Streaming,Error };
struct CameraView {
    uint64_t generation{},sample{};
    CameraStage stage{CameraStage::Idle};
    std::vector<CameraDevice> devices;
    std::vector<CameraMode> modes;
    std::shared_ptr<const CameraFrame> frame;
    std::string status;
    bool retiring{};
};

// A single owner admits actions. Worker values belong only to that generation.
class CameraService {
public:
    explicit CameraService(std::shared_ptr<CameraDriver>);
    ~CameraService();
    CameraView View();
    bool Available() const {return bool(driver);}
    bool Find();
    bool Open(uint64_t,const CameraDevice&);
    bool Start(uint64_t,const CameraMode&);
    void Stop();
private:
    bool Owner() const {return owner==std::this_thread::get_id();}
    bool Ready();
    void Finish(uint64_t,std::string);
    const std::thread::id owner{std::this_thread::get_id()};
    std::shared_ptr<CameraDriver> driver;
    std::mutex mutex;
    std::condition_variable_any changed;
    CameraView view;
    std::optional<CameraMode> chosen;
    bool done{};
    std::jthread worker;
};
std::string CameraModeLabel(const CameraMode&);
}
