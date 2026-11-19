// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "services/camera.h"

namespace roots::gui::platform {
// No path supplied by decoded content or the UI reaches an OS open call.
std::shared_ptr<CameraDriver> NativeCamera();

#if defined(__linux__)
// This port lets the current Linux capture and cleanup code run against a
// controlled device in native tests without loading a driver or granting access.
struct VideoIo {
    virtual ~VideoIo()=default;
    virtual std::optional<CameraDevice> Device(unsigned slot,int fd=-1)=0;
    virtual int Open(unsigned slot)=0;
    virtual int Control(int,unsigned long,void*)=0;
    virtual int Wait(int,int milliseconds,short& events)=0;
    virtual void* Map(int,size_t bytes,uint32_t offset)=0;
    virtual void Unmap(void*,size_t)=0;
    virtual void Close(int)=0;
};
std::shared_ptr<CameraDriver> LinuxCamera(std::shared_ptr<VideoIo>);
#endif
}
