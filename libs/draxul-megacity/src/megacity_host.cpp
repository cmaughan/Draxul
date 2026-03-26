#include "isometric_camera.h"
#include "isometric_scene_pass.h"
#include "scene_world.h"
#include "semantic_city_layout.h"
#include "sign_label_atlas.h"
#include "ui_city_map_panel.h"
#include "ui_treesitter_panel.h"
#include <SDL3/SDL.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <draxul/log.h>
#include <draxul/megacity_host.h>
#include <draxul/text_service.h>
#include <filesystem>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <limits>

#ifndef DRAXUL_REPO_ROOT
#define DRAXUL_REPO_ROOT "."
#endif

namespace draxul
{

namespace
{

constexpr float kMovementSpeedFractionPerSecond = 0.35f;
constexpr float kOrbitSpeedRadiansPerSecond = 1.8f;
constexpr float kZoomSpeedPerSecond = 1.35f;
constexpr float kPitchSpeedRadiansPerSecond = 0.9f;
constexpr float kOrbitDragReferencePixelsPerSecond = 240.0f;
constexpr float kOrbitDragRadiansPerPixel = kOrbitSpeedRadiansPerSecond / kOrbitDragReferencePixelsPerSecond;
constexpr float kDragCatchUpRatePerSecond = 30.0f;
constexpr float kDragPanSettleEpsilon = 1e-4f;
constexpr float kDragOrbitSettleEpsilon = 1e-4f;
constexpr auto kMovementTick = std::chrono::milliseconds(16);
constexpr auto kDragSmoothingTick = std::chrono::milliseconds(8);
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

constexpr glm::vec4 kRoadColor(0.46f, 0.46f, 0.48f, 1.0f);
constexpr glm::vec4 kSidewalkSurfaceColor(0.72f, 0.72f, 0.74f, 1.0f);
constexpr glm::vec4 kRoadSurfaceColor(0.18f, 0.18f, 0.19f, 1.0f);
constexpr glm::vec4 kSignBoardColor(1.0f, 1.0f, 1.0f, 1.0f);
constexpr glm::vec4 kBuildingSignColor = kSignBoardColor;
constexpr glm::vec4 kModuleSignColor = kSignBoardColor;

struct SignPlacementSpec
{
    glm::vec2 center{ 0.0f };
    float width = 1.0f;
    float height = 0.05f;
    float depth = 0.25f;
    float yaw_radians = 0.0f;
    MeshId mesh = MeshId::RoofSign;
};

float building_base_elevation(const MegaCityCodeConfig& config)
{
    return config.sidewalk_surface_lift + config.sidewalk_surface_height;
}

float world_floor_height(const MegaCityCodeConfig& config)
{
    return config.road_surface_height * config.world_floor_height_scale;
}

MegaCityCodeConfig world_rebuild_signature(MegaCityCodeConfig config)
{
    config.auto_rebuild = false;
    config.sign_text_hidden_px = 0.0f;
    config.sign_text_full_px = 0.0f;
    config.output_gamma = 1.0f;
    config.world_floor_height_scale = 0.0f;
    config.world_floor_top_y = 0.0f;
    config.world_floor_grid_y_offset = 0.0f;
    config.world_floor_grid_tile_scale = 0.0f;
    config.world_floor_grid_line_width = 0.0f;
    config.ambient_strength = 0.0f;
    config.directional_light_x = 0.0f;
    config.directional_light_y = 0.0f;
    config.directional_light_z = 0.0f;
    config.point_light_position_valid = false;
    config.point_light_x = 0.0f;
    config.point_light_y = 0.0f;
    config.point_light_z = 0.0f;
    config.point_light_radius = 0.0f;
    config.point_light_brightness = 0.0f;
    config.camera_state_valid = false;
    config.camera_target_x = 0.0f;
    config.camera_target_z = 0.0f;
    config.camera_yaw = 0.0f;
    config.camera_pitch = 0.0f;
    config.camera_orbit_radius = 0.0f;
    config.camera_zoom_half_height = 0.0f;
    return config;
}

bool requires_world_rebuild(const MegaCityCodeConfig& before, const MegaCityCodeConfig& after)
{
    return world_rebuild_signature(before) != world_rebuild_signature(after);
}

std::filesystem::path megacity_db_path()
{
    const std::filesystem::path repo_root = std::filesystem::path(DRAXUL_REPO_ROOT);
    if (const char* base_path_raw = SDL_GetBasePath())
    {
        const std::filesystem::path base_path(base_path_raw);

        std::error_code ec;
        const std::filesystem::path canonical_base = std::filesystem::weakly_canonical(base_path, ec);
        const std::filesystem::path canonical_repo = std::filesystem::weakly_canonical(repo_root, ec);
        if (!canonical_base.empty() && !canonical_repo.empty())
        {
            const std::string base_string = canonical_base.generic_string();
            const std::string repo_string = canonical_repo.generic_string();
            if (base_string == repo_string
                || (base_string.size() > repo_string.size()
                    && base_string.compare(0, repo_string.size(), repo_string) == 0
                    && base_string[repo_string.size()] == '/'))
            {
                return repo_root / "db" / "megacity.sqlite3";
            }
        }
    }

#ifdef __APPLE__
    const char* home = std::getenv("HOME");
    const std::filesystem::path base = home ? std::filesystem::path(home) : repo_root;
    return base / "Library" / "Application Support" / "draxul" / "megacity.sqlite3";
#else
    return repo_root / "db" / "megacity.sqlite3";
#endif
}

bool is_left_arrow(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_LEFT || event.keycode == SDLK_LEFT
        || event.scancode == SDL_SCANCODE_A || event.keycode == SDLK_A;
}

bool is_right_arrow(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_RIGHT || event.keycode == SDLK_RIGHT
        || event.scancode == SDL_SCANCODE_D || event.keycode == SDLK_D;
}

bool is_up_arrow(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_UP || event.keycode == SDLK_UP
        || event.scancode == SDL_SCANCODE_W || event.keycode == SDLK_W;
}

bool is_down_arrow(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_DOWN || event.keycode == SDLK_DOWN
        || event.scancode == SDL_SCANCODE_S || event.keycode == SDLK_S;
}

bool is_orbit_left_key(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_Q || event.keycode == SDLK_Q;
}

bool is_orbit_right_key(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_E || event.keycode == SDLK_E;
}

bool is_zoom_in_key(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_R || event.keycode == SDLK_R;
}

bool is_zoom_out_key(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_F || event.keycode == SDLK_F;
}

bool is_pitch_up_key(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_T || event.keycode == SDLK_T;
}

bool is_pitch_down_key(const KeyEvent& event)
{
    return event.scancode == SDL_SCANCODE_G || event.keycode == SDLK_G;
}

float drag_catch_up_alpha(float dt)
{
    if (dt <= 0.0f)
        return 0.0f;
    return 1.0f - std::exp(-kDragCatchUpRatePerSecond * dt);
}

void clamp_small_pan(glm::vec2& pan)
{
    if (glm::dot(pan, pan) <= kDragPanSettleEpsilon * kDragPanSettleEpsilon)
        pan = glm::vec2(0.0f);
}

void clamp_small_orbit(float& orbit)
{
    if (std::abs(orbit) <= kDragOrbitSettleEpsilon)
        orbit = 0.0f;
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

    // Text height (cross-text dimension) = 1/4 of building width.
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

// Returns 4 wall signs, one per face, text running horizontally across each side,
// aligned with the top of the building.
std::array<SignPlacementSpec, 4> place_building_signs(
    const SemanticCityBuilding& building, std::string_view text, const TextService* text_service,
    const MegaCityCodeConfig& config)
{
    const float footprint = building.metrics.footprint;
    const float half_footprint = footprint * 0.5f;

    // Text spans the full building face; derive sign height from font aspect ratio.
    float sign_width = footprint; // along the wall face
    float sign_height = footprint * 0.25f; // fallback

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

    // Width = building footprint; depth from font aspect ratio, clamped.
    float sign_width = building.metrics.footprint;
    float sign_depth = sign_width * 0.25f; // fallback
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

    // Place at the center of the sidewalk ring just beyond the building edge.
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

SignLabelRequest make_sign_request(
    std::string key, std::string_view text, const SignPlacementSpec& placement,
    const TextService* text_service, const MegaCityCodeConfig& config)
{
    // Small bitmap at native font size — UVs stretch it to fill the sign.
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

    return SignLabelRequest{
        .key = std::move(key),
        .text = std::string(text),
        .target_pixel_width = pixel_width,
        .target_pixel_height = pixel_height,
        .vertical_align = SignLabelVerticalAlign::Center,
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

MegaCityHost::MegaCityHost() = default;

MegaCityHost::~MegaCityHost()
{
    if (grid_thread_.joinable())
        grid_thread_.join();
}

void MegaCityHost::refresh_sign_text_service()
{
    sign_label_atlas_.reset();
    if (sign_font_path_.empty())
    {
        sign_text_service_.reset();
        return;
    }

    if (!sign_text_service_)
        sign_text_service_ = std::make_unique<TextService>();
    else
        sign_text_service_->shutdown();

    TextServiceConfig sign_text_config;
    sign_text_config.font_path = sign_font_path_;
    sign_text_config.enable_ligatures = false;
    if (sign_text_service_->initialize(
            sign_text_config,
            std::max(renderer_config_.sign_label_point_size, 1.0f),
            display_ppi_))
    {
        DRAXUL_LOG_INFO(LogCategory::App, "MegaCityHost: sign label text service initialized");
    }
    else
    {
        sign_text_service_.reset();
        DRAXUL_LOG_WARN(LogCategory::App,
            "MegaCityHost: sign label text service unavailable; rooftop labels disabled");
    }
}

bool MegaCityHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    callbacks_ = &callbacks;
    config_document_ = context.config_document;
    renderer_defaults_ = config_document_
        ? load_megacity_code_defaults(*config_document_)
        : MegaCityCodeConfig{};
    renderer_config_ = config_document_
        ? load_megacity_code_config(*config_document_, renderer_defaults_)
        : renderer_defaults_;
    pending_renderer_config_ = renderer_config_;
    restore_camera_after_initial_build_ = renderer_config_.camera_state_valid;
    sign_font_path_ = context.text_service->primary_font_path();
    display_ppi_ = context.display_ppi;
    viewport_ = context.initial_viewport;
    pixel_w_ = viewport_.pixel_size.x > 0 ? viewport_.pixel_size.x : 800;
    pixel_h_ = viewport_.pixel_size.y > 0 ? viewport_.pixel_size.y : 600;

    world_ = std::make_unique<SceneWorld>();
    camera_ = std::make_unique<IsometricCamera>();
    camera_->set_viewport(pixel_w_, pixel_h_);
    camera_->frame_world_bounds(-2.5f, 2.5f, -2.5f, 2.5f);
    scene_pass_ = std::make_shared<IsometricScenePass>(1, 1, world_->tile_size());
    refresh_sign_text_service();

    running_ = true;
    city_db_reconciled_ = false;
    world_rebuild_pending_ = false;
    city_bounds_valid_ = false;
    last_activity_time_ = std::chrono::steady_clock::now();
    last_pump_time_ = last_activity_time_;
    const std::filesystem::path city_db_path = megacity_db_path();
    if (!city_db_.open(city_db_path))
    {
        DRAXUL_LOG_WARN(LogCategory::App, "MegaCityHost: failed to open city DB at %s: %s",
            city_db_path.string().c_str(), city_db_.last_error().c_str());
    }
    scanner_.start(DRAXUL_REPO_ROOT);
    mark_scene_dirty();

    DRAXUL_LOG_INFO(LogCategory::App, "MegaCityHost initialized (%dx%d), scanning %s, city DB %s",
        pixel_w_, pixel_h_, DRAXUL_REPO_ROOT, city_db_path.string().c_str());
    return true;
}

void MegaCityHost::mark_scene_dirty()
{
    scene_dirty_ = true;
    last_activity_time_ = std::chrono::steady_clock::now();
    if (callbacks_)
        callbacks_->request_frame();
}

void MegaCityHost::mark_world_rebuild_pending()
{
    world_rebuild_pending_ = true;
    last_activity_time_ = std::chrono::steady_clock::now();
    if (callbacks_)
        callbacks_->request_frame();
}

void MegaCityHost::sync_camera_state_to_configs()
{
    if (!camera_)
        return;

    const IsometricCameraState state = camera_->state();
    auto write_state = [&](MegaCityCodeConfig& config) {
        config.camera_state_valid = true;
        config.camera_target_x = state.target.x;
        config.camera_target_z = state.target.z;
        config.camera_yaw = state.yaw;
        config.camera_pitch = state.pitch;
        config.camera_orbit_radius = state.orbit_radius;
        config.camera_zoom_half_height = state.zoom_half_height;
    };

    write_state(renderer_config_);
    write_state(pending_renderer_config_);
}

void MegaCityHost::reset_camera_to_default_frame()
{
    if (!camera_)
        return;

    if (city_bounds_valid_)
        camera_->frame_world_bounds(city_min_x_, city_max_x_, city_min_z_, city_max_z_);
    else
        camera_->frame_world_bounds(-2.5f, 2.5f, -2.5f, 2.5f);

    restore_camera_after_initial_build_ = false;
    sync_camera_state_to_configs();
    mark_scene_dirty();
}

void MegaCityHost::on_key(const KeyEvent& event)
{
    bool changed = false;
    if (is_left_arrow(event))
    {
        changed = move_left_ != event.pressed;
        move_left_ = event.pressed;
    }
    else if (is_right_arrow(event))
    {
        changed = move_right_ != event.pressed;
        move_right_ = event.pressed;
    }
    else if (is_up_arrow(event))
    {
        changed = move_up_ != event.pressed;
        move_up_ = event.pressed;
    }
    else if (is_down_arrow(event))
    {
        changed = move_down_ != event.pressed;
        move_down_ = event.pressed;
    }
    else if (is_orbit_left_key(event))
    {
        changed = orbit_left_ != event.pressed;
        orbit_left_ = event.pressed;
    }
    else if (is_orbit_right_key(event))
    {
        changed = orbit_right_ != event.pressed;
        orbit_right_ = event.pressed;
    }
    else if (is_zoom_in_key(event))
    {
        changed = zoom_in_ != event.pressed;
        zoom_in_ = event.pressed;
    }
    else if (is_zoom_out_key(event))
    {
        changed = zoom_out_ != event.pressed;
        zoom_out_ = event.pressed;
    }
    else if (is_pitch_up_key(event))
    {
        changed = pitch_up_ != event.pressed;
        pitch_up_ = event.pressed;
    }
    else if (is_pitch_down_key(event))
    {
        changed = pitch_down_ != event.pressed;
        pitch_down_ = event.pressed;
    }
    else
    {
        return;
    }

    if (!changed)
        return;

    last_pump_time_ = std::chrono::steady_clock::now();
    mark_scene_dirty();
}

void MegaCityHost::on_mouse_move(const MouseMoveEvent& event)
{
    if (!dragging_scene_ || !camera_)
        return;

    if (SDL_WasInit(SDL_INIT_VIDEO) != 0
        && (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON_LMASK) == 0)
    {
        dragging_scene_ = false;
        return;
    }

    glm::vec2 pixel_delta = event.delta;
    if (glm::dot(pixel_delta, pixel_delta) <= 0.0f)
    {
        const glm::ivec2 fallback_delta = event.pos - last_drag_pos_;
        pixel_delta = glm::vec2(static_cast<float>(fallback_delta.x), static_cast<float>(fallback_delta.y));
    }
    last_drag_pos_ = event.pos;
    if (glm::dot(pixel_delta, pixel_delta) <= 0.0f)
        return;

    if ((event.mod & kModAlt) != 0)
    {
        if (pixel_delta.x != 0.0f)
        {
            pending_drag_orbit_ += -pixel_delta.x * kOrbitDragRadiansPerPixel;
            last_activity_time_ = std::chrono::steady_clock::now();
            if (callbacks_)
                callbacks_->request_frame();
        }
        return;
    }

    const glm::vec2 pan = camera_->pan_delta_for_screen_drag(pixel_delta);
    if (glm::dot(pan, pan) <= 0.0f)
        return;

    pending_drag_pan_ += pan;
    last_activity_time_ = std::chrono::steady_clock::now();
    if (callbacks_)
        callbacks_->request_frame();
}

void MegaCityHost::on_mouse_button(const MouseButtonEvent& event)
{
    if (event.button != SDL_BUTTON_LEFT)
        return;

    dragging_scene_ = event.pressed;
    last_drag_pos_ = event.pos;
}

void MegaCityHost::on_mouse_wheel(const MouseWheelEvent& /*event*/)
{
    // The Megacity view currently has no wheel-driven zoom behavior.
}

void MegaCityHost::set_imgui_font(const std::string&, float)
{
    // Megacity uses the shared app ImGui context and does not own fonts itself.
}

void MegaCityHost::render_imgui(float dt)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(pixel_w_), static_cast<float>(pixel_h_));
    io.DeltaTime = dt > 0.0f ? dt : (1.0f / 60.0f);

    const ImGuiWindowFlags ds_flags = ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus
        | ImGuiWindowFlags_NoBackground;
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(pixel_w_), static_cast<float>(pixel_h_)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##dockspace_root", nullptr, ds_flags);
    ImGui::PopStyleVar(3);
    ImGui::DockSpace(ImGui::GetID("MegaCityDock"), ImVec2(0.0f, 0.0f),
        ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    if (scene_pass_)
        scene_pass_->render_gbuffer_debug_ui();

    // City map overview panel
    {
        std::shared_ptr<const CityGrid> grid;
        {
            std::lock_guard<std::mutex> lock(grid_mutex_);
            grid = city_grid_;
        }
        render_city_map_panel(grid, grid_build_in_progress_.load());
    }

    MegacityRendererControls renderer_controls{
        .config = pending_renderer_config_,
        .defaults = renderer_defaults_,
        .rebuild_pending = world_rebuild_pending_
            || requires_world_rebuild(renderer_config_, pending_renderer_config_),
    };
    if (render_treesitter_panel(
            pixel_w_, pixel_h_, scanner_.snapshot(), semantic_model_.get(), &renderer_controls))
    {
        if (renderer_controls.reset_camera_requested)
            reset_camera_to_default_frame();

        const MegaCityCodeConfig previous_pending = pending_renderer_config_;
        pending_renderer_config_ = renderer_controls.config;
        renderer_defaults_ = renderer_controls.defaults;
        const bool pending_changed = previous_pending != pending_renderer_config_;
        const bool world_rebuild_needed = requires_world_rebuild(renderer_config_, pending_renderer_config_);

        if (world_rebuild_needed)
        {
            if (pending_changed)
                mark_world_rebuild_pending();
        }
        else if (pending_changed)
        {
            renderer_config_ = pending_renderer_config_;
            mark_scene_dirty();
        }

        const bool auto_rebuild_requested = renderer_controls.committed_edit
            && pending_renderer_config_.auto_rebuild
            && world_rebuild_needed;
        if (renderer_controls.rebuild_requested || auto_rebuild_requested)
        {
            renderer_config_ = pending_renderer_config_;
            if (renderer_controls.rebuild_requested && config_document_)
            {
                store_megacity_code_config(*config_document_, pending_renderer_config_, renderer_defaults_);
                config_document_->save();
            }
            refresh_sign_text_service();
            if (city_db_reconciled_)
                rebuild_semantic_city();
            else
                mark_world_rebuild_pending();
        }

        if (renderer_controls.set_defaults_requested && config_document_)
        {
            store_megacity_code_config(*config_document_, pending_renderer_config_, renderer_defaults_);
            config_document_->save();
        }
    }
}

void MegaCityHost::attach_3d_renderer(I3DRenderer& renderer)
{
    renderer_3d_ = &renderer;
    renderer_3d_->register_render_pass(scene_pass_);
    renderer_3d_->set_3d_viewport(viewport_.pixel_pos.x, viewport_.pixel_pos.y, pixel_w_, pixel_h_);

    if (scene_pass_)
    {
        scene_pass_->set_scene(build_scene_snapshot());
        scene_dirty_ = false;
    }
    if (callbacks_)
        callbacks_->request_frame();

    DRAXUL_LOG_INFO(LogCategory::App, "MegaCityHost: 3D renderer attached, scene pass registered");
}

void MegaCityHost::detach_3d_renderer()
{
    if (renderer_3d_)
    {
        renderer_3d_->unregister_render_pass();
        renderer_3d_ = nullptr;
    }
}

void MegaCityHost::shutdown()
{
    if (grid_thread_.joinable())
        grid_thread_.join();
    city_grid_.reset();

    if (config_document_)
        store_megacity_code_config(*config_document_, pending_renderer_config_, renderer_defaults_);
    scanner_.stop();
    city_db_.close();
    if (sign_text_service_)
    {
        sign_text_service_->shutdown();
        sign_text_service_.reset();
    }
    sign_label_atlas_.reset();
    semantic_model_.reset();
    detach_3d_renderer();
    scene_pass_.reset();
    camera_.reset();
    world_.reset();
    running_ = false;
}

bool MegaCityHost::is_running() const
{
    return running_;
}

std::string MegaCityHost::init_error() const
{
    return {};
}

void MegaCityHost::set_viewport(const HostViewport& viewport)
{
    viewport_ = viewport;
    pixel_w_ = viewport.pixel_size.x > 0 ? viewport.pixel_size.x : pixel_w_;
    pixel_h_ = viewport.pixel_size.y > 0 ? viewport.pixel_size.y : pixel_h_;

    if (camera_)
        camera_->set_viewport(pixel_w_, pixel_h_);
    if (renderer_3d_)
        renderer_3d_->set_3d_viewport(viewport_.pixel_pos.x, viewport_.pixel_pos.y, pixel_w_, pixel_h_);

    mark_scene_dirty();
}

SceneSnapshot MegaCityHost::build_scene_snapshot() const
{
    SceneSnapshot scene;
    if (!camera_ || !world_)
        return scene;

    scene.camera.view = camera_->view_matrix();
    scene.camera.proj = camera_->proj_matrix();
    scene.camera.light_dir = glm::normalize(glm::vec4(
        renderer_config_.directional_light_x,
        renderer_config_.directional_light_y,
        renderer_config_.directional_light_z,
        0.0f));
    scene.camera.label_fade_px = glm::vec4(
        renderer_config_.sign_text_hidden_px,
        renderer_config_.sign_text_full_px,
        0.0f,
        0.0f);
    scene.camera.render_tuning = glm::vec4(
        renderer_config_.output_gamma,
        renderer_config_.point_light_brightness,
        renderer_config_.ambient_strength,
        0.0f);

    const GroundFootprint footprint = camera_->visible_ground_footprint(0.0f);
    const float tile_size = world_->tile_size();
    const float grid_tile_size = tile_size * renderer_config_.world_floor_grid_tile_scale;
    scene.floor_grid.enabled = true;
    scene.floor_grid.min_x = static_cast<int>(std::floor(footprint.min_x / grid_tile_size)) - 1;
    scene.floor_grid.max_x = static_cast<int>(std::ceil(footprint.max_x / grid_tile_size)) + 1;
    scene.floor_grid.min_z = static_cast<int>(std::floor(footprint.min_z / grid_tile_size)) - 1;
    scene.floor_grid.max_z = static_cast<int>(std::ceil(footprint.max_z / grid_tile_size)) + 1;
    scene.floor_grid.tile_size = grid_tile_size;
    scene.floor_grid.line_width = tile_size * renderer_config_.world_floor_grid_line_width;
    scene.floor_grid.y = renderer_config_.world_floor_top_y
        - world_floor_height(renderer_config_)
        - renderer_config_.world_floor_grid_y_offset;
    scene.floor_grid.color = glm::vec4(0.62f, 0.62f, 0.66f, 1.0f);
    if (sign_label_atlas_)
        scene.label_atlas = std::shared_ptr<const LabelAtlasData>(sign_label_atlas_, &sign_label_atlas_->image);

    // Query the ECS registry for all entities with position + appearance.
    const auto& reg = world_->registry();
    auto view = reg.view<const WorldPosition, const Elevation, const Appearance>();
    float min_x = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float min_z = std::numeric_limits<float>::max();
    float max_z = std::numeric_limits<float>::lowest();
    float building_min_x = std::numeric_limits<float>::max();
    float building_max_x = std::numeric_limits<float>::lowest();
    float building_min_z = std::numeric_limits<float>::max();
    float building_max_z = std::numeric_limits<float>::lowest();
    float max_building_lot_margin = 0.0f;
    for (auto [entity, pos, elev, appearance] : view.each())
    {
        SceneObject obj;
        obj.mesh = appearance.mesh;
        const glm::vec3 world_pos{ pos.x, elev.value, pos.z };
        float extent_x = 1.0f;
        float extent_z = 1.0f;

        // Scale the cube by building metrics if present.
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), world_pos);
        if (const auto* bm = reg.try_get<BuildingMetrics>(entity))
        {
            extent_x = bm->footprint;
            extent_z = bm->footprint;
            // Shift up by half the height so the base sits on the ground plane.
            transform = glm::translate(transform, glm::vec3(0.0f, bm->height * 0.5f, 0.0f));
            transform = glm::scale(transform, glm::vec3(bm->footprint, bm->height, bm->footprint));
            building_min_x = std::min(building_min_x, pos.x - bm->footprint * 0.5f);
            building_max_x = std::max(building_max_x, pos.x + bm->footprint * 0.5f);
            building_min_z = std::min(building_min_z, pos.z - bm->footprint * 0.5f);
            building_max_z = std::max(building_max_z, pos.z + bm->footprint * 0.5f);
            max_building_lot_margin = std::max(
                max_building_lot_margin, bm->sidewalk_width + bm->road_width);
        }
        else if (const auto* rm = reg.try_get<RoadMetrics>(entity))
        {
            extent_x = rm->extent_x;
            extent_z = rm->extent_z;
            transform = glm::translate(transform, glm::vec3(0.0f, rm->height * 0.5f, 0.0f));
            transform = glm::scale(transform, glm::vec3(rm->extent_x, rm->height, rm->extent_z));
        }
        else if (const auto* sm = reg.try_get<SignMetrics>(entity))
        {
            const bool quarter_turn = std::abs(std::sin(sm->yaw_radians)) > 0.70710678f;
            extent_x = quarter_turn ? sm->depth : sm->width;
            extent_z = quarter_turn ? sm->width : sm->depth;
            transform = glm::rotate(transform, sm->yaw_radians, glm::vec3(0.0f, 1.0f, 0.0f));
            transform = glm::scale(transform, glm::vec3(sm->width, sm->height, sm->depth));
            obj.uv_rect = sm->uv_rect;
            obj.label_ink_pixel_size = sm->label_ink_pixel_size;
        }
        else
        {
            transform = glm::translate(transform, glm::vec3(0.0f, 0.5f, 0.0f));
        }

        obj.world = transform;
        obj.color = appearance.color;
        scene.objects.push_back(obj);

        min_x = std::min(min_x, pos.x - extent_x * 0.5f);
        max_x = std::max(max_x, pos.x + extent_x * 0.5f);
        min_z = std::min(min_z, pos.z - extent_z * 0.5f);
        max_z = std::max(max_z, pos.z + extent_z * 0.5f);
    }

    if (building_min_x <= building_max_x && building_min_z <= building_max_z)
    {
        const float floor_min_x = building_min_x - max_building_lot_margin;
        const float floor_max_x = building_max_x + max_building_lot_margin;
        const float floor_min_z = building_min_z - max_building_lot_margin;
        const float floor_max_z = building_max_z + max_building_lot_margin;

        SceneObject floor;
        floor.mesh = MeshId::Floor;
        floor.color = kRoadColor;
        floor.world = glm::translate(glm::mat4(1.0f),
                          glm::vec3(
                              (floor_min_x + floor_max_x) * 0.5f,
                              renderer_config_.world_floor_top_y - world_floor_height(renderer_config_) * 0.5f,
                              (floor_min_z + floor_max_z) * 0.5f))
            * glm::scale(glm::mat4(1.0f),
                glm::vec3(
                    floor_max_x - floor_min_x,
                    world_floor_height(renderer_config_),
                    floor_max_z - floor_min_z));
        scene.objects.insert(scene.objects.begin(), floor);
    }

    if (min_x > max_x || min_z > max_z)
    {
        min_x = -2.5f;
        max_x = 2.5f;
        min_z = -2.5f;
        max_z = 2.5f;
    }

    const float span = std::max(max_x - min_x, max_z - min_z);
    world_span_ = std::max(span, 1.0f);
    scene.camera.point_light_pos = glm::vec4(
        renderer_config_.point_light_x,
        renderer_config_.point_light_y,
        renderer_config_.point_light_z,
        std::max(renderer_config_.point_light_radius, 1.0f));

    return scene;
}

void MegaCityHost::rebuild_semantic_city()
{
    if (!world_ || !camera_)
        return;

    const bool had_existing_city = semantic_model_ && !semantic_model_->empty();
    std::vector<SemanticCityModuleInput> modules;
    for (const std::string& module_path : city_db_.list_modules())
    {
        const CityModuleRecord mod_record = city_db_.module_record(module_path);
        modules.push_back({ module_path, city_db_.list_classes_in_module(module_path), mod_record.quality });
    }

    auto semantic_model = std::make_shared<SemanticMegacityModel>(
        build_semantic_megacity_model(modules, renderer_config_));
    const SemanticMegacityLayout layout = build_semantic_megacity_layout(*semantic_model, renderer_config_);
    city_bounds_valid_ = !layout.empty();
    if (city_bounds_valid_)
    {
        city_min_x_ = layout.min_x;
        city_max_x_ = layout.max_x;
        city_min_z_ = layout.min_z;
        city_max_z_ = layout.max_z;

        if (!renderer_config_.point_light_position_valid)
        {
            const float span = std::max(layout.max_x - layout.min_x, layout.max_z - layout.min_z);
            const float default_height = std::max(8.0f, span * 0.4f);
            const float default_radius = std::max(24.0f, span * 0.8f);
            auto set_default_light = [&](MegaCityCodeConfig& config) {
                config.point_light_position_valid = true;
                config.point_light_x = layout.min_x;
                config.point_light_y = default_height;
                config.point_light_z = layout.min_z;
                config.point_light_radius = default_radius;
            };
            set_default_light(renderer_config_);
            set_default_light(pending_renderer_config_);
        }
    }
    std::vector<SignLabelRequest> sign_requests;
    sign_requests.reserve(layout.building_count() + layout.modules.size());
    for (const auto& module_layout : layout.modules)
    {
        for (const auto& building : module_layout.buildings)
        {
            const std::string& text = building.display_name.empty() ? building.qualified_name : building.display_name;
            const TextService* ts = sign_text_service_ ? sign_text_service_.get() : nullptr;
            const auto signs = place_building_signs(building, text, ts, renderer_config_);
            // All 4 faces share the same atlas entry — use the first for sizing.
            sign_requests.push_back(make_sign_request(building_sign_key(building), text, signs[0], ts, renderer_config_));
        }

        if (!module_layout.buildings.empty())
        {
            const SemanticCityBuilding& anchor = module_layout.buildings.front();
            const std::string name = module_display_name(module_layout.module_path);
            const SignPlacementSpec road = place_module_road_sign(
                anchor, name, sign_text_service_ ? sign_text_service_.get() : nullptr, renderer_config_);
            sign_requests.push_back(make_sign_request(
                module_sign_key(module_layout.module_path), name, road,
                sign_text_service_ ? sign_text_service_.get() : nullptr, renderer_config_));
        }
    }
    if (sign_text_service_)
        sign_label_atlas_ = build_sign_label_atlas(*sign_text_service_, sign_requests, sign_label_revision_++);
    else
        sign_label_atlas_.reset();

    world_->clear();
    for (const auto& module_layout : layout.modules)
    {
        // Park slab at the center of the module, colored by quality.
        if (module_layout.park_footprint > 0.0f)
        {
            // Lerp from brown (low quality) to green (high quality).
            const glm::vec3 kParkBrown(0.45f, 0.30f, 0.15f);
            const glm::vec3 kParkGreen(0.25f, 0.65f, 0.20f);
            const float q = std::clamp(module_layout.quality, 0.0f, 1.0f);
            const glm::vec3 park_rgb = glm::mix(kParkBrown, kParkGreen, q);
            const glm::vec4 park_color(park_rgb, 1.0f);

            BuildingMetrics park_metrics;
            park_metrics.footprint = module_layout.park_footprint;
            park_metrics.height = renderer_config_.park_height;
            park_metrics.sidewalk_width = 0.0f;
            park_metrics.road_width = 0.0f;
            world_->create_building(
                module_layout.park_center.x,
                module_layout.park_center.y,
                building_base_elevation(renderer_config_),
                park_metrics,
                park_color,
                SourceSymbol{ "", module_layout.module_path });
        }

        const glm::vec4 module_color = module_building_color(module_layout.module_path);
        for (const auto& building : module_layout.buildings)
        {
            float layer_base_y = building_base_elevation(renderer_config_);
            if (building.layers.empty())
            {
                world_->create_building(
                    building.center.x,
                    building.center.y,
                    building_base_elevation(renderer_config_),
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
                    world_->create_building(
                        building.center.x,
                        building.center.y,
                        layer_base_y,
                        layer_metrics,
                        module_building_layer_color(module_color, layer_index),
                        SourceSymbol{ building.source_file_path, building.qualified_name });
                    layer_base_y += layer.height;
                }
            }

            if (sign_label_atlas_)
            {
                const auto it = sign_label_atlas_->entries.find(building_sign_key(building));
                if (it != sign_label_atlas_->entries.end())
                {
                    const std::string& btext = building.display_name.empty() ? building.qualified_name : building.display_name;
                    const TextService* ts = sign_text_service_ ? sign_text_service_.get() : nullptr;
                    const auto face_signs = place_building_signs(building, btext, ts, renderer_config_);
                    const SignMetrics first_sign = make_sign_metrics(face_signs[0], it->second);
                    const float cap_height = first_sign.height;

                    // Add a cap block so signs don't obscure the top function layer.
                    if (cap_height > 0.0f)
                    {
                        BuildingMetrics cap_metrics = building.metrics;
                        cap_metrics.height = cap_height;
                        world_->create_building(
                            building.center.x,
                            building.center.y,
                            building_base_elevation(renderer_config_) + building.metrics.height,
                            cap_metrics,
                            module_color,
                            SourceSymbol{ building.source_file_path, building.qualified_name });
                    }

                    const float total_height = building_base_elevation(renderer_config_) + building.metrics.height + cap_height;
                    for (const SignPlacementSpec& placement : face_signs)
                    {
                        const SignMetrics sign = make_sign_metrics(placement, it->second);
                        const float sign_y = total_height - sign.height * 0.5f;
                        world_->create_sign(
                            placement.center.x,
                            placement.center.y,
                            sign_y,
                            sign,
                            placement.mesh,
                            kBuildingSignColor,
                            SourceSymbol{ building.source_file_path, building.qualified_name });
                    }
                }
            }

            for (const RoadSegmentPlacement& sidewalk : build_sidewalk_segments(building))
            {
                world_->create_road(
                    sidewalk.center.x,
                    sidewalk.center.y,
                    RoadMetrics{ sidewalk.extent.x, sidewalk.extent.y, renderer_config_.sidewalk_surface_height },
                    kSidewalkSurfaceColor,
                    SourceSymbol{ building.source_file_path, building.qualified_name },
                    renderer_config_.sidewalk_surface_lift);
            }

            for (const RoadSegmentPlacement& road : build_road_segments(building))
            {
                world_->create_road(
                    road.center.x,
                    road.center.y,
                    RoadMetrics{ road.extent.x, road.extent.y, renderer_config_.road_surface_height },
                    kRoadSurfaceColor,
                    SourceSymbol{ building.source_file_path, building.qualified_name });
            }
        }

        if (sign_label_atlas_ && !module_layout.buildings.empty())
        {
            const SemanticCityBuilding& anchor = module_layout.buildings.front();
            const auto it = sign_label_atlas_->entries.find(module_sign_key(module_layout.module_path));
            if (it != sign_label_atlas_->entries.end())
            {
                const std::string name = module_display_name(module_layout.module_path);
                const SignPlacementSpec road = place_module_road_sign(
                    anchor, name, sign_text_service_ ? sign_text_service_.get() : nullptr, renderer_config_);
                const SignMetrics sign = make_sign_metrics(road, it->second);
                world_->create_sign(
                    road.center.x,
                    road.center.y,
                    renderer_config_.sidewalk_surface_lift
                        + renderer_config_.sidewalk_surface_height
                        + sign.height * 0.5f
                        + renderer_config_.road_sign_lift,
                    sign,
                    road.mesh,
                    kModuleSignColor,
                    SourceSymbol{ anchor.source_file_path, module_layout.module_path });
            }
        }
    }

    if (!had_existing_city)
    {
        if (restore_camera_after_initial_build_ && renderer_config_.camera_state_valid)
        {
            if (layout.empty())
                camera_->frame_world_bounds(-2.5f, 2.5f, -2.5f, 2.5f);
            else
                camera_->frame_world_bounds(layout.min_x, layout.max_x, layout.min_z, layout.max_z);
            camera_->apply_state(IsometricCameraState{
                .target = { renderer_config_.camera_target_x, 0.0f, renderer_config_.camera_target_z },
                .yaw = renderer_config_.camera_yaw,
                .pitch = renderer_config_.camera_pitch,
                .orbit_radius = renderer_config_.camera_orbit_radius,
                .zoom_half_height = renderer_config_.camera_zoom_half_height,
            });
        }
        else if (layout.empty())
            camera_->frame_world_bounds(-2.5f, 2.5f, -2.5f, 2.5f);
        else
            camera_->frame_world_bounds(layout.min_x, layout.max_x, layout.min_z, layout.max_z);
    }
    restore_camera_after_initial_build_ = false;
    sync_camera_state_to_configs();

    DRAXUL_LOG_INFO(LogCategory::App,
        "MegaCityHost: built semantic megacity with %zu modules and %zu buildings",
        layout.modules.size(),
        layout.building_count());

    semantic_model_ = std::move(semantic_model);
    world_rebuild_pending_ = false;
    mark_scene_dirty();

    launch_grid_build(layout);
}

void MegaCityHost::launch_grid_build(const SemanticMegacityLayout& layout)
{
    // Wait for any previous grid build to finish.
    if (grid_thread_.joinable())
        grid_thread_.join();

    if (layout.empty())
    {
        std::lock_guard<std::mutex> lock(grid_mutex_);
        city_grid_.reset();
        return;
    }

    // Copy the layout and config so the thread owns its data.
    auto layout_copy = std::make_shared<SemanticMegacityLayout>(layout);
    const MegaCityCodeConfig config = renderer_config_;

    grid_build_in_progress_ = true;
    grid_thread_ = std::thread([this, layout_copy, config]() {
        auto grid = std::make_shared<CityGrid>(build_city_grid(*layout_copy, config));

        DRAXUL_LOG_DEBUG(LogCategory::App,
            "MegaCityHost: city grid built: %dx%d cells (%.1f x %.1f world units)",
            grid->cols, grid->rows,
            grid->cols * grid->cell_size, grid->rows * grid->cell_size);

        {
            std::lock_guard<std::mutex> lock(grid_mutex_);
            city_grid_ = std::move(grid);
        }
        grid_build_in_progress_ = false;
    });
}

bool MegaCityHost::movement_active() const
{
    return move_left_ || move_right_ || move_up_ || move_down_ || orbit_left_ || orbit_right_
        || zoom_in_ || zoom_out_ || pitch_up_ || pitch_down_;
}

bool MegaCityHost::drag_smoothing_active() const
{
    return glm::dot(pending_drag_pan_, pending_drag_pan_) > kDragPanSettleEpsilon * kDragPanSettleEpsilon
        || std::abs(pending_drag_orbit_) > kDragOrbitSettleEpsilon;
}

void MegaCityHost::pump()
{
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - last_pump_time_).count();
    bool camera_changed = false;

