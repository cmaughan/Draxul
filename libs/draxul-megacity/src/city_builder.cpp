#include "city_builder.h"
#include "city_helpers.h"
#include "live_city_metrics.h"
#include "scene_world.h"
#include "semantic_city_layout.h"
#include "sign_label_atlas.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <draxul/building_generator.h>
#include <draxul/citydb.h>
#include <draxul/log.h>
#include <draxul/megacity_code_config.h>
#include <draxul/perf_timing.h>
#include <draxul/roof_sign_generator.h>
#include <draxul/text_service.h>
#include <draxul/tree_generator.h>
#include <filesystem>
#include <glm/gtc/constants.hpp>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

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
    MeshId mesh = MeshId::WallSign;
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

std::shared_ptr<const GeometryMesh> build_procedural_building_mesh(
    const SemanticCityBuilding& building,
    const glm::vec4& module_color,
    const MegaCityCodeConfig& config,
    int sides)
{
    PERF_MEASURE();
    DraxulBuildingParams params;
    params.footprint = building.metrics.footprint;
    params.sides = std::max(sides, 3);
    params.middle_strip_scale = 1.0f + std::max(config.building_middle_strip_push, 0.0f);

    if (building.layers.empty())
    {
        params.levels.push_back({ building.metrics.height, glm::vec3(module_color) });
    }
    else
    {
        params.levels.reserve(building.layers.size());
        for (size_t layer_index = 0; layer_index < building.layers.size(); ++layer_index)
        {
            const SemanticBuildingLayer& layer = building.layers[layer_index];
            if (layer.height <= 0.0f)
                continue;
            params.levels.push_back({
                layer.height,
                glm::vec3(module_building_layer_color(
                    module_color,
                    layer_index,
                    config.building_alternate_darkening)),
                static_cast<uint32_t>(layer_index),
            });
        }
    }

    if (params.levels.empty())
        params.levels.push_back({ std::max(building.metrics.height, 0.1f), glm::vec3(module_color), 0u });

    return std::make_shared<GeometryMesh>(generate_draxul_building(params));
}

std::shared_ptr<const GeometryMesh> build_procedural_building_cap_mesh(
    const SemanticCityBuilding& building,
    const glm::vec4& color,
    const MegaCityCodeConfig& config,
    int sides,
    float height)
{
    PERF_MEASURE();
    DraxulBuildingParams params;
    params.footprint = building.metrics.footprint;
    params.sides = std::max(sides, 3);
    params.middle_strip_scale = 1.0f + std::max(config.building_middle_strip_push, 0.0f);
    const uint32_t top_layer_id = building.layers.empty()
        ? 0u
        : static_cast<uint32_t>(building.layers.size() - 1);
    params.levels.push_back({ std::max(height, 0.1f), glm::vec3(color), top_layer_id });
    return std::make_shared<GeometryMesh>(generate_draxul_building(params));
}

std::shared_ptr<const GeometryMesh> build_building_roof_sign_mesh(const RoofSignPlacementSpec& placement)
{
    PERF_MEASURE();
    DraxulRoofSignParams params;
    params.sides = std::max(placement.sides, 3);
    params.inner_radius = std::max(placement.inner_radius, 0.05f);
    params.band_depth = std::max(placement.band_depth, 0.02f);
    params.height = std::max(placement.height, 0.05f);
    return std::make_shared<GeometryMesh>(generate_draxul_roof_sign(params));
}

