#include <draxul/satview/satview_sun_ephemeris.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <draxul/satview/satview_propagation.h>
#include <glm/geometric.hpp>
#include <numbers>

namespace
{

using Catch::Matchers::WithinAbs;
using draxul::satview::kSatViewAstronomicalUnitKm;
using draxul::satview::kSatViewEarthEquatorialRadiusKm;
using draxul::satview::kSatViewEarthOrbitPeriodSeconds;
using draxul::satview::kSatViewSunMeanRadiusKm;
using draxul::satview::kSatViewSunSiderealRotationPeriodSeconds;
using draxul::satview::satview_earth_orbit_track;
using draxul::satview::satview_sun_position;
using draxul::satview::solar_direction_render;

TEST_CASE("SatView Sun ephemeris uses date-aware Earth distance", "[satview][sun]")
{
    constexpr double kMarchEquinoxNoonUtc = 1710936000.0;
    const auto sun = satview_sun_position(kMarchEquinoxNoonUtc);
    const double distance_au = glm::length(sun.equatorial_position_km)
        / kSatViewAstronomicalUnitKm;

    CHECK(distance_au > 0.983);
    CHECK(distance_au < 1.017);
    CHECK_THAT(
        glm::length(sun.render_position_earth_radii),
        WithinAbs(
            glm::length(sun.equatorial_position_km) / kSatViewEarthEquatorialRadiusKm,
            1.0e-9));
    CHECK(glm::dot(
              glm::normalize(sun.render_position_earth_radii),
              solar_direction_render(kMarchEquinoxNoonUtc))
        > 0.999999);
    const double angular_diameter_degrees = 2.0 * std::asin(kSatViewSunMeanRadiusKm / glm::length(sun.equatorial_position_km))
        * 180.0 / std::numbers::pi_v<double>;
    CHECK(angular_diameter_degrees > 0.52);
    CHECK(angular_diameter_degrees < 0.55);
}

TEST_CASE("SatView Sun orientation preserves the IAU pole and rotation period", "[satview][sun]")
{
    constexpr double kJ2000UnixSeconds = 946728000.0;
    const auto first = satview_sun_position(kJ2000UnixSeconds);
    const auto one_rotation = satview_sun_position(
        kJ2000UnixSeconds + kSatViewSunSiderealRotationPeriodSeconds);

    const glm::dvec3 north = first.body_to_render_orientation * glm::dvec3(0.0, 0.0, 1.0);
    const double right_ascension = 286.13 * std::numbers::pi_v<double> / 180.0;
    const double declination = 63.87 * std::numbers::pi_v<double> / 180.0;
    const glm::dvec3 equatorial_north(
        std::cos(declination) * std::cos(right_ascension),
        std::cos(declination) * std::sin(right_ascension),
        std::sin(declination));
    const glm::dvec3 expected_render_north(
        -equatorial_north.y,
        equatorial_north.z,
        -equatorial_north.x);
    CHECK(glm::dot(glm::normalize(north), glm::normalize(expected_render_north)) > 0.999999);

    const glm::dvec3 first_meridian = first.body_to_render_orientation * glm::dvec3(1.0, 0.0, 0.0);
    const glm::dvec3 repeated_meridian = one_rotation.body_to_render_orientation * glm::dvec3(1.0, 0.0, 0.0);
    CHECK(glm::dot(first_meridian, repeated_meridian) > 0.999999);
}

TEST_CASE("SatView Earth orbit track is heliocentric and passes through Earth", "[satview][sun][track]")
{
    constexpr double kCenterSeconds = 1710936000.0;
    constexpr std::size_t kSegments = 256;
    const auto track = satview_earth_orbit_track(kCenterSeconds, kSegments);
    const auto sun = satview_sun_position(kCenterSeconds);

    REQUIRE(track.size() == kSegments);
    CHECK(glm::length(track[kSegments / 2] + sun.render_position_earth_radii) < 1.0e-9);
    for (const glm::dvec3& position : track)
    {
        const double distance_au = glm::length(position)
            * kSatViewEarthEquatorialRadiusKm / kSatViewAstronomicalUnitKm;
        CHECK(distance_au > 0.983);
        CHECK(distance_au < 1.017);
    }

    const auto repeated = satview_earth_orbit_track(
        kCenterSeconds + kSatViewEarthOrbitPeriodSeconds,
        kSegments);
    CHECK(glm::length(track.front() - repeated.front()) < 0.25);
}

} // namespace
