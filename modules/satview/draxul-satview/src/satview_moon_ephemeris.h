#pragma once

#include <cstddef>
#include <glm/glm.hpp>
#include <vector>

namespace draxul::satview
{

inline constexpr double kSatViewMoonMeanRadiusKm = 1737.4;
inline constexpr double kSatViewMoonSiderealPeriodSeconds = 27.321661 * 86400.0;

struct SatViewMoonPosition
{
    glm::dvec3 equatorial_position_km{ 0.0 };
    glm::dvec3 render_position_earth_radii{ 0.0 };
};

[[nodiscard]] SatViewMoonPosition satview_moon_position(double unix_seconds);
[[nodiscard]] std::vector<glm::dvec3> satview_moon_orbit_track(
    double center_unix_seconds,
    std::size_t segment_count);

} // namespace draxul::satview
