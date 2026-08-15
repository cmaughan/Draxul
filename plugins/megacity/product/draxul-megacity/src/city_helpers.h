#pragma once

#include <algorithm>
#include <draxul/megacity_code_config.h>
#include <draxul/pastel_palette.h>
#include <glm/glm.hpp>
#include <string_view>

namespace draxul
{

inline constexpr glm::vec3 kCatppuccinSurface0(0.192f, 0.196f, 0.266f);

inline glm::vec4 module_building_color(std::string_view module_path)
{
    const uint32_t hash = stable_color_hash(module_path);
    const glm::vec4 base = pastel_color_from_hash(hash);
    const uint32_t variant = hash / static_cast<uint32_t>(kPastelAccentPalette.size());
    if ((variant % 3u) == 0u)
        return base;
    if ((variant % 3u) == 1u)
        return glm::vec4(glm::mix(glm::vec3(base), glm::vec3(1.0f), 0.10f), base.a);
    return glm::vec4(glm::mix(glm::vec3(base), kCatppuccinSurface0, 0.12f), base.a);
}

inline glm::vec4 module_building_layer_color(const glm::vec4& module_color, size_t layer_index, float darkening)
{
    if ((layer_index % 2) == 0)
        return module_color;
    const glm::vec3 dark_band = glm::mix(
        glm::vec3(module_color),
        kCatppuccinSurface0,
        std::clamp(darkening, 0.0f, 1.0f));
    return glm::vec4(glm::clamp(dark_band, glm::vec3(0.0f), glm::vec3(1.0f)), module_color.a);
}

inline constexpr float kRoadSurfaceTextureLift = 0.002f;
inline constexpr float kDependencyRouteLift = 0.01f;

inline float building_base_elevation(const MegaCityCodeConfig& config)
{
    return config.sidewalk_surface_lift + config.sidewalk_surface_height;
}

inline float route_base_elevation(const MegaCityCodeConfig& config)
{
    return std::max(
               kRoadSurfaceTextureLift + config.road_surface_height,
               config.sidewalk_surface_lift + config.sidewalk_surface_height)
        + kDependencyRouteLift;
}

inline float world_floor_height(const MegaCityCodeConfig& config)
{
    return config.road_surface_height * config.world_floor_height_scale;
}

// Hollow-ring brick layout: for grid_size <= 2 all slots are filled;
// for grid_size >= 3 only the outer perimeter is used (center is empty).
inline int brick_slots_per_floor(int grid_size)
{
    if (grid_size <= 2)
        return grid_size * grid_size;
    return 4 * (grid_size - 1);
}

// Map a local slot index (0..brick_slots_per_floor-1) to (col, row) on the
// NxN grid.  For N<=2 this fills left-to-right, top-to-bottom.  For N>=3 it
// walks the perimeter: top row → right col → bottom row (reversed) → left col (reversed).
inline std::pair<int, int> brick_slot_position(int local_index, int grid_size)
{
    if (grid_size <= 2)
        return { local_index % grid_size, local_index / grid_size };

    const int perim = 4 * (grid_size - 1);
    const int idx = local_index % perim;
    const int n = grid_size - 1;
    if (idx < grid_size)
        return { idx, 0 }; // top row
    if (idx < grid_size + n - 1)
        return { n, idx - n }; // right column (skipping corner)
    if (idx < 2 * grid_size + n - 2)
        return { n - (idx - (grid_size + n - 1)), n }; // bottom row (reversed)
    return { 0, n - (idx - (2 * n + grid_size - 1)) }; // left column (reversed, skipping corners)
}

} // namespace draxul
