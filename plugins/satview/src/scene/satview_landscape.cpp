#include <draxul/satview/satview_landscape.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <numbers>
#include <span>

namespace draxul::satview
{

namespace
{

constexpr glm::vec4 kFillColor{ 0.006f, 0.008f, 0.012f, 0.98f };
constexpr glm::vec4 kRimColor{ 0.09f, 0.12f, 0.16f, 0.62f };

struct AngularPoint
{
    float azimuth_degrees = 0.0f;
    float altitude_degrees = 0.0f;
};

glm::vec4 landscape_direction(AngularPoint point, float horizon_altitude_degrees)
{
    return glm::vec4(satview_local_azimuth_altitude_direction(
                         glm::radians(point.azimuth_degrees),
                         glm::radians(point.altitude_degrees + horizon_altitude_degrees)),
        1.0f);
}

void add_polygon(
    SatViewLandscapeMesh& mesh,
    std::span<const AngularPoint> points,
    float horizon_altitude_degrees)
{
    if (points.size() < 3)
        return;
    for (size_t index = 1; index + 1 < points.size(); ++index)
    {
        mesh.fill_triangles.push_back({
            landscape_direction(points[0], horizon_altitude_degrees),
            landscape_direction(points[index], horizon_altitude_degrees),
            landscape_direction(points[index + 1], horizon_altitude_degrees),
            kFillColor,
        });
    }
    for (size_t index = 0; index < points.size(); ++index)
    {
        mesh.rim_lines.push_back({
            landscape_direction(points[index], horizon_altitude_degrees),
            landscape_direction(points[(index + 1) % points.size()], horizon_altitude_degrees),
            kRimColor,
        });
    }
}

void add_rectangle(
    SatViewLandscapeMesh& mesh,
    float center_azimuth,
    float width,
    float bottom,
    float top,
    float horizon_altitude_degrees)
{
    const std::array points = {
        AngularPoint{ center_azimuth - width * 0.5f, bottom },
        AngularPoint{ center_azimuth + width * 0.5f, bottom },
        AngularPoint{ center_azimuth + width * 0.5f, top },
        AngularPoint{ center_azimuth - width * 0.5f, top },
    };
    add_polygon(mesh, points, horizon_altitude_degrees);
}

void add_dome(
    SatViewLandscapeMesh& mesh,
    float center_azimuth,
    float horizon_altitude_degrees)
{
    std::vector<AngularPoint> points;
    points.push_back({ center_azimuth - 3.0f, -0.05f });
    points.push_back({ center_azimuth + 3.0f, -0.05f });
    points.push_back({ center_azimuth + 3.0f, 1.6f });
    for (int step = 0; step <= 12; ++step)
    {
        const float angle = static_cast<float>(step) * std::numbers::pi_v<float> / 12.0f;
        points.push_back({
            center_azimuth + 3.0f * std::cos(angle),
            1.6f + 2.1f * std::sin(angle),
        });
    }
    points.push_back({ center_azimuth - 3.0f, 1.6f });
    add_polygon(mesh, points, horizon_altitude_degrees);
}

void add_radio_dish(
    SatViewLandscapeMesh& mesh,
    float center_azimuth,
    bool tilted_left,
    float horizon_altitude_degrees)
{
    add_rectangle(mesh, center_azimuth, 0.55f, -0.05f, 2.0f, horizon_altitude_degrees);
    const float tilt = tilted_left ? -0.55f : 0.55f;
    const std::array support = {
        AngularPoint{ center_azimuth - 0.18f, 1.65f },
        AngularPoint{ center_azimuth + 0.18f, 1.65f },
        AngularPoint{ center_azimuth + tilt + 0.18f, 3.0f },
        AngularPoint{ center_azimuth + tilt - 0.18f, 3.0f },
    };
    add_polygon(mesh, support, horizon_altitude_degrees);

    std::vector<AngularPoint> dish;
    for (int step = 0; step < 16; ++step)
    {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(step) / 16.0f;
        dish.push_back({
            center_azimuth + tilt + 2.1f * std::cos(angle),
            3.25f + 0.72f * std::sin(angle),
        });
    }
    add_polygon(mesh, dish, horizon_altitude_degrees);
}

void add_telescope(
    SatViewLandscapeMesh& mesh,
    float center_azimuth,
    float horizon_altitude_degrees)
{
    add_rectangle(mesh, center_azimuth, 0.8f, -0.05f, 2.4f, horizon_altitude_degrees);
    const std::array barrel = {
        AngularPoint{ center_azimuth - 1.8f, 3.3f },
        AngularPoint{ center_azimuth - 1.45f, 3.8f },
        AngularPoint{ center_azimuth + 2.0f, 2.0f },
        AngularPoint{ center_azimuth + 1.6f, 1.5f },
    };
    add_polygon(mesh, barrel, horizon_altitude_degrees);
}

} // namespace

glm::vec3 satview_local_azimuth_altitude_direction(
    float azimuth_radians,
    float altitude_radians)
{
    const float horizontal = std::cos(altitude_radians);
    return glm::normalize(glm::vec3(
        horizontal * std::sin(azimuth_radians),
        horizontal * std::cos(azimuth_radians),
        std::sin(altitude_radians)));
}

SatViewLandscapeMesh build_satview_observatory_landscape(
    float observer_altitude_earth_radii)
{
    SatViewLandscapeMesh mesh;
    mesh.fill_triangles.reserve(96);
    mesh.rim_lines.reserve(128);

    const float observer_radius = 1.0f + std::max(observer_altitude_earth_radii, 0.0f);
    const float horizon_altitude_degrees = -glm::degrees(std::acos(1.0f / observer_radius));

    add_dome(mesh, 22.0f, horizon_altitude_degrees);
    add_radio_dish(mesh, 92.0f, true, horizon_altitude_degrees);
    add_telescope(mesh, 158.0f, horizon_altitude_degrees);
    add_rectangle(mesh, 212.0f, 5.5f, -0.05f, 2.2f, horizon_altitude_degrees);
    add_radio_dish(mesh, 248.0f, false, horizon_altitude_degrees);
    add_rectangle(mesh, 306.0f, 0.35f, -0.05f, 7.0f, horizon_altitude_degrees);
    return mesh;
}

} // namespace draxul::satview
