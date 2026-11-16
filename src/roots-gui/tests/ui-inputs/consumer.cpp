// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imkit/imkit.h>

#include <cstdio>
#include <cstring>

static_assert(IMGUI_VERSION_NUM == 19297);
#if !defined(IMGUI_HAS_DOCK) || !defined(IMGUI_HAS_VIEWPORT) || !defined(IMGUI_HAS_TEXTURES)
#error Required pinned ImGui capabilities are absent
#endif

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    int executed{0}, failed{0};
    const auto check = [&](bool condition, const char* description) {
        ++executed;
        if (!condition) { ++failed; std::fprintf(stderr, "FAIL %s\n", description); }
    };
    // Force real backend/link dependencies without pretending a native context exists.
    auto* volatile platform_entry = &ImGui_ImplGlfw_InitForOpenGL;
    auto* volatile renderer_entry = &ImGui_ImplOpenGL3_Init;
    check(platform_entry != nullptr && renderer_entry != nullptr, "matching backend symbols link");
    int major{}, minor{}, revision{};
    glfwGetVersion(&major, &minor, &revision);
    check(major == 3 && minor == 5 && revision == 1, "pinned GLFW version");
    std::printf("GLFW compiled capabilities: %s\n", glfwGetVersionString());
    IMGUI_CHECKVERSION();
    auto* context = ImGui::CreateContext();
    check(context != nullptr, "context creation/layout agreement");
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {960, 640};
    io.DeltaTime = 1.0F / 60.0F;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    check(io.BackendFlags == 0, "no native renderer capabilities fabricated");
    check(io.Fonts->AddFontFromFileTTF(argv[1], 14) != nullptr, "vendored Inter font loads");
    unsigned char* pixels{};
    int width{}, height{};
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    check(pixels && width > 0 && height > 0, "host CPU font atlas builds");
    bool selected{false};
    char text[32] = "public fixture";
    const auto theme = imkit::MakePrecisionTheme(imkit::ColorScheme::Dark);
    imkit::ApplyTheme(theme, 1.0F);
    const auto padding = ImGui::GetStyle().FramePadding.y;
    imkit::ApplyTheme(theme, 1.0F);
    check(ImGui::GetStyle().FramePadding.y == padding, "theme application does not accumulate scale");
    for (int frame = 0; frame < 2; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize({600, 400});
        ImGui::Begin("Pinned ImKit consumer");
        bool changed = imkit::Button("Prepare") | imkit::Checkbox("Selected", &selected);
        changed |= imkit::InputText("Public text", text, sizeof(text));
        check(!changed && !selected && std::strcmp(text, "public fixture") == 0, "no input leaves host model unchanged");
        ImGui::End();
        ImGui::Render();
    }
    check(ImGui::GetDrawData() && ImGui::GetDrawData()->TotalVtxCount > 0, "real ImKit produces draw data");
    check((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) == 0, "OS viewports remain opt-in pending native evidence");
    ImGui::DestroyContext(context);
    std::printf("selected=%d executed=%d passed=%d failed=%d skipped=0; CPU consumer only, no native renderer run\n",
                executed, executed, executed - failed, failed);
    return failed ? 1 : 0;
}
