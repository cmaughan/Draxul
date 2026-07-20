#include <draxul/satview/satview_lunar_frame.h>

#include <cmath>
#include <glm/geometric.hpp>

namespace draxul::satview
{

namespace
{

constexpr glm::dvec3 kLunarNorthPoleRender(
    0.39812155,
    0.91733267,
    0.00003544);

} // namespace

SatViewLunarFrame satview_lunar_frame(
    const glm::dvec3& moon_render_position_earth_radii)
{
    const double moon_distance = glm::length(moon_render_position_earth_radii);
    if (moon_distance <= 0.0)
        return {};

    const glm::dvec3 far_axis = moon_render_position_earth_radii / moon_distance;
    const glm::dvec3 north_axis = glm::normalize(
        kLunarNorthPoleRender
        - far_axis * glm::dot(kLunarNorthPoleRender, far_axis));
    const glm::dvec3 local_x_axis = glm::normalize(glm::cross(north_axis, far_axis));
    return {
        moon_render_position_earth_radii,
        -far_axis,
        -local_x_axis,
        north_axis,
    };
}

glm::dvec3 satview_lunar_body_direction(
    double latitude_degrees,
    double longitude_east_degrees)
{
    const double latitude = glm::radians(latitude_degrees);
    const double longitude = glm::radians(longitude_east_degrees);
    const double cos_latitude = std::cos(latitude);
    return glm::dvec3(
        cos_latitude * std::cos(longitude),
        cos_latitude * std::sin(longitude),
        std::sin(latitude));
}

glm::dvec3 satview_lunar_body_to_render_direction(
    const glm::dvec3& lunar_body_direction,
    const glm::dvec3& moon_render_position_earth_radii)
{
    const SatViewLunarFrame frame = satview_lunar_frame(moon_render_position_earth_radii);
    return frame.zero_longitude_axis * lunar_body_direction.x
        + frame.east_axis * lunar_body_direction.y
        + frame.north_axis * lunar_body_direction.z;
}

glm::dvec3 satview_render_to_lunar_body(
    const glm::dvec3& render_position_earth_radii,
    const glm::dvec3& moon_render_position_earth_radii)
{
    const SatViewLunarFrame frame = satview_lunar_frame(moon_render_position_earth_radii);
    const glm::dvec3 relative = render_position_earth_radii - frame.center_render_earth_radii;
    return glm::dvec3(
        glm::dot(relative, frame.zero_longitude_axis),
        glm::dot(relative, frame.east_axis),
        glm::dot(relative, frame.north_axis));
}

glm::dvec3 satview_lunar_surface_render_position(
    double latitude_degrees,
    double longitude_east_degrees,
    double radius_earth_radii,
    const glm::dvec3& moon_render_position_earth_radii)
{
    const glm::dvec3 direction = satview_lunar_body_direction(
        latitude_degrees,
        longitude_east_degrees);
    return moon_render_position_earth_radii
        + satview_lunar_body_to_render_direction(direction, moon_render_position_earth_radii)
        * radius_earth_radii;
}

} // namespace draxul::satview
