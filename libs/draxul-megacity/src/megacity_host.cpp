#include "city_builder.h"
#include "city_input_state.h"
#include "isometric_camera.h"
#include "isometric_scene_pass.h"
#include "scene_snapshot_builder.h"
#include "scene_world.h"
#include "semantic_city_layout.h"
#include "sign_label_atlas.h"
#include "ui_city_map_panel.h"
#include "ui_treesitter_panel.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <draxul/log.h>
#include <draxul/megacity_host.h>
#include <draxul/text_service.h>
#include <filesystem>
#include <imgui.h>

#ifndef DRAXUL_REPO_ROOT
#define DRAXUL_REPO_ROOT "."
#endif

namespace draxul
{

namespace
{

constexpr float kMovementSpeedFractionPerSecond = 0.35f;
constexpr float kOrbitSpeedRadiansPerSecond = 1.8f;
constexpr float kZoomSpeedPerSecond = 1.35f;
constexpr float kPitchSpeedRadiansPerSecond = 0.9f;
constexpr auto kMovementTick = std::chrono::milliseconds(16);
constexpr auto kDragSmoothingTick = std::chrono::milliseconds(8);

MegaCityCodeConfig world_rebuild_signature(MegaCityCodeConfig config)
{
    config.auto_rebuild = false;
    config.sign_text_px_range = glm::vec2(0.0f);
    config.output_gamma = 1.0f;
    config.debug_view = MegaCityDebugView::FinalScene;
    config.wireframe = false;
    config.ao_denoise = true;
    config.ao_radius = 0.0f;
    config.ao_bias = 0.0f;
    config.ao_power = 0.0f;
    config.ao_kernel_size = 0;
    config.world_floor_height_scale = 0.0f;
    config.world_floor_top_y = 0.0f;
    config.world_floor_grid_y_offset = 0.0f;
    config.world_floor_grid_tile_scale = 0.0f;
    config.world_floor_grid_line_width = 0.0f;
    config.ambient_strength = 0.0f;
    config.directional_light_dir = glm::vec3(0.0f);
    config.point_light_position_valid = false;
    config.point_light_position = glm::vec3(0.0f);
    config.point_light_radius = 0.0f;
    config.point_light_brightness = 0.0f;
    config.camera_state_valid = false;
    config.camera_target = glm::vec2(0.0f);
    config.camera_yaw = 0.0f;
    config.camera_pitch = 0.0f;
    config.camera_orbit_radius = 0.0f;
    config.camera_zoom_half_height = 0.0f;
    return config;
}

bool requires_world_rebuild(const MegaCityCodeConfig& before, const MegaCityCodeConfig& after)
{
    return world_rebuild_signature(before) != world_rebuild_signature(after);
}

std::filesystem::path megacity_db_path()
{
    const std::filesystem::path repo_root = std::filesystem::path(DRAXUL_REPO_ROOT);
    if (const char* base_path_raw = SDL_GetBasePath())
    {
        const std::filesystem::path base_path(base_path_raw);

        std::error_code ec;
        const std::filesystem::path canonical_base = std::filesystem::weakly_canonical(base_path, ec);
        const std::filesystem::path canonical_repo = std::filesystem::weakly_canonical(repo_root, ec);
        if (!canonical_base.empty() && !canonical_repo.empty())
        {
            const std::string base_string = canonical_base.generic_string();
            const std::string repo_string = canonical_repo.generic_string();
            if (base_string == repo_string
                || (base_string.size() > repo_string.size()
                    && base_string.compare(0, repo_string.size(), repo_string) == 0
                    && base_string[repo_string.size()] == '/'))
            {
                return repo_root / "db" / "megacity.sqlite3";
            }
        }
    }

#ifdef __APPLE__
    const char* home = std::getenv("HOME");
    const std::filesystem::path base = home ? std::filesystem::path(home) : repo_root;
    return base / "Library" / "Application Support" / "draxul" / "megacity.sqlite3";
#else
    return repo_root / "db" / "megacity.sqlite3";
#endif
}

} // namespace

MegaCityHost::MegaCityHost()
    : input_(std::make_unique<CityInputState>())
{
}

MegaCityHost::~MegaCityHost()
{
    if (grid_thread_.joinable())
        grid_thread_.join();
}

void MegaCityHost::refresh_sign_text_service()
{
    sign_label_atlas_.reset();
    if (sign_font_path_.empty())
    {
        sign_text_service_.reset();
        return;
    }

    if (!sign_text_service_)
        sign_text_service_ = std::make_unique<TextService>();
    else
        sign_text_service_->shutdown();

    TextServiceConfig sign_text_config;
    sign_text_config.font_path = sign_font_path_;
    sign_text_config.enable_ligatures = false;
    if (sign_text_service_->initialize(
            sign_text_config,
            std::max(renderer_config_.sign_label_point_size, 1.0f),
            display_ppi_))
    {
        DRAXUL_LOG_INFO(LogCategory::App, "MegaCityHost: sign label text service initialized");
    }
    else
    {
        sign_text_service_.reset();
        DRAXUL_LOG_WARN(LogCategory::App,
            "MegaCityHost: sign label text service unavailable; rooftop labels disabled");
    }
}

bool MegaCityHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    callbacks_ = &callbacks;
    config_document_ = context.config_document;
    renderer_defaults_ = config_document_
        ? load_megacity_code_defaults(*config_document_)
        : MegaCityCodeConfig{};
    renderer_config_ = config_document_
        ? load_megacity_code_config(*config_document_, renderer_defaults_)
        : renderer_defaults_;
    pending_renderer_config_ = renderer_config_;
    show_ui_panels_ = renderer_config_.show_ui_panels;
    restore_camera_after_initial_build_ = renderer_config_.camera_state_valid;

