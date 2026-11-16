// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
// Backend call order follows the bundled, MIT-licensed GLFW/OpenGL3 example.
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imkit/imkit.h>
#include <cstdio>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    int executed{}, failed{};
    const auto check = [&](bool ok, const char* description) {
        ++executed;
        if (!ok) { ++failed; std::fprintf(stderr, "FAIL %s\n", description); }
    };
    glfwSetErrorCallback([](int code, const char* message) { std::fprintf(stderr, "GLFW %d: %s\n", code, message); });
    if (!glfwInit()) return 2;
    check(glfwGetPlatform() == GLFW_PLATFORM_X11, "real X11 platform");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(640, 480, "Dependency capability probe", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 2; }
    check(glfwGetWindowAttrib(window, GLFW_DECORATED) == GLFW_TRUE, "native decoration requested");
    glfwMakeContextCurrent(window);
    std::printf("GL version=%s renderer=%s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    check(io.Fonts->AddFontFromFileTTF(argv[1], 14) != nullptr, "vendored Inter source");
    const bool platform = ImGui_ImplGlfw_InitForOpenGL(window, true);
    const bool renderer = platform && ImGui_ImplOpenGL3_Init("#version 330 core");
    check(platform && renderer, "real platform/renderer initialization");
    if (renderer) {
        check((io.BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0, "renderer dynamic textures");
        check((io.BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset) != 0, "renderer vertex offset");
        check((io.BackendFlags & ImGuiBackendFlags_RendererHasViewports) != 0 &&
              (io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports) != 0, "compiled X11 viewport capability");
        imkit::ApplyTheme(imkit::MakePrecisionTheme(imkit::ColorScheme::Dark), 1.0F);
        for (int frame = 0; frame < 3; ++frame) {
            glfwPollEvents();
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos({20, 20});
            ImGui::SetNextWindowSize({400, 250});
            ImGui::Begin("Real ImKit renderer probe");
            imkit::Button("Public-data probe");
            ImGui::TextUnformatted("Pinned font and dynamic texture upload");
            ImGui::End();
            ImGui::Render();
            int width{}, height{};
            glfwGetFramebufferSize(window, &width, &height);
            glViewport(0, 0, width, height);
            glClearColor(0.8F, 0.1F, 0.2F, 1.0F);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            check(glGetError() == GL_NO_ERROR, "render without GL errors");
            check(ImGui::GetDrawData()->TotalVtxCount > 0, "real draw data submitted");
            if (frame == 2 && width > 0 && height > 0) {
                std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
                glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                bool changed{false};
                for (std::size_t i = 4; i < pixels.size(); i += 4) {
                    if (pixels[i] != pixels[0] || pixels[i + 1] != pixels[1] || pixels[i + 2] != pixels[2]) { changed = true; break; }
                }
                check(changed && glGetError() == GL_NO_ERROR, "framebuffer contains drawn UI beyond clear color");
                check(io.Fonts->TexRef.GetTexID() != ImTextureID_Invalid, "font texture uploaded by real renderer");
            }
            glfwSwapBuffers(window);
        }
        check((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) == 0, "detachable OS viewports remain disabled");
        ImGui_ImplOpenGL3_Shutdown();
    }
    if (platform) ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    std::printf("selected=%d executed=%d passed=%d failed=%d skipped=0; dependency Xvfb/Mesa probe, not production lifecycle\n", executed, executed, executed - failed, failed);
    return failed ? 1 : 0;
}
