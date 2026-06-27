#include "camera.h"
#include "camera_manipulator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/ext/matrix_transform.hpp>

using Catch::Approx;
using draxul::satview::Camera;
using draxul::satview::Manipulator;

namespace
{

void check_vec3(const glm::vec3& actual, const glm::vec3& expected, float margin = 0.0001f)
{
    CHECK(actual.x == Approx(expected.x).margin(margin));
    CHECK(actual.y == Approx(expected.y).margin(margin));
    CHECK(actual.z == Approx(expected.z).margin(margin));
}

} // namespace

TEST_CASE("SatView EasyRender camera builds an orthonormal look-at frame", "[satview][camera]")
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

TEST_CASE("SatView EasyRender camera preserves its orbit convention", "[satview][camera]")
{
    Camera camera;
    camera.SetPositionAndFocalPoint(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f));
    camera.Orbit(glm::vec2(90.0f, 0.0f));
    camera.UpdateOrbit(50);

    check_vec3(camera.GetPosition(), glm::vec3(-4.0f, 0.0f, 0.0f), 0.001f);
    check_vec3(camera.GetViewDirection(), glm::vec3(1.0f, 0.0f, 0.0f), 0.001f);
    CHECK(glm::dot(camera.GetViewDirection(), camera.GetUp()) == Approx(0.0f).margin(0.0001f));
}

TEST_CASE("SatView EasyRender manipulator maps mouse orbit and dolly deltas", "[satview][camera]")
{
    auto manipulated_camera = std::make_shared<Camera>();
    manipulated_camera->SetPositionAndFocalPoint(glm::vec3(0.0f, 0.0f, 8.0f), glm::vec3(0.0f));
    Manipulator manipulator(manipulated_camera);

    Camera direct_camera;
    direct_camera.SetPositionAndFocalPoint(glm::vec3(0.0f, 0.0f, 8.0f), glm::vec3(0.0f));

    manipulator.MouseDown(glm::vec2(10.0f, 20.0f));
    REQUIRE(manipulator.MouseMove(glm::vec2(30.0f, 10.0f), false));
    manipulated_camera->UpdateOrbit(50);
    direct_camera.Orbit(glm::vec2(10.0f, 5.0f));
    direct_camera.UpdateOrbit(50);
    check_vec3(manipulated_camera->GetPosition(), direct_camera.GetPosition(), 0.001f);

    manipulator.MouseDown(glm::vec2(30.0f, 10.0f));
    REQUIRE(manipulator.MouseMove(glm::vec2(30.0f, 6.0f), true));
    manipulated_camera->UpdatePosition(50);
    CHECK(manipulated_camera->GetDistance() == Approx(7.0f).margin(0.001f));
}

TEST_CASE("SatView EasyRender camera keeps dolly inside scene bounds", "[satview][camera]")
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

TEST_CASE("SatView EasyRender camera stays upright and level across the poles", "[satview][camera]")
{
    Camera camera;
    camera.SetPositionAndFocalPoint(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f));

    // Drive the pitch well past the north pole. The elevation clamp must keep the
    // camera upright (no inversion) and the horizon level (no roll).
    for (int i = 0; i < 12; ++i)
    {
        camera.Orbit(glm::vec2(0.0f, 30.0f));
        camera.UpdateOrbit(50);
    }
    CHECK(camera.GetUp().y > 0.0f);
    CHECK(camera.GetRight().y == Approx(0.0f).margin(0.001f));
    CHECK(glm::length(camera.GetViewDirection()) == Approx(1.0f));
    CHECK(glm::dot(camera.GetViewDirection(), camera.GetUp()) == Approx(0.0f).margin(0.0001f));

    // ...and well past the south pole.
    for (int i = 0; i < 24; ++i)
    {
        camera.Orbit(glm::vec2(0.0f, -30.0f));
        camera.UpdateOrbit(50);
    }
    CHECK(camera.GetUp().y > 0.0f);
    CHECK(camera.GetRight().y == Approx(0.0f).margin(0.001f));
    CHECK(glm::length(camera.GetViewDirection()) == Approx(1.0f));
}

TEST_CASE("SatView EasyRender camera keeps the horizon level after a yawed start", "[satview][camera]")
{
    // A start that is both yawed and pitched used to bake a roll into the frame
    // (shortest-arc quaternion). The turntable frame must be level from frame one.
    Camera camera;
    camera.SetPositionAndFocalPoint(glm::vec3(2.0f, 1.0f, 4.0f), glm::vec3(0.0f));
    CHECK(camera.GetRight().y == Approx(0.0f).margin(0.0001f));

    camera.Orbit(glm::vec2(37.0f, 21.0f));
    camera.UpdateOrbit(50);
    CHECK(camera.GetRight().y == Approx(0.0f).margin(0.0001f));
    CHECK(camera.GetUp().y > 0.0f);
}
