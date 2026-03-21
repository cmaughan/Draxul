#include "cube_render_pass.h"
#include "ui_treesitter_panel.h"
#include <draxul/base_renderer.h>
#include <draxul/log.h>
#include <draxul/megacity_host.h>
#include <draxul/renderer.h>
#include <imgui.h>

#ifndef DRAXUL_REPO_ROOT
#define DRAXUL_REPO_ROOT "."
#endif

namespace draxul
{

MegaCityHost::MegaCityHost()
    : cube_pass_(std::make_shared<CubeRenderPass>())
    , last_frame_time_(std::chrono::steady_clock::now())
{
}

MegaCityHost::~MegaCityHost() = default;

bool MegaCityHost::initialize(const HostContext& context, HostCallbacks callbacks)
{
    callbacks_ = std::move(callbacks);
    viewport_ = context.initial_viewport;
    pixel_w_ = viewport_.pixel_width > 0 ? viewport_.pixel_width : 800;
    pixel_h_ = viewport_.pixel_height > 0 ? viewport_.pixel_height : 600;
    running_ = true;

    scanner_.start(DRAXUL_REPO_ROOT);

    DRAXUL_LOG_INFO(LogCategory::App, "MegaCityHost initialized (%dx%d), scanning %s",
        pixel_w_, pixel_h_, DRAXUL_REPO_ROOT);
    return true;
}

void MegaCityHost::attach_imgui_host(IImGuiHost& host)
{
    imgui_host_ = &host;

    IMGUI_CHECKVERSION();
    imgui_ctx_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui_ctx_);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();
    host.initialize_imgui_backend();

    DRAXUL_LOG_INFO(LogCategory::App, "MegaCityHost: ImGui context created and backend initialized");
}

void MegaCityHost::on_mouse_move(const MouseMoveEvent& event)
{
    if (!imgui_ctx_)
        return;
    ImGui::SetCurrentContext(imgui_ctx_);
    const float scale = viewport_.pixel_scale;
    ImGui::GetIO().AddMousePosEvent(
        static_cast<float>(event.x) * scale, static_cast<float>(event.y) * scale);
}

void MegaCityHost::on_mouse_button(const MouseButtonEvent& event)
{
    if (!imgui_ctx_)
        return;
    ImGui::SetCurrentContext(imgui_ctx_);
    int button = -1;
    switch (event.button)
    {
    case 1:
        button = 0;
        break;
    case 2:
        button = 2;
        break;
    case 3:
        button = 1;
        break;
    default:
        break;
    }
    if (button >= 0)
        ImGui::GetIO().AddMouseButtonEvent(button, event.pressed);
}

void MegaCityHost::on_mouse_wheel(const MouseWheelEvent& event)
{
    if (!imgui_ctx_)
        return;
    ImGui::SetCurrentContext(imgui_ctx_);
    ImGui::GetIO().AddMouseWheelEvent(event.dx, event.dy);
}

void MegaCityHost::set_imgui_font(const std::string& path, float size_pixels)
{
    if (!imgui_ctx_ || !imgui_host_)
        return;

    ImGui::SetCurrentContext(imgui_ctx_);
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    if (!path.empty() && size_pixels > 0.0f)
        io.Fonts->AddFontFromFileTTF(path.c_str(), size_pixels);
    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();
    imgui_host_->rebuild_imgui_font_texture();
}

ImDrawData* MegaCityHost::render_imgui(float dt)
{
    if (!imgui_ctx_ || !imgui_host_)
        return nullptr;

    ImGui::SetCurrentContext(imgui_ctx_);
    imgui_host_->begin_imgui_frame();

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(pixel_w_), static_cast<float>(pixel_h_));
    io.DeltaTime = dt > 0.0f ? dt : (1.0f / 60.0f);

    ImGui::NewFrame();
    render_treesitter_panel(pixel_w_, pixel_h_, scanner_.snapshot());
    ImGui::Render();

    return ImGui::GetDrawData();
}

void MegaCityHost::attach_3d_renderer(I3DRenderer& renderer)
{
    renderer_3d_ = &renderer;
    renderer_3d_->register_render_pass(cube_pass_);
    DRAXUL_LOG_INFO(LogCategory::App, "MegaCityHost: 3D renderer attached, cube pass registered");
}

void MegaCityHost::detach_3d_renderer()
{
    if (renderer_3d_)
    {
        renderer_3d_->unregister_render_pass();
        renderer_3d_ = nullptr;
    }
}

void MegaCityHost::shutdown()
{
    scanner_.stop();

    if (imgui_ctx_)
    {
        ImGui::SetCurrentContext(imgui_ctx_);
        if (imgui_host_)
            imgui_host_->shutdown_imgui_backend();
        ImGui::DestroyContext(imgui_ctx_);
        imgui_ctx_ = nullptr;
    }
    imgui_host_ = nullptr;

    detach_3d_renderer();
    running_ = false;
}

bool MegaCityHost::is_running() const
{
    return running_;
}

std::string MegaCityHost::init_error() const
{
    return {};
}

void MegaCityHost::set_viewport(const HostViewport& viewport)
{
    viewport_ = viewport;
    pixel_w_ = viewport.pixel_width > 0 ? viewport.pixel_width : pixel_w_;
    pixel_h_ = viewport.pixel_height > 0 ? viewport.pixel_height : pixel_h_;
}

void MegaCityHost::pump()
{
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last_frame_time_).count();
    last_frame_time_ = now;

    constexpr float kRotationSpeed = 1.2f; // radians per second
    rotation_angle_ += dt * kRotationSpeed;

    cube_pass_->set_angle(rotation_angle_);
    callbacks_.request_frame();
}

std::optional<std::chrono::steady_clock::time_point> MegaCityHost::next_deadline() const
{
    // Return "now" so pump() is called every frame
    return std::chrono::steady_clock::now();
}

bool MegaCityHost::dispatch_action(std::string_view action)
{
    if (action == "quit" || action == "request_quit")
    {
        running_ = false;
        callbacks_.request_quit();
        return true;
    }
    return false;
}

void MegaCityHost::request_close()
{
    // App-initiated quit: just stop running. Do NOT call callbacks_.request_quit()
    // here — that would call back into App::request_quit(), which calls this method
    // again, causing infinite mutual mutual recursion and a stack overflow.
    running_ = false;
}

Color MegaCityHost::default_background() const
{
    return { 0.05f, 0.05f, 0.10f, 1.0f }; // dark navy — city at night
}

HostRuntimeState MegaCityHost::runtime_state() const
{
    HostRuntimeState s;
    s.content_ready = true;
    s.last_activity_time = last_frame_time_;
    return s;
}

HostDebugState MegaCityHost::debug_state() const
{
    HostDebugState s;
    s.name = "megacity";
    s.grid_cols = 0;
    s.grid_rows = 0;
    s.dirty_cells = 0;
    return s;
}

std::unique_ptr<IHost> create_megacity_host()
{
    return std::make_unique<MegaCityHost>();
}

} // namespace draxul
