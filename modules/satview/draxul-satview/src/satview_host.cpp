#include <draxul/satview/satview_host.h>

#include "camera.h"
#include "camera_manipulator.h"
#include "satview_camera_key_state.h"
#include "satview_cloud_service.h"
#include "satview_geodetic.h"
#include "satview_map_projection.h"
#include "satview_moon_ephemeris.h"
#include "satview_object_style.h"
#include "satview_scene_pass.h"
#include "satview_simulation_worker.h"
#include "satview_time_format.h"

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <draxul/config_document.h>
#include <draxul/host_registry.h>
#include <draxul/imgui_host.h>
#include <draxul/log.h>
#include <draxul/sdl_imgui_input.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <imgui.h>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace draxul::satview
{

namespace
{

constexpr auto kFrameTick = std::chrono::milliseconds(33);
constexpr float kControlPanelDefaultWidth = 430.0f;
constexpr float kControlPanelDefaultHeight = 500.0f;
constexpr float kControlPanelMinWidth = 380.0f;
constexpr float kControlPanelMinHeight = 360.0f;
constexpr float kControlMinWidgetWidth = 96.0f;
constexpr int kClickSelectionMaxDistancePixels = 18;
constexpr int kClickDragSlopPixels = 5;
constexpr float kCameraDefaultDistance = 3.6f;
constexpr float kCameraMinDistance = 1.7f;
constexpr float kCameraDefaultMaxDistance = 12.0f;
constexpr float kCameraMaxDistanceCap = 256.0f;
constexpr float kCameraFitRadiusScale = 3.1f;
constexpr float kMoonCameraSurfaceDistanceScale = 1.03f;
constexpr float kCameraDefaultNearPlane = 0.05f;
constexpr float kCameraMinNearPlane = 0.0005f;
constexpr float kCameraHorizontalOrbitRadiansPerSecond = 1.8f;
constexpr float kCameraVerticalOrbitRadiansPerSecond = 0.9f;
constexpr float kCameraZoomRatePerSecond = 1.35f;
constexpr float kCameraInputMaxDeltaSeconds = 0.1f;
constexpr float kMoonRadiusEarthRadii =
    static_cast<float>(kSatViewMoonMeanRadiusKm / kSatViewEarthEquatorialRadiusKm);

double unix_seconds_now()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration<double>(now).count();
}

template <std::size_t N>
void copy_to_buffer(char (&buffer)[N], const std::string& value)
{
    const std::size_t length = std::min(value.size(), N - 1);
    std::memcpy(buffer, value.data(), length);
    buffer[length] = '\0';
}

void save_merged_satview_config(ConfigDocument* config_document, const SatViewConfig& config)
{
    if (!config_document)
        return;

    ConfigDocument latest = ConfigDocument::load();
    store_satview_config(latest, config);
    latest.save();
    *config_document = std::move(latest);
}

float camera_max_distance_for_radius(float scene_radius)
{
    return std::clamp(
        scene_radius * kCameraFitRadiusScale,
        kCameraDefaultMaxDistance,
        kCameraMaxDistanceCap);
}

float camera_far_plane(float distance, float scene_radius)
{
    return std::max(64.0f, distance + scene_radius * 2.5f + 8.0f);
}

float camera_target_radius(SatViewCameraPov pov)
{
    return pov == SatViewCameraPov::Moon ? kMoonRadiusEarthRadii : 1.0f;
}

float camera_min_distance(SatViewCameraPov pov)
{
    return pov == SatViewCameraPov::Moon
        ? kMoonCameraSurfaceDistanceScale * kMoonRadiusEarthRadii
        : kCameraMinDistance;
}

float camera_near_plane(SatViewCameraPov pov, float distance)
{
    const float surface_clearance = std::max(
        0.0f,
        distance - camera_target_radius(pov));
    return std::clamp(
        surface_clearance * 0.25f,
        kCameraMinNearPlane,
        kCameraDefaultNearPlane);
}

glm::vec3 camera_target_position(
    SatViewCameraPov pov,
    const SatViewMoonPosition& moon)
{
    return pov == SatViewCameraPov::Moon
        ? glm::vec3(moon.render_position_earth_radii)
        : glm::vec3(0.0f);
}

float camera_scene_radius(
    float earth_scene_radius,
    const SatViewMoonPosition& moon,
    SatViewCameraPov pov,
    bool moon_visible)
{
    const float moon_distance = static_cast<float>(glm::length(moon.render_position_earth_radii));
    if (pov == SatViewCameraPov::Moon)
        return std::max(kMoonRadiusEarthRadii, moon_distance + earth_scene_radius);
    if (moon_visible)
        return std::max(earth_scene_radius, moon_distance + kMoonRadiusEarthRadii);
    return earth_scene_radius;
}

glm::vec3 camera_position_from_yaw_pitch(float yaw, float pitch, float distance)
{
    const float cp = std::cos(pitch);
    return glm::vec3(
        cp * std::sin(yaw),
        std::sin(pitch),
        cp * std::cos(yaw))
        * distance;
}

glm::mat4 camera_view_matrix(const Camera& camera)
{
    return glm::lookAtRH(
        camera.GetPosition(),
        camera.GetFocalPoint(),
        camera.GetUp());
}

glm::vec4 orbit_class_color(OrbitClass orbit_class, float alpha)
{
    switch (orbit_class)
    {
    case OrbitClass::LowEarth:
        return glm::vec4(0.18f, 0.78f, 1.00f, alpha);
    case OrbitClass::MediumEarth:
        return glm::vec4(0.46f, 0.92f, 0.42f, alpha);
    case OrbitClass::Geosynchronous:
        return glm::vec4(1.00f, 0.72f, 0.24f, alpha);
    case OrbitClass::HighlyElliptical:
        return glm::vec4(0.96f, 0.42f, 0.90f, alpha);
    case OrbitClass::Other:
        return glm::vec4(0.72f, 0.78f, 0.86f, alpha);
    }
    return glm::vec4(0.72f, 0.78f, 0.86f, alpha);
}

glm::vec4 object_kind_color(SatelliteObjectKind kind, float alpha)
{
    switch (kind)
    {
    case SatelliteObjectKind::Payload:
        return glm::vec4(0.12f, 0.86f, 0.98f, alpha);
    case SatelliteObjectKind::RocketBody:
        return glm::vec4(1.00f, 0.54f, 0.18f, alpha);
    case SatelliteObjectKind::Debris:
        return glm::vec4(1.00f, 0.28f, 0.36f, alpha);
    case SatelliteObjectKind::Unknown:
        return glm::vec4(0.72f, 0.78f, 0.86f, alpha);
    }
    return glm::vec4(0.72f, 0.78f, 0.86f, alpha);
}

glm::vec4 satellite_color(
    OrbitClass orbit_class,
    SatelliteObjectKind object_kind,
    std::uint32_t object_prefix_hash,
    SatViewColorMode color_mode,
    float alpha)
{
    switch (color_mode)
    {
    case SatViewColorMode::NamePrefix:
        return satellite_prefix_color(object_prefix_hash, alpha);
    case SatViewColorMode::OrbitClass:
        return orbit_class_color(orbit_class, alpha);
    case SatViewColorMode::ObjectType:
        return object_kind_color(object_kind, alpha);
    }
    return satellite_prefix_color(object_prefix_hash, alpha);
}

glm::vec3 to_vec3(const glm::dvec3& value)
{
    return glm::vec3(
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z));
}

int orbit_class_sort_key(OrbitClass orbit_class)
{
    switch (orbit_class)
    {
    case OrbitClass::LowEarth:
        return 0;
    case OrbitClass::MediumEarth:
        return 1;
    case OrbitClass::Geosynchronous:
        return 2;
    case OrbitClass::HighlyElliptical:
        return 3;
    case OrbitClass::Other:
        return 4;
    }
    return 4;
}

std::string object_tree_label(const SatellitePropagatedState& state)
{
    std::string label = std::to_string(state.norad_catalog_id);
    if (!state.object_name.empty())
    {
        label += " - ";
        label += state.object_name;
    }
    if (!state.object_id.empty())
    {
        label += " [";
        label += state.object_id;
        label += "]";
    }
    return label;
}

void append_line(
    std::vector<SatViewSceneVertex>& vertices,
    glm::vec3 a,
    glm::vec3 b,
    glm::vec4 color,
    bool earth_fixed = false)
{
    const float coordinate_tag = earth_fixed ? 2.0f : 1.0f;
    vertices.push_back({ glm::vec4(a, -coordinate_tag), color, glm::vec4(b, coordinate_tag) });
    vertices.push_back({ glm::vec4(b, coordinate_tag), color, glm::vec4(a, coordinate_tag) });
}

bool satellite_visible(
    const SatViewFilterState& filter,
    const SatelliteOrbitTrack& track,
    std::string_view source_label)
{
    return satview_filter_matches(filter, make_satview_filter_candidate(track, source_label));
}

