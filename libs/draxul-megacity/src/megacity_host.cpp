#include "building_tooltip.h"
#include "city_builder.h"
#include "city_helpers.h"
#include "city_input_state.h"
#include "city_picking.h"
#include "isometric_camera.h"
#include "isometric_scene_pass.h"
#include "lcov_coverage.h"
#include "live_city_metrics.h"
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
#include <draxul/config_document.h>
#include <draxul/imgui_host.h>
#include <draxul/log.h>
#include <draxul/megacity_host.h>
#include <draxul/perf_timing.h>
#include <draxul/sdl_imgui_input.h>
#include <draxul/text_service.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <imgui.h>
#include <unordered_map>
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
constexpr int kHoverTooltipResetDistancePixels = 4;
constexpr auto kMovementTick = std::chrono::milliseconds(16);
constexpr auto kDragSmoothingTick = std::chrono::milliseconds(8);
constexpr auto kLivePerfRefreshTick = std::chrono::milliseconds(100);

void save_merged_megacity_config(
    ConfigDocument* config_document,
    const MegaCityCodeConfig& current,
    const MegaCityCodeConfig& defaults)
{
    if (!config_document)
        return;

    ConfigDocument latest = ConfigDocument::load();
    store_megacity_code_config(latest, current, defaults);
    latest.save();
    *config_document = std::move(latest);
}

/// Returns true for overlay modes that use the live runtime perf collector (Perf, Coverage).
/// LcovCoverage uses a static import and does not need the runtime collector.
bool is_live_perf_overlay(OverlayMode mode)
{
    return mode == OverlayMode::Perf || mode == OverlayMode::Coverage;
}

/// Returns true if any overlay mode is active (including LcovCoverage).
bool is_overlay_active(OverlayMode mode)
{
    return mode != OverlayMode::None;
}

/// Check whether a file contains a given function name as a substring.
bool file_contains_function(const std::filesystem::path& path, std::string_view function_name)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        return false;
    std::string line;
    while (std::getline(ifs, line))
    {
        if (line.find(function_name) != std::string::npos)
            return true;
    }
    return false;
}

/// Given a header path, return candidate implementation file paths.
/// Covers common patterns: same-dir .cpp, include/→src/ swap, .hpp→.cpp.
std::vector<std::filesystem::path> candidate_impl_files(const std::filesystem::path& header)
{
    std::vector<std::filesystem::path> candidates;
    const auto ext = header.extension().string();
    if (ext != ".h" && ext != ".hpp" && ext != ".hxx")
        return candidates;

    const std::string cpp_ext = ".cpp";
    // Same directory, different extension.
    candidates.push_back(std::filesystem::path(header).replace_extension(cpp_ext));

    // include/.../*.h → src/*.cpp  (strip one level of include nesting)
    const std::string generic = header.generic_string();
    const auto include_pos = generic.find("/include/");
    if (include_pos != std::string::npos)
    {
        // e.g. libs/foo/include/draxul/bar.h → libs/foo/src/bar.cpp
        const std::string prefix = generic.substr(0, include_pos);
        const std::string filename = header.stem().string() + cpp_ext;
        candidates.push_back(std::filesystem::path(prefix + "/src/" + filename));
    }

    return candidates;
}

/// Find the best file to open for a given source_file_path and function_name.
/// If source is a header, looks for a matching .cpp that contains the function.
/// Returns {file_to_open, function_to_search}.
struct ImplementationTarget
{
    std::filesystem::path file;
    std::string function_name;
};

ImplementationTarget find_implementation_file(
    const std::filesystem::path& scan_root,
    std::string_view source_file_path,
    std::string_view function_name)
{
    const std::filesystem::path abs_path = scan_root / source_file_path;
    std::error_code ec;
    const std::filesystem::path canonical_src = std::filesystem::canonical(abs_path, ec);
    if (ec)
        return { abs_path, std::string(function_name) };

    // If it's not a header, just return it directly.
    const auto ext = canonical_src.extension().string();
    if (ext != ".h" && ext != ".hpp" && ext != ".hxx")
        return { canonical_src, std::string(function_name) };

    // It's a header. Try to find a .cpp that contains the function.
    const auto candidates = candidate_impl_files(canonical_src);
    std::filesystem::path best_cpp;
    for (const auto& candidate : candidates)
    {
        std::error_code ec2;
        if (!std::filesystem::exists(candidate, ec2))
            continue;

        const auto canonical_cpp = std::filesystem::canonical(candidate, ec2);
        if (ec2)
            continue;

        if (!function_name.empty() && file_contains_function(canonical_cpp, function_name))
        {
            DRAXUL_LOG_DEBUG(LogCategory::App, "Found implementation: %s contains %.*s",
                canonical_cpp.string().c_str(),
                static_cast<int>(function_name.size()), function_name.data());
            return { canonical_cpp, std::string(function_name) };
        }
    }

    // No .cpp contained the function — fall back to the header.
    return { canonical_src, std::string(function_name) };
}

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
    config.overlay_mode = OverlayMode::None;
    config.performance_heat_log_scale = 0.0f;
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

std::string exact_building_identity_key(
    std::string_view source_file_path,
    std::string_view module_path,
    std::string_view qualified_name)
{
    std::string key;
    key.reserve(source_file_path.size() + module_path.size() + qualified_name.size() + 2);
    key.append(source_file_path);
    key.push_back('|');
    key.append(module_path);
    key.push_back('|');
    key.append(qualified_name);
    return key;
}

std::unordered_set<std::string> connected_building_identities(
    const SemanticMegacityModel& model,
    std::string_view selected_source_file_path,
    std::string_view selected_module_path,
    std::string_view selected_qualified_name)
{
    const std::string selected_identity
        = exact_building_identity_key(selected_source_file_path, selected_module_path, selected_qualified_name);
    std::unordered_set<std::string> connected;
    for (const auto& dep : model.dependencies)
    {
        const std::string source_identity
            = exact_building_identity_key(dep.source_file_path, dep.source_module_path, dep.source_qualified_name);
        const std::string target_identity
            = exact_building_identity_key(dep.target_file_path, dep.target_module_path, dep.target_qualified_name);
        if (source_identity == selected_identity)
            connected.insert(target_identity);
        else if (target_identity == selected_identity)
            connected.insert(source_identity);
    }
    return connected;
}

bool is_module_context_object(const SceneObject& obj)
{
    return obj.role == SceneObject::Role::ModuleOutline
        || obj.role == SceneObject::Role::ModuleLabel;
}

