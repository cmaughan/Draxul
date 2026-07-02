#include "satview_ground_view.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>

namespace
{

using Catch::Matchers::WithinAbs;
using draxul::satview::satview_ground_location_from_map_ndc;
using draxul::satview::satview_ground_location_from_render_position;
using draxul::satview::satview_ground_marker_base_size;
using draxul::satview::satview_ground_render_position;
using draxul::satview::satview_ground_view_matrix;
using draxul::satview::satview_ground_visibility_dot;
using draxul::satview::SatViewGroundLocation;

constexpr double kHalfPi = 0.5 * std::numbers::pi_v<double>;

TEST_CASE("SatView ground view maps centered map clicks to the map center", "[satview][ground]")
{
    const SatViewGroundLocation location = satview_ground_location_from_map_ndc(glm::vec2(0.0f), glm::vec2(0.7f, 0.3f));

    CHECK_THAT(location.longitude_radians, WithinAbs(0.7, 1.0e-6));
    CHECK_THAT(location.latitude_radians, WithinAbs(0.3, 1.0e-6));
}

TEST_CASE("SatView ground view maps map edges to local horizon longitudes", "[satview][ground]")
{
    const SatViewGroundLocation east = satview_ground_location_from_map_ndc(glm::vec2(0.5f, 0.0f), glm::vec2(0.0f));
    const SatViewGroundLocation north = satview_ground_location_from_map_ndc(glm::vec2(0.0f, 1.0f), glm::vec2(0.0f));

    CHECK_THAT(east.longitude_radians, WithinAbs(kHalfPi, 1.0e-6));
    CHECK_THAT(east.latitude_radians, WithinAbs(0.0, 1.0e-6));
    CHECK_THAT(north.longitude_radians, WithinAbs(0.0, 1.0e-6));
    CHECK_THAT(north.latitude_radians, WithinAbs(kHalfPi, 1.0e-6));
}

TEST_CASE("SatView ground view produces an upward render-space observer", "[satview][ground]")
{
    const SatViewGroundLocation location{
        .longitude_radians = 0.0,
        .latitude_radians = 0.0,
    };

    const glm::dvec3 observer = satview_ground_render_position(location, 0.0);

    CHECK_THAT(glm::length(observer), WithinAbs(1.0, 1.0e-9));
    CHECK(satview_ground_visibility_dot(observer * 2.0, observer) > 0.0);
    CHECK(satview_ground_visibility_dot(-observer * 2.0, observer) < 0.0);
}

TEST_CASE("SatView ground view recovers geodetic coordinates from render positions", "[satview][ground]")
{
    const SatViewGroundLocation expected{
        .longitude_radians = -0.8,
        .latitude_radians = 0.45,
    };
    const glm::dvec3 render_position = satview_ground_render_position(expected, 1234567.0);
    const SatViewGroundLocation actual = satview_ground_location_from_render_position(render_position, 1234567.0);

    CHECK_THAT(actual.longitude_radians, WithinAbs(expected.longitude_radians, 1.0e-9));
    CHECK_THAT(actual.latitude_radians, WithinAbs(expected.latitude_radians, 1.0e-9));
}

TEST_CASE("SatView ground view matrix looks above the observer by default", "[satview][ground]")
{
    const SatViewGroundLocation location{
        .longitude_radians = 0.0,
        .latitude_radians = 0.0,
    };
    const glm::dvec3 observer = satview_ground_render_position(location, 0.0);
    const glm::mat4 view = satview_ground_view_matrix(observer, 0.0f, 0.0f);
    const glm::vec4 eye = view * glm::vec4(observer, 1.0f);
    const glm::vec4 above = view * glm::vec4(observer * 2.0, 1.0f);

    CHECK_THAT(eye.x, WithinAbs(0.0f, 1.0e-5f));
    CHECK_THAT(eye.y, WithinAbs(0.0f, 1.0e-5f));
    CHECK(above.z < eye.z);
}

TEST_CASE("SatView ground view keeps zenith above the horizon when tilted", "[satview][ground]")
{
    const SatViewGroundLocation location{
        .longitude_radians = 0.0,
        .latitude_radians = 0.0,
    };
    const glm::dvec3 observer = satview_ground_render_position(location, 0.0);
    const glm::mat4 view = satview_ground_view_matrix(observer, 0.0f, 1.4f);
    const glm::vec4 eye = view * glm::vec4(observer, 1.0f);
    const glm::vec4 zenith = view * glm::vec4(observer * 2.0, 1.0f);

    CHECK(zenith.y > eye.y);
}

TEST_CASE("SatView ground view marker size shrinks toward the horizon", "[satview][ground]")
{
    const glm::dvec3 observer(1.0, 0.0, 0.0);
    const glm::dvec3 overhead_low_orbit(1.06, 0.0, 0.0);
    const glm::dvec3 near_horizon_low_orbit(1.0, 0.36, 0.0);

    const float overhead_size = satview_ground_marker_base_size(overhead_low_orbit, observer);
    const float horizon_size = satview_ground_marker_base_size(near_horizon_low_orbit, observer);

    CHECK_THAT(overhead_size, WithinAbs(0.026f, 1.0e-6f));
    CHECK_THAT(horizon_size, WithinAbs(0.008f, 1.0e-6f));
}

} // namespace
