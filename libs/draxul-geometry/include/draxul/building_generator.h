#pragma once

#include <draxul/geometry_mesh.h>

#include <glm/vec3.hpp>

#include <vector>

namespace draxul
{

struct DraxulBuildingLevel
{
    float height = 1.0f;
    glm::vec3 color{ 1.0f };
};

struct DraxulBuildingParams
{
    float footprint = 1.0f;
    int sides = 4;
    float middle_strip_scale = 1.0f;
    std::vector<DraxulBuildingLevel> levels;
};

[[nodiscard]] GeometryMesh generate_draxul_building(const DraxulBuildingParams& params);

} // namespace draxul
