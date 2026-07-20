#include <draxul/satview/satview_moon_ephemeris.h>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <draxul/satview/satview_propagation.h>
#include <glm/geometric.hpp>
#include <numbers>

namespace
{

using Catch::Matchers::WithinAbs;
using draxul::satview::kSatViewEarthEquatorialRadiusKm;
using draxul::satview::satview_moon_orbit_track;
using draxul::satview::satview_moon_position;
using draxul::satview::solar_direction_render;

TEST_CASE("SatView Moon ephemeris agrees with a JPL Horizons reference vector", "[satview][moon]")
{
    constexpr double kMarchEquinoxNoonUtc = 1710936000.0;
    const glm::dvec3 horizons_icrf_km(
        -246450.8602314188,
        276460.3671206749,
        157023.9968924827);

    const auto moon = satview_moon_position(kMarchEquinoxNoonUtc);
    const double angular_error_radians = std::acos(std::clamp(
        glm::dot(
            glm::normalize(moon.equatorial_position_km),
            glm::normalize(horizons_icrf_km)),
        -1.0,
        1.0));
    CHECK(angular_error_radians * 180.0 / std::numbers::pi_v<double> < 1.0);
    CHECK_THAT(
        glm::length(moon.equatorial_position_km),
        WithinAbs(glm::length(horizons_icrf_km), 1500.0));
}

TEST_CASE("SatView Moon render position uses Earth-radius scene units", "[satview][moon]")
{
    const auto moon = satview_moon_position(1710936000.0);
    CHECK_THAT(
        glm::length(moon.render_position_earth_radii),
        WithinAbs(
            glm::length(moon.equatorial_position_km) / kSatViewEarthEquatorialRadiusKm,
            1.0e-10));
    CHECK(glm::length(moon.render_position_earth_radii) > 55.0);
    CHECK(glm::length(moon.render_position_earth_radii) < 65.0);
}

TEST_CASE("SatView Moon orbit track spans a lunar cycle around its center time", "[satview][moon][track]")
{
    constexpr double kCenterSeconds = 1710936000.0;
    constexpr std::size_t kSegments = 48;
    const auto track = satview_moon_orbit_track(kCenterSeconds, kSegments);

    REQUIRE(track.size() == kSegments);
    const glm::dvec3 center_position = satview_moon_position(kCenterSeconds).render_position_earth_radii;
    CHECK(glm::length(track[kSegments / 2] - center_position) < 1.0e-10);
    for (const glm::dvec3& position : track)
    {
        CHECK(glm::length(position) > 55.0);
        CHECK(glm::length(position) < 65.0);
    }
}

TEST_CASE("SatView Moon phase follows the observer side", "[satview][moon][lighting]")
{
    constexpr double kJune28NoonUtc = 1782648000.0;
    const glm::dvec3 moon = satview_moon_position(kJune28NoonUtc).render_position_earth_radii;
    const glm::dvec3 sun = solar_direction_render(kJune28NoonUtc);
    const auto illuminated_fraction = [&](const glm::dvec3& observer) {
        const glm::dvec3 observer_direction = glm::normalize(observer - moon);
        return 0.5 * (1.0 + glm::dot(observer_direction, sun));
    };

    CHECK(illuminated_fraction(glm::dvec3(0.0)) > 0.97);
    CHECK(illuminated_fraction(moon * 2.0) < 0.03);
}

} // namespace
