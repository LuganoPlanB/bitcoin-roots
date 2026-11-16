// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/native_host.h"
#include "ui/theme/appearance.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
using namespace roots::gui;
namespace {
int checks{},failures{};
void Check(bool ok,const char* id) { ++checks;if (!ok) { ++failures;std::fprintf(stderr,"FAIL %s\n",id); } }
bool Desktop(bool dark)
{
    // Test process uses the owned display's disposable XDG_CONFIG_HOME only.
    const char* command=dark?"gsettings set org.gnome.desktop.interface color-scheme prefer-dark":
        "gsettings set org.gnome.desktop.interface color-scheme prefer-light";
    std::printf("TEST_ARGV %s\n",command);return std::system(command)==0;
}
struct Observer final : platform::NativeObserver {
    bool dark{},cleaned{};int mode{};
    void Frame(platform::NativeHost& host,const platform::NativeFrame& f) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (mode==0) {
            if (f.rendered_frames==4) { Check(!dark,"System initial desktop Light");Check(Desktop(true),"isolated desktop Dark request"); }
            if (f.rendered_frames==24) { Check(dark,"System follows real GSettings Dark while running");Check(Desktop(false),"isolated desktop Light request"); }
            if (f.rendered_frames==44) { Check(!dark,"System follows real GSettings Light while running");host.RequestClose(); }
        } else if (f.rendered_frames==8) { Check(dark,"saved choice loaded by independent native host");host.RequestClose(); }
        if (f.rendered_frames>50) host.RequestClose();
    }
    void Teardown(const platform::NativeTeardown& t) override {
        cleaned=t.renderer_cleared && t.platform_cleared && t.textures_released && t.imgui_cleared && t.context_cleared;
        Check(cleaned,"native preference resources settled");
    }
};
}
int main(int argc,char** argv)
{
    if (argc!=3 || !std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY") || !std::getenv("XDG_CONFIG_HOME")) return 2;
    // Explicit isolation before any GIO/GSettings object exists.
    setenv("GSETTINGS_BACKEND","keyfile",1);
    const std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);
    const auto preference=std::filesystem::path(std::getenv("XDG_CONFIG_HOME"))/"roots-gui/appearance";
    Check(Desktop(false),"isolated desktop initial Light");
    Check(ui::theme::SaveAppearance(preference,ui::theme::Appearance::System),"save System ID");
    for (int mode=0;mode<3;++mode) {
        if (mode==1) Check(Desktop(true),"desktop Dark before reload");
        if (mode==2) { Check(ui::theme::SaveAppearance(preference,ui::theme::Appearance::Dark),"save explicit Dark");Check(Desktop(false),"desktop Light against explicit Dark"); }
        Observer observer;observer.mode=mode;
        { platform::NativeOptions options;options.font_directory=argv[2];options.appearance_file=preference;
          platform::NativeHost host(options,&observer,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance) {
              observer.dark=appearance.dark;static ui::BootstrapState state;ui::DrawBootstrap(theme,appearance,state);
          });host.Run(); }
        Check(observer.cleaned,"host lifetime complete");
    }
    std::ofstream record(out/"result.json");record<<"{\"selected\":3,\"executed\":3,\"passed\":"<<(failures?0:3)<<",\"failed\":"<<(failures?3:0)<<",\"skipped\":0,\"assertions\":"<<checks<<",\"assertionFailures\":"<<failures<<"}\n";
    std::printf("selected=3 executed=3 passed=%d failed=%d skipped=0 assertions=%d assertionFailures=%d\n",failures?0:3,failures?3:0,checks,failures);
    return failures?1:0;
}
