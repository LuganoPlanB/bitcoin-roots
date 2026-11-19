// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/linux/camera.h"

#if defined(__linux__)
#include "adapters/protocol/qr.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <limits>
#include <span>
#include <sys/ioctl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

namespace roots::gui::platform {
namespace {
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr unsigned VIDEO_SLOTS=64,MAX_FORMATS=16,MAX_SIZES=32;
constexpr size_t MAX_BUFFER=4*1024*1024,MAX_TOTAL_MAPPING=12*1024*1024;
constexpr unsigned MAX_BUFFERS=4;
constexpr unsigned CAPTURE_TYPE=V4L2_BUF_TYPE_VIDEO_CAPTURE;
constexpr unsigned CAPTURE_MEMORY=V4L2_MEMORY_MMAP;

std::string Path(unsigned slot) {return "/dev/video"+std::to_string(slot);}

struct OsVideo final:VideoIo {
    std::optional<CameraDevice> Device(unsigned slot,int fd) override
    {
        if (slot>=VIDEO_SLOTS) return {};
        struct stat s{};
        const auto found=fd>=0 ? fstat(fd,&s) : lstat(Path(slot).c_str(),&s);
        if (found!=0 || !S_ISCHR(s.st_mode) || major(s.st_rdev)!=81) return {};
        return CameraDevice{slot,uint64_t(s.st_dev),uint64_t(s.st_ino),uint64_t(s.st_rdev),
            s.st_ctim.tv_sec,s.st_ctim.tv_nsec,"Linux video"+std::to_string(slot)};
    }
    int Open(unsigned slot) override
    {
        if (slot>=VIDEO_SLOTS) {errno=EINVAL;return -1;}
        return open(Path(slot).c_str(),O_RDWR|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
    }
    int Control(int fd,unsigned long kind,void* data) override {return ioctl(fd,kind,data);}
    int Wait(int fd,int milliseconds,short& events) override
    {
        pollfd item{fd,POLLIN,0};
        const int result=poll(&item,1,milliseconds);
        events=item.revents;
        return result;
    }
    void* Map(int fd,size_t bytes,uint32_t offset) override
    {
        if (uint64_t(offset)>uint64_t(std::numeric_limits<off_t>::max())) {errno=EINVAL;return MAP_FAILED;}
        return mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_SHARED,fd,off_t(offset));
    }
    void Unmap(void* address,size_t bytes) override {munmap(address,bytes);}
    void Close(int fd) override {close(fd);}
};

struct CaptureResources {
    struct Mapping {void* data{}; size_t length{}; uint32_t offset{};};
    VideoIo& io;
    int fd;
    bool streaming{},buffers{};
    unsigned count{};
    std::array<Mapping,MAX_BUFFERS> maps{};
    ~CaptureResources()
    {
        if (streaming) {
            unsigned type=CAPTURE_TYPE;
            io.Control(fd,VIDIOC_STREAMOFF,&type);
        }
        for (unsigned i=0;i<count;++i) io.Unmap(maps[i].data,maps[i].length);
        if (buffers) {
            v4l2_requestbuffers release{};
            release.type=CAPTURE_TYPE;
            release.memory=CAPTURE_MEMORY;
            io.Control(fd,VIDIOC_REQBUFS,&release);
        }
        io.Close(fd);
    }
};
int Control(CaptureResources& device,unsigned long kind,void* value,std::stop_token stop)
{
    for (int attempt=0;attempt<3 && !stop.stop_requested();++attempt) {
        const int result=device.io.Control(device.fd,kind,value);
        if (result>=0 || errno!=EINTR) return result;
    }
    errno=stop.stop_requested()?ECANCELED:EINTR;
    return -1;
}
std::string Failure(int result,std::string_view phase)
{
    switch (result) {
    case EACCES:case EPERM:return "Camera access was denied by Linux. You can still import a PNG or QR text.";
    case EBUSY:return "The camera is busy. Close the application using it, or import a PNG or QR text.";
    case ENOENT:case ENODEV:case ENXIO:return "The camera is missing or was disconnected. Scan again only after selecting a current device.";
    case ECANCELED:return "The camera operation was cancelled.";
    default:return "Camera I/O failed while "+std::string(phase)+". You can still import a PNG or QR text.";
    }
}
bool Supported(unsigned width,unsigned height,CameraPixels pixels)
{
    return width>=160 && width<=CAMERA_MAX_WIDTH && height>=120 && height<=CAMERA_MAX_HEIGHT
        && (pixels!=CameraPixels::Yuyv || width%2==0);
}
uint32_t PixelCode(CameraPixels pixels)
{
    return pixels==CameraPixels::Yuyv ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_GREY;
}
std::string SafeCard(const v4l2_capability& caps,const CameraDevice& selected)
{
    std::string name;
    for (const auto ch:caps.card) {
        if (!ch) break;
        name+=ch>=32 && ch<=126 ? char(ch) : '?';
    }
    return name.empty() ? selected.name : selected.name+" · "+name;
}
std::pair<std::vector<CameraMode>,std::string> Modes(CaptureResources& device,std::stop_token stop)
{
    std::vector<CameraMode> modes;
    for (unsigned i=0;i<MAX_FORMATS && !stop.stop_requested();++i) {
        v4l2_fmtdesc format{};
        format.type=CAPTURE_TYPE;
        format.index=i;
        if (Control(device,VIDIOC_ENUM_FMT,&format,stop)<0) {
            if (errno!=EINVAL) return {{},Failure(errno,"enumerating camera formats")};
            break;
        }
        if (format.type!=CAPTURE_TYPE || format.index!=i || (format.flags&V4L2_FMT_FLAG_COMPRESSED)) continue;
        if (format.pixelformat!=V4L2_PIX_FMT_YUYV && format.pixelformat!=V4L2_PIX_FMT_GREY) continue;
        const auto pixels=format.pixelformat==V4L2_PIX_FMT_GREY ? CameraPixels::Gray : CameraPixels::Yuyv;
        for (unsigned j=0;j<MAX_SIZES && !stop.stop_requested();++j) {
            v4l2_frmsizeenum size{};
            size.index=j;
            size.pixel_format=format.pixelformat;
            if (Control(device,VIDIOC_ENUM_FRAMESIZES,&size,stop)<0) {
                if (errno!=EINVAL) return {{},Failure(errno,"enumerating camera resolutions")};
                break;
            }
            if (size.index!=j || size.pixel_format!=format.pixelformat || size.type!=V4L2_FRMSIZE_TYPE_DISCRETE) break;
            CameraMode mode{size.discrete.width,size.discrete.height,pixels};
            if (Supported(mode.width,mode.height,pixels) && std::ranges::find(modes,mode)==modes.end()) {
                modes.push_back(mode);
                if (modes.size()>=CAMERA_MAX_MODES) return {std::move(modes),{}};
            }
        }
    }
    return {std::move(modes),{}};
}

std::string QueueBuffers(CaptureResources& device,size_t image_bytes,std::stop_token stop)
{
    v4l2_requestbuffers request{};
    request.count=3;
    request.type=CAPTURE_TYPE;
    request.memory=CAPTURE_MEMORY;
    if (Control(device,VIDIOC_REQBUFS,&request,stop)<0) return Failure(errno,"requesting camera buffers");
    device.buffers=true;
    if (request.type!=CAPTURE_TYPE || request.memory!=CAPTURE_MEMORY || request.count<2 || request.count>MAX_BUFFERS)
        return "The camera supplied an unsupported buffer queue.";
    size_t total{};
    const long page=sysconf(_SC_PAGE_SIZE);
    for (unsigned i=0;i<request.count;++i) {
        if (stop.stop_requested()) return {};
        v4l2_buffer buffer{};
        buffer.type=CAPTURE_TYPE; buffer.memory=CAPTURE_MEMORY; buffer.index=i;
        if (Control(device,VIDIOC_QUERYBUF,&buffer,stop)<0) return Failure(errno,"checking camera buffer lengths");
        if (buffer.index!=i || buffer.type!=CAPTURE_TYPE || buffer.memory!=CAPTURE_MEMORY
            || buffer.length<image_bytes || buffer.length>MAX_BUFFER || buffer.length>MAX_TOTAL_MAPPING-total
            || page<=0 || buffer.m.offset%unsigned(page)
            || std::ranges::any_of(std::span(device.maps).first(device.count),[&](const auto& earlier) {
                const uint64_t begin=buffer.m.offset,end=begin+buffer.length;
                return begin<uint64_t(earlier.offset)+earlier.length && uint64_t(earlier.offset)<end;
            })) return "The camera supplied invalid or oversized mapped buffers.";
        void* address=device.io.Map(device.fd,buffer.length,buffer.m.offset);
        if (address==MAP_FAILED) return Failure(errno,"mapping camera buffers");
        device.maps[device.count++]={address,buffer.length,buffer.m.offset};
        if (!address) return "The camera supplied an invalid mapped buffer.";
        total+=buffer.length;
        v4l2_buffer queue{};
        queue.type=CAPTURE_TYPE; queue.memory=CAPTURE_MEMORY; queue.index=i;
        if (Control(device,VIDIOC_QBUF,&queue,stop)<0) return Failure(errno,"queuing camera buffers");
    }
    return {};
}

std::string Stream(CaptureResources& device,const CameraMode& mode,size_t stride,size_t needed,
    const CameraCallbacks& callbacks,std::stop_token stop)
{
    auto last_frame=Clock::now(),last_decode=last_frame-250ms;
    const auto expires=last_frame+120s;
    while (!stop.stop_requested()) {
        const auto now=Clock::now();
        if (now>=expires) return "Camera session expired. Start again to scan a new stream.";
        if (now-last_frame>=5s) return "The camera stopped delivering frames.";
        if (now-last_decode<250ms) {
            std::this_thread::sleep_for(std::min(25ms,std::chrono::duration_cast<std::chrono::milliseconds>(250ms-(now-last_decode))));
            continue;
        }
        short events{};
        const int polled=device.io.Wait(device.fd,50,events);
        if (stop.stop_requested()) break;
        if (polled<0) {
            if (errno==EINTR) continue;
            return Failure(errno,"waiting for a frame");
        }
        if (polled==0) continue;
        if (events&(POLLHUP|POLLNVAL)) return Failure(ENODEV,"waiting for a frame");
        if (events&POLLERR) return Failure(EIO,"waiting for a frame");
        if (!(events&POLLIN)) continue;
        v4l2_buffer buffer{};
        buffer.type=CAPTURE_TYPE; buffer.memory=CAPTURE_MEMORY;
        if (Control(device,VIDIOC_DQBUF,&buffer,stop)<0) {
            if (errno==EAGAIN) continue;
            return Failure(errno,"reading a frame");
        }
        if (buffer.index>=device.count || buffer.type!=CAPTURE_TYPE || buffer.memory!=CAPTURE_MEMORY)
            return "The camera supplied an invalid frame identity.";
        const auto& map=device.maps[buffer.index];
        if (!buffer.bytesused || (buffer.flags&V4L2_BUF_FLAG_LAST)) return Failure(ENODEV,"reading a frame");
        if (buffer.flags&V4L2_BUF_FLAG_ERROR) return "The camera reported a damaged frame.";
        if (buffer.length!=map.length || buffer.bytesused>map.length || buffer.bytesused<needed
            || buffer.m.offset!=map.offset || buffer.field!=V4L2_FIELD_NONE)
            return "The camera supplied invalid frame geometry or byte lengths.";
        last_frame=last_decode=Clock::now();
        CameraFrame frame{mode.width,mode.height,std::vector<uint8_t>(size_t(mode.width)*mode.height),{}};
        const auto* raw=static_cast<const uint8_t*>(map.data);
        const size_t sample=mode.pixels==CameraPixels::Gray ? 1 : 2;
        for (size_t y=0;y<mode.height;++y)
            for (size_t x=0;x<mode.width;++x) frame.luma[y*mode.width+x]=raw[y*stride+x*sample];
        v4l2_buffer queue{};
        queue.type=CAPTURE_TYPE; queue.memory=CAPTURE_MEMORY; queue.index=buffer.index;
        if (Control(device,VIDIOC_QBUF,&queue,stop)<0) return Failure(errno,"releasing a frame to the camera");
        frame.qr=protocol::DecodeQrLuma(frame.luma,int(frame.width),int(frame.height),stop);
        if (!stop.stop_requested()) callbacks.frame(std::move(frame));
    }
    return {};
}

class Driver final:public CameraDriver {
    std::shared_ptr<VideoIo> io;
public:
    explicit Driver(std::shared_ptr<VideoIo> port):io(std::move(port)) {}
    std::vector<CameraDevice> Find(std::stop_token stop) override
    {
        std::vector<CameraDevice> devices;
        for (unsigned slot=0;slot<VIDEO_SLOTS && devices.size()<CAMERA_MAX_DEVICES && !stop.stop_requested();++slot)
            if (auto data=io->Device(slot)) devices.push_back(std::move(*data));
        return devices;
    }
    std::string Capture(const CameraDevice& selected,const CameraCallbacks& callbacks,std::stop_token stop) override
    {
        if (stop.stop_requested()) return {};
        if (io->Device(selected.slot)!=selected) return "The selected camera identity changed. Find cameras again.";
        const int fd=io->Open(selected.slot);
        if (fd<0) return Failure(errno,"opening the selected device");
        CaptureResources device{*io,fd};
        if (io->Device(selected.slot,fd)!=selected) return "The camera no longer matches the selected device.";
        v4l2_capability cap{};
        if (Control(device,VIDIOC_QUERYCAP,&cap,stop)<0) return Failure(errno,"checking camera capabilities");
        const auto flags=(cap.capabilities&V4L2_CAP_DEVICE_CAPS)?cap.device_caps:cap.capabilities;
        if (!(flags&V4L2_CAP_VIDEO_CAPTURE) || !(flags&V4L2_CAP_STREAMING))
            return "This camera does not offer supported single-plane streaming.";
        auto [options,mode_error]=Modes(device,stop);
        if (stop.stop_requested()) return {};
        if (!mode_error.empty()) return mode_error;
        if (options.empty()) return "No supported uncompressed YUYV or grayscale resolution is available.";
        const auto mode=callbacks.choose(options,SafeCard(cap,selected));
        if (stop.stop_requested()) return {};
        if (!mode) return "Camera selection expired. Open a device again to choose a resolution.";
        if (std::ranges::find(options,*mode)==options.end()) return "The selected camera resolution is no longer available.";
        v4l2_format format{};
        format.type=CAPTURE_TYPE;
        auto& pixel=format.fmt.pix;
        pixel.width=mode->width; pixel.height=mode->height; pixel.pixelformat=PixelCode(mode->pixels); pixel.field=V4L2_FIELD_NONE;
        if (Control(device,VIDIOC_S_FMT,&format,stop)<0)
            return errno==EINVAL ? "The selected camera resolution or format is unsupported." : Failure(errno,"setting the selected resolution");
        const size_t row=size_t(mode->width)*(mode->pixels==CameraPixels::Gray ? 1 : 2);
        if (format.type!=CAPTURE_TYPE || pixel.width!=mode->width || pixel.height!=mode->height
            || pixel.pixelformat!=PixelCode(mode->pixels) || pixel.field!=V4L2_FIELD_NONE
            || pixel.bytesperline<row || pixel.bytesperline>8192 || pixel.sizeimage>MAX_BUFFER)
            return "The camera returned a different format, resolution or invalid stride.";
        const size_t needed=size_t(pixel.bytesperline)*(mode->height-1)+row;
        if (needed>pixel.sizeimage) return "The camera returned an invalid uncompressed frame size.";
        auto buffers=QueueBuffers(device,pixel.sizeimage,stop);
        if (!buffers.empty() || stop.stop_requested()) return buffers;
        unsigned type=CAPTURE_TYPE;
        if (Control(device,VIDIOC_STREAMON,&type,stop)<0) return Failure(errno,"starting capture");
        device.streaming=true;
        callbacks.started();
        return Stream(device,*mode,pixel.bytesperline,needed,callbacks,stop);
    }
};
}
std::shared_ptr<CameraDriver> LinuxCamera(std::shared_ptr<VideoIo> io)
{
    return io ? std::make_shared<Driver>(std::move(io)) : nullptr;
}
std::shared_ptr<CameraDriver> NativeCamera() {return LinuxCamera(std::make_shared<OsVideo>());}
}
#else
namespace roots::gui::platform {
std::shared_ptr<CameraDriver> NativeCamera() {return {};}
}
#endif
