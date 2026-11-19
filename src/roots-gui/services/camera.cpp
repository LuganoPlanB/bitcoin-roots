// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/camera.h"

#include <algorithm>

namespace roots::gui {
using namespace std::chrono_literals;

CameraService::CameraService(std::shared_ptr<CameraDriver> adapter):driver(std::move(adapter)) {}
CameraService::~CameraService()
{
    Stop();
    if (worker.joinable()) worker.join();
}
bool CameraService::Ready()
{
    if (!Owner() || !driver) return false;
    if (worker.joinable()) {
        {
            std::lock_guard lock(mutex);
            if (!done) return false;
        }
        worker.join();
    }
    return true;
}
CameraView CameraService::View()
{
    if (!Owner()) return {};
    const bool ready=Ready();
    std::lock_guard lock(mutex);
    auto observed=view;
    observed.retiring=!ready && view.stage==CameraStage::Idle && worker.joinable();
    return observed;
}
void CameraService::Finish(uint64_t generation,std::string error)
{
    std::lock_guard lock(mutex);
    if (view.generation==generation && !error.empty()) {
        view.stage=CameraStage::Error;
        view.status=std::move(error);
        view.frame.reset();
        view.modes.clear();
    }
    done=true;
}
bool CameraService::Find()
{
    if (!Ready()) return false;
    std::lock_guard lock(mutex);
    if (view.generation==UINT64_MAX) return false;
    const auto generation=view.generation+1;
    view={generation,0,CameraStage::Finding};
    done=false;
    try {
        worker=std::jthread([this,generation](std::stop_token stop) {
            try {
                auto found=driver->Find(stop);
                {
                    std::lock_guard guard(mutex);
                    if (view.generation==generation && !stop.stop_requested()) {
                        if (found.size()>CAMERA_MAX_DEVICES) found.resize(CAMERA_MAX_DEVICES);
                        view.devices=std::move(found);
                        view.stage=CameraStage::Devices;
                        view.status=view.devices.empty() ? "No supported Linux camera device nodes were found." : "Select a device and open it to check camera access and resolutions.";
                    }
                    done=true;
                }
            } catch (...) {Finish(generation,"Camera discovery failed.");}
        });
    } catch (...) {
        done=true;
        view.stage=CameraStage::Error;
        view.status="Could not start camera discovery.";
        return false;
    }
    return true;
}
bool CameraService::Open(uint64_t generation,const CameraDevice& device)
{
    if (!Ready()) return false;
    std::lock_guard lock(mutex);
    if (view.generation!=generation || generation==UINT64_MAX || std::ranges::find(view.devices,device)==view.devices.end()
        || (view.stage!=CameraStage::Devices && view.stage!=CameraStage::Error && view.stage!=CameraStage::Idle)) return false;
    ++generation;
    view.generation=generation;
    view.sample=0;
    view.frame.reset();
    view.modes.clear();
    view.stage=CameraStage::Opening;
    view.status="Opening the selected camera.";
    chosen.reset();
    done=false;
    try {
        worker=std::jthread([this,device,generation](std::stop_token stop) {
            CameraCallbacks callbacks{
                [this,generation,stop](std::vector<CameraMode> modes,std::string name)->std::optional<CameraMode> {
                    std::unique_lock guard(mutex);
                    if (view.generation!=generation || stop.stop_requested() || modes.empty() || modes.size()>CAMERA_MAX_MODES) return {};
                    view.stage=CameraStage::ChooseMode;
                    view.status="Access granted for "+name+". Choose a resolution, then start capture.";
                    view.modes=std::move(modes);
                    if (!changed.wait_for(guard,stop,30s,[&] {return chosen.has_value() || view.generation!=generation;})) return {};
                    return view.generation==generation && !stop.stop_requested() ? chosen : std::nullopt;
                },
                [this,generation,stop] {
                    std::lock_guard guard(mutex);
                    if (view.generation!=generation || stop.stop_requested()) return;
                    view.stage=CameraStage::Streaming;
                    view.status="Camera is streaming.";
                },
                [this,generation,stop](CameraFrame frame) {
                    auto latest=std::make_shared<const CameraFrame>(std::move(frame));
                    std::lock_guard guard(mutex);
                    if (view.generation!=generation || stop.stop_requested() || view.stage!=CameraStage::Streaming) return;
                    view.frame=std::move(latest);
                    ++view.sample;
                }};
            try {
                auto reason=driver->Capture(device,callbacks,stop);
                if (reason.empty() && !stop.stop_requested()) reason="Camera capture ended.";
                Finish(generation,std::move(reason));
            }
            catch (...) {Finish(generation,"Camera capture could not continue within the available resources.");}
        });
    } catch (...) {
        done=true;
        view.stage=CameraStage::Error;
        view.status="Could not start the camera worker.";
        return false;
    }
    return true;
}
bool CameraService::Start(uint64_t generation,const CameraMode& mode)
{
    if (!Owner()) return false;
    std::lock_guard lock(mutex);
    if (view.generation!=generation || view.stage!=CameraStage::ChooseMode
        || std::ranges::find(view.modes,mode)==view.modes.end()) return false;
    chosen=mode;
    view.stage=CameraStage::Starting;
    view.status="Starting capture at "+CameraModeLabel(mode)+".";
    changed.notify_all();
    return true;
}
void CameraService::Stop()
{
    if (!Owner()) return;
    if (worker.joinable()) worker.request_stop();
    {
        std::lock_guard lock(mutex);
        if (view.generation!=UINT64_MAX) ++view.generation;
        view.sample=0;
        view.stage=CameraStage::Idle;
        view.frame.reset();
        view.modes.clear();
        view.status.clear();
        chosen.reset();
    }
    changed.notify_all();
}

std::string CameraModeLabel(const CameraMode& mode)
{
    return std::to_string(mode.width)+" × "+std::to_string(mode.height)
        +(mode.pixels==CameraPixels::Gray ? " · grayscale" : " · YUYV");
}
}
