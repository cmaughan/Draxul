#include <draxul/megacity_code_config.h>

#include <draxul/toml_support.h>

namespace draxul
{

std::optional<MegaCitySignPlacement> parse_megacity_sign_placement(std::string_view value)
{
    if (value == "roof_north")
        return MegaCitySignPlacement::RoofNorth;
    if (value == "roof_south")
        return MegaCitySignPlacement::RoofSouth;
    if (value == "roof_east")
        return MegaCitySignPlacement::RoofEast;
    if (value == "roof_west")
        return MegaCitySignPlacement::RoofWest;
    if (value == "wall_north")
        return MegaCitySignPlacement::WallNorth;
    if (value == "wall_south")
        return MegaCitySignPlacement::WallSouth;
    if (value == "wall_east")
        return MegaCitySignPlacement::WallEast;
    if (value == "wall_west")
        return MegaCitySignPlacement::WallWest;
    return std::nullopt;
}

std::string_view format_megacity_sign_placement(MegaCitySignPlacement value)
{
    switch (value)
    {
    case MegaCitySignPlacement::RoofNorth:
        return "roof_north";
    case MegaCitySignPlacement::RoofSouth:
        return "roof_south";
    case MegaCitySignPlacement::RoofEast:
        return "roof_east";
    case MegaCitySignPlacement::RoofWest:
        return "roof_west";
    case MegaCitySignPlacement::WallNorth:
        return "wall_north";
    case MegaCitySignPlacement::WallSouth:
        return "wall_south";
    case MegaCitySignPlacement::WallEast:
        return "wall_east";
    case MegaCitySignPlacement::WallWest:
        return "wall_west";
    }
    return "wall_east";
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

    assign_float("sign_text_hidden_px", config.sign_text_hidden_px);
    assign_float("sign_text_full_px", config.sign_text_full_px);
    assign_float("output_gamma", config.output_gamma);
    assign_float("height_multiplier", config.height_multiplier);
    assign_bool("clamp_semantic_metrics", config.clamp_semantic_metrics);
    assign_bool("hide_test_entities", config.hide_test_entities);
    assign_bool("auto_rebuild", config.auto_rebuild);

    assign_float("placement_step", config.placement_step);
    assign_int("max_spiral_rings", config.max_spiral_rings);
    assign_float("lot_road_reserve_fraction", config.lot_road_reserve_fraction);

    assign_float("footprint_base", config.footprint_base);
    assign_float("footprint_min", config.footprint_min);
    assign_float("footprint_max", config.footprint_max);
    assign_float("footprint_unclamped_scale", config.footprint_unclamped_scale);

    assign_float("height_base", config.height_base);
    assign_float("height_mass_weight", config.height_mass_weight);
    assign_float("height_count_weight", config.height_count_weight);
    assign_float("height_min", config.height_min);
    assign_float("height_max", config.height_max);
    assign_float("height_unclamped_count_weight", config.height_unclamped_count_weight);

    assign_float("road_width_base", config.road_width_base);
    assign_float("road_width_scale", config.road_width_scale);
    assign_float("road_width_min", config.road_width_min);
    assign_float("road_width_max", config.road_width_max);
    assign_float("sidewalk_width", config.sidewalk_width);

    assign_float("sign_label_point_size", config.sign_label_point_size);
    if (auto placement = toml_support::get_string(table, "building_sign_placement"))
    {
        if (auto parsed = parse_megacity_sign_placement(*placement); parsed.has_value())
            config.building_sign_placement = *parsed;
    }
    assign_float("roof_sign_thickness", config.roof_sign_thickness);
    assign_float("roof_sign_depth", config.roof_sign_depth);
    assign_float("roof_sign_edge_inset", config.roof_sign_edge_inset);
    assign_float("roof_sign_side_inset", config.roof_sign_side_inset);
    assign_float("wall_sign_thickness", config.wall_sign_thickness);
    assign_float("wall_sign_face_gap", config.wall_sign_face_gap);
    assign_float("wall_sign_width", config.wall_sign_width);
    assign_float("wall_sign_side_inset", config.wall_sign_side_inset);
    assign_float("wall_sign_top_inset", config.wall_sign_top_inset);
    assign_float("wall_sign_bottom_inset", config.wall_sign_bottom_inset);
    assign_int("wall_sign_text_padding", config.wall_sign_text_padding);
    assign_float("road_sign_edge_inset", config.road_sign_edge_inset);
    assign_float("road_sign_side_inset", config.road_sign_side_inset);
    assign_float("minimum_road_sign_depth", config.minimum_road_sign_depth);
    assign_float("sidewalk_sign_edge_inset", config.sidewalk_sign_edge_inset);
    assign_float("road_sign_lift", config.road_sign_lift);
    assign_float("roof_sign_pixels_per_world_unit", config.roof_sign_pixels_per_world_unit);

    assign_float("road_surface_height", config.road_surface_height);
    assign_float("sidewalk_surface_height", config.sidewalk_surface_height);
    assign_float("sidewalk_surface_lift", config.sidewalk_surface_lift);
    assign_float("world_floor_height_scale", config.world_floor_height_scale);
    assign_float("world_floor_top_y", config.world_floor_top_y);
    assign_float("world_floor_grid_y_offset", config.world_floor_grid_y_offset);
    assign_float("world_floor_grid_tile_scale", config.world_floor_grid_tile_scale);
    assign_float("world_floor_grid_line_width", config.world_floor_grid_line_width);

    assign_float("ambient_strength", config.ambient_strength);
    assign_float("directional_light_x", config.directional_light_x);
    assign_float("directional_light_y", config.directional_light_y);
    assign_float("directional_light_z", config.directional_light_z);
    assign_bool("point_light_position_valid", config.point_light_position_valid);
    assign_float("point_light_x", config.point_light_x);
    assign_float("point_light_y", config.point_light_y);
    assign_float("point_light_z", config.point_light_z);
    assign_float("point_light_radius", config.point_light_radius);
    assign_float("point_light_brightness", config.point_light_brightness);
    assign_bool("camera_state_valid", config.camera_state_valid);
    assign_float("camera_target_x", config.camera_target_x);
    assign_float("camera_target_z", config.camera_target_z);
    assign_float("camera_yaw", config.camera_yaw);
    assign_float("camera_pitch", config.camera_pitch);
    assign_float("camera_orbit_radius", config.camera_orbit_radius);
    assign_float("camera_zoom_half_height", config.camera_zoom_half_height);
}

toml::table serialize_megacity_code_table(const MegaCityCodeConfig& config)
{
    toml::table table;
    table.insert_or_assign("sign_text_hidden_px", static_cast<double>(config.sign_text_hidden_px));
    table.insert_or_assign("sign_text_full_px", static_cast<double>(config.sign_text_full_px));
    table.insert_or_assign("output_gamma", static_cast<double>(config.output_gamma));
    table.insert_or_assign("height_multiplier", static_cast<double>(config.height_multiplier));
    table.insert_or_assign("clamp_semantic_metrics", config.clamp_semantic_metrics);
    table.insert_or_assign("hide_test_entities", config.hide_test_entities);
    table.insert_or_assign("auto_rebuild", config.auto_rebuild);
    table.insert_or_assign("placement_step", static_cast<double>(config.placement_step));
    table.insert_or_assign("max_spiral_rings", config.max_spiral_rings);
    table.insert_or_assign("lot_road_reserve_fraction", static_cast<double>(config.lot_road_reserve_fraction));
    table.insert_or_assign("footprint_base", static_cast<double>(config.footprint_base));
    table.insert_or_assign("footprint_min", static_cast<double>(config.footprint_min));
    table.insert_or_assign("footprint_max", static_cast<double>(config.footprint_max));
    table.insert_or_assign("footprint_unclamped_scale", static_cast<double>(config.footprint_unclamped_scale));
    table.insert_or_assign("height_base", static_cast<double>(config.height_base));
    table.insert_or_assign("height_mass_weight", static_cast<double>(config.height_mass_weight));
    table.insert_or_assign("height_count_weight", static_cast<double>(config.height_count_weight));
    table.insert_or_assign("height_min", static_cast<double>(config.height_min));
    table.insert_or_assign("height_max", static_cast<double>(config.height_max));
    table.insert_or_assign("height_unclamped_count_weight", static_cast<double>(config.height_unclamped_count_weight));
    table.insert_or_assign("road_width_base", static_cast<double>(config.road_width_base));
    table.insert_or_assign("road_width_scale", static_cast<double>(config.road_width_scale));
    table.insert_or_assign("road_width_min", static_cast<double>(config.road_width_min));
    table.insert_or_assign("road_width_max", static_cast<double>(config.road_width_max));
    table.insert_or_assign("sidewalk_width", static_cast<double>(config.sidewalk_width));
    table.insert_or_assign("sign_label_point_size", static_cast<double>(config.sign_label_point_size));
    table.insert_or_assign("building_sign_placement", std::string(format_megacity_sign_placement(config.building_sign_placement)));
    table.insert_or_assign("roof_sign_thickness", static_cast<double>(config.roof_sign_thickness));
    table.insert_or_assign("roof_sign_depth", static_cast<double>(config.roof_sign_depth));
    table.insert_or_assign("roof_sign_edge_inset", static_cast<double>(config.roof_sign_edge_inset));
    table.insert_or_assign("roof_sign_side_inset", static_cast<double>(config.roof_sign_side_inset));
    table.insert_or_assign("wall_sign_thickness", static_cast<double>(config.wall_sign_thickness));
    table.insert_or_assign("wall_sign_face_gap", static_cast<double>(config.wall_sign_face_gap));
    table.insert_or_assign("wall_sign_width", static_cast<double>(config.wall_sign_width));
    table.insert_or_assign("wall_sign_side_inset", static_cast<double>(config.wall_sign_side_inset));
    table.insert_or_assign("wall_sign_top_inset", static_cast<double>(config.wall_sign_top_inset));
    table.insert_or_assign("wall_sign_bottom_inset", static_cast<double>(config.wall_sign_bottom_inset));
    table.insert_or_assign("wall_sign_text_padding", config.wall_sign_text_padding);
    table.insert_or_assign("road_sign_edge_inset", static_cast<double>(config.road_sign_edge_inset));
    table.insert_or_assign("road_sign_side_inset", static_cast<double>(config.road_sign_side_inset));
    table.insert_or_assign("minimum_road_sign_depth", static_cast<double>(config.minimum_road_sign_depth));
    table.insert_or_assign("sidewalk_sign_edge_inset", static_cast<double>(config.sidewalk_sign_edge_inset));
    table.insert_or_assign("road_sign_lift", static_cast<double>(config.road_sign_lift));
    table.insert_or_assign("roof_sign_pixels_per_world_unit", static_cast<double>(config.roof_sign_pixels_per_world_unit));
    table.insert_or_assign("road_surface_height", static_cast<double>(config.road_surface_height));
    table.insert_or_assign("sidewalk_surface_height", static_cast<double>(config.sidewalk_surface_height));
    table.insert_or_assign("sidewalk_surface_lift", static_cast<double>(config.sidewalk_surface_lift));
    table.insert_or_assign("world_floor_height_scale", static_cast<double>(config.world_floor_height_scale));
    table.insert_or_assign("world_floor_top_y", static_cast<double>(config.world_floor_top_y));
    table.insert_or_assign("world_floor_grid_y_offset", static_cast<double>(config.world_floor_grid_y_offset));
    table.insert_or_assign("world_floor_grid_tile_scale", static_cast<double>(config.world_floor_grid_tile_scale));
    table.insert_or_assign("world_floor_grid_line_width", static_cast<double>(config.world_floor_grid_line_width));
    table.insert_or_assign("ambient_strength", static_cast<double>(config.ambient_strength));
    table.insert_or_assign("directional_light_x", static_cast<double>(config.directional_light_x));
    table.insert_or_assign("directional_light_y", static_cast<double>(config.directional_light_y));
    table.insert_or_assign("directional_light_z", static_cast<double>(config.directional_light_z));
    table.insert_or_assign("point_light_position_valid", config.point_light_position_valid);
    table.insert_or_assign("point_light_x", static_cast<double>(config.point_light_x));
    table.insert_or_assign("point_light_y", static_cast<double>(config.point_light_y));
    table.insert_or_assign("point_light_z", static_cast<double>(config.point_light_z));
    table.insert_or_assign("point_light_radius", static_cast<double>(config.point_light_radius));
    table.insert_or_assign("point_light_brightness", static_cast<double>(config.point_light_brightness));
    table.insert_or_assign("camera_state_valid", config.camera_state_valid);
    table.insert_or_assign("camera_target_x", static_cast<double>(config.camera_target_x));
    table.insert_or_assign("camera_target_z", static_cast<double>(config.camera_target_z));
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