bool runtime_timing_has_activity(const RuntimePerfFunctionTiming& timing)
{
    return timing.frame_fraction > 0.0f
        || timing.smoothed_frame_fraction > 0.0f
        || timing.frame_microseconds > 0
        || timing.smoothed_microseconds > 0
        || timing.call_count > 0;
}

std::string runtime_perf_function_key(
    std::string_view source_file_path,
    std::string_view owner_qualified_name,
    std::string_view function_name)
{
    std::string key;
    key.reserve(source_file_path.size() + owner_qualified_name.size() + function_name.size() + 2);
    key.append(source_file_path);
    key.push_back('\n');
    key.append(owner_qualified_name);
    key.push_back('\n');
    key.append(function_name);
    return key;
}

void clear_coverage_snapshot(RuntimePerfSnapshot& snapshot)
{
    snapshot = RuntimePerfSnapshot{};
}

void merge_runtime_perf_coverage(RuntimePerfSnapshot& coverage_snapshot, const RuntimePerfSnapshot& latest_snapshot)
{
    coverage_snapshot.generation = latest_snapshot.generation;
    coverage_snapshot.frame_index = latest_snapshot.frame_index;
    coverage_snapshot.frame_time_microseconds = latest_snapshot.frame_time_microseconds;

    std::unordered_map<std::string, size_t> indices_by_key;
    indices_by_key.reserve(coverage_snapshot.functions.size());
    for (size_t i = 0; i < coverage_snapshot.functions.size(); ++i)
    {
        const auto& function = coverage_snapshot.functions[i];
        indices_by_key.emplace(
            runtime_perf_function_key(
                function.source_file_path,
                function.owner_qualified_name,
                function.function_name),
            i);
    }

    for (const RuntimePerfFunctionTiming& timing : latest_snapshot.functions)
    {
        if (!runtime_timing_has_activity(timing))
            continue;

        const std::string key = runtime_perf_function_key(
            timing.source_file_path,
            timing.owner_qualified_name,
            timing.function_name);
        const auto existing = indices_by_key.find(key);
        if (existing == indices_by_key.end())
        {
            coverage_snapshot.functions.push_back(timing);
            indices_by_key.emplace(key, coverage_snapshot.functions.size() - 1);
        }
        else
        {
            coverage_snapshot.functions[existing->second] = timing;
        }
    }
}

const LiveCityBuildingMetric* find_building_metric(
    const LiveCityMetricsSnapshot& snapshot,
    std::string_view source_file_path,
    std::string_view module_path,
    std::string_view qualified_name)
{
    for (const auto& building : snapshot.buildings)
    {
        if (building.source_file_path == source_file_path
            && building.module_path == module_path
            && building.qualified_name == qualified_name)
        {
            return &building;
        }
    }
    return nullptr;
}

const LiveCityFunctionMetric* find_function_metric(
    const LiveCityMetricsSnapshot& snapshot,
    std::string_view source_file_path,
    std::string_view module_path,
    std::string_view qualified_name,
    std::string_view function_name,
    uint32_t layer_index)
{
    for (const auto& function : snapshot.functions)
    {
        if (function.source_file_path == source_file_path
            && function.module_path == module_path
            && function.qualified_name == qualified_name
            && function.function_name == function_name
            && function.layer_index == layer_index)
        {
            return &function;
        }
    }
    return nullptr;
}

void preserve_visible_tooltip(const IsometricScenePass* scene_pass, bool hover_tooltip_visible, SceneSnapshot& snapshot)
{
    if (!scene_pass || !hover_tooltip_visible)
        return;

    const TooltipOverlay& tooltip = scene_pass->scene().tooltip;
    if (!tooltip.valid())
        return;

    snapshot.tooltip = tooltip;
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
    PERF_MEASURE();
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
    PERF_MEASURE();
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

    // Create MegaCity's own ImGui context for isolated docking and layout.
    {
        std::filesystem::path ini_path = ConfigDocument::default_path().parent_path() / "megacity_imgui.ini";
        imgui_ini_path_ = ini_path.string();

        imgui_context_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(imgui_context_);
        ImGuiIO& mc_io = ImGui::GetIO();
        mc_io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        mc_io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
        mc_io.ConfigWindowsResizeFromEdges = true;
        mc_io.IniFilename = nullptr;
        mc_io.LogFilename = nullptr;
        ImGui::StyleColorsDark();

        if (std::filesystem::exists(ini_path))
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
    last_live_perf_refresh_time_ = last_activity_time_;
    last_live_perf_generation_ = 0;
    runtime_perf_collector().set_enabled(is_live_perf_overlay(renderer_config_.overlay_mode));
    const std::filesystem::path city_db_path = megacity_db_path();
    if (!city_db_.open(city_db_path))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "MegaCityHost: failed to open city DB at %s: %s",
            city_db_path.string().c_str(), city_db_.last_error().c_str());
    }
    if (city_db_.schema_migrated())
        restore_camera_after_initial_build_ = false;
    refresh_available_modules();
    scan_root_ = std::filesystem::canonical(DRAXUL_REPO_ROOT);
    scanner_.start(scan_root_);
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
            route_cv_.wait(lock, [this]() {
                return route_worker_stop_ || pending_route_request_.has_value();
            });
            if (route_worker_stop_)
                break;
            request = *pending_route_request_;
            pending_route_request_.reset();
        }

        PERF_MEASURE();

        std::shared_ptr<CityGrid> routed_grid;
        if (request.layout && request.model && request.grid && !request.focus_qualified_name.empty())
        {
            routed_grid = std::make_shared<CityGrid>(*request.grid);
            routed_grid->routes = build_city_routes_for_selection(
                *request.layout,
                *request.model,
                *request.grid,
                request.config,
                request.focus_source_file_path,
                request.focus_module_path,
                request.focus_qualified_name);
        }

        bool should_notify = false;
        {
            std::lock_guard<std::mutex> lock(route_mutex_);
            if (request.generation == route_request_generation_)
                completed_route_result_ = RouteBuildResult{ request.generation, std::move(routed_grid) };
            if (!pending_route_request_.has_value())
                route_build_in_progress_ = false;
            should_notify = !route_worker_stop_;
        }
        if (should_notify && callbacks_)
            callbacks_->request_frame();
    }
}

void MegaCityHost::request_routes_for_focus(
    std::string focus_source_file_path,
    std::string focus_module_path,
    std::string focus_qualified_name)
{
    PERF_MEASURE();
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
            std::move(focus_source_file_path),
            std::move(focus_module_path),
            std::move(focus_qualified_name),
            semantic_layout_,
            semantic_model_,
            std::move(grid),
            renderer_config_,
        };
        completed_route_result_.reset();
        route_build_in_progress_ = true;
    }
    route_cv_.notify_one();
}

