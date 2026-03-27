#pragma once

#include "isometric_scene_types.h"
#include <glm/glm.hpp>
#include <string>

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
    float uv_scale = 0.35f;
    float normal_strength = 1.0f;
    float ao_strength = 1.0f;
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
    float canopy_radius = 0.5f;
};

// Optional link back to the source symbol that generated this entity.
struct SourceSymbol
{
    std::string file;
    std::string name;
};

} // namespace draxul
