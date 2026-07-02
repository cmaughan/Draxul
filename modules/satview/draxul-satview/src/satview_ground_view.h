#pragma once

#include <glm/glm.hpp>

namespace draxul::satview
{

struct SatViewGroundLocation
{
    double longitude_radians = 0.0;
    double latitude_radians = 0.0;
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
[[nodiscard]] glm::mat4 satview_ground_view_matrix(
    const glm::dvec3& observer_render_position,
    float yaw_radians,
    float pitch_radians);
[[nodiscard]] double satview_ground_visibility_dot(
    const glm::dvec3& satellite_render_position,
    const glm::dvec3& observer_render_position);
[[nodiscard]] float satview_ground_marker_base_size(
    const glm::dvec3& satellite_render_position,
    const glm::dvec3& observer_render_position);

} // namespace draxul::satview