void MegaCityHost::clear_active_routes(bool request_frame)
{
    PERF_MEASURE();
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
    PERF_MEASURE();
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
    PERF_MEASURE();
    available_modules_.clear();
    if (city_db_.is_open())
        available_modules_ = city_db_.list_modules();
}

void MegaCityHost::sync_camera_state_to_configs()
{
    PERF_MEASURE();
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
    PERF_MEASURE();
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

void MegaCityHost::on_focus_lost()
{
    input_->reset_keys();
}

void MegaCityHost::on_key(const KeyEvent& event)
{
    PERF_MEASURE();

    // Feed the MegaCity ImGui context so panels receive keyboard input.
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, (event.mod & kModCtrl) != 0);
        io.AddKeyEvent(ImGuiMod_Shift, (event.mod & kModShift) != 0);
        io.AddKeyEvent(ImGuiMod_Alt, (event.mod & kModAlt) != 0);
        io.AddKeyEvent(ImGuiMod_Super, (event.mod & kModSuper) != 0);
        const ImGuiKey key = sdl_scancode_to_imgui_key(event.scancode);
        if (key != ImGuiKey_None)
            io.AddKeyEvent(key, event.pressed);

        if (io.WantCaptureKeyboard)
        {
            if (callbacks_)
                callbacks_->request_frame();
            return;
        }
    }

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

void MegaCityHost::on_text_input(const TextInputEvent& event)
{
    PERF_MEASURE();
    if (!imgui_context_ || event.text.empty())
        return;

    ImGui::SetCurrentContext(imgui_context_);
    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharactersUTF8(event.text.c_str());

    if ((io.WantTextInput || io.WantCaptureKeyboard) && callbacks_)
        callbacks_->request_frame();
}

void MegaCityHost::on_mouse_move(const MouseMoveEvent& event)
{
    PERF_MEASURE();

    // Feed the MegaCity ImGui context so panels receive mouse position.
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        ImGui::GetIO().AddMousePosEvent(static_cast<float>(event.pos.x), static_cast<float>(event.pos.y));

        if (ImGui::GetIO().WantCaptureMouse)
        {
            if (callbacks_)
                callbacks_->request_frame();
            return;
        }
    }

    if (!camera_)
        return;

    const bool shift_held = (event.mod & kModShift) != 0;
    const bool anchor_valid = hover_anchor_pos_.x >= 0 && hover_anchor_pos_.y >= 0;
    const glm::ivec2 previous_anchor = anchor_valid ? hover_anchor_pos_ : event.pos;
    const glm::ivec2 anchor_delta = event.pos - previous_anchor;
    const int anchor_distance_sq = anchor_delta.x * anchor_delta.x + anchor_delta.y * anchor_delta.y;
    const bool significant_hover_move
        = anchor_distance_sq >= (kHoverTooltipResetDistancePixels * kHoverTooltipResetDistancePixels);

    hover_mouse_pos_ = event.pos;
    hover_shift_held_ = shift_held;

    if (!anchor_valid || significant_hover_move)
    {
        hover_anchor_pos_ = event.pos;
        if (!shift_held)
            hover_start_time_ = std::chrono::steady_clock::now();

        const bool had_tooltip = hover_tooltip_visible_;
        hover_tooltip_visible_ = false;
        hover_building_name_.clear();
        if (had_tooltip && scene_pass_)
        {
            scene_pass_->scene().tooltip.visible = false;
            mark_scene_dirty();
        }
    }

    // When shift is held, request a frame so pump() can show tooltip immediately.
    if (shift_held && callbacks_)
        callbacks_->request_frame();

    if (input_->on_mouse_move(event, *camera_))
    {
        last_activity_time_ = std::chrono::steady_clock::now();
        if (callbacks_)
            callbacks_->request_frame();
    }
}

void MegaCityHost::on_mouse_button(const MouseButtonEvent& event)
{
    PERF_MEASURE();

    // Feed the MegaCity ImGui context so panels receive mouse clicks.
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
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

        if (ImGui::GetIO().WantCaptureMouse)
        {
            if (callbacks_)
                callbacks_->request_frame();
            return;
        }
    }

    input_->on_mouse_button(event);
    if (!event.pressed && callbacks_)
        callbacks_->request_frame();
}

void MegaCityHost::on_mouse_wheel(const MouseWheelEvent& event)
{
    // Feed the MegaCity ImGui context so panels receive scroll input.
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        ImGui::GetIO().AddMouseWheelEvent(event.delta.x, event.delta.y);
    }
}

void MegaCityHost::set_imgui_font(const std::string& path, float size_pixels)
{
    if (sign_font_path_ != path)
    {
        sign_font_path_ = path;
        refresh_sign_text_service();
        mark_scene_dirty();
    }
    if (!imgui_context_)
        return;
    ImGui::SetCurrentContext(imgui_context_);
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    if (!path.empty() && size_pixels > 0.0f)
        io.Fonts->AddFontFromFileTTF(path.c_str(), size_pixels);
    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();
    if (imgui_backend_)
        imgui_backend_->rebuild_imgui_font_texture();
}

void MegaCityHost::attach_imgui_host(IImGuiHost& host)
{
    imgui_backend_ = &host;
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        host.initialize_imgui_backend();
        host.rebuild_imgui_font_texture();
    }
}

