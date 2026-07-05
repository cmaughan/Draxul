#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>

namespace draxul::satview
{

struct SatViewLandscapeTriangleInstance
{
    glm::vec4 local_direction0{ 0.0f, 0.0f, 1.0f, 1.0f };
    glm::vec4 local_direction1{ 0.0f, 0.0f, 1.0f, 1.0f };
    glm::vec4 local_direction2{ 0.0f, 0.0f, 1.0f, 1.0f };
    glm::vec4 color{ 1.0f };
};

struct SatViewLandscapeLineInstance
{
    glm::vec4 local_direction0{ 0.0f, 0.0f, 1.0f, 1.0f };
    glm::vec4 local_direction1{ 0.0f, 0.0f, 1.0f, 1.0f };
    glm::vec4 color{ 1.0f };
};

struct SatViewLandscapeMesh
{
    std::vector<SatViewLandscapeTriangleInstance> fill_triangles;
    std::vector<SatViewLandscapeLineInstance> rim_lines;
};

[[nodiscard]] glm::vec3 satview_local_azimuth_altitude_direction(
    float azimuth_radians,
    float altitude_radians);
[[nodiscard]] SatViewLandscapeMesh build_satview_observatory_landscape(
    float observer_altitude_earth_radii);

} // namespace draxul::satview
