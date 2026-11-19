// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "adapters/protocol/qr.h"
#include "platform/linux/camera.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <linux/videodev2.h>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <sys/mman.h>

namespace roots::gui::test {
enum class CameraFault { None,Absent,Permission,Busy,Changed,Unsupported,BadStride,LargeMap,Unplug,Io,Eof,BadFrame,NoFrames };

class OwnedVideo final:public platform::VideoIo {
public:
    static constexpr unsigned WIDTH=320,HEIGHT=240,STRIDE=656;
    struct Counts {
        unsigned opens{},closes{},maps{},unmaps{},starts{},stops{},queues{},dequeues{},releases{};
        bool opened{},streaming{};
    };
    void Fault(CameraFault mode)
    {
        std::lock_guard guard(mutex);
        fault=mode;
    }
    void Payload(std::string_view value)
    {
        const auto matrix=protocol::EncodeQr(value);
        if (!matrix) throw std::runtime_error("fixture text exceeds camera target");
        const int side=matrix->size+8;
        const int pitch=int(HEIGHT)/side;
        if (pitch<3) throw std::runtime_error("fixture QR is too dense for the fixed frame");
        const int start_x=(int(WIDTH)-side*pitch)/2+pitch*4;
        const int start_y=(int(HEIGHT)-side*pitch)/2+pitch*4;
        std::vector<uint8_t> pixels(WIDTH*HEIGHT,255);
        for (int y=0;y<matrix->size*pitch;++y) for (int x=0;x<matrix->size*pitch;++x)
            if (matrix->modules[size_t(y/pitch)*matrix->size+x/pitch]) pixels[size_t(start_y+y)*WIDTH+start_x+x]=0;
        std::lock_guard guard(mutex);
        source=std::move(pixels);
    }
    Counts Observe()
    {
        std::lock_guard guard(mutex);
        return counts;
    }
    std::vector<std::string> Trace()
    {
        std::lock_guard guard(mutex);
        return events;
    }
    std::optional<CameraDevice> Device(unsigned slot,int fd=-1) override
    {
        std::lock_guard guard(mutex);
        if (slot!=0 || fault==CameraFault::Absent) return {};
        return CameraDevice{0,11,fd>=0 && fault==CameraFault::Changed ? 24U : 23U,81,1700000000,0,"Fixture video0"};
    }
    int Open(unsigned slot) override
    {
        std::lock_guard guard(mutex);
        if (slot || counts.opened) return Fail(EBUSY);
        if (fault==CameraFault::Permission) {events.push_back("open:EACCES");return Fail(EACCES);}
        if (fault==CameraFault::Busy) {events.push_back("open:EBUSY");return Fail(EBUSY);}
        ++counts.opens;
        counts.opened=true;
        events.push_back("open");
        return 42;
    }
    void Close(int fd) override
    {
        std::lock_guard guard(mutex);
        if (fd!=42 || !counts.opened) std::terminate();
        counts.opened=false;
        ++counts.closes;
        events.push_back("close");
    }
    int Control(int fd,unsigned long type,void* data) override
    {
        std::lock_guard guard(mutex);
        if (fd!=42 || !counts.opened) return Fail(EBADF);
        switch (type) {
        case VIDIOC_QUERYCAP: {
            auto& cap=*static_cast<v4l2_capability*>(data);
            cap.capabilities=V4L2_CAP_DEVICE_CAPS;
            cap.device_caps=V4L2_CAP_VIDEO_CAPTURE|V4L2_CAP_STREAMING;
            const char name[]{"Controlled YUYV stream"};
            std::memcpy(cap.card,name,sizeof(name));
            events.push_back("querycap");
            return 0;
        }
        case VIDIOC_ENUM_FMT: {
            auto& fmt=*static_cast<v4l2_fmtdesc*>(data);
            if (fmt.index) return Fail(EINVAL);
            fmt.pixelformat=fault==CameraFault::Unsupported ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_YUYV;
            return 0;
        }
        case VIDIOC_ENUM_FRAMESIZES: {
            auto& size=*static_cast<v4l2_frmsizeenum*>(data);
            if (size.index>1) return Fail(EINVAL);
            size.type=V4L2_FRMSIZE_TYPE_DISCRETE;
            size.discrete={size.index?640U:WIDTH,HEIGHT};
            return 0;
        }
        case VIDIOC_S_FMT: {
            auto& fmt=static_cast<v4l2_format*>(data)->fmt.pix;
            image_width=fmt.width;
            fmt.bytesperline=fault==CameraFault::BadStride ? 17 : image_width*2+16;
            fmt.sizeimage=fmt.bytesperline*HEIGHT;
            active_stride=fmt.bytesperline;
            events.push_back("s_fmt:"+std::to_string(fmt.width));
            return 0;
        }
        case VIDIOC_REQBUFS: {
            auto& req=*static_cast<v4l2_requestbuffers*>(data);
            if (req.count==0) {++counts.releases;events.push_back("release_buffers");return 0;}
            req.count=2;
            alloc=active_stride*HEIGHT;
            for (auto& map:memory) map.resize(alloc);
            events.push_back("request_2");
            return 0;
        }
        case VIDIOC_QUERYBUF: {
            auto& buf=*static_cast<v4l2_buffer*>(data);
            if (buf.index>1) return Fail(EINVAL);
            buf.m.offset=buf.index*2*1024*1024;
            buf.length=fault==CameraFault::LargeMap ? UINT32_MAX : alloc;
            return 0;
        }
        case VIDIOC_QBUF:
            ++counts.queues;
            return 0;
        case VIDIOC_STREAMON:
            counts.streaming=true;
            ++counts.starts;
            events.push_back("stream_on");
            return 0;
        case VIDIOC_STREAMOFF:
            counts.streaming=false;
            ++counts.stops;
            events.push_back("stream_off");
            return 0;
        case VIDIOC_DQBUF: {
            if (fault==CameraFault::Io) {events.push_back("dqbuf:EIO");return Fail(EIO);}
            auto& buf=*static_cast<v4l2_buffer*>(data);
            if (!counts.streaming) return Fail(EINVAL);
            buf.index=counts.dequeues++%2;
            buf.m.offset=buf.index*2*1024*1024;
            buf.length=alloc;
            buf.field=V4L2_FIELD_NONE;
            buf.bytesused=fault==CameraFault::Eof ? 0U : fault==CameraFault::BadFrame ? alloc+1 : alloc;
            if (buf.bytesused!=alloc) return 0;
            auto& image=memory[buf.index];
            for (unsigned y=0;y<HEIGHT;++y) for (unsigned x=0;x<image_width;++x) {
                const auto at=size_t(y)*active_stride+2*x;
                image[at]=x<WIDTH ? source[y*WIDTH+x] : 255;
                image[at+1]=128;
            }
            return 0;
        }
        default:return Fail(EINVAL);
        }
    }
    int Wait(int fd,int milliseconds,short& events_out) override
    {
        if (fd!=42 || milliseconds!=50) return Fail(EINVAL);
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        std::lock_guard guard(mutex);
        if (fault==CameraFault::NoFrames) {events_out=0;return 0;}
        events_out=fault==CameraFault::Unplug ? POLLHUP : POLLIN;
        return 1;
    }
    void* Map(int fd,size_t bytes,uint32_t offset) override
    {
        std::lock_guard guard(mutex);
        if (fd!=42 || bytes!=alloc || offset%(2*1024*1024) || offset/(2*1024*1024)>1) {
            errno=EINVAL;
            return MAP_FAILED;
        }
        ++counts.maps;
        events.push_back("map");
        return memory[offset/(2*1024*1024)].data();
    }
    void Unmap(void* address,size_t bytes) override
    {
        std::lock_guard guard(mutex);
        if (bytes!=alloc || (address!=memory[0].data() && address!=memory[1].data())) std::terminate();
        ++counts.unmaps;
        events.push_back("unmap");
    }
private:
    static int Fail(int value) {errno=value;return -1;}
    std::mutex mutex;
    CameraFault fault{};
    Counts counts;
    unsigned image_width=WIDTH,active_stride=STRIDE;
    unsigned alloc{};
    std::array<std::vector<uint8_t>,2> memory;
    std::vector<uint8_t> source=std::vector<uint8_t>(WIDTH*HEIGHT,255);
    std::vector<std::string> events;
};
}
