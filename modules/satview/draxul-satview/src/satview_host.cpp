#include <draxul/satview/satview_host.h>

#include "camera.h"
#include "camera_manipulator.h"
#include "satview_camera_key_state.h"
#include "satview_cloud_service.h"
#include "satview_constellation_boundary_catalog.h"
#include "satview_constellation_catalog.h"
#include "satview_geodetic.h"
#include "satview_ground_view.h"
#include "satview_label_layout.h"
#include "satview_landscape.h"
#include "satview_lunar_frame.h"
#include "satview_lunar_surface_catalog.h"
#include "satview_map_projection.h"
#include "satview_moon_ephemeris.h"
#include "satview_object_style.h"
#include "satview_scene_pass.h"
#include "satview_simulation_worker.h"
#include "satview_sky_projection.h"
#include "satview_solar_system.h"
#include "satview_star_catalog.h"
#include "satview_sun_ephemeris.h"
#include "satview_time_format.h"

#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <draxul/config_document.h>
#include <draxul/host_registry.h>
#include <draxul/imgui_host.h>
#include <draxul/log.h>
#include <draxul/sdl_imgui_input.h>
#include <draxul/text_atlas_builder.h>
#include <draxul/text_service.h>
#include <draxul/unicode.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <limits>
#include <numbers>
#include <string_view>
#include <unordered_map>
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
constexpr const char* kSatViewDockspaceName = "SatViewDockspace";
constexpr const char* kSatViewSceneWindowName = "Scene";
constexpr const char* kSatViewViewWindowName = "View";
constexpr const char* kSatViewRenderingWindowName = "Rendering";
constexpr const char* kSatViewFilterWindowName = "Filter";
constexpr const char* kSatViewSelectionWindowName = "Selection";
constexpr const char* kSatViewAboutWindowName = "About";
constexpr int kClickSelectionMaxDistancePixels = 18;
constexpr int kClickDragSlopPixels = 5;
constexpr float kCameraDefaultDistance = 3.6f;
constexpr float kCameraMinDistance = 1.7f;
constexpr float kCameraDefaultMaxDistance = 12.0f;
constexpr float kCameraMaxDistanceCap = 100000.0f;
constexpr float kCameraFitRadiusScale = 3.1f;
constexpr float kMoonCameraSurfaceDistanceScale = 1.03f;
constexpr float kSunCameraSurfaceDistanceScale = 1.015f;
constexpr float kCameraDefaultNearPlane = 0.05f;
constexpr float kCameraMinNearPlane = 0.0005f;
constexpr float kCameraHorizontalOrbitRadiansPerSecond = 1.8f;
constexpr float kCameraVerticalOrbitRadiansPerSecond = 0.9f;
constexpr float kCameraZoomRatePerSecond = 1.35f;
constexpr float kCameraInputMaxDeltaSeconds = 0.1f;
constexpr float kGroundLookRadiansPerPixel = 0.005f;
constexpr float kGroundKeyboardYawRadiansPerSecond = 1.4f;
constexpr float kGroundKeyboardPitchRadiansPerSecond = 0.8f;
constexpr float kGroundMinimumFovDegrees = 20.0f;
constexpr float kGroundMinimumMarkerScale = 0.05f;
constexpr float kGroundMaximumMarkerScale = 2.0f;
constexpr float kGroundObserverAltitudeEarthRadii = 0.0003f;
constexpr glm::vec4 kConstellationBoundaryColor(1.0f, 0.08f, 0.06f, 0.82f);
constexpr float kMoonRadiusEarthRadii = static_cast<float>(kSatViewMoonMeanRadiusKm / kSatViewEarthEquatorialRadiusKm);
constexpr float kSunRadiusEarthRadii = static_cast<float>(kSatViewSunMeanRadiusKm / kSatViewEarthEquatorialRadiusKm);
constexpr float kEarthOrbitMaximumRadiusEarthRadii = static_cast<float>(
    1.02 * kSatViewAstronomicalUnitKm / kSatViewEarthEquatorialRadiusKm);
constexpr float kCelestialOverlayDistanceEarthRadii = 48.0f;
constexpr float kLunarSurfaceMarkerRadiusScale = 1.001f;
constexpr float kLunarSurfaceMarkerSizeScale = 0.04f;
constexpr float kLunarSurfaceChildExpansionDistanceScale = 3.0f;

std::string constellation_label_key(std::uint32_t area_index)
{
    return "constellation:" + std::to_string(area_index);
}

std::string cardinal_label_key(char cardinal)
{
    return std::string("cardinal:") + cardinal;
}

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

// Reversed-Z projection: swapping near/far maps the near plane to depth 1 and
// the far plane to depth 0, so float32 depth precision stays near-logarithmic
// over the huge near/far ratios a solar-system scene needs. Both renderer
// backends clear depth to 0 and compare GreaterEqual to match.
glm::mat4 reversed_z_perspective(float fovy_radians, float aspect, float near_plane, float far_plane)
{
    return glm::perspectiveRH_ZO(fovy_radians, aspect, far_plane, near_plane);
}

float camera_target_radius(SatViewCameraPov pov)
{
    return pov == SatViewCameraPov::Moon ? kMoonRadiusEarthRadii : 1.0f;
}

