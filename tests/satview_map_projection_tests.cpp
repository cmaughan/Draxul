#include "satview_map_projection.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <draxul/satview/satview_propagation.h>

namespace
{

using Catch::Matchers::WithinAbs;
using draxul::satview::greenwich_sidereal_angle_radians;
using draxul::satview::normalized_satview_map_center;
using draxul::satview::satview_map_position_from_teme;
using draxul::satview::satview_map_pan_delta;

TEST_CASE("SatView map projection aligns TEME Greenwich with the map center", "[satview][map]")
{
    constexpr double kJ2000UnixSeconds = 946728000.0;
    const double sidereal_angle = greenwich_sidereal_angle_radians(kJ2000UnixSeconds);
    const glm::dvec3 teme_greenwich(
        std::cos(sidereal_angle),
        std::sin(sidereal_angle),
        0.0);

    const glm::vec2 map_position =
        satview_map_position_from_teme(teme_greenwich, kJ2000UnixSeconds);
    CHECK_THAT(map_position.x, WithinAbs(0.0f, 1.0e-6f));
    CHECK_THAT(map_position.y, WithinAbs(0.0f, 1.0e-6f));
}

TEST_CASE("SatView map projection recenters longitude and latitude", "[satview][map]")
{
    constexpr double kJ2000UnixSeconds = 946728000.0;
    const double sidereal_angle = greenwich_sidereal_angle_radians(kJ2000UnixSeconds);
    const glm::dvec3 east_90_teme(-std::sin(sidereal_angle), std::cos(sidereal_angle), 0.0);
    const glm::dvec3 north_45_teme(
        std::cos(sidereal_angle) * std::sqrt(0.5),
        std::sin(sidereal_angle) * std::sqrt(0.5),
        std::sqrt(0.5));

    const glm::vec2 east_center = satview_map_position_from_teme(
        east_90_teme,
        kJ2000UnixSeconds,
        glm::vec2(0.5f * std::acos(-1.0f), 0.0f));
    CHECK_THAT(east_center.x, WithinAbs(0.0f, 1.0e-6f));
    CHECK_THAT(east_center.y, WithinAbs(0.0f, 1.0e-6f));

    const glm::vec2 north_center = satview_map_position_from_teme(
        north_45_teme,
        kJ2000UnixSeconds,
        glm::vec2(0.0f, 0.25f * std::acos(-1.0f)));
    CHECK_THAT(north_center.x, WithinAbs(0.0f, 1.0e-6f));
    CHECK_THAT(north_center.y, WithinAbs(0.0f, 1.0e-6f));
}

TEST_CASE("SatView map center wraps longitude and clamps latitude", "[satview][map]")
{
    const glm::vec2 center = normalized_satview_map_center(glm::vec2(7.0f, 2.0f));
    CHECK(center.x >= -std::acos(-1.0f));
    CHECK(center.x <= std::acos(-1.0f));
    CHECK(center.y < 0.5f * std::acos(-1.0f));
}

TEST_CASE("SatView map drag follows the grabbed surface", "[satview][map]")
{
    const glm::vec2 delta = satview_map_pan_delta(
        glm::vec2(200.0f, -100.0f),
        glm::vec2(800.0f, 400.0f));
    CHECK_THAT(delta.x, WithinAbs(-0.5f * std::acos(-1.0f), 1.0e-6f));
    CHECK_THAT(delta.y, WithinAbs(-0.25f * std::acos(-1.0f), 1.0e-6f));
}

} // namespace