void MegaCityHost::render_host_imgui(float dt)
{
    PERF_MEASURE();
    if (!imgui_context_ || !imgui_backend_)
        return;

    ImGui::SetCurrentContext(imgui_context_);
    imgui_backend_->begin_imgui_frame();
    ImGuiIO& io = ImGui::GetIO();
    // DisplaySize must cover from screen origin to the pane's bottom-right corner.
    // ImGui_ImplMetal sets its Metal viewport to (0, 0, DisplaySize), so using the
    // pane size alone would render the ImGui at screen (0,0) instead of at the pane
    // position — causing a gap at the bottom equal to the pane's Y offset (tab bar).
    io.DisplaySize = ImVec2(
        static_cast<float>(viewport_.pixel_pos.x + pixel_w_),
        static_cast<float>(viewport_.pixel_pos.y + pixel_h_));
    io.DeltaTime = dt > 0.0f ? dt : (1.0f / 60.0f);
    ImGui::NewFrame();

    const ImGuiWindowFlags ds_flags = ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoBackground;
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(viewport_.pixel_pos.x), static_cast<float>(viewport_.pixel_pos.y)));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(pixel_w_), static_cast<float>(pixel_h_)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##dockspace_root", nullptr, ds_flags);
    ImGui::PopStyleVar(3);
    const ImGuiID dock_id = ImGui::GetID("MegaCityDock");
    ImGui::DockSpace(dock_id, ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    if (!show_ui_panels_)
    {
        ImGui::Render();
        return;
    }

    if (scene_pass_)
    {

        scene_pass_->render_gbuffer_debug_ui();
    }

    // City map overview panel
    {

        std::shared_ptr<const CityGrid> grid;
        {
            std::lock_guard<std::mutex> lock(grid_mutex_);
            grid = city_grid_;
        }
        render_city_map_panel(grid, grid_build_in_progress_.load());
    }

    std::shared_ptr<const LiveCityPerfDebugState> perf_debug;
    if (semantic_model_)
    {
        if (pending_renderer_config_.overlay_mode == OverlayMode::LcovCoverage && lcov_lookup_)
        {
            perf_debug = std::make_shared<LiveCityPerfDebugState>(
                build_lcov_city_perf_debug_state(*semantic_model_, *lcov_lookup_));
        }
        else
        {
            const RuntimePerfSnapshot perf_snapshot = runtime_perf_collector().latest_snapshot();
            const RuntimePerfSnapshot* debug_snapshot
                = pending_renderer_config_.overlay_mode == OverlayMode::Coverage
                ? &coverage_perf_snapshot_
                : &perf_snapshot;
            perf_debug = std::make_shared<LiveCityPerfDebugState>(
                build_live_city_perf_debug_state(
                    *semantic_model_,
                    debug_snapshot,
                    pending_renderer_config_.overlay_mode == OverlayMode::Coverage));
        }
    }

    MegacityRendererControls renderer_controls{
        .config = pending_renderer_config_,
        .defaults = renderer_defaults_,
        .available_modules = available_modules_,
        .perf_debug = perf_debug,
        .rebuild_pending = world_rebuild_pending_
            || requires_world_rebuild(renderer_config_, pending_renderer_config_),
    };
    if (render_treesitter_panel(
            viewport_.pixel_pos.x,
            viewport_.pixel_pos.y,
            pixel_w_,
            pixel_h_,
            scanner_.snapshot(),
            semantic_model_.get(),
            &renderer_controls))
    {
        if (renderer_controls.reset_camera_requested)
            reset_camera_to_default_frame();

        const MegaCityCodeConfig previous_pending = pending_renderer_config_;
        pending_renderer_config_ = renderer_controls.config;
        renderer_defaults_ = renderer_controls.defaults;
        auto persist_renderer_config = [&]() {
            save_merged_megacity_config(config_document_, pending_renderer_config_, renderer_defaults_);
        };
        const bool pending_changed = previous_pending != pending_renderer_config_;
        const bool coverage_mode_toggled
            = (previous_pending.overlay_mode == OverlayMode::Coverage)
            != (pending_renderer_config_.overlay_mode == OverlayMode::Coverage);
        const bool lcov_mode_toggled
            = (previous_pending.overlay_mode == OverlayMode::LcovCoverage)
            != (pending_renderer_config_.overlay_mode == OverlayMode::LcovCoverage);
        const bool world_rebuild_needed = requires_world_rebuild(renderer_config_, pending_renderer_config_);

        if (coverage_mode_toggled)
        {
            clear_coverage_snapshot(coverage_perf_snapshot_);
            last_live_perf_generation_ = 0;
        }

        if (lcov_mode_toggled && pending_renderer_config_.overlay_mode == OverlayMode::LcovCoverage)
        {
            // Load LCOV report on activation — pick the most recent of
            // db/coverage.lcov (committed by CI) and build/coverage.lcov (local).
            const std::filesystem::path repo_root(DRAXUL_REPO_ROOT);
            const std::filesystem::path db_lcov = repo_root / "db" / "coverage.lcov";
            const std::filesystem::path build_lcov = repo_root / "build" / "coverage.lcov";
            std::filesystem::path lcov_path;
            {
                std::error_code ec;
                const auto db_time = std::filesystem::last_write_time(db_lcov, ec);
                const bool db_ok = !ec;
                const auto build_time = std::filesystem::last_write_time(build_lcov, ec);
                const bool build_ok = !ec;
                if (db_ok && build_ok)
                    lcov_path = (build_time >= db_time) ? build_lcov : db_lcov;
                else if (build_ok)
                    lcov_path = build_lcov;
                else if (db_ok)
                    lcov_path = db_lcov;
            }
            const auto report = lcov_path.empty() ? LcovCoverageReport{} : load_lcov_file(lcov_path);
            if (report.total_functions > 0)
            {
                lcov_lookup_ = std::make_shared<LcovFunctionLookup>(build_lcov_lookup(report, repo_root));
                DRAXUL_LOG_DEBUG(LogCategory::App,
                    "LCOV loaded from %s: %u total functions, %u covered",
                    lcov_path.string().c_str(), report.total_functions, report.covered_functions);
            }
            else
            {
                lcov_lookup_.reset();
                DRAXUL_LOG_WARN(LogCategory::App, "No LCOV file found (checked db/ and build/)");
            }
            // Build LCOV metrics immediately
            if (semantic_model_ && lcov_lookup_)
            {
                live_metrics_ = std::make_shared<LiveCityMetricsSnapshot>(
                    build_lcov_city_metrics_snapshot(*semantic_model_, *lcov_lookup_));
                mark_scene_dirty();
            }
        }
        else if (lcov_mode_toggled)
        {
            // Leaving LCOV mode — clear lookup
            lcov_lookup_.reset();
        }

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
        if (renderer_controls.committed_edit || renderer_controls.rebuild_requested || renderer_controls.set_defaults_requested)
            persist_renderer_config();
        if (renderer_controls.rebuild_requested || auto_rebuild_requested)
        {
            renderer_config_ = pending_renderer_config_;
            refresh_sign_text_service();
            if (city_db_reconciled_)
                rebuild_semantic_city();
            else
                mark_world_rebuild_pending();
        }
    }

    ImGui::Render();
}