    if (camera_)
    {
        if (movement_active())
        {
            glm::vec2 pan_input{ 0.0f, 0.0f };
            if (move_left_)
                pan_input.x -= 1.0f;
            if (move_right_)
                pan_input.x += 1.0f;
            if (move_up_)
                pan_input.y += 1.0f;
            if (move_down_)
                pan_input.y -= 1.0f;

            const float pan_distance = dt * kMovementSpeedFractionPerSecond * world_span_;
            if (pan_distance > 0.0f && glm::dot(pan_input, pan_input) > 0.0f)
            {
                const glm::vec2 right = camera_->planar_right_vector();
                const glm::vec2 up = camera_->planar_up_vector();
                const glm::vec2 pan = glm::normalize(pan_input.x * right + pan_input.y * up);
                camera_->translate_target(pan.x * pan_distance, pan.y * pan_distance);
                camera_changed = true;
            }

            float orbit = 0.0f;
            if (orbit_left_)
                orbit += 1.0f;
            if (orbit_right_)
                orbit -= 1.0f;
            if (orbit != 0.0f && dt > 0.0f)
            {
                camera_->orbit_target(orbit * dt * kOrbitSpeedRadiansPerSecond);
                camera_changed = true;
            }

            float zoom = 0.0f;
            if (zoom_in_)
                zoom -= 1.0f;
            if (zoom_out_)
                zoom += 1.0f;
            if (zoom != 0.0f && dt > 0.0f)
            {
                camera_->zoom_by(zoom * dt * kZoomSpeedPerSecond);
                camera_changed = true;
            }

            float pitch = 0.0f;
            if (pitch_up_)
                pitch += 1.0f;
            if (pitch_down_)
                pitch -= 1.0f;
            if (pitch != 0.0f && dt > 0.0f)
            {
                camera_->adjust_pitch(pitch * dt * kPitchSpeedRadiansPerSecond);
                camera_changed = true;
            }
        }

        if (drag_smoothing_active())
        {
            const float alpha = drag_catch_up_alpha(dt);
            if (alpha > 0.0f)
            {
                if (glm::dot(pending_drag_pan_, pending_drag_pan_) > 0.0f)
                {
                    glm::vec2 applied_pan = pending_drag_pan_ * alpha;
                    camera_->translate_target(applied_pan.x, applied_pan.y);
                    pending_drag_pan_ -= applied_pan;
                    clamp_small_pan(pending_drag_pan_);
                    camera_changed = true;
                }

                if (pending_drag_orbit_ != 0.0f)
                {
                    const float applied_orbit = pending_drag_orbit_ * alpha;
                    camera_->orbit_target(applied_orbit);
                    pending_drag_orbit_ -= applied_orbit;
                    clamp_small_orbit(pending_drag_orbit_);
                    camera_changed = true;
                }
            }
        }
    }

