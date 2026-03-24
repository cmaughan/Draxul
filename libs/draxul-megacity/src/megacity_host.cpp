#include "isometric_camera.h"
#include "isometric_scene_pass.h"
#include "isometric_world.h"
#include "ui_treesitter_panel.h"
#include <SDL3/SDL.h>
#include <draxul/log.h>
#include <draxul/megacity_host.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#ifndef DRAXUL_REPO_ROOT
#define DRAXUL_REPO_ROOT "."
#endif

namespace draxul
{

MegaCityHost::MegaCityHost() = default;

MegaCityHost::~MegaCityHost() = default;

bool MegaCityHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    callbacks_ = &callbacks;
    viewport_ = context.initial_viewport;
    pixel_w_ = viewport_.pixel_size.x > 0 ? viewport_.pixel_size.x : 800;
    pixel_h_ = viewport_.pixel_size.y > 0 ? viewport_.pixel_size.y : 600;

    world_ = std::make_unique<IsometricWorld>();
    camera_ = std::make_unique<IsometricCamera>();
    camera_->set_viewport(pixel_w_, pixel_h_);
    camera_->look_at_world_center(world_->width() * world_->tile_size(), world_->height() * world_->tile_size());
    scene_pass_ = std::make_shared<IsometricScenePass>(world_->width(), world_->height(), world_->tile_size());

    running_ = true;
    last_activity_time_ = std::chrono::steady_clock::now();
    scanner_.start(DRAXUL_REPO_ROOT);
    mark_scene_dirty();

    DRAXUL_LOG_INFO(LogCategory::App, "MegaCityHost initialized (%dx%d), scanning %s",
        pixel_w_, pixel_h_, DRAXUL_REPO_ROOT);
    return true;
}

void MegaCityHost::mark_scene_dirty()
{
    scene_dirty_ = true;
    last_activity_time_ = std::chrono::steady_clock::now();
    if (callbacks_)
        callbacks_->request_frame();
}

void MegaCityHost::on_key(const KeyEvent& event)
{
    if (!event.pressed || !world_ || world_->objects().empty())
        return;

    int dx = 0;
    int dy = 0;
    switch (event.keycode)
    {
    case SDLK_LEFT:
        dx = -1;
        break;
    case SDLK_RIGHT:
        dx = 1;
        break;
    case SDLK_UP:
        dy = -1;
        break;
    case SDLK_DOWN:
        dy = 1;
        break;
    default:
        return;
    }

    auto& obj = world_->objects().front();
    const int next_x = obj.x + dx;
    const int next_y = obj.y + dy;
    if (!world_->is_valid(next_x, next_y))
        return;

    obj.x = next_x;
    obj.y = next_y;
    mark_scene_dirty();
}

void MegaCityHost::on_mouse_move(const MouseMoveEvent& /*event*/)
{
    // The Megacity camera is fixed in the first isometric prototype.
}

void MegaCityHost::on_mouse_button(const MouseButtonEvent& /*event*/)
{
    // The Megacity view currently has no mouse-click interactions.
}

void MegaCityHost::on_mouse_wheel(const MouseWheelEvent& /*event*/)
{
    // The Megacity view currently has no wheel-driven zoom behavior.
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
    renderer_3d_->register_render_pass(scene_pass_);
    renderer_3d_->set_3d_viewport(viewport_.pixel_pos.x, viewport_.pixel_pos.y, pixel_w_, pixel_h_);

    if (scene_pass_)
    {
        scene_pass_->set_scene(build_scene_snapshot());
        scene_dirty_ = false;
    }
    if (callbacks_)
        callbacks_->request_frame();

    DRAXUL_LOG_INFO(LogCategory::App, "MegaCityHost: 3D renderer attached, scene pass registered");
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
    scene_pass_.reset();
    camera_.reset();
    world_.reset();
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

    if (camera_)
        camera_->set_viewport(pixel_w_, pixel_h_);
    if (renderer_3d_)
        renderer_3d_->set_3d_viewport(viewport_.pixel_pos.x, viewport_.pixel_pos.y, pixel_w_, pixel_h_);

    mark_scene_dirty();
}

SceneSnapshot MegaCityHost::build_scene_snapshot() const
{
    SceneSnapshot scene;
    if (!camera_ || !world_)
        return scene;

    scene.camera.view = camera_->view_matrix();
    scene.camera.proj = camera_->proj_matrix();
    scene.camera.light_dir = glm::normalize(glm::vec4(-0.5f, -1.0f, -0.3f, 0.0f));

    SceneObject grid;
    grid.mesh = MeshId::Grid;
    grid.world = glm::mat4(1.0f);
    grid.color = glm::vec4(1.0f);
    scene.objects.push_back(grid);

    for (const auto& obj : world_->objects())
    {
        SceneObject cube;
        cube.mesh = MeshId::Cube;
        const glm::vec3 pos = world_->grid_to_world(obj.x, obj.y, static_cast<float>(obj.elevation));
        cube.world = glm::translate(glm::mat4(1.0f), pos + glm::vec3(0.0f, 0.5f, 0.0f));
        cube.color = glm::vec4(obj.color, 1.0f);
        scene.objects.push_back(cube);
    }

    return scene;
}

void MegaCityHost::pump()
{
    if (!scene_dirty_ || !scene_pass_)
        return;

    scene_pass_->set_scene(build_scene_snapshot());
    scene_dirty_ = false;
}

std::optional<std::chrono::steady_clock::time_point> MegaCityHost::next_deadline() const
{
    return std::nullopt;
}

bool MegaCityHost::dispatch_action(std::string_view action)
{
    if (action == "quit" || action == "request_quit")
    {
        running_ = false;
        if (callbacks_)
            callbacks_->request_quit();
        return true;
    }
    return false;
}

void MegaCityHost::request_close()
{
    running_ = false;
}

Color MegaCityHost::default_background() const
{
    return Color(0.05f, 0.05f, 0.10f, 1.0f);
}

HostRuntimeState MegaCityHost::runtime_state() const
{
    HostRuntimeState s;
    s.content_ready = true;
    s.last_activity_time = last_activity_time_;
    return s;
}

HostDebugState MegaCityHost::debug_state() const
{
    HostDebugState s;
    s.name = "megacity";
    s.grid_cols = 0;
    s.grid_rows = 0;
    s.dirty_cells = scene_dirty_ ? 1u : 0u;
    return s;
}

std::unique_ptr<IHost> create_megacity_host()
{
    return std::make_unique<MegaCityHost>();
}

} // namespace draxul
