#include "city_builder.h"
#include "city_helpers.h"
#include "scene_world.h"
#include "semantic_city_layout.h"
#include "sign_label_atlas.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <draxul/citydb.h>
#include <draxul/log.h>
#include <draxul/megacity_code_config.h>
#include <draxul/text_service.h>
#include <filesystem>
#include <glm/gtc/constants.hpp>
#include <string>
#include <string_view>

namespace draxul
{

namespace
{

constexpr glm::vec3 kCatppuccinSurface0(0.192f, 0.196f, 0.266f);
constexpr std::array<glm::vec4, 26> kModuleAccentPalette = {
    glm::vec4(0.961f, 0.878f, 0.863f, 1.0f), // rosewater
    glm::vec4(0.949f, 0.804f, 0.804f, 1.0f), // flamingo
    glm::vec4(0.957f, 0.761f, 0.906f, 1.0f), // pink
    glm::vec4(0.796f, 0.651f, 0.969f, 1.0f), // mauve
    glm::vec4(0.953f, 0.545f, 0.659f, 1.0f), // red
    glm::vec4(0.922f, 0.627f, 0.675f, 1.0f), // maroon
    glm::vec4(0.980f, 0.702f, 0.529f, 1.0f), // peach
    glm::vec4(0.976f, 0.886f, 0.686f, 1.0f), // yellow
    glm::vec4(0.651f, 0.890f, 0.631f, 1.0f), // green
    glm::vec4(0.580f, 0.886f, 0.835f, 1.0f), // teal
    glm::vec4(0.537f, 0.863f, 0.922f, 1.0f), // sky
    glm::vec4(0.455f, 0.780f, 0.925f, 1.0f), // sapphire
    glm::vec4(0.537f, 0.706f, 0.980f, 1.0f), // blue
    glm::vec4(0.706f, 0.745f, 0.996f, 1.0f), // lavender
    glm::vec4(0.855f, 0.733f, 0.502f, 1.0f), // amber
    glm::vec4(0.643f, 0.827f, 0.502f, 1.0f), // lime
    glm::vec4(0.502f, 0.745f, 0.682f, 1.0f), // sage
    glm::vec4(0.749f, 0.565f, 0.827f, 1.0f), // orchid
    glm::vec4(0.890f, 0.643f, 0.584f, 1.0f), // coral
    glm::vec4(0.584f, 0.647f, 0.890f, 1.0f), // periwinkle
    glm::vec4(0.827f, 0.827f, 0.584f, 1.0f), // khaki
    glm::vec4(0.502f, 0.827f, 0.890f, 1.0f), // cyan
    glm::vec4(0.890f, 0.502f, 0.765f, 1.0f), // magenta
    glm::vec4(0.765f, 0.890f, 0.502f, 1.0f), // chartreuse
    glm::vec4(0.682f, 0.549f, 0.451f, 1.0f), // sienna
    glm::vec4(0.502f, 0.682f, 0.827f, 1.0f), // steel
};

constexpr glm::vec4 kSidewalkSurfaceColor(0.72f, 0.72f, 0.74f, 1.0f);
constexpr float kRoadSurfaceTextureLift = 0.002f;
constexpr float kRoadMaterialUvScale = 0.28f;

struct SignPlacementSpec
{
    glm::vec2 center{ 0.0f };
    float width = 1.0f;
    float height = 0.05f;
    float depth = 0.25f;
    float yaw_radians = 0.0f;
    MeshId mesh = MeshId::RoofSign;
};

glm::vec4 color_with_alpha(const glm::vec3& color, float alpha = 1.0f)
{
    return glm::vec4(
        std::clamp(color.r, 0.0f, 1.0f),
        std::clamp(color.g, 0.0f, 1.0f),
        std::clamp(color.b, 0.0f, 1.0f),
        alpha);
}

glm::vec4 module_sign_board_color(const MegaCityCodeConfig& config)
{
    return color_with_alpha(config.module_sign_board_color);
}

glm::vec4 building_sign_board_color(const MegaCityCodeConfig& config)
{
    return color_with_alpha(config.building_sign_board_color);
}

uint8_t color_channel_to_byte(float value)
{
    return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

uint32_t stable_module_hash(std::string_view text)
{
    uint32_t hash = 2166136261u;
    for (const unsigned char ch : text)
    {
        hash ^= ch;
        hash *= 16777619u;
    }
    return hash;
}

glm::vec4 module_building_color(std::string_view module_path)
{
    const uint32_t hash = stable_module_hash(module_path);
    const glm::vec4 base = kModuleAccentPalette[hash % kModuleAccentPalette.size()];
    const uint32_t variant = hash / static_cast<uint32_t>(kModuleAccentPalette.size());
    if ((variant % 3u) == 0u)
        return base;
    if ((variant % 3u) == 1u)
        return glm::vec4(glm::mix(glm::vec3(base), glm::vec3(1.0f), 0.10f), base.a);
    return glm::vec4(glm::mix(glm::vec3(base), kCatppuccinSurface0, 0.12f), base.a);
}

glm::vec4 module_building_layer_color(const glm::vec4& module_color, size_t layer_index)
{
    if ((layer_index % 2) == 0)
        return module_color;
    const glm::vec3 dark_band = glm::mix(glm::vec3(module_color), kCatppuccinSurface0, 0.28f);
    return glm::vec4(glm::clamp(dark_band, glm::vec3(0.0f), glm::vec3(1.0f)), module_color.a);
}

std::string module_display_name(std::string_view module_path)
{
    const std::filesystem::path path(module_path);
    const std::string leaf = path.filename().string();
    return !leaf.empty() ? leaf : std::string(module_path);
}

float clamp_sign_width(float available_width, float inset)
{
    return std::max(0.35f, available_width - 2.0f * inset);
}

float clamp_sign_depth(float available_depth, float inset, const MegaCityCodeConfig& config)
{
    const float max_depth = std::max(0.16f, available_depth - 2.0f * inset);
    return std::clamp(std::min(config.roof_sign_depth, max_depth), 0.16f, config.roof_sign_depth);
}

float clamp_wall_sign_height(const SemanticCityBuilding& building, const MegaCityCodeConfig& config)
{
    return std::max(0.6f, building.metrics.height - (config.wall_sign_top_inset + config.wall_sign_bottom_inset));
}

float clamp_wall_sign_width(const SemanticCityBuilding& building, const MegaCityCodeConfig& config)
{
    const float max_width = std::max(0.24f, building.metrics.footprint - 2.0f * config.wall_sign_side_inset);
    return std::clamp(config.wall_sign_width, 0.24f, max_width);
}

SignPlacementSpec place_roof_sign(const SemanticCityBuilding& building, const MegaCityCodeConfig& config)
{
    SignPlacementSpec placement;
    placement.width = clamp_sign_width(building.metrics.footprint, config.roof_sign_side_inset);
    placement.height = config.roof_sign_thickness;
    placement.depth = clamp_sign_depth(building.metrics.footprint, config.roof_sign_edge_inset, config);
    placement.mesh = MeshId::RoofSign;

    const float half_footprint = building.metrics.footprint * 0.5f;
    const float center_offset = half_footprint - placement.depth * 0.5f - config.roof_sign_edge_inset;

    switch (config.building_sign_placement)
    {
    case MegaCitySignPlacement::RoofNorth:
        placement.center = { building.center.x, building.center.y + center_offset };
        placement.yaw_radians = 0.0f;
        break;
    case MegaCitySignPlacement::RoofSouth:
        placement.center = { building.center.x, building.center.y - center_offset };
        placement.yaw_radians = 0.0f;
        break;
    case MegaCitySignPlacement::RoofEast:
        placement.center = { building.center.x + center_offset, building.center.y };
        placement.yaw_radians = glm::half_pi<float>();
        break;
    case MegaCitySignPlacement::RoofWest:
        placement.center = { building.center.x - center_offset, building.center.y };
        placement.yaw_radians = glm::half_pi<float>();
        break;
    default:
        break;
    }

    return placement;
}

SignPlacementSpec place_wall_sign(
    const SemanticCityBuilding& building, std::string_view text, const TextService* text_service,
    const MegaCityCodeConfig& config)
{
    SignPlacementSpec placement;
    const float footprint = building.metrics.footprint;
    const float max_height = clamp_wall_sign_height(building, config);

    const float char_height = footprint * 0.25f;
    float sign_width = char_height + 2.0f * config.wall_sign_side_inset;
    float sign_height = max_height;

    if (text_service && !text.empty())
    {
        const int cw = std::max(text_service->metrics().cell_width, 1);
        const int ch = std::max(text_service->metrics().cell_height, 1);
        const float aspect = static_cast<float>(cw) / static_cast<float>(ch);
        const float char_width = char_height * aspect;
        const float text_run = static_cast<float>(text.size()) * char_width;
        sign_height = std::min(max_height, text_run + 2.0f * config.wall_sign_side_inset);
    }

    placement.width = std::max(0.24f, sign_width);
    placement.height = std::max(0.24f, sign_height);
    placement.depth = config.wall_sign_thickness;
    placement.mesh = MeshId::WallSign;

    const float half_footprint = building.metrics.footprint * 0.5f;
    const float wall_offset = half_footprint + placement.depth * 0.5f + config.wall_sign_face_gap;

    switch (config.building_sign_placement)
    {
    case MegaCitySignPlacement::WallNorth:
        placement.center = { building.center.x, building.center.y + wall_offset };
        placement.yaw_radians = 0.0f;
        break;
    case MegaCitySignPlacement::WallSouth:
        placement.center = { building.center.x, building.center.y - wall_offset };
        placement.yaw_radians = glm::pi<float>();
        break;
    case MegaCitySignPlacement::WallEast:
        placement.center = { building.center.x + wall_offset, building.center.y };
        placement.yaw_radians = glm::half_pi<float>();
        break;
    case MegaCitySignPlacement::WallWest:
        placement.center = { building.center.x - wall_offset, building.center.y };
        placement.yaw_radians = -glm::half_pi<float>();
        break;
    default:
        break;
    }

    return placement;
}

std::array<SignPlacementSpec, 4> place_building_signs(
    const SemanticCityBuilding& building, std::string_view text, const TextService* text_service,
    const MegaCityCodeConfig& config)
{
    const float footprint = building.metrics.footprint;
    const float half_footprint = footprint * 0.5f;

    float sign_width = footprint;
    float sign_height = footprint * 0.25f;

    if (text_service && !text.empty())
    {
        const int cw = std::max(text_service->metrics().cell_width, 1);
        const int ch = std::max(text_service->metrics().cell_height, 1);
        const float aspect = static_cast<float>(ch) / static_cast<float>(cw);
        const float char_width = footprint / std::max(static_cast<float>(text.size()), 1.0f);
        sign_height = char_width * aspect + 2.0f * config.wall_sign_side_inset;
    }

    sign_width = std::max(0.35f, sign_width);
    sign_height = std::clamp(sign_height, 0.24f, building.metrics.height * 0.15f);

    const float wall_offset = half_footprint + config.wall_sign_thickness * 0.5f + config.wall_sign_face_gap;

    std::array<SignPlacementSpec, 4> signs;
    // North
    signs[0].width = sign_width;
    signs[0].height = sign_height;
    signs[0].depth = config.wall_sign_thickness;
    signs[0].mesh = MeshId::WallSign;
    signs[0].center = { building.center.x, building.center.y + wall_offset };
    signs[0].yaw_radians = 0.0f;
    // South
    signs[1].width = sign_width;
    signs[1].height = sign_height;
    signs[1].depth = config.wall_sign_thickness;
    signs[1].mesh = MeshId::WallSign;
    signs[1].center = { building.center.x, building.center.y - wall_offset };
    signs[1].yaw_radians = glm::pi<float>();
    // East
    signs[2].width = sign_width;
    signs[2].height = sign_height;
    signs[2].depth = config.wall_sign_thickness;
    signs[2].mesh = MeshId::WallSign;
    signs[2].center = { building.center.x + wall_offset, building.center.y };
    signs[2].yaw_radians = glm::half_pi<float>();
    // West
    signs[3].width = sign_width;
    signs[3].height = sign_height;
    signs[3].depth = config.wall_sign_thickness;
    signs[3].mesh = MeshId::WallSign;
    signs[3].center = { building.center.x - wall_offset, building.center.y };
    signs[3].yaw_radians = -glm::half_pi<float>();

    return signs;
}

SignPlacementSpec place_module_road_sign(
    const SemanticCityBuilding& building, std::string_view text, const TextService* text_service,
    const MegaCityCodeConfig& config)
{
    SignPlacementSpec placement;
    const float sidewalk_width = building.metrics.sidewalk_width;

    float sign_width = building.metrics.footprint;
    float sign_depth = sign_width * 0.25f;
    if (text_service && !text.empty())
    {
        const int cw = std::max(text_service->metrics().cell_width, 1);
        const int ch = std::max(text_service->metrics().cell_height, 1);
        const float aspect = static_cast<float>(ch) / static_cast<float>(cw);
        const float char_width = sign_width / std::max(static_cast<float>(text.size()), 1.0f);
        sign_depth = char_width * aspect + 2.0f * config.road_sign_edge_inset;
    }

    placement.width = std::max(0.35f, sign_width);
    placement.height = config.roof_sign_thickness;
    placement.depth = std::clamp(
        sign_depth, config.minimum_road_sign_depth,
        std::max(config.minimum_road_sign_depth, sidewalk_width - 2.0f * config.sidewalk_sign_edge_inset));
    placement.mesh = MeshId::RoofSign;

    const float half_footprint = building.metrics.footprint * 0.5f;
    const float center_offset = half_footprint + sidewalk_width * 0.5f;

    switch (config.building_sign_placement)
    {
    case MegaCitySignPlacement::RoofNorth:
    case MegaCitySignPlacement::WallNorth:
        placement.center = { building.center.x, building.center.y + center_offset };
        placement.yaw_radians = 0.0f;
        break;
    case MegaCitySignPlacement::RoofSouth:
    case MegaCitySignPlacement::WallSouth:
        placement.center = { building.center.x, building.center.y - center_offset };
        placement.yaw_radians = 0.0f;
        break;
    case MegaCitySignPlacement::RoofEast:
    case MegaCitySignPlacement::WallEast:
        placement.center = { building.center.x + center_offset, building.center.y };
        placement.yaw_radians = glm::half_pi<float>();
        break;
    case MegaCitySignPlacement::RoofWest:
    case MegaCitySignPlacement::WallWest:
        placement.center = { building.center.x - center_offset, building.center.y };
        placement.yaw_radians = glm::half_pi<float>();
        break;
    }

    return placement;
}

// Returns two signs for a park: [0] on the south edge facing north, [1] on the north edge facing south.
// Text on each side faces outward so it's readable from either direction.
std::array<SignPlacementSpec, 2> place_module_park_signs(
    glm::vec2 park_center, float park_footprint, std::string_view text, const TextService* text_service,
    const MegaCityCodeConfig& config)
{
    SignPlacementSpec base;

    float sign_width = park_footprint;
    float sign_depth = sign_width * 0.25f;
    if (text_service && !text.empty())
    {
        const int cw = std::max(text_service->metrics().cell_width, 1);
        const int ch = std::max(text_service->metrics().cell_height, 1);
        const float aspect = static_cast<float>(ch) / static_cast<float>(cw);
        const float char_width = sign_width / std::max(static_cast<float>(text.size()), 1.0f);
        sign_depth = char_width * aspect + 2.0f * config.road_sign_edge_inset;
    }
    base.width = std::max(0.35f, sign_width);
    base.height = config.roof_sign_thickness;
    base.depth = std::clamp(
        sign_depth, config.minimum_road_sign_depth, park_footprint * 0.33f);
    base.mesh = MeshId::RoofSign;

    const float half = park_footprint * 0.5f;

    // South edge, facing south (yaw = π) — readable approaching from the south
    SignPlacementSpec south = base;
    south.center = { park_center.x, park_center.y - half + base.depth * 0.5f };
    south.yaw_radians = glm::pi<float>();

    // North edge, facing north (yaw = 0) — readable approaching from the north
    SignPlacementSpec north = base;
    north.center = { park_center.x, park_center.y + half - base.depth * 0.5f };
    north.yaw_radians = 0.0f;

    return { south, north };
}

SignLabelRequest make_sign_request(
    std::string key, std::string_view text, const SignPlacementSpec& placement,
    const TextService* text_service, const MegaCityCodeConfig& config)
{
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

    const bool building_sign = placement.mesh == MeshId::WallSign;
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

} // namespace

CityBuildResult build_city(
    SceneWorld& world,
    CityDatabase& city_db,
    TextService* text_service,
    const std::vector<std::string>& available_modules,
    const MegaCityCodeConfig& config,
    uint64_t& sign_label_revision)
{
    CityBuildResult result;

    std::vector<SemanticCityModuleInput> modules;
    for (const std::string& module_path : available_modules)
    {
        if (!config.selected_module_path.empty()
            && module_path != config.selected_module_path)
            continue;
        const CityModuleRecord mod_record = city_db.module_record(module_path);
        modules.push_back({ module_path, city_db.list_classes_in_module(module_path), mod_record.quality, mod_record.health });
    }

    auto semantic_model = std::make_shared<SemanticMegacityModel>(
        build_semantic_megacity_model(modules, config));
    semantic_model->codebase_health = city_db.codebase_health();
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
            const auto signs = place_building_signs(building, text, text_service, config);
            sign_requests.push_back(make_sign_request(building_sign_key(building), text, signs[0], text_service, config));
        }

        if (module_layout.park_footprint > 0.0f)
        {
            const std::string name = module_display_name(module_layout.module_path);
            const auto park_signs = place_module_park_signs(
                module_layout.park_center, module_layout.park_footprint,
                name, text_service, config);
            // Both signs share the same atlas entry (same text/key).
            auto request = make_sign_request(
                module_sign_key(module_layout.module_path), name, park_signs[0], text_service, config);
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
    const CitySurfaceBounds road_surface_bounds = compute_city_road_surface_bounds(*layout);
    if (road_surface_bounds.valid())
    {
        world.create_road_surface(
            (road_surface_bounds.min_x + road_surface_bounds.max_x) * 0.5f,
            (road_surface_bounds.min_z + road_surface_bounds.max_z) * 0.5f,
            RoadSurfaceMetrics{
                road_surface_bounds.max_x - road_surface_bounds.min_x,
                road_surface_bounds.max_z - road_surface_bounds.min_z,
                kRoadMaterialUvScale,
                1.0f,
                1.0f,
            },
            SourceSymbol{},
            config.road_surface_height + kRoadSurfaceTextureLift);
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
                SourceSymbol{ "", module_layout.module_path },
                MaterialId::FlatColor);

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
                    SourceSymbol{ "", module_layout.module_path },
                    config.sidewalk_surface_lift);
            }
        }

