#pragma once

#include <glm/glm.hpp>

namespace draxul::satview
{

inline constexpr double kSatViewMoonMeanRadiusKm = 1737.4;

struct SatViewMoonPosition
{
    glm::dvec3 equatorial_position_km{ 0.0 };
    glm::dvec3 render_position_earth_radii{ 0.0 };
};

[[nodiscard]] SatViewMoonPosition satview_moon_position(double unix_seconds);

} // namespace draxul::satview
