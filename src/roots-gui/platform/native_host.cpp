// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
// Stock backend sequencing follows the bundled MIT-licensed GLFW/OpenGL3 example.
#include "platform/native_host.h"
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <imkit/imkit.h>
#if !defined(__APPLE__)
#include <imgui_impl_opengl3.h>
#endif
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

namespace roots::gui::platform {
namespace {
std::atomic_bool active_host{false};
float ValidScale(float value) { return std::isfinite(value) && value > 0 ? value : 1; }
}
std::filesystem::path ExecutableFontDirectory()
{
#if defined(__linux__)
    return std::filesystem::read_symlink("/proc/self/exe").parent_path() / "roots-gui-assets/fonts";
#else
    throw std::runtime_error("native asset location is not yet qualified on this platform");
#endif
}
struct NativeHost::Impl {
    NativeOptions options;
    NativeObserver* observer{};
    NativeHost::Draw content;
    NativeHost* host{};
    std::thread::id owner{std::this_thread::get_id()};
    GLFWwindow* window{};
    bool acquired{}, glfw{}, context{}, platform{}, renderer{}, dirty{true};
    int iteration{}, rendered{};
    ui::BootstrapState draw_state;
    ui::BootstrapAppearance appearance;
    imkit::Theme theme;
    ImFont* regular{};
    ImFont* emphasis{};
    std::string renderer_name;
    std::vector<unsigned char> pixels;
    std::vector<unsigned int> texture_ids;
    int frame_left{}, frame_top{}, frame_right{}, frame_bottom{}, frame_queries{};
    float frame_dpi_x{}, frame_dpi_y{};
    bool frame_extents_known{};

