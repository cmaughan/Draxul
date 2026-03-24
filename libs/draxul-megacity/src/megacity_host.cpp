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

bool MegaCityHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    callbacks_ = &callbacks;
    viewport_ = context.initial_viewport;
    pixel_w_ = viewport_.pixel_size.x > 0 ? viewport_.pixel_size.x : 800;
    pixel_h_ = viewport_.pixel_size.y > 0 ? viewport_.pixel_size.y : 600;
    running_ = true;

    scanner_.start(DRAXUL_REPO_ROOT);

    DRAXUL_LOG_INFO(LogCategory::App, "MegaCityHost initialized (%dx%d), scanning %s",
        pixel_w_, pixel_h_, DRAXUL_REPO_ROOT);
    return true;
}

void MegaCityHost::on_mouse_move(const MouseMoveEvent& /*event*/)
{
    // The Megacity camera is keyboard-driven today; mouse motion is ignored.
}

void MegaCityHost::on_mouse_button(const MouseButtonEvent& /*event*/)
{
    // The Megacity view currently has no mouse-click interactions.
}

void MegaCityHost::on_mouse_wheel(const MouseWheelEvent& /*event*/)
{
    // The Megacity view currently has no wheel-driven zoom or scroll behavior.
}

void MegaCityHost::set_imgui_font(const std::string&, float)
{
    // Megacity uses the shared app ImGui context and does not own fonts itself.
}

void MegaCityHost::render_imgui(float dt)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(pixel_w_), static_cast<float>(pixel_h_));
    io.DeltaTime = dt > 0.0f ? dt : (1.0f / 60.0f);

    // Fullscreen dockspace — PassthruCentralNode keeps the 3D background visible
    // through any undocked central area.
    const ImGuiWindowFlags ds_flags = ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoBackground;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(pixel_w_), static_cast<float>(pixel_h_)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##dockspace_root", nullptr, ds_flags);
    ImGui::PopStyleVar(3);
    ImGui::DockSpace(ImGui::GetID("MegaCityDock"), ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    render_treesitter_panel(pixel_w_, pixel_h_, scanner_.snapshot());
}

void MegaCityHost::attach_3d_renderer(I3DRenderer& renderer)
{
    renderer_3d_ = &renderer;
    renderer_3d_->register_render_pass(cube_pass_);
    renderer_3d_->set_3d_viewport(viewport_.pixel_pos.x, viewport_.pixel_pos.y, pixel_w_, pixel_h_);
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
    pixel_w_ = viewport.pixel_size.x > 0 ? viewport.pixel_size.x : pixel_w_;
    pixel_h_ = viewport.pixel_size.y > 0 ? viewport.pixel_size.y : pixel_h_;
    if (renderer_3d_)
        renderer_3d_->set_3d_viewport(viewport_.pixel_pos.x, viewport_.pixel_pos.y, pixel_w_, pixel_h_);
}

void MegaCityHost::pump()
{
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - last_frame_time_).count();
    last_frame_time_ = now;

    constexpr float kRotationSpeed = 1.2f; // radians per second
    rotation_angle_ += dt * kRotationSpeed;

    cube_pass_->set_angle(rotation_angle_);
    callbacks_->request_frame();
}

std::optional<std::chrono::steady_clock::time_point> MegaCityHost::next_deadline() const
{
    // Return "now" so pump() is called every frame.
    return std::chrono::steady_clock::now();
}

bool MegaCityHost::dispatch_action(std::string_view action)
{
    if (action == "quit" || action == "request_quit")
    {
        running_ = false;
        callbacks_->request_quit();
        return true;
    }
    return false;
}

void MegaCityHost::request_close()
{
    // App-initiated quit: just stop running. Do not call request_quit() here,
    // because App::request_quit() already delegates to this method.
    running_ = false;
}

Color MegaCityHost::default_background() const
{
    return Color(0.05f, 0.05f, 0.10f, 1.0f); // dark navy, for the night-city scene
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
