#pragma once

#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace draxul::satview
{

struct SatViewGroundLocation
{
    double longitude_radians = 0.0;
    double latitude_radians = 0.0;
};

struct SatViewGroundBasis
{
    glm::dvec3 east{ 1.0, 0.0, 0.0 };
    glm::dvec3 north{ 0.0, 1.0, 0.0 };
    glm::dvec3 up{ 0.0, 0.0, 1.0 };
};

[[nodiscard]] SatViewGroundLocation satview_ground_location_from_map_ndc(
    glm::vec2 ndc_position,
    glm::vec2 center_radians);
[[nodiscard]] glm::dvec3 satview_ground_render_position(
    SatViewGroundLocation location,
    double unix_seconds);
[[nodiscard]] SatViewGroundBasis satview_ground_basis(
    const glm::dvec3& observer_render_position);
[[nodiscard]] glm::dvec3 satview_ground_local_direction_to_render(
    const glm::dvec3& observer_render_position,
    const glm::dvec3& local_east_north_up_direction);
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
[[nodiscard]] std::size_t satview_ground_track_subdivision_count(
    const glm::dvec3& segment_start,
    const glm::dvec3& segment_end,
    const glm::dvec3& observer_render_position);
[[nodiscard]] glm::dvec3 satview_interpolate_track_arc(
    const glm::dvec3& segment_start,
    const glm::dvec3& segment_end,
    double interpolation);

} // namespace draxul::satview
