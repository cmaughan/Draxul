#pragma once

#include <draxul/geometry_mesh.h>

#include "semantic_city_layout.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace draxul
{

struct MegaCityCodeConfig;
struct SemanticMegacityModel;
struct SemanticMegacityLayout;
struct SignLabelAtlas;
class CityDatabase;
class SceneWorld;
class TextService;

struct CityBuildResult
{
    std::shared_ptr<const SemanticMegacityModel> semantic_model;
    std::shared_ptr<SignLabelAtlas> sign_label_atlas;
    std::unique_ptr<SemanticMegacityLayout> layout;
    bool city_bounds_valid = false;
    float min_x = 0.0f;
    float max_x = 0.0f;
    float min_z = 0.0f;
    float max_z = 0.0f;
    bool computed_default_light = false;
    float default_light_x = 0.0f;
    float default_light_y = 0.0f;
    float default_light_z = 0.0f;
    float default_light_radius = 0.0f;
    std::shared_ptr<const GeometryMesh> tree_bark_mesh;
    std::shared_ptr<const GeometryMesh> tree_leaf_mesh;
};

[[nodiscard]] int procedural_building_side_count(
    int incident_connection_count,
    int connected_hex_building_threshold,
    int connected_oct_building_threshold);

void emit_route_entities(
    SceneWorld& world,
    const std::vector<CityGrid::RoutePolyline>& routes,
    const MegaCityCodeConfig& config);

// Build (or rebuild) the semantic city into the given SceneWorld.
// Clears the world, queries the city DB, lays out modules, creates all ECS
// entities (buildings, parks, signs, sidewalks, road surfaces).
//
// The returned CityBuildResult owns the layout (for use by launch_grid_build).
CityBuildResult build_city(
    SceneWorld& world,
    CityDatabase& city_db,
    TextService* text_service,
    const std::vector<std::string>& available_modules,
    const MegaCityCodeConfig& config,
    uint64_t& sign_label_revision);

} // namespace draxul