    if (camera_changed)
    {
        sync_camera_state_to_configs();
        scene_dirty_ = true;
        last_activity_time_ = now;
        if (callbacks_)
            callbacks_->request_frame();
    }

    last_pump_time_ = now;

    if (!city_db_reconciled_ && city_db_.is_open())
    {
        if (const auto snapshot = scanner_.snapshot(); snapshot && snapshot->complete)
        {
            if (city_db_.reconcile_snapshot(*snapshot))
            {
                city_db_reconciled_ = true;
                rebuild_semantic_city();
                const auto& stats = city_db_.stats();
                DRAXUL_LOG_INFO(LogCategory::App,
                    "MegaCityHost: reconciled Tree-sitter snapshot into %s (%zu files, %zu symbols, %zu entities)",
                    city_db_.path().string().c_str(),
                    stats.file_count,
                    stats.symbol_count,
                    stats.city_entity_count);
            }
            else
            {
                DRAXUL_LOG_WARN(LogCategory::App,
                    "MegaCityHost: city DB reconcile failed for %s: %s",
                    city_db_.path().string().c_str(),
                    city_db_.last_error().c_str());
            }
        }
    }

    if (scene_dirty_ && scene_pass_)
    {
        scene_pass_->set_scene(build_scene_snapshot());
        scene_dirty_ = false;
    }
    if (running_ && continuous_refresh_enabled_ && callbacks_)
        callbacks_->request_frame();
}

std::optional<std::chrono::steady_clock::time_point> MegaCityHost::next_deadline() const
{
    if (!running_)
        return std::nullopt;
    if (!continuous_refresh_enabled_ && !movement_active() && !drag_smoothing_active())
        return std::nullopt;
    if (drag_smoothing_active())
        return std::chrono::steady_clock::now() + kDragSmoothingTick;
    return std::chrono::steady_clock::now() + kMovementTick;
}

bool MegaCityHost::dispatch_action(std::string_view action)
{
    if (action == "quit" || action == "request_quit")
    {
        running_ = false;
        if (callbacks_)
            callbacks_->request_quit();
        return true;
    }
    return false;
}

void MegaCityHost::request_close()
{
    running_ = false;
}

Color MegaCityHost::default_background() const
{
    return Color(0.05f, 0.05f, 0.10f, 1.0f);
}

HostRuntimeState MegaCityHost::runtime_state() const
{
    HostRuntimeState s;
    s.content_ready = true;
    s.last_activity_time = last_activity_time_;
    return s;
}

HostDebugState MegaCityHost::debug_state() const
{
    HostDebugState s;
    s.name = "megacity";
    s.grid_cols = 0;
    s.grid_rows = 0;
    s.dirty_cells = scene_dirty_ ? 1u : 0u;
    return s;
}

std::unique_ptr<IHost> create_megacity_host()
{
    return std::make_unique<MegaCityHost>();
}

} // namespace draxul