        const glm::vec4 module_color = module_building_color(module_layout.module_path);
        for (const auto& building : module_layout.buildings)
        {
            float layer_base_y = building_base_elevation(config);
            if (building.layers.empty())
            {
                world.create_building(
                    building.center.x,
                    building.center.y,
                    building_base_elevation(config),
                    building.metrics,
                    module_color,
                    SourceSymbol{ building.source_file_path, building.qualified_name });
            }
            else
            {
                for (size_t layer_index = 0; layer_index < building.layers.size(); ++layer_index)
                {
                    const SemanticBuildingLayer& layer = building.layers[layer_index];
                    if (layer.height <= 0.0f)
                        continue;

                    BuildingMetrics layer_metrics = building.metrics;
                    layer_metrics.height = layer.height;
                    world.create_building(
                        building.center.x,
                        building.center.y,
                        layer_base_y,
                        layer_metrics,
                        module_building_layer_color(module_color, layer_index),
                        SourceSymbol{ building.source_file_path, building.qualified_name });
                    layer_base_y += layer.height;
                }
            }

            if (sign_label_atlas)
            {
                const auto it = sign_label_atlas->entries.find(building_sign_key(building));
                if (it != sign_label_atlas->entries.end())
                {
                    const std::string& btext = building.display_name.empty() ? building.qualified_name : building.display_name;
                    const auto face_signs = place_building_signs(building, btext, text_service, config);
                    const SignMetrics first_sign = make_sign_metrics(face_signs[0], it->second);
                    const float cap_height = first_sign.height;

                    if (cap_height > 0.0f)
                    {
                        BuildingMetrics cap_metrics = building.metrics;
                        cap_metrics.height = cap_height;
                        world.create_building(
                            building.center.x,
                            building.center.y,
                            building_base_elevation(config) + building.metrics.height,
                            cap_metrics,
                            module_color,
                            SourceSymbol{ building.source_file_path, building.qualified_name });
                    }

                    const float total_height = building_base_elevation(config) + building.metrics.height + cap_height;
                    for (const SignPlacementSpec& placement : face_signs)
                    {
                        const SignMetrics sign = make_sign_metrics(placement, it->second);
                        const float sign_y = total_height - sign.height * 0.5f;
                        const glm::vec4 sign_color
                            = placement.mesh == MeshId::RoofSign ? module_sign_board_color(config)
                                                                 : building_sign_board_color(config);
                        world.create_sign(
                            placement.center.x,
                            placement.center.y,
                            sign_y,
                            sign,
                            placement.mesh,
                            sign_color,
                            SourceSymbol{ building.source_file_path, building.qualified_name });
                    }
                }
            }

            for (const RoadSegmentPlacement& sidewalk : build_sidewalk_segments(building))
            {
                world.create_road(
                    sidewalk.center.x,
                    sidewalk.center.y,
                    RoadMetrics{ sidewalk.extent.x, sidewalk.extent.y, config.sidewalk_surface_height },
                    kSidewalkSurfaceColor,
                    SourceSymbol{ building.source_file_path, building.qualified_name },
                    config.sidewalk_surface_lift);
            }
        }

