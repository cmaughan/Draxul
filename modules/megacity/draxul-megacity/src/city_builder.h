#pragma once

#include <draxul/code_semantic_model.h>
#include <draxul/geometry_mesh.h>

#include "semantic_city_layout.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace draxul
{

struct MegaCityCodeConfig;
struct LiveCityMetricsSnapshot;
struct SemanticMegacityModel;
struct SemanticMegacityLayout;
struct SignLabelAtlas;
class SceneWorld;
class TextService;

struct CityBuildResult
{
    std::shared_ptr<const SemanticMegacityModel> semantic_model;
    std::shared_ptr<const LiveCityMetricsSnapshot> live_metrics;
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

struct SemanticCodeModelBuildResult
{
    std::shared_ptr<SemanticMegacityModel> semantic_model;
    std::shared_ptr<const LiveCityMetricsSnapshot> live_metrics;
};

[[nodiscard]] TreeMetrics tree_metrics_from_meshes(
    const GeometryMesh& bark_mesh, const GeometryMesh& leaf_mesh);

[[nodiscard]] int procedural_building_side_count(
    int incident_connection_count,
    int connected_hex_building_threshold,
    int connected_oct_building_threshold);

void emit_route_entities(
    SceneWorld& world,
    const std::vector<CityGrid::RoutePolyline>& routes,
    const MegaCityCodeConfig& config);

// Build the city presentation model from the neutral semantic code snapshot.
SemanticCodeModelBuildResult build_semantic_code_model(
    const CodeSemanticSnapshot& semantics,
    const MegaCityCodeConfig& config);

// Build (or rebuild) the semantic city into the given SceneWorld.
// Clears the world, projects the semantic snapshot into city records, lays out modules, creates all ECS
// entities (buildings, parks, signs, sidewalks, road surfaces).
//
// The returned CityBuildResult owns the layout (for use by launch_grid_build).
CityBuildResult build_city(
    SceneWorld& world,
    const CodeSemanticSnapshot& semantics,
    TextService* text_service,
    const MegaCityCodeConfig& config,
    uint64_t& sign_label_revision);

} // namespace draxul
