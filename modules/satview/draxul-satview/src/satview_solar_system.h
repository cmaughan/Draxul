#pragma once

#include <draxul/satview/satview_config.h>
#include <glm/glm.hpp>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace draxul::satview
{

struct SatViewSolarSystemBody
{
    SatViewCameraPov id = SatViewCameraPov::Earth;
    std::string_view name;
    std::string_view config_name;
    std::string_view system_name;
    std::optional<SatViewCameraPov> parent;
    double equatorial_radius_km = 1.0;
    double polar_radius_km = 1.0;
    double semi_major_axis_km = 0.0;
    double eccentricity = 0.0;
    double orbital_period_days = 0.0;
    double inclination_degrees = 0.0;
    double ascending_node_degrees = 0.0;
    double phase_degrees = 0.0;
    double rotation_period_hours = 24.0;
    glm::vec3 display_color{ 0.72f, 0.74f, 0.78f };
    std::string_view texture_path;
    bool emissive = false;
};

struct SatViewContextBodyProxy
{
    glm::dvec3 position_focus_radii{ 0.0 };
    double radius_focus_radii = 0.0;
    double angular_radius_radians = 0.0;
};

struct SatViewBodyOrbitTrack
{
    SatViewCameraPov body = SatViewCameraPov::Earth;
    std::vector<glm::dvec3> points_focus_radii;
    double radius_focus_radii = 0.0;
    glm::vec4 color{ 1.0f };
};

struct SatViewBodyRenderInstance
{
    SatViewCameraPov body = SatViewCameraPov::Earth;
    glm::dvec3 position_focus_radii{ 0.0 };
    double radius_focus_radii = 0.0;
    double rotation_radians = 0.0;
    double polar_radius_ratio = 1.0;
    bool emissive = false;
};

struct SatViewRingBand
{
    double inner_radius_body_radii = 0.0;
    double outer_radius_body_radii = 0.0;
    glm::vec4 color{ 1.0f };
};

[[nodiscard]] std::span<const SatViewSolarSystemBody> satview_solar_system_bodies();
[[nodiscard]] const SatViewSolarSystemBody& satview_solar_system_body(SatViewCameraPov id);
[[nodiscard]] std::optional<SatViewCameraPov> satview_camera_pov_from_config_name(
    std::string_view name);
[[nodiscard]] bool satview_uses_generic_body_view(SatViewCameraPov id);
[[nodiscard]] std::vector<const SatViewSolarSystemBody*> satview_child_bodies(
    SatViewCameraPov parent);
[[nodiscard]] glm::dvec3 satview_body_position_in_parent_frame(
    const SatViewSolarSystemBody& body,
    double unix_seconds);
[[nodiscard]] glm::dvec3 satview_body_position_in_solar_frame(
    SatViewCameraPov id,
    double unix_seconds);
[[nodiscard]] std::optional<SatViewContextBodyProxy> satview_context_body_proxy(
    SatViewCameraPov focus,
    SatViewCameraPov target,
    const glm::dvec3& observer_position_focus_radii,
    double unix_seconds,
    double proxy_distance_focus_radii);
[[nodiscard]] std::vector<glm::dvec3> satview_body_orbit_in_parent_frame(
    const SatViewSolarSystemBody& body,
    std::size_t segment_count);
[[nodiscard]] std::vector<SatViewBodyOrbitTrack> satview_child_orbit_tracks(
    SatViewCameraPov parent,
    const SatViewPlanetTrackConfig& planet_tracks,
    std::size_t segment_count);
[[nodiscard]] std::vector<SatViewBodyRenderInstance> satview_child_body_instances(
    SatViewCameraPov parent,
    double unix_seconds);
[[nodiscard]] std::span<const SatViewRingBand> satview_planetary_ring_bands(
    SatViewCameraPov body);
[[nodiscard]] glm::dvec3 satview_body_sun_direction(
    SatViewCameraPov id,
    double unix_seconds);
[[nodiscard]] double satview_body_rotation_radians(
    const SatViewSolarSystemBody& body,
    double unix_seconds);

} // namespace draxul::satview