bool satellite_visible(
    const SatViewFilterState& filter,
    const SatellitePropagatedState& state,
    std::string_view source_label)
{
    return satview_filter_matches(filter, make_satview_filter_candidate(state, source_label));
}

void append_track_vertices(
    std::vector<SatViewSceneVertex>& vertices,
    const SatViewSimulationSnapshot& snapshot,
    const SatViewFilterState& filter,
    std::string_view source_label,
    std::optional<std::int64_t> selected_id,
    SatViewTrackDisplayMode track_display_mode,
    SatViewColorMode color_mode,
    SatViewProjectionMode projection_mode,
    SatViewCameraPov camera_pov)
{
    if (!snapshot.tracks)
        return;
    for (const SatelliteOrbitTrack& track : *snapshot.tracks)
    {
        const bool selected = selected_id.has_value() && track.norad_catalog_id == *selected_id;
        if (track_display_mode == SatViewTrackDisplayMode::SelectedOnly
            && (!selected_id.has_value() || track.norad_catalog_id != *selected_id))
        {
            continue;
        }

        if (!selected && !satellite_visible(filter, track, source_label))
            continue;

        const glm::vec4 color = selected
            ? glm::mix(
                  satellite_color(track.orbit_class, track.object_kind, track.object_prefix_hash, color_mode, 0.98f),
                  glm::vec4(1.0f),
                  0.38f)
            : satellite_color(track.orbit_class, track.object_kind, track.object_prefix_hash, color_mode, 0.62f);
        const bool earth_ground_track = projection_mode == SatViewProjectionMode::Map
            && camera_pov == SatViewCameraPov::Earth;
        const auto& points = earth_ground_track
            ? track.render_points_earth_radii
            : track.render_teme_points_earth_radii;
        if (points.size() < 2)
            continue;
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            append_line(
                vertices,
                to_vec3(points[i - 1]),
                to_vec3(points[i]),
                color,
                earth_ground_track);
        }
        if (!earth_ground_track)
        {
            append_line(vertices, to_vec3(points.back()), to_vec3(points.front()),
                color * glm::vec4(1.0f, 1.0f, 1.0f, 0.82f));
        }
    }
}

void append_moon_track_vertices(
    std::vector<SatViewSceneVertex>& vertices,
    double center_seconds,
    std::size_t segment_count)
{
    const glm::vec4 color(0.92f, 0.86f, 0.66f, 0.82f);
    const std::vector<glm::dvec3> points =
        satview_moon_orbit_track(center_seconds, segment_count);
    if (points.size() < 2)
        return;
    for (std::size_t i = 1; i < points.size(); ++i)
        append_line(vertices, to_vec3(points[i - 1]), to_vec3(points[i]), color);
    append_line(vertices, to_vec3(points.back()), to_vec3(points.front()), color);
}

double render_simulation_seconds(const SatViewSimulationSnapshot& snapshot)
{
    return satview_snapshot_render_seconds(snapshot, std::chrono::steady_clock::now());
}

glm::dvec3 interpolated_teme_position(
    const SatViewSimulationSnapshot& snapshot,
    std::size_t state_index,
    double render_seconds)
{
    if (state_index >= snapshot.states.size()
        || state_index >= snapshot.next_teme_positions_km.size()
        || snapshot.next_simulation_seconds <= snapshot.simulation_seconds)
    {
        return snapshot.states[state_index].teme_position_km;
    }

    const double span = snapshot.next_simulation_seconds - snapshot.simulation_seconds;
    const double alpha = std::clamp((render_seconds - snapshot.simulation_seconds) / span, 0.0, 1.0);
    return glm::mix(snapshot.states[state_index].teme_position_km, snapshot.next_teme_positions_km[state_index], alpha);
}

float marker_interpolation_alpha(const SatViewSimulationSnapshot& snapshot, double render_seconds)
{
    if (snapshot.next_simulation_seconds <= snapshot.simulation_seconds)
        return 0.0f;

    const double span = snapshot.next_simulation_seconds - snapshot.simulation_seconds;
    return static_cast<float>(std::clamp((render_seconds - snapshot.simulation_seconds) / span, 0.0, 1.0));
}

glm::dvec3 next_teme_position(const SatViewSimulationSnapshot& snapshot, std::size_t state_index)
{
    if (state_index >= snapshot.next_teme_positions_km.size())
        return snapshot.states[state_index].teme_position_km;
    return snapshot.next_teme_positions_km[state_index];
}

void append_marker_instances(
    std::vector<SatViewMarkerInstance>& markers,
    const SatViewSimulationSnapshot& snapshot,
    const SatViewFilterState& filter,
    std::string_view source_label,
    std::optional<std::int64_t> selected_id,
    SatViewColorMode color_mode,
    std::size_t marker_limit)
{
    std::size_t visible_marker_index = 0;
    for (std::size_t state_index = 0; state_index < snapshot.states.size(); ++state_index)
    {
        const SatellitePropagatedState& state = snapshot.states[state_index];
        const bool selected = selected_id.has_value() && state.norad_catalog_id == *selected_id;
        const bool visible = satellite_visible(filter, state, source_label);
        if (!visible && !selected)
            continue;

        const bool under_marker_limit = marker_limit == 0 || visible_marker_index < marker_limit;
        if (visible)
            ++visible_marker_index;
        if (!under_marker_limit && !selected)
            continue;

        const glm::vec3 position0 = to_vec3(teme_position_to_render_earth_radii(state.teme_position_km));
        const glm::vec3 position1 = to_vec3(
            teme_position_to_render_earth_radii(next_teme_position(snapshot, state_index)));
        const float range = glm::length(position0);
        const float base_size = std::clamp(0.006f + range * 0.0022f, 0.008f, 0.026f);
        const float size = selected ? base_size * 2.2f : base_size;
        const glm::vec4 color = selected
            ? glm::vec4(1.0f, 0.96f, 0.68f, 1.0f)
            : glm::mix(
                satellite_color(state.orbit_class, state.object_kind, state.object_prefix_hash, color_mode, 0.95f),
                glm::vec4(1.0f),
                0.18f);
        markers.push_back({
            glm::vec4(position0, size),
            glm::vec4(position1, selected ? 1.0f : 0.0f),
            color,
        });
    }
}

float visible_scene_radius(
    const SatViewSimulationSnapshot* snapshot,
    const SatViewFilterState& filter,
    std::optional<std::int64_t> selected_id,
    SatViewTrackDisplayMode track_display_mode)
{
    if (!snapshot)
        return 1.0f;

    const std::string_view source_label = snapshot->source_label;
    float radius = 1.0f;
    if (snapshot->tracks)
    {
        for (const SatelliteOrbitTrack& track : *snapshot->tracks)
        {
            if (track_display_mode == SatViewTrackDisplayMode::SelectedOnly
                && (!selected_id.has_value() || track.norad_catalog_id != *selected_id))
            {
                continue;
            }
            if (!satellite_visible(filter, track, source_label))
                continue;

            for (const glm::dvec3& point : track.render_teme_points_earth_radii)
                radius = std::max(radius, glm::length(to_vec3(point)));
        }
    }

    for (std::size_t state_index = 0; state_index < snapshot->states.size(); ++state_index)
    {
        const SatellitePropagatedState& state = snapshot->states[state_index];
        if (!satellite_visible(filter, state, source_label))
            continue;

        radius = std::max(radius, glm::length(to_vec3(state.teme_position_km / kSatViewEarthEquatorialRadiusKm)));
        if (state_index < snapshot->next_teme_positions_km.size())
        {
            radius = std::max(radius,
                glm::length(to_vec3(snapshot->next_teme_positions_km[state_index] / kSatViewEarthEquatorialRadiusKm)));
        }
    }
    return radius;
}

float control_widget_width(const char* label)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float available = ImGui::GetContentRegionAvail().x;
    const float label_width = ImGui::CalcTextSize(label).x;
    const float row_spacing = style.ItemInnerSpacing.x;
    return std::max(kControlMinWidgetWidth, available - label_width - row_spacing);
}

} // namespace

SatViewHost::SatViewHost()
    : cloud_service_(std::make_unique<SatViewCloudService>())
    , camera_(std::make_shared<Camera>())
    , camera_manipulator_(std::make_unique<Manipulator>(camera_))
    , camera_keys_(std::make_unique<SatViewCameraKeyState>())
{
}

SatViewHost::~SatViewHost()
{
    shutdown();
}