        if (sign_label_atlas && module_layout.park_footprint > 0.0f)
        {
            const auto it = sign_label_atlas->entries.find(module_sign_key(module_layout.module_path));
            if (it != sign_label_atlas->entries.end())
            {
                const std::string name = module_display_name(module_layout.module_path);
                const auto park_signs = place_module_park_signs(
                    module_layout.park_center, module_layout.park_footprint,
                    name, text_service, config);

                // Place both signs (south and north edges) so text is readable from either side.
                for (const SignPlacementSpec& park_sign : park_signs)
                {
                    const SignMetrics sign = make_sign_metrics(park_sign, it->second);
                    world.create_sign(
                        park_sign.center.x,
                        park_sign.center.y,
                        building_base_elevation(config)
                            + config.park_height
                            + sign.height * 0.5f
                            + config.road_sign_lift,
                        sign,
                        park_sign.mesh,
                        module_sign_board_color(config),
                        SourceSymbol{ "", module_layout.module_path });
                }
            }
        }
    }

    DRAXUL_LOG_INFO(LogCategory::App,
        "CityBuilder: built semantic megacity with %zu modules and %zu buildings",
        layout->modules.size(),
        layout->building_count());

    result.semantic_model = std::move(semantic_model);
    result.sign_label_atlas = std::move(sign_label_atlas);
    result.layout = std::move(layout);
    return result;
}

} // namespace draxul
