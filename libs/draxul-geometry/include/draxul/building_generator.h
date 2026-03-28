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

// Generate a flat annular ring mesh (for sidewalks that conform to building outlines).
// The ring is centered at the origin in XZ, at the given Y elevation, with inner_radius
// and outer_radius defining the ring width. The polygon has `sides` edges.
[[nodiscard]] GeometryMesh generate_sidewalk_ring(
    int sides, float inner_radius, float outer_radius, float y, float height,
    const glm::vec3& color);

} // namespace draxul
