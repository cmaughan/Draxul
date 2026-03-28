#pragma once

#include "isometric_scene_types.h"

#include <array>
#include <glm/glm.hpp>

namespace draxul
{

struct DirectionalShadowCascade
{
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f };
    float split_depth = 1.0f;
};

struct DirectionalShadowCascadeSet
{
    std::array<DirectionalShadowCascade, kShadowCascadeCount> cascades{};
    uint32_t cascade_count = kShadowCascadeCount;
    int resolution = 2048;
    float sample_depth_bias = 0.0015f;
    float normal_bias = 0.04f;
};

[[nodiscard]] DirectionalShadowCascadeSet build_directional_shadow_cascades(
    const SceneCameraData& camera,
    int resolution = 2048);

[[nodiscard]] glm::mat4 shadow_texture_matrix(const glm::mat4& world_to_clip);

} // namespace draxul
