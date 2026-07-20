#include <draxul/satview/satview_lunar_frame.h>
#include <draxul/satview/satview_map_projection.h>
#include <draxul/satview/satview_moon_ephemeris.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/geometric.hpp>

namespace
{

using Catch::Matchers::WithinAbs;
using namespace draxul::satview;

TEST_CASE("SatView lunar body directions round trip through the Moon frame", "[satview][moon][surface]")
{
    const glm::dvec3 moon_position = satview_moon_position(946728000.0).render_position_earth_radii;
    const glm::dvec3 body = satview_lunar_body_direction(-69.374, 32.3187);
    const glm::dvec3 render = moon_position
        + satview_lunar_body_to_render_direction(body, moon_position) * 0.25;
    const glm::dvec3 round_trip = satview_render_to_lunar_body(render, moon_position) / 0.25;

    CHECK_THAT(round_trip.x, WithinAbs(body.x, 1.0e-12));
    CHECK_THAT(round_trip.y, WithinAbs(body.y, 1.0e-12));
    CHECK_THAT(round_trip.z, WithinAbs(body.z, 1.0e-12));
}

TEST_CASE("SatView lunar regression sites retain LROC map coordinates", "[satview][moon][surface]")
{
    struct Site
    {
        double latitude;
        double longitude;
    };
    const Site sites[] = {
        { 0.67416, 23.47314 },
        { -45.4562, 177.5886 },
        { -69.374, 32.3187 },
        { 18.5622, 61.8102 },
    };

    for (const Site& site : sites)
    {
        const glm::vec2 map = satview_map_position_from_lunar_body(
            satview_lunar_body_direction(site.latitude, site.longitude));
        CHECK_THAT(map.x, WithinAbs(static_cast<float>(site.longitude / 180.0), 1.0e-6f));
        CHECK_THAT(map.y, WithinAbs(static_cast<float>(site.latitude / 90.0), 1.0e-6f));
    }
}

TEST_CASE("SatView lunar near and far side directions face opposite the Earth", "[satview][moon][surface]")
{
    const glm::dvec3 moon_position = satview_moon_position(946728000.0).render_position_earth_radii;
    const glm::dvec3 earth_direction = glm::normalize(-moon_position);
    const glm::dvec3 apollo_direction = satview_lunar_body_to_render_direction(
        satview_lunar_body_direction(0.67416, 23.47314), moon_position);
    const glm::dvec3 change_four_direction = satview_lunar_body_to_render_direction(
        satview_lunar_body_direction(-45.4562, 177.5886), moon_position);

    CHECK(glm::dot(apollo_direction, earth_direction) > 0.8);
    CHECK(glm::dot(change_four_direction, earth_direction) < -0.6);
}

} // namespace
