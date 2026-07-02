#pragma once

#include <draxul/codeviz_scene_types.h>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <utility>

namespace draxul
{

// --- Core spatial components ---

// World-space XZ position on the ground plane.
struct WorldPosition
{
    float x = 0.0f;
    float z = 0.0f;
};

// Vertical offset above the ground plane.
struct Elevation
{
    float value = 0.0f;
};

// --- Appearance ---

struct Appearance
{
    MeshId mesh = MeshId::Cube;
    CodeVizMaterialPreset material = CodeVizMaterialPreset::FlatColor;
    bool double_sided = false;
    glm::vec4 color{ 1.0f };
    glm::vec4 material_info{ 0.0f, 1.0f, 1.0f, 1.0f };
};

// --- Presentation shape components ---

// Generic vertical block. The city metaphor maps these to buildings.
struct BlockMetrics
{
    float footprint = 1.0f; // XZ extent in tile units
    float height = 1.0f; // Y extent in world units
    float sidewalk_width = 0.0f;
    float road_width = 0.0f;
};

struct StripMetrics
{
    float extent_x = 1.0f;
    float extent_z = 1.0f;
    float height = 0.02f;
};

struct TexturedSurfaceMetrics
{
    float extent_x = 1.0f;
    float extent_z = 1.0f;
    float height = 0.03f;
    float uv_scale = 0.35f;
    float normal_strength = 1.0f;
    float ao_strength = 1.0f;
};

struct LinkSegmentMetrics
{
    float extent_x = 1.0f;
    float extent_z = 1.0f;
    float height = 0.04f;
    float yaw_radians = 0.0f;
    float pitch_radians = 0.0f; // tilt along the length axis (for sloped routes)
};

struct RegionSurfaceMetrics
{
    float extent_x = 1.0f;
    float extent_z = 1.0f;
    float height = 0.02f;
};

struct EllipsoidMetrics
{
    float radius_x = 0.5f;
    float radius_y = 0.5f;
    float radius_z = 0.5f;
};

struct LabelPanelMetrics
{
    float width = 1.0f;
    float height = 0.05f;
    float depth = 0.42f;
    float yaw_radians = 0.0f;
    glm::vec4 uv_rect{ 0.0f, 0.0f, 1.0f, 1.0f };
    glm::vec2 label_ink_pixel_size{ 0.0f };
};

struct FoliageMetrics
{
    float height = 1.0f;
    float canopy_radius = 0.5f;
};

// Optional link back to the source symbol that generated this entity.
struct CodeVizSemanticRef
{
    std::string file;
    std::string name;
    std::string module_path;
    uint64_t semantic_node_id = 0;
    uint64_t semantic_edge_id = 0;
};

enum class CustomMeshTransformMode : uint8_t
{
    Baked,
    ScaleByBlockMetrics,
    ScaleByLabelMetrics,
};

struct CustomMeshRef
{
    CustomMeshRef() = default;
    explicit CustomMeshRef(
        std::shared_ptr<const GeometryMesh> mesh_value,
        CustomMeshTransformMode transform_mode_value = CustomMeshTransformMode::Baked)
        : mesh(std::move(mesh_value))
        , transform_mode(transform_mode_value)
    {
    }

    std::shared_ptr<const GeometryMesh> mesh;
    CustomMeshTransformMode transform_mode = CustomMeshTransformMode::Baked;
};

// Links a relationship segment entity to the code symbols it connects.
struct RelationshipLink
{
    std::string source_file_path;
    std::string source_module_path;
    std::string source_qualified_name;
    std::string target_file_path;
    std::string target_module_path;
    std::string target_qualified_name;
};

// City-metaphor compatibility names. Keep these in the city layer over time,
// but aliases avoid a noisy semantic-city rewrite while the shared scene layer
// becomes metaphor-neutral.
using BuildingMetrics = BlockMetrics;
using RoadMetrics = StripMetrics;
using RoadSurfaceMetrics = TexturedSurfaceMetrics;
using RouteSegmentMetrics = LinkSegmentMetrics;
using ModuleSurfaceMetrics = RegionSurfaceMetrics;
using SignMetrics = LabelPanelMetrics;
using TreeMetrics = FoliageMetrics;
using RouteLink = RelationshipLink;

} // namespace draxul
