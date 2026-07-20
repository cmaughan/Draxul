#include <draxul/satview/satview_landscape.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

using namespace draxul::satview;

TEST_CASE("SatView local azimuth directions use north-through-east convention", "[satview][landscape]")
{
    CHECK(satview_local_azimuth_altitude_direction(0.0f, 0.0f)
        == glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 east = satview_local_azimuth_altitude_direction(
        glm::radians(90.0f), 0.0f);
    CHECK(east.x == Catch::Approx(1.0f).margin(0.0001f));
    CHECK(east.y == Catch::Approx(0.0f).margin(0.0001f));
    CHECK(east.z == Catch::Approx(0.0f).margin(0.0001f));
}

TEST_CASE("SatView observatory landscape is a bounded angular mesh", "[satview][landscape]")
{
    constexpr float kObserverAltitudeEarthRadii = 0.0003f;
    const SatViewLandscapeMesh mesh = build_satview_observatory_landscape(kObserverAltitudeEarthRadii);
    const float expected_horizon_altitude = -std::acos(1.0f / (1.0f + kObserverAltitudeEarthRadii));
    REQUIRE(mesh.fill_triangles.size() > 40);
    REQUIRE(mesh.rim_lines.size() > 50);
    CHECK(std::asin(mesh.fill_triangles.front().local_direction0.z)
        == Catch::Approx(expected_horizon_altitude + glm::radians(-0.05f)).margin(0.0001f));
    for (const SatViewLandscapeTriangleInstance& triangle : mesh.fill_triangles)
    {
        for (const glm::vec4& direction : {
                 triangle.local_direction0,
                 triangle.local_direction1,
                 triangle.local_direction2 })
        {
            CHECK(glm::length(glm::vec3(direction))
                == Catch::Approx(1.0f).margin(0.0001f));
            const float altitude = std::asin(direction.z);
            CHECK(altitude >= expected_horizon_altitude + glm::radians(-0.06f));
            CHECK(altitude <= expected_horizon_altitude + glm::radians(7.01f));
        }
    }
}
