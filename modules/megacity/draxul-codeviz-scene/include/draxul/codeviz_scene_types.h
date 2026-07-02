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

enum class CodeVizProjectionMode : uint8_t
{
    Orthographic,
    Perspective,
};

enum class CodeVizMeshId : uint32_t
{
    Grid,
    Floor,
    Cube,
    FoliageStem,
    FoliageCards,
    TexturedSurface,
    TopLabelPanel,
    FrontLabelPanel,
    Custom,
};

enum class CodeVizMaterialPreset : uint32_t
{
    FlatColor = 0,
    TexturedPbr0 = 1,
    TexturedPbr1 = 2,
    VertexTintPbr0 = 3,
    AlphaMaskedPbr0 = 4,
    TexturedPbr2 = 5,
};

enum class CodeVizShadingModel : uint32_t
{
    FlatColor = 0,
    TexturedPbr = 1,
    VertexTintPbr = 2,
    AlphaMaskedPbr = 3,
};

enum class CodeVizTextureId : uint32_t
{
    FallbackBaseColorSrgb = 0,
    FallbackScalar = 1,
    FallbackNormal = 2,
    Material0BaseColor = 3,
    Material0Normal = 4,
    Material0Roughness = 5,
    Material0AmbientOcclusion = 6,
    Material1BaseColor = 7,
    Material1Normal = 8,
    Material1Roughness = 9,
    Material1AmbientOcclusion = 10,
    Material2BaseColor = 11,
    Material2Normal = 12,
    Material2Roughness = 13,
    Material2Metallic = 14,
    Material2AmbientOcclusion = 15,
    Material4BaseColor = 16,
    Material4Normal = 17,
    Material4Roughness = 18,
    Material4Opacity = 19,
    Material4Scattering = 20,
    Material3BaseColor = 21,
    Material3Normal = 22,
    Material3Roughness = 23,
    Material3AmbientOcclusion = 24,
};

constexpr uint32_t kCodeVizMaterialTextureCount = 25;
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
            && rgba.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    }
};

struct CodeVizRenderable
{
    enum class Role : uint32_t
    {
        None = 0,
        ModuleOutline = 1,
        ModulePark = 2,
        ModuleLabel = 3,
    };

    CodeVizMeshId mesh = CodeVizMeshId::Cube;
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
    uint64_t semantic_node_id = 0;
    uint64_t semantic_edge_id = 0;
    std::string route_source_file_path;
    std::string route_source_module_path;
    std::string route_source;
    std::string route_target_file_path;
    std::string route_target_module_path;
    std::string route_target;
};

struct CodeVizMaterial
{
    CodeVizShadingModel shading_model = CodeVizShadingModel::FlatColor;
    glm::vec4 scalar_params{ 1.0f, 1.0f, 1.0f, 0.0f }; // x = material-specific primary scalar, y = normal strength, z = material-specific secondary scalar, w = metallic
    glm::uvec4 texture_indices{
        static_cast<uint32_t>(CodeVizTextureId::FallbackBaseColorSrgb),
        static_cast<uint32_t>(CodeVizTextureId::FallbackNormal),
        static_cast<uint32_t>(CodeVizTextureId::FallbackScalar),
        static_cast<uint32_t>(CodeVizTextureId::FallbackScalar),
    };
    glm::uvec4 metadata{ 0u };
};

struct CodeVizCameraData
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
    float scene_far_ndc = 1.0f; // max scene depth in NDC [0,1] — used to tighten shadow cascades
    float zoom_half_height = 4.0f; // effective view half-height at the focus depth
    CodeVizProjectionMode projection_mode = CodeVizProjectionMode::Orthographic;
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
            && rgba.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    }
};

struct CodeVizSceneSnapshot
{
    CodeVizCameraData camera;
    FloorGridSpec floor_grid;
    std::shared_ptr<const LabelAtlasData> label_atlas;
    std::shared_ptr<const MeshData> foliage_stem_mesh;
    std::shared_ptr<const MeshData> foliage_card_mesh;
    std::vector<std::shared_ptr<const MeshData>> custom_meshes;
    std::vector<float> performance_heat_values;
    std::vector<CodeVizMaterial> materials;
    std::vector<CodeVizRenderable> objects;
    uint32_t opaque_count = 0;
    TooltipOverlay tooltip;
};

} // namespace draxul
