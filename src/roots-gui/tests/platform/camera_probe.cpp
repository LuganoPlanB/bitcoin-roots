// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "tests/platform/camera_fixture.h"

#include <iostream>

using namespace roots::gui;
using namespace std::chrono_literals;
using test::CameraFault;
namespace {
void Check(bool condition,std::string_view reason)
{
    if (!condition) throw std::runtime_error(std::string(reason));
}
CameraView Wait(CameraService& camera,const auto& accept)
{
    const auto end=std::chrono::steady_clock::now()+3s;
    while (std::chrono::steady_clock::now()<end) {
        auto view=camera.View();
        if (accept(view)) return view;
        std::this_thread::sleep_for(2ms);
    }
    throw std::runtime_error("camera view timed out");
}
CameraView Stage(CameraService& camera,CameraStage stage)
{
    return Wait(camera,[=](const auto& view){return view.stage==stage && !view.retiring;});
}
void Closed(test::OwnedVideo& io)
{
    const auto counts=io.Observe();
    Check(!counts.opened && !counts.streaming && counts.opens==counts.closes && counts.maps==counts.unmaps
        && counts.starts==counts.stops,"owned close, STREAMOFF and unmap");
}
void Case(CameraFault fault,std::string_view expected,bool streams=false,bool before_map=false)
{
    auto io=std::make_shared<test::OwnedVideo>();
    CameraService camera(platform::LinuxCamera(io));
    io->Fault(fault);
    Check(camera.Find(),"explicit passive enumeration");
    auto choices=Stage(camera,CameraStage::Devices);
    Check(io->Observe().opens==0,"enumeration must not open");
    if (fault==CameraFault::Absent) {
        Check(choices.devices.empty() && choices.status.find(expected)!=choices.status.npos,"absent listing");
    } else {
        Check(choices.devices.size()==1 && camera.Open(choices.generation,choices.devices.front()),"explicit open");
        if (streams || before_map) {
            auto modes=Stage(camera,CameraStage::ChooseMode);
            Check(modes.modes.size()==2 && io->Observe().starts==0,"resolution enumeration does not start capture");
            Check(camera.Start(modes.generation,modes.modes.front()),"explicit start");
        }
        const auto failed=Stage(camera,CameraStage::Error);
        Check(failed.status.find(expected)!=failed.status.npos && !failed.frame,"typed error with no retained preview");
        Check(io->Observe().starts==unsigned(streams),"stream start admission");
        if (before_map) Check(io->Observe().maps==0,"unchecked range never mapped");
    }
    Closed(*io);
    std::cout<<expected<<": pass\n";
}
}

int main()
{
    try {
        const auto physical=platform::NativeCamera()->Find({});
        std::cout<<"Host V4L2 candidates (passive stat only): "<<physical.size()<<'\n';
        for (const auto& device:physical) std::cout<<device.name<<'\n';
        Case(CameraFault::Absent,"No supported Linux camera");
        Case(CameraFault::Permission,"access was denied");
        Case(CameraFault::Busy,"camera is busy");
        Case(CameraFault::Changed,"no longer matches");
        Case(CameraFault::Unsupported,"No supported uncompressed");
        Case(CameraFault::BadStride,"invalid stride",false,true);
        Case(CameraFault::LargeMap,"oversized mapped buffers",false,true);
        Case(CameraFault::Unplug,"was disconnected",true);
        Case(CameraFault::Io,"I/O failed",true);
        Case(CameraFault::Eof,"was disconnected",true);
        Case(CameraFault::BadFrame,"invalid frame geometry",true);

        auto io=std::make_shared<test::OwnedVideo>();
        CameraService camera(platform::LinuxCamera(io));
        io->Payload("bcrt1qrk5wn7dxwy8l6ru39h6xvmdhxnvdh8p0kpjlxw");
        Check(camera.Find(),"discover reusable device");
        auto view=Stage(camera,CameraStage::Devices);
        const auto device=view.devices.front();
        Check(camera.Open(view.generation,device),"open for cancellation before start");
        view=Stage(camera,CameraStage::ChooseMode);
        const auto closed_generation=view.generation;
        camera.Stop();
        const auto settled=[&] {
            const auto end=std::chrono::steady_clock::now()+700ms;
            while (io->Observe().opened && std::chrono::steady_clock::now()<end) {
                camera.View();
                std::this_thread::sleep_for(2ms);
            }
            Closed(*io);
        };
        settled();
        Check(!camera.Start(closed_generation,view.modes.front()),"retired resolution cannot start");
        view=Stage(camera,CameraStage::Idle);
        Check(camera.Open(view.generation,device),"deliberate reopen after cancel");
        view=Stage(camera,CameraStage::ChooseMode);
        Check(!camera.Start(view.generation,{UINT32_MAX,120,CameraPixels::Gray}),"injected unsupported resolution");
        Check(camera.Start(view.generation,view.modes.front()),"real controlled MMAP stream");
        view=Wait(camera,[](const auto& v){return v.frame && v.frame->qr.has_value();});
        Check(view.frame->qr=="bcrt1qrk5wn7dxwy8l6ru39h6xvmdhxnvdh8p0kpjlxw"
            && view.frame->luma.size()==320*240,"decoded exact luma from padded YUYV frames");
        io->Fault(CameraFault::NoFrames);
        camera.Stop();
        Check(!camera.View().frame,"retire current frame immediately");
        settled();
        const auto final=camera.View();
        Check(final.generation>view.generation && !camera.Open(view.generation,device),"late actions cannot restart the old stream");
        std::cout<<"cancel-open, cancel-stream, explicit restart and stale generation: pass\n";
        for (const auto& event:io->Trace()) std::cout<<event<<'\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
