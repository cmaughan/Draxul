#include "support/temp_dir.h"

#include <draxul/config_document.h>
#include <draxul/megacity_code_config.h>

#include <catch2/catch_all.hpp>

using namespace draxul;
using namespace draxul::tests;

TEST_CASE("megacity config round-trips through config document",
    "[config][megacity]")
{
    ConfigDocument document;
    MegaCityCodeConfig defaults;
    defaults.selected_module_path = "app";
    defaults.sign_text_px_range = { 2.5f, 12.0f };
    defaults.clamp_semantic_metrics = true;
    defaults.auto_rebuild = true;

    MegaCityCodeConfig current = defaults;
    current.selected_module_path = "libs/draxul-megacity";
    current.height_multiplier = 2.25f;
    current.hide_test_entities = false;
    current.hide_struct_entities = true;
    current.point_shadow_debug_scene = true;
    current.selection_dependency_alpha = 0.68f;
    current.selection_hidden_alpha = 0.22f;
    current.selection_hidden_hover_alpha = 0.46f;
    current.selection_hidden_hover_raise_seconds = 0.65f;
    current.selection_hidden_hover_fall_seconds = 1.4f;
    current.selection_hidden_road_alpha = 0.88f;
    current.debug_view = MegaCityDebugView::Normals;
    current.ao_denoise = false;
    current.ao_radius = 2.4f;
    current.ao_bias = 0.08f;
    current.ao_power = 1.9f;
    current.ao_kernel_size = 24;
    current.connected_hex_building_threshold = 14;
    current.connected_oct_building_threshold = 28;
    current.building_middle_strip_push = 0.09f;
    current.building_alternate_darkening = 0.41f;
    current.overlay_mode = OverlayMode::Coverage;
    current.performance_heat_log_scale = 75.0f;
    current.flat_color_roughness = 0.58f;
    current.flat_color_metallic = 0.27f;
    current.central_park_tree_age_years = 48.0f;
    current.central_park_tree_seed = 101;
    current.central_park_tree_overall_scale = 1.35f;
    current.central_park_tree_radial_segments = 14;
    current.central_park_tree_max_branch_depth = 4;
    current.central_park_tree_child_branches_min = 3;
    current.central_park_tree_child_branches_max = 5;
    current.central_park_tree_branch_length_scale = 0.74f;
    current.central_park_tree_branch_radius_scale = 0.58f;
    current.central_park_tree_upward_bias = 0.52f;
    current.central_park_tree_outward_bias = 0.92f;
    current.central_park_tree_curvature = 0.24f;
    current.central_park_tree_trunk_wander = 0.18f;
    current.central_park_tree_branch_wander = 0.36f;
    current.central_park_tree_wander_frequency = 0.44f;
    current.central_park_tree_wander_deviation = 0.72f;
    current.central_park_tree_leaf_density = 1.65f;
    current.central_park_tree_leaf_orientation_randomness = 0.58f;
    current.central_park_tree_leaf_size_range = glm::vec2(2.2f, 6.8f);
    current.central_park_tree_leaf_start_depth = 2;
    current.central_park_tree_bark_color_noise = 0.06f;
    current.central_park_tree_bark_root = glm::vec3(0.29f, 0.20f, 0.14f);
    current.central_park_tree_bark_tip = glm::vec3(0.63f, 0.49f, 0.36f);
    current.module_sign_board_color = glm::vec3(0.85f, 0.80f, 0.72f);
    current.module_sign_text_color = glm::vec3(0.15f, 0.10f, 0.05f);
    current.building_sign_board_color = glm::vec3(0.72f, 0.78f, 0.88f);
    current.building_sign_text_color = glm::vec3(0.08f, 0.09f, 0.15f);
    current.roof_sign_min_width_per_character = 0.31f;
    current.ambient_strength = 0.62f;
    current.point_light_position_valid = true;
    current.point_light_position = { -14.0f, 18.5f, -9.5f };
    current.point_light_radius = 37.0f;
    current.point_light_brightness = 1.7f;
    current.tone_map_exposure = 1.18f;
    current.tone_map_white_point = 5.5f;
    current.dependency_route_layer_step = 0.048f;
    current.directional_light_dir.x = -0.25f;
    current.world_floor_grid_tile_scale = 3.0f;
    current.auto_rebuild = false;
    current.camera_state_valid = true;
    current.camera_target = { 12.5f, -8.25f };
    current.camera_yaw = -1.75f;
    current.camera_pitch = 0.93f;
    current.camera_orbit_radius = 21.0f;
    current.camera_zoom_half_height = 13.5f;
    current.projection_mode = MegaCityProjectionMode::Perspective;

    store_megacity_code_config(document, current, defaults);

    const MegaCityCodeConfig loaded_defaults
        = load_megacity_code_defaults(document);
    const MegaCityCodeConfig loaded_current
        = load_megacity_code_config(document, loaded_defaults);

    REQUIRE(loaded_defaults == defaults);
    REQUIRE(loaded_current == current);
}

TEST_CASE("MegaCity stale graphify config falls back to Tree-sitter",
    "[config][megacity]")
{
    ConfigDocument document;
    toml::table& table = document.ensure_table("mega_city_code");
    table.insert_or_assign("code_source", "graphify");
    table.insert_or_assign("graphify_graph_path", "custom/merged-graph.json");

    const MegaCityCodeConfig defaults;
    const MegaCityCodeConfig loaded
        = load_megacity_code_config(document, defaults);
    CHECK(loaded.code_source == MegaCityCodeSource::TreeSitterDb);

    ConfigDocument saved;
    store_megacity_code_config(saved, loaded, defaults);
    TempDir temp("draxul-megacity-code-source-config");
    const std::filesystem::path path = temp.path / "config.toml";
    saved.save_to_path(path);

    const ConfigDocument round_tripped
        = ConfigDocument::load_from_path(path);
    const MegaCityCodeConfig saved_defaults
        = load_megacity_code_defaults(round_tripped);
    const MegaCityCodeConfig saved_current
        = load_megacity_code_config(round_tripped, saved_defaults);
    const toml::table* saved_table
        = round_tripped.find_table("mega_city_code");
    REQUIRE(saved_table != nullptr);
    CHECK(saved_current.code_source == MegaCityCodeSource::TreeSitterDb);
    CHECK_FALSE(saved_table->contains("graphify_graph_path"));
}
