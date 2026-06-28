#include "camera.h"
#include "camera_manipulator.h"
#include "satview_render_vk_math.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

using Catch::Approx;
using draxul::satview::Camera;
using draxul::satview::Manipulator;
using draxul::satview::make_satview_vulkan_clip_matrix;

namespace
{

void check_vec3(const glm::vec3& actual, const glm::vec3& expected, float margin = 0.0001f)
{
    CHECK(actual.x == Approx(expected.x).margin(margin));
    CHECK(actual.y == Approx(expected.y).margin(margin));
    CHECK(actual.z == Approx(expected.z).margin(margin));
}

void check_camera_frame(const Camera& camera)
{
    CHECK(glm::length(camera.GetViewDirection()) == Approx(1.0f).margin(0.0001f));
    CHECK(glm::length(camera.GetRight()) == Approx(1.0f).margin(0.0001f));
    CHECK(glm::length(camera.GetUp()) == Approx(1.0f).margin(0.0001f));
    CHECK(glm::dot(camera.GetViewDirection(), camera.GetRight()) == Approx(0.0f).margin(0.0001f));
    CHECK(glm::dot(camera.GetViewDirection(), camera.GetUp()) == Approx(0.0f).margin(0.0001f));
    CHECK(glm::dot(camera.GetRight(), camera.GetUp()) == Approx(0.0f).margin(0.0001f));
    check_vec3(glm::cross(camera.GetRight(), camera.GetUp()), -camera.GetViewDirection());
}

} // namespace

TEST_CASE("SatView quaternion camera builds an orthonormal look-at frame", "[satview][camera]")
{
    Camera camera;
    camera.SetPositionAndFocalPoint(glm::vec3(2.0f, 1.0f, 4.0f), glm::vec3(0.0f));

    CHECK(glm::length(camera.GetViewDirection()) == Approx(1.0f));
    CHECK(glm::length(camera.GetRight()) == Approx(1.0f));
    CHECK(glm::length(camera.GetUp()) == Approx(1.0f));
    CHECK(glm::dot(camera.GetViewDirection(), camera.GetRight()) == Approx(0.0f).margin(0.0001f));
    CHECK(glm::dot(camera.GetViewDirection(), camera.GetUp()) == Approx(0.0f).margin(0.0001f));

    const glm::mat4 view = glm::lookAtRH(
        camera.GetPosition(),
        camera.GetFocalPoint(),
        camera.GetUp());
    check_vec3(glm::vec3(view * glm::vec4(camera.GetPosition(), 1.0f)), glm::vec3(0.0f));
}

TEST_CASE("SatView quaternion camera preserves its orbit convention", "[satview][camera]")
{
    Camera camera;
    camera.SetPositionAndFocalPoint(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f));
    camera.Orbit(glm::vec2(90.0f, 0.0f));
    camera.UpdateOrbit(50);

    check_vec3(camera.GetPosition(), glm::vec3(-4.0f, 0.0f, 0.0f), 0.001f);
    check_vec3(camera.GetViewDirection(), glm::vec3(1.0f, 0.0f, 0.0f), 0.001f);
    CHECK(glm::dot(camera.GetViewDirection(), camera.GetUp()) == Approx(0.0f).margin(0.0001f));
}

TEST_CASE("SatView quaternion camera retargets without changing its view frame", "[satview][camera]")
{
    Camera camera;
    camera.SetDistanceLimits(0.25f, 200.0f);
    camera.SetPositionAndFocalPoint(glm::vec3(2.0f, 1.0f, 4.0f), glm::vec3(0.0f));
    const glm::quat orientation = camera.GetOrientation();
    const glm::vec3 view_direction = camera.GetViewDirection();

    const glm::vec3 moon_target(20.0f, 8.0f, -54.0f);
    camera.SetFocalPointAndDistance(moon_target, 1.25f);

    check_vec3(camera.GetFocalPoint(), moon_target);
    check_vec3(camera.GetViewDirection(), view_direction);
    CHECK(std::abs(glm::dot(camera.GetOrientation(), orientation)) == Approx(1.0f));
    CHECK(camera.GetDistance() == Approx(1.25f));
    check_vec3(camera.GetPosition(), moon_target - view_direction * 1.25f);

    camera.SetFocalPoint(moon_target + glm::vec3(1.0f, -2.0f, 3.0f));
    CHECK(camera.GetDistance() == Approx(1.25f));
    check_vec3(camera.GetViewDirection(), view_direction);
}

