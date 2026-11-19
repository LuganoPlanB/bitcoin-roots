// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
// Stock backend sequencing follows the bundled MIT-licensed GLFW/OpenGL3 example.
#include "platform/native_host.h"
#include "platform/linux/appearance.h"
#include "ui/components/semantic_packet.h"
#include "ui/theme/appearance.h"
#include "ui/theme/theme.h"
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
#include <cstring>
#include <utility>

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
    std::optional<DesktopRuntime> desktop;
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
    SystemAppearance system_appearance;
    std::unique_ptr<LinuxAccessibility> accessibility;
    std::unique_ptr<NativeInput> input;
    const char* (*old_clipboard_get)(ImGuiContext*){};
    void (*old_clipboard_set)(ImGuiContext*,const char*){};
    void* old_clipboard_data{};bool clipboard_hooks{};
    ui::theme::Appearance choice{ui::theme::Appearance::Light};
    bool invalid_saved{},save_failed{};
    std::optional<bool> last_system_dark;
    ImFont* regular{};
    ImFont* emphasis{};
    ImFont* monospace{};
    std::string renderer_name;
    std::vector<unsigned char> pixels;
    std::vector<DroppedPaths> drops;
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
        if (!options.appearance_file.empty()) {
            const auto saved=ui::theme::LoadAppearance(options.appearance_file);
            choice=saved.choice;
            draw_state.show_appearance=true;
            draw_state.appearance_choice=static_cast<int>(choice);
            invalid_saved=!saved.valid;
        }
#if defined(__APPLE__)
        throw std::runtime_error("macOS Metal production lifecycle remains follow-on work");
#else
        if (options.width < 960 || options.height < 640 || !std::isfinite(options.font_scale) ||
            options.font_scale < .5F || options.font_scale > 2.F) throw std::invalid_argument("invalid native window/font settings");
        if (!std::isfinite(options.fixed_delta_seconds) || options.fixed_delta_seconds<0 || options.fixed_delta_seconds>1)
            throw std::invalid_argument("invalid presentation clock");
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
        glfwSetWindowUserPointer(window,this);
        glfwSetDropCallback(window,[](GLFWwindow* window,int count,const char** paths) {
            auto* self=static_cast<Impl*>(glfwGetWindowUserPointer(window));self->Owner();
            if(count<=0 || count>16 || self->drops.size()>=16)return;
            DroppedPaths drop;glfwGetCursorPos(window,&drop.x,&drop.y);
            for(int i=0;i<count;++i){if(!paths[i])return;const auto length=strnlen(paths[i],4097);if(!length || length>4096)return;drop.paths.emplace_back(paths[i],length);}
            self->drops.push_back(std::move(drop));
        });
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
        monospace=io.Fonts->AddFontDefaultVector(); // Embedded MIT ProggyForever; atlas-owned technical face.
        if (!monospace) throw std::runtime_error("required embedded monospace font could not be loaded");
        io.FontDefault = regular; // Atlas owns file data; pointers survive all submitted frames.
        if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) throw std::runtime_error("GLFW ImGui backend initialization failed");
        platform = true; // Stock callbacks always forward focus, mouse, key and character events.
        if (!ImGui_ImplOpenGL3_Init("#version 330 core")) throw std::runtime_error("OpenGL3 ImGui backend initialization failed");
        renderer = true;
        if(options.accessibility)accessibility=std::make_unique<LinuxAccessibility>(*options.accessibility);
        input=std::make_unique<NativeInput>(window);
        if(input->Status().clipboard){auto& platform_io=ImGui::GetPlatformIO();old_clipboard_get=platform_io.Platform_GetClipboardTextFn;old_clipboard_set=platform_io.Platform_SetClipboardTextFn;old_clipboard_data=platform_io.Platform_ClipboardUserData;platform_io.Platform_ClipboardUserData=input.get();clipboard_hooks=true;
            platform_io.Platform_GetClipboardTextFn=[](ImGuiContext*){return static_cast<NativeInput*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->ClipboardForPaste();};
            platform_io.Platform_SetClipboardTextFn=[](ImGuiContext*,const char* text){if(text)static_cast<NativeInput*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->Copy(text);};
        }
