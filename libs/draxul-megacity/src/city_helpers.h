#pragma once

#include <algorithm>
#include <array>
#include <draxul/megacity_code_config.h>
#include <glm/glm.hpp>
#include <string_view>

namespace draxul
{

inline constexpr glm::vec3 kCatppuccinSurface0(0.192f, 0.196f, 0.266f);
inline constexpr std::array<glm::vec4, 26> kModuleAccentPalette = {
    glm::vec4(0.961f, 0.878f, 0.863f, 1.0f),
    glm::vec4(0.949f, 0.804f, 0.804f, 1.0f),
    glm::vec4(0.957f, 0.761f, 0.906f, 1.0f),
    glm::vec4(0.796f, 0.651f, 0.969f, 1.0f),
    glm::vec4(0.953f, 0.545f, 0.659f, 1.0f),
    glm::vec4(0.922f, 0.627f, 0.675f, 1.0f),
    glm::vec4(0.980f, 0.702f, 0.529f, 1.0f),
    glm::vec4(0.976f, 0.886f, 0.686f, 1.0f),
    glm::vec4(0.651f, 0.890f, 0.631f, 1.0f),
    glm::vec4(0.580f, 0.886f, 0.835f, 1.0f),
    glm::vec4(0.537f, 0.863f, 0.922f, 1.0f),
    glm::vec4(0.455f, 0.780f, 0.925f, 1.0f),
    glm::vec4(0.537f, 0.706f, 0.980f, 1.0f),
    glm::vec4(0.706f, 0.745f, 0.996f, 1.0f),
    glm::vec4(0.855f, 0.733f, 0.502f, 1.0f),
    glm::vec4(0.643f, 0.827f, 0.502f, 1.0f),
    glm::vec4(0.502f, 0.745f, 0.682f, 1.0f),
    glm::vec4(0.749f, 0.565f, 0.827f, 1.0f),
    glm::vec4(0.890f, 0.643f, 0.584f, 1.0f),
    glm::vec4(0.584f, 0.647f, 0.890f, 1.0f),
    glm::vec4(0.827f, 0.827f, 0.584f, 1.0f),
    glm::vec4(0.502f, 0.827f, 0.890f, 1.0f),
    glm::vec4(0.890f, 0.502f, 0.765f, 1.0f),
    glm::vec4(0.765f, 0.890f, 0.502f, 1.0f),
    glm::vec4(0.682f, 0.549f, 0.451f, 1.0f),
    glm::vec4(0.502f, 0.682f, 0.827f, 1.0f),
};

inline uint32_t stable_module_hash(std::string_view text)
{
    uint32_t hash = 2166136261u;
    for (const unsigned char ch : text)
    {
        hash ^= ch;
        hash *= 16777619u;
    }
    return hash;
}

inline glm::vec4 module_building_color(std::string_view module_path)
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

} // namespace draxul