TEST_CASE("SatView manipulator maps mouse orbit and dolly deltas", "[satview][camera]")
{
    auto manipulated_camera = std::make_shared<Camera>();
    manipulated_camera->SetPositionAndFocalPoint(glm::vec3(0.0f, 0.0f, 8.0f), glm::vec3(0.0f));
    Manipulator manipulator(manipulated_camera);

    Camera direct_camera;
    direct_camera.SetPositionAndFocalPoint(glm::vec3(0.0f, 0.0f, 8.0f), glm::vec3(0.0f));

    manipulator.MouseDown(glm::vec2(10.0f, 20.0f));
    REQUIRE(manipulator.MouseMove(glm::vec2(30.0f, 10.0f), false));
    manipulated_camera->UpdateOrbit(50);
    direct_camera.Orbit(glm::vec2(10.0f, -5.0f));
    direct_camera.UpdateOrbit(50);
    check_vec3(manipulated_camera->GetPosition(), direct_camera.GetPosition(), 0.001f);

    manipulator.MouseDown(glm::vec2(30.0f, 10.0f));
    REQUIRE(manipulator.MouseMove(glm::vec2(30.0f, 6.0f), true));
    manipulated_camera->UpdatePosition(50);
    CHECK(manipulated_camera->GetDistance() == Approx(7.0f).margin(0.001f));
}

TEST_CASE("SatView quaternion camera keeps dolly inside scene bounds", "[satview][camera]")
{
    Camera camera;
    camera.SetDistanceLimits(2.0f, 12.0f);
    camera.SetPositionAndFocalPoint(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f));

    camera.Dolly(100.0f);
    camera.UpdatePosition(50);
    CHECK(camera.GetDistance() == Approx(2.0f));

    camera.ClearMotion();
    camera.Dolly(-100.0f);
    camera.UpdatePosition(50);
    CHECK(camera.GetDistance() == Approx(12.0f));
}

TEST_CASE("SatView quaternion camera crosses both poles continuously", "[satview][camera]")
{
    Camera camera;
    camera.SetPositionAndFocalPoint(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f));

    glm::vec3 previous_direction = glm::normalize(camera.GetPosition());
    for (int i = 0; i < 12; ++i)
    {
        camera.Orbit(glm::vec2(0.0f, 30.0f));
        camera.UpdateOrbit(50);
        check_camera_frame(camera);

        const glm::vec3 direction = glm::normalize(camera.GetPosition());
        CHECK(glm::dot(previous_direction, direction) > 0.85f);
        previous_direction = direction;

        if (i == 2)
            check_vec3(direction, glm::vec3(0.0f, 1.0f, 0.0f), 0.001f);
        else if (i == 5)
            check_vec3(direction, glm::vec3(0.0f, 0.0f, -1.0f), 0.001f);
        else if (i == 8)
            check_vec3(direction, glm::vec3(0.0f, -1.0f, 0.0f), 0.001f);
    }
    check_vec3(glm::normalize(camera.GetPosition()), glm::vec3(0.0f, 0.0f, 1.0f), 0.001f);
}

TEST_CASE("SatView quaternion camera keeps local marker axes stable at a pole", "[satview][camera]")
{
    Camera camera;
    camera.SetPositionAndFocalPoint(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f));
    camera.Orbit(glm::vec2(0.0f, 90.0f));
    camera.UpdateOrbit(50);
    check_vec3(glm::normalize(camera.GetPosition()), glm::vec3(0.0f, 1.0f, 0.0f), 0.001f);
    check_camera_frame(camera);

    const glm::vec3 position_before_yaw = camera.GetPosition();
    camera.Orbit(glm::vec2(30.0f, 0.0f));
    camera.UpdateOrbit(50);
    check_camera_frame(camera);
    CHECK(glm::distance(position_before_yaw, camera.GetPosition()) > 0.1f);

    const glm::quat orientation = camera.GetOrientation();
    check_vec3(camera.GetRight(), orientation * glm::vec3(1.0f, 0.0f, 0.0f));
    check_vec3(camera.GetUp(), orientation * glm::vec3(0.0f, 1.0f, 0.0f));
}

TEST_CASE("SatView Vulkan conversion reflects the complete clip-space Y row", "[satview][camera][vulkan]")
{
    const glm::mat4 view = glm::lookAtRH(
        glm::vec3(2.3f, 1.7f, -3.9f),
        glm::vec3(0.0f),
        glm::normalize(glm::vec3(0.2f, 1.0f, 0.3f)));
    const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 16.0f / 9.0f, 0.05f, 200.0f);
    const glm::mat4 world_to_clip = projection * view;
    const glm::mat4 vulkan_world_to_clip = make_satview_vulkan_clip_matrix(world_to_clip);

    const glm::vec4 points[] = {
        glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
        glm::vec4(1.0f, -0.5f, 0.25f, 1.0f),
        glm::vec4(-2.0f, 0.75f, 1.5f, 1.0f),
    };
    for (const glm::vec4& point : points)
    {
        const glm::vec4 clip = world_to_clip * point;
        const glm::vec4 vulkan_clip = vulkan_world_to_clip * point;
        CHECK(vulkan_clip.x == Approx(clip.x).margin(0.000001f));
        CHECK(vulkan_clip.y == Approx(-clip.y).margin(0.000001f));
        CHECK(vulkan_clip.z == Approx(clip.z).margin(0.000001f));
        CHECK(vulkan_clip.w == Approx(clip.w).margin(0.000001f));
    }
}