    Impl(NativeOptions value, NativeObserver* inspect, NativeHost::Draw draw) : options(std::move(value)), observer(inspect), content(std::move(draw))
    {
        if (active_host.exchange(true)) throw std::logic_error("one native host may own GLFW/ImGui at a time");
        acquired = true;
    }
    void Owner() const
    {
        if (owner != std::this_thread::get_id()) throw std::logic_error("native operations require the host thread");
    }
    void Initialize()
    {
        Owner();
#if defined(__APPLE__)
        throw std::runtime_error("macOS Metal production lifecycle remains follow-on work");
#else
        if (options.width < 960 || options.height < 640 || !std::isfinite(options.font_scale) ||
            options.font_scale < .5F || options.font_scale > 2.F) throw std::invalid_argument("invalid native window/font settings");
        glfwSetErrorCallback([](int code, const char* message) { std::fprintf(stderr, "GLFW %d: %s\n", code, message); });
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        glfw = true;
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
        window = glfwCreateWindow(options.width, options.height, "Bitcoin Roots", nullptr, nullptr);
        if (!window) throw std::runtime_error("native window/OpenGL context creation failed");
        glfwSetWindowSizeLimits(window, 960, 640, GLFW_DONT_CARE, GLFW_DONT_CARE);
        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);
        const auto* name = glGetString(GL_RENDERER);
        if (!name) throw std::runtime_error("OpenGL context could not be made current");
        renderer_name = reinterpret_cast<const char*>(name);
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        context = true;
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigDpiScaleFonts = false; // Host derives typography/layout once from native scale.
        io.ConfigDpiScaleViewports = false;
        if (options.font_directory.empty()) options.font_directory = ExecutableFontDirectory();
        for (const auto* file : {"Inter-Regular.ttf", "Inter-SemiBold.ttf"}) {
            if (!std::filesystem::is_regular_file(options.font_directory/file)) throw std::runtime_error("required Inter font asset is missing");
        }
        regular = io.Fonts->AddFontFromFileTTF((options.font_directory/"Inter-Regular.ttf").string().c_str(), 14);
        emphasis = io.Fonts->AddFontFromFileTTF((options.font_directory/"Inter-SemiBold.ttf").string().c_str(), 20);
        if (!regular || !emphasis) throw std::runtime_error("required licensed Inter fonts could not be loaded");
        io.FontDefault = regular; // Atlas owns file data; pointers survive all submitted frames.
        if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) throw std::runtime_error("GLFW ImGui backend initialization failed");
        platform = true; // Stock callbacks always forward focus, mouse, key and character events.
        if (!ImGui_ImplOpenGL3_Init("#version 330 core")) throw std::runtime_error("OpenGL3 ImGui backend initialization failed");
        renderer = true;
#endif
    }
    NativeFrame Facts()
    {
        NativeFrame f;
        f.iteration = iteration; f.rendered_frames = rendered;
        glfwGetWindowSize(window, &f.width, &f.height);
        glfwGetFramebufferSize(window, &f.framebuffer_width, &f.framebuffer_height);
        glfwGetWindowContentScale(window, &f.dpi_x, &f.dpi_y);
        f.dpi_x = ValidScale(f.dpi_x); f.dpi_y = ValidScale(f.dpi_y);
        f.effective_scale = options.font_scale * std::max(f.dpi_x, f.dpi_y);
        f.iconified = glfwGetWindowAttrib(window, GLFW_ICONIFIED);
        f.decorated = glfwGetWindowAttrib(window, GLFW_DECORATED);
        f.should_close = glfwWindowShouldClose(window);
        // Some X11 WMs do not answer frame-extents requests for unmapped
        // windows. Decorations are observation metadata, not a rendering input:
        // cache mapped extents and never query them while minimized/hidden.
        if (!f.iconified && glfwGetWindowAttrib(window, GLFW_VISIBLE) &&
            (!frame_extents_known || frame_dpi_x != f.dpi_x || frame_dpi_y != f.dpi_y)) {
            glfwGetWindowFrameSize(window, &frame_left, &frame_top, &frame_right, &frame_bottom);
            ++frame_queries;
            frame_extents_known = frame_left || frame_top || frame_right || frame_bottom;
            frame_dpi_x = f.dpi_x; frame_dpi_y = f.dpi_y;
        }
        f.frame_left = frame_left; f.frame_top = frame_top;
        f.frame_right = frame_right; f.frame_bottom = frame_bottom;
        f.frame_extents_queries = frame_queries;
        return f;
    }
    int Loop()
    {
        Owner();
#if defined(__APPLE__)
        return 1;
#else
        for (;;) {
            ++iteration;
            glfwPollEvents();
            auto facts = Facts();
            if (observer) observer->Idle(*host, facts);
            if (glfwWindowShouldClose(window)) break;
            facts = Facts();
            if (facts.iconified || facts.framebuffer_width <= 0 || facts.framebuffer_height <= 0) {
                glfwWaitEventsTimeout(.01); // Continue pumping events; never render a zero drawable.
                continue;
            }
            const float dpi = std::max(facts.dpi_x, facts.dpi_y);
            if (dirty || appearance.dpi_scale != dpi) {
                glfwSetWindowSizeLimits(window, int(std::ceil(960 * dpi)), int(std::ceil(640 * dpi)), GLFW_DONT_CARE, GLFW_DONT_CARE);
                appearance = {options.dark, options.font_scale, dpi};
                theme = ui::MakeBootstrapTheme(appearance);
                theme.fonts = {regular, emphasis, emphasis};
                ImGui::GetStyle().FontScaleDpi = 1;
                imkit::ApplyTheme(theme, 1); // Fresh base tokens; no cumulative size/font scaling.
                ImGui::GetStyle().Colors[ImGuiCol_NavCursor] = {.455F, .408F, 1, 1};
                dirty = false;
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            ImGui::PushFont(regular, theme.typography.body);
            if (content) content(theme, appearance);
            else facts.draw = ui::DrawBootstrap(theme, appearance, draw_state);
            ImGui::PopFont();
            auto& io = ImGui::GetIO();
            facts.space_down = ImGui::IsKeyDown(ImGuiKey_Space);
            facts.mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            facts.capture_keyboard = io.WantCaptureKeyboard;
            facts.capture_mouse = io.WantCaptureMouse;
            ImGui::Render();
            for (auto* texture : ImGui::GetPlatformIO().Textures) {
                facts.texture_creates += texture->Status == ImTextureStatus_WantCreate;
                facts.texture_updates += texture->Status == ImTextureStatus_WantUpdates;
            }
            glViewport(0, 0, facts.framebuffer_width, facts.framebuffer_height);
            glClearColor(theme.semantic.canvas.x, theme.semantic.canvas.y, theme.semantic.canvas.z, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            facts.gl_error = glGetError();
            if (facts.gl_error) throw std::runtime_error("native renderer reported an OpenGL error");
            for (auto* texture : ImGui::GetPlatformIO().Textures) {
                if (texture->TexID != ImTextureID_Invalid) {
                    const auto id = static_cast<unsigned int>(texture->TexID);
                    if (std::find(texture_ids.begin(), texture_ids.end(), id) == texture_ids.end()) texture_ids.push_back(id);
                    ++facts.live_textures;
                }
            }
            facts.font_texture = static_cast<unsigned int>(io.Fonts->TexRef.GetTexID());
            facts.rendered_frames = ++rendered;
            if (observer && observer->CaptureFramebuffer()) {
                pixels.resize(static_cast<std::size_t>(facts.framebuffer_width) * facts.framebuffer_height * 4);
                glReadPixels(0, 0, facts.framebuffer_width, facts.framebuffer_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                if (glGetError()) throw std::runtime_error("framebuffer readback failed");
                facts.rgba = pixels;
            }
            if (facts.draw.close_requested) glfwSetWindowShouldClose(window, GLFW_TRUE);
            if (observer) observer->Frame(*host, facts);
            glfwSwapBuffers(window);
        }
        return 0;
#endif
    }
    ~Impl()
    {
        if (owner != std::this_thread::get_id()) std::terminate();
        NativeTeardown state;
#if !defined(__APPLE__)
        if (window) glfwMakeContextCurrent(window);
        if (renderer) ImGui_ImplOpenGL3_Shutdown();
        if (context) state.renderer_cleared = ImGui::GetIO().BackendRendererUserData == nullptr;
        state.textures_released = true;
        for (const auto id : texture_ids) state.textures_released &= glIsTexture(id) == GL_FALSE;
        if (platform) ImGui_ImplGlfw_Shutdown();
        if (context) state.platform_cleared = ImGui::GetIO().BackendPlatformUserData == nullptr;
        if (context) ImGui::DestroyContext();
        state.imgui_cleared = ImGui::GetCurrentContext() == nullptr;
        if (window) { glfwMakeContextCurrent(nullptr); glfwDestroyWindow(window); }
        state.context_cleared = !glfw || glfwGetCurrentContext() == nullptr;
        if (glfw) glfwTerminate();
#endif
        if (acquired) active_host = false;
        if (observer) { try { observer->Teardown(state); } catch (...) { std::fputs("native teardown observer failed\n", stderr); } }
    }
};
NativeHost::NativeHost(NativeOptions options, NativeObserver* observer, Draw draw) : m_impl(std::make_unique<Impl>(std::move(options), observer, std::move(draw)))
{
    m_impl->host = this;
    m_impl->Initialize();
}
NativeHost::~NativeHost() = default;
int NativeHost::Run() { return m_impl->Loop(); }
GLFWwindow* NativeHost::Window() { m_impl->Owner(); return m_impl->window; }
void NativeHost::RequestClose() { m_impl->Owner(); glfwSetWindowShouldClose(m_impl->window, GLFW_TRUE); }
void NativeHost::SetFontScale(float scale)
{
    m_impl->Owner();
    if (!std::isfinite(scale) || scale < .5F || scale > 2) throw std::invalid_argument("font scale must be between 50% and 200%");
    m_impl->options.font_scale = scale;
    m_impl->dirty = true;
}
void NativeHost::RecreateRendererResources()
{
    m_impl->Owner();
#if !defined(__APPLE__)
    ImGui_ImplOpenGL3_DestroyDeviceObjects();
#endif
}
const std::string& NativeHost::Renderer() const { m_impl->Owner(); return m_impl->renderer_name; }
}