bool SatViewHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    callbacks_ = &callbacks;
    config_document_ = context.config_document;
    viewport_ = context.initial_viewport;
    show_ui_panel_ = context.launch_options.show_host_ui_panels;
    continuous_refresh_enabled_ = context.launch_options.request_continuous_refresh;
    apply_config(config_document_ ? load_satview_config(*config_document_) : SatViewConfig{});
    IMGUI_CHECKVERSION();
    imgui_context_ = ImGui::CreateContext();
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
        io.ConfigWindowsResizeFromEdges = true;
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        ImGui::StyleColorsDark();
    }
    catalog_service_.start();
    SatViewCloudService::Config cloud_config;
    cloud_config.cache_directory = SatViewCatalogService::default_cache_directory();
    cloud_service_->start(std::move(cloud_config));
    simulated_seconds_ = unix_seconds_now();
    last_draw_simulation_seconds_ = simulated_seconds_;
    const glm::vec3 sun = glm::vec3(solar_direction_render(simulated_seconds_));
    const SatViewMoonPosition moon = satview_moon_position(simulated_seconds_);
    const glm::vec3 target = camera_target_position(camera_pov_, moon);
    const float target_radius = camera_target_radius(camera_pov_);
    camera_->SetDistanceLimits(
        camera_min_distance(camera_pov_),
        kCameraDefaultMaxDistance);
    camera_->SetPositionAndFocalPoint(
        target + camera_position_from_yaw_pitch(
                     std::atan2(sun.x, sun.z) + 0.65f,
                     0.25f,
                     kCameraDefaultDistance * target_radius),
        target);
    last_pump_time_ = std::chrono::steady_clock::now();
    last_activity_time_ = last_pump_time_;
    scene_pass_ = std::make_shared<SatViewScenePass>();
    SatViewSimulationControls simulation_controls;
    simulation_controls.time_speed = time_speed_;
    simulation_controls.paused = paused_;
    simulation_controls.track_satellite_limit = track_satellite_limit_;
    simulation_controls.track_sample_count = track_sample_count_;
    simulation_controls.refresh_tracks_each_step = refresh_tracks_each_step_;
    simulation_controls.selected_track_norad_catalog_id = selected_norad_catalog_id_;
    simulation_worker_ = std::make_unique<SatViewSimulationWorker>();
    simulation_worker_->start(simulated_seconds_, simulation_controls);
    running_ = true;
    callbacks.set_window_title("SatView");
    request_redraw();
    return true;
}

void SatViewHost::shutdown()
{
    persist_config();
    config_dirty_ = false;
    config_document_ = nullptr;
    catalog_service_.stop();
    if (cloud_service_)
        cloud_service_->stop();
    if (simulation_worker_)
        simulation_worker_->stop();
    running_ = false;
    dragging_ = false;
    pending_click_ = false;
    scene_pass_.reset();
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        if (imgui_backend_)
            imgui_backend_->shutdown_imgui_backend();
        ImGui::DestroyContext(imgui_context_);
        imgui_context_ = nullptr;
        imgui_backend_ = nullptr;
    }
}

bool SatViewHost::is_running() const
{
    return running_;
}

std::string SatViewHost::init_error() const
{
    return init_error_;
}

void SatViewHost::set_viewport(const HostViewport& viewport)
{
    viewport_ = viewport;
    request_redraw();
}

void SatViewHost::pump()
{
    if (!running_)
        return;

    catalog_service_.pump();
    if (cloud_service_)
    {
        cloud_service_->pump();
        if (auto image = cloud_service_->take_pending_image())
        {
            scene_pass_->set_cloud_image(std::move(image));
            request_redraw();
        }
    }
    const std::uint64_t catalog_generation = catalog_service_.catalog_generation();
    if (simulation_worker_ && catalog_generation != 0 && catalog_generation != simulation_catalog_generation_)
    {
        simulation_catalog_generation_ = catalog_generation;
        simulation_worker_->set_catalog(catalog_service_.catalog(), catalog_generation);
    }
    if (simulation_settings_dirty_)
    {
        sync_simulation_render_settings();
        simulation_settings_dirty_ = false;
    }
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last_pump_time_).count();
    last_pump_time_ = now;
    last_imgui_delta_seconds_ = dt;
    if (camera_keys_->movement_active())
    {
        const float input_dt = std::clamp(dt, 0.0f, kCameraInputMaxDeltaSeconds);
        const SatViewCameraMovement movement = camera_keys_->movement();
        if (projection_mode_ == SatViewProjectionMode::Map)
        {
            pan_map(glm::vec2(
                -kCameraHorizontalOrbitRadiansPerSecond * movement.orbit.x * input_dt,
                kCameraVerticalOrbitRadiansPerSecond * movement.orbit.y * input_dt));
        }
        else
        {
            camera_->Orbit(glm::vec2(
                glm::degrees(kCameraHorizontalOrbitRadiansPerSecond) * movement.orbit.x * input_dt,
                glm::degrees(kCameraVerticalOrbitRadiansPerSecond) * movement.orbit.y * input_dt));

            if (movement.zoom != 0.0f)
            {
                const float current_distance = camera_->GetDistance();
                const float target_distance = std::clamp(
                    current_distance * std::exp(movement.zoom * kCameraZoomRatePerSecond * input_dt),
                    camera_->GetMinDistance(),
                    camera_->GetMaxDistance());
                camera_->Dolly(current_distance - target_distance);
            }
        }
    }
    request_redraw();
}

void SatViewHost::draw(IFrameContext& frame)
{
    if (!scene_pass_)
        return;

    auto snapshot_guard = simulation_worker_ ? simulation_worker_->acquire_latest() : SatViewSnapshotExchange::ReadGuard{};
    const SatViewSimulationSnapshot* snapshot = snapshot_guard.get();
    const double simulation_seconds = snapshot ? render_simulation_seconds(*snapshot) : last_draw_simulation_seconds_;
    last_draw_simulation_seconds_ = simulation_seconds;
    const bool map_projection = projection_mode_ == SatViewProjectionMode::Map;
    const SatViewMoonPosition moon = satview_moon_position(simulation_seconds);
    const float earth_scene_radius =
        visible_scene_radius(snapshot, filter_, selected_norad_catalog_id_, track_display_mode_);
    const float scene_radius = camera_scene_radius(
        earth_scene_radius,
        moon,
        camera_pov_,
        moon_enabled_);
    camera_->SetFocalPoint(camera_target_position(camera_pov_, moon));
    camera_->SetDistanceLimits(
        camera_min_distance(camera_pov_),
        camera_max_distance_for_radius(scene_radius));

    const int pixel_w = std::max(1, viewport_.pixel_size.x);
    const int pixel_h = std::max(1, viewport_.pixel_size.y);
    camera_->SetFilmSize(static_cast<float>(pixel_w), static_cast<float>(pixel_h));
    if (!map_projection && camera_->PreRender())
        request_redraw();
    const glm::vec3 eye = camera_->GetPosition();
    const glm::mat4 view = camera_view_matrix(*camera_);
    const glm::mat4 proj = glm::perspectiveRH_ZO(
        glm::radians(camera_->GetFieldOfView()),
        camera_->GetAspectRatio(),
        camera_near_plane(camera_pov_, camera_->GetDistance()),
        camera_far_plane(camera_->GetDistance(), scene_radius));

    SatViewFrameUniforms uniforms;
    uniforms.view_proj = map_projection ? glm::mat4(1.0f) : proj * view;
    const float projection_scale = map_projection
        ? -static_cast<float>(pixel_h) / static_cast<float>(pixel_w)
        : 1.0f;
    uniforms.camera_pos = map_projection
        ? glm::vec4(
              camera_pov_ == SatViewCameraPov::Moon
                  ? glm::vec3(moon.render_position_earth_radii)
                  : glm::vec3(0.0f),
              projection_scale)
        : glm::vec4(eye, projection_scale);
    if (map_projection)
    {
        uniforms.camera_orientation = glm::vec4(
            map_center_radians_,
            camera_pov_ == SatViewCameraPov::Moon ? 1.0f : 0.0f,
            0.0f);
    }
    else
    {
        const glm::quat camera_orientation = camera_->GetOrientation();
        uniforms.camera_orientation = glm::vec4(
            camera_orientation.x,
            camera_orientation.y,
            camera_orientation.z,
            camera_orientation.w);
    }
    const glm::vec3 sun = glm::vec3(solar_direction_render(simulation_seconds));
    const float cloud_mode = !clouds_enabled_ ? 0.0f : (realistic_clouds_enabled_ ? 2.0f : 1.0f);
    uniforms.sun_dir_time = glm::vec4(sun, cloud_mode);
    uniforms.render_params = glm::vec4(
        static_cast<float>(kSatViewSphereLatitudeBands),
        static_cast<float>(kSatViewSphereLongitudeBands),
        static_cast<float>(greenwich_sidereal_angle_radians(simulation_seconds)),
        snapshot ? marker_interpolation_alpha(*snapshot, simulation_seconds) : 0.0f);
    scene_pass_->set_frame(uniforms);
    scene_pass_->set_atmosphere_enabled(atmosphere_enabled_);
    scene_pass_->set_map_projection(
        map_projection,
        camera_pov_ == SatViewCameraPov::Moon);
    scene_pass_->set_moon(
        glm::vec4(glm::vec3(moon.render_position_earth_radii), kMoonRadiusEarthRadii),
        moon_enabled_);

    const void* track_source = snapshot && snapshot->tracks
        ? snapshot->tracks.get()
        : nullptr;
    const bool show_moon_track = moon_track_enabled_
        && !(map_projection && camera_pov_ == SatViewCameraPov::Moon);
    bool moon_track_window_changed = false;
    if (show_moon_track
        && (!moon_track_center_seconds_.has_value()
            || std::abs(simulation_seconds - *moon_track_center_seconds_)
                >= 0.5 * kSatViewMoonSiderealPeriodSeconds))
    {
        moon_track_center_seconds_ = simulation_seconds;
        moon_track_window_changed = true;
    }

    if (track_buffer_dirty_
        || track_source != uploaded_track_source_
        || moon_track_window_changed)
    {
        std::vector<SatViewSceneVertex> track_vertices;
        const std::size_t track_count = snapshot && snapshot->tracks
            ? snapshot->tracks->size()
            : 0;
        track_vertices.reserve(
            (track_count + (show_moon_track ? 1 : 0))
            * (track_sample_count_ + 1) * 2);
        if (snapshot)
        {
            append_track_vertices(
                track_vertices,
                *snapshot,
                filter_,
                snapshot->source_label,
                selected_norad_catalog_id_,
                track_display_mode_,
                color_mode_,
                projection_mode_,
                camera_pov_);
        }
        if (show_moon_track)
        {
            append_moon_track_vertices(
                track_vertices,
                *moon_track_center_seconds_,
                track_sample_count_);
        }
        scene_pass_->set_track_vertices(track_vertices);
        uploaded_track_source_ = track_source;
        track_buffer_dirty_ = false;
    }

    if (snapshot)
    {
        if (marker_buffer_dirty_ || snapshot->generation != uploaded_marker_generation_)
        {
            std::vector<SatViewMarkerInstance> markers;
            markers.reserve(snapshot->states.size());
            append_marker_instances(
                markers,
                *snapshot,
                filter_,
                snapshot->source_label,
                selected_norad_catalog_id_,
                color_mode_,
                marker_satellite_limit_);
            scene_pass_->set_markers(markers);
            uploaded_marker_generation_ = snapshot->generation;
            marker_buffer_dirty_ = false;
        }
    }
    else
    {
        scene_pass_->set_markers(std::span<const SatViewMarkerInstance>{});
        uploaded_marker_generation_ = 0;
        marker_buffer_dirty_ = false;
    }

    RenderViewport viewport;
    viewport.x = viewport_.pixel_pos.x;
    viewport.y = viewport_.pixel_pos.y;
    viewport.width = pixel_w;
    viewport.height = pixel_h;
    frame.record_render_pass(*scene_pass_, viewport);
    render_host_imgui(last_imgui_delta_seconds_, snapshot);
    if (imgui_context_ && imgui_backend_)
        frame.render_imgui(ImGui::GetDrawData(), imgui_context_);
    frame.flush_submit_chunk();
}

