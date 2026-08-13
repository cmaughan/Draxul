#pragma once

#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <vector>

namespace draxul
{

struct GeometryVertex
{
    glm::vec3 position{ 0.0f };
    glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
    glm::vec3 color{ 1.0f };
    glm::vec2 uv{ 0.0f };
    float tex_blend = 0.0f;
    glm::vec4 tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
    float layer_id = 0.0f;
};

struct GeometryMesh
{
    std::vector<GeometryVertex> vertices;
    std::vector<uint16_t> indices;
};

} // namespace draxul
