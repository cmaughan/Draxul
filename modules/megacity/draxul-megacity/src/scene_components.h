#pragma once

#include "isometric_scene_types.h"
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
    MaterialId material = MaterialId::FlatColor;
    bool double_sided = false;
    glm::vec4 color{ 1.0f };
    glm::vec4 material_info{ 0.0f, 1.0f, 1.0f, 1.0f };
};

// --- Entity-type markers (tag components) ---

// Code-analysis metrics for buildings (concrete classes).
struct BuildingMetrics
{
    float footprint = 1.0f; // XZ extent in tile units
    float height = 1.0f; // Y extent in world units
    float sidewalk_width = 0.0f;
    float road_width = 0.0f;
};

struct RoadMetrics
{
    float extent_x = 1.0f;
    float extent_z = 1.0f;
    float height = 0.02f;
};

struct RoadSurfaceMetrics
{
    float extent_x = 1.0f;
    float extent_z = 1.0f;
    float height = 0.03f;
    float uv_scale = 0.35f;
    float normal_strength = 1.0f;
    float ao_strength = 1.0f;
};

struct RouteSegmentMetrics
{
    float extent_x = 1.0f;
    float extent_z = 1.0f;
    float height = 0.04f;
    float yaw_radians = 0.0f;
    float pitch_radians = 0.0f; // tilt along the length axis (for sloped routes)
};

struct ModuleSurfaceMetrics
{
    float extent_x = 1.0f;
    float extent_z = 1.0f;
    float height = 0.02f;
};

struct BiologyEllipsoidMetrics
{
    float radius_x = 0.5f;
    float radius_y = 0.5f;
    float radius_z = 0.5f;
};

struct SignMetrics
{
    float width = 1.0f;
    float height = 0.05f;
    float depth = 0.42f;
    float yaw_radians = 0.0f;
    glm::vec4 uv_rect{ 0.0f, 0.0f, 1.0f, 1.0f };
    glm::vec2 label_ink_pixel_size{ 0.0f };
};

// Code-analysis metrics for trees (free functions).
struct TreeMetrics
{
    float height = 1.0f;
    float canopy_radius = 0.5f;
};

// Optional link back to the source symbol that generated this entity.
struct SourceSymbol
{
    std::string file;
    std::string name;
    std::string module_path;
};

enum class CustomMeshTransformMode : uint8_t
{
    Baked,
    ScaleByBuildingMetrics,
    ScaleBySignMetrics,
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

// Links a route segment entity to the buildings it connects.
struct RouteLink
{
    std::string source_file_path;
    std::string source_module_path;
    std::string source_qualified_name;
    std::string target_file_path;
    std::string target_module_path;
    std::string target_qualified_name;
};

} // namespace draxul
