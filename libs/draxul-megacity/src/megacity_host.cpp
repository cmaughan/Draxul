#include "building_tooltip.h"
#include "city_builder.h"
#include "city_input_state.h"
#include "city_picking.h"
#include "isometric_camera.h"
#include "isometric_scene_pass.h"
#include "scene_snapshot_builder.h"
#include "scene_world.h"
#include "semantic_city_layout.h"
#include "sign_label_atlas.h"
#include "ui_city_map_panel.h"
#include "ui_treesitter_panel.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <draxul/log.h>
#include <draxul/megacity_host.h>
#include <draxul/text_service.h>
#include <filesystem>
#include <imgui.h>
#include <unordered_set>

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
    config.dependency_route_layer_step = 0.0f;
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

std::string building_identity_key(std::string_view module_path, std::string_view qualified_name)
{
    std::string key;
    key.reserve(module_path.size() + qualified_name.size() + 1);
    key.append(module_path);
    key.push_back('|');
    key.append(qualified_name);
    return key;
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
    {
        std::lock_guard<std::mutex> lock(route_mutex_);
        route_worker_stop_ = true;
        pending_route_request_.reset();
    }
    route_cv_.notify_all();
    if (route_thread_.joinable())
        route_thread_.join();
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

    // Tooltip text service at configurable point size.
    tooltip_text_service_.reset();
    if (!sign_font_path_.empty())
    {
        tooltip_text_service_ = std::make_unique<TextService>();
        TextServiceConfig tooltip_config;
        tooltip_config.font_path = sign_font_path_;
        tooltip_config.enable_ligatures = false;
        const float tooltip_pt = std::max(renderer_config_.tooltip_point_size, 4.0f);
        if (!tooltip_text_service_->initialize(tooltip_config, tooltip_pt, display_ppi_))
            tooltip_text_service_.reset();
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
    route_worker_stop_ = false;
    route_thread_ = std::thread([this]() { route_worker_loop(); });
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

void MegaCityHost::route_worker_loop()
{
    while (true)
    {
        RouteBuildRequest request;
        {
            std::unique_lock<std::mutex> lock(route_mutex_);
            route_cv_.wait(lock, [&]() {
                return route_worker_stop_ || pending_route_request_.has_value();
            });
            if (route_worker_stop_)
                break;
            request = *pending_route_request_;
            pending_route_request_.reset();
        }

        std::shared_ptr<CityGrid> routed_grid;
        if (request.layout && request.model && request.grid && !request.focus_qualified_name.empty())
        {
            routed_grid = std::make_shared<CityGrid>(*request.grid);
            routed_grid->routes = build_city_routes_for_selection(
                *request.layout,
                *request.model,
                *request.grid,
                building_identity_key(request.focus_module_path, request.focus_qualified_name));
        }

        {
            std::lock_guard<std::mutex> lock(route_mutex_);
            if (request.generation == route_request_generation_)
                completed_route_result_ = RouteBuildResult{ request.generation, std::move(routed_grid) };
            if (!pending_route_request_.has_value())
                route_build_in_progress_ = false;
        }
        if (callbacks_)
            callbacks_->request_frame();
    }
}

void MegaCityHost::request_routes_for_focus(std::string focus_module_path, std::string focus_qualified_name)
{
    if (focus_qualified_name.empty() || !semantic_layout_ || !semantic_model_)
        return;

    std::shared_ptr<const CityGrid> grid;
    {
        std::lock_guard<std::mutex> lock(grid_mutex_);
        grid = city_grid_;
    }
    if (!grid)
        return;

    {
        std::lock_guard<std::mutex> lock(route_mutex_);
        pending_route_request_ = RouteBuildRequest{
            ++route_request_generation_,
            std::move(focus_module_path),
            std::move(focus_qualified_name),
            semantic_layout_,
            semantic_model_,
            std::move(grid),
        };
        completed_route_result_.reset();
        route_build_in_progress_ = true;
    }
    route_cv_.notify_one();
}

void MegaCityHost::clear_active_routes(bool request_frame)
{
    {
        std::lock_guard<std::mutex> lock(grid_mutex_);
        if (city_grid_)
        {
            auto cleared_grid = std::make_shared<CityGrid>(*city_grid_);
            cleared_grid->routes.clear();
            city_grid_ = std::move(cleared_grid);
        }
    }

    if (world_)
        world_->clear_route_segments();
    scene_dirty_ = true;
    last_activity_time_ = std::chrono::steady_clock::now();
    if (request_frame && callbacks_)
        callbacks_->request_frame();
}

void MegaCityHost::consume_completed_routes()
{
    std::optional<RouteBuildResult> result;
    {
        std::lock_guard<std::mutex> lock(route_mutex_);
        if (!completed_route_result_.has_value())
            return;
        result = std::move(completed_route_result_);
        completed_route_result_.reset();
    }
    if (!result.has_value())
        return;

    if (result->grid)
    {
        std::lock_guard<std::mutex> lock(grid_mutex_);
        city_grid_ = result->grid;
    }

    if (world_)
    {
        world_->clear_route_segments();
        if (result->grid)
            emit_route_entities(*world_, result->grid->routes, renderer_config_);
    }
    scene_dirty_ = true;
    last_activity_time_ = std::chrono::steady_clock::now();
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

    // Escape clears building selection
    if (event.pressed
        && (event.scancode == SDL_SCANCODE_ESCAPE || event.keycode == SDLK_ESCAPE)
        && !selected_building_name_.empty())
    {
        clear_selection();
        return;
    }

    if ((event.scancode == SDL_SCANCODE_SPACE || event.keycode == SDLK_SPACE)
        && !selected_building_name_.empty())
    {
        if (hidden_hover_active_ != event.pressed)
        {
            hidden_hover_active_ = event.pressed;
            if (callbacks_)
                callbacks_->request_frame();
        }
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

    // Reset hover tooltip whenever the mouse moves.
    const bool had_tooltip = hover_tooltip_visible_;
    hover_mouse_pos_ = event.pos;
    hover_start_time_ = std::chrono::steady_clock::now();
    hover_tooltip_visible_ = false;
    hover_building_name_.clear();
    if (had_tooltip && scene_pass_)
    {
        scene_pass_->scene().tooltip.visible = false;
        mark_scene_dirty();
    }

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
    if (!event.pressed && callbacks_)
        callbacks_->request_frame();
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
            if (world_ && previous_pending.dependency_route_layer_step != renderer_config_.dependency_route_layer_step)
            {
                std::shared_ptr<const CityGrid> grid;
                {
                    std::lock_guard<std::mutex> lock(grid_mutex_);
                    grid = city_grid_;
                }
                if (grid && !grid->routes.empty())
                {
                    world_->clear_route_segments();
                    emit_route_entities(*world_, grid->routes, renderer_config_);
                }
            }
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
        if (!selected_building_name_.empty())
            apply_selection_opacity();
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
    {
        std::lock_guard<std::mutex> lock(route_mutex_);
        route_worker_stop_ = true;
        pending_route_request_.reset();
        completed_route_result_.reset();
    }
    route_cv_.notify_all();
    if (route_thread_.joinable())
        route_thread_.join();
    city_grid_.reset();
    semantic_layout_.reset();

    // Save ImGui layout state
    if (ImGui::GetCurrentContext() != nullptr && !imgui_ini_path_.empty())
        ImGui::SaveIniSettingsToDisk(imgui_ini_path_.c_str());

    pending_renderer_config_.show_ui_panels = show_ui_panels_;
    if (config_document_)
        store_megacity_code_config(*config_document_, pending_renderer_config_, renderer_defaults_);
    scanner_.stop();
    city_db_.close();
    if (tooltip_text_service_)
    {
        tooltip_text_service_->shutdown();
        tooltip_text_service_.reset();
    }
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
    semantic_layout_ = result.layout
        ? std::make_shared<SemanticMegacityLayout>(*result.layout)
        : nullptr;
    clear_active_routes(false);
    {
        std::lock_guard<std::mutex> lock(route_mutex_);
        ++route_request_generation_;
        pending_route_request_.reset();
        completed_route_result_.reset();
        route_build_in_progress_ = false;
    }

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

    if (semantic_layout_ && semantic_model_)
        launch_grid_build(*semantic_layout_, *semantic_model_);
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
    const MegaCityCodeConfig config = renderer_config_;

    grid_build_in_progress_ = true;
    grid_thread_ = std::thread([this, layout_copy, config]() {
        auto grid = std::make_shared<CityGrid>(build_city_grid(*layout_copy, config));

        DRAXUL_LOG_DEBUG(LogCategory::App,
            "MegaCityHost: city grid built: %dx%d cells (%.1f x %.1f world units)",
            grid->cols, grid->rows,
            grid->cols * grid->cell_size, grid->rows * grid->cell_size);

        {
            std::lock_guard<std::mutex> lock(grid_mutex_);
            city_grid_ = std::move(grid);
        }
        grid_build_in_progress_ = false;
        if (callbacks_)
            callbacks_->request_frame();
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

        if (auto click = input_->consume_click())
            handle_click(*click);
    }

    if (camera_changed)
    {
        sync_camera_state_to_configs();
        scene_dirty_ = true;
        last_activity_time_ = now;
        if (callbacks_)
            callbacks_->request_frame();
    }

    bool selection_alpha_changed = false;
    if (!selected_building_name_.empty())
        selection_alpha_changed = update_hidden_hover_blend(dt, now);

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

    consume_completed_routes();

    if (!selected_building_name_.empty() && semantic_layout_ && semantic_model_)
    {
        std::shared_ptr<const CityGrid> grid;
        {
            std::lock_guard<std::mutex> lock(grid_mutex_);
            grid = city_grid_;
        }
        if (grid && grid->routes.empty() && !route_build_in_progress_.load())
            request_routes_for_focus(selected_building_module_path_, selected_building_name_);
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
        if (!selected_building_name_.empty())
            apply_selection_opacity();
        scene_dirty_ = false;
    }
    else if (selection_alpha_changed)
    {
        apply_selection_opacity();
    }

    // Hover tooltip: show after mouse is still for >1s over a building.
    if (!hover_tooltip_visible_ && hover_mouse_pos_.x >= 0
        && camera_ && semantic_model_ && scene_pass_ && tooltip_text_service_)
    {
        const auto hover_elapsed = std::chrono::duration<float>(now - hover_start_time_).count();
        if (hover_elapsed >= 1.0f)
        {
            const glm::ivec2 local_pos = hover_mouse_pos_ - viewport_.pixel_pos;
            if (local_pos.x >= 0 && local_pos.y >= 0 && local_pos.x < pixel_w_ && local_pos.y < pixel_h_)
            {
                auto hit = pick_building(local_pos, pixel_w_, pixel_h_, *camera_, *semantic_model_);
                if (hit)
                {
                    // Look up full building data.
                    for (const auto& mod : semantic_model_->modules)
                    {
                        if (mod.module_path != hit->module_path)
                            continue;
                        for (const auto& bldg : mod.buildings)
                        {
                            if (bldg.qualified_name != hit->qualified_name)
                                continue;

                            BuildingTooltipData tooltip_data;
                            tooltip_data.name = bldg.display_name;
                            tooltip_data.module_path = bldg.module_path;
                            tooltip_data.function_count = bldg.function_count;
                            tooltip_data.field_count = bldg.base_size;

                            // Determine which function layer the ray hit.
                            if (!bldg.layers.empty() && hit->hit_y >= 0.0f)
                            {
                                float cumulative_y = 0.0f;
                                for (const auto& layer : bldg.layers)
                                {
                                    cumulative_y += layer.height;
                                    if (hit->hit_y <= cumulative_y)
                                    {
                                        if (!layer.function_name.empty())
                                            tooltip_data.hovered_function = layer.function_name;
                                        break;
                                    }
                                }
                            }

                            auto bitmap = rasterize_tooltip(*tooltip_text_service_, tooltip_data);
                            if (bitmap.valid())
                            {
                                ++tooltip_revision_;
                                auto& tooltip = scene_pass_->scene().tooltip;
                                tooltip.visible = true;
                                // Position tooltip offset from cursor, clamped to viewport.
                                const int offset_x = 16;
                                const int offset_y = 16;
                                int tx = hover_mouse_pos_.x + offset_x;
                                int ty = hover_mouse_pos_.y + offset_y;
                                if (tx + bitmap.width > viewport_.pixel_pos.x + pixel_w_)
                                    tx = hover_mouse_pos_.x - bitmap.width - offset_x;
                                if (ty + bitmap.height > viewport_.pixel_pos.y + pixel_h_)
                                    ty = hover_mouse_pos_.y - bitmap.height - offset_y;
                                tooltip.screen_pos = glm::vec2(
                                    static_cast<float>(tx), static_cast<float>(ty));
                                tooltip.width = bitmap.width;
                                tooltip.height = bitmap.height;
                                tooltip.rgba = std::move(bitmap.rgba);
                                tooltip.revision = tooltip_revision_;
                                hover_tooltip_visible_ = true;
                                hover_building_name_ = hit->qualified_name;
                                if (callbacks_)
                                    callbacks_->request_frame();
                            }
                            break;
                        }
                        break;
                    }
                }
            }
        }
    }

    if (!continuous_refresh_enabled_ && selection_alpha_changed && callbacks_)
        callbacks_->request_frame();
    if (running_ && continuous_refresh_enabled_ && callbacks_)
        callbacks_->request_frame();
}

std::optional<std::chrono::steady_clock::time_point> MegaCityHost::next_deadline() const
{
    if (!running_)
        return std::nullopt;

    // Schedule a wake-up for the hover tooltip if the mouse is resting.
    if (!hover_tooltip_visible_ && hover_mouse_pos_.x >= 0)
    {
        const auto tooltip_deadline = hover_start_time_ + std::chrono::milliseconds(1050);
        if (tooltip_deadline > std::chrono::steady_clock::now())
            return tooltip_deadline;
    }

    if (!continuous_refresh_enabled_ && !input_->movement_active() && !input_->drag_smoothing_active()
        && hidden_hover_blend_ <= 1e-3f)
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

void MegaCityHost::handle_click(const glm::ivec2& screen_pos)
{
    if (!camera_ || !semantic_model_ || !scene_pass_)
        return;

    // Convert window-space click to viewport-local coordinates.
    const glm::ivec2 local_pos = screen_pos - viewport_.pixel_pos;
    if (local_pos.x < 0 || local_pos.y < 0 || local_pos.x >= pixel_w_ || local_pos.y >= pixel_h_)
        return;

    DRAXUL_LOG_DEBUG(LogCategory::App, "Click at (%d,%d) viewport-local (%d,%d) viewport %dx%d",
        screen_pos.x, screen_pos.y, local_pos.x, local_pos.y, pixel_w_, pixel_h_);

    auto hit = pick_building(local_pos, pixel_w_, pixel_h_, *camera_, *semantic_model_);
    if (hit)
    {
        if (hit->qualified_name == selected_building_name_
            && hit->module_path == selected_building_module_path_)
        {
            clear_selection();
            return;
        }
        clear_active_routes(false);
        selected_building_name_ = hit->qualified_name;
        selected_building_module_path_ = hit->module_path;
        DRAXUL_LOG_DEBUG(LogCategory::App, "Selected building: %s (%s)",
            selected_building_name_.c_str(),
            selected_building_module_path_.c_str());
        apply_selection_opacity();
        request_routes_for_focus(selected_building_module_path_, selected_building_name_);
    }
    else
    {
        clear_selection();
    }
}

bool MegaCityHost::update_hidden_hover_blend(float dt, std::chrono::steady_clock::time_point now)
{
    (void)now;
    if (selected_building_name_.empty())
    {
        const bool changed = hidden_hover_blend_ != 0.0f;
        hidden_hover_blend_ = 0.0f;
        return changed;
    }

    const float target_blend = hidden_hover_active_ ? 1.0f : 0.0f;
    const float duration_seconds = target_blend > hidden_hover_blend_
        ? std::max(renderer_config_.selection_hidden_hover_raise_seconds, 1e-3f)
        : std::max(renderer_config_.selection_hidden_hover_fall_seconds, 1e-3f);
    const float step = std::clamp(dt / duration_seconds, 0.0f, 1.0f);
    const float previous_blend = hidden_hover_blend_;
    hidden_hover_blend_ += (target_blend - hidden_hover_blend_) * step;
    if (std::abs(hidden_hover_blend_ - target_blend) <= 1e-3f)
        hidden_hover_blend_ = target_blend;

    return std::abs(hidden_hover_blend_ - previous_blend) > 1e-5f;
}

void MegaCityHost::apply_selection_opacity()
{
    if (!scene_pass_ || !semantic_model_ || selected_building_name_.empty())
        return;

    const std::string selected_identity
        = building_identity_key(selected_building_module_path_, selected_building_name_);
    std::unordered_set<std::string> connected;
    for (const auto& dep : semantic_model_->dependencies)
    {
        const std::string source_identity
            = building_identity_key(dep.source_module_path, dep.source_qualified_name);
        const std::string target_identity
            = building_identity_key(dep.target_module_path, dep.target_qualified_name);
        if (source_identity == selected_identity)
            connected.insert(target_identity);
        else if (target_identity == selected_identity)
            connected.insert(source_identity);
    }

    SceneSnapshot& scene = scene_pass_->scene();
    const float hidden_building_alpha = std::clamp(
        renderer_config_.selection_hidden_alpha
            + (renderer_config_.selection_hidden_hover_alpha - renderer_config_.selection_hidden_alpha)
                * hidden_hover_blend_,
        0.0f, 1.0f);
    for (auto& obj : scene.objects)
    {
        const std::string object_identity = obj.source_name.empty()
            ? std::string()
            : building_identity_key(obj.source_module_path, obj.source_name);
        const bool is_selected = !object_identity.empty() && object_identity == selected_identity;
        const bool is_connected = !object_identity.empty() && connected.count(object_identity) > 0;
        const bool is_selected_route = !obj.route_source.empty()
            && (building_identity_key(obj.route_source_module_path, obj.route_source) == selected_identity
                || building_identity_key(obj.route_target_module_path, obj.route_target) == selected_identity);

        float alpha = obj.mesh == MeshId::RoadSurface
            ? renderer_config_.selection_hidden_road_alpha
            : renderer_config_.selection_hidden_alpha;
        if (!object_identity.empty() && !is_selected && !is_connected && obj.mesh != MeshId::RoadSurface)
            alpha = hidden_building_alpha;
        if (is_connected)
            alpha = renderer_config_.selection_dependency_alpha;
        if (is_selected || is_selected_route)
            alpha = 1.0f;

        obj.color.a = std::clamp(alpha, 0.0f, 1.0f);
    }

    sort_scene_objects(scene);

    if (callbacks_)
        callbacks_->request_frame();
}

void MegaCityHost::clear_selection()
{
    if (selected_building_name_.empty())
        return;

    selected_building_name_.clear();
    selected_building_module_path_.clear();
    hidden_hover_active_ = false;
    hidden_hover_blend_ = 0.0f;

    if (scene_pass_)
    {
        SceneSnapshot& scene = scene_pass_->scene();
        for (auto& obj : scene.objects)
            obj.color.a = 1.0f;
        sort_scene_objects(scene);
    }

    if (callbacks_)
        callbacks_->request_frame();
}

std::unique_ptr<IHost> create_megacity_host()
{
    return std::make_unique<MegaCityHost>();
}

} // namespace draxul