std::optional<std::chrono::steady_clock::time_point> SatViewHost::next_deadline() const
{
    if (!running_)
        return std::nullopt;
    if (continuous_refresh_enabled_)
        return std::chrono::steady_clock::now();
    return std::chrono::steady_clock::now() + kFrameTick;
}

void SatViewHost::on_mouse_button(const MouseButtonEvent& event)
{
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
            if (event.button == SDL_BUTTON_LEFT && !event.pressed)
            {
                dragging_ = false;
                pending_click_ = false;
                if (projection_mode_ == SatViewProjectionMode::Globe)
                    camera_manipulator_->MouseUp(glm::vec2(event.pos));
            }
            request_redraw();
            return;
        }
    }

    if (event.button != SDL_BUTTON_LEFT)
        return;
    dragging_ = event.pressed;
    if (event.pressed)
    {
        pending_click_ = true;
        click_start_pos_ = event.pos;
        last_map_drag_pos_ = event.pos;
        if (projection_mode_ == SatViewProjectionMode::Globe)
            camera_manipulator_->MouseDown(glm::vec2(event.pos));
    }
    else
    {
        if (projection_mode_ == SatViewProjectionMode::Globe)
            camera_manipulator_->MouseUp(glm::vec2(event.pos));
        if (pending_click_)
        {
            const glm::ivec2 delta = event.pos - click_start_pos_;
            const int distance_sq = delta.x * delta.x + delta.y * delta.y;
            if (distance_sq <= kClickDragSlopPixels * kClickDragSlopPixels)
                select_nearest_satellite(event.pos);
        }
        pending_click_ = false;
    }
    last_activity_time_ = std::chrono::steady_clock::now();
}

void SatViewHost::on_mouse_move(const MouseMoveEvent& event)
{
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        ImGui::GetIO().AddMousePosEvent(static_cast<float>(event.pos.x), static_cast<float>(event.pos.y));
        if (ImGui::GetIO().WantCaptureMouse)
        {
            request_redraw();
            return;
        }
    }

    if (pending_click_)
    {
        const glm::ivec2 delta = event.pos - click_start_pos_;
        const int distance_sq = delta.x * delta.x + delta.y * delta.y;
        if (distance_sq > kClickDragSlopPixels * kClickDragSlopPixels)
            pending_click_ = false;
    }

    const bool left_button_down = dragging_ || (event.buttons & SDL_BUTTON_LMASK) != 0;
    if (projection_mode_ == SatViewProjectionMode::Map)
    {
        glm::vec2 pixel_delta = event.delta;
        if (pixel_delta == glm::vec2(0.0f))
            pixel_delta = glm::vec2(event.pos - last_map_drag_pos_);
        last_map_drag_pos_ = event.pos;
        if (!left_button_down || pending_click_)
            return;

        pan_map(satview_map_pan_delta(
            pixel_delta,
            glm::vec2(viewport_.pixel_size)));
        return;
    }

    if (!left_button_down)
        return;
    if (camera_manipulator_->MouseMove(
            glm::vec2(event.pos),
            (event.mod & kModCtrl) != 0))
    {
        request_redraw();
    }
}

void SatViewHost::on_mouse_wheel(const MouseWheelEvent& event)
{
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        ImGui::GetIO().AddMouseWheelEvent(event.delta.x, event.delta.y);
        if (ImGui::GetIO().WantCaptureMouse)
        {
            request_redraw();
            return;
        }
    }

    if (projection_mode_ == SatViewProjectionMode::Map)
        return;

    const float current_distance = camera_->GetDistance();
    const float target_distance = std::clamp(
        current_distance * std::pow(0.88f, event.delta.y),
        camera_->GetMinDistance(),
        camera_->GetMaxDistance());
    camera_manipulator_->Dolly(current_distance - target_distance);
    request_redraw();
}

void SatViewHost::on_key(const KeyEvent& event)
{
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
            if (!event.pressed)
                camera_keys_->on_key(event);
            request_redraw();
            return;
        }
    }

    if (camera_keys_->on_key(event))
        request_redraw();
    if (!event.pressed)
        return;
    if (event.keycode == SDLK_F1 || event.scancode == SDL_SCANCODE_F1)
    {
        show_ui_panel_ = !show_ui_panel_;
        request_redraw();
        return;
    }
    if (event.keycode == SDLK_ESCAPE && selected_norad_catalog_id_.has_value())
    {
        selected_norad_catalog_id_.reset();
        simulation_settings_dirty_ = true;
        invalidate_visual_buffers();
        request_redraw();
        return;
    }
    if (event.keycode == SDLK_SPACE)
    {
        paused_ = !paused_;
        sync_simulation_controls();
        request_redraw();
        return;
    }
    if (event.keycode == SDLK_LEFTBRACKET)
    {
        time_speed_ = std::max(1.0f, time_speed_ * 0.5f);
        sync_simulation_controls();
        request_redraw();
        return;
    }
    if (event.keycode == SDLK_RIGHTBRACKET)
    {
        time_speed_ = std::min(3600.0f, time_speed_ * 2.0f);
        sync_simulation_controls();
        request_redraw();
        return;
    }
    if (event.keycode == SDLK_HOME)
    {
        reset_camera();
        return;
    }
    if (event.keycode == SDLK_R && (event.mod & kModCtrl) != 0)
    {
        catalog_service_.request_refresh();
        if (cloud_service_)
            cloud_service_->request_refresh();
        request_redraw();
        return;
    }
}

void SatViewHost::on_text_input(const TextInputEvent& event)
{
    if (!imgui_context_ || event.text.empty())
        return;

    ImGui::SetCurrentContext(imgui_context_);
    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharactersUTF8(event.text.c_str());
    if (io.WantTextInput || io.WantCaptureKeyboard)
        request_redraw();
}

