#pragma once

#include <draxul/config_document.h>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>

namespace draxul
{

enum class OverlayMode : uint8_t
{
    None,
    Perf,
    Coverage,
    LcovCoverage,
};

enum class MegaCityDebugView : uint8_t
{
    FinalScene,
    AmbientOcclusion,
    AmbientOcclusionDenoised,
    Normals,
    WorldPosition,
    Roughness,
    Metallic,
    Albedo,
    Tangents,
    UV,
    Depth,
    Bitangents,
    TbnPacked,
    DirectionalShadow,
    PointShadow,
    PointShadowFace,
    PointShadowStoredDepth,
    PointShadowDepthDelta,
};

struct MegaCityCodeConfig
{
    std::string selected_module_path;
    glm::vec2 sign_text_px_range{ 1.5f, 8.0f }; // (hidden, full)
    MegaCityDebugView debug_view = MegaCityDebugView::FinalScene;
    bool wireframe = false;
    bool ao_denoise = true;
    float ao_radius = 1.41f;
    float ao_bias = 0.06f;
    float ao_power = 3.21f;
    int ao_kernel_size = 38;
    float height_multiplier = 1.3f;
    bool clamp_semantic_metrics = false;
    bool hide_test_entities = true;
    bool hide_struct_entities = true;
    bool point_shadow_debug_scene = false;
    bool auto_rebuild = true;
    bool show_ui_panels = true;
    float selection_dependency_alpha = 0.32f;
    float selection_hidden_alpha = 0.0f;
    float selection_hidden_hover_alpha = 0.72f;
    float selection_hidden_hover_raise_seconds = 0.1f;
    float selection_hidden_hover_fall_seconds = 0.2f;
    float selection_hidden_road_alpha = 1.0f;

    float placement_step = 0.5f;
    int max_spiral_rings = 4096;

    float footprint_base = 2.2f;
    glm::vec2 footprint_range{ 1.1f, 15.3f }; // (min, max)
    float footprint_unclamped_scale = 0.18f;

    float height_base = 1.7f;
    float height_mass_weight = 1.33f;
    float height_count_weight = 0.45f;
    glm::vec2 height_range{ 2.0f, 15.4f }; // (min, max)
    float height_unclamped_count_weight = 0.27f;
    int connected_hex_building_threshold = 14;
    int connected_oct_building_threshold = 42;
    float building_middle_strip_push = 0.0f;
    float building_alternate_darkening = 0.48f;
    OverlayMode overlay_mode = OverlayMode::None;
    float performance_heat_log_scale = 0.0f;
    float flat_color_roughness = 1.0f;
    float flat_color_metallic = 0.92f;

    float road_width_base = 2.56f;
    float road_width_scale = 0.81f;
    glm::vec2 road_width_range{ 0.6f, 3.0f }; // (min, max)
    float sidewalk_width = 1.0f;
    float module_border_alpha = 0.18f;
    float dependency_route_layer_step = 0.132f;
    float park_footprint = 6.0f;
    float park_height = 0.15f;
    float park_sidewalk_width = 1.0f;
    float park_road_width = 1.0f;
    float park_sign_max_depth_fraction = 0.33f; // max sign depth as fraction of park footprint
    glm::vec2 central_park_scale{ 2.0f, 2.0f }; // (area, border) 1..3 multipliers
    float central_park_tree_age_years = 83.5f;
    int central_park_tree_seed = 34;
    float central_park_tree_overall_scale = 3.2f;
    int central_park_tree_radial_segments = 11;
    int central_park_tree_max_branch_depth = 8;
    int central_park_tree_child_branches_min = 2;
    int central_park_tree_child_branches_max = 6;
    float central_park_tree_branch_length_scale = 0.7f;
    float central_park_tree_branch_radius_scale = 0.84f;
    float central_park_tree_upward_bias = 0.51f;
    float central_park_tree_outward_bias = 0.73f;
    float central_park_tree_curvature = 0.68f;
    float central_park_tree_trunk_wander = 2.0f;
    float central_park_tree_branch_wander = 1.82f;
    float central_park_tree_wander_frequency = 0.46f;
    float central_park_tree_wander_deviation = 0.14f;
    float central_park_tree_leaf_density = 3.5f;
    float central_park_tree_leaf_orientation_randomness = 0.82f;
    glm::vec2 central_park_tree_leaf_size_range{ 1.3f, 4.3f };
    int central_park_tree_leaf_start_depth = 2;
    float central_park_tree_bark_color_noise = 0.16f;
    glm::vec3 central_park_tree_bark_root{ 0.543f, 0.45f, 0.378f };
    glm::vec3 central_park_tree_bark_tip{ 0.354f, 0.236f, 0.127f };

    float sign_label_point_size = 13.0f;
    float tooltip_point_size = 10.5f;
    glm::vec3 module_sign_board_color{ 0.229f, 0.243f, 0.205f };
    glm::vec3 module_sign_text_color{ 1.0f, 1.0f, 1.0f };
    glm::vec3 building_sign_board_color{ 0.275f, 0.275f, 0.275f };
    glm::vec3 building_sign_text_color{ 1.0f, 1.0f, 1.0f };
    float roof_sign_thickness = 0.63f;
    float roof_sign_min_width_per_character = 0.28f;
    float wall_sign_thickness = 0.24f;
    float wall_sign_face_gap = 0.086f;
    float wall_sign_side_inset = 0.56f;
    int wall_sign_text_padding = 4;
    float road_sign_edge_inset = 0.06f;
    float minimum_road_sign_depth = 1.27f;
    float sidewalk_sign_edge_inset = 1.26f;
    float road_sign_lift = 0.25f;

    float road_surface_height = 0.025f;
    float sidewalk_surface_height = 0.151f;
    float sidewalk_surface_lift = 0.136f;
    float world_floor_height_scale = 0.5f;
    float world_floor_top_y = -0.01f;
    float world_floor_grid_y_offset = 0.0015f;
    float world_floor_grid_tile_scale = 2.0f;
    float world_floor_grid_line_width = 0.08f;

    float ambient_strength = 0.24f;
    float tone_map_exposure = 1.32f;
    float tone_map_white_point = 0.9f;
    glm::vec3 directional_light_dir{ 0.5f, -1.0f, 1.0f };
    bool point_light_position_valid = true;
    glm::vec3 point_light_position{ 0.0f, 33.0f, 0.0f };
    float point_light_radius = 237.9f;
    float point_light_brightness = 0.14f;

    bool camera_state_valid = false;
    glm::vec2 camera_target{ 0.0f };
    float camera_yaw = -2.35619449f;
    float camera_pitch = 0.72425002f;
    float camera_orbit_radius = 7.07106781f;
    float camera_zoom_half_height = 4.0f;

    bool operator==(const MegaCityCodeConfig&) const = default;
};

[[nodiscard]] std::optional<MegaCityDebugView> parse_megacity_debug_view(std::string_view value);
[[nodiscard]] std::string_view format_megacity_debug_view(MegaCityDebugView value);

void apply_megacity_code_table(MegaCityCodeConfig& config, const toml::table& table);
[[nodiscard]] toml::table serialize_megacity_code_table(const MegaCityCodeConfig& config);
[[nodiscard]] MegaCityCodeConfig load_megacity_code_defaults(const ConfigDocument& document);
[[nodiscard]] MegaCityCodeConfig load_megacity_code_config(const ConfigDocument& document, const MegaCityCodeConfig& defaults);
void store_megacity_code_config(ConfigDocument& document, const MegaCityCodeConfig& current, const MegaCityCodeConfig& defaults);

} // namespace draxul