void build_point_shadow_debug_scene(
    SceneWorld& world,
    const MegaCityCodeConfig& config,
    const std::shared_ptr<const GeometryMesh>& tree_bark_mesh,
    const std::shared_ptr<const GeometryMesh>& tree_leaf_mesh,
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
        SourceSymbol{ "", "PointShadowDebugGround", "" },
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
        SourceSymbol{ "", "PointShadowDebugPrimary", "" },
        MaterialId::FlatColor,
        build_procedural_building_mesh(
            SemanticCityBuilding{
                .metrics = primary_metrics,
                .center = kPointShadowDebugPrimaryCenter,
            },
            glm::vec4(0.86f, 0.74f, 0.62f, 1.0f),
            config,
            4),
        1.0f);

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
        SourceSymbol{ "", "PointShadowDebugSecondary", "" },
        MaterialId::FlatColor,
        build_procedural_building_mesh(
            SemanticCityBuilding{
                .metrics = secondary_metrics,
                .center = kPointShadowDebugSecondaryCenter,
            },
            glm::vec4(0.58f, 0.72f, 0.90f, 1.0f),
            config,
            4),
        1.0f);

    if (tree_bark_mesh && tree_leaf_mesh && tree_metrics.height > 0.0f)
    {
        world.create_tree_bark(
            kPointShadowDebugTreeCenter.x,
            kPointShadowDebugTreeCenter.y,
            building_base_elevation(config),
            tree_metrics,
            glm::vec4(1.0f),
            SourceSymbol{ "", "PointShadowDebugTreeBark", "" });
        world.create_tree_leaves(
            kPointShadowDebugTreeCenter.x,
            kPointShadowDebugTreeCenter.y,
            building_base_elevation(config),
            tree_metrics,
            glm::vec4(1.0f),
            SourceSymbol{ "", "PointShadowDebugTreeLeaves", "" });
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
        signs[index].mesh = MeshId::WallSign;
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

CityBuildResult build_city(
    SceneWorld& world,
    CityDatabase& city_db,
    TextService* text_service,
    const std::vector<std::string>& available_modules,
    const MegaCityCodeConfig& config,
    uint64_t& sign_label_revision)
{
    PERF_MEASURE();
    CityBuildResult result;

    std::vector<SemanticCityModuleInput> modules;
    for (const std::string& module_path : available_modules)
    {
        if (!config.selected_module_path.empty()
            && module_path != config.selected_module_path)
            continue;
        const CityModuleRecord mod_record = city_db.module_record(module_path);
        modules.push_back({
            module_path,
            city_db.list_classes_in_module(module_path),
            city_db.list_class_dependencies_in_module(module_path),
            mod_record.quality,
            mod_record.health,
        });
    }

    auto semantic_model = std::make_shared<SemanticMegacityModel>(
        build_semantic_megacity_model(modules, config));
    semantic_model->codebase_health = city_db.codebase_health();
    const RuntimePerfSnapshot perf_snapshot = runtime_perf_collector().latest_snapshot();
    result.live_metrics = std::make_shared<LiveCityMetricsSnapshot>(
        build_live_city_metrics_snapshot(
            *semantic_model,
            perf_snapshot.generation != 0 ? &perf_snapshot : nullptr));
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
    std::vector<SignLabelRequest> sign_requests;
    sign_requests.reserve(layout->building_count() + layout->modules.size());
    for (const auto& module_layout : layout->modules)
    {
        for (const auto& building : module_layout.buildings)
        {
            const std::string& text = building.display_name.empty() ? building.qualified_name : building.display_name;
            sign_requests.push_back(make_sign_request(building_sign_key(building), text, {}, text_service, config, true));
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
    result.tree_bark_mesh = central_park_tree_bark_mesh;
    result.tree_leaf_mesh = central_park_tree_leaf_mesh;

    if (config.point_shadow_debug_scene)
    {
        world.clear();
        build_point_shadow_debug_scene(
            world,
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
            SourceSymbol{},
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
            SourceSymbol{ "", module_layout.module_path, module_layout.module_path },
            module_surface_elevation);
        world.create_module_surface(
            center_x,
            module_layout.min_z + border_width * 0.5f,
            ModuleSurfaceMetrics{ extent_x, border_width, kModuleSurfaceHeight },
            module_color,
            SourceSymbol{ "", module_layout.module_path, module_layout.module_path },
            module_surface_elevation);
        world.create_module_surface(
            module_layout.min_x + border_width * 0.5f,
            center_z,
            ModuleSurfaceMetrics{ border_width, inner_extent_z, kModuleSurfaceHeight },
            module_color,
            SourceSymbol{ "", module_layout.module_path, module_layout.module_path },
            module_surface_elevation);
        world.create_module_surface(
            module_layout.max_x - border_width * 0.5f,
            center_z,
            ModuleSurfaceMetrics{ border_width, inner_extent_z, kModuleSurfaceHeight },
            module_color,
            SourceSymbol{ "", module_layout.module_path, module_layout.module_path },
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
                SourceSymbol{ "", module_layout.module_path, module_layout.module_path },
                MaterialId::FlatColor);

            if (module_layout.is_central_park)
            {
                world.create_tree_bark(
                    module_layout.park_center.x,
                    module_layout.park_center.y,
                    building_base_elevation(config) + config.park_height,
                    central_park_tree_metrics,
                    glm::vec4(1.0f),
                    SourceSymbol{ "", "CentralParkTreeBark", module_layout.module_path });
                world.create_tree_leaves(
                    module_layout.park_center.x,
                    module_layout.park_center.y,
                    building_base_elevation(config) + config.park_height,
                    central_park_tree_metrics,
                    glm::vec4(1.0f),
                    SourceSymbol{ "", "CentralParkTreeLeaves", module_layout.module_path });
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
                    SourceSymbol{ "", module_layout.module_path, module_layout.module_path },
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
            const int building_side_count = procedural_building_side_count(
                incident_connection_count,
                config.connected_hex_building_threshold,
                config.connected_oct_building_threshold);
            world.create_building(
                building.center.x,
                building.center.y,
                building_base_elevation(config),
                building.metrics,
                glm::vec4(1.0f),
                SourceSymbol{ building.source_file_path, building.qualified_name, building.module_path },
                MaterialId::FlatColor,
                build_procedural_building_mesh(
                    building,
                    module_color,
                    config,
                    building_side_count),
                1.0f);

            if (sign_label_atlas)
            {
                const auto it = sign_label_atlas->entries.find(building_sign_key(building));
                if (it != sign_label_atlas->entries.end())
                {
                    const std::string& btext = building.display_name.empty() ? building.qualified_name : building.display_name;
                    const RoofSignPlacementSpec roof_sign
                        = place_building_roof_sign(building, btext, text_service, config, building_side_count);
                    const SignMetrics sign_metrics = make_sign_metrics(roof_sign, it->second);
                    const float cap_height = sign_metrics.height;

                    if (cap_height > 0.0f)
                    {
                        BuildingMetrics cap_metrics = building.metrics;
                        cap_metrics.height = cap_height;
                        world.create_building(
                            building.center.x,
                            building.center.y,
                            building_base_elevation(config) + building.metrics.height,
                            cap_metrics,
                            glm::vec4(1.0f),
                            SourceSymbol{ building.source_file_path, building.qualified_name, building.module_path },
                            MaterialId::FlatColor,
                            build_procedural_building_cap_mesh(
                                building,
                                module_color,
                                config,
                                building_side_count,
                                cap_height),
                            1.0f);
                    }

                    const float sign_y = building_base_elevation(config) + building.metrics.height + sign_metrics.height * 0.5f;
                    world.create_sign(
                        roof_sign.center.x,
                        roof_sign.center.y,
                        sign_y,
                        sign_metrics,
                        MeshId::Custom,
                        building_sign_board_color(config),
                        SourceSymbol{ building.source_file_path, building.qualified_name, building.module_path },
                        build_building_roof_sign_mesh(roof_sign));
                }
            }

            {
                const float inner_r = building.metrics.footprint * 0.5f;
                const float outer_r = inner_r + building.metrics.sidewalk_width;
                auto ring_mesh = std::make_shared<GeometryMesh>(generate_sidewalk_ring(
                    building_side_count, inner_r, outer_r,
                    0.0f, config.sidewalk_surface_height,
                    glm::vec3(kSidewalkSurfaceColor)));
                BuildingMetrics sidewalk_metrics;
                sidewalk_metrics.footprint = outer_r * 2.0f;
                sidewalk_metrics.height = config.sidewalk_surface_height;
                world.create_building(
                    building.center.x,
                    building.center.y,
                    config.sidewalk_surface_lift,
                    sidewalk_metrics,
                    kSidewalkSurfaceColor,
                    SourceSymbol{ building.source_file_path, building.qualified_name, building.module_path },
                    MaterialId::PavingSidewalk,
                    std::move(ring_mesh));
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
                        SourceSymbol{ "", module_layout.module_path, module_layout.module_path });
                }
            }
        }
    }

    DRAXUL_LOG_INFO(LogCategory::App,
        "CityBuilder: built semantic megacity with %zu modules and %zu buildings",
        layout->modules.size(),
        layout->building_count());

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
    SceneWorld& world,
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

        const float layered_route_elevation = route.elevation;
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
            constexpr float kMaxSubSegmentLength = 0.5f;
            const int sub_count = std::max(1, static_cast<int>(std::ceil(length / kMaxSubSegmentLength)));
            const float sub_length = length / static_cast<float>(sub_count);
            const glm::vec2 dir = delta / length;

            for (int sub = 0; sub < sub_count; ++sub)
            {
                const float frac0 = static_cast<float>(sub) / static_cast<float>(sub_count);
                const float frac1 = static_cast<float>(sub + 1) / static_cast<float>(sub_count);
                const glm::vec2 sub_a = a + dir * (frac0 * length);
                const glm::vec2 sub_b = a + dir * (frac1 * length);
                const float sub_mid_t = glm::mix(color_t_start, color_t_end, (frac0 + frac1) * 0.5f);
                const glm::vec4 color = glm::mix(route.source_color, route.target_color, sub_mid_t);

                world.create_route_segment(
                    (sub_a.x + sub_b.x) * 0.5f,
                    (sub_a.y + sub_b.y) * 0.5f,
                    RouteSegmentMetrics{
                        std::max(sub_length, route_width),
                        route_width,
                        kDependencyRouteHeight,
                        -std::atan2(delta.y, delta.x),
                    },
                    color,
                    SourceSymbol{},
                    layered_route_elevation,
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