void SatViewHost::on_focus_lost()
{
    dragging_ = false;
    pending_click_ = false;
    camera_keys_->reset();
    camera_manipulator_->Cancel();
}

bool SatViewHost::dispatch_action(std::string_view action)
{
    if (action == "toggle_host_ui" || action == "toggle_ui_panels" || action == "satview_toggle_ui")
    {
        show_ui_panel_ = !show_ui_panel_;
        request_redraw();
        return true;
    }
    if (action == "satview_pause" || action == "satview_toggle_pause")
    {
        paused_ = !paused_;
        sync_simulation_controls();
        request_redraw();
        return true;
    }
    if (action == "satview_time_slower")
    {
        time_speed_ = std::max(1.0f, time_speed_ * 0.5f);
        sync_simulation_controls();
        request_redraw();
        return true;
    }
    if (action == "satview_time_faster")
    {
        time_speed_ = std::min(3600.0f, time_speed_ * 2.0f);
        sync_simulation_controls();
        request_redraw();
        return true;
    }
    if (action == "satview_reset_camera")
    {
        reset_camera();
        return true;
    }
    if (action == "satview_refresh_catalog")
    {
        catalog_service_.request_refresh();
        if (cloud_service_)
            cloud_service_->request_refresh();
        request_redraw();
        return true;
    }
    if (action == "satview_clear_selection")
    {
        selected_norad_catalog_id_.reset();
        simulation_settings_dirty_ = true;
        invalidate_visual_buffers();
        request_redraw();
        return true;
    }
    if (action == "quit" || action == "request_quit")
    {
        running_ = false;
        if (callbacks_)
            callbacks_->request_quit();
        return true;
    }
    return false;
}

void SatViewHost::request_close()
{
    catalog_service_.stop();
    if (simulation_worker_)
        simulation_worker_->stop();
    running_ = false;
}

std::string SatViewHost::status_text() const
{
    const std::string view = projection_mode_ == SatViewProjectionMode::Map
        ? (camera_pov_ == SatViewCameraPov::Moon ? "moon map" : "earth map")
        : (camera_pov_ == SatViewCameraPov::Moon ? "moon" : "earth");
    const std::string mode = paused_ ? "satview paused " + view : "satview " + view;
    const std::string catalog_status = catalog_service_.status_text();
    auto snapshot = simulation_worker_ ? simulation_worker_->acquire_latest() : SatViewSnapshotExchange::ReadGuard{};
    const std::string propagation_status = snapshot ? snapshot->status_text : std::string{};
    if (catalog_status.empty() && propagation_status.empty())
        return mode;
    if (propagation_status.empty())
        return mode + " | " + catalog_status;
    if (catalog_status.empty())
        return mode + " | " + propagation_status;
    return mode + " | " + catalog_status + " | " + propagation_status;
}

Color SatViewHost::default_background() const
{
    return Color(0.005f, 0.008f, 0.018f, 1.0f);
}

HostRuntimeState SatViewHost::runtime_state() const
{
    HostRuntimeState state;
    state.content_ready = true;
    state.last_activity_time = last_activity_time_;
    return state;
}

HostDebugState SatViewHost::debug_state() const
{
    HostDebugState state;
    state.name = "SatView";
    state.grid_cols = 0;
    state.grid_rows = 0;
    state.dirty_cells = 0;
    return state;
}

void SatViewHost::attach_imgui_host(IImGuiHost& host)
{
    imgui_backend_ = &host;
    if (!imgui_context_)
        return;

    ImGui::SetCurrentContext(imgui_context_);
    host.initialize_imgui_backend();
    host.rebuild_imgui_font_texture();
}

void SatViewHost::set_imgui_font(const std::string& path, float size_pixels)
{
    imgui_font_path_ = path;
    imgui_font_size_pixels_ = size_pixels;
    if (!imgui_context_)
        return;

    ImGui::SetCurrentContext(imgui_context_);
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    if (!imgui_font_path_.empty() && imgui_font_size_pixels_ > 0.0f)
        io.Fonts->AddFontFromFileTTF(imgui_font_path_.c_str(), imgui_font_size_pixels_);
    if (io.Fonts->Fonts.empty())
        io.Fonts->AddFontDefault();
    if (imgui_backend_)
        imgui_backend_->rebuild_imgui_font_texture();
}

void SatViewHost::request_redraw()
{
    last_activity_time_ = std::chrono::steady_clock::now();
    if (callbacks_)
        callbacks_->request_frame();
}

void SatViewHost::invalidate_track_buffer()
{
    track_buffer_dirty_ = true;
}

void SatViewHost::invalidate_marker_buffer()
{
    marker_buffer_dirty_ = true;
}

void SatViewHost::invalidate_visual_buffers()
{
    invalidate_track_buffer();
    invalidate_marker_buffer();
}

SatViewConfig SatViewHost::current_config() const
{
    SatViewConfig config;
    config.filter = filter_;
    config.color_mode = color_mode_;
    config.track_display_mode = track_display_mode_;
    config.projection_mode = projection_mode_;
    config.camera_pov = camera_pov_;
    config.track_satellite_limit = track_satellite_limit_;
    config.track_sample_count = track_sample_count_;
    config.refresh_tracks_each_step = refresh_tracks_each_step_;
    config.marker_satellite_limit = marker_satellite_limit_;
    config.time_speed = time_speed_;
    config.clouds_enabled = clouds_enabled_;
    config.realistic_clouds_enabled = realistic_clouds_enabled_;
    config.atmosphere_enabled = atmosphere_enabled_;
    config.moon_enabled = moon_enabled_;
    config.moon_track_enabled = moon_track_enabled_;
    return config;
}

void SatViewHost::apply_config(const SatViewConfig& config)
{
    filter_ = config.filter;
    color_mode_ = config.color_mode;
    track_display_mode_ = config.track_display_mode;
    projection_mode_ = config.projection_mode;
    camera_pov_ = config.camera_pov;
    track_satellite_limit_ = config.track_satellite_limit;
    track_sample_count_ = config.track_sample_count;
    refresh_tracks_each_step_ = config.refresh_tracks_each_step;
    marker_satellite_limit_ = config.marker_satellite_limit;
    time_speed_ = config.time_speed;
    clouds_enabled_ = config.clouds_enabled;
    realistic_clouds_enabled_ = config.realistic_clouds_enabled;
    atmosphere_enabled_ = config.atmosphere_enabled;
    moon_enabled_ = config.moon_enabled || camera_pov_ == SatViewCameraPov::Moon;
    moon_track_enabled_ = config.moon_track_enabled;
    copy_to_buffer(search_buffer_, filter_.search_text);
    copy_to_buffer(object_type_buffer_, filter_.object_type_text);
    copy_to_buffer(source_buffer_, filter_.source_text);
}

void SatViewHost::persist_config()
{
    save_merged_satview_config(config_document_, current_config());
}

void SatViewHost::sync_simulation_controls()
{
    if (simulation_worker_)
        simulation_worker_->set_controls(time_speed_, paused_);
}

void SatViewHost::sync_simulation_render_settings()
{
    if (simulation_worker_)
    {
        simulation_worker_->set_render_settings(
            track_satellite_limit_,
            track_sample_count_,
            refresh_tracks_each_step_,
            selected_norad_catalog_id_);
    }
}

void SatViewHost::set_real_time()
{
    simulated_seconds_ = unix_seconds_now();
    last_draw_simulation_seconds_ = simulated_seconds_;
    time_speed_ = 1.0f;
    paused_ = false;
    if (simulation_worker_)
        simulation_worker_->set_clock(simulated_seconds_, time_speed_, paused_);
    request_redraw();
}

void SatViewHost::set_camera_pov(SatViewCameraPov pov, double simulation_seconds)
{
    if (camera_pov_ == pov)
        return;

    const float normalized_distance =
        camera_->GetDistance() / camera_target_radius(camera_pov_);
    camera_pov_ = pov;
    simulation_settings_dirty_ = true;
    if (camera_pov_ == SatViewCameraPov::Moon)
        moon_enabled_ = true;

    const SatViewMoonPosition moon = satview_moon_position(simulation_seconds);
    const float target_radius = camera_target_radius(camera_pov_);
    camera_->ClearMotion();
    camera_manipulator_->Cancel();
    camera_keys_->reset();
    camera_->SetDistanceLimits(
        camera_min_distance(camera_pov_),
        kCameraMaxDistanceCap);
    camera_->SetFocalPointAndDistance(
        camera_target_position(camera_pov_, moon),
        normalized_distance * target_radius);
    request_redraw();
}