    // Enable ImGui layout persistence alongside config.toml
    {
        std::filesystem::path ini_path = ConfigDocument::default_path().parent_path() / "megacity_imgui.ini";
        imgui_ini_path_ = ini_path.string();
        if (ImGui::GetCurrentContext() != nullptr && std::filesystem::exists(ini_path))
            ImGui::LoadIniSettingsFromDisk(imgui_ini_path_.c_str());
    }
    sign_font_path_ = context.text_service->primary_font_path();
    display_ppi_ = context.display_ppi;
    viewport_ = context.initial_viewport;
    pixel_w_ = viewport_.pixel_size.x > 0 ? viewport_.pixel_size.x : 800;
    pixel_h_ = viewport_.pixel_size.y > 0 ? viewport_.pixel_size.y : 600;

    world_ = std::make_unique<SceneWorld>();
    camera_ = std::make_unique<IsometricCamera>();
    camera_->set_viewport(pixel_w_, pixel_h_);
    camera_->frame_world_bounds(-2.5f, 2.5f, -2.5f, 2.5f);
    scene_pass_ = std::make_shared<IsometricScenePass>(1, 1, world_->tile_size());
    refresh_sign_text_service();

    running_ = true;
    city_db_reconciled_ = false;
    world_rebuild_pending_ = false;
    city_bounds_valid_ = false;
    last_activity_time_ = std::chrono::steady_clock::now();
    last_pump_time_ = last_activity_time_;
    const std::filesystem::path city_db_path = megacity_db_path();
    if (!city_db_.open(city_db_path))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "MegaCityHost: failed to open city DB at %s: %s",
            city_db_path.string().c_str(), city_db_.last_error().c_str());
    }
    refresh_available_modules();
    scanner_.start(DRAXUL_REPO_ROOT);
    mark_scene_dirty();

    DRAXUL_LOG_INFO(LogCategory::App, "MegaCityHost initialized (%dx%d), scanning %s, city DB %s",
        pixel_w_, pixel_h_, DRAXUL_REPO_ROOT, city_db_path.string().c_str());
    return true;
}

void MegaCityHost::mark_scene_dirty()
{
    scene_dirty_ = true;
    last_activity_time_ = std::chrono::steady_clock::now();
    if (callbacks_)
        callbacks_->request_frame();
}

void MegaCityHost::mark_world_rebuild_pending()
{
    world_rebuild_pending_ = true;
    last_activity_time_ = std::chrono::steady_clock::now();
    if (callbacks_)
        callbacks_->request_frame();
}

void MegaCityHost::refresh_available_modules()
{
    available_modules_.clear();
    if (city_db_.is_open())
        available_modules_ = city_db_.list_modules();
}

void MegaCityHost::sync_camera_state_to_configs()
{
    if (!camera_)
        return;

    const IsometricCameraState state = camera_->state();
    auto write_state = [&](MegaCityCodeConfig& config) {
        config.camera_state_valid = true;
        config.camera_target = glm::vec2(state.target.x, state.target.z);
        config.camera_yaw = state.yaw;
        config.camera_pitch = state.pitch;
        config.camera_orbit_radius = state.orbit_radius;
        config.camera_zoom_half_height = state.zoom_half_height;
    };

    write_state(renderer_config_);
    write_state(pending_renderer_config_);
}