#endif
    }
    NativeFrame Facts()
    {
        NativeFrame f;
        f.iteration = iteration; f.rendered_frames = rendered;
        glfwGetWindowSize(window, &f.width, &f.height);
        glfwGetFramebufferSize(window, &f.framebuffer_width, &f.framebuffer_height);
        glfwGetWindowContentScale(window, &f.dpi_x, &f.dpi_y);
        if(input)if(auto scale=input->Status().system_scale)f.dpi_x=f.dpi_y=*scale;
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
            if(desktop)desktop->Poll();
            if(input)input->Pump();
            if(accessibility)accessibility->Pump();
            auto facts = Facts();
            if (observer) observer->Idle(*host, facts);
            if (glfwWindowShouldClose(window)) {
                if (!options.before_close || options.before_close()) break;
                glfwSetWindowShouldClose(window, GLFW_FALSE);
            }
            facts = Facts();
            if (facts.iconified || !glfwGetWindowAttrib(window,GLFW_VISIBLE)
                || facts.framebuffer_width <= 0 || facts.framebuffer_height <= 0) {
                if (options.on_draw_suspended) options.on_draw_suspended();
                if(desktop)desktop->Suspend();
                if(input){input->BeginFrame();input->EndFrame();}
                if(accessibility)accessibility->Update(0,0,0,0,false);
                glfwWaitEventsTimeout(.01); // Continue pumping events; never render a zero drawable.
                continue;
            }
            const float dpi = std::max(facts.dpi_x, facts.dpi_y);
            if (!options.appearance_file.empty()) {
                last_system_dark=options.system_appearance_reader?options.system_appearance_reader():system_appearance.Dark();
                const bool dark=ui::theme::IsDark(choice, last_system_dark.value_or(false));
                if (options.dark != dark) { options.dark=dark; dirty=true; }
            }
            if (dirty || appearance.dpi_scale != dpi) {
                glfwSetWindowSizeLimits(window, int(std::ceil(960 * dpi)), int(std::ceil(640 * dpi)), GLFW_DONT_CARE, GLFW_DONT_CARE);
                appearance = {options.dark, options.font_scale, dpi};
                appearance.monospace=monospace;
                theme = ui::MakeBootstrapTheme(appearance);
                theme.fonts = {regular, emphasis, emphasis};
                ImGui::GetStyle().FontScaleDpi = 1;
                ui::theme::Apply(theme); // Fresh base tokens; no cumulative size/font scaling.
                dirty = false;
            }
            // Status can change while the projected color stays Light. Publish
            // it every frame, independently of theme/font resource rebuilds.
            appearance.status=host->AppearanceState();
            appearance.reduced_motion=input?input->Status().reduced_motion.value_or(true):true;
            theme.motion.reducedMotion=appearance.reduced_motion;
            draw_state.appearance_choice=appearance.status.choice;
            draw_state.appearance_error=appearance.status.invalid_saved || appearance.status.save_failed;
            draw_state.system_unknown=appearance.status.system_unavailable;
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            if (options.fixed_delta_seconds>0) ImGui::GetIO().DeltaTime=options.fixed_delta_seconds;
            ImGui::NewFrame();
            ui::components::NativeInputScope native_scope(input.get());if(input)input->BeginFrame();
            ImGui::PushFont(regular, theme.typography.body);
            if (content) content(theme, appearance);
            else facts.draw = ui::DrawBootstrap(theme, appearance, draw_state);
            if(input){input->EndFrame();input->Pump();}
            if(accessibility){int x{},y{};glfwGetWindowPos(window,&x,&y);accessibility->Update(x,y,facts.width,facts.height,glfwGetWindowAttrib(window,GLFW_FOCUSED));accessibility->Pump();}
            if (facts.draw.appearance_requested >= 0 && !options.appearance_file.empty()) {
                host->SetAppearance(facts.draw.appearance_requested);
            }
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
        desktop.reset();
        if(context && clipboard_hooks){auto& platform_io=ImGui::GetPlatformIO();platform_io.Platform_GetClipboardTextFn=old_clipboard_get;platform_io.Platform_SetClipboardTextFn=old_clipboard_set;platform_io.Platform_ClipboardUserData=old_clipboard_data;}
        if(input){state.input_cleared=input->Close();input.reset();}
        if(accessibility){state.accessibility_cleared=accessibility->Close();accessibility.reset();}
        if(options.accessibility)state.accessibility_cleared=options.accessibility->Close() && state.accessibility_cleared;
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
    if(m_impl->options.desktop_ports)m_impl->desktop.emplace(*m_impl->options.desktop_ports);
}
NativeHost::~NativeHost() = default;
int NativeHost::Run() { return m_impl->Loop(); }
GLFWwindow* NativeHost::Window() { m_impl->Owner(); return m_impl->window; }
void NativeHost::RequestClose() { m_impl->Owner(); glfwSetWindowShouldClose(m_impl->window, GLFW_TRUE); }
bool NativeHost::Minimize()
{
    m_impl->Owner();
    if(!m_impl->window || glfwWindowShouldClose(m_impl->window))return false;
    glfwIconifyWindow(m_impl->window);
    return true;
}
DesktopRuntime* NativeHost::Desktop()
{
    m_impl->Owner();
    return m_impl->desktop?&*m_impl->desktop:nullptr;
}
void NativeHost::SetFontScale(float scale)
{
    m_impl->Owner();
    if (!std::isfinite(scale) || scale < .5F || scale > 2) throw std::invalid_argument("font scale must be between 50% and 200%");
    m_impl->options.font_scale = scale;
    m_impl->dirty = true;
}
bool NativeHost::SetAppearance(int choice)
{
    m_impl->Owner();
    if(choice<0 || choice>2)return false;
    if(m_impl->options.appearance_file.empty()){m_impl->save_failed=true;return false;}
    const auto value=static_cast<ui::theme::Appearance>(choice);
    if(!ui::theme::SaveAppearance(m_impl->options.appearance_file,value)){m_impl->save_failed=true;return false;}
    m_impl->choice=value;m_impl->draw_state.appearance_choice=choice;m_impl->draw_state.appearance_error=false;
    m_impl->invalid_saved=false;m_impl->save_failed=false;
    return true;
}
ui::AppearanceStatus NativeHost::AppearanceState() const
{
    m_impl->Owner();
    return {m_impl->options.appearance_file.empty()?-1:static_cast<int>(m_impl->choice),m_impl->invalid_saved,m_impl->save_failed,
        !m_impl->options.appearance_file.empty() && m_impl->choice==ui::theme::Appearance::System && !m_impl->last_system_dark.has_value()};
}
NativeInput& NativeHost::Input(){m_impl->Owner();if(!m_impl->input)throw std::logic_error("native input is not initialized");return *m_impl->input;}
InputStatus NativeHost::InputState() const {m_impl->Owner();return m_impl->input?m_impl->input->Status():InputStatus{};}
AccessibilityStatus NativeHost::AccessibilityState() const {m_impl->Owner();return m_impl->accessibility?m_impl->accessibility->Status():AccessibilityStatus{false,false,false,true,0,0,0,"","No semantic publisher is associated with this native host."};}
void NativeHost::RecreateRendererResources()
{
    m_impl->Owner();
#if !defined(__APPLE__)
    ImGui_ImplOpenGL3_DestroyDeviceObjects();
#endif
}
const std::string& NativeHost::Renderer() const { m_impl->Owner(); return m_impl->renderer_name; }
}

namespace roots::gui::platform {
std::vector<DroppedPaths> NativeHost::TakeDroppedPaths() {m_impl->Owner();return std::exchange(m_impl->drops,{});}
}
