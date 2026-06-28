#pragma once

#include <glm/glm.hpp>

namespace draxul::satview
{

enum class SatViewMapBody
{
    Earth,
    Moon
};

[[nodiscard]] glm::vec2 satview_map_position_from_teme(
    const glm::dvec3& teme_position,
    double unix_seconds,
    glm::vec2 center_radians = glm::vec2(0.0f),
    SatViewMapBody body = SatViewMapBody::Earth,
    const glm::dvec3& moon_render_position_earth_radii = glm::dvec3(0.0));
[[nodiscard]] glm::vec2 normalized_satview_map_center(glm::vec2 center_radians);
[[nodiscard]] glm::vec2 satview_map_pan_delta(
    glm::vec2 pixel_delta,
    glm::vec2 viewport_size);

} // namespace draxul::satview