void SatViewHost::reset_camera()
{
    if (projection_mode_ == SatViewProjectionMode::Map)
    {
        map_center_radians_ = glm::vec2(0.0f);
        request_redraw();
        return;
    }

    const double simulation_seconds = simulation_worker_
        ? simulation_worker_->current_simulation_seconds()
        : last_draw_simulation_seconds_;
    const glm::vec3 sun = glm::vec3(solar_direction_render(simulation_seconds));
    const SatViewMoonPosition moon = satview_moon_position(simulation_seconds);
    const glm::vec3 target = camera_target_position(camera_pov_, moon);
    const float target_radius = camera_target_radius(camera_pov_);
    camera_->ClearMotion();
    camera_->SetDistanceLimits(
        camera_min_distance(camera_pov_),
        kCameraMaxDistanceCap);
    camera_->SetPositionAndFocalPoint(
        target + camera_position_from_yaw_pitch(
                     std::atan2(sun.x, sun.z) + 0.65f,
                     0.25f,
                     std::min(kCameraDefaultDistance * target_radius, camera_->GetMaxDistance())),
        target);
    request_redraw();
}

void SatViewHost::pan_map(glm::vec2 delta_radians)
{
    map_center_radians_ = normalized_satview_map_center(map_center_radians_ + delta_radians);
    request_redraw();
}

void SatViewHost::rebuild_object_tree(const SatViewSimulationSnapshot* snapshot)
{
    if (!snapshot)
    {
        object_tree_catalog_generation_ = 0;
        object_tree_state_count_ = 0;
        object_tree_entries_.clear();
        filtered_object_tree_indices_.clear();
        return;
    }
    if (object_tree_catalog_generation_ == snapshot->catalog_generation
        && object_tree_state_count_ == snapshot->states.size())
    {
        return;
    }

    object_tree_catalog_generation_ = snapshot->catalog_generation;
    object_tree_state_count_ = snapshot->states.size();
    object_tree_entries_.clear();
    object_tree_entries_.reserve(snapshot->states.size());
    for (std::size_t state_index = 0; state_index < snapshot->states.size(); ++state_index)
    {
        const SatellitePropagatedState& state = snapshot->states[state_index];
        object_tree_entries_.push_back({
            state.orbit_class,
            normalized_satellite_prefix(state.object_name),
            object_tree_label(state),
            state.object_name,
            state.norad_catalog_id,
            state_index,
        });
    }

    std::sort(object_tree_entries_.begin(), object_tree_entries_.end(),
        [](const ObjectTreeEntry& a, const ObjectTreeEntry& b) {
            const int orbit_a = orbit_class_sort_key(a.orbit_class);
            const int orbit_b = orbit_class_sort_key(b.orbit_class);
            if (orbit_a != orbit_b)
                return orbit_a < orbit_b;
            if (a.prefix != b.prefix)
                return a.prefix < b.prefix;
            if (a.object_name != b.object_name)
                return a.object_name < b.object_name;
            return a.norad_catalog_id < b.norad_catalog_id;
        });
}

