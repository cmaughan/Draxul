#include "satview_sky_projection.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace
{

using Catch::Matchers::WithinAbs;
using draxul::satview::satview_clamp_ground_fov_degrees;
using draxul::satview::satview_project_camera_direction;
using draxul::satview::satview_stereographic_frame_transform;
using draxul::satview::satview_unproject_ground_ndc;
using draxul::satview::SatViewGroundProjection;

void check_round_trip(
    SatViewGroundProjection projection,
    glm::vec3 direction,
    float fov_degrees,
    float aspect)
{
    direction = glm::normalize(direction);
    const auto projected = satview_project_camera_direction(
        projection,
        direction,
        fov_degrees,
        aspect);
    REQUIRE(projected.valid);
    const glm::vec3 recovered = satview_unproject_ground_ndc(
        projection,
        projected.ndc,
        fov_degrees,
        aspect);
    CHECK_THAT(glm::dot(direction, recovered), WithinAbs(1.0f, 1.0e-5f));
}

TEST_CASE("SatView sky projections place the view direction at screen center", "[satview][projection]")
{
    for (const auto projection : {
             SatViewGroundProjection::Stereographic,
             SatViewGroundProjection::Perspective })
    {
        const auto point = satview_project_camera_direction(
            projection,
            glm::vec3(0.0f, 0.0f, -1.0f),
            60.0f,
            16.0f / 9.0f);
        REQUIRE(point.valid);
        CHECK_THAT(point.ndc.x, WithinAbs(0.0f, 1.0e-6f));
        CHECK_THAT(point.ndc.y, WithinAbs(0.0f, 1.0e-6f));
    }
}

TEST_CASE("SatView sky projections round trip camera directions", "[satview][projection]")
{
    check_round_trip(
        SatViewGroundProjection::Perspective,
        glm::vec3(0.24f, -0.18f, -1.0f),
        80.0f,
        1.6f);
    check_round_trip(
        SatViewGroundProjection::Stereographic,
        glm::vec3(0.72f, 0.31f, -0.22f),
        200.0f,
        1.6f);
}

TEST_CASE("SatView sky projection field of view reaches the vertical viewport edge", "[satview][projection]")
{
    for (const auto projection : {
             SatViewGroundProjection::Stereographic,
             SatViewGroundProjection::Perspective })
    {
        constexpr float fov_degrees = 100.0f;
        const float half_fov = 0.5f * glm::radians(fov_degrees);
        const glm::vec3 top_edge_direction(
            0.0f,
            std::sin(half_fov),
            -std::cos(half_fov));
        const auto point = satview_project_camera_direction(
            projection,
            top_edge_direction,
            fov_degrees,
            16.0f / 9.0f);

        REQUIRE(point.valid);
        CHECK_THAT(point.ndc.x, WithinAbs(0.0f, 1.0e-6f));
        CHECK_THAT(point.ndc.y, WithinAbs(1.0f, 1.0e-5f));
    }
}

TEST_CASE("SatView stereographic projection can represent directions behind the view plane", "[satview][projection]")
{
    const auto stereographic = satview_project_camera_direction(
        SatViewGroundProjection::Stereographic,
        glm::vec3(0.45f, 0.0f, 0.89f),
        235.0f,
        1.0f);
    const auto perspective = satview_project_camera_direction(
        SatViewGroundProjection::Perspective,
        glm::vec3(0.45f, 0.0f, 0.89f),
        120.0f,
        1.0f);

    CHECK(stereographic.valid);
    CHECK_FALSE(perspective.valid);
}

TEST_CASE("SatView ground projection clamps each lens to its useful field of view", "[satview][projection]")
{
    CHECK(satview_clamp_ground_fov_degrees(
              SatViewGroundProjection::Perspective,
              300.0f)
        == 120.0f);
    CHECK(satview_clamp_ground_fov_degrees(
              SatViewGroundProjection::Stereographic,
              300.0f)
        == 235.0f);
}

TEST_CASE("SatView stereographic frame transform carries rotation and lens scale", "[satview][projection]")
{
    const glm::mat4 view = glm::lookAtRH(
        glm::vec3(2.0f, 1.0f, 0.5f),
        glm::vec3(0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 transform = satview_stereographic_frame_transform(view, 90.0f, 2.0f);

    CHECK(glm::mat3(transform) == glm::mat3(view));
    CHECK_THAT(transform[3][2], WithinAbs(std::tan(glm::radians(22.5f)), 1.0e-6f));
    CHECK_THAT(transform[3][3], WithinAbs(0.5f, 1.0e-6f));
}

} // namespace
