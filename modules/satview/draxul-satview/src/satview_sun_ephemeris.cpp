#include "satview_sun_ephemeris.h"

#include <cmath>
#include <algorithm>
#include <draxul/satview/satview_propagation.h>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <numbers>

namespace draxul::satview
{

namespace
{

double radians(double degrees)
{
    return std::remainder(degrees, 360.0) * std::numbers::pi_v<double> / 180.0;
}

glm::dvec3 equatorial_to_render(const glm::dvec3& equatorial)
{
    return glm::dvec3(-equatorial.y, equatorial.z, -equatorial.x);
}

glm::dquat sun_body_to_render_orientation(double days_since_j2000)
{
    constexpr double kPoleRightAscensionDegrees = 286.13;
    constexpr double kPoleDeclinationDegrees = 63.87;
    constexpr double kPrimeMeridianAtJ2000Degrees = 84.176;
    constexpr double kPrimeMeridianDegreesPerDay = 14.1844;

    const double right_ascension = radians(kPoleRightAscensionDegrees);
    const double declination = radians(kPoleDeclinationDegrees);
    const glm::dvec3 equatorial_north(
        std::cos(declination) * std::cos(right_ascension),
        std::cos(declination) * std::sin(right_ascension),
        std::sin(declination));
    const glm::dvec3 north = glm::normalize(equatorial_to_render(equatorial_north));

    const glm::dvec3 equatorial_x_render(0.0, 0.0, -1.0);
    const glm::dvec3 reference_x = glm::normalize(
        equatorial_x_render - north * glm::dot(equatorial_x_render, north));
    const glm::dvec3 reference_y = glm::normalize(glm::cross(north, reference_x));
    const double prime_meridian = radians(
        kPrimeMeridianAtJ2000Degrees
        + kPrimeMeridianDegreesPerDay * days_since_j2000);
    const glm::dvec3 body_x = glm::normalize(
        reference_x * std::cos(prime_meridian)
        + reference_y * std::sin(prime_meridian));
    const glm::dvec3 body_y = glm::normalize(glm::cross(north, body_x));
    return glm::normalize(glm::quat_cast(glm::dmat3(body_x, body_y, north)));
}

} // namespace

SatViewSunPosition satview_sun_position(double unix_seconds)
{
    const double days_since_j2000 =
        julian_date_from_unix_seconds(unix_seconds).value() - 2451545.0;
    const double mean_anomaly = radians(357.529 + 0.98560028 * days_since_j2000);
    const double distance_au =
        1.00014 - 0.01671 * std::cos(mean_anomaly)
        - 0.00014 * std::cos(2.0 * mean_anomaly);
    const glm::dvec3 equatorial_position_km =
        solar_direction_teme(unix_seconds)
        * (distance_au * kSatViewAstronomicalUnitKm);

    return {
        equatorial_position_km,
        teme_position_to_render_earth_radii(equatorial_position_km),
        sun_body_to_render_orientation(days_since_j2000),
    };
}

std::vector<glm::dvec3> satview_earth_orbit_track(
    double center_unix_seconds,
    std::size_t segment_count)
{
    segment_count = std::max<std::size_t>(3, segment_count);
    std::vector<glm::dvec3> positions;
    positions.reserve(segment_count);

    const double start_seconds =
        center_unix_seconds - 0.5 * kSatViewEarthOrbitPeriodSeconds;
    for (std::size_t i = 0; i < segment_count; ++i)
    {
        const double fraction = static_cast<double>(i) / static_cast<double>(segment_count);
        positions.push_back(-satview_sun_position(
            start_seconds + fraction * kSatViewEarthOrbitPeriodSeconds)
                                 .render_position_earth_radii);
    }
    return positions;
}

} // namespace draxul::satview
