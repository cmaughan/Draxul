#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace draxul
{

enum class MeshId : uint32_t
{
    Grid,
    Cube,
};

struct SceneObject
{
    MeshId mesh = MeshId::Cube;
    glm::mat4 world{ 1.0f };
    glm::vec4 color{ 1.0f };
};

struct SceneCameraData
{
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f };
    glm::vec4 light_dir{ -0.5f, -1.0f, -0.3f, 0.0f };
};

struct SceneSnapshot
{
    SceneCameraData camera;
    std::vector<SceneObject> objects;
};

struct SceneVertex
{
    glm::vec3 position{ 0.0f };
    glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
    glm::vec3 color{ 1.0f };
};

struct MeshData
{
    std::vector<SceneVertex> vertices;
    std::vector<uint16_t> indices;
};

} // namespace draxul
