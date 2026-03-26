#pragma once

#include <draxul/config_document.h>

#include <cstdint>
#include <toml++/toml.hpp>

namespace draxul
{

enum class MegaCitySignPlacement : uint8_t
{
    RoofNorth,
    RoofSouth,
    RoofEast,
    RoofWest,
    WallNorth,
    WallSouth,
    WallEast,
    WallWest,
};

struct MegaCityCodeConfig
{
    float sign_text_hidden_px = 1.5f;
    float sign_text_full_px = 8.0f;
    float output_gamma = 1.0f;
    float height_multiplier = 1.5f;
    bool clamp_semantic_metrics = false;
    bool hide_test_entities = true;
    bool auto_rebuild = true;

    float placement_step = 0.5f;
    int max_spiral_rings = 4096;
    float lot_road_reserve_fraction = 0.25f;

    float footprint_base = 1.0f;
    float footprint_min = 1.0f;
    float footprint_max = 9.0f;
    float footprint_unclamped_scale = 0.15f;

    float height_base = 2.0f;
    float height_mass_weight = 1.35f;
    float height_count_weight = 0.45f;
    float height_min = 2.0f;
    float height_max = 12.0f;
    float height_unclamped_count_weight = 0.27f;

    float road_width_base = 0.6f;
    float road_width_scale = 0.85f;
    float road_width_min = 0.6f;
    float road_width_max = 3.0f;
    float sidewalk_width = 1.0f;

    float sign_label_point_size = 18.0f;
    MegaCitySignPlacement building_sign_placement = MegaCitySignPlacement::WallEast;
    float roof_sign_thickness = 0.05f;
    float roof_sign_depth = 0.42f;
    float roof_sign_edge_inset = 0.08f;
    float roof_sign_side_inset = 0.12f;
    float wall_sign_thickness = 0.05f;
    float wall_sign_face_gap = 0.02f;
    float wall_sign_width = 1.92f;
    float wall_sign_side_inset = 0.12f;
    float wall_sign_top_inset = 0.18f;
    float wall_sign_bottom_inset = 0.28f;
    int wall_sign_text_padding = 4;
    float road_sign_edge_inset = 0.06f;
    float road_sign_side_inset = 0.12f;
    float minimum_road_sign_depth = 0.16f;
    float sidewalk_sign_edge_inset = 0.04f;
    float road_sign_lift = 0.006f;
    float roof_sign_pixels_per_world_unit = 192.0f;

    float road_surface_height = 0.03f;
    float sidewalk_surface_height = 0.18f;
    float sidewalk_surface_lift = 0.024f;
    float world_floor_height_scale = 0.5f;
    float world_floor_top_y = -0.01f;
    float world_floor_grid_y_offset = 0.0015f;
    float world_floor_grid_tile_scale = 2.0f;
    float world_floor_grid_line_width = 0.08f;

    float ambient_strength = 0.45f;
    float directional_light_x = -0.5f;
    float directional_light_y = -1.0f;
    float directional_light_z = -0.3f;
    bool point_light_position_valid = false;
    float point_light_x = 0.0f;
    float point_light_y = 0.0f;
    float point_light_z = 0.0f;
    float point_light_radius = 24.0f;
    float point_light_brightness = 1.0f;

    bool camera_state_valid = false;
    float camera_target_x = 0.0f;
    float camera_target_z = 0.0f;
    float camera_yaw = -2.35619449f;
    float camera_pitch = 0.72425002f;
    float camera_orbit_radius = 7.07106781f;
    float camera_zoom_half_height = 4.0f;

    bool operator==(const MegaCityCodeConfig&) const = default;
};

[[nodiscard]] std::optional<MegaCitySignPlacement> parse_megacity_sign_placement(std::string_view value);
[[nodiscard]] std::string_view format_megacity_sign_placement(MegaCitySignPlacement value);

void apply_megacity_code_table(MegaCityCodeConfig& config, const toml::table& table);
[[nodiscard]] toml::table serialize_megacity_code_table(const MegaCityCodeConfig& config);
[[nodiscard]] MegaCityCodeConfig load_megacity_code_defaults(const ConfigDocument& document);
[[nodiscard]] MegaCityCodeConfig load_megacity_code_config(const ConfigDocument& document, const MegaCityCodeConfig& defaults);
void store_megacity_code_config(ConfigDocument& document, const MegaCityCodeConfig& current, const MegaCityCodeConfig& defaults);

} // namespace draxul