float camera_min_distance(SatViewCameraPov pov)
{
    if (pov == SatViewCameraPov::Earth)
        return kCameraMinDistance;
    if (pov == SatViewCameraPov::Moon)
        return kMoonCameraSurfaceDistanceScale * kMoonRadiusEarthRadii;
    return kSunCameraSurfaceDistanceScale;
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

float clamp_ground_marker_scale(float scale)
{
    return std::clamp(
        scale,
        kGroundMinimumMarkerScale,
        kGroundMaximumMarkerScale);
}

glm::vec2 screen_ndc(glm::ivec2 screen_pos, const HostViewport& viewport)
{
    const glm::vec2 local = glm::vec2(screen_pos - viewport.pixel_pos);
    const glm::vec2 size = glm::max(glm::vec2(viewport.pixel_size), glm::vec2(1.0f));
    return glm::vec2(
        local.x / size.x * 2.0f - 1.0f,
        1.0f - local.y / size.y * 2.0f);
}

bool viewport_contains(const HostViewport& viewport, glm::ivec2 position)
{
    const glm::ivec2 end = viewport.pixel_pos + viewport.pixel_size;
    return position.x >= viewport.pixel_pos.x
        && position.y >= viewport.pixel_pos.y
        && position.x < end.x
        && position.y < end.y;
}

bool imgui_mouse_targets_scene(const HostViewport& scene_viewport, glm::ivec2 position)
{
    if (!viewport_contains(scene_viewport, position))
        return false;
    ImGuiContext* context = ImGui::GetCurrentContext();
    return !context
        || !context->IO.WantCaptureMouse
        || (context->HoveredWindow
            && std::strcmp(context->HoveredWindow->Name, kSatViewSceneWindowName) == 0);
}

std::optional<glm::dvec3> ray_sphere_hit(
    const glm::dvec3& origin,
    const glm::dvec3& direction,
    double radius)
{
    const double b = glm::dot(origin, direction);
    const double c = glm::dot(origin, origin) - radius * radius;
    const double discriminant = b * b - c;
    if (discriminant < 0.0)
        return std::nullopt;

    const double root = std::sqrt(discriminant);
    double t = -b - root;
    if (t < 0.0)
        t = -b + root;
    if (t < 0.0)
        return std::nullopt;
    return origin + direction * t;
}

glm::vec3 camera_target_position(
    SatViewCameraPov pov,
    const SatViewMoonPosition& moon,
    const SatViewSunPosition& sun)
{
    if (pov == SatViewCameraPov::Moon)
        return glm::vec3(moon.render_position_earth_radii);
    return glm::vec3(0.0f);
}

bool moon_orbit_track_visible(
    bool enabled,
    SatViewSatelliteDisplayMode display_mode,
    SatViewProjectionMode projection_mode,
    SatViewCameraPov camera_pov)
{
    return enabled
        && display_mode != SatViewSatelliteDisplayMode::MarkersOnly
        && projection_mode != SatViewProjectionMode::Ground
        && camera_pov == SatViewCameraPov::Earth;
}

bool earth_orbit_track_visible(
    bool enabled,
    SatViewSatelliteDisplayMode display_mode,
    SatViewProjectionMode projection_mode,
    SatViewCameraPov camera_pov)
{
    return enabled
        && display_mode != SatViewSatelliteDisplayMode::MarkersOnly
        && projection_mode != SatViewProjectionMode::Ground
        && camera_pov == SatViewCameraPov::Sun;
}

float camera_scene_radius(
    float earth_scene_radius,
    const SatViewMoonPosition& moon,
    const SatViewSunPosition& sun,
    SatViewCameraPov pov,
    bool moon_visible,
    bool sun_visible,
    bool earth_track_visible)
{
    const glm::vec3 target = camera_target_position(pov, moon, sun);
    float radius = camera_target_radius(pov);
    radius = std::max(radius, glm::length(target) + earth_scene_radius);
    if (moon_visible)
    {
        radius = std::max(
            radius,
            glm::length(glm::vec3(moon.render_position_earth_radii) - target)
                + kMoonRadiusEarthRadii);
    }
    if (sun_visible)
    {
        radius = std::max(
            radius,
            glm::length(glm::vec3(sun.render_position_earth_radii) - target)
                + kSunRadiusEarthRadii);
    }
    if (earth_track_visible)
    {
        radius = std::max(
            radius,
            glm::length(glm::vec3(sun.render_position_earth_radii) - target)
                + kEarthOrbitMaximumRadiusEarthRadii);
    }
    return radius;
}

const char* camera_pov_name(SatViewCameraPov pov)
{
    return satview_solar_system_body(pov).name.data();
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

glm::vec4 population_color(SatellitePopulation population, float alpha)
{
    switch (population)
    {
    case SatellitePopulation::ActivePayload:
        return glm::vec4(0.18f, 0.88f, 1.00f, alpha);
    case SatellitePopulation::InactivePayload:
        return glm::vec4(0.48f, 0.55f, 0.72f, alpha);
    case SatellitePopulation::RocketBody:
        return glm::vec4(1.00f, 0.58f, 0.18f, alpha);
    case SatellitePopulation::Debris:
        return glm::vec4(1.00f, 0.28f, 0.36f, alpha);
    case SatellitePopulation::Unknown:
        return glm::vec4(0.72f, 0.78f, 0.86f, alpha);
    }
    return glm::vec4(0.72f, 0.78f, 0.86f, alpha);
}

glm::vec4 satellite_color(
    OrbitClass orbit_class,
    SatelliteObjectKind object_kind,
    SatellitePopulation population,
    std::uint32_t object_prefix_hash,
    SatViewColorMode color_mode,
    float alpha)
{
    switch (color_mode)
    {
    case SatViewColorMode::Population:
        return population_color(population, alpha);
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

int population_sort_key(SatellitePopulation population)
{
    switch (population)
    {
    case SatellitePopulation::ActivePayload:
        return 0;
    case SatellitePopulation::InactivePayload:
        return 1;
    case SatellitePopulation::RocketBody:
        return 2;
    case SatellitePopulation::Debris:
        return 3;
    case SatellitePopulation::Unknown:
        return 4;
    }
    return 4;
}

std::string object_tree_label(const SatellitePropagatedState& state)
{
    std::string label = std::to_string(state.norad_catalog_id);
    const SatelliteStaticMetadata* metadata = state.metadata.get();
    if (metadata && !metadata->object_name.empty())
    {
        label += " - ";
        label += metadata->object_name;
    }
    if (metadata && !metadata->object_id.empty())
    {
        label += " [";
        label += metadata->object_id;
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

std::string object_tree_label(const SatelliteRecord& record)
{
    std::string label = std::to_string(record.norad_catalog_id);
    if (!record.object_name.empty())
    {
        label += " - ";
        label += record.object_name;
    }
    if (!record.object_id.empty())
    {
        label += " [";
        label += record.object_id;
        label += "]";
    }
    if (!record.renderable)
        label += " (catalog only)";
    return label;
}

void append_ground_track_arc(
    std::vector<SatViewSceneVertex>& vertices,
    const glm::dvec3& start,
    const glm::dvec3& end,
    glm::vec4 color,
    const glm::dvec3& observer_render_position,
    bool ground_horizon_occlusion)
{
    const std::size_t subdivisions = satview_ground_track_subdivision_count(
        start,
        end,
        observer_render_position);
    glm::dvec3 previous = start;
    for (std::size_t step = 1; step <= subdivisions; ++step)
    {
        const glm::dvec3 current = satview_interpolate_track_arc(
            start,
            end,
            static_cast<double>(step) / static_cast<double>(subdivisions));
        if (!ground_horizon_occlusion
            || (satview_ground_visibility_dot(previous, observer_render_position) > 0.0
                && satview_ground_visibility_dot(current, observer_render_position) > 0.0))
        {
            append_line(vertices, to_vec3(previous), to_vec3(current), color);
        }
        previous = current;
    }
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

bool satellite_display_shows_tracks(SatViewSatelliteDisplayMode mode)
{
    return mode != SatViewSatelliteDisplayMode::MarkersOnly;
}

bool satellite_display_shows_markers(SatViewSatelliteDisplayMode mode)
{
    return mode != SatViewSatelliteDisplayMode::TracksOnly;
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
    SatViewCameraPov camera_pov,
    double simulation_seconds,
    std::optional<glm::dvec3> ground_observer_render_position,
    bool ground_horizon_occlusion)
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

        const float fidelity_alpha = track.solution_kind == OrbitSolutionKind::SatcatSummaryEstimate
            ? 0.55f
            : 1.0f;
        const glm::vec4 color = selected
            ? glm::mix(
                  satellite_color(track.orbit_class, track.object_kind, track.population,
                      track.object_prefix_hash, color_mode, 0.98f * fidelity_alpha),
                  glm::vec4(1.0f, 1.0f, 1.0f, 0.98f * fidelity_alpha),
                  0.38f)
            : satellite_color(track.orbit_class, track.object_kind, track.population,
                  track.object_prefix_hash, color_mode, 0.62f * fidelity_alpha);
        const bool earth_ground_track = projection_mode == SatViewProjectionMode::Map
            && camera_pov == SatViewCameraPov::Earth;
        const auto& points = earth_ground_track
            ? track.render_points_earth_radii
            : track.render_teme_points_earth_radii;
        if (points.size() < 2)
            continue;
        const glm::dvec3 track_render_offset = earth_ground_track
            ? glm::dvec3(0.0)
            : teme_position_to_render_earth_radii(
                  satellite_track_anchor_offset_km(track, simulation_seconds));
        for (std::size_t i = 1; i < points.size(); ++i)
        {
            const glm::dvec3 start = points[i - 1] + track_render_offset;
            const glm::dvec3 end = points[i] + track_render_offset;
            if (ground_horizon_occlusion
                && ground_observer_render_position.has_value()
                && (satview_ground_visibility_dot(start, *ground_observer_render_position) <= 0.0
                    || satview_ground_visibility_dot(end, *ground_observer_render_position) <= 0.0))
            {
                continue;
            }
            if (ground_observer_render_position.has_value())
            {
                append_ground_track_arc(
                    vertices,
                    start,
                    end,
                    color,
                    *ground_observer_render_position,
                    ground_horizon_occlusion);
            }
            else
            {
                append_line(
                    vertices,
                    to_vec3(start),
                    to_vec3(end),
                    color,
                    earth_ground_track);
            }
        }
        const glm::dvec3 closing_start = points.back() + track_render_offset;
        const glm::dvec3 closing_end = points.front() + track_render_offset;
        const bool closing_segment_visible = !ground_horizon_occlusion
            || !ground_observer_render_position.has_value()
            || (satview_ground_visibility_dot(closing_start, *ground_observer_render_position) > 0.0
                && satview_ground_visibility_dot(closing_end, *ground_observer_render_position) > 0.0);
        // Sampled ephemerides describe a finite time window, not necessarily one
        // complete revolution. Joining their endpoints invents a chord through the
        // orbit (and sometimes the central body).
        const bool closed_orbit = track.solution_kind != OrbitSolutionKind::SampledEphemeris;
        if (closed_orbit && !earth_ground_track && closing_segment_visible)
        {
            const glm::vec4 closing_color = color * glm::vec4(1.0f, 1.0f, 1.0f, 0.82f);
            if (ground_observer_render_position.has_value())
            {
                append_ground_track_arc(
                    vertices,
                    closing_start,
                    closing_end,
                    closing_color,
                    *ground_observer_render_position,
                    ground_horizon_occlusion);
            }
            else
            {
                append_line(vertices, to_vec3(closing_start), to_vec3(closing_end), closing_color);
            }
        }
    }
}

void append_moon_track_vertices(
    std::vector<SatViewSceneVertex>& vertices,
    double center_seconds,
    std::size_t segment_count)
{
    const glm::vec4 color(0.92f, 0.86f, 0.66f, 0.82f);
    const std::vector<glm::dvec3> points = satview_moon_orbit_track(center_seconds, segment_count);
    if (points.size() < 2)
        return;
    for (std::size_t i = 1; i < points.size(); ++i)
        append_line(vertices, to_vec3(points[i - 1]), to_vec3(points[i]), color);
    append_line(vertices, to_vec3(points.back()), to_vec3(points.front()), color);
}

void append_earth_track_vertices(
    std::vector<SatViewSceneVertex>& vertices,
    double center_seconds,
    std::size_t segment_count)
{
    const glm::vec4 color(0.30f, 0.68f, 1.00f, 0.88f);
    const std::vector<glm::dvec3> points = satview_earth_orbit_track(center_seconds, segment_count);
    if (points.size() < 2)
        return;
    for (std::size_t i = 1; i < points.size(); ++i)
        append_line(vertices, to_vec3(points[i - 1]), to_vec3(points[i]), color);
    append_line(vertices, to_vec3(points.back()), to_vec3(points.front()), color);
}

float solar_system_scene_radius(SatViewCameraPov pov)
{
    const SatViewSolarSystemBody& parent = satview_solar_system_body(pov);
    float radius = 1.0f;
    for (const SatViewSolarSystemBody* child : satview_child_bodies(pov))
    {
        const double apoapsis_km = child->semi_major_axis_km * (1.0 + child->eccentricity);
        const double child_radius = child->equatorial_radius_km / parent.equatorial_radius_km;
        radius = std::max(
            radius,
            static_cast<float>(apoapsis_km / parent.equatorial_radius_km + child_radius));
    }
    return radius;
}

void append_natural_satellite_tracks(
    std::vector<SatViewSceneVertex>& vertices,
    SatViewCameraPov parent_id,
    const SatViewPlanetTrackConfig& planet_tracks,
    std::size_t segment_count)
{
    for (const SatViewBodyOrbitTrack& track :
        satview_child_orbit_tracks(parent_id, planet_tracks, segment_count))
    {
        for (std::size_t index = 1; index < track.points_focus_radii.size(); ++index)
        {
            append_line(
                vertices,
                to_vec3(track.points_focus_radii[index - 1]),
                to_vec3(track.points_focus_radii[index]),
                track.color);
        }
        append_line(
            vertices,
            to_vec3(track.points_focus_radii.back()),
            to_vec3(track.points_focus_radii.front()),
            track.color * glm::vec4(1.0f, 1.0f, 1.0f, 0.82f));
    }
}

void append_planetary_ring_tracks(
    std::vector<SatViewSceneVertex>& vertices,
    SatViewCameraPov body_id)
{
    if (body_id != SatViewCameraPov::Uranus)
        return;

    constexpr std::size_t ring_count = 9;
    constexpr double inner_radius = 1.57;
    constexpr double outer_radius = 2.00;
    constexpr std::size_t kSegments = 128;
    for (std::size_t ring = 0; ring < ring_count; ++ring)
    {
        const double t = ring_count == 1
            ? 0.0
            : static_cast<double>(ring) / static_cast<double>(ring_count - 1);
        const double radius = glm::mix(inner_radius, outer_radius, t);
        constexpr float band = 0.34f;
        const glm::vec4 color(0.52f, 0.72f, 0.75f, band);
        glm::vec3 previous(static_cast<float>(radius), 0.0f, 0.0f);
        for (std::size_t segment = 1; segment <= kSegments; ++segment)
        {
            const double angle = 2.0 * std::numbers::pi
                * static_cast<double>(segment) / static_cast<double>(kSegments);
            const glm::vec3 current(
                static_cast<float>(radius * std::cos(angle)),
                0.0f,
                static_cast<float>(radius * std::sin(angle)));
            append_line(vertices, previous, current, color);
            previous = current;
        }
    }
}

void append_natural_satellite_markers(
    std::vector<SatViewMarkerInstance>& markers,
    SatViewCameraPov parent_id,
    double simulation_seconds)
{
    const SatViewSolarSystemBody& parent = satview_solar_system_body(parent_id);
    for (const SatViewSolarSystemBody* child : satview_child_bodies(parent_id))
    {
        const glm::vec3 position = to_vec3(
            satview_body_position_in_parent_frame(*child, simulation_seconds)
            / parent.equatorial_radius_km);
        const float physical_radius = static_cast<float>(
            child->equatorial_radius_km / parent.equatorial_radius_km);
        const float marker_radius = std::max(
            physical_radius,
            std::clamp(0.006f + glm::length(position) * 0.0015f, 0.010f, 0.035f));
        SatViewMarkerInstance marker;
        marker.position0_size = glm::vec4(position, marker_radius);
        marker.position1_selected = glm::vec4(position, 0.0f);
        marker.color = glm::vec4(child->display_color, 0.96f);
        marker.style.x = 3.0f;
        markers.push_back(marker);
    }
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
    std::size_t marker_limit,
    std::optional<glm::dvec3> ground_observer_render_position,
    bool ground_horizon_occlusion,
    float ground_marker_scale)
{
    std::size_t visible_marker_index = 0;
    for (std::size_t state_index = 0; state_index < snapshot.states.size(); ++state_index)
    {
        const SatellitePropagatedState& state = snapshot.states[state_index];
        const bool selected = selected_id.has_value() && state.norad_catalog_id == *selected_id;
        const bool visible = satellite_visible(filter, state, source_label);
        if (!visible && !selected)
            continue;

        const glm::vec3 position0 = to_vec3(teme_position_to_render_earth_radii(state.teme_position_km));
        if (ground_horizon_occlusion
            && ground_observer_render_position.has_value()
            && satview_ground_visibility_dot(
                   glm::dvec3(position0),
                   *ground_observer_render_position)
                <= 0.0)
        {
            continue;
        }

        const bool under_marker_limit = marker_limit == 0 || visible_marker_index < marker_limit;
        if (visible)
            ++visible_marker_index;
        if (!under_marker_limit && !selected)
            continue;

        const glm::vec3 position1 = to_vec3(
            teme_position_to_render_earth_radii(next_teme_position(snapshot, state_index)));
        const float range = glm::length(position0);
        const float base_size = ground_observer_render_position.has_value()
            ? satview_ground_marker_base_size(glm::dvec3(position0), *ground_observer_render_position)
                * ground_marker_scale
            : std::clamp(0.006f + range * 0.0022f, 0.008f, 0.026f);
        const float size = selected ? base_size * 2.2f : base_size;
        const float fidelity_alpha = state.solution_kind == OrbitSolutionKind::SatcatSummaryEstimate
            ? 0.55f
            : 1.0f;
        const glm::vec4 color = selected
            ? glm::vec4(1.0f, 0.96f, 0.68f, fidelity_alpha)
            : glm::mix(
                  satellite_color(state.orbit_class, state.object_kind, state.population,
                      state.object_prefix_hash, color_mode, 0.95f * fidelity_alpha),
                  glm::vec4(1.0f, 1.0f, 1.0f, 0.95f * fidelity_alpha),
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
    SatViewTrackDisplayMode track_display_mode,
    SatViewSatelliteDisplayMode satellite_display_mode)
{
    if (!snapshot)
        return 1.0f;

    const std::string_view source_label = snapshot->source_label;
    float radius = 1.0f;
    if (satellite_display_shows_tracks(satellite_display_mode) && snapshot->tracks)
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

    if (!satellite_display_shows_markers(satellite_display_mode))
        return radius;

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

namespace
{

bool lunar_surface_kind_visible(
    const SatViewLunarSurfaceObject& object,
    bool show_landers,
    bool show_rovers,
    bool show_instruments,
    bool show_impacts,
    bool show_crewed_artifacts,
    bool show_approximate_locations);
bool lunar_surface_site_marker_visible(
    const SatViewLunarSurfaceCatalog& catalog,
    std::size_t object_index,
    bool show_landers,
    bool show_rovers,
    bool show_instruments,
    bool show_impacts,
    bool show_crewed_artifacts,
    bool show_approximate_locations);
void append_lunar_surface_marker_instances(
    std::vector<SatViewMarkerInstance>& markers,
    const SatViewLunarSurfaceCatalog& catalog,
    const glm::dvec3& moon_render_position,
    glm::vec2 map_center_radians,
    bool map_projection,
    bool show_children,
    std::optional<std::size_t> selected_index,
    bool show_landers,
    bool show_rovers,
    bool show_instruments,
    bool show_impacts,
    bool show_crewed_artifacts,
    bool show_approximate_locations);

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
    app_text_service_ = context.text_service;
    display_ppi_ = context.display_ppi;
    scene_font_path_ = app_text_service_ ? app_text_service_->primary_font_path() : std::string{};
    viewport_ = context.initial_viewport;
    scene_viewport_ = viewport_;
    show_ui_panel_ = context.launch_options.show_host_ui_panels;
    continuous_refresh_enabled_ = context.launch_options.request_continuous_refresh;
    apply_config(config_document_ ? load_satview_config(*config_document_) : SatViewConfig{});
    IMGUI_CHECKVERSION();
    imgui_context_ = ImGui::CreateContext();
    if (imgui_context_)
    {
        ImGui::SetCurrentContext(imgui_context_);
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;
        io.ConfigWindowsResizeFromEdges = true;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
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
    const SatViewSunPosition sun_position = satview_sun_position(simulated_seconds_);
    const glm::vec3 target = camera_target_position(camera_pov_, moon, sun_position);
    const float target_radius = camera_target_radius(camera_pov_);
    camera_->SetDistanceLimits(
        camera_min_distance(camera_pov_),
        std::max(kCameraDefaultMaxDistance, kCameraDefaultMaxDistance * target_radius));
    camera_->SetPositionAndFocalPoint(
        target + camera_position_from_yaw_pitch(std::atan2(sun.x, sun.z) + 0.65f, 0.25f, kCameraDefaultDistance * target_radius),
        target);
    last_pump_time_ = std::chrono::steady_clock::now();
    last_activity_time_ = last_pump_time_;
    next_frame_time_ = last_pump_time_ + kFrameTick;
    scene_pass_ = std::make_shared<SatViewScenePass>();
    lunar_surface_catalog_ = std::make_unique<SatViewLunarSurfaceCatalog>(
        load_satview_lunar_surface_catalog());
    if (!lunar_surface_catalog_->error.empty())
    {
        DRAXUL_LOG_WARN(LogCategory::App, "SatView: %s", lunar_surface_catalog_->error.c_str());
    }
    stars_ = load_satview_star_catalog(kMaximumStarCatalogCount);
    constellation_lines_ = load_satview_constellation_catalog();
    constellation_boundary_catalog_ = std::make_unique<SatViewConstellationBoundaryCatalog>(
        load_satview_constellation_boundary_catalog());
    constellation_boundary_lines_.clear();
    constellation_boundary_lines_.reserve(constellation_boundary_catalog_->segments.size());
    for (const SatViewConstellationBoundarySegment& segment : constellation_boundary_catalog_->segments)
    {
        constellation_boundary_lines_.push_back({
            glm::vec4(segment.start_direction, constellation_boundary_width_),
            glm::vec4(segment.end_direction, 0.0f),
            kConstellationBoundaryColor,
            glm::vec4(0.0f),
        });
    }
    rebuild_visible_stars();
    scene_pass_->set_stars(visible_stars_);
    update_constellation_line_styles();
    scene_pass_->set_constellation_lines_enabled(constellation_lines_enabled_);
    scene_pass_->set_constellation_boundaries_enabled(constellation_boundaries_enabled_);
    scene_pass_->set_observatory_landscape(
        build_satview_observatory_landscape(kGroundObserverAltitudeEarthRadii));
    scene_pass_->set_observatory_horizon_enabled(observatory_horizon_enabled_);
    scene_pass_->set_milky_way_enabled(milky_way_enabled_);
    scene_pass_->set_milky_way_brightness(milky_way_brightness_);
    scene_pass_->set_star_magnitude_range(star_min_magnitude_, star_max_magnitude_);
    scene_pass_->set_star_brightness_scale(star_brightness_scale_);
    scene_pass_->set_tone_mapping(tone_map_exposure_, tone_map_white_point_);
    scene_pass_->set_hdr_debug_enabled(show_hdr_debug_panel_);
    refresh_scene_text_service();
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
    scene_text_atlas_.reset();
    if (scene_text_service_)
        scene_text_service_->shutdown();
    scene_text_service_.reset();
    constellation_boundary_catalog_.reset();
    lunar_surface_catalog_.reset();
    app_text_service_ = nullptr;
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
    if (!show_ui_panel_ || !imgui_context_ || !imgui_backend_)
        scene_viewport_ = viewport_;
    request_redraw();
}

void SatViewHost::pump()
{
    if (!running_)
        return;

    bool redraw_needed = false;
    catalog_service_.pump();
    if (cloud_service_)
    {
        cloud_service_->pump();
        if (auto image = cloud_service_->take_pending_image())
        {
            scene_pass_->set_cloud_image(std::move(image));
            redraw_needed = true;
        }
    }
    const std::uint64_t catalog_generation = catalog_service_.catalog_generation();
    if (simulation_worker_ && catalog_generation != 0 && catalog_generation != simulation_catalog_generation_)
    {
        simulation_catalog_generation_ = catalog_generation;
        catalog_snapshot_ = catalog_service_.catalog();
        simulation_worker_->set_catalog(catalog_snapshot_, catalog_generation);
        redraw_needed = true;
    }
    if (simulation_settings_dirty_)
    {
        sync_simulation_render_settings();
        simulation_settings_dirty_ = false;
        redraw_needed = true;
    }
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last_pump_time_).count();
    last_pump_time_ = now;
    last_imgui_delta_seconds_ = dt;
    const bool frame_tick_due = continuous_refresh_enabled_ || now >= next_frame_time_;
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
        else if (projection_mode_ == SatViewProjectionMode::Ground)
        {
            ground_camera_orientation_ = satview_rotate_ground_camera(
                ground_camera_orientation_,
                glm::vec2(
                    kGroundKeyboardYawRadiansPerSecond * movement.orbit.x * input_dt,
                    -kGroundKeyboardPitchRadiansPerSecond * movement.orbit.y * input_dt));
            if (movement.zoom != 0.0f)
            {
                ground_fov_degrees_ = satview_clamp_ground_fov_degrees(
                    ground_projection_,
                    ground_fov_degrees_ * std::exp(movement.zoom * kCameraZoomRatePerSecond * input_dt));
                config_dirty_ = true;
            }
            redraw_needed = true;
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
        redraw_needed = true;
    }
    if (frame_tick_due)
        redraw_needed = true;

    if (redraw_needed)
    {
        request_redraw();
        next_frame_time_ = continuous_refresh_enabled_ ? now : now + kFrameTick;
    }
}

void SatViewHost::draw(IFrameContext& frame)
{
    if (!scene_pass_)
        return;

    auto snapshot_guard = simulation_worker_ ? simulation_worker_->acquire_latest() : SatViewSnapshotExchange::ReadGuard{};
    const SatViewSimulationSnapshot* snapshot = snapshot_guard.get();
    const double simulation_seconds = snapshot ? render_simulation_seconds(*snapshot) : last_draw_simulation_seconds_;
    last_draw_simulation_seconds_ = simulation_seconds;
    if (!show_ui_panel_ || !imgui_context_ || !imgui_backend_)
        scene_viewport_ = viewport_;
    render_host_imgui(last_imgui_delta_seconds_, snapshot);
    const bool map_projection = projection_mode_ == SatViewProjectionMode::Map;
    const bool ground_projection = projection_mode_ == SatViewProjectionMode::Ground;
    const bool generic_body_view = satview_uses_generic_body_view(camera_pov_);
    const bool earth_track_visible = earth_orbit_track_visible(
                                         earth_track_enabled_, satellite_display_mode_, projection_mode_, camera_pov_)
        && !generic_body_view;
    const SatViewMoonPosition moon = satview_moon_position(simulation_seconds);
    const SatViewSunPosition sun_position = satview_sun_position(simulation_seconds);
    const float earth_scene_radius = visible_scene_radius(
        snapshot,
        filter_,
        selected_norad_catalog_id_,
        track_display_mode_,
        satellite_display_mode_);
    const glm::dvec3 ground_observer = ground_observer_render_position(simulation_seconds);
    const glm::vec3 ground_eye = glm::vec3(
        ground_observer * (1.0 + static_cast<double>(kGroundObserverAltitudeEarthRadii)));
    const float scene_radius = generic_body_view
        ? solar_system_scene_radius(camera_pov_)
        : camera_scene_radius(
              earth_scene_radius,
              moon,
              sun_position,
              camera_pov_,
              moon_enabled_,
              sun_enabled_,
              earth_track_visible);
    const SatViewSolarSystemBody& focus_body = satview_solar_system_body(camera_pov_);
    std::optional<SatViewContextBodyState> context_sun_state;
    std::optional<SatViewContextBodyState> context_parent_state;
    const SatViewSolarSystemBody* context_parent_body = nullptr;
    if (generic_body_view && !map_projection)
    {
        // Positions and radii are observer-independent, so last frame's camera
        // position is a fine observer for the inside-the-body guard.
        const glm::dvec3 context_observer(camera_->GetPosition());
        if (camera_pov_ != SatViewCameraPov::Sun && sun_enabled_)
        {
            context_sun_state = satview_context_body_state(
                camera_pov_,
                SatViewCameraPov::Sun,
                context_observer,
                simulation_seconds);
        }
        if (focus_body.parent.has_value()
            && *focus_body.parent != SatViewCameraPov::Sun)
        {
            context_parent_body = &satview_solar_system_body(*focus_body.parent);
            context_parent_state = satview_context_body_state(
                camera_pov_,
                context_parent_body->id,
                context_observer,
                simulation_seconds);
        }
    }
    // Contextual bodies sit at true interplanetary distances; extend both the
    // far plane and the camera zoom ceiling to cover them, matching the Earth
    // view where the visible sun already grows the scene radius.
    float far_radius = scene_radius;
    if (context_sun_state)
    {
        far_radius = std::max(
            far_radius,
            static_cast<float>(glm::length(context_sun_state->position_focus_radii)
                + context_sun_state->radius_focus_radii));
    }
    if (context_parent_state)
    {
        far_radius = std::max(
            far_radius,
            static_cast<float>(glm::length(context_parent_state->position_focus_radii)
                + context_parent_state->radius_focus_radii));
    }
    camera_->SetFocalPoint(camera_target_position(camera_pov_, moon, sun_position));
    camera_->SetDistanceLimits(
        camera_min_distance(camera_pov_),
        camera_max_distance_for_radius(far_radius));

    const int pixel_w = std::max(1, scene_viewport_.pixel_size.x);
    const int pixel_h = std::max(1, scene_viewport_.pixel_size.y);
    camera_->SetFilmSize(static_cast<float>(pixel_w), static_cast<float>(pixel_h));
    if (!map_projection && !ground_projection && camera_->PreRender())
        request_redraw();
    const glm::mat4 view = ground_projection
        ? satview_ground_view_matrix(ground_eye, ground_camera_orientation_)
        : camera_view_matrix(*camera_);
    const glm::vec3 eye = ground_projection ? ground_eye : camera_->GetPosition();
    const float viewport_aspect = static_cast<float>(pixel_w) / static_cast<float>(pixel_h);
    const glm::mat4 proj = reversed_z_perspective(
        glm::radians(ground_projection ? ground_fov_degrees_ : camera_->GetFieldOfView()),
        viewport_aspect,
        ground_projection ? kCameraMinNearPlane : camera_near_plane(camera_pov_, camera_->GetDistance()),
        ground_projection ? camera_far_plane(1.0f, far_radius) : camera_far_plane(camera_->GetDistance(), far_radius));

    SatViewFrameUniforms uniforms;
    uniforms.view_proj = map_projection
        ? glm::mat4(1.0f)
        : ground_projection && ground_projection_ == SatViewGroundProjection::Stereographic
        ? satview_stereographic_frame_transform(
              view,
              ground_fov_degrees_,
              viewport_aspect)
        : proj * view;
    const float projection_aspect_scale = static_cast<float>(pixel_h) / static_cast<float>(pixel_w);
    const float projection_code = map_projection
        ? -projection_aspect_scale
        : ground_projection
        ? (ground_projection_ == SatViewGroundProjection::Stereographic ? 3.0f : 1.0f)
            + (ground_horizon_occlusion_ ? 1.0f : 0.0f)
        : 0.0f;
    uniforms.camera_pos = map_projection
        ? glm::vec4(
              camera_target_position(camera_pov_, moon, sun_position),
              projection_code)
        : glm::vec4(eye, projection_code);
    if (map_projection)
    {
        uniforms.camera_orientation = glm::vec4(
            map_center_radians_,
            camera_pov_ == SatViewCameraPov::Moon
                ? 1.0f
                : camera_pov_ == SatViewCameraPov::Sun ? 2.0f
                                                       : 0.0f,
            0.0f);
    }
    else if (ground_projection)
    {
        const glm::quat camera_orientation = glm::quat_cast(glm::mat3(glm::inverse(view)));
        uniforms.camera_orientation = glm::vec4(
            camera_orientation.x,
            camera_orientation.y,
            camera_orientation.z,
            camera_orientation.w);
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
    const glm::vec3 sun = generic_body_view
        ? glm::vec3(satview_body_sun_direction(camera_pov_, simulation_seconds))
        : glm::vec3(solar_direction_render(simulation_seconds));
    const float cloud_mode = !clouds_enabled_ ? 0.0f : (realistic_clouds_enabled_ ? 2.0f : 1.0f);
    uniforms.sun_dir_time = glm::vec4(sun, cloud_mode);
    const glm::quat sun_orientation = glm::quat(sun_position.body_to_render_orientation);
    if (map_projection && camera_pov_ == SatViewCameraPov::Sun)
    {
        uniforms.sun_dir_time = glm::vec4(
            sun_orientation.x,
            sun_orientation.y,
            sun_orientation.z,
            sun_orientation.w);
    }
    uniforms.render_params = glm::vec4(
        static_cast<float>(kSatViewSphereLatitudeBands),
        static_cast<float>(kSatViewSphereLongitudeBands),
        static_cast<float>(greenwich_sidereal_angle_radians(simulation_seconds)),
        snapshot ? marker_interpolation_alpha(*snapshot, simulation_seconds) : 0.0f);
    scene_pass_->set_frame(uniforms);
    scene_pass_->set_atmosphere_enabled(atmosphere_enabled_ && !generic_body_view);
    scene_pass_->set_ground_visible(ground_visible_);
    scene_pass_->set_projection_mode(
        map_projection,
        camera_pov_ == SatViewCameraPov::Moon,
        camera_pov_ == SatViewCameraPov::Sun && !generic_body_view,
        ground_projection);
    const bool moon_above_ground_horizon = !ground_projection
        || !ground_horizon_occlusion_
        || satview_ground_visibility_dot(moon.render_position_earth_radii, ground_observer) > 0.0;
    const glm::vec4 moon_render_position_radius(
        glm::vec3(moon.render_position_earth_radii), kMoonRadiusEarthRadii);
    scene_pass_->set_moon(
        moon_render_position_radius,
        !generic_body_view && moon_enabled_ && moon_above_ground_horizon);
    glm::vec4 sun_render_position_radius(
        glm::vec3(sun_position.render_position_earth_radii), kSunRadiusEarthRadii);
    if (context_sun_state)
    {
        sun_render_position_radius = glm::vec4(
            glm::vec3(context_sun_state->position_focus_radii),
            static_cast<float>(context_sun_state->radius_focus_radii));
    }
    const bool sun_above_ground_horizon = !ground_projection
        || !ground_horizon_occlusion_
        || satview_ground_visibility_dot(
               glm::dvec3(glm::vec3(sun_render_position_radius)),
               ground_observer)
            > 0.0;
    scene_pass_->set_sun(
        sun_render_position_radius,
        sun_orientation,
        context_sun_state.has_value()
            || (!generic_body_view && sun_enabled_ && sun_above_ground_horizon));
    scene_pass_->set_context_body(
        context_parent_body ? context_parent_body->id : SatViewCameraPov::Earth,
        context_parent_state
            ? glm::vec4(
                  glm::vec3(context_parent_state->position_focus_radii),
                  static_cast<float>(context_parent_state->radius_focus_radii))
            : glm::vec4(0.0f),
        context_parent_body
            ? static_cast<float>(satview_body_rotation_radians(
                  *context_parent_body, simulation_seconds))
            : 0.0f,
        context_parent_body
            ? static_cast<float>(context_parent_body->polar_radius_km
                  / context_parent_body->equatorial_radius_km)
            : 1.0f,
        context_parent_state.has_value());
    scene_pass_->set_focus_body(
        camera_pov_,
        static_cast<float>(satview_body_rotation_radians(focus_body, simulation_seconds)),
        static_cast<float>(focus_body.polar_radius_km / focus_body.equatorial_radius_km),
        focus_body.emissive,
        generic_body_view);
    std::vector<SatViewBodyRenderInstance> child_body_instances;
    if (generic_body_view && !map_projection && projection_mode_ == SatViewProjectionMode::Globe)
        child_body_instances = satview_child_body_instances(camera_pov_, simulation_seconds);
    scene_pass_->set_child_bodies(child_body_instances);
    std::vector<SatViewRingBand> ring_bands;
    if (generic_body_view && !map_projection && projection_mode_ == SatViewProjectionMode::Globe)
    {
        const auto source_ring_bands = satview_planetary_ring_bands(camera_pov_);
        ring_bands.assign(source_ring_bands.begin(), source_ring_bands.end());
        if (camera_pov_ != SatViewCameraPov::Saturn)
        {
            if (context_parent_body && context_parent_state)
            {
                for (SatViewRingBand& band : ring_bands)
                {
                    band.center_focus_radii = context_parent_state->position_focus_radii;
                    band.radius_scale_focus_radii = context_parent_state->radius_focus_radii;
                }
            }
            else
            {
                ring_bands.clear();
            }
        }
    }
    scene_pass_->set_ring_bands(ring_bands);
    scene_pass_->set_star_magnitude_range(star_min_magnitude_, star_max_magnitude_);
    scene_pass_->set_star_brightness_scale(star_brightness_scale_);
    scene_pass_->set_star_projection_aspect_scale(projection_aspect_scale);
    scene_pass_->set_constellation_lines_enabled(constellation_lines_enabled_);
    scene_pass_->set_constellation_boundaries_enabled(constellation_boundaries_enabled_);
    scene_pass_->set_observatory_horizon_enabled(
        ground_projection && observatory_horizon_enabled_);
    scene_pass_->set_milky_way_enabled(milky_way_enabled_);
    scene_pass_->set_milky_way_brightness(milky_way_brightness_);
    scene_pass_->set_tone_mapping(tone_map_exposure_, tone_map_white_point_);
    scene_pass_->set_hdr_debug_enabled(show_hdr_debug_panel_);
    rebuild_scene_labels(
        view, proj, ground_observer, moon_render_position_radius, sun_render_position_radius);

    const void* track_source = !generic_body_view && snapshot && snapshot->tracks
        ? snapshot->tracks.get()
        : nullptr;
    const bool show_tracks = satellite_display_shows_tracks(satellite_display_mode_);
    const bool show_markers = satellite_display_shows_markers(satellite_display_mode_);
    const std::optional<glm::dvec3> ground_context = ground_projection
        ? std::optional<glm::dvec3>(ground_observer)
        : std::nullopt;
    const bool show_moon_track = moon_orbit_track_visible(
        moon_track_enabled_, satellite_display_mode_, projection_mode_, camera_pov_);
    bool moon_track_window_changed = false;
    if (show_moon_track
        && (!moon_track_center_seconds_.has_value()
            || std::abs(simulation_seconds - *moon_track_center_seconds_)
                >= 0.5 * kSatViewMoonSiderealPeriodSeconds))
    {
        moon_track_center_seconds_ = simulation_seconds;
        moon_track_window_changed = true;
    }
    bool earth_track_window_changed = false;
    if (earth_track_visible
        && (!earth_track_center_seconds_.has_value()
            || std::abs(simulation_seconds - *earth_track_center_seconds_)
                >= 0.5 * kSatViewEarthOrbitPeriodSeconds))
    {
        earth_track_center_seconds_ = simulation_seconds;
        earth_track_window_changed = true;
    }
    if (track_buffer_dirty_
        || earth_track_window_changed
        || earth_track_visible != uploaded_earth_track_visible_)
    {
        std::vector<SatViewSceneVertex> earth_track_vertices;
        if (earth_track_visible)
        {
            earth_track_vertices.reserve((track_sample_count_ + 1) * 2);
            append_earth_track_vertices(
                earth_track_vertices,
                *earth_track_center_seconds_,
                track_sample_count_);
        }
        scene_pass_->set_earth_track_vertices(earth_track_vertices);
        uploaded_earth_track_visible_ = earth_track_visible;
    }

    const bool lunar_tracks_need_reanchoring = show_tracks
        && snapshot
        && snapshot->tracks
        && std::ranges::any_of(*snapshot->tracks, [](const SatelliteOrbitTrack& track) {
               return track.central_body == CentralBody::Moon;
           });
    if (track_buffer_dirty_
        || track_source != uploaded_track_source_
        || moon_track_window_changed
        || lunar_tracks_need_reanchoring)
    {
        std::vector<SatViewSceneVertex> track_vertices;
        if (show_tracks)
        {
            const std::size_t track_count = snapshot && snapshot->tracks
                ? snapshot->tracks->size()
                : 0;
            track_vertices.reserve(
                (track_count + (show_moon_track ? 1 : 0))
                * (track_sample_count_ + 1) * 2);
            if (generic_body_view && projection_mode_ == SatViewProjectionMode::Globe)
            {
                append_natural_satellite_tracks(
                    track_vertices,
                    camera_pov_,
                    planet_tracks_,
                    track_sample_count_);
                append_planetary_ring_tracks(track_vertices, camera_pov_);
            }
            else if (snapshot)
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
                    camera_pov_,
                    simulation_seconds,
                    ground_context,
                    ground_horizon_occlusion_);
            }
            if (show_moon_track)
            {
                append_moon_track_vertices(
                    track_vertices,
                    *moon_track_center_seconds_,
                    track_sample_count_);
            }
        }
        scene_pass_->set_track_vertices(track_vertices);
        uploaded_track_source_ = track_source;
        track_buffer_dirty_ = false;
    }

    if (generic_body_view)
    {
        std::vector<SatViewMarkerInstance> markers;
        if (show_markers && projection_mode_ == SatViewProjectionMode::Globe)
            append_natural_satellite_markers(markers, camera_pov_, simulation_seconds);
        scene_pass_->set_markers(markers);
        uploaded_marker_generation_ = 0;
        marker_buffer_dirty_ = false;
    }
    else if (snapshot)
    {
        if (marker_buffer_dirty_ || snapshot->generation != uploaded_marker_generation_)
        {
            std::vector<SatViewMarkerInstance> markers;
            if (show_markers)
            {
                markers.reserve(snapshot->states.size());
                append_marker_instances(
                    markers,
                    *snapshot,
                    filter_,
                    snapshot->source_label,
                    selected_norad_catalog_id_,
                    color_mode_,
                    marker_satellite_limit_,
                    ground_context,
                    ground_horizon_occlusion_,
                    ground_marker_scale_);
            }
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

    std::vector<SatViewMarkerInstance> surface_markers;
    if (lunar_surface_objects_enabled_
        && lunar_surface_catalog_
        && lunar_surface_catalog_->error.empty()
        && camera_pov_ == SatViewCameraPov::Moon
        && projection_mode_ != SatViewProjectionMode::Ground)
    {
        surface_markers.reserve(lunar_surface_catalog_->objects.size() * 2);
        const bool show_children = projection_mode_ == SatViewProjectionMode::Globe
            && camera_->GetDistance()
                <= kMoonRadiusEarthRadii * kLunarSurfaceChildExpansionDistanceScale;
        append_lunar_surface_marker_instances(
            surface_markers,
            *lunar_surface_catalog_,
            moon.render_position_earth_radii,
            map_center_radians_,
            map_projection,
            show_children,
            selected_lunar_surface_index_,
            show_lunar_landers_,
            show_lunar_rovers_,
            show_lunar_instruments_,
            show_lunar_impacts_,
            show_lunar_crewed_artifacts_,
            show_lunar_approximate_locations_);
    }
    scene_pass_->set_surface_markers(surface_markers);

    RenderViewport viewport;
    viewport.x = scene_viewport_.pixel_pos.x;
    viewport.y = scene_viewport_.pixel_pos.y;
    viewport.width = pixel_w;
    viewport.height = pixel_h;
    frame.record_render_pass(*scene_pass_, viewport);
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
    return next_frame_time_;
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

        const bool scene_input = dragging_
            || imgui_mouse_targets_scene(scene_viewport_, event.pos);
        if (!scene_input)
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
        if (event.clicks >= 2 && projection_mode_ != SatViewProjectionMode::Ground)
        {
            const double simulation_seconds = simulation_worker_
                ? simulation_worker_->current_simulation_seconds()
                : last_draw_simulation_seconds_;
            if (select_nearest_natural_body(event.pos, true))
            {
                dragging_ = false;
                pending_click_ = false;
                last_activity_time_ = std::chrono::steady_clock::now();
                return;
            }
            if (enter_ground_view_from_screen(event.pos, simulation_seconds))
            {
                dragging_ = false;
                pending_click_ = false;
                last_activity_time_ = std::chrono::steady_clock::now();
                return;
            }
        }
        pending_click_ = true;
        click_start_pos_ = event.pos;
        last_map_drag_pos_ = event.pos;
        last_ground_drag_pos_ = event.pos;
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
                select_nearest_object(event.pos);
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
        const bool scene_input = dragging_
            || imgui_mouse_targets_scene(scene_viewport_, event.pos);
        if (!scene_input)
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
            glm::vec2(scene_viewport_.pixel_size)));
        return;
    }

    if (projection_mode_ == SatViewProjectionMode::Ground)
    {
        glm::vec2 pixel_delta = event.delta;
        if (pixel_delta == glm::vec2(0.0f))
            pixel_delta = glm::vec2(event.pos - last_ground_drag_pos_);
        last_ground_drag_pos_ = event.pos;
        if (!left_button_down || pending_click_)
            return;

        ground_camera_orientation_ = satview_rotate_ground_camera(
            ground_camera_orientation_,
            glm::vec2(
                -pixel_delta.x * kGroundLookRadiansPerPixel,
                pixel_delta.y * kGroundLookRadiansPerPixel));
        request_redraw();
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
        if (!imgui_mouse_targets_scene(scene_viewport_, event.pos))
        {
            request_redraw();
            return;
        }
    }

    if (projection_mode_ == SatViewProjectionMode::Map)
        return;
    if (projection_mode_ == SatViewProjectionMode::Ground)
    {
        ground_fov_degrees_ = satview_clamp_ground_fov_degrees(
            ground_projection_,
            ground_fov_degrees_ * std::pow(0.92f, event.delta.y));
        config_dirty_ = true;
        request_redraw();
        return;
    }

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
    if (event.keycode == SDLK_ESCAPE
        && (selected_norad_catalog_id_.has_value()
            || selected_lunar_surface_index_.has_value()
            || selected_natural_body_.has_value()))
    {
        selected_norad_catalog_id_.reset();
        selected_lunar_surface_index_.reset();
        selected_natural_body_.reset();
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
        selected_lunar_surface_index_.reset();
        selected_natural_body_.reset();
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
    const std::string view = projection_mode_ == SatViewProjectionMode::Ground
        ? "earth ground"
        : projection_mode_ == SatViewProjectionMode::Map
        ? std::string(camera_pov_name(camera_pov_)) + " map"
        : camera_pov_name(camera_pov_);
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
    const bool scene_font_changed = scene_font_path_ != path;
    scene_font_path_ = path;
    imgui_font_path_ = path;
    imgui_font_size_pixels_ = size_pixels;
    if (scene_font_changed)
        refresh_scene_text_service();
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

void SatViewHost::on_font_metrics_changed()
{
    refresh_scene_text_service();
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
    config.satellite_display_mode = satellite_display_mode_;
    config.projection_mode = projection_mode_;
    config.camera_pov = camera_pov_;
    config.track_satellite_limit = track_satellite_limit_;
    config.track_sample_count = track_sample_count_;
    config.refresh_tracks_each_step = refresh_tracks_each_step_;
    config.marker_satellite_limit = marker_satellite_limit_;
    config.star_min_magnitude = star_min_magnitude_;
    config.star_max_magnitude = star_max_magnitude_;
    config.star_brightness_scale = star_brightness_scale_;
    config.constellation_figure_width = constellation_figure_width_;
    config.constellation_boundary_width = constellation_boundary_width_;
    config.constellation_lines_enabled = constellation_lines_enabled_;
    config.constellation_boundaries_enabled = constellation_boundaries_enabled_;
    config.constellation_labels_enabled = constellation_labels_enabled_;
    config.milky_way_enabled = milky_way_enabled_;
    config.milky_way_brightness = milky_way_brightness_;
    config.tone_map_exposure = tone_map_exposure_;
    config.tone_map_white_point = tone_map_white_point_;
    config.show_hdr_debug_panel = show_hdr_debug_panel_;
    config.time_speed = time_speed_;
    config.clouds_enabled = clouds_enabled_;
    config.realistic_clouds_enabled = realistic_clouds_enabled_;
    config.atmosphere_enabled = atmosphere_enabled_;
    config.moon_enabled = moon_enabled_;
    config.moon_track_enabled = moon_track_enabled_;
    config.lunar_surface_objects_enabled = lunar_surface_objects_enabled_;
    config.show_lunar_landers = show_lunar_landers_;
    config.show_lunar_rovers = show_lunar_rovers_;
    config.show_lunar_instruments = show_lunar_instruments_;
    config.show_lunar_impacts = show_lunar_impacts_;
    config.show_lunar_crewed_artifacts = show_lunar_crewed_artifacts_;
    config.show_lunar_approximate_locations = show_lunar_approximate_locations_;
    config.earth_track_enabled = earth_track_enabled_;
    config.planet_tracks = planet_tracks_;
    config.sun_enabled = sun_enabled_;
    config.ground_projection = ground_projection_;
    config.ground_fov_degrees = ground_fov_degrees_;
    config.ground_marker_scale = ground_marker_scale_;
    config.ground_visible = ground_visible_;
    config.ground_horizon_occlusion = ground_horizon_occlusion_;
    config.observatory_horizon_enabled = observatory_horizon_enabled_;
    config.cardinal_labels_enabled = cardinal_labels_enabled_;
    config.ground_longitude_radians = ground_location_radians_.x;
    config.ground_latitude_radians = ground_location_radians_.y;
    return config;
}

void SatViewHost::apply_config(const SatViewConfig& config)
{
    filter_ = config.filter;
    color_mode_ = config.color_mode;
    track_display_mode_ = config.track_display_mode;
    satellite_display_mode_ = config.satellite_display_mode;
    projection_mode_ = config.projection_mode;
    camera_pov_ = config.camera_pov;
    if (camera_pov_ != SatViewCameraPov::Earth
        && projection_mode_ == SatViewProjectionMode::Ground)
    {
        projection_mode_ = SatViewProjectionMode::Globe;
    }
    if (camera_pov_ == SatViewCameraPov::Earth)
        satview_select_central_body(filter_, CentralBody::Earth);
    else if (camera_pov_ == SatViewCameraPov::Moon)
        satview_select_central_body(filter_, CentralBody::Moon);
    else
        satview_select_central_body(filter_, CentralBody::Other);
    track_satellite_limit_ = config.track_satellite_limit;
    track_sample_count_ = config.track_sample_count;
    refresh_tracks_each_step_ = config.refresh_tracks_each_step;
    marker_satellite_limit_ = config.marker_satellite_limit;
    star_min_magnitude_ = std::clamp(
        config.star_min_magnitude,
        kMinimumStarMagnitude,
        kMaximumStarMagnitude);
    star_max_magnitude_ = std::clamp(
        config.star_max_magnitude,
        kMinimumStarMagnitude,
        kMaximumStarMagnitude);
    if (star_min_magnitude_ > star_max_magnitude_)
        std::swap(star_min_magnitude_, star_max_magnitude_);
    star_brightness_scale_ = std::clamp(
        config.star_brightness_scale,
        kMinimumStarBrightnessScale,
        kMaximumStarBrightnessScale);
    constellation_figure_width_ = std::clamp(
        config.constellation_figure_width,
        kMinimumConstellationLineWidth,
        kMaximumConstellationLineWidth);
    constellation_boundary_width_ = std::clamp(
        config.constellation_boundary_width,
        kMinimumConstellationLineWidth,
        kMaximumConstellationLineWidth);
    constellation_lines_enabled_ = config.constellation_lines_enabled;
    constellation_boundaries_enabled_ = config.constellation_boundaries_enabled;
    constellation_labels_enabled_ = config.constellation_labels_enabled;
    milky_way_enabled_ = config.milky_way_enabled;
    milky_way_brightness_ = std::clamp(
        config.milky_way_brightness,
        kMinimumMilkyWayBrightness,
        kMaximumMilkyWayBrightness);
    tone_map_exposure_ = std::clamp(
        config.tone_map_exposure,
        kMinimumToneMapExposure,
        kMaximumToneMapExposure);
    tone_map_white_point_ = std::clamp(
        config.tone_map_white_point,
        kMinimumToneMapWhitePoint,
        kMaximumToneMapWhitePoint);
    show_hdr_debug_panel_ = config.show_hdr_debug_panel;
    time_speed_ = config.time_speed;
    clouds_enabled_ = config.clouds_enabled;
    realistic_clouds_enabled_ = config.realistic_clouds_enabled;
    atmosphere_enabled_ = config.atmosphere_enabled;
    moon_enabled_ = config.moon_enabled || camera_pov_ == SatViewCameraPov::Moon;
    moon_track_enabled_ = config.moon_track_enabled;
    lunar_surface_objects_enabled_ = config.lunar_surface_objects_enabled;
    show_lunar_landers_ = config.show_lunar_landers;
    show_lunar_rovers_ = config.show_lunar_rovers;
    show_lunar_instruments_ = config.show_lunar_instruments;
    show_lunar_impacts_ = config.show_lunar_impacts;
    show_lunar_crewed_artifacts_ = config.show_lunar_crewed_artifacts;
    show_lunar_approximate_locations_ = config.show_lunar_approximate_locations;
    earth_track_enabled_ = config.earth_track_enabled;
    planet_tracks_ = config.planet_tracks;
    sun_enabled_ = config.sun_enabled || camera_pov_ == SatViewCameraPov::Sun;
    ground_projection_ = config.ground_projection;
    ground_fov_degrees_ = satview_clamp_ground_fov_degrees(
        ground_projection_,
        config.ground_fov_degrees);
    ground_marker_scale_ = clamp_ground_marker_scale(config.ground_marker_scale);
    ground_visible_ = config.ground_visible;
    ground_horizon_occlusion_ = config.ground_horizon_occlusion;
    observatory_horizon_enabled_ = config.observatory_horizon_enabled;
    cardinal_labels_enabled_ = config.cardinal_labels_enabled;
    ground_location_radians_ = glm::dvec2(
        config.ground_longitude_radians,
        config.ground_latitude_radians);
    copy_to_buffer(search_buffer_, filter_.search_text);
    copy_to_buffer(object_type_buffer_, filter_.object_type_text);
    copy_to_buffer(source_buffer_, filter_.source_text);
    rebuild_visible_stars();
    update_constellation_line_styles();
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
        std::optional<CentralBody> track_central_body;
        if (filter_.show_moon && !filter_.show_earth)
            track_central_body = CentralBody::Moon;
        else if (filter_.show_earth && !filter_.show_moon)
            track_central_body = CentralBody::Earth;
        simulation_worker_->set_render_settings(
            track_satellite_limit_,
            track_sample_count_,
            refresh_tracks_each_step_,
            selected_norad_catalog_id_,
            track_central_body);
    }
}

namespace
{

bool lunar_surface_kind_visible(
    const SatViewLunarSurfaceObject& object,
    bool show_landers,
    bool show_rovers,
    bool show_instruments,
    bool show_impacts,
    bool show_crewed_artifacts,
    bool show_approximate_locations)
{
    if (object.crewed_mission && !show_crewed_artifacts)
        return false;
    if (object.location_quality == SatViewLunarLocationQuality::Approximate
        && !show_approximate_locations)
    {
        return false;
    }
    switch (object.kind)
    {
    case SatViewLunarSurfaceKind::Lander:
        return show_landers;
    case SatViewLunarSurfaceKind::Rover:
        return show_rovers;
    case SatViewLunarSurfaceKind::DeployedInstrument:
    case SatViewLunarSurfaceKind::Retroreflector:
        return show_instruments;
    case SatViewLunarSurfaceKind::ImpactSite:
    case SatViewLunarSurfaceKind::RocketStage:
        return show_impacts;
    case SatViewLunarSurfaceKind::CrewedArtifact:
        return show_crewed_artifacts;
    case SatViewLunarSurfaceKind::Unknown:
        return show_landers;
    }
    return false;
}

float lunar_surface_marker_style(SatViewLunarSurfaceKind kind)
{
    switch (kind)
    {
    case SatViewLunarSurfaceKind::Lander:
        return 1.0f;
    case SatViewLunarSurfaceKind::Rover:
        return 2.0f;
    case SatViewLunarSurfaceKind::DeployedInstrument:
    case SatViewLunarSurfaceKind::Retroreflector:
        return 3.0f;
    case SatViewLunarSurfaceKind::ImpactSite:
    case SatViewLunarSurfaceKind::RocketStage:
        return 4.0f;
    case SatViewLunarSurfaceKind::CrewedArtifact:
        return 5.0f;
    case SatViewLunarSurfaceKind::Unknown:
        return 1.0f;
    }
    return 1.0f;
}

bool lunar_surface_site_marker_visible(
    const SatViewLunarSurfaceCatalog& catalog,
    std::size_t object_index,
    bool show_landers,
    bool show_rovers,
    bool show_instruments,
    bool show_impacts,
    bool show_crewed_artifacts,
    bool show_approximate_locations)
{
    if (object_index >= catalog.objects.size())
        return false;
    const SatViewLunarSurfaceObject& object = catalog.objects[object_index];
    if (!object.renderable()
        || !lunar_surface_kind_visible(
            object,
            show_landers,
            show_rovers,
            show_instruments,
            show_impacts,
            show_crewed_artifacts,
            show_approximate_locations))
    {
        return false;
    }

    std::optional<std::size_t> best_visible;
    for (std::size_t index = 0; index < catalog.objects.size(); ++index)
    {
        const SatViewLunarSurfaceObject& candidate = catalog.objects[index];
        if (candidate.parent_site_id != object.parent_site_id
            || !candidate.renderable()
            || !lunar_surface_kind_visible(
                candidate,
                show_landers,
                show_rovers,
                show_instruments,
                show_impacts,
                show_crewed_artifacts,
                show_approximate_locations))
        {
            continue;
        }
        if (candidate.site_representative)
            return index == object_index;
        if (!best_visible.has_value()
            || candidate.display_rank < catalog.objects[*best_visible].display_rank
            || (candidate.display_rank == catalog.objects[*best_visible].display_rank
                && candidate.id < catalog.objects[*best_visible].id))
        {
            best_visible = index;
        }
    }
    return best_visible.has_value() && *best_visible == object_index;
}

void append_lunar_surface_marker_instances(
    std::vector<SatViewMarkerInstance>& markers,
    const SatViewLunarSurfaceCatalog& catalog,
    const glm::dvec3& moon_render_position,
    glm::vec2 map_center_radians,
    bool map_projection,
    bool show_children,
    std::optional<std::size_t> selected_index,
    bool show_landers,
    bool show_rovers,
    bool show_instruments,
    bool show_impacts,
    bool show_crewed_artifacts,
    bool show_approximate_locations)
{
    for (std::size_t index = 0; index < catalog.objects.size(); ++index)
    {
        const SatViewLunarSurfaceObject& object = catalog.objects[index];
        const bool selected = selected_index.has_value() && *selected_index == index;
        const bool filtered_visible = lunar_surface_kind_visible(
            object,
            show_landers,
            show_rovers,
            show_instruments,
            show_impacts,
            show_crewed_artifacts,
            show_approximate_locations);
        const bool site_marker_visible = lunar_surface_site_marker_visible(
            catalog,
            index,
            show_landers,
            show_rovers,
            show_instruments,
            show_impacts,
            show_crewed_artifacts,
            show_approximate_locations);
        if ((!filtered_visible || (!show_children && !site_marker_visible)) && !selected)
            continue;
        if (!object.renderable())
            continue;

        const glm::dvec3 body_direction = satview_lunar_body_direction(
            object.latitude_degrees,
            object.longitude_east_degrees);
        const glm::vec3 surface_normal = glm::normalize(to_vec3(
            satview_lunar_body_to_render_direction(body_direction, moon_render_position)));
        const glm::vec3 position = to_vec3(satview_lunar_surface_render_position(
            object.latitude_degrees,
            object.longitude_east_degrees,
            static_cast<double>(kMoonRadiusEarthRadii * kLunarSurfaceMarkerRadiusScale),
            moon_render_position));
        const float size = kMoonRadiusEarthRadii * kLunarSurfaceMarkerSizeScale
            * (selected ? 1.8f : 1.0f);
        const float quality_alpha = object.location_quality == SatViewLunarLocationQuality::Approximate
            ? 0.58f
            : 0.98f;
        const glm::vec4 color = selected
            ? glm::vec4(0.96f, 0.10f, 0.06f, 1.0f)
            : population_color(SatellitePopulation::ActivePayload, quality_alpha);
        const float style = lunar_surface_marker_style(object.kind);
        const auto add_marker = [&](float map_shift) {
            markers.push_back({
                glm::vec4(position, size),
                glm::vec4(position, selected ? 1.0f : 0.0f),
                color,
                glm::vec4(style, map_shift, 0.0f, 0.0f),
                glm::vec4(surface_normal, 1.0f),
            });
        };
        add_marker(0.0f);
        if (map_projection)
        {
            const float map_x = satview_map_position_from_lunar_body(
                body_direction,
                map_center_radians)
                                    .x;
            if (map_x > 0.9f)
                add_marker(-2.0f);
            else if (map_x < -0.9f)
                add_marker(2.0f);
        }
    }
}

} // namespace

void SatViewHost::rebuild_visible_stars()
{
    visible_stars_.clear();
    visible_stars_.reserve(stars_.size());
    for (const SatViewStarInstance& star : stars_)
    {
        const float magnitude = star.direction_magnitude.w;
        if (magnitude >= star_min_magnitude_ && magnitude <= star_max_magnitude_)
            visible_stars_.push_back(star);
    }
    if (scene_pass_)
        scene_pass_->set_stars(visible_stars_);
}

void SatViewHost::update_constellation_line_styles()
{
    for (SatViewCelestialLineInstance& line : constellation_lines_)
        line.start_direction_width.w = constellation_figure_width_;
    for (SatViewCelestialLineInstance& line : constellation_boundary_lines_)
    {
        line.start_direction_width.w = constellation_boundary_width_;
        line.end_direction_dash.w = 0.0f;
        line.color = kConstellationBoundaryColor;
        line.style = glm::vec4(0.0f);
    }
    if (scene_pass_)
    {
        scene_pass_->set_constellation_lines(constellation_lines_);
        scene_pass_->set_constellation_boundary_lines(constellation_boundary_lines_);
    }
}

void SatViewHost::refresh_scene_text_service()
{
    scene_text_atlas_.reset();
    if (!app_text_service_ || scene_font_path_.empty())
    {
        scene_text_service_.reset();
        return;
    }

    if (!scene_text_service_)
        scene_text_service_ = std::make_unique<TextService>();
    else
        scene_text_service_->shutdown();
    TextServiceConfig config;
    config.font_path = scene_font_path_;
    config.enable_ligatures = false;
    if (!scene_text_service_->initialize(
            config,
            app_text_service_->point_size(),
            display_ppi_))
    {
        DRAXUL_LOG_WARN(LogCategory::App,
            "SatView: scene text service unavailable; sky labels disabled");
        scene_text_service_.reset();
        return;
    }
    rebuild_scene_text_atlas();
    request_redraw();
}

void SatViewHost::rebuild_scene_text_atlas()
{
    if (!scene_text_service_ || !constellation_boundary_catalog_)
        return;
    const FontMetrics metrics = scene_text_service_->metrics();
    std::vector<TextAtlasRequest> requests;
    requests.reserve(constellation_boundary_catalog_->labels.size() + 4u);
    for (const SatViewConstellationAreaLabel& label : constellation_boundary_catalog_->labels)
    {
        const int codepoint_count = std::max(
            utf8_codepoint_indices(label.name).back(), 1);
        TextAtlasRequest request;
        request.key = constellation_label_key(label.area_index);
        request.text = label.name;
        request.target_pixel_size = {
            codepoint_count * std::max(metrics.cell_width, 1) + 8,
            std::max(metrics.cell_height, 1) + 8,
        };
        request.padding = 4;
        requests.push_back(std::move(request));
    }
    for (const char cardinal : std::string_view("NESW"))
    {
        TextAtlasRequest request;
        request.key = cardinal_label_key(cardinal);
        request.text.assign(1, cardinal);
        request.target_pixel_size = {
            std::max(metrics.cell_width, 1) + 8,
            std::max(metrics.cell_height, 1) + 8,
        };
        request.padding = 4;
        requests.push_back(std::move(request));
    }
    scene_text_atlas_ = std::make_shared<TextAtlas>(build_text_atlas(
        *scene_text_service_, requests, ++scene_text_atlas_revision_));
    if (scene_pass_ && scene_text_atlas_->image.valid())
    {
        scene_pass_->set_label_atlas(std::shared_ptr<const TextAtlasImage>(
            scene_text_atlas_, &scene_text_atlas_->image));
    }
}

void SatViewHost::rebuild_scene_labels(
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::dvec3& ground_observer,
    glm::vec4 moon_position_radius,
    glm::vec4 sun_position_radius)
{
    constellation_label_instances_.clear();
    cardinal_label_instances_.clear();
    if (!scene_pass_ || !scene_text_atlas_ || projection_mode_ == SatViewProjectionMode::Map
        || (!constellation_labels_enabled_
            && !(projection_mode_ == SatViewProjectionMode::Ground && cardinal_labels_enabled_)))
    {
        scene_pass_->set_constellation_labels({});
        scene_pass_->set_cardinal_labels({});
        return;
    }

    const bool ground = projection_mode_ == SatViewProjectionMode::Ground;
    const float aspect = static_cast<float>(std::max(scene_viewport_.pixel_size.x, 1))
        / static_cast<float>(std::max(scene_viewport_.pixel_size.y, 1));
    const glm::vec3 observer_up = glm::normalize(glm::vec3(ground_observer));
    const glm::vec3 eye = ground ? glm::vec3(ground_observer) : camera_->GetPosition();
    auto project_world_position = [&](glm::vec3 world_position) -> std::optional<glm::vec2> {
        if (ground)
        {
            const glm::vec3 direction = glm::normalize(world_position - eye);
            if (ground_horizon_occlusion_ && glm::dot(direction, observer_up) <= 0.0f)
                return std::nullopt;
            const glm::vec3 camera_direction = glm::normalize(glm::mat3(view) * direction);
            const SatViewSkyProjectionPoint point = satview_project_camera_direction(
                ground_projection_, camera_direction, ground_fov_degrees_, aspect);
            return point.valid ? std::optional<glm::vec2>(point.ndc) : std::nullopt;
        }
        const glm::vec4 clip = projection * view * glm::vec4(world_position, 1.0f);
        if (clip.w <= 0.000001f)
            return std::nullopt;
        return glm::vec2(clip) / clip.w;
    };
    auto project_direction = [&](glm::vec3 direction) -> std::optional<glm::vec2> {
        return project_world_position(
            eye + glm::normalize(direction) * kCelestialOverlayDistanceEarthRadii);
    };

    std::vector<SatViewLabelLayoutCandidate> candidates;
    if (constellation_labels_enabled_ && constellation_boundary_catalog_)
    {
        candidates.reserve(constellation_boundary_catalog_->labels.size() + 4u);
        for (const SatViewConstellationAreaLabel& label : constellation_boundary_catalog_->labels)
        {
            const auto entry = scene_text_atlas_->entries.find(
                constellation_label_key(label.area_index));
            const std::optional<glm::vec2> ndc = project_direction(label.direction);
            if (entry == scene_text_atlas_->entries.end() || !ndc)
                continue;
            SatViewLabelLayoutCandidate candidate;
            candidate.key = label.designation + ":" + std::to_string(label.area_index);
            candidate.rank = static_cast<int>(label.rank);
            candidate.ndc_center = *ndc;
            candidate.instance.direction_priority = glm::vec4(
                label.direction, static_cast<float>(label.rank));
            candidate.instance.uv_rect = entry->second.uv_rect;
            candidate.instance.pixel_size_offset = glm::vec4(
                glm::vec2(entry->second.pixel_size), 0.0f, 0.0f);
            candidate.instance.color = glm::vec4(0.70f, 0.74f, 0.78f, 0.88f);
            candidates.push_back(std::move(candidate));
        }
    }

    if (ground && cardinal_labels_enabled_)
    {
        constexpr std::array cardinal_data = {
            std::pair{ 'N', 0.0f },
            std::pair{ 'E', 90.0f },
            std::pair{ 'S', 180.0f },
            std::pair{ 'W', 270.0f },
        };
        for (const auto [cardinal, azimuth_degrees] : cardinal_data)
        {
            const glm::vec3 local = satview_local_azimuth_altitude_direction(
                glm::radians(azimuth_degrees), glm::radians(4.0f));
            const glm::vec3 direction = glm::vec3(satview_ground_local_direction_to_render(
                ground_observer, glm::dvec3(local)));
            const auto entry = scene_text_atlas_->entries.find(cardinal_label_key(cardinal));
            const std::optional<glm::vec2> ndc = project_direction(direction);
            if (entry == scene_text_atlas_->entries.end() || !ndc)
                continue;
            SatViewLabelLayoutCandidate candidate;
            candidate.key = cardinal_label_key(cardinal);
            candidate.rank = 0;
            candidate.ndc_center = *ndc;
            candidate.instance.direction_priority = glm::vec4(direction, 0.0f);
            candidate.instance.uv_rect = entry->second.uv_rect;
            candidate.instance.pixel_size_offset = glm::vec4(
                glm::vec2(entry->second.pixel_size), 0.0f, 0.0f);
            candidate.instance.color = glm::vec4(0.72f, 0.84f, 0.96f, 0.98f);
            candidates.push_back(std::move(candidate));
        }
    }

    std::vector<SatViewLabelLayoutExclusion> exclusions;
    const glm::vec3 camera_right = glm::normalize(glm::vec3(glm::inverse(view)[0]));
    auto reserve_body = [&](glm::vec4 position_radius, bool enabled) {
        if (!enabled || position_radius.w <= 0.0f)
            return;
        const glm::vec3 center_world(position_radius);
        const std::optional<glm::vec2> center = project_world_position(center_world);
        const std::optional<glm::vec2> edge = project_world_position(
            center_world + camera_right * position_radius.w);
        if (!center || !edge)
            return;
        const glm::vec2 pixel_delta = (*edge - *center)
            * glm::vec2(scene_viewport_.pixel_size) * 0.5f;
        const float diameter = std::max(glm::length(pixel_delta) * 2.0f + 16.0f, 32.0f);
        exclusions.push_back({ *center, glm::vec2(diameter) });
    };
    const bool legacy_earth_moon_view = !satview_uses_generic_body_view(camera_pov_);
    reserve_body(moon_position_radius, legacy_earth_moon_view && moon_enabled_);
    const bool contextual_sun = !legacy_earth_moon_view
        && camera_pov_ != SatViewCameraPov::Sun
        && sun_enabled_;
    reserve_body(
        sun_position_radius,
        (legacy_earth_moon_view && sun_enabled_) || contextual_sun);

    for (const SatViewLabelInstance& label : layout_satview_labels(
             std::move(candidates), scene_viewport_.pixel_size, exclusions))
    {
        if (label.direction_priority.w <= 0.0f)
            cardinal_label_instances_.push_back(label);
        else
            constellation_label_instances_.push_back(label);
    }
    scene_pass_->set_constellation_labels(constellation_label_instances_);
    scene_pass_->set_cardinal_labels(cardinal_label_instances_);
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
    const bool pov_changed = camera_pov_ != pov;
    const bool central_body_changed = pov == SatViewCameraPov::Moon
        ? !filter_.show_moon || filter_.show_earth
        : pov == SatViewCameraPov::Earth
        ? !filter_.show_earth || filter_.show_moon
        : filter_.show_earth || filter_.show_moon;
    if (pov == SatViewCameraPov::Moon)
    {
        moon_enabled_ = true;
        satview_select_central_body(filter_, CentralBody::Moon);
    }
    else if (pov == SatViewCameraPov::Earth)
    {
        satview_select_central_body(filter_, CentralBody::Earth);
    }
    else
    {
        satview_select_central_body(filter_, CentralBody::Other);
    }
    if (pov == SatViewCameraPov::Sun)
        sun_enabled_ = true;
    if (pov != SatViewCameraPov::Earth
        && projection_mode_ == SatViewProjectionMode::Ground)
    {
        projection_mode_ = SatViewProjectionMode::Globe;
    }
    if (!pov_changed)
    {
        if (central_body_changed)
        {
            simulation_settings_dirty_ = true;
            invalidate_visual_buffers();
            request_redraw();
        }
        return;
    }

    const float normalized_distance = camera_->GetDistance() / camera_target_radius(camera_pov_);
    camera_pov_ = pov;
    simulation_settings_dirty_ = true;

    const SatViewMoonPosition moon = satview_moon_position(simulation_seconds);
    const SatViewSunPosition sun = satview_sun_position(simulation_seconds);
    const float target_radius = camera_target_radius(camera_pov_);
    camera_->ClearMotion();
    camera_manipulator_->Cancel();
    camera_keys_->reset();
    camera_->SetDistanceLimits(
        camera_min_distance(camera_pov_),
        kCameraMaxDistanceCap);
    camera_->SetFocalPointAndDistance(
        camera_target_position(camera_pov_, moon, sun),
        normalized_distance * target_radius);
    invalidate_visual_buffers();
    request_redraw();
}

void SatViewHost::reset_camera()
{
    if (projection_mode_ == SatViewProjectionMode::Ground)
    {
        ground_camera_orientation_ = satview_default_ground_camera_orientation();
        request_redraw();
        return;
    }

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
    const SatViewSunPosition sun_position = satview_sun_position(simulation_seconds);
    const glm::vec3 target = camera_target_position(camera_pov_, moon, sun_position);
    const float target_radius = camera_target_radius(camera_pov_);
    camera_->ClearMotion();
    camera_->SetDistanceLimits(
        camera_min_distance(camera_pov_),
        kCameraMaxDistanceCap);
    camera_->SetPositionAndFocalPoint(
        target + camera_position_from_yaw_pitch(std::atan2(sun.x, sun.z) + 0.65f, 0.25f, std::min(kCameraDefaultDistance * target_radius, camera_->GetMaxDistance())),
        target);
    request_redraw();
}

void SatViewHost::reset_to_default_settings()
{
    apply_config(SatViewConfig{});
    selected_norad_catalog_id_.reset();
    selected_lunar_surface_index_.reset();
    selected_natural_body_.reset();
    map_center_radians_ = glm::vec2(0.0f);
    ground_camera_orientation_ = satview_default_ground_camera_orientation();
    moon_track_center_seconds_.reset();
    earth_track_center_seconds_.reset();
    uploaded_earth_track_visible_ = false;
    dragging_ = false;
    pending_click_ = false;
    paused_ = false;
    simulated_seconds_ = unix_seconds_now();
    last_draw_simulation_seconds_ = simulated_seconds_;

    const glm::vec3 sun = glm::vec3(solar_direction_render(simulated_seconds_));
    const SatViewMoonPosition moon = satview_moon_position(simulated_seconds_);
    const SatViewSunPosition sun_position = satview_sun_position(simulated_seconds_);
    const glm::vec3 target = camera_target_position(camera_pov_, moon, sun_position);
    const float target_radius = camera_target_radius(camera_pov_);
    camera_->ClearMotion();
    camera_manipulator_->Cancel();
    camera_keys_->reset();
    camera_->SetDistanceLimits(
        camera_min_distance(camera_pov_),
        std::max(kCameraDefaultMaxDistance, kCameraDefaultMaxDistance * target_radius));
    camera_->SetPositionAndFocalPoint(
        target + camera_position_from_yaw_pitch(std::atan2(sun.x, sun.z) + 0.65f, 0.25f, kCameraDefaultDistance * target_radius),
        target);

    if (simulation_worker_)
        simulation_worker_->set_clock(simulated_seconds_, time_speed_, paused_);
    simulation_settings_dirty_ = true;
    config_dirty_ = true;
    invalidate_visual_buffers();
    request_redraw();
}

void SatViewHost::pan_map(glm::vec2 delta_radians)
{
    map_center_radians_ = normalized_satview_map_center(map_center_radians_ + delta_radians);
    request_redraw();
}

void SatViewHost::enter_ground_view_at(glm::dvec2 longitude_latitude_radians)
{
    projection_mode_ = SatViewProjectionMode::Ground;
    camera_pov_ = SatViewCameraPov::Earth;
    satview_select_central_body(filter_, CentralBody::Earth);
    ground_location_radians_ = longitude_latitude_radians;
    ground_camera_orientation_ = satview_default_ground_camera_orientation();
    camera_->ClearMotion();
    camera_manipulator_->Cancel();
    camera_keys_->reset();
    invalidate_visual_buffers();
    simulation_settings_dirty_ = true;
    config_dirty_ = true;
    request_redraw();
}

bool SatViewHost::enter_ground_view_from_screen(glm::ivec2 screen_pos, double simulation_seconds)
{
    if (projection_mode_ == SatViewProjectionMode::Map)
    {
        if (camera_pov_ != SatViewCameraPov::Earth)
            return false;
        const SatViewGroundLocation location = satview_ground_location_from_map_ndc(
            screen_ndc(screen_pos, scene_viewport_),
            map_center_radians_);
        enter_ground_view_at(glm::dvec2(location.longitude_radians, location.latitude_radians));
        return true;
    }

    if (projection_mode_ != SatViewProjectionMode::Globe || camera_pov_ != SatViewCameraPov::Earth)
        return false;

    const int pixel_w = std::max(1, scene_viewport_.pixel_size.x);
    const int pixel_h = std::max(1, scene_viewport_.pixel_size.y);
    const glm::vec2 ndc = screen_ndc(screen_pos, scene_viewport_);
    const float earth_scene_radius = visible_scene_radius(
        nullptr,
        filter_,
        selected_norad_catalog_id_,
        track_display_mode_,
        satellite_display_mode_);
    const glm::mat4 view = camera_view_matrix(*camera_);
    const glm::mat4 proj = reversed_z_perspective(
        glm::radians(camera_->GetFieldOfView()),
        static_cast<float>(pixel_w) / static_cast<float>(pixel_h),
        camera_near_plane(camera_pov_, camera_->GetDistance()),
        camera_far_plane(camera_->GetDistance(), earth_scene_radius));
    const glm::mat4 inv_view_proj = glm::inverse(proj * view);
    // Reversed-Z: NDC depth 1 is the near plane, 0 is the far plane.
    glm::vec4 near_clip(ndc, 1.0f, 1.0f);
    glm::vec4 far_clip(ndc, 0.0f, 1.0f);
    near_clip = inv_view_proj * near_clip;
    far_clip = inv_view_proj * far_clip;
    const glm::dvec3 origin = glm::dvec3(near_clip) / static_cast<double>(near_clip.w);
    const glm::dvec3 far_point = glm::dvec3(far_clip) / static_cast<double>(far_clip.w);
    const glm::dvec3 direction = glm::normalize(far_point - origin);
    const std::optional<glm::dvec3> hit = ray_sphere_hit(origin, direction, 1.0);
    if (!hit.has_value())
        return false;

    const SatViewGroundLocation location = satview_ground_location_from_render_position(*hit, simulation_seconds);
    enter_ground_view_at(glm::dvec2(location.longitude_radians, location.latitude_radians));
    return true;
}

glm::dvec3 SatViewHost::ground_observer_render_position(double simulation_seconds) const
{
    return satview_ground_render_position(
        SatViewGroundLocation{
            .longitude_radians = ground_location_radians_.x,
            .latitude_radians = ground_location_radians_.y,
        },
        simulation_seconds);
}

void SatViewHost::rebuild_object_tree(const SatViewSimulationSnapshot* snapshot)
{
    if (catalog_snapshot_.objects.empty())
    {
        object_tree_catalog_generation_ = 0;
        object_tree_state_count_ = 0;
        object_tree_entries_.clear();
        filtered_object_tree_indices_.clear();
        return;
    }
    const std::size_t state_count = snapshot ? snapshot->states.size() : 0;
    if (object_tree_catalog_generation_ == simulation_catalog_generation_
        && object_tree_state_count_ == state_count)
    {
        return;
    }

    object_tree_catalog_generation_ = simulation_catalog_generation_;
    object_tree_state_count_ = state_count;
    object_tree_entries_.clear();
    object_tree_entries_.reserve(catalog_snapshot_.objects.size());
    std::unordered_map<std::int64_t, std::size_t> state_indices;
    if (snapshot)
    {
        state_indices.reserve(snapshot->states.size());
        for (std::size_t state_index = 0; state_index < snapshot->states.size(); ++state_index)
            state_indices.emplace(snapshot->states[state_index].norad_catalog_id, state_index);
    }
    for (std::size_t catalog_index = 0; catalog_index < catalog_snapshot_.objects.size(); ++catalog_index)
    {
        const SatelliteRecord& record = catalog_snapshot_.objects[catalog_index];
        const auto state = state_indices.find(record.norad_catalog_id);
        object_tree_entries_.push_back({
            record.central_body,
            record.population,
            record.orbit_class,
            normalized_satellite_prefix(record.object_name),
            object_tree_label(record),
            record.object_name,
            record.norad_catalog_id,
            catalog_index,
            state == state_indices.end()
                ? std::optional<std::size_t>{}
                : std::optional<std::size_t>{ state->second },
        });
    }

    std::sort(object_tree_entries_.begin(), object_tree_entries_.end(),
        [](const ObjectTreeEntry& a, const ObjectTreeEntry& b) {
            const int population_a = population_sort_key(a.population);
            const int population_b = population_sort_key(b.population);
            if (population_a != population_b)
                return population_a < population_b;
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

    if (catalog_snapshot_.objects.empty())
    {
        ImGui::TextDisabled("Object tree pending catalog.");
        return;
    }

    filtered_object_tree_indices_.clear();
    filtered_object_tree_indices_.reserve(object_tree_entries_.size());
    for (std::size_t entry_index = 0; entry_index < object_tree_entries_.size(); ++entry_index)
    {
        const ObjectTreeEntry& entry = object_tree_entries_[entry_index];
        const bool visible_state = snapshot
            && entry.state_index.has_value()
            && *entry.state_index < snapshot->states.size()
            && satellite_visible(filter_, snapshot->states[*entry.state_index], snapshot->source_label);
        const bool visible_catalog_only = !entry.state_index.has_value()
            && entry.catalog_index < catalog_snapshot_.objects.size()
            && satview_filter_matches(
                filter_,
                make_satview_filter_candidate(catalog_snapshot_.objects[entry.catalog_index]));
        if (visible_state || visible_catalog_only)
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
    std::size_t population_begin = 0;
    while (population_begin < filtered_object_tree_indices_.size())
    {
        const SatellitePopulation population = filtered_entry(population_begin).population;
        std::size_t population_end = population_begin + 1;
        while (population_end < filtered_object_tree_indices_.size()
            && filtered_entry(population_end).population == population)
        {
            ++population_end;
        }

        const std::string_view population_name = satellite_population_name(population);
        ImGui::PushID(population_sort_key(population));
        if (color_mode_ == SatViewColorMode::Population)
        {
            const glm::vec4 color = population_color(population, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color.r, color.g, color.b, color.a));
        }
        const bool population_open = ImGui::TreeNodeEx(
            "##population",
            group_flags,
            "%.*s (%zu)",
            static_cast<int>(population_name.size()),
            population_name.data(),
            population_end - population_begin);
        if (color_mode_ == SatViewColorMode::Population)
            ImGui::PopStyleColor();
        if (population_open)
        {
            std::size_t orbit_begin = population_begin;
            while (orbit_begin < population_end)
            {
                const OrbitClass orbit_class = filtered_entry(orbit_begin).orbit_class;
                std::size_t orbit_end = orbit_begin + 1;
                while (orbit_end < population_end
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
                                for (int local_index = clipper.DisplayStart;
                                    local_index < clipper.DisplayEnd;
                                    ++local_index)
                                {
                                    const ObjectTreeEntry& entry = filtered_entry(
                                        prefix_begin + static_cast<std::size_t>(local_index));
                                    const bool selected = selected_norad_catalog_id_.has_value()
                                        && entry.norad_catalog_id == *selected_norad_catalog_id_;
                                    ImGui::PushID(entry.label.c_str());
                                    if (ImGui::Selectable(entry.label.c_str(), selected))
                                    {
                                        selected_norad_catalog_id_ = entry.norad_catalog_id;
                                        selected_lunar_surface_index_.reset();
                                        selected_natural_body_.reset();
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
            ImGui::TreePop();
        }
        ImGui::PopID();
        population_begin = population_end;
    }

    ImGui::EndChild();
}

void SatViewHost::render_lunar_surface_tree(bool& changed)
{
    if (!lunar_surface_catalog_)
    {
        ImGui::TextDisabled("Lunar surface catalogue pending.");
        return;
    }
    if (!lunar_surface_catalog_->error.empty())
    {
        ImGui::TextWrapped("%s", lunar_surface_catalog_->error.c_str());
        return;
    }

    std::vector<std::size_t> visible_indices;
    visible_indices.reserve(lunar_surface_catalog_->objects.size());
    for (std::size_t index = 0; index < lunar_surface_catalog_->objects.size(); ++index)
    {
        const SatViewLunarSurfaceObject& object = lunar_surface_catalog_->objects[index];
        const bool selected = selected_lunar_surface_index_.has_value()
            && *selected_lunar_surface_index_ == index;
        if (selected || lunar_surface_kind_visible(object, show_lunar_landers_, show_lunar_rovers_, show_lunar_instruments_, show_lunar_impacts_, show_lunar_crewed_artifacts_, show_lunar_approximate_locations_))
        {
            visible_indices.push_back(index);
        }
    }
    std::sort(visible_indices.begin(), visible_indices.end(), [&](std::size_t left, std::size_t right) {
        const SatViewLunarSurfaceObject& a = lunar_surface_catalog_->objects[left];
        const SatViewLunarSurfaceObject& b = lunar_surface_catalog_->objects[right];
        if (a.mission_name != b.mission_name)
            return a.mission_name < b.mission_name;
        if (a.parent_site_id != b.parent_site_id)
            return a.parent_site_id < b.parent_site_id;
        if (a.display_rank != b.display_rank)
            return a.display_rank < b.display_rank;
        return a.display_name < b.display_name;
    });

    ImGui::Text("Surface objects: %zu filtered / %zu total (%zu sites)",
        visible_indices.size(),
        lunar_surface_catalog_->objects.size(),
        lunar_surface_catalog_->site_count);
    if (visible_indices.empty())
    {
        ImGui::TextDisabled("No lunar surface objects match the filters.");
        return;
    }

    const ImGuiTreeNodeFlags group_flags = ImGuiTreeNodeFlags_SpanAvailWidth;
    const float tree_height = std::max(180.0f, ImGui::GetTextLineHeightWithSpacing() * 14.0f);
    if (!ImGui::BeginChild(
            "##satview_lunar_surface_tree",
            ImVec2(0.0f, tree_height),
            ImGuiChildFlags_Border,
            ImGuiWindowFlags_AlwaysVerticalScrollbar))
    {
        ImGui::EndChild();
        return;
    }

    const auto object_at = [&](std::size_t visible_index) -> const SatViewLunarSurfaceObject& {
        return lunar_surface_catalog_->objects[visible_indices[visible_index]];
    };
    std::size_t mission_begin = 0;
    while (mission_begin < visible_indices.size())
    {
        const std::string& mission_id = object_at(mission_begin).mission_id;
        std::size_t mission_end = mission_begin + 1;
        while (mission_end < visible_indices.size()
            && object_at(mission_end).mission_id == mission_id)
        {
            ++mission_end;
        }

        ImGui::PushID(mission_id.c_str());
        const SatViewLunarSurfaceObject& first = object_at(mission_begin);
        const bool mission_open = ImGui::TreeNodeEx(
            "##mission",
            group_flags,
            "%s (%zu)",
            first.mission_name.c_str(),
            mission_end - mission_begin);
        if (mission_open)
        {
            std::size_t site_begin = mission_begin;
            while (site_begin < mission_end)
            {
                const std::string& site_id = object_at(site_begin).parent_site_id;
                std::size_t site_end = site_begin + 1;
                while (site_end < mission_end
                    && object_at(site_end).parent_site_id == site_id)
                {
                    ++site_end;
                }

                const SatViewLunarSurfaceObject* representative = &object_at(site_begin);
                for (std::size_t index = site_begin; index < site_end; ++index)
                {
                    if (object_at(index).site_representative)
                    {
                        representative = &object_at(index);
                        break;
                    }
                }
                ImGui::PushID(site_id.c_str());
                const bool site_open = ImGui::TreeNodeEx(
                    "##site",
                    group_flags,
                    "%s (%zu)",
                    representative->display_name.c_str(),
                    site_end - site_begin);
                if (site_open)
                {
                    for (std::size_t index = site_begin; index < site_end; ++index)
                    {
                        const std::size_t object_index = visible_indices[index];
                        const SatViewLunarSurfaceObject& object = object_at(index);
                        const bool selected = selected_lunar_surface_index_.has_value()
                            && *selected_lunar_surface_index_ == object_index;
                        const std::string label = object.display_name + " ("
                            + std::string(satview_lunar_surface_kind_name(object.kind)) + ")";
                        ImGui::PushID(object.id.c_str());
                        if (ImGui::Selectable(label.c_str(), selected))
                        {
                            selected_lunar_surface_index_ = object_index;
                            selected_norad_catalog_id_.reset();
                            selected_natural_body_.reset();
                            sync_simulation_render_settings();
                            changed = true;
                        }
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
                site_begin = site_end;
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
        mission_begin = mission_end;
    }

    ImGui::EndChild();
}

void SatViewHost::render_dockspace(bool keep_alive_only)
{
    ImGuiWindowFlags root_flags = ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus
        | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoBackground;
    if (keep_alive_only)
        root_flags |= ImGuiWindowFlags_NoInputs;
    const ImVec2 pane_position(
        static_cast<float>(viewport_.pixel_pos.x),
        static_cast<float>(viewport_.pixel_pos.y));
    const ImVec2 pane_size(
        static_cast<float>(std::max(viewport_.pixel_size.x, 1)),
        static_cast<float>(std::max(viewport_.pixel_size.y, 1)));
    ImGui::SetNextWindowPos(pane_position);
    ImGui::SetNextWindowSize(pane_size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##satview_dockspace_root", nullptr, root_flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockspace_id = ImGui::GetID(kSatViewDockspaceName);
    if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr)
    {
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, pane_size);
        const float left_ratio = std::clamp(
            kControlPanelDefaultWidth / pane_size.x,
            0.20f,
            0.48f);
        ImGuiID dock_left = 0;
        ImGuiID dock_scene = 0;
        ImGui::DockBuilderSplitNode(
            dockspace_id,
            ImGuiDir_Left,
            left_ratio,
            &dock_left,
            &dock_scene);
        ImGui::DockBuilderDockWindow(kSatViewSceneWindowName, dock_scene);
        ImGui::DockBuilderDockWindow(kSatViewViewWindowName, dock_left);
        ImGui::DockBuilderDockWindow(kSatViewRenderingWindowName, dock_left);
        ImGui::DockBuilderDockWindow(kSatViewFilterWindowName, dock_left);
        ImGui::DockBuilderDockWindow(kSatViewSelectionWindowName, dock_left);
        ImGui::DockBuilderDockWindow(kSatViewAboutWindowName, dock_left);
        ImGui::DockBuilderFinish(dockspace_id);
        ImGui::SetWindowFocus(kSatViewViewWindowName);
    }
    const ImGuiDockNodeFlags dockspace_flags = keep_alive_only
        ? ImGuiDockNodeFlags_KeepAliveOnly
        : ImGuiDockNodeFlags_None;
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
    ImGui::End();
}

void SatViewHost::render_scene_panel()
{
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoBackground;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(kSatViewSceneWindowName, nullptr, flags);
    ImGui::PopStyleVar();
    if (visible)
    {
        const ImVec2 content_position = ImGui::GetCursorScreenPos();
        const ImVec2 content_size = ImGui::GetContentRegionAvail();
        const int pane_left = viewport_.pixel_pos.x;
        const int pane_top = viewport_.pixel_pos.y;
        const int pane_right = pane_left + std::max(viewport_.pixel_size.x, 1);
        const int pane_bottom = pane_top + std::max(viewport_.pixel_size.y, 1);
        const int left = std::clamp(static_cast<int>(std::floor(content_position.x)), pane_left, pane_right);
        const int top = std::clamp(static_cast<int>(std::floor(content_position.y)), pane_top, pane_bottom);
        const int right = std::clamp(
            static_cast<int>(std::ceil(content_position.x + content_size.x)),
            left,
            pane_right);
        const int bottom = std::clamp(
            static_cast<int>(std::ceil(content_position.y + content_size.y)),
            top,
            pane_bottom);
        scene_viewport_.pixel_pos = glm::ivec2(left, top);
        scene_viewport_.pixel_size = glm::ivec2(
            std::max(right - left, 1),
            std::max(bottom - top, 1));
        ImGui::Dummy(ImVec2(
            std::max(content_size.x, 1.0f),
            std::max(content_size.y, 1.0f)));
    }
    ImGui::End();
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

    render_dockspace(!show_ui_panel_);
    if (show_ui_panel_)
    {
        render_scene_panel();
        render_control_panel(snapshot);
    }
    if (show_hdr_debug_panel_ && scene_pass_)
        scene_pass_->render_hdr_debug_ui();

    ImGui::Render();
}

void SatViewHost::render_view_display_controls(bool& changed)
{
    auto set_control_width = [](const char* label) {
        ImGui::SetNextItemWidth(control_widget_width(label));
    };

    ImGui::SeparatorText("Show");
    if (ImGui::Checkbox("Clouds", &clouds_enabled_))
        request_redraw();
    if (ImGui::Checkbox("Show atmosphere", &atmosphere_enabled_))
        request_redraw();
    ImGui::SameLine();
    if (camera_pov_ == SatViewCameraPov::Moon)
        ImGui::BeginDisabled();
    if (ImGui::Checkbox("Moon", &moon_enabled_))
        request_redraw();
    if (camera_pov_ == SatViewCameraPov::Moon)
        ImGui::EndDisabled();
    ImGui::SameLine();
    if (camera_pov_ == SatViewCameraPov::Sun)
        ImGui::BeginDisabled();
    if (ImGui::Checkbox("Sun", &sun_enabled_))
        request_redraw();
    if (camera_pov_ == SatViewCameraPov::Sun)
        ImGui::EndDisabled();
    if (ImGui::Checkbox("Lunar surface objects", &lunar_surface_objects_enabled_))
        changed = true;
    const bool moon_track_available = camera_pov_ == SatViewCameraPov::Earth;
    if (!moon_track_available)
        ImGui::BeginDisabled();
    if (ImGui::Checkbox("Moon track", &moon_track_enabled_))
    {
        track_buffer_dirty_ = true;
        request_redraw();
    }
    if (!moon_track_available)
        ImGui::EndDisabled();
    ImGui::SameLine();
    const bool earth_track_available = camera_pov_ == SatViewCameraPov::Sun;
    if (!earth_track_available)
        ImGui::BeginDisabled();
    if (ImGui::Checkbox("Earth track", &earth_track_enabled_))
    {
        track_buffer_dirty_ = true;
        request_redraw();
    }
    if (!earth_track_available)
        ImGui::EndDisabled();
    if (camera_pov_ == SatViewCameraPov::Sun)
    {
        ImGui::SeparatorText("Planet tracks");
        const auto planet_track_checkbox = [&](SatViewCameraPov body) {
            bool enabled = satview_planet_track_enabled(planet_tracks_, body);
            if (ImGui::Checkbox(camera_pov_name(body), &enabled))
            {
                satview_set_planet_track_enabled(planet_tracks_, body, enabled);
                track_buffer_dirty_ = true;
                request_redraw();
            }
        };
        for (std::size_t index = 0; index < kSatViewPlanetTrackBodies.size(); ++index)
        {
            if (index % 2 == 1)
                ImGui::SameLine();
            planet_track_checkbox(kSatViewPlanetTrackBodies[index]);
        }
        if (ImGui::Button("All planet tracks"))
        {
            for (const SatViewCameraPov body : kSatViewPlanetTrackBodies)
                satview_set_planet_track_enabled(planet_tracks_, body, true);
            track_buffer_dirty_ = true;
            request_redraw();
        }
        ImGui::SameLine();
        if (ImGui::Button("No planet tracks"))
        {
            for (const SatViewCameraPov body : kSatViewPlanetTrackBodies)
                satview_set_planet_track_enabled(planet_tracks_, body, false);
            track_buffer_dirty_ = true;
            request_redraw();
        }
    }

    float star_min_magnitude = star_min_magnitude_;
    set_control_width("Star min mag");
    if (ImGui::SliderFloat(
            "Star min mag", &star_min_magnitude,
            kMinimumStarMagnitude, kMaximumStarMagnitude, "%.1f"))
    {
        star_min_magnitude_ = std::min(
            std::clamp(star_min_magnitude, kMinimumStarMagnitude, kMaximumStarMagnitude),
            star_max_magnitude_);
        rebuild_visible_stars();
        request_redraw();
    }
    float star_max_magnitude = star_max_magnitude_;
    set_control_width("Star max mag");
    if (ImGui::SliderFloat(
            "Star max mag", &star_max_magnitude,
            kMinimumStarMagnitude, kMaximumStarMagnitude, "%.1f"))
    {
        star_max_magnitude_ = std::max(
            std::clamp(star_max_magnitude, kMinimumStarMagnitude, kMaximumStarMagnitude),
            star_min_magnitude_);
        rebuild_visible_stars();
        request_redraw();
    }
    if (ImGui::Checkbox("Constellation figures", &constellation_lines_enabled_))
        request_redraw();
    if (ImGui::Checkbox("Constellation boundaries", &constellation_boundaries_enabled_))
        request_redraw();
    if (ImGui::Checkbox("Constellation labels", &constellation_labels_enabled_))
        request_redraw();
    if (ImGui::Checkbox("Milky Way background", &milky_way_enabled_))
        request_redraw();

    int satellite_display_index = 0;
    switch (satellite_display_mode_)
    {
    case SatViewSatelliteDisplayMode::TracksAndMarkers:
        satellite_display_index = 0;
        break;
    case SatViewSatelliteDisplayMode::TracksOnly:
        satellite_display_index = 1;
        break;
    case SatViewSatelliteDisplayMode::MarkersOnly:
        satellite_display_index = 2;
        break;
    }
    const char* satellite_display_modes[] = {
        "Tracks + Satellites", "Tracks Only", "Satellites Only"
    };
    set_control_width("Display");
    if (ImGui::Combo("Display", &satellite_display_index, satellite_display_modes, 3))
    {
        satellite_display_mode_ = satellite_display_index == 1
            ? SatViewSatelliteDisplayMode::TracksOnly
            : satellite_display_index == 2
            ? SatViewSatelliteDisplayMode::MarkersOnly
            : SatViewSatelliteDisplayMode::TracksAndMarkers;
        changed = true;
    }

    if (!satellite_display_shows_tracks(satellite_display_mode_))
        ImGui::BeginDisabled();
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
    if (!satellite_display_shows_tracks(satellite_display_mode_))
        ImGui::EndDisabled();
}

void SatViewHost::render_visual_controls()
{
    auto set_control_width = [](const char* label) {
        ImGui::SetNextItemWidth(control_widget_width(label));
    };

    if (!clouds_enabled_)
        ImGui::BeginDisabled();
    if (ImGui::Checkbox("Realistic clouds", &realistic_clouds_enabled_))
        request_redraw();
    if (!clouds_enabled_)
        ImGui::EndDisabled();

    float star_brightness = star_brightness_scale_;
    set_control_width("Star brightness");
    if (ImGui::SliderFloat(
            "Star brightness", &star_brightness,
            kMinimumStarBrightnessScale, kMaximumStarBrightnessScale, "%.2fx"))
    {
        star_brightness_scale_ = std::clamp(
            star_brightness,
            kMinimumStarBrightnessScale,
            kMaximumStarBrightnessScale);
        request_redraw();
    }
    float constellation_figure_width = constellation_figure_width_;
    set_control_width("Constellation figure width");
    if (ImGui::SliderFloat(
            "Constellation figure width", &constellation_figure_width,
            kMinimumConstellationLineWidth, kMaximumConstellationLineWidth, "%.1f px"))
    {
        constellation_figure_width_ = std::clamp(
            constellation_figure_width,
            kMinimumConstellationLineWidth,
            kMaximumConstellationLineWidth);
        update_constellation_line_styles();
        request_redraw();
    }
    float constellation_boundary_width = constellation_boundary_width_;
    set_control_width("Constellation boundary width");
    if (ImGui::SliderFloat(
            "Constellation boundary width", &constellation_boundary_width,
            kMinimumConstellationLineWidth, kMaximumConstellationLineWidth, "%.1f px"))
    {
        constellation_boundary_width_ = std::clamp(
            constellation_boundary_width,
            kMinimumConstellationLineWidth,
            kMaximumConstellationLineWidth);
        update_constellation_line_styles();
        request_redraw();
    }
    float milky_way_brightness = milky_way_brightness_;
    set_control_width("Milky Way brightness");
    if (ImGui::SliderFloat(
            "Milky Way brightness", &milky_way_brightness,
            kMinimumMilkyWayBrightness, kMaximumMilkyWayBrightness, "%.2f"))
    {
        milky_way_brightness_ = std::clamp(
            milky_way_brightness,
            kMinimumMilkyWayBrightness,
            kMaximumMilkyWayBrightness);
        request_redraw();
    }

    if (projection_mode_ != SatViewProjectionMode::Ground)
        ImGui::BeginDisabled();
    float ground_marker_scale = ground_marker_scale_;
    set_control_width("Ground marker scale");
    if (ImGui::SliderFloat(
            "Ground marker scale",
            &ground_marker_scale,
            kGroundMinimumMarkerScale,
            kGroundMaximumMarkerScale,
            "%.2fx",
            ImGuiSliderFlags_Logarithmic))
    {
        ground_marker_scale_ = clamp_ground_marker_scale(ground_marker_scale);
        marker_buffer_dirty_ = true;
        request_redraw();
    }
    if (projection_mode_ != SatViewProjectionMode::Ground)
        ImGui::EndDisabled();
}

void SatViewHost::render_control_panel(const SatViewSimulationSnapshot* snapshot)
{
    bool changed = false;
    const SatViewConfig config_before = current_config();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    auto set_control_width = [](const char* label) {
        ImGui::SetNextItemWidth(control_widget_width(label));
    };
    const double displayed_simulation_seconds = snapshot
        ? render_simulation_seconds(*snapshot)
        : last_draw_simulation_seconds_;
    const auto catalog_snapshot = catalog_service_.status();
    const bool show_tracks = satellite_display_shows_tracks(satellite_display_mode_);
    const bool show_markers = satellite_display_shows_markers(satellite_display_mode_);

    if (ImGui::Begin(kSatViewViewWindowName, nullptr, flags))
    {
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
        if (ImGui::Button("Reset Defaults"))
        {
            reset_to_default_settings();
            changed = true;
        }

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
        ImGui::SameLine();
        if (ImGui::RadioButton("Ground", projection_mode_ == SatViewProjectionMode::Ground))
        {
            enter_ground_view_at(ground_location_radians_);
            changed = true;
        }

        ImGui::TextUnformatted("POV");
        ImGui::SameLine();
        ImGui::PushID("camera_pov");
        if (projection_mode_ == SatViewProjectionMode::Ground)
            ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(std::max(190.0f, ImGui::GetContentRegionAvail().x));
        if (ImGui::BeginCombo("##body", satview_solar_system_body(camera_pov_).name.data()))
        {
            std::string_view previous_system;
            for (const SatViewSolarSystemBody& body : satview_solar_system_bodies())
            {
                if (body.system_name != previous_system)
                {
                    if (!previous_system.empty())
                        ImGui::Separator();
                    ImGui::TextDisabled("%.*s",
                        static_cast<int>(body.system_name.size()), body.system_name.data());
                    previous_system = body.system_name;
                }
                const bool selected = camera_pov_ == body.id;
                const std::string label = body.parent.has_value()
                        && *body.parent != SatViewCameraPov::Sun
                    ? std::string("   ") + std::string(body.name)
                    : std::string(body.name);
                if (ImGui::Selectable(label.c_str(), selected))
                {
                    set_camera_pov(body.id, displayed_simulation_seconds);
                    changed = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (projection_mode_ == SatViewProjectionMode::Ground)
            ImGui::EndDisabled();
        ImGui::PopID();
        const SatViewSolarSystemBody& selected_body = satview_solar_system_body(camera_pov_);
        if (satview_uses_generic_body_view(camera_pov_))
        {
            ImGui::TextDisabled(
                "Equatorial radius: %.0f km%s",
                selected_body.equatorial_radius_km,
                selected_body.polar_radius_km < selected_body.equatorial_radius_km * 0.995
                    ? " (ellipsoid)"
                    : "");
        }

        if (!satview_uses_generic_body_view(camera_pov_))
        {
            int central_body_index = filter_.show_earth && filter_.show_moon
                ? 2
                : (filter_.show_moon ? 1 : 0);
            const char* central_body_options[] = { "Earth", "Moon", "All" };
            set_control_width("Central body");
            if (ImGui::Combo("Central body", &central_body_index, central_body_options, 3))
            {
                filter_.show_earth = central_body_index != 1;
                filter_.show_moon = central_body_index != 0;
                simulation_settings_dirty_ = true;
                changed = true;
            }
        }
        else
        {
            ImGui::TextDisabled("Approximate local tracks: major natural satellites");
            if (camera_pov_ != SatViewCameraPov::Sun)
            {
                if (selected_body.parent.has_value()
                    && *selected_body.parent != SatViewCameraPov::Sun)
                {
                    const SatViewSolarSystemBody& parent = satview_solar_system_body(*selected_body.parent);
                    ImGui::TextDisabled(
                        "Sky context: Sun and %.*s",
                        static_cast<int>(parent.name.size()),
                        parent.name.data());
                }
                else
                {
                    ImGui::TextDisabled("Sky context: Sun");
                }
            }
        }

        render_view_display_controls(changed);

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
        if (projection_mode_ == SatViewProjectionMode::Ground)
        {
            ImGui::Text("Ground lat: %.3f", glm::degrees(ground_location_radians_.y));
            ImGui::Text("Ground lon: %.3f", glm::degrees(ground_location_radians_.x));
            ImGui::TextUnformatted("Projection");
            if (ImGui::RadioButton(
                    "Stereographic",
                    ground_projection_ == SatViewGroundProjection::Stereographic))
            {
                ground_projection_ = SatViewGroundProjection::Stereographic;
                ground_fov_degrees_ = satview_clamp_ground_fov_degrees(
                    ground_projection_,
                    ground_fov_degrees_);
                changed = true;
            }
            if (ImGui::RadioButton(
                    "Perspective",
                    ground_projection_ == SatViewGroundProjection::Perspective))
            {
                ground_projection_ = SatViewGroundProjection::Perspective;
                ground_fov_degrees_ = satview_clamp_ground_fov_degrees(
                    ground_projection_,
                    ground_fov_degrees_);
                changed = true;
            }
            float ground_fov = ground_fov_degrees_;
            set_control_width("Angle of view");
            if (ImGui::SliderFloat(
                    "Angle of view",
                    &ground_fov,
                    kGroundMinimumFovDegrees,
                    satview_maximum_ground_fov_degrees(ground_projection_),
                    "%.0f deg"))
            {
                ground_fov_degrees_ = satview_clamp_ground_fov_degrees(
                    ground_projection_,
                    ground_fov);
                changed = true;
            }
            if (ImGui::Checkbox("Show ground", &ground_visible_))
                changed = true;
            if (ImGui::Checkbox("Horizon occlusion", &ground_horizon_occlusion_))
            {
                invalidate_visual_buffers();
                changed = true;
            }
            if (ImGui::Checkbox("Observatory horizon", &observatory_horizon_enabled_))
                changed = true;
            if (ImGui::Checkbox("Cardinal labels", &cardinal_labels_enabled_))
                changed = true;
            if (ImGui::Button("Back to Globe"))
            {
                projection_mode_ = SatViewProjectionMode::Globe;
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Back to Map"))
            {
                projection_mode_ = SatViewProjectionMode::Map;
                changed = true;
            }
        }
        else if (projection_mode_ == SatViewProjectionMode::Map)
        {
            ImGui::Text("Center lat: %.1f", glm::degrees(map_center_radians_.y));
            ImGui::Text("Center lon: %.1f", glm::degrees(map_center_radians_.x));
            ImGui::TextDisabled("Double-click the map to enter ground view.");
        }
        else
        {
            ImGui::TextDisabled("Double-click Earth to enter ground view.");
        }
    }
    ImGui::End();

    if (ImGui::Begin(kSatViewFilterWindowName, nullptr, flags))
    {
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
        changed |= ImGui::Checkbox("SSO candidates only", &filter_.sun_synchronous_only);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Derived from the current orbit's J2 nodal precession; "
                "this is independent of the LEO/MEO/GEO/HEO class.");
        }

        ImGui::SeparatorText("Population");
        const auto population_checkbox = [&](const char* label,
                                             bool& visible,
                                             SatellitePopulation population,
                                             std::size_t count) {
            const glm::vec4 color = population_color(population, 1.0f);
            ImGui::TextColored(ImVec4(color.r, color.g, color.b, color.a), "%zu", count);
            ImGui::SameLine();
            changed |= ImGui::Checkbox(label, &visible);
        };
        population_checkbox("Active payloads", filter_.show_active_payloads,
            SatellitePopulation::ActivePayload, catalog_snapshot.populations.active_payloads);
        population_checkbox("Inactive payloads", filter_.show_inactive_payloads,
            SatellitePopulation::InactivePayload, catalog_snapshot.populations.inactive_payloads);
        population_checkbox("Rocket bodies", filter_.show_rocket_bodies,
            SatellitePopulation::RocketBody, catalog_snapshot.populations.rocket_bodies);
        population_checkbox("Debris", filter_.show_debris,
            SatellitePopulation::Debris, catalog_snapshot.populations.debris);
        population_checkbox("Unknown", filter_.show_unknown_population,
            SatellitePopulation::Unknown, catalog_snapshot.populations.unknown);
        changed |= ImGui::Checkbox("Show SATCAT summary estimates", &filter_.show_summary_estimates);
        changed |= ImGui::Checkbox("Show catalog-only objects", &filter_.show_catalog_only);
        ImGui::TextDisabled("Earth SATCAT-only positions are estimates; Moon rows need ephemerides.");

        render_object_tree(snapshot, changed);
        ImGui::SeparatorText("Lunar Surface");
        changed |= ImGui::Checkbox("Landing sites and landers", &show_lunar_landers_);
        changed |= ImGui::Checkbox("Rovers", &show_lunar_rovers_);
        changed |= ImGui::Checkbox("Instruments and reflectors", &show_lunar_instruments_);
        changed |= ImGui::Checkbox("Impacts and rocket stages", &show_lunar_impacts_);
        changed |= ImGui::Checkbox("Crewed artifacts", &show_lunar_crewed_artifacts_);
        changed |= ImGui::Checkbox("Approximate locations", &show_lunar_approximate_locations_);
        render_lunar_surface_tree(changed);
    }
    ImGui::End();

    if (ImGui::Begin(kSatViewRenderingWindowName, nullptr, flags))
    {
        render_visual_controls();
        ImGui::SeparatorText("Tone Mapping");
        float exposure = tone_map_exposure_;
        set_control_width("Exposure");
        if (ImGui::SliderFloat(
                "Exposure",
                &exposure,
                kMinimumToneMapExposure,
                kMaximumToneMapExposure,
                "%.2f"))
        {
            tone_map_exposure_ = std::clamp(
                exposure,
                kMinimumToneMapExposure,
                kMaximumToneMapExposure);
            request_redraw();
        }
        float white_point = tone_map_white_point_;
        set_control_width("White point");
        if (ImGui::SliderFloat(
                "White point",
                &white_point,
                kMinimumToneMapWhitePoint,
                kMaximumToneMapWhitePoint,
                "%.2f",
                ImGuiSliderFlags_Logarithmic))
        {
            tone_map_white_point_ = std::clamp(
                white_point,
                kMinimumToneMapWhitePoint,
                kMaximumToneMapWhitePoint);
            request_redraw();
        }
        if (ImGui::Checkbox("HDR buffer debug", &show_hdr_debug_panel_))
            request_redraw();

        int color_mode_index = static_cast<int>(color_mode_);
        const char* color_modes[] = { "Population", "Name Prefix", "Orbit Class", "Object Type" };
        set_control_width("Color");
        if (ImGui::Combo("Color", &color_mode_index, color_modes, 4))
        {
            color_mode_ = static_cast<SatViewColorMode>(color_mode_index);
            changed = true;
        }

        if (!show_tracks)
            ImGui::BeginDisabled();

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
        if (!show_tracks)
            ImGui::EndDisabled();

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
        if (!show_markers)
            ImGui::BeginDisabled();
        set_control_width("Marker cap");
        if (ImGui::Combo("Marker cap", &marker_limit_index, kMarkerLimitLabels, 6))
        {
            marker_satellite_limit_ = kMarkerLimitValues[marker_limit_index];
            changed = true;
        }
        if (!show_markers)
            ImGui::EndDisabled();
    }
    ImGui::End();

    if (ImGui::Begin(kSatViewAboutWindowName, nullptr, flags))
    {
        ImGui::SeparatorText("Sources");
        const std::string catalog_status = catalog_service_.status_text();
        ImGui::TextWrapped("%s", catalog_status.empty() ? "catalog pending" : catalog_status.c_str());
        const auto data_source_name = [](SatViewCatalogService::DataSource source) {
            switch (source)
            {
            case SatViewCatalogService::DataSource::Live:
                return "live";
            case SatViewCatalogService::DataSource::Cache:
                return "cache";
            case SatViewCatalogService::DataSource::Sample:
                return "sample";
            case SatViewCatalogService::DataSource::None:
                return "pending";
            }
            return "pending";
        };
        ImGui::Text("GP: %s, %zu records",
            data_source_name(catalog_snapshot.gp.data_source),
            catalog_snapshot.gp.object_count);
        ImGui::Text("SATCAT: %s, %zu retained, %zu excluded",
            data_source_name(catalog_snapshot.satcat.data_source),
            catalog_snapshot.satcat.object_count,
            catalog_snapshot.satcat.excluded_records);
        ImGui::Text("Merged: %zu total, %zu renderable",
            catalog_snapshot.object_count,
            catalog_snapshot.renderable_count);
        ImGui::Text("Skipped from scene: %zu (%zu retained without an orbit)",
            catalog_snapshot.skipped_records,
            catalog_snapshot.non_renderable_count);
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
        const std::size_t filtered_markers = snapshot
            ? static_cast<std::size_t>(std::ranges::count_if(
                  snapshot->states,
                  [this, source_label](const SatellitePropagatedState& state) {
                      return satellite_visible(filter_, state, source_label);
                  }))
            : 0;
        const std::size_t rendered_markers = !show_markers
            ? 0
            : (marker_satellite_limit_ == 0
                      ? filtered_markers
                      : std::min(filtered_markers, marker_satellite_limit_));
        const std::size_t total_markers = snapshot ? snapshot->states.size() : 0;
        const std::size_t total_tracks = (snapshot && snapshot->tracks) ? snapshot->tracks->size() : 0;
        ImGui::Text("Markers: %zu / %zu", rendered_markers, total_markers);
        ImGui::Text("Paths: %zu / %zu", visible_track_count(snapshot), total_tracks);
        ImGui::Text("Stars: %zu / %zu",
            visible_stars_.size(),
            stars_.size());
        if (lunar_surface_catalog_ && lunar_surface_catalog_->error.empty())
        {
            ImGui::Text("Lunar surface: %zu objects, %zu sites",
                lunar_surface_catalog_->objects.size(),
                lunar_surface_catalog_->site_count);
            ImGui::TextDisabled("Coordinates: LROC; identity enrichment: GCAT CC BY 4.0");
        }
        else if (lunar_surface_catalog_)
        {
            ImGui::TextWrapped("Lunar surface: %s", lunar_surface_catalog_->error.c_str());
        }
    }
    ImGui::End();

    if (ImGui::Begin(kSatViewSelectionWindowName, nullptr, flags))
    {
        if (const SatViewLunarSurfaceObject* selected = selected_lunar_surface_object())
        {
            ImGui::TextWrapped("%s", selected->display_name.c_str());
            ImGui::Text("Mission: %s", selected->mission_name.c_str());
            if (!selected->vehicle_name.empty() && selected->vehicle_name != selected->display_name)
                ImGui::TextWrapped("Vehicle: %s", selected->vehicle_name.c_str());
            const std::string_view kind = satview_lunar_surface_kind_name(selected->kind);
            ImGui::Text("Kind: %.*s", static_cast<int>(kind.size()), kind.data());
            if (!selected->status.empty())
                ImGui::Text("Status: %s", selected->status.c_str());
            if (selected->renderable())
            {
                ImGui::Text("Latitude: %.5f %c",
                    std::abs(selected->latitude_degrees),
                    selected->latitude_degrees >= 0.0 ? 'N' : 'S');
                ImGui::Text("Longitude: %.5f %c",
                    std::abs(selected->longitude_east_degrees),
                    selected->longitude_east_degrees >= 0.0 ? 'E' : 'W');
            }
            else
            {
                ImGui::TextDisabled("No independently located coordinate.");
            }
            if (!selected->arrival_date.empty())
                ImGui::Text("Arrival: %s", selected->arrival_date.c_str());
            const std::string_view quality = satview_lunar_location_quality_name(
                selected->location_quality);
            ImGui::Text("Location: %.*s", static_cast<int>(quality.size()), quality.data());
            if (selected->coordinate_uncertainty_m.has_value())
                ImGui::Text("Uncertainty: %.1f m", *selected->coordinate_uncertainty_m);
            if (!selected->coordinate_source.empty())
                ImGui::TextWrapped("Coordinate source: %s", selected->coordinate_source.c_str());
            if (!selected->owner.empty() || !selected->country.empty())
                ImGui::Text("Owner: %s%s%s",
                    selected->owner.c_str(),
                    !selected->owner.empty() && !selected->country.empty() ? " / " : "",
                    selected->country.c_str());
            if (!selected->cospar_id.empty())
                ImGui::Text("COSPAR: %s", selected->cospar_id.c_str());
            if (!selected->gcat_id.empty())
                ImGui::Text("GCAT: %s", selected->gcat_id.c_str());
            if (!selected->references.empty())
            {
                ImGui::SeparatorText("References");
                for (const std::string& reference : selected->references)
                    ImGui::TextWrapped("%s", reference.c_str());
            }
            if (!selected->renderable())
                ImGui::BeginDisabled();
            if (ImGui::Button("Center on site"))
            {
                center_selected_lunar_surface_object(displayed_simulation_seconds);
                changed = true;
            }
            if (!selected->renderable())
                ImGui::EndDisabled();
            if (ImGui::Button("Clear Selection"))
            {
                selected_lunar_surface_index_.reset();
                selected_natural_body_.reset();
                changed = true;
            }
        }
        else if (const SatViewSolarSystemBody* selected = selected_natural_body())
        {
            ImGui::TextWrapped("%s", selected->name.data());
            ImGui::Text("System: %s", selected->system_name.data());
            if (selected->parent.has_value())
                ImGui::Text("Orbits: %s", camera_pov_name(*selected->parent));
            ImGui::Text("Equatorial radius: %.1f km", selected->equatorial_radius_km);
            ImGui::Text("Polar radius: %.1f km", selected->polar_radius_km);
            if (selected->semi_major_axis_km > 0.0)
                ImGui::Text("Semi-major axis: %.0f km", selected->semi_major_axis_km);
            if (selected->orbital_period_days > 0.0)
                ImGui::Text("Orbital period: %.3f days", selected->orbital_period_days);
            if (ImGui::Button("Go to body"))
            {
                set_camera_pov(selected->id, displayed_simulation_seconds);
                changed = true;
            }
            if (ImGui::Button("Clear Selection"))
            {
                selected_natural_body_.reset();
                changed = true;
            }
        }
        else if (const SatellitePropagatedState* selected = selected_satellite(snapshot))
        {
            const SatelliteStaticMetadata* metadata = selected->metadata.get();
            ImGui::TextWrapped("%s",
                metadata && !metadata->object_name.empty() ? metadata->object_name.c_str() : "Unnamed object");
            ImGui::Text("NORAD: %lld", static_cast<long long>(selected->norad_catalog_id));
            if (metadata && !metadata->object_id.empty())
                ImGui::Text("Object ID: %s", metadata->object_id.c_str());
            ImGui::Text("Orbit: %.*s",
                static_cast<int>(orbit_class_name(selected->orbit_class).size()),
                orbit_class_name(selected->orbit_class).data());
            ImGui::Text("Central body: %.*s",
                static_cast<int>(central_body_name(selected->central_body).size()),
                central_body_name(selected->central_body).data());
            ImGui::Text("Sun-synchronous: %s",
                selected->sun_synchronous_candidate ? "Candidate (derived)" : "No");
            if (metadata && !metadata->object_type.empty())
                ImGui::Text("Type: %s", metadata->object_type.c_str());
            ImGui::Text("Kind: %.*s",
                static_cast<int>(satellite_object_kind_name(selected->object_kind).size()),
                satellite_object_kind_name(selected->object_kind).data());
            ImGui::Text("Population: %.*s",
                static_cast<int>(satellite_population_name(selected->population).size()),
                satellite_population_name(selected->population).data());
            ImGui::Text("Solution: %.*s",
                static_cast<int>(orbit_solution_kind_name(selected->solution_kind).size()),
                orbit_solution_kind_name(selected->solution_kind).data());
            if (metadata && !metadata->owner.empty())
                ImGui::Text("Owner: %s", metadata->owner.c_str());
            if (metadata && !metadata->operational_status_code.empty())
                ImGui::Text("Status: %s", metadata->operational_status_code.c_str());
            if (metadata && !metadata->data_status_code.empty())
                ImGui::Text("Data status: %s", metadata->data_status_code.c_str());
            if (metadata && !metadata->ephemeris_source.empty())
                ImGui::TextWrapped("Ephemeris: %s", metadata->ephemeris_source.c_str());
            if (metadata && !metadata->ephemeris_frame.empty())
                ImGui::Text("Frame: %s", metadata->ephemeris_frame.c_str());
            if (metadata && metadata->ephemeris_start_unix_seconds.has_value()
                && metadata->ephemeris_end_unix_seconds.has_value())
            {
                const std::string valid_from = format_local_simulation_time(
                    *metadata->ephemeris_start_unix_seconds);
                const std::string valid_until = format_local_simulation_time(
                    *metadata->ephemeris_end_unix_seconds);
                ImGui::TextWrapped("Valid: %s to %s", valid_from.c_str(), valid_until.c_str());
            }
            if (metadata && metadata->radar_cross_section_m2.has_value())
                ImGui::Text("Radar cross section: %.3g m^2", *metadata->radar_cross_section_m2);
            if (metadata && !metadata->classification_type.empty())
                ImGui::Text("Class: %s", metadata->classification_type.c_str());
            ImGui::Text("Period: %.1f min", selected->period_minutes);
            if (std::isfinite(selected->minutes_since_epoch))
                ImGui::Text("Epoch age: %.1f h", selected->minutes_since_epoch / 60.0);
            else
                ImGui::TextDisabled("Epoch age: unavailable for SATCAT summary");
            if (selected->central_body == CentralBody::Earth)
            {
                const SatViewGeodeticPosition geodetic = satview_geodetic_from_ecef(selected->ecef_position_km);
                ImGui::Text("Latitude: %.3f %c",
                    std::abs(geodetic.latitude_degrees),
                    geodetic.latitude_degrees >= 0.0 ? 'N' : 'S');
                ImGui::Text("Longitude: %.3f %c",
                    std::abs(geodetic.longitude_degrees),
                    geodetic.longitude_degrees >= 0.0 ? 'E' : 'W');
            }
            const glm::dvec3 central_position = selected->central_body == CentralBody::Moon
                ? satview_moon_position(displayed_simulation_seconds).equatorial_position_km
                : glm::dvec3(0.0);
            const double central_radius_km = selected->central_body == CentralBody::Moon
                ? kSatViewMoonMeanRadiusKm
                : kSatViewEarthEquatorialRadiusKm;
            const double altitude_km = glm::length(selected->teme_position_km - central_position)
                - central_radius_km;
            ImGui::Text("Altitude: %.0f km", altitude_km);
            const double speed_km_s = glm::length(selected->teme_velocity_km_per_s);
            ImGui::Text("Speed: %.2f km/s", speed_km_s);
            if (ImGui::Button("Clear Selection"))
            {
                selected_norad_catalog_id_.reset();
                selected_natural_body_.reset();
                simulation_settings_dirty_ = true;
                changed = true;
            }
        }
        else if (const SatelliteRecord* selected = selected_catalog_record())
        {
            ImGui::TextWrapped("%s",
                selected->object_name.empty() ? "Unnamed object" : selected->object_name.c_str());
            ImGui::Text("NORAD: %lld", static_cast<long long>(selected->norad_catalog_id));
            if (!selected->object_id.empty())
                ImGui::Text("Object ID: %s", selected->object_id.c_str());
            ImGui::Text("Central body: %.*s",
                static_cast<int>(central_body_name(selected->central_body).size()),
                central_body_name(selected->central_body).data());
            ImGui::Text("Kind: %.*s",
                static_cast<int>(satellite_object_kind_name(selected->object_kind).size()),
                satellite_object_kind_name(selected->object_kind).data());
            ImGui::Text("Population: %.*s",
                static_cast<int>(satellite_population_name(selected->population).size()),
                satellite_population_name(selected->population).data());
            ImGui::Text("Fidelity: %.*s",
                static_cast<int>(orbit_solution_kind_name(selected->solution_kind).size()),
                orbit_solution_kind_name(selected->solution_kind).data());
            if (!selected->owner.empty())
                ImGui::Text("Owner: %s", selected->owner.c_str());
            if (!selected->operational_status_code.empty())
                ImGui::Text("Status: %s", selected->operational_status_code.c_str());
            ImGui::TextDisabled("No usable public orbital state; this object is not rendered.");
            if (ImGui::Button("Clear Selection"))
            {
                selected_norad_catalog_id_.reset();
                selected_natural_body_.reset();
                simulation_settings_dirty_ = true;
                changed = true;
            }
        }
        else
        {
            ImGui::TextDisabled("No object selected.");
        }
    }
    ImGui::End();

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
}

std::size_t SatViewHost::visible_track_count(const SatViewSimulationSnapshot* snapshot) const
{
    if (!satellite_display_shows_tracks(satellite_display_mode_))
        return 0;

    const bool ground_projection = projection_mode_ == SatViewProjectionMode::Ground;
    const bool earth_track_visible = earth_orbit_track_visible(
        earth_track_enabled_, satellite_display_mode_, projection_mode_, camera_pov_);
    const std::optional<glm::dvec3> ground_observer = ground_projection
            && ground_horizon_occlusion_
            && snapshot
        ? std::optional<glm::dvec3>(ground_observer_render_position(render_simulation_seconds(*snapshot)))
        : std::nullopt;
    std::size_t count = moon_orbit_track_visible(
                            moon_track_enabled_, satellite_display_mode_, projection_mode_, camera_pov_)
        ? 1
        : 0;
    if (earth_track_visible)
        ++count;
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

        if (!satellite_visible(filter_, track, source_label))
            continue;

        if (ground_observer.has_value())
        {
            const bool has_visible_sample = std::ranges::any_of(
                track.render_teme_points_earth_radii,
                [&](const glm::dvec3& point) {
                    return satview_ground_visibility_dot(point, *ground_observer) > 0.0;
                });
            if (!has_visible_sample)
                continue;
        }

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

const SatelliteRecord* SatViewHost::selected_catalog_record() const
{
    if (!selected_norad_catalog_id_.has_value())
        return nullptr;
    const auto found = std::ranges::find(
        catalog_snapshot_.objects,
        *selected_norad_catalog_id_,
        &SatelliteRecord::norad_catalog_id);
    return found == catalog_snapshot_.objects.end() ? nullptr : &*found;
}

const SatViewLunarSurfaceObject* SatViewHost::selected_lunar_surface_object() const
{
    if (!lunar_surface_catalog_
        || !selected_lunar_surface_index_.has_value()
        || *selected_lunar_surface_index_ >= lunar_surface_catalog_->objects.size())
    {
        return nullptr;
    }
    return &lunar_surface_catalog_->objects[*selected_lunar_surface_index_];
}

const SatViewSolarSystemBody* SatViewHost::selected_natural_body() const
{
    if (!selected_natural_body_.has_value())
        return nullptr;
    return &satview_solar_system_body(*selected_natural_body_);
}

void SatViewHost::center_selected_lunar_surface_object(double simulation_seconds)
{
    const SatViewLunarSurfaceObject* selected = selected_lunar_surface_object();
    if (!selected || !selected->renderable())
        return;

    set_camera_pov(SatViewCameraPov::Moon, simulation_seconds);
    if (projection_mode_ == SatViewProjectionMode::Ground)
        projection_mode_ = SatViewProjectionMode::Globe;
    if (projection_mode_ == SatViewProjectionMode::Map)
    {
        map_center_radians_ = normalized_satview_map_center(glm::vec2(
            glm::radians(static_cast<float>(selected->longitude_east_degrees)),
            glm::radians(static_cast<float>(selected->latitude_degrees))));
    }
    else
    {
        const glm::dvec3 moon_position = satview_moon_position(
            simulation_seconds)
                                             .render_position_earth_radii;
        const glm::dvec3 direction = satview_lunar_body_to_render_direction(
            satview_lunar_body_direction(
                selected->latitude_degrees,
                selected->longitude_east_degrees),
            moon_position);
        const float distance = std::max(
            camera_->GetDistance(),
            kMoonRadiusEarthRadii * kLunarSurfaceChildExpansionDistanceScale);
        camera_->SetPositionAndFocalPoint(
            glm::vec3(moon_position + direction * static_cast<double>(distance)),
            glm::vec3(moon_position));
    }
    request_redraw();
}

void SatViewHost::clear_selection_if_missing(const SatViewSimulationSnapshot* snapshot)
{
    if (selected_norad_catalog_id_.has_value() && !selected_catalog_record())
    {
        selected_norad_catalog_id_.reset();
        simulation_settings_dirty_ = true;
        invalidate_visual_buffers();
    }
    if (selected_lunar_surface_index_.has_value()
        && (!lunar_surface_catalog_
            || *selected_lunar_surface_index_ >= lunar_surface_catalog_->objects.size()))
    {
        selected_lunar_surface_index_.reset();
        invalidate_visual_buffers();
    }
}

void SatViewHost::select_nearest_object(const glm::ivec2& screen_pos)
{
    if (select_nearest_lunar_surface_object(screen_pos))
        return;
    if (satview_uses_generic_body_view(camera_pov_))
    {
        select_nearest_natural_body(screen_pos, false);
        return;
    }
    if (!satellite_display_shows_markers(satellite_display_mode_))
        return;

    auto snapshot_guard = simulation_worker_ ? simulation_worker_->acquire_latest() : SatViewSnapshotExchange::ReadGuard{};
    const SatViewSimulationSnapshot* snapshot = snapshot_guard.get();
    if (!snapshot || snapshot->states.empty())
        return;

    const int pixel_w = std::max(1, scene_viewport_.pixel_size.x);
    const int pixel_h = std::max(1, scene_viewport_.pixel_size.y);
    const double render_seconds = render_simulation_seconds(*snapshot);
    const SatViewMoonPosition moon = satview_moon_position(render_seconds);
    const SatViewSunPosition sun = satview_sun_position(render_seconds);
    const bool ground_projection = projection_mode_ == SatViewProjectionMode::Ground;
    const bool map_projection = projection_mode_ == SatViewProjectionMode::Map;
    const bool earth_track_visible = earth_orbit_track_visible(
        earth_track_enabled_, satellite_display_mode_, projection_mode_, camera_pov_);
    const float earth_scene_radius = visible_scene_radius(
        snapshot,
        filter_,
        selected_norad_catalog_id_,
        track_display_mode_,
        satellite_display_mode_);
    const float scene_radius = camera_scene_radius(
        earth_scene_radius,
        moon,
        sun,
        camera_pov_,
        moon_enabled_,
        sun_enabled_,
        earth_track_visible);
    const glm::dvec3 ground_observer = ground_observer_render_position(render_seconds);
    const glm::vec3 ground_eye = glm::vec3(
        ground_observer * (1.0 + static_cast<double>(kGroundObserverAltitudeEarthRadii)));
    const glm::mat4 view = ground_projection
        ? satview_ground_view_matrix(ground_eye, ground_camera_orientation_)
        : camera_view_matrix(*camera_);
    const float viewport_aspect = static_cast<float>(pixel_w) / static_cast<float>(pixel_h);
    const glm::mat4 proj = reversed_z_perspective(
        glm::radians(ground_projection ? ground_fov_degrees_ : camera_->GetFieldOfView()),
        viewport_aspect,
        ground_projection ? kCameraMinNearPlane : camera_near_plane(camera_pov_, camera_->GetDistance()),
        ground_projection ? camera_far_plane(1.0f, scene_radius) : camera_far_plane(camera_->GetDistance(), scene_radius));
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
        const glm::dvec3 teme_position = interpolated_teme_position(*snapshot, state_index, render_seconds);
        if (projection_mode_ == SatViewProjectionMode::Map)
        {
            const glm::vec2 map_position = satview_map_position_from_teme(
                teme_position,
                render_seconds,
                map_center_radians_,
                camera_pov_ == SatViewCameraPov::Moon
                    ? SatViewMapBody::Moon
                    : camera_pov_ == SatViewCameraPov::Sun
                    ? SatViewMapBody::Sun
                    : SatViewMapBody::Earth,
                moon.render_position_earth_radii,
                sun.render_position_earth_radii,
                sun.body_to_render_orientation);
            ndc = glm::vec3(map_position, 0.2f);
        }
        else
        {
            const glm::vec3 world = to_vec3(teme_position_to_render_earth_radii(teme_position));
            if (ground_projection
                && ground_horizon_occlusion_
                && satview_ground_visibility_dot(glm::dvec3(world), ground_observer) <= 0.0)
            {
                continue;
            }
            if (ground_projection)
            {
                const glm::vec3 camera_direction = glm::mat3(view)
                    * glm::normalize(world - ground_eye);
                const SatViewSkyProjectionPoint point = satview_project_camera_direction(
                    ground_projection_,
                    camera_direction,
                    ground_fov_degrees_,
                    viewport_aspect);
                if (!point.valid)
                    continue;
                ndc = glm::vec3(point.ndc, 0.5f);
            }
            else
            {
                const glm::vec4 clip = view_proj * glm::vec4(world, 1.0f);
                if (clip.w <= 0.0f)
                    continue;
                ndc = glm::vec3(clip) / clip.w;
            }
            if (ndc.x < -1.1f || ndc.x > 1.1f || ndc.y < -1.1f || ndc.y > 1.1f
                || ndc.z < 0.0f || ndc.z > 1.0f)
            {
                continue;
            }
        }

        const float sx = static_cast<float>(scene_viewport_.pixel_pos.x)
            + (ndc.x * 0.5f + 0.5f) * static_cast<float>(pixel_w);
        const float sy = static_cast<float>(scene_viewport_.pixel_pos.y)
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
        if (nearest_id.has_value())
        {
            selected_lunar_surface_index_.reset();
            selected_natural_body_.reset();
        }
        sync_simulation_render_settings();
        invalidate_visual_buffers();
    }
    request_redraw();
}

bool SatViewHost::select_nearest_natural_body(const glm::ivec2& screen_pos, bool enter_body)
{
    if (!satview_uses_generic_body_view(camera_pov_)
        || projection_mode_ != SatViewProjectionMode::Globe)
    {
        return false;
    }

    const double simulation_seconds = simulation_worker_
        ? simulation_worker_->current_simulation_seconds()
        : last_draw_simulation_seconds_;
    const std::vector<SatViewBodyRenderInstance> instances = satview_child_body_instances(camera_pov_, simulation_seconds);
    if (instances.empty())
        return false;

    const int pixel_w = std::max(1, scene_viewport_.pixel_size.x);
    const int pixel_h = std::max(1, scene_viewport_.pixel_size.y);
    const float viewport_aspect = static_cast<float>(pixel_w) / static_cast<float>(pixel_h);
    const float scene_radius = solar_system_scene_radius(camera_pov_);
    const glm::mat4 view = camera_view_matrix(*camera_);
    const glm::mat4 proj = reversed_z_perspective(
        glm::radians(camera_->GetFieldOfView()),
        viewport_aspect,
        camera_near_plane(camera_pov_, camera_->GetDistance()),
        camera_far_plane(camera_->GetDistance(), scene_radius));
    const glm::mat4 view_proj = proj * view;

    std::optional<SatViewCameraPov> nearest_body;
    float nearest_distance_sq = static_cast<float>(
        kClickSelectionMaxDistancePixels * kClickSelectionMaxDistancePixels);
    for (const SatViewBodyRenderInstance& instance : instances)
    {
        const glm::vec3 world = to_vec3(instance.position_focus_radii);
        const glm::vec4 clip = view_proj * glm::vec4(world, 1.0f);
        if (clip.w <= 0.0f)
            continue;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.x < -1.1f || ndc.x > 1.1f || ndc.y < -1.1f || ndc.y > 1.1f
            || ndc.z < 0.0f || ndc.z > 1.0f)
        {
            continue;
        }
        const float sx = static_cast<float>(scene_viewport_.pixel_pos.x)
            + (ndc.x * 0.5f + 0.5f) * static_cast<float>(pixel_w);
        const float sy = static_cast<float>(scene_viewport_.pixel_pos.y)
            + (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(pixel_h);
        const glm::vec2 delta = glm::vec2(sx, sy) - glm::vec2(screen_pos);
        const float distance_sq = glm::dot(delta, delta);
        if (distance_sq < nearest_distance_sq)
        {
            nearest_distance_sq = distance_sq;
            nearest_body = instance.body;
        }
    }

    if (!nearest_body.has_value())
        return false;

    selected_natural_body_ = nearest_body;
    selected_norad_catalog_id_.reset();
    selected_lunar_surface_index_.reset();
    invalidate_visual_buffers();
    if (enter_body)
        set_camera_pov(*nearest_body, simulation_seconds);
    request_redraw();
    return true;
}

bool SatViewHost::select_nearest_lunar_surface_object(const glm::ivec2& screen_pos)
{
    if (!lunar_surface_objects_enabled_
        || !lunar_surface_catalog_
        || !lunar_surface_catalog_->error.empty()
        || camera_pov_ != SatViewCameraPov::Moon
        || projection_mode_ == SatViewProjectionMode::Ground)
    {
        return false;
    }

    const int pixel_width = std::max(1, scene_viewport_.pixel_size.x);
    const int pixel_height = std::max(1, scene_viewport_.pixel_size.y);
    const double simulation_seconds = last_draw_simulation_seconds_;
    const glm::dvec3 moon_position = satview_moon_position(
        simulation_seconds)
                                         .render_position_earth_radii;
    const bool map_projection = projection_mode_ == SatViewProjectionMode::Map;
    const bool show_children = !map_projection
        && camera_->GetDistance()
            <= kMoonRadiusEarthRadii * kLunarSurfaceChildExpansionDistanceScale;
    const float viewport_aspect = static_cast<float>(pixel_width)
        / static_cast<float>(pixel_height);
    const glm::mat4 view_projection = reversed_z_perspective(
                                          glm::radians(camera_->GetFieldOfView()),
                                          viewport_aspect,
                                          kCameraMinNearPlane,
                                          kCameraMaxDistanceCap)
        * camera_view_matrix(*camera_);

    std::optional<std::size_t> nearest_index;
    float nearest_distance_squared = static_cast<float>(
        kClickSelectionMaxDistancePixels * kClickSelectionMaxDistancePixels);
    for (std::size_t index = 0; index < lunar_surface_catalog_->objects.size(); ++index)
    {
        const SatViewLunarSurfaceObject& object = lunar_surface_catalog_->objects[index];
        const bool selected = selected_lunar_surface_index_.has_value()
            && *selected_lunar_surface_index_ == index;
        const bool visible = lunar_surface_kind_visible(
            object,
            show_lunar_landers_,
            show_lunar_rovers_,
            show_lunar_instruments_,
            show_lunar_impacts_,
            show_lunar_crewed_artifacts_,
            show_lunar_approximate_locations_);
        const bool site_marker_visible = lunar_surface_site_marker_visible(
            *lunar_surface_catalog_,
            index,
            show_lunar_landers_,
            show_lunar_rovers_,
            show_lunar_instruments_,
            show_lunar_impacts_,
            show_lunar_crewed_artifacts_,
            show_lunar_approximate_locations_);
        if ((!visible || (!show_children && !site_marker_visible)) && !selected)
            continue;

        glm::vec2 ndc;
        if (map_projection)
        {
            ndc = satview_map_position_from_lunar_body(
                satview_lunar_body_direction(
                    object.latitude_degrees,
                    object.longitude_east_degrees),
                map_center_radians_);
        }
        else
        {
            const glm::dvec3 world = satview_lunar_surface_render_position(
                object.latitude_degrees,
                object.longitude_east_degrees,
                static_cast<double>(kMoonRadiusEarthRadii * kLunarSurfaceMarkerRadiusScale),
                moon_position);
            const glm::dvec3 outward = glm::normalize(world - moon_position);
            if (glm::dot(outward, glm::dvec3(camera_->GetPosition()) - world) <= 0.0)
                continue;
            const glm::vec4 clip = view_projection * glm::vec4(to_vec3(world), 1.0f);
            if (clip.w <= 0.0f)
                continue;
            const glm::vec3 projected = glm::vec3(clip) / clip.w;
            if (projected.z < 0.0f || projected.z > 1.0f)
                continue;
            ndc = glm::vec2(projected);
        }

        const auto consider_position = [&](float ndc_x) {
            if (ndc_x < -1.1f || ndc_x > 1.1f || ndc.y < -1.1f || ndc.y > 1.1f)
                return;
            const float screen_x = static_cast<float>(scene_viewport_.pixel_pos.x)
                + (ndc_x * 0.5f + 0.5f) * static_cast<float>(pixel_width);
            const float screen_y = static_cast<float>(scene_viewport_.pixel_pos.y)
                + (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(pixel_height);
            const glm::vec2 delta = glm::vec2(screen_x, screen_y) - glm::vec2(screen_pos);
            const float distance_squared = glm::dot(delta, delta);
            if (distance_squared < nearest_distance_squared)
            {
                nearest_distance_squared = distance_squared;
                nearest_index = index;
            }
        };
        consider_position(ndc.x);
        if (map_projection && ndc.x > 0.9f)
            consider_position(ndc.x - 2.0f);
        else if (map_projection && ndc.x < -0.9f)
            consider_position(ndc.x + 2.0f);
    }

    if (!nearest_index.has_value())
        return false;
    selected_lunar_surface_index_ = nearest_index;
    selected_norad_catalog_id_.reset();
    selected_natural_body_.reset();
    sync_simulation_render_settings();
    invalidate_visual_buffers();
    request_redraw();
    return true;
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
