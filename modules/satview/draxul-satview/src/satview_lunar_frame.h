#pragma once

#include <glm/glm.hpp>

namespace draxul::satview
{

struct SatViewLunarFrame
{
    glm::dvec3 center_render_earth_radii{ 0.0 };
    glm::dvec3 zero_longitude_axis{ 1.0, 0.0, 0.0 };
    glm::dvec3 east_axis{ 0.0, 1.0, 0.0 };
    glm::dvec3 north_axis{ 0.0, 0.0, 1.0 };
};

[[nodiscard]] SatViewLunarFrame satview_lunar_frame(
    const glm::dvec3& moon_render_position_earth_radii);
[[nodiscard]] glm::dvec3 satview_lunar_body_direction(
    double latitude_degrees,
    double longitude_east_degrees);
[[nodiscard]] glm::dvec3 satview_lunar_body_to_render_direction(
    const glm::dvec3& lunar_body_direction,
    const glm::dvec3& moon_render_position_earth_radii);
[[nodiscard]] glm::dvec3 satview_render_to_lunar_body(
    const glm::dvec3& render_position_earth_radii,
    const glm::dvec3& moon_render_position_earth_radii);
[[nodiscard]] glm::dvec3 satview_lunar_surface_render_position(
    double latitude_degrees,
    double longitude_east_degrees,
    double radius_earth_radii,
    const glm::dvec3& moon_render_position_earth_radii);

} // namespace draxul::satview
