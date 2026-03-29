#pragma once

#include <cstdint>
#include <draxul/geometry_mesh.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace draxul
{

using SceneVertex = GeometryVertex;
using MeshData = GeometryMesh;

enum class MeshId : uint32_t
{
    Grid,
    Floor,
    Cube,
    TreeBark,
    TreeLeaves,
    RoadSurface,
    RoofSign,
    WallSign,
    Custom,
};

enum class MaterialId : uint32_t
{
    FlatColor = 0,
    AsphaltRoad = 1,
    PavingSidewalk = 2,
    WoodBuilding = 3,
    LeafCards = 4,
    TreeBark = 5,
};

enum class MaterialShadingModel : uint32_t
{
    FlatColor = 0,
    TexturedTintedPbr = 1,
    VertexTintPbr = 2,
    LeafCutoutPbr = 3,
};

enum class SceneTextureId : uint32_t
{
    FallbackAlbedoSrgb = 0,
    FallbackScalar = 1,
    FallbackNormal = 2,
    AsphaltAlbedo = 3,
    AsphaltNormal = 4,
    AsphaltRoughness = 5,
    AsphaltAo = 6,
    SidewalkAlbedo = 7,
    SidewalkNormal = 8,
    SidewalkRoughness = 9,
    SidewalkAo = 10,
    WoodAlbedo = 11,
    WoodNormal = 12,
    WoodRoughness = 13,
    WoodMetalness = 14,
    WoodAo = 15,
    LeafAlbedo = 16,
    LeafNormal = 17,
    LeafRoughness = 18,
    LeafOpacity = 19,
    LeafScattering = 20,
    BarkAlbedo = 21,
    BarkNormal = 22,
    BarkRoughness = 23,
    BarkAo = 24,
};

constexpr uint32_t kSceneMaterialTextureCount = 25;
constexpr uint32_t kMaxSceneMaterials = 64;
constexpr uint32_t kShadowCascadeCount = 3;
constexpr uint32_t kPointShadowFaceCount = 6;

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
    enum class Role : uint32_t
    {
        None = 0,
        ModuleOutline = 1,
        ModulePark = 2,
        ModuleLabel = 3,
    };

    MeshId mesh = MeshId::Cube;
    uint32_t custom_mesh_index = UINT32_MAX;
    uint32_t material_index = 0;
    bool double_sided = false;
    Role role = Role::None;
    glm::mat4 world{ 1.0f };
    glm::vec4 color{ 1.0f };
    glm::vec4 uv_rect{ 0.0f, 0.0f, 1.0f, 1.0f };
    glm::vec2 label_ink_pixel_size{ 0.0f };
    uint32_t performance_heat_offset = 0;
    uint32_t performance_heat_count = 0;

    // Identity: links this object back to its ECS source for runtime queries (e.g. selection).
    std::string source_name;
    std::string source_module_path;
    std::string source_file_path;
    std::string route_source_file_path;
    std::string route_source_module_path;
    std::string route_source;
    std::string route_target_file_path;
    std::string route_target_module_path;
    std::string route_target;
};

struct SceneMaterial
{
    MaterialShadingModel shading_model = MaterialShadingModel::FlatColor;
    glm::vec4 scalar_params{ 1.0f, 1.0f, 1.0f, 0.0f }; // x = material-specific primary scalar, y = normal strength, z = material-specific secondary scalar, w = metallic
    glm::uvec4 texture_indices{
        static_cast<uint32_t>(SceneTextureId::FallbackAlbedoSrgb),
        static_cast<uint32_t>(SceneTextureId::FallbackNormal),
        static_cast<uint32_t>(SceneTextureId::FallbackScalar),
        static_cast<uint32_t>(SceneTextureId::FallbackScalar),
    };
    glm::uvec4 metadata{ 0u };
};

struct SceneCameraData
{
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f };
    glm::mat4 inv_view_proj{ 1.0f };
    glm::vec4 camera_pos{ 0.0f, 8.0f, 0.0f, 1.0f };
    glm::vec4 light_dir{ -0.5f, -1.0f, -0.3f, 0.0f };
    glm::vec4 point_light_pos{ 4.0f, 6.0f, 4.0f, 12.0f }; // xyz = position, w = radius
    glm::vec4 label_fade_px{ 1.5f, 8.0f, 0.0f, 0.68f }; // x/y = label fade range, z = perf mode enabled, w = perf blend
    glm::vec4 render_tuning{ 1.0f, 1.0f, 0.45f, 4.0f }; // x = tone map exposure, y = point brightness, z = ambient, w = tone map white point
    glm::vec4 perf_tuning{ 0.0f, 0.0f, 0.0f, 0.0f }; // x = perf log scale
    glm::vec4 ao_settings{ 1.6f, 0.12f, 1.35f, 0.0f }; // x = radius (world units), y = bias, z = power
    glm::vec4 debug_view{ 0.0f, 1.0f, 16.0f, 0.0f }; // x = AO debug mode, y = AO denoise enabled, z = AO kernel size
    glm::vec4 world_debug_bounds{ -5.0f, 5.0f, -5.0f, 5.0f }; // x = min x, y = max x, z = min z, w = max z
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

struct TooltipOverlay
{
    bool visible = false;
    glm::vec2 screen_pos{ 0.0f }; // pixel position (top-left of tooltip)
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
    uint64_t revision = 0;

    [[nodiscard]] bool valid() const
    {
        return visible && width > 0 && height > 0
            && rgba.size() == static_cast<size_t>(width * height * 4);
    }
};

struct SceneSnapshot
{
    SceneCameraData camera;
    FloorGridSpec floor_grid;
    std::shared_ptr<const LabelAtlasData> label_atlas;
    std::shared_ptr<const MeshData> tree_bark_mesh;
    std::shared_ptr<const MeshData> tree_leaf_mesh;
    std::vector<std::shared_ptr<const MeshData>> custom_meshes;
    std::vector<float> performance_heat_values;
    std::vector<SceneMaterial> materials;
    std::vector<SceneObject> objects;
    uint32_t opaque_count = 0;
    TooltipOverlay tooltip;
};

} // namespace draxul
