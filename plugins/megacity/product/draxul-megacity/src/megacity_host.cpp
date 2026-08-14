#include "biology_builder.h"
#include "building_tooltip.h"
#include "city_builder.h"
#include "city_helpers.h"
#include "city_meshes.h"
#include "city_picking.h"
#include "city_selection.h"
#include "megacity_camera_input.h"
#include "megacity_host_panels.h"
#include <draxul/isometric_camera.h>
#include <draxul/codeviz_scene_pass.h>
#include "live_city_metrics.h"
#include "metrics_overlay_controller.h"
#include "scene_publication.h"
#include <draxul/codeviz_scene_world.h>
#include "semantic_city_layout.h"
#include "semantic_source_controller.h"
#include "sign_label_atlas.h"
#include "ui_treesitter_panel.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <draxul/codeviz_scene_sort.h>
#include <draxul/code_semantic_model.h>
#include <draxul/config_document.h>
#include <draxul/imgui_host.h>
#include <draxul/log.h>
#include <draxul/megacity_host.h>
#include <draxul/perf_timing.h>
#include <draxul/text_service.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <imgui.h>
#include <unordered_map>
#include <unordered_set>

namespace draxul
{

namespace
{

constexpr int kHoverTooltipResetDistancePixels = 4;
constexpr auto kMovementTick = std::chrono::milliseconds(16);
constexpr auto kDragSmoothingTick = std::chrono::milliseconds(8);

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

const SemanticCityBuilding* find_layout_building_for_route_debug(
    const SemanticMegacityLayout& layout,
    const SemanticMegacityModel& model,
    std::string_view module_path,
    std::string_view qualified_name)
{
    auto find_exact = [&](std::string_view target_name) -> const SemanticCityBuilding* {
        for (const auto& module_layout : layout.modules)
        {
            if (module_layout.module_path != module_path)
                continue;
            for (const auto& building : module_layout.buildings)
            {
                if (building.qualified_name == target_name)
                    return &building;
            }
        }
        return nullptr;
    };

    if (const auto* direct = find_exact(qualified_name))
        return direct;

    const auto function_bundle = model.function_bundle_remap.find(std::string(qualified_name));
    if (function_bundle != model.function_bundle_remap.end())
    {
        if (const auto* bundle = find_exact(function_bundle->second))
            return bundle;
    }

    const auto struct_stack = model.struct_stack_remap.find(std::string(qualified_name));
    if (struct_stack != model.struct_stack_remap.end())
    {
        if (const auto* stack = find_exact(struct_stack->second))
            return stack;
    }

    return nullptr;
}

void log_route_focus_debug(
    const SemanticMegacityLayout& layout,
    const SemanticMegacityModel& model,
    std::string_view focus_module_path,
    std::string_view focus_qualified_name,
    std::string_view focus_function_name,
    const std::vector<CityGrid::RoutePolyline>* routes)
{
    const auto* focus_building = find_layout_building_for_route_debug(
        layout,
        model,
        focus_module_path,
        focus_qualified_name);

    size_t matching_dependencies = 0;
    size_t routable_dependencies = 0;
    for (const auto& dep : model.dependencies)
    {
        if (!focus_function_name.empty()
            && dep.source_qualified_name != focus_function_name
            && dep.target_qualified_name != focus_function_name)
        {
            continue;
        }

        const auto* source_building = find_layout_building_for_route_debug(
            layout,
            model,
            dep.source_module_path,
            dep.source_qualified_name);
        const auto* target_building = find_layout_building_for_route_debug(
            layout,
            model,
            dep.target_module_path,
            dep.target_qualified_name);

        const bool touches_focus = focus_building
            && ((source_building && source_building->qualified_name == focus_building->qualified_name
                    && source_building->module_path == focus_building->module_path)
                || (target_building && target_building->qualified_name == focus_building->qualified_name
                    && target_building->module_path == focus_building->module_path));
        if (!touches_focus)
            continue;

        ++matching_dependencies;
        if (source_building && target_building && source_building != target_building)
            ++routable_dependencies;

        DRAXUL_LOG_DEBUG(LogCategory::App,
            "MegaCity route candidate: focus=%s function=%s dep=%s -> %s field=%s type=%s source_present=%d target_present=%d",
            std::string(focus_qualified_name).c_str(),
            focus_function_name.empty() ? "(none)" : std::string(focus_function_name).c_str(),
            dep.source_qualified_name.c_str(),
            dep.target_qualified_name.c_str(),
            dep.field_name.c_str(),
            dep.field_type_name.c_str(),
            source_building ? 1 : 0,
            target_building ? 1 : 0);
    }

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "MegaCity route focus summary: building=%s module=%s function=%s focus_building_present=%d matching_dependencies=%zu routable_dependencies=%zu",
        std::string(focus_qualified_name).c_str(),
        std::string(focus_module_path).c_str(),
        focus_function_name.empty() ? "(none)" : std::string(focus_function_name).c_str(),
        focus_building ? 1 : 0,
        matching_dependencies,
        routable_dependencies);

    if (!routes)
        return;

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "MegaCity route build result: building=%s function=%s routes=%zu",
        std::string(focus_qualified_name).c_str(),
        focus_function_name.empty() ? "(none)" : std::string(focus_function_name).c_str(),
        routes->size());
    for (const auto& route : *routes)
    {
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "MegaCity route found: %s -> %s points=%zu",
            route.source_qualified_name.c_str(),
            route.target_qualified_name.c_str(),
            route.world_points.size());
    }
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

std::optional<std::filesystem::path> resolve_scan_root(
    const PluginRuntimeLaunchOptions& launch, std::string* error_message)
{
    const std::filesystem::path requested = launch.source_path.empty()
        ? std::filesystem::current_path()
        : std::filesystem::path(launch.source_path);

    std::error_code ec;
    if (!std::filesystem::exists(requested, ec) || ec)
    {
        if (error_message)
            *error_message = "MegaCity source path does not exist: " + requested.string();
        return std::nullopt;
    }
    if (!std::filesystem::is_directory(requested, ec) || ec)
    {
        if (error_message)
            *error_message = "MegaCity source path is not a directory: " + requested.string();
        return std::nullopt;
    }

    const std::filesystem::path canonical = std::filesystem::weakly_canonical(requested, ec);
    if (ec)
    {
        if (error_message)
            *error_message = "MegaCity source path could not be resolved: " + requested.string();
        return std::nullopt;
    }
    return canonical;
}