void MegaCityHost::shutdown()
{
    PERF_MEASURE();
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

    // Destroy pass-owned Vulkan debug textures while this ImGui backend is still alive.
    scene_pass_.reset();

    // Tear down our own ImGui context.
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        if (!imgui_ini_path_.empty())
            ImGui::SaveIniSettingsToDisk(imgui_ini_path_.c_str());
        if (imgui_backend_)
            imgui_backend_->shutdown_imgui_backend();
        ImGui::DestroyContext(imgui_context_);
        imgui_context_ = nullptr;
        imgui_backend_ = nullptr;
    }

    pending_renderer_config_.show_ui_panels = show_ui_panels_;
    save_merged_megacity_config(config_document_, pending_renderer_config_, renderer_defaults_);
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
    runtime_perf_collector().set_enabled(false);
    sign_label_atlas_.reset();
    live_metrics_.reset();
    semantic_model_.reset();
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
    PERF_MEASURE();
    viewport_ = viewport;
    pixel_w_ = viewport.pixel_size.x > 0 ? viewport.pixel_size.x : pixel_w_;
    pixel_h_ = viewport.pixel_size.y > 0 ? viewport.pixel_size.y : pixel_h_;

    if (camera_)
        camera_->set_viewport(pixel_w_, pixel_h_);

    mark_scene_dirty();
}

