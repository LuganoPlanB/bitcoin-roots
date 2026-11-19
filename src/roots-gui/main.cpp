// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "application/build_info.h"
#include "platform/foundation.h"
#include "ui/bootstrap.h"
#include <cstdio>
#include <exception>
#include <string_view>
int main(int argc, char** argv)
{
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--version") {
            std::printf("roots-gui %s\n",roots::gui::GUI_VERSION);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--build-contract") {
            const auto value = "{\"stage\":\"native-bootstrap\"," + roots::gui::ProviderBuildInfo() + "," +
                roots::gui::ui::BuildInfo() + "," + roots::gui::platform::BuildInfo() + "}";
            std::puts(value.c_str());
            return 0;
        }
        if(argc==2 && std::string_view(argv[1]).starts_with("--resume-gui-fd=")){
            auto resumed=roots::gui::platform::ConsumeRestartContext(argv[1]);
            if(!resumed){std::fputs("roots-gui: invalid GUI restart context\n",stderr);return 2;}
            return roots::gui::platform::Run(std::move(resumed));
        }
        if (argc != 1) {
            std::fputs("usage: roots-gui [--version|--build-contract]\n", stderr);
            return 2;
        }
        return roots::gui::platform::Run();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "roots-gui: %s\n", error.what());
        return 1;
    }
}
