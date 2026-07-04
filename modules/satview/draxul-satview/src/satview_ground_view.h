#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace draxul::satview
{

struct SatViewGroundLocation
{
    double longitude_radians = 0.0;
    double latitude_radians = 0.0;
};

struct SatViewGroundBodyProxy
{
    glm::dvec3 render_position_earth_radii{ 0.0 };
    double radius_earth_radii = 0.0;
};

[[nodiscard]] SatViewGroundLocation satview_ground_location_from_map_ndc(
    glm::vec2 ndc_position,
    glm::vec2 center_radians);
[[nodiscard]] glm::dvec3 satview_ground_render_position(
    SatViewGroundLocation location,
    double unix_seconds);
[[nodiscard]] SatViewGroundLocation satview_ground_location_from_render_position(
    const glm::dvec3& render_position,
    double unix_seconds);
[[nodiscard]] glm::quat satview_default_ground_camera_orientation();
[[nodiscard]] glm::quat satview_rotate_ground_camera(
    glm::quat local_camera_orientation,
    glm::vec2 yaw_pitch_delta_radians);
[[nodiscard]] glm::quat satview_ground_camera_world_orientation(
    const glm::dvec3& observer_render_position,
    glm::quat local_camera_orientation);
[[nodiscard]] glm::mat4 satview_ground_view_matrix(
    const glm::dvec3& observer_render_position,
    glm::quat local_camera_orientation);
[[nodiscard]] double satview_ground_visibility_dot(
    const glm::dvec3& satellite_render_position,
    const glm::dvec3& observer_render_position);
[[nodiscard]] float satview_ground_marker_base_size(
    const glm::dvec3& satellite_render_position,
    const glm::dvec3& observer_render_position);
[[nodiscard]] SatViewGroundBodyProxy satview_ground_body_proxy(
    const glm::dvec3& body_render_position,
    double body_radius_earth_radii,
    const glm::dvec3& observer_render_position,
    double proxy_distance_earth_radii);

} // namespace draxul::satview