void MegaCityHost::rebuild_semantic_city()
{
    PERF_MEASURE();
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
    live_metrics_ = std::move(result.live_metrics);
    last_live_perf_generation_ = 0;
    semantic_model_ = std::move(result.semantic_model);

    // If LCOV overlay is active, (re)load coverage data now that the model is available
    if (renderer_config_.overlay_mode == OverlayMode::LcovCoverage && semantic_model_)
    {
        if (!lcov_lookup_)
        {
            const std::filesystem::path repo_root(DRAXUL_REPO_ROOT);
            const std::filesystem::path lcov_path = repo_root / "build" / "coverage.lcov";
            const auto report = load_lcov_file(lcov_path);
            if (report.total_functions > 0)
            {
                lcov_lookup_ = std::make_shared<LcovFunctionLookup>(build_lcov_lookup(report, repo_root));
                DRAXUL_LOG_DEBUG(LogCategory::App,
                    "LCOV loaded on world rebuild: %u total functions, %u covered",
                    report.total_functions, report.covered_functions);
            }
        }
        if (lcov_lookup_)
        {
            live_metrics_ = std::make_shared<LiveCityMetricsSnapshot>(
                build_lcov_city_metrics_snapshot(*semantic_model_, *lcov_lookup_));
        }
    }

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
            camera_->reframe_world_bounds(-2.5f, 2.5f, -2.5f, 2.5f);
        else
            camera_->reframe_world_bounds(city_min_x_, city_max_x_, city_min_z_, city_max_z_);
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
    PERF_MEASURE();
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
        PERF_MEASURE();
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
    PERF_MEASURE();
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last_pump_time_).count();
    last_imgui_delta_seconds_ = dt;
    runtime_perf_collector().set_enabled(is_live_perf_overlay(renderer_config_.overlay_mode));
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

        if (auto dbl = input_->consume_double_click())
            handle_double_click(*dbl);
        else if (auto click = input_->consume_click())
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

    if (is_live_perf_overlay(renderer_config_.overlay_mode)
        && semantic_model_
        && now - last_live_perf_refresh_time_ >= kLivePerfRefreshTick)
    {
        last_live_perf_refresh_time_ = now;
        const RuntimePerfSnapshot perf_snapshot = runtime_perf_collector().latest_snapshot();
        if (perf_snapshot.generation != last_live_perf_generation_)
        {
            const RuntimePerfSnapshot* metrics_snapshot = &perf_snapshot;
            if (renderer_config_.overlay_mode == OverlayMode::Coverage)
            {
                merge_runtime_perf_coverage(coverage_perf_snapshot_, perf_snapshot);
                metrics_snapshot = &coverage_perf_snapshot_;
            }
            live_metrics_ = std::make_shared<LiveCityMetricsSnapshot>(
                build_live_city_metrics_snapshot(
                    *semantic_model_,
                    metrics_snapshot,
                    renderer_config_.overlay_mode == OverlayMode::Coverage));
            last_live_perf_generation_ = perf_snapshot.generation;
            mark_scene_dirty();
        }
    }

    if (!selected_building_name_.empty() && semantic_layout_ && semantic_model_)
    {
        std::shared_ptr<const CityGrid> grid;
        {
            std::lock_guard<std::mutex> lock(grid_mutex_);
            grid = city_grid_;
        }
        if (grid && grid->routes.empty() && !route_build_in_progress_.load())
            request_routes_for_focus(
                selected_building_source_file_,
                selected_building_module_path_,
                selected_building_name_);
    }

    if (scene_dirty_ && scene_pass_ && camera_ && world_)
    {
        auto result = build_scene_snapshot(
            *camera_,
            *world_,
            renderer_config_,
            live_metrics_,
            sign_label_atlas_,
            tree_bark_mesh_,
            tree_leaf_mesh_);
        preserve_visible_tooltip(scene_pass_.get(), hover_tooltip_visible_, result.snapshot);
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

    // Hover tooltip: show after mouse is still for >1s over a building,
    // or immediately when shift is held.
    if (!hover_tooltip_visible_ && hover_mouse_pos_.x >= 0
        && camera_ && semantic_model_ && semantic_layout_ && scene_pass_ && tooltip_text_service_)
    {
        const auto hover_elapsed = std::chrono::duration<float>(now - hover_start_time_).count();
        const bool tooltip_ready = hover_shift_held_ || hover_elapsed >= 1.0f;
        if (tooltip_ready)
        {
            const glm::ivec2 local_pos = hover_mouse_pos_ - viewport_.pixel_pos;
            if (local_pos.x >= 0 && local_pos.y >= 0 && local_pos.x < pixel_w_ && local_pos.y < pixel_h_)
            {
                // Helper to position and display a tooltip bitmap at the cursor.
                const auto show_tooltip_bitmap = [&](TooltipBitmap& bitmap) {
                    if (!bitmap.valid())
                        return;
                    ++tooltip_revision_;
                    auto& tooltip = scene_pass_->scene().tooltip;
                    tooltip.visible = true;
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
                    if (callbacks_)
                        callbacks_->request_frame();
                };

                std::function<bool(const std::string&, const std::string&, const std::string&)> pick_filter;
                if (!selected_building_name_.empty() && !hidden_hover_active_)
                {
                    const std::string selected_identity = exact_building_identity_key(
                        selected_building_source_file_,
                        selected_building_module_path_,
                        selected_building_name_);
                    const std::unordered_set<std::string> connected = connected_building_identities(
                        *semantic_model_,
                        selected_building_source_file_,
                        selected_building_module_path_,
                        selected_building_name_);
                    pick_filter = [selected_identity, connected](
                                      const std::string& source_file_path,
                                      const std::string& module_path,
                                      const std::string& qualified_name) {
                        const std::string identity
                            = exact_building_identity_key(source_file_path, module_path, qualified_name);
                        return identity == selected_identity || connected.count(identity) > 0;
                    };
                }

                auto hit = pick_building(
                    local_pos,
                    pixel_w_,
                    pixel_h_,
                    *camera_,
                    *semantic_layout_,
                    pick_filter,
                    semantic_model_.get(),
                    &renderer_config_);
                if (hit)
                {
                    // Look up full building data.
                    for (const auto& mod : semantic_model_->modules)
                    {
                        if (mod.module_path != hit->module_path)
                            continue;
                        for (const auto& bldg : mod.buildings)
                        {
                            if (bldg.qualified_name != hit->qualified_name
                                || bldg.source_file_path != hit->source_file_path)
                                continue;

                            BuildingTooltipData tooltip_data;
                            tooltip_data.name = bldg.display_name;
                            tooltip_data.module_path = bldg.module_path;
                            tooltip_data.function_count = bldg.function_count;
                            tooltip_data.field_count = bldg.base_size;
                            tooltip_data.lcov_mode = (renderer_config_.overlay_mode == OverlayMode::LcovCoverage);
                            if (live_metrics_)
                            {
                                if (const auto* building_metric = find_building_metric(
                                        *live_metrics_,
                                        bldg.source_file_path,
                                        bldg.module_path,
                                        bldg.qualified_name))
                                {
                                    tooltip_data.has_building_perf = true;
                                    tooltip_data.building_frame_fraction = building_metric->frame_fraction;
                                    tooltip_data.building_smoothed_frame_fraction = building_metric->smoothed_frame_fraction;
                                    tooltip_data.building_heat = building_metric->heat;
                                }
                            }

                            // Determine which semantic function layer the pick hit.
                            if (!bldg.layers.empty())
                            {
                                auto apply_layer_tooltip = [&](size_t layer_index) {
                                    if (layer_index >= bldg.layers.size())
                                        return false;

                                    const auto& layer = bldg.layers[layer_index];
                                    if (!layer.function_name.empty())
                                    {
                                        tooltip_data.hovered_function = layer.function_name;
                                        if (live_metrics_)
                                        {
                                            if (const auto* function_metric = find_function_metric(
                                                    *live_metrics_,
                                                    bldg.source_file_path,
                                                    bldg.module_path,
                                                    bldg.qualified_name,
                                                    layer.function_name,
                                                    static_cast<uint32_t>(layer_index)))
                                            {
                                                tooltip_data.has_function_perf = true;
                                                tooltip_data.function_frame_fraction = function_metric->frame_fraction;
                                                tooltip_data.function_smoothed_frame_fraction
                                                    = function_metric->smoothed_frame_fraction;
                                                tooltip_data.function_heat = function_metric->heat;
                                            }
                                        }
                                    }
                                    return true;
                                };

                                if (hit->has_layer_index)
                                {
                                    apply_layer_tooltip(static_cast<size_t>(hit->layer_index));
                                }
                                else if (hit->hit_y >= 0.0f)
                                {
                                    float cumulative_y = 0.0f;
                                    for (size_t layer_index = 0; layer_index < bldg.layers.size(); ++layer_index)
                                    {
                                        cumulative_y += bldg.layers[layer_index].height;
                                        if (hit->hit_y <= cumulative_y)
                                        {
                                            apply_layer_tooltip(layer_index);
                                            break;
                                        }
                                    }
                                }
                            }

                            auto bitmap = rasterize_building_tooltip(*tooltip_text_service_, tooltip_data);
                            hover_building_name_ = hit->qualified_name;
                            show_tooltip_bitmap(bitmap);
                            break;
                        }
                        break;
                    }
                }
                else
                {
                    // No building hit — try picking a park or tree.
                    const float ndc_x = 2.0f * static_cast<float>(local_pos.x) / static_cast<float>(pixel_w_) - 1.0f;
                    const float ndc_y = 1.0f - 2.0f * static_cast<float>(local_pos.y) / static_cast<float>(pixel_h_);
                    const glm::mat4 inv_vp = glm::inverse(camera_->proj_matrix() * camera_->view_matrix());
                    glm::vec4 near_h = inv_vp * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
                    glm::vec4 far_h = inv_vp * glm::vec4(ndc_x, ndc_y, 1.0f, 1.0f);
                    near_h /= near_h.w;
                    far_h /= far_h.w;
                    const glm::vec3 ray_origin(near_h);
                    const glm::vec3 ray_dir = glm::normalize(glm::vec3(far_h) - glm::vec3(near_h));

                    bool picked_park_or_tree = false;
                    for (const auto& mod : semantic_layout_->modules)
                    {
                        if (mod.park_footprint <= 0.0f)
                            continue;

                        const float park_half = mod.park_footprint * 0.5f;
                        const float park_base = building_base_elevation(renderer_config_);
                        const float park_top = park_base + renderer_config_.park_height;

                        // Check park slab AABB.
                        const glm::vec3 park_min(
                            mod.park_center.x - park_half, park_base,
                            mod.park_center.y - park_half);
                        const glm::vec3 park_max(
                            mod.park_center.x + park_half, park_top,
                            mod.park_center.y + park_half);

                        float t_min_r = -std::numeric_limits<float>::max();
                        float t_max_r = std::numeric_limits<float>::max();
                        bool park_hit = true;
                        for (int axis = 0; axis < 3; ++axis)
                        {
                            if (std::abs(ray_dir[axis]) < 1e-8f)
                            {
                                if (ray_origin[axis] < park_min[axis] || ray_origin[axis] > park_max[axis])
                                {
                                    park_hit = false;
                                    break;
                                }
                            }
                            else
                            {
                                float inv_d = 1.0f / ray_dir[axis];
                                float t1 = (park_min[axis] - ray_origin[axis]) * inv_d;
                                float t2 = (park_max[axis] - ray_origin[axis]) * inv_d;
                                if (t1 > t2)
                                    std::swap(t1, t2);
                                t_min_r = std::max(t_min_r, t1);
                                t_max_r = std::min(t_max_r, t2);
                                if (t_min_r > t_max_r)
                                {
                                    park_hit = false;
                                    break;
                                }
                            }
                        }
                        if (park_hit && t_min_r >= 0.0f)
                        {
                            BuildingTooltipData tooltip_data;
                            tooltip_data.name = mod.is_central_park ? "Central Park" : "Module Park";
                            tooltip_data.park_module = mod.module_path;
                            tooltip_data.park_quality = mod.quality;
                            tooltip_data.park_footprint = mod.park_footprint;
                            auto bitmap = rasterize_building_tooltip(*tooltip_text_service_, tooltip_data);
                            show_tooltip_bitmap(bitmap);
                            picked_park_or_tree = true;
                            break;
                        }
                    }

                    if (!picked_park_or_tree && hover_shift_held_)
                    {
                        // No park/tree hit — try picking a route segment (shift only).
                        std::shared_ptr<const CityGrid> grid;
                        {
                            std::lock_guard<std::mutex> lock(grid_mutex_);
                            grid = city_grid_;
                        }
                        if (grid && !grid->routes.empty() && std::abs(ray_dir.y) > 1e-6f)
                        {
                            // Find the nearest route segment using each route's own elevation.
                            float best_dist_sq = std::numeric_limits<float>::max();
                            const CityGrid::RoutePolyline* best_route = nullptr;
                            constexpr float kMaxPickDist = 1.0f;

                            for (const auto& route : grid->routes)
                            {
                                const float t = (route.elevation - ray_origin.y) / ray_dir.y;
                                if (t < 0.0f)
                                    continue;
                                const glm::vec2 hit_pos(
                                    ray_origin.x + t * ray_dir.x,
                                    ray_origin.z + t * ray_dir.z);

                                for (size_t i = 1; i < route.world_points.size(); ++i)
                                {
                                    const glm::vec2 a = route.world_points[i - 1];
                                    const glm::vec2 b = route.world_points[i];
                                    const glm::vec2 ab = b - a;
                                    const float seg_len_sq = glm::dot(ab, ab);
                                    float proj_t = 0.0f;
                                    if (seg_len_sq > 1e-8f)
                                        proj_t = std::clamp(
                                            glm::dot(hit_pos - a, ab) / seg_len_sq, 0.0f, 1.0f);
                                    const glm::vec2 closest = a + proj_t * ab;
                                    const float dist_sq
                                        = glm::dot(hit_pos - closest, hit_pos - closest);
                                    if (dist_sq < best_dist_sq)
                                    {
                                        best_dist_sq = dist_sq;
                                        best_route = &route;
                                    }
                                }
                            }

                            if (best_route && best_dist_sq <= kMaxPickDist * kMaxPickDist)
                            {
                                BuildingTooltipData tooltip_data;
                                tooltip_data.route_source = best_route->source_qualified_name;
                                tooltip_data.route_target = best_route->target_qualified_name;
                                tooltip_data.route_field_name = best_route->field_name;
                                tooltip_data.route_field_type = best_route->field_type_name;
                                auto bitmap = rasterize_building_tooltip(*tooltip_text_service_, tooltip_data);
                                show_tooltip_bitmap(bitmap);
                            }
                        }
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

void MegaCityHost::draw(IFrameContext& frame)
{
    if (!scene_pass_)
        return;

    if (scene_dirty_ && camera_ && world_)
    {
        auto result = build_scene_snapshot(
            *camera_,
            *world_,
            renderer_config_,
            live_metrics_,
            sign_label_atlas_,
            tree_bark_mesh_,
            tree_leaf_mesh_);
        preserve_visible_tooltip(scene_pass_.get(), hover_tooltip_visible_, result.snapshot);
        world_span_ = result.world_span;
        scene_pass_->set_scene(std::move(result.snapshot));
        if (!selected_building_name_.empty())
            apply_selection_opacity();
        scene_dirty_ = false;
    }

    RenderViewport viewport;
    viewport.x = viewport_.pixel_pos.x;
    viewport.y = viewport_.pixel_pos.y;
    viewport.width = pixel_w_;
    viewport.height = pixel_h_;
    frame.record_render_pass(*scene_pass_, viewport);

    render_host_imgui(last_imgui_delta_seconds_);
    if (imgui_context_ && imgui_backend_)
        frame.render_imgui(ImGui::GetDrawData(), imgui_context_);
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
        && hidden_hover_blend_ <= 1e-3f
        && !is_overlay_active(renderer_config_.overlay_mode))
        return std::nullopt;
    if (input_->drag_smoothing_active())
        return std::chrono::steady_clock::now() + kDragSmoothingTick;
    if (is_live_perf_overlay(renderer_config_.overlay_mode))
        return std::chrono::steady_clock::now() + kLivePerfRefreshTick;
    return std::chrono::steady_clock::now() + kMovementTick;
}

bool MegaCityHost::dispatch_action(std::string_view action)
{
    PERF_MEASURE();
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
    PERF_MEASURE();
    if (!camera_ || !semantic_model_ || !semantic_layout_ || !scene_pass_)
        return;

    // Convert window-space click to viewport-local coordinates.
    const glm::ivec2 local_pos = screen_pos - viewport_.pixel_pos;
    if (local_pos.x < 0 || local_pos.y < 0 || local_pos.x >= pixel_w_ || local_pos.y >= pixel_h_)
        return;

    DRAXUL_LOG_DEBUG(LogCategory::App, "Click at (%d,%d) viewport-local (%d,%d) viewport %dx%d",
        screen_pos.x, screen_pos.y, local_pos.x, local_pos.y, pixel_w_, pixel_h_);

    std::function<bool(const std::string&, const std::string&, const std::string&)> pick_filter;
    if (!selected_building_name_.empty() && !hidden_hover_active_)
    {
        const std::string selected_identity = exact_building_identity_key(
            selected_building_source_file_,
            selected_building_module_path_,
            selected_building_name_);
        const std::unordered_set<std::string> connected = connected_building_identities(
            *semantic_model_,
            selected_building_source_file_,
            selected_building_module_path_,
            selected_building_name_);
        pick_filter = [selected_identity, connected](
                          const std::string& source_file_path,
                          const std::string& module_path,
                          const std::string& qualified_name) {
            const std::string identity = exact_building_identity_key(source_file_path, module_path, qualified_name);
            return identity == selected_identity || connected.count(identity) > 0;
        };
    }

    auto hit = pick_building(
        local_pos,
        pixel_w_,
        pixel_h_,
        *camera_,
        *semantic_layout_,
        pick_filter,
        semantic_model_.get(),
        &renderer_config_);
    if (hit)
    {
        if (hit->qualified_name == selected_building_name_
            && hit->module_path == selected_building_module_path_
            && hit->source_file_path == selected_building_source_file_)
        {
            if (callbacks_)
                callbacks_->request_frame();
            return;
        }
        clear_active_routes(false);
        selected_building_name_ = hit->qualified_name;
        selected_building_module_path_ = hit->module_path;
        selected_building_source_file_ = hit->source_file_path;
        DRAXUL_LOG_DEBUG(LogCategory::App, "Selected building: %s (%s)",
            selected_building_name_.c_str(),
            selected_building_module_path_.c_str());
        apply_selection_opacity();
        request_routes_for_focus(
            selected_building_source_file_,
            selected_building_module_path_,
            selected_building_name_);
    }
    else
    {
        clear_selection();
    }
}

void MegaCityHost::handle_double_click(const glm::ivec2& screen_pos)
{
    PERF_MEASURE();
    if (!camera_ || !semantic_model_ || !semantic_layout_ || !scene_pass_ || !callbacks_)
        return;

    const glm::ivec2 local_pos = screen_pos - viewport_.pixel_pos;
    if (local_pos.x < 0 || local_pos.y < 0 || local_pos.x >= pixel_w_ || local_pos.y >= pixel_h_)
        return;

    auto hit = pick_building(
        local_pos,
        pixel_w_,
        pixel_h_,
        *camera_,
        *semantic_layout_,
        nullptr,
        semantic_model_.get(),
        &renderer_config_);
    if (!hit)
        return;

    // Find the building in the semantic model to resolve source file and function layer.
    for (const auto& mod : semantic_model_->modules)
    {
        if (mod.module_path != hit->module_path)
            continue;
        for (const auto& bldg : mod.buildings)
        {
            if (bldg.qualified_name != hit->qualified_name
                || bldg.source_file_path != hit->source_file_path)
                continue;

            // Resolve which function layer was hit.
            std::string function_name;
            if (!bldg.layers.empty())
            {
                auto try_layer = [&](size_t idx) {
                    if (idx < bldg.layers.size() && !bldg.layers[idx].function_name.empty())
                        function_name = bldg.layers[idx].function_name;
                };

                if (hit->has_layer_index)
                {
                    try_layer(static_cast<size_t>(hit->layer_index));
                }
                else if (hit->hit_y >= 0.0f)
                {
                    float cumulative_y = 0.0f;
                    for (size_t i = 0; i < bldg.layers.size(); ++i)
                    {
                        cumulative_y += bldg.layers[i].height;
                        if (hit->hit_y <= cumulative_y)
                        {
                            try_layer(i);
                            break;
                        }
                    }
                }
            }

            const auto target = find_implementation_file(
                scan_root_, bldg.source_file_path, function_name);

            DRAXUL_LOG_DEBUG(LogCategory::App,
                "Double-click: opening %s, function=%s, qualified=%s (source was %s)",
                target.file.string().c_str(),
                target.function_name.empty() ? "(none)" : target.function_name.c_str(),
                bldg.qualified_name.c_str(),
                bldg.source_file_path.c_str());

            if (!target.function_name.empty())
            {
                // Format: open_file_at_function:path|qualified_name|function_name
                callbacks_->dispatch_to_nvim_host(
                    "open_file_at_function:" + target.file.string() + "|"
                    + bldg.qualified_name + "|" + target.function_name);
            }
            else
            {
                callbacks_->dispatch_to_nvim_host("open_file:" + target.file.string());
            }
            return;
        }
    }
}

bool MegaCityHost::update_hidden_hover_blend(float dt, std::chrono::steady_clock::time_point now)
{
    PERF_MEASURE();
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
    PERF_MEASURE();
    if (!scene_pass_ || !semantic_model_ || selected_building_name_.empty())
        return;

    const std::string selected_identity
        = exact_building_identity_key(
            selected_building_source_file_,
            selected_building_module_path_,
            selected_building_name_);
    const std::unordered_set<std::string> connected = connected_building_identities(
        *semantic_model_,
        selected_building_source_file_,
        selected_building_module_path_,
        selected_building_name_);
    SceneSnapshot& scene = scene_pass_->scene();
    std::unordered_set<std::string> visible_modules;
    visible_modules.emplace(selected_building_module_path_);
    for (const auto& obj : scene.objects)
    {
        const std::string object_identity = obj.source_name.empty()
            ? std::string()
            : exact_building_identity_key(obj.source_file_path, obj.source_module_path, obj.source_name);
        if ((!object_identity.empty() && object_identity == selected_identity)
            || (!object_identity.empty() && connected.count(object_identity) > 0))
        {
            visible_modules.emplace(obj.source_module_path);
        }
    }

    const float hidden_building_alpha = std::clamp(
        renderer_config_.selection_hidden_alpha
            + (renderer_config_.selection_hidden_hover_alpha - renderer_config_.selection_hidden_alpha)
                * hidden_hover_blend_,
        0.0f, 1.0f);
    for (auto& obj : scene.objects)
    {
        const std::string object_identity = obj.source_name.empty()
            ? std::string()
            : exact_building_identity_key(obj.source_file_path, obj.source_module_path, obj.source_name);
        const bool is_selected = !object_identity.empty() && object_identity == selected_identity;
        const bool is_connected = !object_identity.empty() && connected.count(object_identity) > 0;
        const bool is_module_context = is_module_context_object(obj)
            && visible_modules.count(obj.source_module_path) > 0;
        const bool is_selected_route = !obj.route_source.empty()
            && (exact_building_identity_key(
                    obj.route_source_file_path,
                    obj.route_source_module_path,
                    obj.route_source)
                    == selected_identity
                || exact_building_identity_key(
                       obj.route_target_file_path,
                       obj.route_target_module_path,
                       obj.route_target)
                    == selected_identity);

        float alpha = obj.mesh == MeshId::RoadSurface
            ? renderer_config_.selection_hidden_road_alpha
            : renderer_config_.selection_hidden_alpha;
        if (!is_selected
            && !is_connected
            && !is_module_context
            && !is_selected_route
            && obj.mesh != MeshId::RoadSurface)
            alpha = hidden_building_alpha;
        if (is_connected || is_module_context)
            alpha = renderer_config_.selection_dependency_alpha;
        if (is_selected || is_selected_route)
            alpha = 1.0f;

        obj.color.a = std::clamp(alpha, 0.0f, 1.0f);
    }

    sort_scene_objects(scene);
}

void MegaCityHost::clear_selection()
{
    PERF_MEASURE();
    if (selected_building_name_.empty())
        return;

    selected_building_name_.clear();
    selected_building_module_path_.clear();
    selected_building_source_file_.clear();
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
