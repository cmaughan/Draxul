#pragma once

#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace draxul::satview
{

inline constexpr double kSatViewSunMeanRadiusKm = 695700.0;
inline constexpr double kSatViewAstronomicalUnitKm = 149597870.7;
inline constexpr double kSatViewSunSiderealRotationPeriodSeconds = 360.0 / 14.1844 * 86400.0;
inline constexpr double kSatViewEarthOrbitPeriodSeconds = 360.0 / 0.9856474 * 86400.0;

struct SatViewSunPosition
{
    glm::dvec3 equatorial_position_km{ 0.0 };
    glm::dvec3 render_position_earth_radii{ 0.0 };
    glm::dquat body_to_render_orientation{ 1.0, 0.0, 0.0, 0.0 };
};

[[nodiscard]] SatViewSunPosition satview_sun_position(double unix_seconds);
[[nodiscard]] std::vector<glm::dvec3> satview_earth_orbit_track(
    double center_unix_seconds,
    std::size_t segment_count);

} // namespace draxul::satview