std::filesystem::path fallback_scan_root()
{
    std::error_code ec;
    std::filesystem::path root = std::filesystem::current_path(ec);
    if (!ec && !root.empty())
        return root;

    return ".";
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
    config.projection_mode = MegaCityProjectionMode::Orthographic;
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

bool is_module_context_object(const CodeVizRenderable& obj)
{
    return obj.role == CodeVizRenderable::Role::ModuleOutline
        || obj.role == CodeVizRenderable::Role::ModuleLabel;
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

bool is_biology_view(MegaCityVisualizationMode mode)
{
    return mode == MegaCityVisualizationMode::Biology;
}

const char* visualization_host_name(MegaCityVisualizationMode mode)
{
    return is_biology_view(mode) ? "bioview" : "megacity";
}

const char* visualization_log_name(MegaCityVisualizationMode mode)
{
    return is_biology_view(mode) ? "BioViewHost" : "MegaCityHost";
}

} // namespace

MegaCityHost::MegaCityHost(MegaCityVisualizationMode mode)
    : visualization_mode_(mode)
    , camera_input_(std::make_unique<MegacityCameraInput>())
    , semantic_source_(std::make_unique<SemanticSourceController>())
    , metrics_overlay_(std::make_unique<MetricsOverlayController>())
{
}

MegaCityHost::~MegaCityHost()
{
    {
        std::lock_guard<std::mutex> lock(route_mutex_);
        route_worker_stop_ = true;
        pending_route_request_.reset();
    }
    route_cv_.notify_all();
    join_all_grid_threads();
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

bool MegaCityHost::initialize(const PluginRuntimeContext& context, PluginRuntimeCallbacks& callbacks)
{
    PERF_MEASURE();
    init_error_.clear();
    callbacks_ = &callbacks;
    config_document_ = context.config_document;
    renderer_defaults_ = config_document_
        ? load_megacity_code_defaults(*config_document_)
        : MegaCityCodeConfig{};
    renderer_config_ = config_document_
        ? load_megacity_code_config(*config_document_, renderer_defaults_)
        : renderer_defaults_;
    pending_renderer_config_ = renderer_config_;
    show_ui_panels_ = renderer_config_.show_ui_panels && context.launch_options.show_ui_panels;
    continuous_refresh_enabled_ = context.launch_options.request_continuous_refresh;
    restore_camera_after_initial_build_ = renderer_config_.camera_state_valid;

    const auto resolved_scan_root = resolve_scan_root(context.launch_options, &init_error_);
    if (!resolved_scan_root)
        return false;
    semantic_source_->set_root(*resolved_scan_root);
    metrics_overlay_->set_source_root(*resolved_scan_root);

    // Create MegaCity's own ImGui context for isolated docking and layout.
    {
        const std::filesystem::path settings_root = context.storage_directory.empty()
            ? ConfigDocument::default_path().parent_path()
            : context.storage_directory;
        std::filesystem::path ini_path = settings_root
            / (is_biology_view(visualization_mode_) ? "bioview_imgui.ini" : "megacity_imgui.ini");
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
    sign_font_path_ = context.text_service
        ? context.text_service->primary_font_path() : std::string{};
    display_ppi_ = context.display_ppi;
    viewport_ = context.initial_viewport;
    pixel_w_ = viewport_.pixel_size.x > 0 ? viewport_.pixel_size.x : 800;
    pixel_h_ = viewport_.pixel_size.y > 0 ? viewport_.pixel_size.y : 600;

    world_ = std::make_unique<CodeVizSceneWorld>();
    camera_ = std::make_unique<IsometricCamera>();
    camera_->set_viewport(pixel_w_, pixel_h_);
    camera_->set_projection_mode(renderer_config_.projection_mode);
    camera_->frame_world_bounds(-2.5f, 2.5f, -2.5f, 2.5f);
    scene_pass_ = std::make_shared<CodeVizScenePass>(1, 1, world_->tile_size());
    refresh_sign_text_service();

    world_rebuild_pending_ = false;
    city_bounds_valid_ = false;
    last_activity_time_ = std::chrono::steady_clock::now();
    last_pump_time_ = last_activity_time_;
    metrics_overlay_->reset();
    metrics_overlay_->set_collection_enabled(is_biology_view(visualization_mode_), renderer_config_.overlay_mode);

    route_worker_stop_ = false;
    start_tree_sitter_semantic_source();

    running_ = true;
    route_thread_ = std::thread([this]() { route_worker_loop(); });
    mark_scene_dirty();

    DRAXUL_LOG_INFO(LogCategory::App, "%s initialized (%dx%d), scanning %s",
        visualization_log_name(visualization_mode_), pixel_w_, pixel_h_, semantic_source_->root().string().c_str());
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
            log_route_focus_debug(
                *request.layout,
                *request.model,
                request.focus_module_path,
                request.focus_qualified_name,
                request.focus_function_name,
                nullptr);
            routed_grid = std::make_shared<CityGrid>(*request.grid);
            routed_grid->routes = build_city_routes_for_selection(
                *request.layout,
                *request.model,
                *request.grid,
                request.config,
                request.focus_source_file_path,
                request.focus_module_path,
                request.focus_qualified_name,
                request.focus_function_name);
            log_route_focus_debug(
                *request.layout,
                *request.model,
                request.focus_module_path,
                request.focus_qualified_name,
                request.focus_function_name,
                &routed_grid->routes);
            if (routed_grid->routes.empty())
            {
                DRAXUL_LOG_WARN(LogCategory::App,
                    "MegaCity route build produced no routes for building=%s module=%s function=%s",
                    request.focus_qualified_name.c_str(),
                    request.focus_module_path.c_str(),
                    request.focus_function_name.empty() ? "(none)" : request.focus_function_name.c_str());
            }
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

bool MegaCityHost::request_routes_for_focus(
    std::string focus_source_file_path,
    std::string focus_module_path,
    std::string focus_qualified_name,
    std::string focus_function_name)
{
    PERF_MEASURE();
    if (focus_qualified_name.empty() || !semantic_layout_ || !semantic_model_)
        return false;

    std::shared_ptr<const CityGrid> grid;
    {
        std::lock_guard<std::mutex> lock(grid_mutex_);
        grid = city_grid_;
    }
    if (!grid)
        return false;

    {
        std::lock_guard<std::mutex> lock(route_mutex_);
        pending_route_request_ = RouteBuildRequest{
            ++route_request_generation_,
            std::move(focus_source_file_path),
            std::move(focus_module_path),
            std::move(focus_qualified_name),
            std::move(focus_function_name),
            semantic_layout_,
            semantic_model_,
            std::move(grid),
            renderer_config_,
        };
        completed_route_result_.reset();
        route_build_in_progress_ = true;
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "Queued focused route request: building=%s module=%s function=%s",
            pending_route_request_->focus_qualified_name.c_str(),
            pending_route_request_->focus_module_path.c_str(),
            pending_route_request_->focus_function_name.empty() ? "(none)"
                                                                : pending_route_request_->focus_function_name.c_str());
    }
    route_cv_.notify_one();
    return true;
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
    selection_routes_requested_ = false;

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
    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Consumed focused route result: route_polylines=%zu",
        result->grid ? result->grid->routes.size() : 0);
    scene_dirty_ = true;
    last_activity_time_ = std::chrono::steady_clock::now();
    if (callbacks_)
        callbacks_->request_frame();
}

void MegaCityHost::clear_semantic_city()
{
    PERF_MEASURE();
    request_grid_build_cancel();
    grid_build_generation_.fetch_add(1);
    retire_grid_thread();
    join_finished_grid_threads();
    grid_build_in_progress_ = false;
    if (world_)
        world_->clear();
    semantic_model_ = std::make_shared<SemanticMegacityModel>();
    semantic_layout_.reset();
    code_semantics_.reset();
    metrics_overlay_->reset();
    sign_label_atlas_.reset();
    foliage_stem_mesh_.reset();
    foliage_card_mesh_.reset();
    city_bounds_valid_ = false;
    {
        std::lock_guard<std::mutex> lock(grid_mutex_);
        city_grid_.reset();
    }
    clear_active_routes(false);
    world_rebuild_pending_ = false;
    mark_scene_dirty();
}

void MegaCityHost::request_grid_build_cancel()
{
    cancel_build_ = true;
    if (grid_thread_cancel_)
        grid_thread_cancel_->store(true);
    for (auto& retired : retired_grid_threads_)
    {
        if (retired.cancel)
            retired.cancel->store(true);
    }
}

void MegaCityHost::retire_grid_thread()
{
    if (!grid_thread_.joinable())
    {
        grid_thread_cancel_.reset();
        grid_thread_finished_.reset();
        return;
    }

    if (grid_thread_finished_ && grid_thread_finished_->load())
    {
        grid_thread_.join();
    }
    else
    {
        retired_grid_threads_.push_back(RetiredGridThread{
            std::move(grid_thread_),
            std::move(grid_thread_cancel_),
            std::move(grid_thread_finished_),
        });
    }

    grid_thread_cancel_.reset();
    grid_thread_finished_.reset();
}

void MegaCityHost::join_finished_grid_threads()
{
    auto it = retired_grid_threads_.begin();
    while (it != retired_grid_threads_.end())
    {
        if (!it->thread.joinable())
        {
            it = retired_grid_threads_.erase(it);
            continue;
        }

        if (it->finished && it->finished->load())
        {
            it->thread.join();
            it = retired_grid_threads_.erase(it);
            continue;
        }

        ++it;
    }
}

void MegaCityHost::join_all_grid_threads()
{
    request_grid_build_cancel();
    grid_build_generation_.fetch_add(1);
    if (grid_thread_.joinable())
        grid_thread_.join();
    grid_thread_cancel_.reset();
    grid_thread_finished_.reset();

    for (auto& retired : retired_grid_threads_)
    {
        if (retired.thread.joinable())
            retired.thread.join();
    }
    retired_grid_threads_.clear();
    grid_build_in_progress_ = false;
}

void MegaCityHost::start_tree_sitter_semantic_source()
{
    PERF_MEASURE();
    code_semantics_.reset();
    semantic_source_->start();
}

void MegaCityHost::stop_tree_sitter_semantic_source()
{
    PERF_MEASURE();
    semantic_source_->stop();
    code_semantics_.reset();
}

void MegaCityHost::sync_camera_state_to_configs()
{
    PERF_MEASURE();
    if (!camera_)
        return;

    const IsometricCameraState state = camera_->state();
    auto write_state = [&](MegaCityCodeConfig& config) {
        config.projection_mode = state.projection_mode;
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
    camera_input_->reset_keys();
}

void MegaCityHost::on_key(const KeyEvent& event)
{
    PERF_MEASURE();

    if (route_megacity_imgui_key(imgui_context_, event))
    {
        if (callbacks_)
            callbacks_->request_frame();
        return;
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

    if (camera_input_->on_key(event))
    {
        last_pump_time_ = std::chrono::steady_clock::now();
        mark_scene_dirty();
    }
}

void MegaCityHost::on_text_input(const TextInputEvent& event)
{
    PERF_MEASURE();
    if (route_megacity_imgui_text(imgui_context_, event) && callbacks_)
        callbacks_->request_frame();
}

void MegaCityHost::on_mouse_move(const MouseMoveEvent& event)
{
    PERF_MEASURE();

    if (route_megacity_imgui_mouse_move(imgui_context_, event))
    {
        if (callbacks_)
            callbacks_->request_frame();
        return;
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

    if (camera_input_->on_mouse_move(event, *camera_))
    {
        last_activity_time_ = std::chrono::steady_clock::now();
        if (callbacks_)
            callbacks_->request_frame();
    }
}

void MegaCityHost::on_mouse_button(const MouseButtonEvent& event)
{
    PERF_MEASURE();

    if (route_megacity_imgui_mouse_button(imgui_context_, event))
    {
        if (callbacks_)
            callbacks_->request_frame();
        return;
    }

    camera_input_->on_mouse_button(event);
    if (!event.pressed && callbacks_)
        callbacks_->request_frame();
}

void MegaCityHost::on_mouse_wheel(const MouseWheelEvent& event)
{
    route_megacity_imgui_mouse_wheel(imgui_context_, event);
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
    MegacityHostPanelFrame panel_frame(
        imgui_context_, imgui_backend_, viewport_, pixel_w_, pixel_h_, dt, show_ui_panels_);
    if (!panel_frame.active() || !panel_frame.panels_visible())
        return;

    std::shared_ptr<const CityGrid> grid;
    {
        std::lock_guard<std::mutex> lock(grid_mutex_);
        grid = city_grid_;
    }
    panel_frame.render_fixed_panels(
        scene_pass_.get(), is_biology_view(visualization_mode_), grid, grid_build_in_progress_.load());

    const auto perf_debug = is_biology_view(visualization_mode_)
        ? nullptr
        : metrics_overlay_->build_debug_state(pending_renderer_config_.overlay_mode, semantic_model_.get());

    CodeVizRendererControls renderer_controls{
        .config = pending_renderer_config_,
        .defaults = renderer_defaults_,
        .available_modules = semantic_source_->available_modules(),
        .perf_debug = perf_debug,
        .rebuild_pending = world_rebuild_pending_
            || requires_world_rebuild(renderer_config_, pending_renderer_config_),
    };
    const auto scanner_snapshot = semantic_source_->scanner_snapshot();
    const CodebaseScanProgress scanner_progress = semantic_source_->progress();
    const CodeVisualizationPanelMode panel_mode = is_biology_view(visualization_mode_)
        ? CodeVisualizationPanelMode::Biology
        : CodeVisualizationPanelMode::City;
    if (render_treesitter_panel(
            viewport_.pixel_pos.x,
            viewport_.pixel_pos.y,
            pixel_w_,
            pixel_h_,
            scanner_snapshot,
            scanner_progress,
            semantic_model_.get(),
            code_semantics_.get(),
            &renderer_controls,
            panel_mode))
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
        const bool world_rebuild_needed = requires_world_rebuild(renderer_config_, pending_renderer_config_);
        if (metrics_overlay_->apply_mode_transition(
                previous_pending.overlay_mode,
                pending_renderer_config_.overlay_mode,
                semantic_model_.get()))
            mark_scene_dirty();

        if (world_rebuild_needed)
        {
            if (pending_changed)
                mark_world_rebuild_pending();
        }
        else if (pending_changed)
        {
            renderer_config_ = pending_renderer_config_;
            if (camera_ && previous_pending.projection_mode != renderer_config_.projection_mode)
                camera_->set_projection_mode(renderer_config_.projection_mode);
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
            if (!semantic_source_->ready() || !semantic_source_->started())
            {
                start_tree_sitter_semantic_source();
            }
            const bool semantic_source_ready = semantic_source_->ready();
            if (semantic_source_ready)
                rebuild_semantic_city();
            else
                mark_world_rebuild_pending();
        }
    }

    panel_frame.finish();
}

void MegaCityHost::quiesce()
{
    {
        std::lock_guard<std::mutex> lock(route_mutex_);
        route_worker_stop_ = true;
        pending_route_request_.reset();
        completed_route_result_.reset();
    }
    route_cv_.notify_all();
    semantic_source_->stop();

    join_all_grid_threads();
    if (route_thread_.joinable())
        route_thread_.join();
    running_ = false;
}

void MegaCityHost::shutdown()
{
    PERF_MEASURE();

    quiesce();
    city_grid_.reset();
    semantic_layout_.reset();
    code_semantics_.reset();

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
    metrics_overlay_->set_collection_enabled(true, OverlayMode::None);
    sign_label_atlas_.reset();
    metrics_overlay_->reset();
    semantic_model_.reset();
    code_semantics_.reset();
    camera_.reset();
    world_.reset();
}

bool MegaCityHost::is_running() const
{
    return running_;
}

bool MegaCityHost::requires_periodic_wake() const
{
    return running_ && (
        (semantic_source_ && semantic_source_->started()
            && !semantic_source_->ready())
        || grid_build_in_progress_.load()
        || route_build_in_progress_.load());
}

std::string MegaCityHost::init_error() const
{
    return init_error_;
}

void MegaCityHost::set_viewport(const PluginRuntimeViewport& viewport)
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
    if (!code_semantics_)
        return;
    bool result_bounds_valid = false;
    float result_min_x = 0.0f;
    float result_max_x = 0.0f;
    float result_min_z = 0.0f;
    float result_max_z = 0.0f;
    bool result_computed_default_light = false;
    float result_default_light_x = 0.0f;
    float result_default_light_y = 0.0f;
    float result_default_light_z = 0.0f;
    float result_default_light_radius = 0.0f;
    std::shared_ptr<const SemanticMegacityModel> result_semantic_model;
    std::shared_ptr<const LiveCityMetricsSnapshot> result_live_metrics;
    std::shared_ptr<SignLabelAtlas> result_sign_label_atlas;
    std::shared_ptr<SemanticMegacityLayout> result_semantic_layout;

    if (is_biology_view(visualization_mode_))
    {
        BiologyBuildResult result = build_biology_view(
            *world_,
            *code_semantics_,
            renderer_config_);
        foliage_stem_mesh_.reset();
        foliage_card_mesh_.reset();
        result_bounds_valid = result.bounds_valid;
        result_min_x = result.min_x;
        result_max_x = result.max_x;
        result_min_z = result.min_z;
        result_max_z = result.max_z;
        result_computed_default_light = result.computed_default_light;
        result_default_light_x = result.default_light_x;
        result_default_light_y = result.default_light_y;
        result_default_light_z = result.default_light_z;
        result_default_light_radius = result.default_light_radius;
        result_semantic_model.reset();
        result_live_metrics.reset();
    }
    else
    {
        CityBuildResult result = build_city(
            *world_, *code_semantics_, sign_text_service_.get(),
            renderer_config_, sign_label_revision_);
        foliage_stem_mesh_ = result.foliage_stem_mesh;
        foliage_card_mesh_ = result.foliage_card_mesh;
        result_bounds_valid = result.city_bounds_valid;
        result_min_x = result.min_x;
        result_max_x = result.max_x;
        result_min_z = result.min_z;
        result_max_z = result.max_z;
        result_computed_default_light = result.computed_default_light;
        result_default_light_x = result.default_light_x;
        result_default_light_y = result.default_light_y;
        result_default_light_z = result.default_light_z;
        result_default_light_radius = result.default_light_radius;
        result_semantic_model = std::move(result.semantic_model);
        result_live_metrics = std::move(result.live_metrics);
        result_sign_label_atlas = std::move(result.sign_label_atlas);
        result_semantic_layout = result.layout
            ? std::make_shared<SemanticMegacityLayout>(*result.layout)
            : nullptr;
    }

    // Apply presentation bounds.
    city_bounds_valid_ = result_bounds_valid;
    if (city_bounds_valid_)
    {
        city_min_x_ = result_min_x;
        city_max_x_ = result_max_x;
        city_min_z_ = result_min_z;
        city_max_z_ = result_max_z;
    }

    // Apply default point light if the builder computed one.
    if (result_computed_default_light)
    {
        auto set_default_light = [&](MegaCityCodeConfig& config) {
            config.point_light_position_valid = true;
            config.point_light_position = glm::vec3(
                result_default_light_x, result_default_light_y, result_default_light_z);
            config.point_light_radius = result_default_light_radius;
        };
        set_default_light(renderer_config_);
        set_default_light(pending_renderer_config_);
    }

    sign_label_atlas_ = std::move(result_sign_label_atlas);
    metrics_overlay_->adopt_build_metrics(std::move(result_live_metrics));
    semantic_model_ = std::move(result_semantic_model);

    metrics_overlay_->rebuild_lcov_metrics(renderer_config_.overlay_mode, semantic_model_.get());

    semantic_layout_ = std::move(result_semantic_layout);
    clear_active_routes(false);
    {
        std::lock_guard<std::mutex> lock(route_mutex_);
        ++route_request_generation_;
        pending_route_request_.reset();
        completed_route_result_.reset();
        route_build_in_progress_ = false;
    }
    if (!semantic_layout_)
    {
        request_grid_build_cancel();
        {
            std::lock_guard<std::mutex> lock(grid_mutex_);
            city_grid_.reset();
        }
        grid_build_in_progress_ = false;
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
                .projection_mode = renderer_config_.projection_mode,
            });
        }
        else if (!city_bounds_valid_)
            camera_->reframe_world_bounds(-2.5f, 2.5f, -2.5f, 2.5f);
        else
            camera_->reframe_world_bounds(city_min_x_, city_max_x_, city_min_z_, city_max_z_);
    }
    if (camera_)
        camera_->set_projection_mode(renderer_config_.projection_mode);
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
    request_grid_build_cancel();
    retire_grid_thread();
    join_finished_grid_threads();
    const uint64_t generation = grid_build_generation_.fetch_add(1) + 1;
    cancel_build_ = false;

    if (layout.empty())
    {
        std::lock_guard<std::mutex> lock(grid_mutex_);
        city_grid_.reset();
        grid_build_in_progress_ = false;
        return;
    }

    // Copy the layout and config so the thread owns its data.
    auto layout_copy = std::make_shared<SemanticMegacityLayout>(layout);
    auto model_copy = std::make_shared<SemanticMegacityModel>(model);
    const MegaCityCodeConfig config = renderer_config_;
    auto cancel_token = std::make_shared<std::atomic<bool>>(false);
    auto finished_token = std::make_shared<std::atomic<bool>>(false);

    grid_thread_cancel_ = cancel_token;
    grid_thread_finished_ = finished_token;
    grid_build_in_progress_ = true;
    grid_thread_ = std::thread([this, layout_copy, model_copy, config, generation, cancel_token, finished_token]() {
        PERF_MEASURE();
        auto finish = [&]() {
            if (generation == grid_build_generation_.load() && cancel_token && !cancel_token->load())
                grid_build_in_progress_ = false;
            finished_token->store(true);
        };

        if (cancel_token->load() || generation != grid_build_generation_.load())
        {
            finish();
            return;
        }

        auto grid = std::make_shared<CityGrid>(build_city_grid(*layout_copy, *model_copy, config));

        if (cancel_token->load() || generation != grid_build_generation_.load())
        {
            finish();
            return;
        }

        DRAXUL_LOG_DEBUG(LogCategory::App,
            "MegaCityHost: city grid built: %dx%d cells (%.1f x %.1f world units)",
            grid->cols, grid->rows,
            grid->cols * grid->cell_size, grid->rows * grid->cell_size);

        {
            std::lock_guard<std::mutex> lock(grid_mutex_);
            city_grid_ = std::move(grid);
        }
        finish();
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
    metrics_overlay_->set_collection_enabled(is_biology_view(visualization_mode_), renderer_config_.overlay_mode);
    if (camera_)
    {
        const MegacityCameraInputFrame input_frame = camera_input_->update(dt, world_span_, *camera_);
        if (input_frame.double_click)
            handle_double_click(*input_frame.double_click);
        else if (input_frame.click)
            handle_click(*input_frame.click);

        if (input_frame.camera_changed)
        {
            sync_camera_state_to_configs();
            scene_dirty_ = true;
            last_activity_time_ = now;
            if (callbacks_)
                callbacks_->request_frame();
        }
    }

    bool selection_alpha_changed = false;
    if (!selected_building_name_.empty())
        selection_alpha_changed = update_hidden_hover_blend(dt, now);

    last_pump_time_ = now;

    if (auto source_update = semantic_source_->poll())
    {
        code_semantics_ = source_update->semantic_snapshot;
        const auto layout_start = std::chrono::steady_clock::now();
        rebuild_semantic_city();
        const auto layout_end = std::chrono::steady_clock::now();
        const auto layout_ms = std::chrono::duration<double, std::milli>(layout_end - layout_start).count();
        DRAXUL_LOG_INFO(LogCategory::App,
            "%s: built Tree-sitter semantic snapshot (%zu files, %zu modules)",
            visualization_log_name(visualization_mode_),
            source_update->parsed_snapshot->files.size(),
            source_update->available_modules.size());
        DRAXUL_LOG_DEBUG(LogCategory::App,
            "%s: scan %.0fms, semantic snapshot %.0fms, presentation %.0fms",
            visualization_log_name(visualization_mode_),
            source_update->scan_ms,
            source_update->semantic_ms,
            layout_ms);
    }

    consume_completed_routes();

    if (!is_biology_view(visualization_mode_)
        && metrics_overlay_->refresh_live_metrics(now, renderer_config_.overlay_mode, semantic_model_.get()))
        mark_scene_dirty();

    if (!selected_building_name_.empty() && semantic_layout_ && semantic_model_
        && !selection_routes_requested_)
    {
        std::shared_ptr<const CityGrid> grid;
        {
            std::lock_guard<std::mutex> lock(grid_mutex_);
            grid = city_grid_;
        }
        if (grid && grid->routes.empty() && !route_build_in_progress_.load())
        {
            selection_routes_requested_ = request_routes_for_focus(
                selected_building_source_file_,
                selected_building_module_path_,
                selected_building_name_,
                selected_function_name_);
        }
    }

    if (scene_dirty_ && scene_pass_ && camera_ && world_)
    {
        world_span_ = publish_scene_snapshot(
            *scene_pass_,
            *camera_,
            *world_,
            renderer_config_,
            metrics_overlay_->metrics(),
            sign_label_atlas_,
            foliage_stem_mesh_,
            foliage_card_mesh_,
            hover_tooltip_visible_);
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
                        selected_building_name_,
                        selected_function_name_);
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
                            tooltip_data.is_function_bundle = bldg.is_free_function;
                            tooltip_data.is_struct_stack = bldg.is_struct_stack;
                            tooltip_data.lcov_mode = (renderer_config_.overlay_mode == OverlayMode::LcovCoverage);
                            if (const auto live_metrics = metrics_overlay_->metrics())
                            {
                                if (const auto* building_metric = find_building_metric(
                                        *live_metrics,
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
                                        if (bldg.is_struct_stack)
                                            tooltip_data.hovered_field_count = layer.function_size;
                                        if ((bldg.is_free_function || bldg.is_struct_stack) && !layer.source_file_path.empty())
                                            tooltip_data.module_path = layer.source_file_path;
                                        if (const auto live_metrics = metrics_overlay_->metrics())
                                        {
                                            if (const auto* function_metric = find_function_metric(
                                                    *live_metrics,
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
                                    // hit_y is in absolute world space; subtract base
                                    // elevation for comparison with cumulative layer heights.
                                    const float rel_y
                                        = hit->hit_y - building_base_elevation(renderer_config_);
                                    if (bldg.is_struct_stack)
                                    {
                                        // Brick layout: resolve floor from Y, pick first brick on that floor.
                                        const int grid_size = std::max(renderer_config_.struct_brick_grid_size, 1);
                                        const int bpf = brick_slots_per_floor(grid_size);
                                        const float floor_gap = renderer_config_.struct_stack_gap;
                                        float cumulative_y = 0.0f;
                                        const int num_floors = (static_cast<int>(bldg.layers.size()) + bpf - 1) / bpf;
                                        for (int fi = 0; fi < num_floors; ++fi)
                                        {
                                            const size_t fs = static_cast<size_t>(fi) * bpf;
                                            const size_t fe = std::min(fs + static_cast<size_t>(bpf), bldg.layers.size());
                                            float fh = 0.0f;
                                            for (size_t bi = fs; bi < fe; ++bi)
                                                fh = std::max(fh, bldg.layers[bi].height);
                                            cumulative_y += fh;
                                            if (rel_y <= cumulative_y)
                                            {
                                                apply_layer_tooltip(fs);
                                                break;
                                            }
                                            if (fi + 1 < num_floors)
                                                cumulative_y += floor_gap;
                                        }
                                    }
                                    else
                                    {
                                        float cumulative_y = 0.0f;
                                        for (size_t layer_index = 0; layer_index < bldg.layers.size(); ++layer_index)
                                        {
                                            cumulative_y += bldg.layers[layer_index].height;
                                            if (rel_y <= cumulative_y)
                                            {
                                                apply_layer_tooltip(layer_index);
                                                break;
                                            }
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
                    std::shared_ptr<const CityGrid> grid;
                    if (hover_shift_held_)
                    {
                        std::lock_guard<std::mutex> lock(grid_mutex_);
                        grid = city_grid_;
                    }
                    if (const auto feature = pick_city_feature(
                            local_pos,
                            pixel_w_,
                            pixel_h_,
                            *camera_,
                            *semantic_layout_,
                            renderer_config_,
                            grid.get(),
                            hover_shift_held_))
                    {
                        BuildingTooltipData tooltip_data;
                        if (feature->kind == CityFeaturePick::Kind::Park)
                        {
                            tooltip_data.name = feature->is_central_park ? "Central Park" : "Module Park";
                            tooltip_data.park_module = feature->module_path;
                            tooltip_data.park_quality = feature->park_quality;
                            tooltip_data.park_footprint = feature->park_footprint;
                        }
                        else
                        {
                            tooltip_data.route_source = feature->route_source;
                            tooltip_data.route_target = feature->route_target;
                            tooltip_data.route_field_name = feature->route_field_name;
                            tooltip_data.route_field_type = feature->route_field_type;
                        }
                        auto bitmap = rasterize_building_tooltip(*tooltip_text_service_, tooltip_data);
                        show_tooltip_bitmap(bitmap);
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

    if (imgui_settle_frames_ > 0)
    {
        --imgui_settle_frames_;
        if (callbacks_)
            callbacks_->request_frame();
    }

    if (scene_dirty_ && camera_ && world_)
    {
        world_span_ = publish_scene_snapshot(
            *scene_pass_,
            *camera_,
            *world_,
            renderer_config_,
            metrics_overlay_->metrics(),
            sign_label_atlas_,
            foliage_stem_mesh_,
            foliage_card_mesh_,
            hover_tooltip_visible_);
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
    frame.flush_submit_chunk();
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

    if (imgui_settle_frames_ > 0)
        return std::chrono::steady_clock::now();

    if (!continuous_refresh_enabled_ && !camera_input_->movement_active() && !camera_input_->drag_smoothing_active()
        && hidden_hover_blend_ <= 1e-3f
        && !is_overlay_active(renderer_config_.overlay_mode))
        return std::nullopt;
    if (camera_input_->drag_smoothing_active())
        return std::chrono::steady_clock::now() + kDragSmoothingTick;
    if (!is_biology_view(visualization_mode_) && is_live_perf_overlay(renderer_config_.overlay_mode))
        return std::chrono::steady_clock::now() + MetricsOverlayController::refresh_interval();
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
        // ImGui docking layout changes need multiple frames to settle.
        imgui_settle_frames_ = 3;
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

std::string MegaCityHost::status_text() const
{
    std::string status = visualization_host_name(visualization_mode_);
    if (semantic_source_ && semantic_source_->started()
        && !semantic_source_->ready())
        return status + " | scanning";
    if (scene_pass_)
        status += " | " + std::to_string(scene_pass_->scene().objects.size())
            + " objects";
    return status;
}

Color MegaCityHost::default_background() const
{
    if (is_biology_view(visualization_mode_))
        return Color(0.035f, 0.055f, 0.045f, 1.0f);
    return Color(0.05f, 0.05f, 0.10f, 1.0f);
}

PluginRuntimeState MegaCityHost::runtime_state() const
{
    PluginRuntimeState s;
    s.content_ready = true;
    s.last_activity_time = last_activity_time_;
    return s;
}

PluginDebugState MegaCityHost::debug_state() const
{
    PluginDebugState s;
    s.name = visualization_host_name(visualization_mode_);
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
            selected_building_name_,
            selected_function_name_);
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
        // For function bundles, different layers on the same building should
        // trigger a route refresh — only skip if it's truly the exact same pick.
        bool same_pick = hit->qualified_name == selected_building_name_
            && hit->module_path == selected_building_module_path_
            && hit->source_file_path == selected_building_source_file_;
        if (same_pick && !selected_function_name_.empty() && hit->has_layer_index)
            same_pick = false; // Different layer may have been clicked; re-resolve below.
        if (same_pick)
        {
            bool has_active_routes = false;
            {
                std::lock_guard<std::mutex> lock(grid_mutex_);
                has_active_routes = city_grid_ && !city_grid_->routes.empty();
            }
            if (has_active_routes || route_build_in_progress_.load())
            {
                if (callbacks_)
                    callbacks_->request_frame();
                return;
            }
        }
        clear_active_routes(true);
        selected_building_name_ = hit->qualified_name;
        selected_building_module_path_ = hit->module_path;
        selected_building_source_file_ = hit->source_file_path;
        selected_function_name_.clear();

        // For function bundles, resolve which individual function layer was clicked.
        for (const auto& mod : semantic_model_->modules)
        {
            if (mod.module_path != hit->module_path)
                continue;
            for (const auto& bldg : mod.buildings)
            {
                if (bldg.qualified_name != hit->qualified_name
                    || bldg.source_file_path != hit->source_file_path)
                    continue;
                if ((bldg.is_free_function || bldg.is_struct_stack) && !bldg.layers.empty())
                {
                    if (hit->has_layer_index && hit->layer_index < bldg.layers.size())
                    {
                        selected_function_name_ = bldg.layers[hit->layer_index].function_name;
                    }
                    else if (hit->hit_y >= 0.0f)
                    {
                        // hit_y is in absolute world space; subtract base elevation to
                        // get a value comparable to cumulative layer heights.
                        const float relative_y
                            = hit->hit_y - building_base_elevation(renderer_config_);
                        if (bldg.is_struct_stack)
                        {
                            const int gs = std::max(renderer_config_.struct_brick_grid_size, 1);
                            const int bpf = brick_slots_per_floor(gs);
                            const float fg = renderer_config_.struct_stack_gap;
                            float cumulative_y = 0.0f;
                            const int nf = (static_cast<int>(bldg.layers.size()) + bpf - 1) / bpf;
                            for (int fi = 0; fi < nf; ++fi)
                            {
                                const size_t fs = static_cast<size_t>(fi) * bpf;
                                const size_t fe = std::min(fs + static_cast<size_t>(bpf), bldg.layers.size());
                                float fh = 0.0f;
                                for (size_t bi = fs; bi < fe; ++bi)
                                    fh = std::max(fh, bldg.layers[bi].height);
                                cumulative_y += fh;
                                if (relative_y <= cumulative_y)
                                {
                                    selected_function_name_ = bldg.layers[fs].function_name;
                                    break;
                                }
                                if (fi + 1 < nf)
                                    cumulative_y += fg;
                            }
                        }
                        else
                        {
                            float cumulative_y = 0.0f;
                            for (size_t i = 0; i < bldg.layers.size(); ++i)
                            {
                                cumulative_y += bldg.layers[i].height;
                                if (relative_y <= cumulative_y)
                                {
                                    selected_function_name_ = bldg.layers[i].function_name;
                                    break;
                                }
                            }
                        }
                    }
                }
                break;
            }
            break;
        }

        DRAXUL_LOG_DEBUG(LogCategory::App, "Selected building: %s (%s) function=%s",
            selected_building_name_.c_str(),
            selected_building_module_path_.c_str(),
            selected_function_name_.empty() ? "(none)" : selected_function_name_.c_str());
        apply_selection_opacity();
        selection_routes_requested_ = request_routes_for_focus(
            selected_building_source_file_,
            selected_building_module_path_,
            selected_building_name_,
            selected_function_name_);
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
            std::string layer_source_file;
            if (!bldg.layers.empty())
            {
                auto try_layer = [&](size_t idx) {
                    if (idx < bldg.layers.size() && !bldg.layers[idx].function_name.empty())
                    {
                        function_name = bldg.layers[idx].function_name;
                        layer_source_file = bldg.layers[idx].source_file_path;
                    }
                };

                if (hit->has_layer_index)
                {
                    try_layer(static_cast<size_t>(hit->layer_index));
                }
                else if (hit->hit_y >= 0.0f)
                {
                    const float rel_y
                        = hit->hit_y - building_base_elevation(renderer_config_);
                    if (bldg.is_struct_stack)
                    {
                        const int gs = std::max(renderer_config_.struct_brick_grid_size, 1);
                        const int bpf = gs * gs;
                        const float fg = renderer_config_.struct_stack_gap;
                        float cumulative_y = 0.0f;
                        const int nf = (static_cast<int>(bldg.layers.size()) + bpf - 1) / bpf;
                        for (int fi = 0; fi < nf; ++fi)
                        {
                            const size_t fs = static_cast<size_t>(fi) * bpf;
                            const size_t fe = std::min(fs + static_cast<size_t>(bpf), bldg.layers.size());
                            float fh = 0.0f;
                            for (size_t bi = fs; bi < fe; ++bi)
                                fh = std::max(fh, bldg.layers[bi].height);
                            cumulative_y += fh;
                            if (rel_y <= cumulative_y)
                            {
                                try_layer(fs);
                                break;
                            }
                            if (fi + 1 < nf)
                                cumulative_y += fg;
                        }
                    }
                    else
                    {
                        float cumulative_y = 0.0f;
                        for (size_t i = 0; i < bldg.layers.size(); ++i)
                        {
                            cumulative_y += bldg.layers[i].height;
                            if (rel_y <= cumulative_y)
                            {
                                try_layer(i);
                                break;
                            }
                        }
                    }
                }
            }

            // For function bundles and struct stacks, use the per-layer source file path.
            const std::string& nav_source_file = (!layer_source_file.empty())
                ? layer_source_file
                : bldg.source_file_path;

            // For struct stacks, the "function_name" is actually the struct's qualified_name.
            const std::string& nav_qualified_name = (bldg.is_struct_stack && !function_name.empty())
                ? function_name
                : bldg.qualified_name;

            const bool open_as_type = bldg.is_struct || bldg.is_struct_stack;
            const auto target = find_implementation_file(
                semantic_source_->root(),
                nav_source_file,
                open_as_type ? std::string_view{} : std::string_view(function_name));

            DRAXUL_LOG_DEBUG(LogCategory::App,
                "Double-click: opening %s, function=%s, qualified=%s (source was %s, type_nav=%d)",
                target.file.string().c_str(),
                target.function_name.empty() ? "(none)" : target.function_name.c_str(),
                nav_qualified_name.c_str(),
                nav_source_file.c_str(),
                open_as_type ? 1 : 0);

            if (open_as_type)
            {
                callbacks_->dispatch_source_action(
                    "open_file_at_type:" + target.file.string() + "|" + nav_qualified_name);
            }
            else if (!target.function_name.empty())
            {
                // Format: open_file_at_function:path|qualified_name|function_name
                callbacks_->dispatch_source_action(
                    "open_file_at_function:" + target.file.string() + "|"
                    + nav_qualified_name + "|" + target.function_name);
            }
            else
            {
                callbacks_->dispatch_source_action("open_file:" + target.file.string());
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
        selected_building_name_,
        selected_function_name_);
    CodeVizSceneSnapshot& scene = scene_pass_->scene();
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
        auto resolve_route_identity = [&](const std::string& file_path, const std::string& module_path,
                                          const std::string& qualified_name) -> std::string {
            std::string identity = exact_building_identity_key(file_path, module_path, qualified_name);
            if (identity == selected_identity)
                return identity;
            auto remap_it = semantic_model_->function_bundle_remap.find(qualified_name);
            if (remap_it != semantic_model_->function_bundle_remap.end())
                return exact_building_identity_key("", module_path, remap_it->second);
            auto struct_remap_it = semantic_model_->struct_stack_remap.find(qualified_name);
            if (struct_remap_it != semantic_model_->struct_stack_remap.end())
                return exact_building_identity_key("", module_path, struct_remap_it->second);
            return identity;
        };
        const bool is_selected_route = !obj.route_source.empty()
            && (resolve_route_identity(
                    obj.route_source_file_path,
                    obj.route_source_module_path,
                    obj.route_source)
                    == selected_identity
                || resolve_route_identity(
                       obj.route_target_file_path,
                       obj.route_target_module_path,
                       obj.route_target)
                    == selected_identity);

        float alpha = obj.mesh == kCityRoadSurfaceMesh
            ? renderer_config_.selection_hidden_road_alpha
            : renderer_config_.selection_hidden_alpha;
        if (!is_selected
            && !is_connected
            && !is_module_context
            && !is_selected_route
            && obj.mesh != kCityRoadSurfaceMesh)
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
    selected_function_name_.clear();
    selection_routes_requested_ = false;
    hidden_hover_active_ = false;
    hidden_hover_blend_ = 0.0f;

    if (scene_pass_)
    {
        CodeVizSceneSnapshot& scene = scene_pass_->scene();
        for (auto& obj : scene.objects)
            obj.color.a = 1.0f;
        sort_scene_objects(scene);
    }

    if (callbacks_)
        callbacks_->request_frame();
}

} // namespace draxul
