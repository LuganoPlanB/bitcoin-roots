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
            std::puts("roots-gui 32.1-roots.1");
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--build-contract") {
            const auto value = "{\"stage\":\"native-bootstrap\"," + roots::gui::ProviderBuildInfo() + "," +
                roots::gui::ui::BuildInfo() + "," + roots::gui::platform::BuildInfo() + "}";
            std::puts(value.c_str());
            return 0;
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
