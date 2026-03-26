#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace draxul
{

enum class MeshId : uint32_t
{
    Grid,
    Floor,
    Cube,
    RoofSign,
    WallSign,
};

struct LabelAtlasData
{
    int width = 0;
    int height = 0;
    uint64_t revision = 0;
    std::vector<uint8_t> rgba;

    [[nodiscard]] bool valid() const
    {
        return width > 0 && height > 0
            && rgba.size() == static_cast<size_t>(width * height * 4);
    }
};

struct SceneObject
{
    MeshId mesh = MeshId::Cube;
    glm::mat4 world{ 1.0f };
    glm::vec4 color{ 1.0f };
    glm::vec4 uv_rect{ 0.0f, 0.0f, 1.0f, 1.0f };
    glm::vec2 label_ink_pixel_size{ 0.0f };
};

struct SceneCameraData
{
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f };
    glm::vec4 light_dir{ -0.5f, -1.0f, -0.3f, 0.0f };
    glm::vec4 point_light_pos{ 4.0f, 6.0f, 4.0f, 12.0f }; // xyz = position, w = radius
    glm::vec4 label_fade_px{ 1.5f, 8.0f, 0.0f, 0.0f };
    glm::vec4 render_tuning{ 1.0f, 1.0f, 0.45f, 0.0f }; // x = output gamma, y = point brightness, z = ambient
};

struct FloorGridSpec
{
    bool enabled = false;
    int min_x = 0;
    int max_x = 0;
    int min_z = 0;
    int max_z = 0;
    float tile_size = 1.0f;
    float line_width = 0.04f;
    float y = -0.001f;
    glm::vec4 color{ 0.45f, 0.45f, 0.48f, 1.0f };
};

struct SceneSnapshot
{
    SceneCameraData camera;
    FloorGridSpec floor_grid;
    std::shared_ptr<const LabelAtlasData> label_atlas;
    std::vector<SceneObject> objects;
};

struct SceneVertex
{
    glm::vec3 position{ 0.0f };
    glm::vec3 normal{ 0.0f, 1.0f, 0.0f };
    glm::vec3 color{ 1.0f };
    glm::vec2 uv{ 0.0f };
    float tex_blend = 0.0f;
};

struct MeshData
{
    std::vector<SceneVertex> vertices;
    std::vector<uint16_t> indices;
};

} // namespace draxul
