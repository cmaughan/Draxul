#include "city_builder.h"
#include "city_helpers.h"
#include "city_materials.h"
#include "city_meshes.h"
#include "live_city_metrics.h"
#include <draxul/codeviz_scene_world.h>
#include "semantic_city_layout.h"
#include "sign_label_atlas.h"
#include "static_mesh_family_cache.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <draxul/building_generator.h>
#include <draxul/log.h>
#include <draxul/megacity_code_config.h>
#include <draxul/perf_timing.h>
#include <draxul/roof_sign_generator.h>
#include <draxul/text_service.h>
#include <draxul/tree_generator.h>
#include <filesystem>
#include <glm/gtc/constants.hpp>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace draxul
{

namespace
{

constexpr glm::vec4 kSidewalkSurfaceColor(0.72f, 0.72f, 0.74f, 1.0f);
constexpr float kRoadMaterialUvScale = 0.28f;
constexpr float kModuleSurfaceHeight = 0.018f;
constexpr float kModuleSurfaceLift = 0.003f;
constexpr float kDependencyRouteWidthScale = 0.27f;
constexpr float kDependencyRouteMinWidth = 0.135f;
constexpr float kDependencyRouteHeight = 0.045f;
constexpr int kHexBuildingIncidentConnectionThreshold = 6;
constexpr float kPointShadowDebugSceneHalfExtent = 9.0f;
constexpr float kPointShadowDebugPrimaryFootprint = 2.5f;
constexpr float kPointShadowDebugPrimaryHeight = 4.5f;
constexpr float kPointShadowDebugSecondaryFootprint = 1.8f;
constexpr float kPointShadowDebugSecondaryHeight = 2.4f;
constexpr glm::vec2 kPointShadowDebugPrimaryCenter(0.0f, 0.0f);
constexpr glm::vec2 kPointShadowDebugSecondaryCenter(4.0f, -2.5f);
constexpr glm::vec2 kPointShadowDebugTreeCenter(-4.0f, 2.0f);

struct SignPlacementSpec
{
    glm::vec2 center{ 0.0f };
    float width = 1.0f;
    float height = 0.05f;
    float depth = 0.25f;
    float yaw_radians = 0.0f;
    CodeVizMeshId mesh = kCityWallSignMesh;
};

struct RoofSignPlacementSpec
{
    glm::vec2 center{ 0.0f };
    float outer_diameter = 1.0f;
    float inner_radius = 0.5f;
    float height = 0.25f;
    float band_depth = 0.08f;
    float yaw_radians = 0.0f;
    int sides = 4;
};

glm::vec4 color_with_alpha(const glm::vec3& color, float alpha = 1.0f)
{
    return glm::vec4(
        std::clamp(color.r, 0.0f, 1.0f),
        std::clamp(color.g, 0.0f, 1.0f),
        std::clamp(color.b, 0.0f, 1.0f),
        alpha);
}

glm::vec4 building_sign_board_color(const MegaCityCodeConfig& config)
{
    return color_with_alpha(config.building_sign_board_color);
}

glm::vec4 dark_module_sign_board_color(std::string_view module_path)
{
    const glm::vec4 module_color = module_building_color(module_path);
    const glm::vec3 darkened = glm::mix(glm::vec3(module_color), kCatppuccinSurface0, 0.45f);
    return glm::vec4(glm::clamp(darkened, glm::vec3(0.0f), glm::vec3(1.0f)), module_color.a);
}

uint8_t color_channel_to_byte(float value)
{
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

DraxulTreeParams make_central_park_tree_params(const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
    DraxulTreeParams params = make_tree_params_from_age(config.central_park_tree_age_years);
    params.seed = static_cast<uint64_t>(std::max(config.central_park_tree_seed, 0));
    params.overall_scale *= std::max(config.central_park_tree_overall_scale, 0.1f);
    params.radial_segments = std::max(config.central_park_tree_radial_segments, 3);
    params.max_branch_depth = std::max(config.central_park_tree_max_branch_depth, 0);
    params.child_branches_min = std::max(config.central_park_tree_child_branches_min, 0);
    params.child_branches_max = std::max(config.central_park_tree_child_branches_max, params.child_branches_min);
    params.branch_length_scale = std::clamp(config.central_park_tree_branch_length_scale, 0.1f, 1.0f);
    params.branch_radius_scale = std::clamp(config.central_park_tree_branch_radius_scale, 0.1f, 1.0f);
    params.upward_bias = config.central_park_tree_upward_bias;
    params.outward_bias = std::max(config.central_park_tree_outward_bias, 0.0f);
    params.curvature = std::clamp(config.central_park_tree_curvature, 0.0f, 1.0f);
    params.trunk_wander = std::clamp(config.central_park_tree_trunk_wander, 0.0f, 2.0f);
    params.branch_wander = std::clamp(config.central_park_tree_branch_wander, 0.0f, 2.0f);
    params.wander_frequency = std::clamp(config.central_park_tree_wander_frequency, 0.0f, 1.0f);
    params.wander_deviation = std::clamp(config.central_park_tree_wander_deviation, 0.0f, 2.0f);
    params.leaf_density = std::clamp(config.central_park_tree_leaf_density, 0.0f, 10.0f);
    params.leaf_orientation_randomness = std::clamp(
        config.central_park_tree_leaf_orientation_randomness,
        0.0f,
        1.0f);
    params.leaf_size_range = glm::clamp(
        config.central_park_tree_leaf_size_range,
        glm::vec2(0.1f),
        glm::vec2(12.0f));
    params.leaf_start_depth = std::max(config.central_park_tree_leaf_start_depth, 0);
    params.bark_color_noise = std::clamp(config.central_park_tree_bark_color_noise, 0.0f, 0.5f);
    params.bark_color_root = glm::clamp(config.central_park_tree_bark_root, glm::vec3(0.0f), glm::vec3(1.0f));
    params.bark_color_tip = glm::clamp(config.central_park_tree_bark_tip, glm::vec3(0.0f), glm::vec3(1.0f));
    return params;
}

} // namespace

namespace
{

TreeMetrics tree_metrics_from_mesh(const GeometryMesh& mesh)
{
    PERF_MEASURE();
    TreeMetrics metrics{};
    if (mesh.vertices.empty())
        return metrics;

    float min_y = std::numeric_limits<float>::max();
    float max_y = std::numeric_limits<float>::lowest();
    float max_radius_sq = 0.0f;
    for (const GeometryVertex& vertex : mesh.vertices)
    {
        min_y = std::min(min_y, vertex.position.y);
        max_y = std::max(max_y, vertex.position.y);
        const float radius_sq = vertex.position.x * vertex.position.x
            + vertex.position.z * vertex.position.z;
        max_radius_sq = std::max(max_radius_sq, radius_sq);
    }

    metrics.height = std::max(max_y - min_y, 0.5f);
    metrics.canopy_radius = std::max(std::sqrt(max_radius_sq), 0.25f);
    return metrics;
}

} // namespace

TreeMetrics tree_metrics_from_meshes(const GeometryMesh& bark_mesh, const GeometryMesh& leaf_mesh)
{
    PERF_MEASURE();
    TreeMetrics bark_metrics = tree_metrics_from_mesh(bark_mesh);
    if (!leaf_mesh.vertices.empty())
    {
        TreeMetrics leaf_metrics = tree_metrics_from_mesh(leaf_mesh);
        bark_metrics.height = std::max(bark_metrics.height, leaf_metrics.height);
        bark_metrics.canopy_radius = std::max(bark_metrics.canopy_radius, leaf_metrics.canopy_radius);
    }
    return bark_metrics;
}

namespace
{

float layer_color_multiplier(size_t layer_index, float darkening)
{
    return (layer_index % 2) == 0
        ? 1.0f
        : std::clamp(1.0f - darkening, 0.0f, 1.0f);
}

std::shared_ptr<const GeometryMesh> build_procedural_building_mesh(
    StaticMeshFamilyCache& cache,
    const SemanticCityBuilding& building,
    const MegaCityCodeConfig& config,
    int sides,
    float level_gap = 0.0f)
{
    PERF_MEASURE();
    StaticBuildingRingSpec spec;
    spec.sides = std::max(sides, 3);
    spec.middle_strip_scale = 1.0f + std::max(config.building_middle_strip_push, 0.0f);

    if (building.layers.empty())
    {
        spec.layers.push_back({ 1.0f, 1.0f, 0u });
    }
    else
    {
        size_t positive_layer_count = 0;
        float total_height = 0.0f;
        for (const SemanticBuildingLayer& layer : building.layers)
        {
            if (layer.height <= 0.0f)
                continue;
            ++positive_layer_count;
            total_height += layer.height;
        }
        if (positive_layer_count > 1 && level_gap > 0.0f)
            total_height += level_gap * static_cast<float>(positive_layer_count - 1);
        total_height = std::max(total_height, 1e-4f);
        spec.level_gap_fraction = positive_layer_count > 1 ? std::max(level_gap, 0.0f) / total_height : 0.0f;
        spec.layers.reserve(positive_layer_count);
        for (size_t layer_index = 0; layer_index < building.layers.size(); ++layer_index)
        {
            const SemanticBuildingLayer& layer = building.layers[layer_index];
            if (layer.height <= 0.0f)
                continue;
            spec.layers.push_back({
                layer.height / total_height,
                layer_color_multiplier(layer_index, config.building_alternate_darkening),
                static_cast<uint32_t>(layer_index),
            });
        }
    }

    if (spec.layers.empty())
        spec.layers.push_back({ 1.0f, 1.0f, 0u });

    return cache.building_ring(spec);
}

std::shared_ptr<const GeometryMesh> build_procedural_brick_building_mesh(
    StaticMeshFamilyCache& cache,
    const SemanticCityBuilding& building,
    const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
    const int grid_size = std::max(config.struct_brick_grid_size, 1);
    const int bricks_per_floor = brick_slots_per_floor(grid_size);

    StaticBrickStackSpec spec;
    spec.grid_size = grid_size;
    spec.brick_gap_fraction = std::max(config.struct_brick_gap, 0.0f)
        / std::max(building.metrics.footprint, 0.1f);

    if (building.layers.empty())
    {
        spec.bricks.push_back({ 1.0f, 1.0f, 0u });
    }
    else
    {
        std::vector<size_t> positive_layer_indices;
        positive_layer_indices.reserve(building.layers.size());
        for (size_t i = 0; i < building.layers.size(); ++i)
            if (building.layers[i].height > 0.0f)
                positive_layer_indices.push_back(i);

        const int num_bricks = static_cast<int>(positive_layer_indices.size());
        const int num_floors = std::max(1, (num_bricks + bricks_per_floor - 1) / bricks_per_floor);
        std::vector<float> floor_heights(static_cast<size_t>(num_floors), 0.0f);
        for (int compact_index = 0; compact_index < num_bricks; ++compact_index)
        {
            const SemanticBuildingLayer& layer = building.layers[positive_layer_indices[static_cast<size_t>(compact_index)]];
            const int floor = compact_index / bricks_per_floor;
            floor_heights[static_cast<size_t>(floor)] = std::max(floor_heights[static_cast<size_t>(floor)], layer.height);
        }
        float total_height = 0.0f;
        for (float floor_height : floor_heights)
            total_height += floor_height;
        const float floor_gap = std::max(config.struct_stack_gap, 0.0f);
        if (num_floors > 1)
            total_height += floor_gap * static_cast<float>(num_floors - 1);
        total_height = std::max(total_height, 1e-4f);
        spec.floor_gap_fraction = num_floors > 1 ? floor_gap / total_height : 0.0f;

        spec.bricks.reserve(positive_layer_indices.size());
        for (size_t compact_index = 0; compact_index < positive_layer_indices.size(); ++compact_index)
        {
            const size_t layer_index = positive_layer_indices[compact_index];
            const SemanticBuildingLayer& layer = building.layers[layer_index];
            const int local = static_cast<int>(compact_index) % bricks_per_floor;
            const auto [col, row] = brick_slot_position(local, grid_size);
            const int floor = static_cast<int>(compact_index) / bricks_per_floor;
            // Checkerboard: alternate color based on (col + row + floor) parity.
            const size_t color_index = static_cast<size_t>((col + row + floor) % 2);
            spec.bricks.push_back({
                layer.height / total_height,
                layer_color_multiplier(color_index, config.building_alternate_darkening),
                static_cast<uint32_t>(layer_index),
            });
        }
    }

    if (spec.bricks.empty())
        spec.bricks.push_back({ 1.0f, 1.0f, 0u });

    return cache.brick_stack(spec);
}

std::shared_ptr<const GeometryMesh> build_procedural_building_cap_mesh(
    StaticMeshFamilyCache& cache,
    const SemanticCityBuilding& building,
    const MegaCityCodeConfig& config,
    int sides)
{
    PERF_MEASURE();
    const uint32_t top_layer_id = building.layers.empty()
        ? 0u
        : static_cast<uint32_t>(building.layers.size() - 1);
    return cache.building_cap(
        std::max(sides, 3),
        1.0f + std::max(config.building_middle_strip_push, 0.0f),
        top_layer_id);
}

std::shared_ptr<const GeometryMesh> build_building_roof_sign_mesh(
    StaticMeshFamilyCache& cache,
    const RoofSignPlacementSpec& placement)
{
    PERF_MEASURE();
    const float outer_radius = std::max(placement.inner_radius + placement.band_depth, 0.05f);
    StaticRoofSignRingSpec spec;
    spec.sides = std::max(placement.sides, 3);
    spec.inner_radius_fraction = 0.5f * std::max(placement.inner_radius, 0.01f) / outer_radius;
    return cache.roof_sign_ring(spec);
}

void build_point_shadow_debug_scene(
    CodeVizSceneWorld& world,
    StaticMeshFamilyCache& static_meshes,
    const MegaCityCodeConfig& config,
    const std::shared_ptr<const GeometryMesh>& foliage_stem_mesh,
    const std::shared_ptr<const GeometryMesh>& foliage_card_mesh,
    const TreeMetrics& tree_metrics)
{
    PERF_MEASURE();
    world.create_road_surface(
        0.0f,
        0.0f,
        RoadSurfaceMetrics{
            kPointShadowDebugSceneHalfExtent * 2.0f,
            kPointShadowDebugSceneHalfExtent * 2.0f,
            config.road_surface_height,
            kRoadMaterialUvScale,
            1.0f,
            1.0f,
        },
        CodeVizSemanticRef{ "", "PointShadowDebugGround", "" },
        kRoadSurfaceTextureLift);

    BuildingMetrics primary_metrics;
    primary_metrics.footprint = kPointShadowDebugPrimaryFootprint;
    primary_metrics.height = kPointShadowDebugPrimaryHeight;
    primary_metrics.sidewalk_width = 0.0f;
    primary_metrics.road_width = 0.0f;
    world.create_building(
        kPointShadowDebugPrimaryCenter.x,
        kPointShadowDebugPrimaryCenter.y,
        building_base_elevation(config),
        primary_metrics,
        glm::vec4(0.86f, 0.74f, 0.62f, 1.0f),
        CodeVizSemanticRef{ "", "PointShadowDebugPrimary", "" },
        CodeVizMaterialPreset::FlatColor,
        build_procedural_building_mesh(
            static_meshes,
            SemanticCityBuilding{
                .metrics = primary_metrics,
                .center = kPointShadowDebugPrimaryCenter,
            },
            config,
            4),
        1.0f,
        CustomMeshTransformMode::ScaleByBlockMetrics);

    BuildingMetrics secondary_metrics;
    secondary_metrics.footprint = kPointShadowDebugSecondaryFootprint;
    secondary_metrics.height = kPointShadowDebugSecondaryHeight;
    secondary_metrics.sidewalk_width = 0.0f;
    secondary_metrics.road_width = 0.0f;
    world.create_building(
        kPointShadowDebugSecondaryCenter.x,
        kPointShadowDebugSecondaryCenter.y,
        building_base_elevation(config),
        secondary_metrics,
        glm::vec4(0.58f, 0.72f, 0.90f, 1.0f),
        CodeVizSemanticRef{ "", "PointShadowDebugSecondary", "" },
        CodeVizMaterialPreset::FlatColor,
        build_procedural_building_mesh(
            static_meshes,
            SemanticCityBuilding{
                .metrics = secondary_metrics,
                .center = kPointShadowDebugSecondaryCenter,
            },
            config,
            4),
        1.0f,
        CustomMeshTransformMode::ScaleByBlockMetrics);

    if (foliage_stem_mesh && foliage_card_mesh && tree_metrics.height > 0.0f)
    {
        world.create_tree_bark(
            kPointShadowDebugTreeCenter.x,
            kPointShadowDebugTreeCenter.y,
            building_base_elevation(config),
            tree_metrics,
            glm::vec4(1.0f),
            CodeVizSemanticRef{ "", "PointShadowDebugTreeBark", "" });
        world.create_tree_leaves(
            kPointShadowDebugTreeCenter.x,
            kPointShadowDebugTreeCenter.y,
            building_base_elevation(config),
            tree_metrics,
            glm::vec4(1.0f),
            CodeVizSemanticRef{ "", "PointShadowDebugTreeLeaves", "" });
    }
}

std::string building_connection_key(
    std::string_view source_file_path,
    std::string_view module_path,
    std::string_view qualified_name)
{
    std::string key;
    key.reserve(source_file_path.size() + module_path.size() + qualified_name.size() + 2);
    key.append(source_file_path);
    key.push_back('\n');
    key.append(module_path);
    key.push_back('\n');
    key.append(qualified_name);
    return key;
}

std::unordered_map<std::string, int> build_incident_connection_counts(const SemanticMegacityModel& model)
{
    PERF_MEASURE();
    std::unordered_map<std::string, int> connection_counts;
    connection_counts.reserve(model.dependencies.size() * 2);
    for (const SemanticCityDependency& dependency : model.dependencies)
    {
        ++connection_counts[building_connection_key(
            dependency.source_file_path,
            dependency.source_module_path,
            dependency.source_qualified_name)];
        ++connection_counts[building_connection_key(
            dependency.target_file_path,
            dependency.target_module_path,
            dependency.target_qualified_name)];
    }
    return connection_counts;
}

std::string module_display_name(std::string_view module_path)
{
    PERF_MEASURE();
    const std::filesystem::path path(module_path);
    const std::string leaf = path.filename().string();
    return !leaf.empty() ? leaf : std::string(module_path);
}

float compute_building_sign_height(
    const SemanticCityBuilding& building, std::string_view text, const TextService* text_service,
    const MegaCityCodeConfig& config, float face_width)
{
    PERF_MEASURE();
    const float clamped_face_width = std::max(face_width, 0.1f);
    float sign_height = clamped_face_width * 0.25f;

    if (text_service && !text.empty())
    {
        const int cw = std::max(text_service->metrics().cell_width, 1);
        const int ch = std::max(text_service->metrics().cell_height, 1);
        const float aspect = static_cast<float>(ch) / static_cast<float>(cw);
        const float char_width = clamped_face_width / std::max(static_cast<float>(text.size()), 1.0f);
        sign_height = char_width * aspect + 2.0f * config.wall_sign_side_inset;
    }

    return std::clamp(sign_height, 0.24f, building.metrics.height * 0.15f);
}

float roof_sign_outer_radius_for_face_width(int sides, float face_width)
{
    const int clamped_sides = std::max(sides, 3);
    const float clamped_face_width = std::max(face_width, 0.1f);
    if (clamped_sides == 4)
        return clamped_face_width * 0.5f;

    const float half_angle = glm::pi<float>() / static_cast<float>(clamped_sides);
    const float sin_half_angle = std::max(std::sin(half_angle), 1e-4f);
    return clamped_face_width / (2.0f * sin_half_angle);
}

float roof_sign_face_width_for_outer_radius(int sides, float outer_radius)
{
    const int clamped_sides = std::max(sides, 3);
    const float clamped_outer_radius = std::max(outer_radius, 0.05f);
    if (clamped_sides == 4)
        return clamped_outer_radius * 2.0f;

    const float half_angle = glm::pi<float>() / static_cast<float>(clamped_sides);
    return 2.0f * clamped_outer_radius * std::sin(half_angle);
}

RoofSignPlacementSpec place_building_roof_sign(
    const SemanticCityBuilding& building, std::string_view text, const TextService* text_service,
    const MegaCityCodeConfig& config, int sides)
{
    PERF_MEASURE();
    RoofSignPlacementSpec placement;
    placement.center = building.center;
    placement.sides = std::max(sides, 3);
    placement.band_depth = std::max(config.wall_sign_thickness, 0.02f);
    const float base_outer_radius
        = std::max(building.metrics.footprint * 0.5f + config.wall_sign_face_gap + placement.band_depth, 0.05f);
    const float min_face_width_for_text = std::max(
        static_cast<float>(text.size()) * std::max(config.roof_sign_min_width_per_character, 0.0f)
            + 2.0f * config.wall_sign_side_inset,
        0.0f);
    const float text_outer_radius = roof_sign_outer_radius_for_face_width(placement.sides, min_face_width_for_text);
    const float outer_radius = std::max(base_outer_radius, text_outer_radius);
    placement.inner_radius = std::max(outer_radius - placement.band_depth, 0.05f);
    placement.outer_diameter = outer_radius * 2.0f;
    placement.height = compute_building_sign_height(
        building,
        text,
        text_service,
        config,
        roof_sign_face_width_for_outer_radius(placement.sides, outer_radius));
    return placement;
}

// Returns two signs for a module: [0] on the south border facing south, [1] on the north border facing north.
// The label sits on the module outline rather than over the park.
std::array<SignPlacementSpec, 2> place_module_boundary_signs(
    const SemanticCityModuleLayout& module_layout, std::string_view text, const TextService* text_service,
    const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
    const std::array<ModuleBoundarySignPlacement, 2> placements
        = build_module_boundary_sign_placements(module_layout, config);

    float sign_height = placements[0].width * 0.25f;
    if (text_service && !text.empty())
    {
        const int cw = std::max(text_service->metrics().cell_width, 1);
        const int ch = std::max(text_service->metrics().cell_height, 1);
        const float aspect = static_cast<float>(ch) / static_cast<float>(cw);
        const float char_width = placements[0].width / std::max(static_cast<float>(text.size()), 1.0f);
        sign_height = char_width * aspect + 2.0f * config.road_sign_edge_inset;
    }
    sign_height = std::max(0.24f, sign_height);

    std::array<SignPlacementSpec, 2> signs;
    for (size_t index = 0; index < placements.size(); ++index)
    {
        signs[index].center = placements[index].center;
        signs[index].width = placements[index].width;
        signs[index].height = sign_height;
        signs[index].depth = placements[index].depth;
        signs[index].yaw_radians = placements[index].yaw_radians;
        signs[index].mesh = kCityWallSignMesh;
    }
    return signs;
}

SignLabelRequest make_sign_request(
    std::string key, std::string_view text, const SignPlacementSpec& placement,
    const TextService* text_service, const MegaCityCodeConfig& config, bool building_sign)
{
    PERF_MEASURE();
    int pixel_width;
    int pixel_height;
    if (text_service && !text.empty())
    {
        const int cw = std::max(text_service->metrics().cell_width, 1);
        const int ch = std::max(text_service->metrics().cell_height, 1);
        const int kPad = std::max(config.wall_sign_text_padding, 0);
        pixel_width = static_cast<int>(text.size()) * cw + 2 * kPad;
        pixel_height = ch + 2 * kPad;
    }
    else
    {
        pixel_width = std::max(1, static_cast<int>(text.size()) * 8);
        pixel_height = 16;
    }

    const glm::vec3& text_color = building_sign ? config.building_sign_text_color : config.module_sign_text_color;
    return SignLabelRequest{
        .key = std::move(key),
        .text = std::string(text),
        .target_pixel_width = pixel_width,
        .target_pixel_height = pixel_height,
        .vertical_align = SignLabelVerticalAlign::Center,
        .text_r = color_channel_to_byte(text_color.r),
        .text_g = color_channel_to_byte(text_color.g),
        .text_b = color_channel_to_byte(text_color.b),
    };
}

std::string building_sign_key(const SemanticCityBuilding& building)
{
    return "building:" + building.qualified_name;
}

std::string module_sign_key(std::string_view module_path)
{
    return "module:" + std::string(module_path);
}

SignMetrics make_sign_metrics(const SignPlacementSpec& placement, const SignAtlasEntry& entry)
{
    return SignMetrics{
        .width = placement.width,
        .height = placement.height,
        .depth = placement.depth,
        .yaw_radians = placement.yaw_radians,
        .uv_rect = entry.uv_rect,
        .label_ink_pixel_size = glm::vec2(entry.ink_pixel_size),
    };
}

SignMetrics make_sign_metrics(const RoofSignPlacementSpec& placement, const SignAtlasEntry& entry)
{
    return SignMetrics{
        .width = placement.outer_diameter,
        .height = placement.height,
        .depth = placement.band_depth,
        .yaw_radians = placement.yaw_radians,
        .uv_rect = entry.uv_rect,
        .label_ink_pixel_size = glm::vec2(entry.ink_pixel_size),
    };
}

enum class CityRole
{
    ConcreteClass,
    AbstractClass,
    DataStruct,
    FreeFunction,
    Method,
    Include,
};

struct EntitySpec
{
    const char* entity_kind = "";
};

[[nodiscard]] EntitySpec entity_spec(CityRole role)
{
    switch (role)
    {
    case CityRole::ConcreteClass:
        return { "building" };
    case CityRole::AbstractClass:
        return { "tower" };
    case CityRole::DataStruct:
        return { "block" };
    case CityRole::FreeFunction:
        return { "tree" };
    case CityRole::Method:
    case CityRole::Include:
        return {};
    }
    return {};
}

[[nodiscard]] const CodeSemanticNode* find_semantic_node(
    const CodeSemanticSnapshot& semantics,
    CodeSemanticNodeId id)
{
    const auto index_it = semantics.indexes.node_index_by_id.find(id);
    if (index_it == semantics.indexes.node_index_by_id.end()
        || index_it->second >= semantics.nodes.size())
    {
        return nullptr;
    }
    return &semantics.nodes[index_it->second];
}

[[nodiscard]] std::vector<const CodeSemanticNode*> semantic_children(
    const CodeSemanticSnapshot& semantics,
    CodeSemanticNodeId parent_id)
{
    std::vector<const CodeSemanticNode*> children;
    const auto child_ids_it = semantics.indexes.nodes_by_parent.find(parent_id);
    if (child_ids_it == semantics.indexes.nodes_by_parent.end())
        return children;

    children.reserve(child_ids_it->second.size());
    for (const CodeSemanticNodeId child_id : child_ids_it->second)
    {
        if (const CodeSemanticNode* child = find_semantic_node(semantics, child_id))
            children.push_back(child);
    }
    return children;
}

[[nodiscard]] CityRole city_role_for_node(const CodeSemanticNode& node)
{
    if (node.kind == CodeSemanticNodeKind::Function)
        return CityRole::FreeFunction;
    if (node.kind == CodeSemanticNodeKind::Method)
        return CityRole::Method;
    if (node.kind == CodeSemanticNodeKind::Include)
        return CityRole::Include;
    if (node.kind == CodeSemanticNodeKind::Type)
    {
        if (node.is_abstract)
            return CityRole::AbstractClass;
        if (node.type_kind == CodeSemanticTypeKind::Struct && node.metrics.method_count == 0)
            return CityRole::DataStruct;
        return CityRole::ConcreteClass;
    }
    return CityRole::Include;
}

[[nodiscard]] int semantic_function_size(const CodeSemanticNode& node)
{
    return std::max(1, static_cast<int>(node.metrics.line_count));
}

[[nodiscard]] int semantic_free_function_size(const CodeSemanticNode& node)
{
    if (node.source.end_line >= node.source.start_line && node.source.start_line > 0)
        return std::max(1, static_cast<int>(node.source.end_line - node.source.start_line));
    return semantic_function_size(node);
}

[[nodiscard]] std::unordered_map<CodeSemanticNodeId, std::vector<CodeSemanticNodeId>>
build_inheritance_descendants(const CodeSemanticSnapshot& semantics)
{
    PERF_MEASURE();
    std::unordered_map<CodeSemanticNodeId, std::vector<CodeSemanticNodeId>> children_of;
    for (const CodeSemanticEdge& edge : semantics.edges)
    {
        if (edge.kind != CodeSemanticEdgeKind::Inherits || edge.source_id == edge.target_id)
            continue;
        children_of[edge.target_id].push_back(edge.source_id);
    }

    std::unordered_map<CodeSemanticNodeId, std::vector<CodeSemanticNodeId>> descendants;
    for (const auto& [parent_id, direct_children] : children_of)
    {
        std::vector<CodeSemanticNodeId> all;
        all.push_back(parent_id);
        std::unordered_set<CodeSemanticNodeId> visited;
        visited.insert(parent_id);
        std::vector<CodeSemanticNodeId> frontier = direct_children;
        while (!frontier.empty())
        {
            const CodeSemanticNodeId current = frontier.back();
            frontier.pop_back();
            if (!visited.insert(current).second)
                continue;

            all.push_back(current);
            const auto children_it = children_of.find(current);
            if (children_it != children_of.end())
            {
                for (const CodeSemanticNodeId child : children_it->second)
                    frontier.push_back(child);
            }
        }
        descendants[parent_id] = std::move(all);
    }
    return descendants;
}

struct ModuleAgg
{
    int building_count = 0;
    int total_functions = 0;
    int total_function_lines = 0;
    int total_fields = 0;
    int total_road_size = 0;
};

struct EntityInfo
{
    CodeSemanticNodeId id = 0;
    std::string qualified_name;
    std::string module_path;
    std::string source_file_path;
};

struct PendingDependency
{
    CodeSemanticNodeId source_entity_id = 0;
    std::string field_name;
    std::string field_type_name;
    CodeSemanticNodeId target_type_id = 0;
    bool is_abstract_ref = false;
};

struct CitySemanticProjection
{
    std::vector<SemanticCityModuleInput> modules;
    CodebaseHealthMetrics health;
};

[[nodiscard]] bool module_is_visible(std::string_view module_path, const MegaCityCodeConfig& config)
{
    return config.selected_module_path.empty() || module_path == config.selected_module_path;
}

[[nodiscard]] const CodeSemanticNode* dependency_source_entity(
    const CodeSemanticSnapshot& semantics,
    const CodeSemanticNode& edge_source)
{
    if (edge_source.kind == CodeSemanticNodeKind::Field)
    {
        const CodeSemanticNode* parent = find_semantic_node(semantics, edge_source.parent_id);
        if (parent && (parent->kind == CodeSemanticNodeKind::Type || parent->kind == CodeSemanticNodeKind::Function))
            return parent;
        return nullptr;
    }
    if (edge_source.kind == CodeSemanticNodeKind::Type || edge_source.kind == CodeSemanticNodeKind::Function)
        return &edge_source;
    return nullptr;
}

[[nodiscard]] CitySemanticProjection build_city_semantic_projection(
    const CodeSemanticSnapshot& semantics,
    const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
    CitySemanticProjection projection;
    std::unordered_map<std::string, std::vector<CityClassRecord>> rows_by_module;
    std::unordered_map<std::string, std::vector<CityDependencyRecord>> deps_by_module;
    std::unordered_map<std::string, ModuleAgg> module_agg;
    std::unordered_map<CodeSemanticNodeId, EntityInfo> entities_by_id;
    std::unordered_map<CodeSemanticNodeId, std::unordered_set<CodeSemanticNodeId>> dependency_targets_by_source;
    std::vector<PendingDependency> pending_dependencies;

    const auto inheritance_descendants = build_inheritance_descendants(semantics);

    for (const CodeSemanticEdge& edge : semantics.edges)
    {
        if (edge.kind != CodeSemanticEdgeKind::ReferencesType)
            continue;

        const CodeSemanticNode* edge_source = find_semantic_node(semantics, edge.source_id);
        const CodeSemanticNode* direct_target = find_semantic_node(semantics, edge.target_id);
        if (!edge_source || !direct_target || direct_target->kind != CodeSemanticNodeKind::Type)
            continue;

        const CodeSemanticNode* source_entity = dependency_source_entity(semantics, *edge_source);
        if (!source_entity || !module_is_visible(source_entity->module_path, config))
            continue;

        std::vector<CodeSemanticNodeId> target_ids;
        bool is_abstract_ref = direct_target->is_abstract;
        if (is_abstract_ref)
        {
            const auto descendants_it = inheritance_descendants.find(direct_target->id);
            if (descendants_it != inheritance_descendants.end())
                target_ids = descendants_it->second;
        }
        if (target_ids.empty())
            target_ids = { direct_target->id };

        for (const CodeSemanticNodeId target_id : target_ids)
        {
            if (target_id == source_entity->id)
                continue;
            dependency_targets_by_source[source_entity->id].insert(target_id);
            pending_dependencies.push_back(PendingDependency{
                source_entity->id,
                edge_source->kind == CodeSemanticNodeKind::Field ? edge_source->name : std::string(),
                edge.label,
                target_id,
                is_abstract_ref,
            });
        }
    }

    for (const CodeSemanticNode& node : semantics.nodes)
    {
        if ((node.kind != CodeSemanticNodeKind::Type && node.kind != CodeSemanticNodeKind::Function)
            || !module_is_visible(node.module_path, config))
        {
            continue;
        }

        const CityRole role = city_role_for_node(node);
        if (role == CityRole::Method || role == CityRole::Include)
            continue;

        const EntitySpec spec = entity_spec(role);
        CityClassRecord row;
        row.name = node.name;
        row.qualified_name = node.qualified_name;
        row.module_path = node.module_path;
        row.source_file_path = node.source.file_path;
        row.entity_kind = spec.entity_kind;
        row.is_struct = node.type_kind == CodeSemanticTypeKind::Struct && node.metrics.method_count == 0;
        row.base_size = node.kind == CodeSemanticNodeKind::Type ? static_cast<int>(node.metrics.field_count) : 0;
        row.is_abstract = node.is_abstract;

        if (node.kind == CodeSemanticNodeKind::Type)
        {
            const std::vector<const CodeSemanticNode*> children = semantic_children(semantics, node.id);
            for (const CodeSemanticNode* child : children)
            {
                if (!child || child->kind != CodeSemanticNodeKind::Method)
                    continue;
                row.function_sizes.push_back(semantic_function_size(*child));
                row.function_names.push_back(child->name);
            }
            row.building_functions = static_cast<int>(row.function_sizes.size());
        }
        else
        {
            row.building_functions = 1;
            row.function_sizes = { semantic_free_function_size(node) };
            row.function_names = { row.name };
        }

        if (const auto deps_it = dependency_targets_by_source.find(node.id);
            deps_it != dependency_targets_by_source.end())
        {
            row.road_size = static_cast<int>(deps_it->second.size());
        }

        const std::string module_path = row.module_path;
        rows_by_module[module_path].push_back(row);
        entities_by_id.emplace(node.id, EntityInfo{
            node.id,
            row.qualified_name,
            module_path,
            row.source_file_path,
        });

        if (role == CityRole::ConcreteClass || role == CityRole::AbstractClass || role == CityRole::DataStruct)
        {
            auto& agg = module_agg[module_path];
            ++agg.building_count;
            agg.total_functions += row.building_functions;
            for (const int function_size : row.function_sizes)
                agg.total_function_lines += function_size;
            agg.total_fields += row.base_size;
            agg.total_road_size += row.road_size;
        }
    }

    std::set<std::tuple<CodeSemanticNodeId, CodeSemanticNodeId, std::string, std::string>> dependency_keys;
    for (const PendingDependency& pending : pending_dependencies)
    {
        const auto source_it = entities_by_id.find(pending.source_entity_id);
        const auto target_it = entities_by_id.find(pending.target_type_id);
        if (source_it == entities_by_id.end() || target_it == entities_by_id.end())
            continue;

        const auto key = std::make_tuple(
            pending.source_entity_id,
            pending.target_type_id,
            pending.field_name,
            pending.field_type_name);
        if (!dependency_keys.insert(key).second)
            continue;

        const EntityInfo& source = source_it->second;
        const EntityInfo& target = target_it->second;
        deps_by_module[source.module_path].push_back(CityDependencyRecord{
            source.qualified_name,
            source.module_path,
            pending.field_name,
            pending.field_type_name,
            target.qualified_name,
            target.module_path,
            source.source_file_path,
            target.source_file_path,
            pending.is_abstract_ref,
        });
    }

    float weighted_complexity = 0.0f;
    float weighted_cohesion = 0.0f;
    float weighted_coupling = 0.0f;
    int total_weight = 0;
    for (const auto& [module_path, rows] : rows_by_module)
    {
        float module_quality = 0.5f;
        CodebaseHealthMetrics module_health;
        if (const auto agg_it = module_agg.find(module_path); agg_it != module_agg.end())
        {
            const ModuleAgg& agg = agg_it->second;
            const float avg_function_size = agg.total_functions > 0
                ? static_cast<float>(agg.total_function_lines) / static_cast<float>(agg.total_functions)
                : 0.0f;
            module_health.complexity = agg.total_functions > 0
                ? 1.0f / (1.0f + avg_function_size / 10.0f)
                : 0.5f;
            const float avg_cohesion_ratio = agg.building_count > 0
                ? static_cast<float>(agg.total_functions) / static_cast<float>(std::max(agg.total_fields, 1))
                : 0.0f;
            module_health.cohesion = agg.building_count > 0
                ? avg_cohesion_ratio / (avg_cohesion_ratio + 1.0f)
                : 0.5f;
            const float avg_deps = agg.building_count > 0
                ? static_cast<float>(agg.total_road_size) / static_cast<float>(agg.building_count)
                : 0.0f;
            module_health.coupling = agg.building_count > 0
                ? 1.0f / (1.0f + avg_deps / 3.0f)
                : 0.5f;
            module_quality = module_health.complexity;

            weighted_complexity += static_cast<float>(agg.building_count) * module_health.complexity;
            weighted_cohesion += static_cast<float>(agg.building_count) * module_health.cohesion;
            weighted_coupling += static_cast<float>(agg.building_count) * module_health.coupling;
            total_weight += agg.building_count;
        }

        auto deps_it = deps_by_module.find(module_path);
        auto rows_copy = rows;
        std::sort(rows_copy.begin(), rows_copy.end(), [](const CityClassRecord& a, const CityClassRecord& b) {
            return a.qualified_name < b.qualified_name;
        });
        if (deps_it != deps_by_module.end())
        {
            std::sort(deps_it->second.begin(), deps_it->second.end(), [](const CityDependencyRecord& a, const CityDependencyRecord& b) {
                return std::tie(a.source_qualified_name, a.field_name, a.target_qualified_name)
                    < std::tie(b.source_qualified_name, b.field_name, b.target_qualified_name);
            });
        }

        projection.modules.push_back(SemanticCityModuleInput{
            module_path,
            std::move(rows_copy),
            deps_it != deps_by_module.end() ? std::move(deps_it->second) : std::vector<CityDependencyRecord>{},
            module_quality,
            module_health,
        });
    }

    std::sort(projection.modules.begin(), projection.modules.end(), [](const SemanticCityModuleInput& a, const SemanticCityModuleInput& b) {
        return a.module_path < b.module_path;
    });

    projection.health = semantics.health;
    if (total_weight > 0)
    {
        projection.health.complexity = weighted_complexity / static_cast<float>(total_weight);
        projection.health.cohesion = weighted_cohesion / static_cast<float>(total_weight);
        projection.health.coupling = weighted_coupling / static_cast<float>(total_weight);
    }

    return projection;
}

} // namespace

int procedural_building_side_count(
    int incident_connection_count,
    int connected_hex_building_threshold,
    int connected_oct_building_threshold)
{
    const int hex_threshold = std::max(connected_hex_building_threshold, 1);
    const int oct_threshold = std::max(connected_oct_building_threshold, hex_threshold + 1);
    if (incident_connection_count >= oct_threshold)
        return 8;
    if (incident_connection_count >= hex_threshold)
        return 6;
    return 4;
}

SemanticCodeModelBuildResult build_semantic_code_model(
    const CodeSemanticSnapshot& semantics,
    const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
    SemanticCodeModelBuildResult result;

    CitySemanticProjection projection = build_city_semantic_projection(semantics, config);

    result.semantic_model = std::make_shared<SemanticMegacityModel>(
        build_semantic_megacity_model(projection.modules, config));
    result.semantic_model->codebase_health = projection.health;
    const RuntimePerfSnapshot perf_snapshot = runtime_perf_collector().latest_snapshot();
    result.live_metrics = std::make_shared<LiveCityMetricsSnapshot>(
        build_live_city_metrics_snapshot(
            *result.semantic_model,
            perf_snapshot.generation != 0 ? &perf_snapshot : nullptr));
    return result;
}

CityBuildResult build_city(
    CodeVizSceneWorld& world,
    const CodeSemanticSnapshot& semantics,
    TextService* text_service,
    const MegaCityCodeConfig& config,
    uint64_t& sign_label_revision)
{
    PERF_MEASURE();
    CityBuildResult result;

    SemanticCodeModelBuildResult semantic_result = build_semantic_code_model(
        semantics,
        config);
    auto semantic_model = std::move(semantic_result.semantic_model);
    result.live_metrics = std::move(semantic_result.live_metrics);
    const std::unordered_map<std::string, int> building_connection_counts
        = build_incident_connection_counts(*semantic_model);
    auto layout = std::make_unique<SemanticMegacityLayout>(
        build_semantic_megacity_layout(*semantic_model, config));
    result.city_bounds_valid = !layout->empty();
    if (result.city_bounds_valid)
    {
        result.min_x = layout->min_x;
        result.max_x = layout->max_x;
        result.min_z = layout->min_z;
        result.max_z = layout->max_z;

        if (!config.point_light_position_valid)
        {
            const float span = std::max(layout->max_x - layout->min_x, layout->max_z - layout->min_z);
            result.computed_default_light = true;
            result.default_light_x = layout->min_x;
            result.default_light_y = std::max(8.0f, span * 0.4f);
            result.default_light_z = layout->min_z;
            result.default_light_radius = std::max(24.0f, span * 0.8f);
        }
    }

    // Build sign label requests.
    constexpr size_t kMaxSignChars = 15;
    std::vector<SignLabelRequest> sign_requests;
    sign_requests.reserve(layout->building_count() + layout->modules.size());
    for (const auto& module_layout : layout->modules)
    {
        for (const auto& building : module_layout.buildings)
        {
            const std::string& full_text = building.display_name.empty() ? building.qualified_name : building.display_name;
            const std::string text = full_text.size() <= kMaxSignChars
                ? full_text
                : full_text.substr(0, kMaxSignChars - 3) + "...";
            auto sign_req = make_sign_request(building_sign_key(building), text, {}, text_service, config, true);
            if (building.is_free_function)
            {
                sign_req.text_r = color_channel_to_byte(config.function_sign_text_color.r);
                sign_req.text_g = color_channel_to_byte(config.function_sign_text_color.g);
                sign_req.text_b = color_channel_to_byte(config.function_sign_text_color.b);
            }
            sign_requests.push_back(std::move(sign_req));
        }

        const float extent_x = module_layout.max_x - module_layout.min_x;
        const float extent_z = module_layout.max_z - module_layout.min_z;
        if (!module_layout.is_central_park
            && !module_layout.buildings.empty()
            && extent_x > 1e-4f
            && extent_z > 1e-4f)
        {
            const std::string name = module_display_name(module_layout.module_path);
            const auto boundary_signs = place_module_boundary_signs(
                module_layout,
                name,
                text_service,
                config);
            // Both signs share the same atlas entry (same text/key).
            auto request = make_sign_request(
                module_sign_key(module_layout.module_path), name, boundary_signs[0], text_service, config, false);
            request.text_r = 255;
            request.text_g = 255;
            request.text_b = 255;
            sign_requests.push_back(std::move(request));
        }
    }

    std::shared_ptr<SignLabelAtlas> sign_label_atlas;
    if (text_service)
        sign_label_atlas = build_sign_label_atlas(*text_service, sign_requests, sign_label_revision++);

    // Populate the ECS world.
    world.clear();
    std::shared_ptr<const GeometryMesh> central_park_tree_bark_mesh;
    std::shared_ptr<const GeometryMesh> central_park_tree_leaf_mesh;
    TreeMetrics central_park_tree_metrics;
    for (const auto& module_layout : layout->modules)
    {
        if ((module_layout.is_central_park && module_layout.park_footprint > 0.0f)
            || config.point_shadow_debug_scene)
        {
            DraxulTreeMeshes generated_tree = generate_draxul_tree_meshes(make_central_park_tree_params(config));
            auto bark_mesh = std::make_shared<GeometryMesh>(std::move(generated_tree.bark_mesh));
            auto leaf_mesh = std::make_shared<GeometryMesh>(std::move(generated_tree.leaf_mesh));
            central_park_tree_metrics = tree_metrics_from_meshes(*bark_mesh, *leaf_mesh);
            central_park_tree_bark_mesh = std::move(bark_mesh);
            central_park_tree_leaf_mesh = std::move(leaf_mesh);
            break;
        }
    }
    result.foliage_stem_mesh = central_park_tree_bark_mesh;
    result.foliage_card_mesh = central_park_tree_leaf_mesh;

    StaticMeshFamilyCache static_meshes;

    if (config.point_shadow_debug_scene)
    {
        world.clear();
        build_point_shadow_debug_scene(
            world,
            static_meshes,
            config,
            central_park_tree_bark_mesh,
            central_park_tree_leaf_mesh,
            central_park_tree_metrics);
        result.city_bounds_valid = true;
        result.min_x = -kPointShadowDebugSceneHalfExtent;
        result.max_x = kPointShadowDebugSceneHalfExtent;
        result.min_z = -kPointShadowDebugSceneHalfExtent;
        result.max_z = kPointShadowDebugSceneHalfExtent;
        if (!config.point_light_position_valid)
        {
            result.computed_default_light = true;
            result.default_light_x = 3.0f;
            result.default_light_y = 6.0f;
            result.default_light_z = 3.0f;
            result.default_light_radius = 18.0f;
        }
        result.semantic_model = std::move(semantic_model);
        result.sign_label_atlas = std::move(sign_label_atlas);
        result.layout = std::move(layout);
        return result;
    }

    const CitySurfaceBounds road_surface_bounds = compute_city_road_surface_bounds(*layout);
    if (road_surface_bounds.valid())
    {
        world.create_road_surface(
            (road_surface_bounds.min_x + road_surface_bounds.max_x) * 0.5f,
            (road_surface_bounds.min_z + road_surface_bounds.max_z) * 0.5f,
            RoadSurfaceMetrics{
                road_surface_bounds.max_x - road_surface_bounds.min_x,
                road_surface_bounds.max_z - road_surface_bounds.min_z,
                config.road_surface_height,
                kRoadMaterialUvScale,
                1.0f,
                1.0f,
            },
            CodeVizSemanticRef{},
            kRoadSurfaceTextureLift);
    }

    const float module_surface_elevation
        = kRoadSurfaceTextureLift + config.road_surface_height + kModuleSurfaceLift;
    for (const auto& module_layout : layout->modules)
    {
        if (module_layout.is_central_park || module_layout.buildings.empty())
            continue;

        const float extent_x = module_layout.max_x - module_layout.min_x;
        const float extent_z = module_layout.max_z - module_layout.min_z;
        if (extent_x <= 1e-4f || extent_z <= 1e-4f)
            continue;

        const float border_width = compute_module_border_width(module_layout, config);
        if (border_width <= 1e-4f)
            continue;

        const glm::vec4 base_color = module_building_color(module_layout.module_path);
        const glm::vec4 module_color(glm::vec3(base_color), base_color.a * config.module_border_alpha);
        const float center_x = (module_layout.min_x + module_layout.max_x) * 0.5f;
        const float center_z = (module_layout.min_z + module_layout.max_z) * 0.5f;
        const float inner_extent_z = std::max(extent_z - 2.0f * border_width, border_width);

        world.create_module_surface(
            center_x,
            module_layout.max_z - border_width * 0.5f,
            ModuleSurfaceMetrics{ extent_x, border_width, kModuleSurfaceHeight },
            module_color,
            CodeVizSemanticRef{ "", module_layout.module_path, module_layout.module_path },
            module_surface_elevation);
        world.create_module_surface(
            center_x,
            module_layout.min_z + border_width * 0.5f,
            ModuleSurfaceMetrics{ extent_x, border_width, kModuleSurfaceHeight },
            module_color,
            CodeVizSemanticRef{ "", module_layout.module_path, module_layout.module_path },
            module_surface_elevation);
        world.create_module_surface(
            module_layout.min_x + border_width * 0.5f,
            center_z,
            ModuleSurfaceMetrics{ border_width, inner_extent_z, kModuleSurfaceHeight },
            module_color,
            CodeVizSemanticRef{ "", module_layout.module_path, module_layout.module_path },
            module_surface_elevation);
        world.create_module_surface(
            module_layout.max_x - border_width * 0.5f,
            center_z,
            ModuleSurfaceMetrics{ border_width, inner_extent_z, kModuleSurfaceHeight },
            module_color,
            CodeVizSemanticRef{ "", module_layout.module_path, module_layout.module_path },
            module_surface_elevation);
    }

    for (const auto& module_layout : layout->modules)
    {
        // Park slab at the center of the module, colored by quality.
        if (module_layout.park_footprint > 0.0f)
        {
            const glm::vec3 kParkBrown(0.45f, 0.30f, 0.15f);
            const glm::vec3 kParkGreen(0.25f, 0.65f, 0.20f);
            const float q = std::clamp(module_layout.quality, 0.0f, 1.0f);
            const glm::vec3 park_rgb = glm::mix(kParkBrown, kParkGreen, q);
            const glm::vec4 park_color(park_rgb, 1.0f);

            BuildingMetrics park_metrics;
            park_metrics.footprint = module_layout.park_footprint;
            park_metrics.height = config.park_height;
            park_metrics.sidewalk_width = module_layout.park_sidewalk_width;
            park_metrics.road_width = module_layout.park_road_width;
            world.create_building(
                module_layout.park_center.x,
                module_layout.park_center.y,
                building_base_elevation(config),
                park_metrics,
                park_color,
                CodeVizSemanticRef{ "", module_layout.module_path, module_layout.module_path },
                CodeVizMaterialPreset::FlatColor);

            if (module_layout.is_central_park)
            {
                world.create_tree_bark(
                    module_layout.park_center.x,
                    module_layout.park_center.y,
                    building_base_elevation(config) + config.park_height,
                    central_park_tree_metrics,
                    glm::vec4(1.0f),
                    CodeVizSemanticRef{ "", "CentralParkTreeBark", module_layout.module_path });
                world.create_tree_leaves(
                    module_layout.park_center.x,
                    module_layout.park_center.y,
                    building_base_elevation(config) + config.park_height,
                    central_park_tree_metrics,
                    glm::vec4(1.0f),
                    CodeVizSemanticRef{ "", "CentralParkTreeLeaves", module_layout.module_path });
            }

            // Reuse the building sidewalk/road segment builders for the park.
            SemanticCityBuilding park_building;
            park_building.center = module_layout.park_center;
            park_building.metrics = park_metrics;

            for (const RoadSegmentPlacement& sidewalk : build_sidewalk_segments(park_building))
            {
                world.create_road(
                    sidewalk.center.x,
                    sidewalk.center.y,
                    RoadMetrics{ sidewalk.extent.x, sidewalk.extent.y, config.sidewalk_surface_height },
                    kSidewalkSurfaceColor,
                    CodeVizSemanticRef{ "", module_layout.module_path, module_layout.module_path },
                    config.sidewalk_surface_lift);
            }
        }

        const glm::vec4 module_color = module_building_color(module_layout.module_path);
        for (const auto& building : module_layout.buildings)
        {
            const auto count_it = building_connection_counts.find(
                building_connection_key(building.source_file_path, building.module_path, building.qualified_name));
            const int incident_connection_count
                = count_it != building_connection_counts.end() ? count_it->second : 0;
            const int building_side_count = building.is_free_function
                ? 3
                : building.is_struct_stack
                ? 4
                : procedural_building_side_count(
                      incident_connection_count,
                      config.connected_hex_building_threshold,
                      config.connected_oct_building_threshold);
            const float building_level_gap = building.is_struct_stack ? config.struct_stack_gap : 0.0f;
            auto building_mesh = building.is_struct_stack
                ? build_procedural_brick_building_mesh(static_meshes, building, config)
                : build_procedural_building_mesh(static_meshes, building, config, building_side_count, building_level_gap);
            world.create_building(
                building.center.x,
                building.center.y,
                building_base_elevation(config),
                building.metrics,
                module_color,
                CodeVizSemanticRef{ building.source_file_path, building.qualified_name, building.module_path },
                CodeVizMaterialPreset::FlatColor,
                std::move(building_mesh),
                1.0f,
                CustomMeshTransformMode::ScaleByBlockMetrics);

            if (sign_label_atlas)
            {
                const auto it = sign_label_atlas->entries.find(building_sign_key(building));
                if (it != sign_label_atlas->entries.end())
                {
                    const std::string& btext_full = building.display_name.empty() ? building.qualified_name : building.display_name;
                    const std::string btext = btext_full.size() <= kMaxSignChars
                        ? btext_full
                        : btext_full.substr(0, kMaxSignChars - 3) + "...";
                    const RoofSignPlacementSpec roof_sign
                        = place_building_roof_sign(building, btext, text_service, config, building_side_count);
                    const SignMetrics sign_metrics = make_sign_metrics(roof_sign, it->second);
                    const float cap_height = sign_metrics.height;

                    if (cap_height > 0.0f)
                    {
                        const glm::vec4 cap_color = building.is_free_function
                            ? color_with_alpha(config.function_sign_board_color)
                            : (building.is_struct_stack || building.is_struct)
                            ? color_with_alpha(config.struct_sign_board_color)
                            : module_color;
                        BuildingMetrics cap_metrics = building.metrics;
                        cap_metrics.height = cap_height;
                        world.create_building(
                            building.center.x,
                            building.center.y,
                            building_base_elevation(config) + building.metrics.height,
                            cap_metrics,
                            cap_color,
                            CodeVizSemanticRef{ building.source_file_path, building.qualified_name, building.module_path },
                            CodeVizMaterialPreset::FlatColor,
                            build_procedural_building_cap_mesh(
                                static_meshes,
                                building,
                                config,
                                building_side_count),
                            1.0f,
                            CustomMeshTransformMode::ScaleByBlockMetrics);
                    }

                    const float sign_y = building_base_elevation(config) + building.metrics.height + sign_metrics.height * 0.5f;
                    const glm::vec4 sign_board = building.is_free_function
                        ? color_with_alpha(config.function_sign_board_color)
                        : (building.is_struct_stack || building.is_struct)
                        ? color_with_alpha(config.struct_sign_board_color)
                        : building_sign_board_color(config);
                    world.create_sign(
                        roof_sign.center.x,
                        roof_sign.center.y,
                        sign_y,
                        sign_metrics,
                        CodeVizMeshId::Custom,
                        sign_board,
                        CodeVizSemanticRef{ building.source_file_path, building.qualified_name, building.module_path },
                        build_building_roof_sign_mesh(static_meshes, roof_sign),
                        CustomMeshTransformMode::ScaleByLabelMetrics);
                }
            }

            {
                const float inner_r = building.metrics.footprint * 0.5f;
                const float outer_r = inner_r + building.metrics.sidewalk_width;
                StaticSidewalkRingSpec sidewalk_spec;
                sidewalk_spec.sides = building_side_count;
                sidewalk_spec.inner_radius_fraction = outer_r > 1e-4f ? 0.5f * inner_r / outer_r : 0.49f;
                sidewalk_spec.color = glm::vec3(kSidewalkSurfaceColor);
                auto ring_mesh = static_meshes.sidewalk_ring(sidewalk_spec);
                BuildingMetrics sidewalk_metrics;
                sidewalk_metrics.footprint = outer_r * 2.0f;
                sidewalk_metrics.height = config.sidewalk_surface_height;
                world.create_building(
                    building.center.x,
                    building.center.y,
                    config.sidewalk_surface_lift,
                    sidewalk_metrics,
                    kSidewalkSurfaceColor,
                    CodeVizSemanticRef{ building.source_file_path, building.qualified_name, building.module_path },
                    kCityPavingSidewalkMaterial,
                    std::move(ring_mesh),
                    0.0f,
                    CustomMeshTransformMode::ScaleByBlockMetrics);
            }
        }

        const float extent_x = module_layout.max_x - module_layout.min_x;
        const float extent_z = module_layout.max_z - module_layout.min_z;
        if (sign_label_atlas
            && !module_layout.is_central_park
            && !module_layout.buildings.empty()
            && extent_x > 1e-4f
            && extent_z > 1e-4f)
        {
            const auto it = sign_label_atlas->entries.find(module_sign_key(module_layout.module_path));
            if (it != sign_label_atlas->entries.end())
            {
                const std::string name = module_display_name(module_layout.module_path);
                const auto boundary_signs = place_module_boundary_signs(
                    module_layout,
                    name,
                    text_service,
                    config);

                // Place both signs on the module border so the label sits on the outline itself.
                for (const SignPlacementSpec& boundary_sign : boundary_signs)
                {
                    const SignMetrics sign = make_sign_metrics(boundary_sign, it->second);
                    const float module_surface_elevation
                        = kRoadSurfaceTextureLift + config.road_surface_height + kModuleSurfaceLift;
                    world.create_sign(
                        boundary_sign.center.x,
                        boundary_sign.center.y,
                        module_surface_elevation
                            + kModuleSurfaceHeight
                            + sign.height * 0.5f
                            + config.road_sign_lift,
                        sign,
                        boundary_sign.mesh,
                        dark_module_sign_board_color(module_layout.module_path),
                        CodeVizSemanticRef{ "", module_layout.module_path, module_layout.module_path });
                }
            }
        }
    }

    DRAXUL_LOG_INFO(LogCategory::App,
        "CityBuilder: built semantic megacity with %zu modules and %zu buildings",
        layout->modules.size(),
        layout->building_count());
    DRAXUL_LOG_INFO(LogCategory::App,
        "CityBuilder: static mesh family cache retained %zu reusable meshes",
        static_meshes.size());

    // Sync building centers from layout back into the semantic model.
    // The layout applies module placement offsets that the model doesn't have,
    // and picking/selection queries use the model's building centers.
    {
        std::unordered_map<std::string, glm::vec2> layout_centers;
        for (const auto& module_layout : layout->modules)
            for (const auto& building : module_layout.buildings)
                layout_centers[building_connection_key(
                    building.source_file_path,
                    building.module_path,
                    building.qualified_name)]
                    = building.center;

        for (auto& mod : semantic_model->modules)
            for (auto& building : mod.buildings)
            {
                auto it = layout_centers.find(building_connection_key(
                    building.source_file_path,
                    building.module_path,
                    building.qualified_name));
                if (it != layout_centers.end())
                    building.center = it->second;
            }
    }

    result.semantic_model = std::move(semantic_model);
    result.sign_label_atlas = std::move(sign_label_atlas);
    result.layout = std::move(layout);
    return result;
}

void emit_route_entities(
    CodeVizSceneWorld& world,
    const std::vector<CityGrid::RoutePolyline>& routes,
    const MegaCityCodeConfig& config)
{
    PERF_MEASURE();
    const float route_width = std::max(
        config.placement_step * kDependencyRouteWidthScale,
        kDependencyRouteMinWidth);

    for (size_t route_index = 0; route_index < routes.size(); ++route_index)
    {
        const auto& route = routes[route_index];
        if (route.world_points.size() < 2)
            continue;

        float total_length = 0.0f;
        for (size_t point_index = 1; point_index < route.world_points.size(); ++point_index)
            total_length += glm::length(route.world_points[point_index] - route.world_points[point_index - 1]);
        if (total_length <= 1e-4f)
            continue;

        // Compute a single pitch angle for the whole route from the elevation delta
        // and total XZ length.  Each segment shares this pitch so the route is one
        // smooth slope rather than discrete steps.
        const float elev_delta = route.target_elevation - route.source_elevation;
        const float pitch = (total_length > 1e-4f) ? std::atan2(elev_delta, total_length) : 0.0f;

        float traversed_length = 0.0f;
        for (size_t point_index = 1; point_index < route.world_points.size(); ++point_index)
        {
            const glm::vec2 a = route.world_points[point_index - 1];
            const glm::vec2 b = route.world_points[point_index];
            const glm::vec2 delta = b - a;
            const float length = glm::length(delta);
            if (length <= 1e-4f)
                continue;

            // Subdivide the segment so the color gradient is smooth rather than
            // stepping at each bend point.
            const float color_t_start = std::clamp(traversed_length / total_length, 0.0f, 1.0f);
            const float color_t_end = std::clamp((traversed_length + length) / total_length, 0.0f, 1.0f);
            constexpr float kMaxSubSegmentLength = 4.0f;
            const int sub_count = std::max(1, static_cast<int>(std::ceil(length / kMaxSubSegmentLength)));
            const float sub_length = length / static_cast<float>(sub_count);
            const glm::vec2 dir = delta / length;
            // When pitched, the segment's horizontal projection shrinks by cos(pitch).
            // Stretch extent_x so the projected length equals the horizontal spacing.
            const float cos_pitch = std::cos(pitch);
            const float extent_scale = (cos_pitch > 1e-3f) ? 1.0f / cos_pitch : 1.0f;

            for (int sub = 0; sub < sub_count; ++sub)
            {
                const float frac0 = static_cast<float>(sub) / static_cast<float>(sub_count);
                const float frac1 = static_cast<float>(sub + 1) / static_cast<float>(sub_count);
                const glm::vec2 sub_a = a + dir * (frac0 * length);
                const glm::vec2 sub_b = a + dir * (frac1 * length);
                const float sub_mid_t = glm::mix(color_t_start, color_t_end, (frac0 + frac1) * 0.5f);
                const glm::vec4 color = glm::mix(route.source_color, route.target_color, sub_mid_t);
                const float seg_elev = glm::mix(route.source_elevation, route.target_elevation, sub_mid_t);

                world.create_route_segment(
                    (sub_a.x + sub_b.x) * 0.5f,
                    (sub_a.y + sub_b.y) * 0.5f,
                    RouteSegmentMetrics{
                        std::max(sub_length, route_width) * extent_scale,
                        route_width,
                        kDependencyRouteHeight,
                        -std::atan2(delta.y, delta.x),
                        pitch,
                    },
                    color,
                    CodeVizSemanticRef{},
                    seg_elev,
                    RouteLink{
                        route.source_file_path,
                        route.source_module_path,
                        route.source_qualified_name,
                        route.target_file_path,
                        route.target_module_path,
                        route.target_qualified_name,
                    });
            }

            traversed_length += length;
        }
    }
}

} // namespace draxul