void MegaCityHost::reset_camera_to_default_frame()
{
    if (!camera_)
        return;

    if (city_bounds_valid_)
        camera_->frame_world_bounds(city_min_x_, city_max_x_, city_min_z_, city_max_z_);
    else
        camera_->frame_world_bounds(-2.5f, 2.5f, -2.5f, 2.5f);

    restore_camera_after_initial_build_ = false;
    sync_camera_state_to_configs();
    mark_scene_dirty();
}

void MegaCityHost::on_key(const KeyEvent& event)
{
    // F1 toggles UI panels (press only, not release)
    if (event.pressed
        && (event.scancode == SDL_SCANCODE_F1 || event.keycode == SDLK_F1))
    {
        dispatch_action("toggle_ui_panels");
        return;
    }

    if (input_->on_key(event))
    {
        last_pump_time_ = std::chrono::steady_clock::now();
        mark_scene_dirty();
    }
}

void MegaCityHost::on_mouse_move(const MouseMoveEvent& event)
{
    if (!camera_)
        return;

    if (input_->on_mouse_move(event, *camera_))
    {
        last_activity_time_ = std::chrono::steady_clock::now();
        if (callbacks_)
            callbacks_->request_frame();
    }
}

void MegaCityHost::on_mouse_button(const MouseButtonEvent& event)
{
    input_->on_mouse_button(event);
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

    if (!show_ui_panels_)
        return;

    if (scene_pass_)
        scene_pass_->render_gbuffer_debug_ui();

    // City map overview panel
    {
        std::shared_ptr<const CityGrid> grid;
        {
            std::lock_guard<std::mutex> lock(grid_mutex_);
            grid = city_grid_;
        }
        render_city_map_panel(grid, grid_build_in_progress_.load());
    }

    MegacityRendererControls renderer_controls{
        .config = pending_renderer_config_,
        .defaults = renderer_defaults_,
        .available_modules = available_modules_,
        .rebuild_pending = world_rebuild_pending_
            || requires_world_rebuild(renderer_config_, pending_renderer_config_),
    };
    if (render_treesitter_panel(
            pixel_w_, pixel_h_, scanner_.snapshot(), semantic_model_.get(), &renderer_controls))
    {
        if (renderer_controls.reset_camera_requested)
            reset_camera_to_default_frame();

        const MegaCityCodeConfig previous_pending = pending_renderer_config_;
        pending_renderer_config_ = renderer_controls.config;
        renderer_defaults_ = renderer_controls.defaults;
        const bool pending_changed = previous_pending != pending_renderer_config_;
        const bool world_rebuild_needed = requires_world_rebuild(renderer_config_, pending_renderer_config_);

        if (world_rebuild_needed)
        {
            if (pending_changed)
                mark_world_rebuild_pending();
        }
        else if (pending_changed)
        {
            renderer_config_ = pending_renderer_config_;
            mark_scene_dirty();
        }

        const bool auto_rebuild_requested = renderer_controls.committed_edit
            && pending_renderer_config_.auto_rebuild
            && world_rebuild_needed;
        if (renderer_controls.rebuild_requested || auto_rebuild_requested)
        {
            renderer_config_ = pending_renderer_config_;
            if (renderer_controls.rebuild_requested && config_document_)
            {
                store_megacity_code_config(*config_document_, pending_renderer_config_, renderer_defaults_);
                config_document_->save();
            }
            refresh_sign_text_service();
            if (city_db_reconciled_)
                rebuild_semantic_city();
            else
                mark_world_rebuild_pending();
        }

        if (renderer_controls.set_defaults_requested && config_document_)
        {
            store_megacity_code_config(*config_document_, pending_renderer_config_, renderer_defaults_);
            config_document_->save();
        }
    }
}

