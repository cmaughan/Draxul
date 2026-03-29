#include <draxul/megacity_code_config.h>

#include <draxul/toml_support.h>

namespace draxul
{

std::optional<MegaCityDebugView> parse_megacity_debug_view(std::string_view value)
{
    if (value == "final_scene")
        return MegaCityDebugView::FinalScene;
    if (value == "ambient_occlusion")
        return MegaCityDebugView::AmbientOcclusion;
    if (value == "ambient_occlusion_denoised")
        return MegaCityDebugView::AmbientOcclusionDenoised;
    if (value == "normals" || value == "decoded_normals")
        return MegaCityDebugView::Normals;
    if (value == "world_position")
        return MegaCityDebugView::WorldPosition;
    if (value == "roughness")
        return MegaCityDebugView::Roughness;
    if (value == "metallic")
        return MegaCityDebugView::Metallic;
    if (value == "albedo")
        return MegaCityDebugView::Albedo;
    if (value == "tangents")
        return MegaCityDebugView::Tangents;
    if (value == "uv")
        return MegaCityDebugView::UV;
    if (value == "depth")
        return MegaCityDebugView::Depth;
    if (value == "bitangents")
        return MegaCityDebugView::Bitangents;
    if (value == "tbn_packed")
        return MegaCityDebugView::TbnPacked;
    if (value == "directional_shadow")
        return MegaCityDebugView::DirectionalShadow;
    if (value == "point_shadow")
        return MegaCityDebugView::PointShadow;
    if (value == "point_shadow_face")
        return MegaCityDebugView::PointShadowFace;
    if (value == "point_shadow_stored_depth")
        return MegaCityDebugView::PointShadowStoredDepth;
    if (value == "point_shadow_depth_delta")
        return MegaCityDebugView::PointShadowDepthDelta;
    return std::nullopt;
}

std::string_view format_megacity_debug_view(MegaCityDebugView value)
{
    switch (value)
    {
    case MegaCityDebugView::FinalScene:
        return "final_scene";
    case MegaCityDebugView::AmbientOcclusion:
        return "ambient_occlusion";
    case MegaCityDebugView::AmbientOcclusionDenoised:
        return "ambient_occlusion_denoised";
    case MegaCityDebugView::Normals:
        return "normals";
    case MegaCityDebugView::WorldPosition:
        return "world_position";
    case MegaCityDebugView::Roughness:
        return "roughness";
    case MegaCityDebugView::Metallic:
        return "metallic";
    case MegaCityDebugView::Albedo:
        return "albedo";
    case MegaCityDebugView::Tangents:
        return "tangents";
    case MegaCityDebugView::UV:
        return "uv";
    case MegaCityDebugView::Depth:
        return "depth";
    case MegaCityDebugView::Bitangents:
        return "bitangents";
    case MegaCityDebugView::TbnPacked:
        return "tbn_packed";
    case MegaCityDebugView::DirectionalShadow:
        return "directional_shadow";
    case MegaCityDebugView::PointShadow:
        return "point_shadow";
    case MegaCityDebugView::PointShadowFace:
        return "point_shadow_face";
    case MegaCityDebugView::PointShadowStoredDepth:
        return "point_shadow_stored_depth";
    case MegaCityDebugView::PointShadowDepthDelta:
        return "point_shadow_depth_delta";
    }
    return "final_scene";
}

namespace
{

std::optional<float> get_float(const toml::table& table, const char* key)
{
    if (auto parsed = toml_support::get_double(table, key); parsed.has_value())
        return static_cast<float>(*parsed);
    if (auto parsed = toml_support::get_int(table, key); parsed.has_value())
        return static_cast<float>(*parsed);
    return std::nullopt;
}

void assign_vec2(const toml::table& table, const char* key, glm::vec2& target)
{
    if (auto parsed = toml_support::get_vec2(table, key); parsed.has_value())
        target = *parsed;
}

void assign_vec3(const toml::table& table, const char* key, glm::vec3& target)
{
    if (auto parsed = toml_support::get_vec3(table, key); parsed.has_value())
        target = *parsed;
}

void assign_legacy_color3(
    const toml::table& table, const char* key_r, const char* key_g, const char* key_b, glm::vec3& target)
{
    if (auto parsed = get_float(table, key_r); parsed.has_value())
        target.r = *parsed;
    if (auto parsed = get_float(table, key_g); parsed.has_value())
        target.g = *parsed;
    if (auto parsed = get_float(table, key_b); parsed.has_value())
        target.b = *parsed;
}

} // namespace

void apply_megacity_code_table(MegaCityCodeConfig& config, const toml::table& table)
{
    auto assign_float = [&](const char* key, float& target) {
        if (auto parsed = get_float(table, key); parsed.has_value())
            target = *parsed;
    };
    auto assign_int = [&](const char* key, int& target) {
        if (auto parsed = toml_support::get_int(table, key); parsed.has_value())
            target = static_cast<int>(*parsed);
    };
    auto assign_bool = [&](const char* key, bool& target) {
        if (auto parsed = toml_support::get_bool(table, key); parsed.has_value())
            target = *parsed;
    };

    if (auto selected_module_path = toml_support::get_string(table, "selected_module_path"); selected_module_path.has_value())
        config.selected_module_path = *selected_module_path;
    assign_vec2(table, "sign_text_px_range", config.sign_text_px_range);
    if (auto dv = toml_support::get_string(table, "debug_view"))
    {
        if (auto parsed = parse_megacity_debug_view(*dv); parsed.has_value())
            config.debug_view = *parsed;
    }
    else if (auto legacy_dv = toml_support::get_string(table, "ao_debug_view"))
    {
        if (auto parsed = parse_megacity_debug_view(*legacy_dv); parsed.has_value())
            config.debug_view = *parsed;
    }
    assign_bool("wireframe", config.wireframe);
    assign_bool("ao_denoise", config.ao_denoise);
    assign_float("ao_radius", config.ao_radius);
    assign_float("ao_bias", config.ao_bias);
    assign_float("ao_power", config.ao_power);
    assign_int("ao_kernel_size", config.ao_kernel_size);
    assign_float("height_multiplier", config.height_multiplier);
    assign_bool("clamp_semantic_metrics", config.clamp_semantic_metrics);
    assign_bool("hide_test_entities", config.hide_test_entities);
    assign_bool("hide_struct_entities", config.hide_struct_entities);
    assign_bool("point_shadow_debug_scene", config.point_shadow_debug_scene);
    assign_bool("auto_rebuild", config.auto_rebuild);
    assign_bool("show_ui_panels", config.show_ui_panels);
    assign_float("selection_dependency_alpha", config.selection_dependency_alpha);
    assign_float("selection_hidden_alpha", config.selection_hidden_alpha);
    assign_float("selection_hidden_hover_alpha", config.selection_hidden_hover_alpha);
    assign_float("selection_hidden_hover_raise_seconds", config.selection_hidden_hover_raise_seconds);
    assign_float("selection_hidden_hover_fall_seconds", config.selection_hidden_hover_fall_seconds);
    assign_float("selection_hidden_road_alpha", config.selection_hidden_road_alpha);

    assign_float("placement_step", config.placement_step);
    assign_int("max_spiral_rings", config.max_spiral_rings);

    assign_float("footprint_base", config.footprint_base);
    assign_vec2(table, "footprint_range", config.footprint_range);
    assign_float("footprint_unclamped_scale", config.footprint_unclamped_scale);

    assign_float("height_base", config.height_base);
    assign_float("height_mass_weight", config.height_mass_weight);
    assign_float("height_count_weight", config.height_count_weight);
    assign_vec2(table, "height_range", config.height_range);
    assign_float("height_unclamped_count_weight", config.height_unclamped_count_weight);
    assign_int("connected_hex_building_threshold", config.connected_hex_building_threshold);
    assign_int("connected_oct_building_threshold", config.connected_oct_building_threshold);
    assign_float("building_middle_strip_push", config.building_middle_strip_push);
    assign_float("building_alternate_darkening", config.building_alternate_darkening);
    if (auto om = toml_support::get_string(table, "overlay_mode"))
    {
        if (*om == "perf")
            config.overlay_mode = OverlayMode::Perf;
        else if (*om == "coverage")
            config.overlay_mode = OverlayMode::Coverage;
        else
            config.overlay_mode = OverlayMode::None;
    }
    else
    {
        // Legacy: migrate old booleans.
        bool legacy_perf = false;
        bool legacy_coverage = false;
        assign_bool("performance_heat_mode", legacy_perf);
        assign_bool("performance_heat_coverage_mode", legacy_coverage);
        if (legacy_coverage)
            config.overlay_mode = OverlayMode::Coverage;
        else if (legacy_perf)
            config.overlay_mode = OverlayMode::Perf;
    }
    assign_float("performance_heat_log_scale", config.performance_heat_log_scale);
    assign_float("flat_color_roughness", config.flat_color_roughness);
    assign_float("flat_color_metallic", config.flat_color_metallic);

    assign_float("road_width_base", config.road_width_base);
    assign_float("road_width_scale", config.road_width_scale);
    assign_vec2(table, "road_width_range", config.road_width_range);
    assign_float("sidewalk_width", config.sidewalk_width);
    assign_float("module_border_alpha", config.module_border_alpha);
    assign_float("dependency_route_layer_step", config.dependency_route_layer_step);
    assign_float("park_footprint", config.park_footprint);
    assign_float("park_height", config.park_height);
    assign_float("park_sidewalk_width", config.park_sidewalk_width);
    assign_float("park_road_width", config.park_road_width);
    assign_float("park_sign_max_depth_fraction", config.park_sign_max_depth_fraction);
    assign_vec2(table, "central_park_scale", config.central_park_scale);
    assign_float("central_park_tree_age_years", config.central_park_tree_age_years);
    assign_int("central_park_tree_seed", config.central_park_tree_seed);
    assign_float("central_park_tree_overall_scale", config.central_park_tree_overall_scale);
    assign_int("central_park_tree_radial_segments", config.central_park_tree_radial_segments);
    assign_int("central_park_tree_max_branch_depth", config.central_park_tree_max_branch_depth);
    assign_int("central_park_tree_child_branches_min", config.central_park_tree_child_branches_min);
    assign_int("central_park_tree_child_branches_max", config.central_park_tree_child_branches_max);
    assign_float("central_park_tree_branch_length_scale", config.central_park_tree_branch_length_scale);
    assign_float("central_park_tree_branch_radius_scale", config.central_park_tree_branch_radius_scale);
    assign_float("central_park_tree_upward_bias", config.central_park_tree_upward_bias);
    assign_float("central_park_tree_outward_bias", config.central_park_tree_outward_bias);
    assign_float("central_park_tree_curvature", config.central_park_tree_curvature);
    assign_float("central_park_tree_trunk_wander", config.central_park_tree_trunk_wander);
    assign_float("central_park_tree_branch_wander", config.central_park_tree_branch_wander);
    assign_float("central_park_tree_wander_frequency", config.central_park_tree_wander_frequency);
    assign_float("central_park_tree_wander_deviation", config.central_park_tree_wander_deviation);
    assign_float("central_park_tree_leaf_density", config.central_park_tree_leaf_density);
    assign_float(
        "central_park_tree_leaf_orientation_randomness",
        config.central_park_tree_leaf_orientation_randomness);
    assign_vec2(table, "central_park_tree_leaf_size_range", config.central_park_tree_leaf_size_range);
    assign_int("central_park_tree_leaf_start_depth", config.central_park_tree_leaf_start_depth);
    assign_float("central_park_tree_bark_color_noise", config.central_park_tree_bark_color_noise);
    assign_vec3(table, "central_park_tree_bark_root", config.central_park_tree_bark_root);
    assign_vec3(table, "central_park_tree_bark_tip", config.central_park_tree_bark_tip);

    assign_float("sign_label_point_size", config.sign_label_point_size);
    assign_float("tooltip_point_size", config.tooltip_point_size);
    assign_vec3(table, "module_sign_board_color", config.module_sign_board_color);
    assign_vec3(table, "module_sign_text_color", config.module_sign_text_color);
    assign_vec3(table, "building_sign_board_color", config.building_sign_board_color);
    assign_vec3(table, "building_sign_text_color", config.building_sign_text_color);
    assign_legacy_color3(
        table, "module_sign_board_r", "module_sign_board_g", "module_sign_board_b", config.module_sign_board_color);
    assign_legacy_color3(
        table, "module_sign_text_r", "module_sign_text_g", "module_sign_text_b", config.module_sign_text_color);
    assign_legacy_color3(
        table, "building_sign_board_r", "building_sign_board_g", "building_sign_board_b",
        config.building_sign_board_color);
    assign_legacy_color3(
        table, "building_sign_text_r", "building_sign_text_g", "building_sign_text_b", config.building_sign_text_color);
    assign_legacy_color3(table, "flat_sign_board_r", "flat_sign_board_g", "flat_sign_board_b", config.module_sign_board_color);
    assign_legacy_color3(table, "flat_sign_text_r", "flat_sign_text_g", "flat_sign_text_b", config.module_sign_text_color);
    assign_legacy_color3(table, "wall_sign_board_r", "wall_sign_board_g", "wall_sign_board_b", config.building_sign_board_color);
    assign_legacy_color3(table, "wall_sign_text_r", "wall_sign_text_g", "wall_sign_text_b", config.building_sign_text_color);
    assign_legacy_color3(table, "sign_text_r", "sign_text_g", "sign_text_b", config.module_sign_text_color);
    assign_float("roof_sign_thickness", config.roof_sign_thickness);
    assign_float("roof_sign_min_width_per_character", config.roof_sign_min_width_per_character);
    assign_float("wall_sign_thickness", config.wall_sign_thickness);
    assign_float("wall_sign_face_gap", config.wall_sign_face_gap);
    assign_float("wall_sign_side_inset", config.wall_sign_side_inset);
    assign_int("wall_sign_text_padding", config.wall_sign_text_padding);
    assign_float("road_sign_edge_inset", config.road_sign_edge_inset);
    assign_float("minimum_road_sign_depth", config.minimum_road_sign_depth);
    assign_float("sidewalk_sign_edge_inset", config.sidewalk_sign_edge_inset);
    assign_float("road_sign_lift", config.road_sign_lift);

    assign_float("road_surface_height", config.road_surface_height);
    assign_float("sidewalk_surface_height", config.sidewalk_surface_height);
    assign_float("sidewalk_surface_lift", config.sidewalk_surface_lift);
    assign_float("world_floor_height_scale", config.world_floor_height_scale);
    assign_float("world_floor_top_y", config.world_floor_top_y);
    assign_float("world_floor_grid_y_offset", config.world_floor_grid_y_offset);
    assign_float("world_floor_grid_tile_scale", config.world_floor_grid_tile_scale);
    assign_float("world_floor_grid_line_width", config.world_floor_grid_line_width);

    assign_float("ambient_strength", config.ambient_strength);
    assign_float("tone_map_exposure", config.tone_map_exposure);
    assign_float("tone_map_white_point", config.tone_map_white_point);
    assign_vec3(table, "directional_light_dir", config.directional_light_dir);
    assign_bool("point_light_position_valid", config.point_light_position_valid);
    assign_vec3(table, "point_light_position", config.point_light_position);
    assign_float("point_light_radius", config.point_light_radius);
    assign_float("point_light_brightness", config.point_light_brightness);
    assign_bool("camera_state_valid", config.camera_state_valid);
    assign_vec2(table, "camera_target", config.camera_target);
    assign_float("camera_yaw", config.camera_yaw);
    assign_float("camera_pitch", config.camera_pitch);
    assign_float("camera_orbit_radius", config.camera_orbit_radius);
    assign_float("camera_zoom_half_height", config.camera_zoom_half_height);
}

toml::table serialize_megacity_code_table(const MegaCityCodeConfig& config)
{
    toml::table table;
    table.insert_or_assign("selected_module_path", config.selected_module_path);
    toml_support::insert_vec2(table, "sign_text_px_range", config.sign_text_px_range);
    table.insert_or_assign("debug_view", std::string(format_megacity_debug_view(config.debug_view)));
    table.insert_or_assign("wireframe", config.wireframe);
    table.insert_or_assign("ao_denoise", config.ao_denoise);
    table.insert_or_assign("ao_radius", static_cast<double>(config.ao_radius));
    table.insert_or_assign("ao_bias", static_cast<double>(config.ao_bias));
    table.insert_or_assign("ao_power", static_cast<double>(config.ao_power));
    table.insert_or_assign("ao_kernel_size", config.ao_kernel_size);
    table.insert_or_assign("height_multiplier", static_cast<double>(config.height_multiplier));
    table.insert_or_assign("clamp_semantic_metrics", config.clamp_semantic_metrics);
    table.insert_or_assign("hide_test_entities", config.hide_test_entities);
    table.insert_or_assign("hide_struct_entities", config.hide_struct_entities);
    table.insert_or_assign("point_shadow_debug_scene", config.point_shadow_debug_scene);
    table.insert_or_assign("auto_rebuild", config.auto_rebuild);
    table.insert_or_assign("show_ui_panels", config.show_ui_panels);
    table.insert_or_assign("selection_dependency_alpha", static_cast<double>(config.selection_dependency_alpha));
    table.insert_or_assign("selection_hidden_alpha", static_cast<double>(config.selection_hidden_alpha));
    table.insert_or_assign("selection_hidden_hover_alpha", static_cast<double>(config.selection_hidden_hover_alpha));
    table.insert_or_assign("selection_hidden_hover_raise_seconds",
        static_cast<double>(config.selection_hidden_hover_raise_seconds));
    table.insert_or_assign("selection_hidden_hover_fall_seconds",
        static_cast<double>(config.selection_hidden_hover_fall_seconds));
    table.insert_or_assign("selection_hidden_road_alpha", static_cast<double>(config.selection_hidden_road_alpha));
    table.insert_or_assign("placement_step", static_cast<double>(config.placement_step));
    table.insert_or_assign("max_spiral_rings", config.max_spiral_rings);
    table.insert_or_assign("footprint_base", static_cast<double>(config.footprint_base));
    toml_support::insert_vec2(table, "footprint_range", config.footprint_range);
    table.insert_or_assign("footprint_unclamped_scale", static_cast<double>(config.footprint_unclamped_scale));
    table.insert_or_assign("height_base", static_cast<double>(config.height_base));
    table.insert_or_assign("height_mass_weight", static_cast<double>(config.height_mass_weight));
    table.insert_or_assign("height_count_weight", static_cast<double>(config.height_count_weight));
    toml_support::insert_vec2(table, "height_range", config.height_range);
    table.insert_or_assign("height_unclamped_count_weight", static_cast<double>(config.height_unclamped_count_weight));
    table.insert_or_assign("connected_hex_building_threshold", config.connected_hex_building_threshold);
    table.insert_or_assign("connected_oct_building_threshold", config.connected_oct_building_threshold);
    table.insert_or_assign("building_middle_strip_push", static_cast<double>(config.building_middle_strip_push));
    table.insert_or_assign("building_alternate_darkening", static_cast<double>(config.building_alternate_darkening));
    {
        const char* om_str = "none";
        if (config.overlay_mode == OverlayMode::Perf)
            om_str = "perf";
        else if (config.overlay_mode == OverlayMode::Coverage)
            om_str = "coverage";
        table.insert_or_assign("overlay_mode", std::string(om_str));
    }
    table.insert_or_assign("performance_heat_log_scale", static_cast<double>(config.performance_heat_log_scale));
    table.insert_or_assign("flat_color_roughness", static_cast<double>(config.flat_color_roughness));
    table.insert_or_assign("flat_color_metallic", static_cast<double>(config.flat_color_metallic));
    table.insert_or_assign("road_width_base", static_cast<double>(config.road_width_base));
    table.insert_or_assign("road_width_scale", static_cast<double>(config.road_width_scale));
    toml_support::insert_vec2(table, "road_width_range", config.road_width_range);
    table.insert_or_assign("sidewalk_width", static_cast<double>(config.sidewalk_width));
    table.insert_or_assign("module_border_alpha", static_cast<double>(config.module_border_alpha));
    table.insert_or_assign("dependency_route_layer_step", static_cast<double>(config.dependency_route_layer_step));
    table.insert_or_assign("park_footprint", static_cast<double>(config.park_footprint));
    table.insert_or_assign("park_height", static_cast<double>(config.park_height));
    table.insert_or_assign("park_sidewalk_width", static_cast<double>(config.park_sidewalk_width));
    table.insert_or_assign("park_road_width", static_cast<double>(config.park_road_width));
    table.insert_or_assign("park_sign_max_depth_fraction", static_cast<double>(config.park_sign_max_depth_fraction));
    toml_support::insert_vec2(table, "central_park_scale", config.central_park_scale);
    table.insert_or_assign("central_park_tree_age_years", static_cast<double>(config.central_park_tree_age_years));
    table.insert_or_assign("central_park_tree_seed", config.central_park_tree_seed);
    table.insert_or_assign("central_park_tree_overall_scale", static_cast<double>(config.central_park_tree_overall_scale));
    table.insert_or_assign("central_park_tree_radial_segments", config.central_park_tree_radial_segments);
    table.insert_or_assign("central_park_tree_max_branch_depth", config.central_park_tree_max_branch_depth);
    table.insert_or_assign("central_park_tree_child_branches_min", config.central_park_tree_child_branches_min);
    table.insert_or_assign("central_park_tree_child_branches_max", config.central_park_tree_child_branches_max);
    table.insert_or_assign("central_park_tree_branch_length_scale", static_cast<double>(config.central_park_tree_branch_length_scale));
    table.insert_or_assign("central_park_tree_branch_radius_scale", static_cast<double>(config.central_park_tree_branch_radius_scale));
    table.insert_or_assign("central_park_tree_upward_bias", static_cast<double>(config.central_park_tree_upward_bias));
    table.insert_or_assign("central_park_tree_outward_bias", static_cast<double>(config.central_park_tree_outward_bias));
    table.insert_or_assign("central_park_tree_curvature", static_cast<double>(config.central_park_tree_curvature));
    table.insert_or_assign("central_park_tree_trunk_wander", static_cast<double>(config.central_park_tree_trunk_wander));
    table.insert_or_assign("central_park_tree_branch_wander", static_cast<double>(config.central_park_tree_branch_wander));
    table.insert_or_assign("central_park_tree_wander_frequency", static_cast<double>(config.central_park_tree_wander_frequency));
    table.insert_or_assign("central_park_tree_wander_deviation", static_cast<double>(config.central_park_tree_wander_deviation));
    table.insert_or_assign("central_park_tree_leaf_density", static_cast<double>(config.central_park_tree_leaf_density));
    table.insert_or_assign(
        "central_park_tree_leaf_orientation_randomness",
        static_cast<double>(config.central_park_tree_leaf_orientation_randomness));
    toml_support::insert_vec2(table, "central_park_tree_leaf_size_range", config.central_park_tree_leaf_size_range);
    table.insert_or_assign("central_park_tree_leaf_start_depth", config.central_park_tree_leaf_start_depth);
    table.insert_or_assign("central_park_tree_bark_color_noise", static_cast<double>(config.central_park_tree_bark_color_noise));
    table.insert_or_assign("central_park_tree_bark_root", toml_support::make_array(config.central_park_tree_bark_root));
    table.insert_or_assign("central_park_tree_bark_tip", toml_support::make_array(config.central_park_tree_bark_tip));
    table.insert_or_assign("sign_label_point_size", static_cast<double>(config.sign_label_point_size));
    table.insert_or_assign("tooltip_point_size", static_cast<double>(config.tooltip_point_size));
    toml_support::insert_vec3(table, "module_sign_board_color", config.module_sign_board_color);
    toml_support::insert_vec3(table, "module_sign_text_color", config.module_sign_text_color);
    toml_support::insert_vec3(table, "building_sign_board_color", config.building_sign_board_color);
    toml_support::insert_vec3(table, "building_sign_text_color", config.building_sign_text_color);
    table.insert_or_assign("roof_sign_thickness", static_cast<double>(config.roof_sign_thickness));
    table.insert_or_assign(
        "roof_sign_min_width_per_character",
        static_cast<double>(config.roof_sign_min_width_per_character));
    table.insert_or_assign("wall_sign_thickness", static_cast<double>(config.wall_sign_thickness));
    table.insert_or_assign("wall_sign_face_gap", static_cast<double>(config.wall_sign_face_gap));
    table.insert_or_assign("wall_sign_side_inset", static_cast<double>(config.wall_sign_side_inset));
    table.insert_or_assign("wall_sign_text_padding", config.wall_sign_text_padding);
    table.insert_or_assign("road_sign_edge_inset", static_cast<double>(config.road_sign_edge_inset));
    table.insert_or_assign("minimum_road_sign_depth", static_cast<double>(config.minimum_road_sign_depth));
    table.insert_or_assign("sidewalk_sign_edge_inset", static_cast<double>(config.sidewalk_sign_edge_inset));
    table.insert_or_assign("road_sign_lift", static_cast<double>(config.road_sign_lift));
    table.insert_or_assign("road_surface_height", static_cast<double>(config.road_surface_height));
    table.insert_or_assign("sidewalk_surface_height", static_cast<double>(config.sidewalk_surface_height));
    table.insert_or_assign("sidewalk_surface_lift", static_cast<double>(config.sidewalk_surface_lift));
    table.insert_or_assign("world_floor_height_scale", static_cast<double>(config.world_floor_height_scale));
    table.insert_or_assign("world_floor_top_y", static_cast<double>(config.world_floor_top_y));
    table.insert_or_assign("world_floor_grid_y_offset", static_cast<double>(config.world_floor_grid_y_offset));
    table.insert_or_assign("world_floor_grid_tile_scale", static_cast<double>(config.world_floor_grid_tile_scale));
    table.insert_or_assign("world_floor_grid_line_width", static_cast<double>(config.world_floor_grid_line_width));
    table.insert_or_assign("ambient_strength", static_cast<double>(config.ambient_strength));
    table.insert_or_assign("tone_map_exposure", static_cast<double>(config.tone_map_exposure));
    table.insert_or_assign("tone_map_white_point", static_cast<double>(config.tone_map_white_point));
    toml_support::insert_vec3(table, "directional_light_dir", config.directional_light_dir);
    table.insert_or_assign("point_light_position_valid", config.point_light_position_valid);
    toml_support::insert_vec3(table, "point_light_position", config.point_light_position);
    table.insert_or_assign("point_light_radius", static_cast<double>(config.point_light_radius));
    table.insert_or_assign("point_light_brightness", static_cast<double>(config.point_light_brightness));
    table.insert_or_assign("camera_state_valid", config.camera_state_valid);
    toml_support::insert_vec2(table, "camera_target", config.camera_target);
    table.insert_or_assign("camera_yaw", static_cast<double>(config.camera_yaw));
    table.insert_or_assign("camera_pitch", static_cast<double>(config.camera_pitch));
    table.insert_or_assign("camera_orbit_radius", static_cast<double>(config.camera_orbit_radius));
    table.insert_or_assign("camera_zoom_half_height", static_cast<double>(config.camera_zoom_half_height));
    return table;
}

MegaCityCodeConfig load_megacity_code_defaults(const ConfigDocument& document)
{
    MegaCityCodeConfig defaults;
    if (const toml::table* table = document.find_table("mega_city_code.defaults"))
        apply_megacity_code_table(defaults, *table);
    return defaults;
}

MegaCityCodeConfig load_megacity_code_config(const ConfigDocument& document, const MegaCityCodeConfig& defaults)
{
    MegaCityCodeConfig config = defaults;
    if (const toml::table* table = document.find_table("mega_city_code"))
        apply_megacity_code_table(config, *table);
    return config;
}

void store_megacity_code_config(ConfigDocument& document, const MegaCityCodeConfig& current, const MegaCityCodeConfig& defaults)
{
    toml::table& section = document.ensure_table("mega_city_code");
    section = serialize_megacity_code_table(current);
    section.insert_or_assign("defaults", serialize_megacity_code_table(defaults));
}

} // namespace draxul
