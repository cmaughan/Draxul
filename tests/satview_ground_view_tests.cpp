#include "satview_ground_view.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <numbers>

namespace
{

using Catch::Matchers::WithinAbs;
using draxul::satview::satview_ground_body_proxy;
using draxul::satview::satview_ground_basis;
using draxul::satview::satview_ground_local_direction_to_render;
using draxul::satview::satview_default_ground_camera_orientation;
using draxul::satview::satview_ground_camera_world_orientation;
using draxul::satview::satview_ground_location_from_map_ndc;
using draxul::satview::satview_ground_location_from_render_position;
using draxul::satview::satview_ground_marker_base_size;
using draxul::satview::satview_ground_track_subdivision_count;
using draxul::satview::satview_ground_render_position;
using draxul::satview::satview_ground_view_matrix;
using draxul::satview::satview_ground_visibility_dot;
using draxul::satview::satview_rotate_ground_camera;
using draxul::satview::satview_interpolate_track_arc;
using draxul::satview::SatViewGroundLocation;
using draxul::satview::SatViewGroundBasis;

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
    const glm::mat4 view = satview_ground_view_matrix(
        observer,
        satview_default_ground_camera_orientation());
    const glm::vec4 eye = view * glm::vec4(observer, 1.0f);
    const glm::vec4 above = view * glm::vec4(observer * 2.0, 1.0f);

    CHECK_THAT(eye.x, WithinAbs(0.0f, 1.0e-5f));
    CHECK_THAT(eye.y, WithinAbs(0.0f, 1.0e-5f));
    CHECK(above.z < eye.z);
}

TEST_CASE("SatView ground camera crosses zenith and nadir without degenerating", "[satview][ground]")
{
    const SatViewGroundLocation location{
        .longitude_radians = 0.0,
        .latitude_radians = 0.0,
    };
    const glm::dvec3 observer = satview_ground_render_position(location, 0.0);
    glm::quat orientation = satview_default_ground_camera_orientation();
    for (int step = 0; step < 200; ++step)
        orientation = satview_rotate_ground_camera(orientation, glm::vec2(0.0f, 0.01f));

    const glm::quat world_orientation = satview_ground_camera_world_orientation(observer, orientation);
    const glm::vec3 forward = world_orientation * glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 camera_up = world_orientation * glm::vec3(0.0f, 1.0f, 0.0f);

    CHECK_THAT(glm::length(forward), WithinAbs(1.0f, 1.0e-5f));
    CHECK_THAT(glm::length(camera_up), WithinAbs(1.0f, 1.0e-5f));
    CHECK_THAT(glm::dot(forward, camera_up), WithinAbs(0.0f, 1.0e-5f));
    CHECK(glm::dot(forward, glm::normalize(glm::vec3(observer))) < 0.0f);
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

TEST_CASE("SatView ground tracks subdivide large apparent sweeps", "[satview][ground]")
{
    const glm::dvec3 observer(1.0, 0.0, 0.0);
    const glm::dvec3 overhead(1.06, 0.0, 0.0);
    const glm::dvec3 nearby(1.06, 0.002, 0.0);
    const glm::dvec3 horizon(1.0, 0.36, 0.0);

    CHECK(satview_ground_track_subdivision_count(overhead, nearby, observer) == 1);
    CHECK(satview_ground_track_subdivision_count(overhead, horizon, observer) >= 20);
}

TEST_CASE("SatView ground track interpolation follows the Earth-centered arc", "[satview][ground]")
{
    constexpr double radius = 1.2;
    const glm::dvec3 start(radius, 0.0, 0.0);
    const glm::dvec3 end(0.0, radius, 0.0);

    const glm::dvec3 midpoint = satview_interpolate_track_arc(start, end, 0.5);

    CHECK_THAT(glm::length(midpoint), WithinAbs(radius, 1.0e-12));
    CHECK_THAT(midpoint.x, WithinAbs(radius / std::sqrt(2.0), 1.0e-12));
    CHECK_THAT(midpoint.y, WithinAbs(radius / std::sqrt(2.0), 1.0e-12));
    CHECK_THAT(midpoint.z, WithinAbs(0.0, 1.0e-12));
}

TEST_CASE("SatView ground view proxy preserves distant body direction and angular size", "[satview][ground]")
{
    const glm::dvec3 observer(1.0, 0.0, 0.0);
    const glm::dvec3 body(15000.0, 18000.0, -7000.0);
    constexpr double kBodyRadius = 109.0;
    constexpr double kProxyDistance = 96.0;

    const auto proxy = satview_ground_body_proxy(body, kBodyRadius, observer, kProxyDistance);
    const glm::dvec3 actual_offset = body - observer;
    const glm::dvec3 proxy_offset = proxy.render_position_earth_radii - observer;

    CHECK_THAT(glm::length(proxy_offset), WithinAbs(kProxyDistance, 1.0e-9));
    CHECK_THAT(
        glm::dot(glm::normalize(actual_offset), glm::normalize(proxy_offset)),
        WithinAbs(1.0, 1.0e-12));
    CHECK_THAT(
        proxy.radius_earth_radii / glm::length(proxy_offset),
        WithinAbs(kBodyRadius / glm::length(actual_offset), 1.0e-12));
}

TEST_CASE("SatView ground basis maps local east north and up consistently", "[satview][ground]")
{
    const glm::dvec3 observer = glm::normalize(glm::dvec3(-0.4, 0.7, -0.5));
    const SatViewGroundBasis basis = satview_ground_basis(observer);

    CHECK_THAT(glm::length(basis.east), WithinAbs(1.0, 1.0e-12));
    CHECK_THAT(glm::length(basis.north), WithinAbs(1.0, 1.0e-12));
    CHECK_THAT(glm::length(basis.up), WithinAbs(1.0, 1.0e-12));
    CHECK_THAT(glm::dot(basis.east, basis.north), WithinAbs(0.0, 1.0e-12));
    CHECK_THAT(glm::dot(basis.east, basis.up), WithinAbs(0.0, 1.0e-12));
    CHECK_THAT(glm::dot(basis.north, basis.up), WithinAbs(0.0, 1.0e-12));
    CHECK_THAT(
        glm::dot(satview_ground_local_direction_to_render(observer, { 1.0, 0.0, 0.0 }), basis.east),
        WithinAbs(1.0, 1.0e-12));
}

} // namespace