void SatViewHost::render_object_tree(const SatViewSimulationSnapshot* snapshot, bool& changed)
{
    rebuild_object_tree(snapshot);

    if (!snapshot)
    {
        ImGui::TextDisabled("Object tree pending catalog.");
        return;
    }

    filtered_object_tree_indices_.clear();
    filtered_object_tree_indices_.reserve(object_tree_entries_.size());
    const std::string_view source_label = snapshot->source_label;
    for (std::size_t entry_index = 0; entry_index < object_tree_entries_.size(); ++entry_index)
    {
        const ObjectTreeEntry& entry = object_tree_entries_[entry_index];
        if (entry.state_index < snapshot->states.size()
            && satellite_visible(filter_, snapshot->states[entry.state_index], source_label))
        {
            filtered_object_tree_indices_.push_back(entry_index);
        }
    }

    ImGui::Text("Objects: %zu filtered / %zu total",
        filtered_object_tree_indices_.size(),
        object_tree_entries_.size());
    if (filtered_object_tree_indices_.empty())
    {
        ImGui::TextDisabled("No objects match the current filters.");
        return;
    }

    const auto filtered_entry = [this](std::size_t filtered_index) -> const ObjectTreeEntry& {
        return object_tree_entries_[filtered_object_tree_indices_[filtered_index]];
    };
    const float tree_height = std::max(180.0f, ImGui::GetTextLineHeightWithSpacing() * 14.0f);
    if (!ImGui::BeginChild(
            "##satview_object_tree",
            ImVec2(0.0f, tree_height),
            ImGuiChildFlags_Border,
            ImGuiWindowFlags_AlwaysVerticalScrollbar))
    {
        ImGui::EndChild();
        return;
    }

    const ImGuiTreeNodeFlags group_flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    std::size_t orbit_begin = 0;
    while (orbit_begin < filtered_object_tree_indices_.size())
    {
        const OrbitClass orbit_class = filtered_entry(orbit_begin).orbit_class;
        std::size_t orbit_end = orbit_begin + 1;
        while (orbit_end < filtered_object_tree_indices_.size()
            && filtered_entry(orbit_end).orbit_class == orbit_class)
        {
            ++orbit_end;
        }

        const std::string_view orbit_name = orbit_class_name(orbit_class);
        ImGui::PushID(orbit_class_sort_key(orbit_class));
        const bool orbit_open = ImGui::TreeNodeEx(
            "##orbit",
            group_flags,
            "%.*s (%zu)",
            static_cast<int>(orbit_name.size()),
            orbit_name.data(),
            orbit_end - orbit_begin);
        if (orbit_open)
        {
            std::size_t prefix_begin = orbit_begin;
            while (prefix_begin < orbit_end)
            {
                const std::string& prefix = filtered_entry(prefix_begin).prefix;
                std::size_t prefix_end = prefix_begin + 1;
                while (prefix_end < orbit_end && filtered_entry(prefix_end).prefix == prefix)
                    ++prefix_end;

                ImGui::PushID(prefix.c_str());
                if (color_mode_ == SatViewColorMode::NamePrefix)
                {
                    const glm::vec4 prefix_color = satellite_prefix_color(stable_color_hash(prefix));
                    ImGui::PushStyleColor(
                        ImGuiCol_Text,
                        ImVec4(prefix_color.r, prefix_color.g, prefix_color.b, prefix_color.a));
                }
                const bool prefix_open = ImGui::TreeNodeEx(
                    "##prefix",
                    group_flags,
                    "%s (%zu)",
                    prefix.c_str(),
                    prefix_end - prefix_begin);
                if (color_mode_ == SatViewColorMode::NamePrefix)
                    ImGui::PopStyleColor();
                if (prefix_open)
                {
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(prefix_end - prefix_begin));
                    while (clipper.Step())
                    {
                        for (int local_index = clipper.DisplayStart; local_index < clipper.DisplayEnd; ++local_index)
                        {
                            const ObjectTreeEntry& entry =
                                filtered_entry(prefix_begin + static_cast<std::size_t>(local_index));
                            const bool selected = selected_norad_catalog_id_.has_value()
                                && entry.norad_catalog_id == *selected_norad_catalog_id_;
                            ImGui::PushID(entry.label.c_str());
                            if (ImGui::Selectable(entry.label.c_str(), selected))
                            {
                                selected_norad_catalog_id_ = entry.norad_catalog_id;
                                simulation_settings_dirty_ = true;
                                changed = true;
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                            ImGui::PopID();
                        }
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
                prefix_begin = prefix_end;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        orbit_begin = orbit_end;
    }

    ImGui::EndChild();
}

void SatViewHost::render_host_imgui(float dt, const SatViewSimulationSnapshot* snapshot)
{
    if (!imgui_context_ || !imgui_backend_)
        return;

    ImGui::SetCurrentContext(imgui_context_);
    imgui_backend_->begin_imgui_frame();
    ImGuiIO& io = ImGui::GetIO();
    const int pixel_w = std::max(1, viewport_.pixel_size.x);
    const int pixel_h = std::max(1, viewport_.pixel_size.y);
    io.DisplaySize = ImVec2(
        static_cast<float>(viewport_.pixel_pos.x + pixel_w),
        static_cast<float>(viewport_.pixel_pos.y + pixel_h));
    io.DeltaTime = dt > 0.0f ? dt : (1.0f / 60.0f);
    ImGui::NewFrame();

    if (show_ui_panel_)
        render_control_panel(snapshot);

    ImGui::Render();
}

void SatViewHost::render_control_panel(const SatViewSimulationSnapshot* snapshot)
{
    const int panel_x = viewport_.pixel_pos.x + 12;
    const int panel_y = viewport_.pixel_pos.y + 12;
    ImGui::SetNextWindowPos(ImVec2(static_cast<float>(panel_x), static_cast<float>(panel_y)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(kControlPanelDefaultWidth, kControlPanelDefaultHeight), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(kControlPanelMinWidth, kControlPanelMinHeight),
        ImVec2(100000.0f, 100000.0f));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("SatView", &show_ui_panel_, flags))
    {
        ImGui::End();
        return;
    }

    bool changed = false;
    const SatViewConfig config_before = current_config();
    auto set_control_width = [](const char* label) {
        ImGui::SetNextItemWidth(control_widget_width(label));
    };
    const double displayed_simulation_seconds = snapshot
        ? render_simulation_seconds(*snapshot)
        : last_draw_simulation_seconds_;

    ImGui::TextUnformatted("View");
    ImGui::SameLine();
    if (ImGui::RadioButton("Globe", projection_mode_ == SatViewProjectionMode::Globe))
    {
        projection_mode_ = SatViewProjectionMode::Globe;
        simulation_settings_dirty_ = true;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Map", projection_mode_ == SatViewProjectionMode::Map))
    {
        projection_mode_ = SatViewProjectionMode::Map;
        simulation_settings_dirty_ = true;
        camera_->ClearMotion();
        camera_manipulator_->Cancel();
        camera_keys_->reset();
        changed = true;
    }

    ImGui::TextUnformatted("POV");
    ImGui::SameLine();
    ImGui::PushID("camera_pov");
    if (ImGui::RadioButton("Earth", camera_pov_ == SatViewCameraPov::Earth))
    {
        set_camera_pov(SatViewCameraPov::Earth, displayed_simulation_seconds);
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Moon", camera_pov_ == SatViewCameraPov::Moon))
    {
        set_camera_pov(SatViewCameraPov::Moon, displayed_simulation_seconds);
        changed = true;
    }
    ImGui::PopID();

    if (ImGui::Button(paused_ ? "Resume" : "Pause"))
    {
        paused_ = !paused_;
        sync_simulation_controls();
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Real Time"))
        set_real_time();
    ImGui::SameLine();
    if (ImGui::Button("Reset Camera"))
    {
        reset_camera();
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh"))
    {
        catalog_service_.request_refresh();
        if (cloud_service_)
            cloud_service_->request_refresh();
        changed = true;
    }

    float speed = time_speed_;
    set_control_width("Speed");
    if (ImGui::SliderFloat("Speed", &speed, 1.0f, 3600.0f, "%.0fx", ImGuiSliderFlags_Logarithmic))
    {
        time_speed_ = std::clamp(speed, 1.0f, 3600.0f);
        sync_simulation_controls();
        changed = true;
    }

    const std::string local_time = format_local_simulation_time(displayed_simulation_seconds);
    ImGui::Text("Local time: %s", local_time.c_str());
    if (projection_mode_ == SatViewProjectionMode::Map)
    {
        ImGui::Text("Center lat: %.1f", glm::degrees(map_center_radians_.y));
        ImGui::Text("Center lon: %.1f", glm::degrees(map_center_radians_.x));
    }

    ImGui::SeparatorText("Objects");
    set_control_width("Search");
    if (ImGui::InputText("Search", search_buffer_, sizeof(search_buffer_)))
    {
        filter_.search_text = search_buffer_;
        changed = true;
    }
    set_control_width("Type");
    if (ImGui::InputText("Type", object_type_buffer_, sizeof(object_type_buffer_)))
    {
        filter_.object_type_text = object_type_buffer_;
        changed = true;
    }
    set_control_width("Source");
    if (ImGui::InputText("Source", source_buffer_, sizeof(source_buffer_)))
    {
        filter_.source_text = source_buffer_;
        changed = true;
    }

    float max_age_days = static_cast<float>(filter_.max_epoch_age_days);
    set_control_width("Age days");
    if (ImGui::DragFloat("Age days", &max_age_days, 0.1f, 0.0f, 30.0f, "%.1f"))
    {
        filter_.max_epoch_age_days = static_cast<double>(std::max(0.0f, max_age_days));
        changed = true;
    }

    changed |= ImGui::Checkbox("LEO", &filter_.show_low_earth);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("MEO", &filter_.show_medium_earth);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("GEO", &filter_.show_geosynchronous);
    changed |= ImGui::Checkbox("HEO", &filter_.show_highly_elliptical);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Other", &filter_.show_other);

    render_object_tree(snapshot, changed);

    ImGui::SeparatorText("Visuals");
    if (ImGui::Checkbox("Clouds", &clouds_enabled_))
        request_redraw();
    ImGui::SameLine();
    if (!clouds_enabled_)
        ImGui::BeginDisabled();
    if (ImGui::Checkbox("Realistic clouds", &realistic_clouds_enabled_))
        request_redraw();
    if (!clouds_enabled_)
        ImGui::EndDisabled();
    if (ImGui::Checkbox("Atmosphere", &atmosphere_enabled_))
        request_redraw();
    ImGui::SameLine();
    if (camera_pov_ == SatViewCameraPov::Moon)
        ImGui::BeginDisabled();
    if (ImGui::Checkbox("Moon", &moon_enabled_))
        request_redraw();
    if (camera_pov_ == SatViewCameraPov::Moon)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Checkbox("Moon track", &moon_track_enabled_))
    {
        track_buffer_dirty_ = true;
        request_redraw();
    }

    int color_mode_index = static_cast<int>(color_mode_);
    const char* color_modes[] = { "Name Prefix", "Orbit Class", "Object Type" };
    set_control_width("Color");
    if (ImGui::Combo("Color", &color_mode_index, color_modes, 3))
    {
        color_mode_ = static_cast<SatViewColorMode>(color_mode_index);
        changed = true;
    }

    int track_display_index = track_display_mode_ == SatViewTrackDisplayMode::SelectedOnly ? 1 : 0;
    const char* track_display_modes[] = { "All Sampled", "Selected Only" };
    set_control_width("Paths");
    if (ImGui::Combo("Paths", &track_display_index, track_display_modes, 2))
    {
        track_display_mode_ = track_display_index == 1
            ? SatViewTrackDisplayMode::SelectedOnly
            : SatViewTrackDisplayMode::AllSampled;
        changed = true;
    }

    const std::size_t available_track_count = snapshot ? snapshot->states.size() : 0;
    const std::size_t track_limit_ceiling = available_track_count > 0
        ? available_track_count
        : std::max(track_satellite_limit_, kDefaultTrackSatelliteLimit);
    const int maximum_track_limit = static_cast<int>(std::min(
        track_limit_ceiling,
        static_cast<std::size_t>(std::numeric_limits<int>::max())));
    const int minimum_track_limit = 1;
    int track_limit = std::clamp(
        static_cast<int>(std::min(
            track_satellite_limit_,
            static_cast<std::size_t>(std::numeric_limits<int>::max()))),
        minimum_track_limit,
        maximum_track_limit);
    set_control_width("Track count");
    if (ImGui::SliderInt("Track count", &track_limit, minimum_track_limit, maximum_track_limit))
    {
        track_satellite_limit_ = static_cast<std::size_t>(track_limit);
        simulation_settings_dirty_ = true;
        changed = true;
    }

    int track_samples = static_cast<int>(track_sample_count_);
    set_control_width("Track samples");
    if (ImGui::SliderInt("Track samples", &track_samples, 12,
            static_cast<int>(kMaximumTrackSampleCount)))
    {
        track_sample_count_ = static_cast<std::size_t>(std::max(12, track_samples));
        simulation_settings_dirty_ = true;
        changed = true;
    }

    if (ImGui::Checkbox("Refresh paths every step", &refresh_tracks_each_step_))
    {
        simulation_settings_dirty_ = true;
        changed = true;
    }

    static constexpr std::size_t kMarkerLimitValues[] = { 0, 8192, 4096, 2048, 1024, 512 };
    static constexpr const char* kMarkerLimitLabels[] = { "All", "8192", "4096", "2048", "1024", "512" };
    int marker_limit_index = 0;
    for (int i = 0; i < 6; ++i)
    {
        if (marker_satellite_limit_ == kMarkerLimitValues[i])
        {
            marker_limit_index = i;
            break;
        }
    }
    set_control_width("Marker cap");
    if (ImGui::Combo("Marker cap", &marker_limit_index, kMarkerLimitLabels, 6))
    {
        marker_satellite_limit_ = kMarkerLimitValues[marker_limit_index];
        changed = true;
    }

    ImGui::SeparatorText("Catalog");
    const std::string catalog_status = catalog_service_.status_text();
    ImGui::TextWrapped("%s", catalog_status.empty() ? "catalog pending" : catalog_status.c_str());
    if (cloud_service_)
    {
        const std::string cloud_status = cloud_service_->status_text();
        ImGui::TextWrapped("Clouds: %s", cloud_status.c_str());
        ImGui::TextDisabled("Contains modified EUMETSAT data");
    }
    const std::string_view source_label = snapshot ? std::string_view(snapshot->source_label) : std::string_view{};
    if (source_label.empty())
        ImGui::TextDisabled("Source: pending");
    else
        ImGui::Text("Source: %.*s", static_cast<int>(source_label.size()), source_label.data());
    const std::size_t filtered_markers = filtered_object_tree_indices_.size();
    const std::size_t rendered_markers = marker_satellite_limit_ == 0
        ? filtered_markers
        : std::min(filtered_markers, marker_satellite_limit_);
    const std::size_t total_markers = snapshot ? snapshot->states.size() : 0;
    const std::size_t total_tracks = (snapshot && snapshot->tracks) ? snapshot->tracks->size() : 0;
    ImGui::Text("Markers: %zu / %zu", rendered_markers, total_markers);
    ImGui::Text("Paths: %zu / %zu", visible_track_count(snapshot), total_tracks);

    ImGui::SeparatorText("Selection");
    if (const SatellitePropagatedState* selected = selected_satellite(snapshot))
    {
        ImGui::TextWrapped("%s", selected->object_name.c_str());
        ImGui::Text("NORAD: %lld", static_cast<long long>(selected->norad_catalog_id));
        if (!selected->object_id.empty())
            ImGui::Text("Object ID: %s", selected->object_id.c_str());
        ImGui::Text("Orbit: %.*s",
            static_cast<int>(orbit_class_name(selected->orbit_class).size()),
            orbit_class_name(selected->orbit_class).data());
        if (!selected->object_type.empty())
            ImGui::Text("Type: %s", selected->object_type.c_str());
        ImGui::Text("Kind: %.*s",
            static_cast<int>(satellite_object_kind_name(selected->object_kind).size()),
            satellite_object_kind_name(selected->object_kind).data());
        if (!selected->classification_type.empty())
            ImGui::Text("Class: %s", selected->classification_type.c_str());
        ImGui::Text("Period: %.1f min", selected->period_minutes);
        ImGui::Text("Epoch age: %.1f h", selected->minutes_since_epoch / 60.0);
        const SatViewGeodeticPosition geodetic =
            satview_geodetic_from_ecef(selected->ecef_position_km);
        ImGui::Text("Latitude: %.3f %c",
            std::abs(geodetic.latitude_degrees),
            geodetic.latitude_degrees >= 0.0 ? 'N' : 'S');
        ImGui::Text("Longitude: %.3f %c",
            std::abs(geodetic.longitude_degrees),
            geodetic.longitude_degrees >= 0.0 ? 'E' : 'W');
        const double altitude_km = glm::length(selected->teme_position_km) - kSatViewEarthEquatorialRadiusKm;
        ImGui::Text("Altitude: %.0f km", altitude_km);
        const double speed_km_s = glm::length(selected->teme_velocity_km_per_s);
        ImGui::Text("Speed: %.2f km/s", speed_km_s);
        if (ImGui::Button("Clear Selection"))
        {
            selected_norad_catalog_id_.reset();
            simulation_settings_dirty_ = true;
            changed = true;
        }
    }
    else
    {
        ImGui::TextDisabled("No satellite selected.");
    }

    if (changed)
    {
        invalidate_visual_buffers();
        clear_selection_if_missing(snapshot);
        request_redraw();
    }

    if (current_config() != config_before)
        config_dirty_ = true;
    if (config_dirty_ && !ImGui::IsAnyItemActive())
    {
        persist_config();
        config_dirty_ = false;
    }

    ImGui::End();
}

std::size_t SatViewHost::visible_track_count(const SatViewSimulationSnapshot* snapshot) const
{
    std::size_t count = moon_track_enabled_
            && !(projection_mode_ == SatViewProjectionMode::Map
                && camera_pov_ == SatViewCameraPov::Moon)
        ? 1
        : 0;
    if (!snapshot || !snapshot->tracks)
        return count;

    const std::string_view source_label = snapshot->source_label;
    for (const SatelliteOrbitTrack& track : *snapshot->tracks)
    {
        if (track_display_mode_ == SatViewTrackDisplayMode::SelectedOnly
            && (!selected_norad_catalog_id_.has_value()
                || track.norad_catalog_id != *selected_norad_catalog_id_))
        {
            continue;
        }

        if (satellite_visible(filter_, track, source_label))
            ++count;
    }
    return count;
}

const SatellitePropagatedState* SatViewHost::selected_satellite(const SatViewSimulationSnapshot* snapshot) const
{
    if (!snapshot || !selected_norad_catalog_id_.has_value())
        return nullptr;

    for (const SatellitePropagatedState& state : snapshot->states)
    {
        if (state.norad_catalog_id == *selected_norad_catalog_id_)
            return &state;
    }
    return nullptr;
}

void SatViewHost::clear_selection_if_missing(const SatViewSimulationSnapshot* snapshot)
{
    if (selected_norad_catalog_id_.has_value() && snapshot && !selected_satellite(snapshot))
    {
        selected_norad_catalog_id_.reset();
        simulation_settings_dirty_ = true;
        invalidate_visual_buffers();
    }
}

void SatViewHost::select_nearest_satellite(const glm::ivec2& screen_pos)
{
    auto snapshot_guard = simulation_worker_ ? simulation_worker_->acquire_latest() : SatViewSnapshotExchange::ReadGuard{};
    const SatViewSimulationSnapshot* snapshot = snapshot_guard.get();
    if (!snapshot || snapshot->states.empty())
        return;

    const int pixel_w = std::max(1, viewport_.pixel_size.x);
    const int pixel_h = std::max(1, viewport_.pixel_size.y);
    const double render_seconds = render_simulation_seconds(*snapshot);
    const SatViewMoonPosition moon = satview_moon_position(render_seconds);
    const float earth_scene_radius =
        visible_scene_radius(snapshot, filter_, selected_norad_catalog_id_, track_display_mode_);
    const float scene_radius = camera_scene_radius(
        earth_scene_radius,
        moon,
        camera_pov_,
        moon_enabled_);
    const glm::mat4 view = camera_view_matrix(*camera_);
    const glm::mat4 proj = glm::perspectiveRH_ZO(
        glm::radians(camera_->GetFieldOfView()),
        static_cast<float>(pixel_w) / static_cast<float>(pixel_h),
        camera_near_plane(camera_pov_, camera_->GetDistance()),
        camera_far_plane(camera_->GetDistance(), scene_radius));
    const glm::mat4 view_proj = proj * view;
    const std::string_view source_label = snapshot->source_label;

    std::optional<std::int64_t> nearest_id;
    float nearest_distance_sq = static_cast<float>(
        kClickSelectionMaxDistancePixels * kClickSelectionMaxDistancePixels);
    std::size_t visible_marker_index = 0;
    for (std::size_t state_index = 0; state_index < snapshot->states.size(); ++state_index)
    {
        const SatellitePropagatedState& state = snapshot->states[state_index];
        if (!satellite_visible(filter_, state, source_label))
            continue;

        if (marker_satellite_limit_ != 0 && visible_marker_index >= marker_satellite_limit_)
            break;
        ++visible_marker_index;

        glm::vec3 ndc;
        const glm::dvec3 teme_position =
            interpolated_teme_position(*snapshot, state_index, render_seconds);
        if (projection_mode_ == SatViewProjectionMode::Map)
        {
            const glm::vec2 map_position =
                satview_map_position_from_teme(
                    teme_position,
                    render_seconds,
                    map_center_radians_,
                    camera_pov_ == SatViewCameraPov::Moon
                        ? SatViewMapBody::Moon
                        : SatViewMapBody::Earth,
                    moon.render_position_earth_radii);
            ndc = glm::vec3(map_position, 0.2f);
        }
        else
        {
            const glm::vec3 world = to_vec3(teme_position_to_render_earth_radii(teme_position));
            const glm::vec4 clip = view_proj * glm::vec4(world, 1.0f);
            if (clip.w <= 0.0f)
                continue;
            ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1.1f || ndc.x > 1.1f || ndc.y < -1.1f || ndc.y > 1.1f
                || ndc.z < 0.0f || ndc.z > 1.0f)
            {
                continue;
            }
        }

        const float sx = static_cast<float>(viewport_.pixel_pos.x)
            + (ndc.x * 0.5f + 0.5f) * static_cast<float>(pixel_w);
        const float sy = static_cast<float>(viewport_.pixel_pos.y)
            + (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(pixel_h);
        const glm::vec2 delta = glm::vec2(sx, sy) - glm::vec2(screen_pos);
        const float distance_sq = glm::dot(delta, delta);
        if (distance_sq < nearest_distance_sq)
        {
            nearest_distance_sq = distance_sq;
            nearest_id = state.norad_catalog_id;
        }
    }

    if (nearest_id != selected_norad_catalog_id_)
    {
        selected_norad_catalog_id_ = nearest_id;
        sync_simulation_render_settings();
        invalidate_visual_buffers();
    }
    request_redraw();
}

std::unique_ptr<IHost> create_satview_host()
{
    return std::make_unique<SatViewHost>();
}

void register_satview_host_provider(HostProviderRegistry& registry)
{
    registry.register_provider(HostKind::SatView, create_satview_host);
}

} // namespace draxul::satview