void MegaCityHost::attach_3d_renderer(I3DRenderer& renderer)
{
    renderer_3d_ = &renderer;
    renderer_3d_->register_render_pass(scene_pass_);
    renderer_3d_->set_3d_viewport(viewport_.pixel_pos.x, viewport_.pixel_pos.y, pixel_w_, pixel_h_);

    if (scene_pass_ && camera_ && world_)
    {
        auto result = build_scene_snapshot(
            *camera_,
            *world_,
            renderer_config_,
            sign_label_atlas_,
            tree_bark_mesh_,
            tree_leaf_mesh_);
        world_span_ = result.world_span;
        scene_pass_->set_scene(std::move(result.snapshot));
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
    if (grid_thread_.joinable())
        grid_thread_.join();
    city_grid_.reset();

    // Save ImGui layout state
    if (ImGui::GetCurrentContext() != nullptr && !imgui_ini_path_.empty())
        ImGui::SaveIniSettingsToDisk(imgui_ini_path_.c_str());

    pending_renderer_config_.show_ui_panels = show_ui_panels_;
    if (config_document_)
        store_megacity_code_config(*config_document_, pending_renderer_config_, renderer_defaults_);
    scanner_.stop();
    city_db_.close();
    if (sign_text_service_)
    {
        sign_text_service_->shutdown();
        sign_text_service_.reset();
    }
    sign_label_atlas_.reset();
    semantic_model_.reset();
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

void MegaCityHost::rebuild_semantic_city()
{
    if (!world_ || !camera_)
        return;

    const bool had_existing_city = semantic_model_ && !semantic_model_->empty();
    refresh_available_modules();

    auto result = build_city(
        *world_, city_db_, sign_text_service_.get(),
        available_modules_, renderer_config_, sign_label_revision_);
    tree_bark_mesh_ = result.tree_bark_mesh;
    tree_leaf_mesh_ = result.tree_leaf_mesh;

    // Apply city bounds.
    city_bounds_valid_ = result.city_bounds_valid;
    if (city_bounds_valid_)
    {
        city_min_x_ = result.min_x;
        city_max_x_ = result.max_x;
        city_min_z_ = result.min_z;
        city_max_z_ = result.max_z;
    }

    // Apply default point light if the builder computed one.
    if (result.computed_default_light)
    {
        auto set_default_light = [&](MegaCityCodeConfig& config) {
            config.point_light_position_valid = true;
            config.point_light_position = glm::vec3(
                result.default_light_x, result.default_light_y, result.default_light_z);
            config.point_light_radius = result.default_light_radius;
        };
        set_default_light(renderer_config_);
        set_default_light(pending_renderer_config_);
    }

    sign_label_atlas_ = std::move(result.sign_label_atlas);
    semantic_model_ = std::move(result.semantic_model);

    // Camera framing for first build or empty city.
    if (!had_existing_city)
    {
        if (restore_camera_after_initial_build_ && renderer_config_.camera_state_valid)
        {
            if (!city_bounds_valid_)
                camera_->frame_world_bounds(-2.5f, 2.5f, -2.5f, 2.5f);
            else
                camera_->frame_world_bounds(city_min_x_, city_max_x_, city_min_z_, city_max_z_);
            camera_->apply_state(IsometricCameraState{
                .target = { renderer_config_.camera_target.x, 0.0f, renderer_config_.camera_target.y },
                .yaw = renderer_config_.camera_yaw,
                .pitch = renderer_config_.camera_pitch,
                .orbit_radius = renderer_config_.camera_orbit_radius,
                .zoom_half_height = renderer_config_.camera_zoom_half_height,
            });
        }
        else if (!city_bounds_valid_)
            camera_->frame_world_bounds(-2.5f, 2.5f, -2.5f, 2.5f);
        else
            camera_->frame_world_bounds(city_min_x_, city_max_x_, city_min_z_, city_max_z_);
    }
    restore_camera_after_initial_build_ = false;
    sync_camera_state_to_configs();

    world_rebuild_pending_ = false;
    mark_scene_dirty();

    if (result.layout && semantic_model_)
        launch_grid_build(*result.layout, *semantic_model_);
}

void MegaCityHost::launch_grid_build(const SemanticMegacityLayout& layout, const SemanticMegacityModel& model)
{
    // Wait for any previous grid build to finish.
    if (grid_thread_.joinable())
        grid_thread_.join();

    if (layout.empty())
    {
        std::lock_guard<std::mutex> lock(grid_mutex_);
        city_grid_.reset();
        return;
    }

    // Copy the layout and config so the thread owns its data.
    auto layout_copy = std::make_shared<SemanticMegacityLayout>(layout);
    auto model_copy = std::make_shared<SemanticMegacityModel>(model);
    const MegaCityCodeConfig config = renderer_config_;

    grid_build_in_progress_ = true;
    grid_thread_ = std::thread([this, layout_copy, model_copy, config]() {
        auto grid = std::make_shared<CityGrid>(build_city_grid(*layout_copy, *model_copy, config));

        DRAXUL_LOG_DEBUG(LogCategory::App,
            "MegaCityHost: city grid built: %dx%d cells (%.1f x %.1f world units)",
            grid->cols, grid->rows,
            grid->cols * grid->cell_size, grid->rows * grid->cell_size);

        {
            std::lock_guard<std::mutex> lock(grid_mutex_);
            city_grid_ = std::move(grid);
        }
        grid_build_in_progress_ = false;
    });
}

void MegaCityHost::pump()
{
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last_pump_time_).count();
    bool camera_changed = false;

    if (camera_)
    {
        if (input_->movement_active())
        {
            const CameraMovement m = input_->movement();
            const float pan_distance = dt * kMovementSpeedFractionPerSecond * world_span_;
            if (pan_distance > 0.0f && glm::dot(m.pan_input, m.pan_input) > 0.0f)
            {
                const glm::vec2 right = camera_->planar_right_vector();
                const glm::vec2 up = camera_->planar_up_vector();
                const glm::vec2 pan = glm::normalize(m.pan_input.x * right + m.pan_input.y * up);
                camera_->translate_target(pan.x * pan_distance, pan.y * pan_distance);
                camera_changed = true;
            }
            if (m.orbit != 0.0f && dt > 0.0f)
            {
                camera_->orbit_target(m.orbit * dt * kOrbitSpeedRadiansPerSecond);
                camera_changed = true;
            }
            if (m.zoom != 0.0f && dt > 0.0f)
            {
                camera_->zoom_by(m.zoom * dt * kZoomSpeedPerSecond);
                camera_changed = true;
            }
            if (m.pitch != 0.0f && dt > 0.0f)
            {
                camera_->adjust_pitch(m.pitch * dt * kPitchSpeedRadiansPerSecond);
                camera_changed = true;
            }
        }

        if (input_->apply_drag_smoothing(dt, *camera_))
            camera_changed = true;
    }

    if (camera_changed)
    {
        sync_camera_state_to_configs();
        scene_dirty_ = true;
        last_activity_time_ = now;
        if (callbacks_)
            callbacks_->request_frame();
    }

    last_pump_time_ = now;

    if (!city_db_reconciled_ && city_db_.is_open())
    {
        if (const auto snapshot = scanner_.snapshot(); snapshot && snapshot->complete)
        {
            if (city_db_.reconcile_snapshot(*snapshot))
            {
                city_db_reconciled_ = true;
                refresh_available_modules();
                rebuild_semantic_city();
                const auto& stats = city_db_.stats();
                DRAXUL_LOG_INFO(LogCategory::App,
                    "MegaCityHost: reconciled Tree-sitter snapshot into %s (%zu files, %zu symbols, %zu entities)",
                    city_db_.path().string().c_str(),
                    stats.file_count,
                    stats.symbol_count,
                    stats.city_entity_count);
            }
            else
            {
                DRAXUL_LOG_WARN(LogCategory::App,
                    "MegaCityHost: city DB reconcile failed for %s: %s",
                    city_db_.path().string().c_str(),
                    city_db_.last_error().c_str());
            }
        }
    }

    if (scene_dirty_ && scene_pass_ && camera_ && world_)
    {
        auto result = build_scene_snapshot(
            *camera_,
            *world_,
            renderer_config_,
            sign_label_atlas_,
            tree_bark_mesh_,
            tree_leaf_mesh_);
        world_span_ = result.world_span;
        scene_pass_->set_scene(std::move(result.snapshot));
        scene_dirty_ = false;
    }
    if (running_ && continuous_refresh_enabled_ && callbacks_)
        callbacks_->request_frame();
}

std::optional<std::chrono::steady_clock::time_point> MegaCityHost::next_deadline() const
{
    if (!running_)
        return std::nullopt;
    if (!continuous_refresh_enabled_ && !input_->movement_active() && !input_->drag_smoothing_active())
        return std::nullopt;
    if (input_->drag_smoothing_active())
        return std::chrono::steady_clock::now() + kDragSmoothingTick;
    return std::chrono::steady_clock::now() + kMovementTick;
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
    if (action == "toggle_ui_panels")
    {
        show_ui_panels_ = !show_ui_panels_;
        if (callbacks_)
            callbacks_->request_frame();
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
