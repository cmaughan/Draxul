#pragma once

#include <draxul/geometry_mesh.h>

#include <glm/vec3.hpp>

namespace draxul
{

struct DraxulRoofSignParams
{
    int sides = 4;
    float inner_radius = 0.5f;
    float band_depth = 0.08f;
    float height = 0.25f;
    glm::vec3 color{ 1.0f };
};

// Generate a vertical polygonal sign ring centered at the origin.
// The ring repeats the same label UVs on each outward-facing wall segment.
[[nodiscard]] GeometryMesh generate_draxul_roof_sign(const DraxulRoofSignParams& params);

} // namespace draxul
