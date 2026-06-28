#include "satview_map_projection.h"

#include <algorithm>
#include <cmath>
#include <draxul/satview/satview_propagation.h>
#include <glm/geometric.hpp>
#include <numbers>

namespace draxul::satview
{

namespace
{

glm::vec2 map_position(double longitude, double latitude)
{
    constexpr double kPi = std::numbers::pi_v<double>;
    return glm::vec2(
        static_cast<float>(longitude / kPi),
        static_cast<float>(latitude / (0.5 * kPi)));
}

glm::dvec3 teme_to_ecef(const glm::dvec3& position, double sidereal_angle)
{
    const double c = std::cos(sidereal_angle);
    const double s = std::sin(sidereal_angle);
    return glm::dvec3(
        c * position.x + s * position.y,
        -s * position.x + c * position.y,
        position.z);
}

glm::dvec3 ecef_to_map_local(const glm::dvec3& position, glm::dvec2 center)
{
    const double cos_longitude = std::cos(center.x);
    const double sin_longitude = std::sin(center.x);
    const double cos_latitude = std::cos(center.y);
    const double sin_latitude = std::sin(center.y);
    const glm::dvec3 center_axis(
        cos_latitude * cos_longitude,
        cos_latitude * sin_longitude,
        sin_latitude);
    const glm::dvec3 east_axis(-sin_longitude, cos_longitude, 0.0);
    const glm::dvec3 north_axis(
        -sin_latitude * cos_longitude,
        -sin_latitude * sin_longitude,
        cos_latitude);
    return glm::dvec3(
        glm::dot(position, center_axis),
        glm::dot(position, east_axis),
        glm::dot(position, north_axis));
}

glm::vec2 map_position_from_cartesian(const glm::dvec3& position)
{
    const double radius = glm::length(position);
    if (radius <= 0.0)
        return glm::vec2(0.0f);

    const double longitude = std::atan2(position.y, position.x);
    const double latitude = std::asin(std::clamp(position.z / radius, -1.0, 1.0));
    return map_position(longitude, latitude);
}

} // namespace

glm::vec2 satview_map_position_from_teme(
    const glm::dvec3& teme_position,
    double unix_seconds,
    glm::vec2 center_radians)
{
    center_radians = normalized_satview_map_center(center_radians);
    const glm::dvec3 ecef_position = teme_to_ecef(
        teme_position,
        greenwich_sidereal_angle_radians(unix_seconds));
    return map_position_from_cartesian(ecef_to_map_local(
        ecef_position,
        glm::dvec2(center_radians)));
}

glm::vec2 normalized_satview_map_center(glm::vec2 center_radians)
{
    constexpr float kPi = std::numbers::pi_v<float>;
    constexpr float kLatitudeLimit = 0.5f * kPi - 0.001f;
    center_radians.x = std::remainder(center_radians.x, 2.0f * kPi);
    center_radians.y = std::clamp(center_radians.y, -kLatitudeLimit, kLatitudeLimit);
    return center_radians;
}

glm::vec2 satview_map_pan_delta(glm::vec2 pixel_delta, glm::vec2 viewport_size)
{
    constexpr float kPi = std::numbers::pi_v<float>;
    viewport_size = glm::max(viewport_size, glm::vec2(1.0f));
    return glm::vec2(
        -pixel_delta.x * 2.0f * kPi / viewport_size.x,
        pixel_delta.y * kPi / viewport_size.y);
}

} // namespace draxul::satview
