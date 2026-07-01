#pragma once

#include "codeviz_scene_types.h"

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
    int resolution = 4096;
    float sample_depth_bias = 0.0005f;
    float normal_bias = 0.015f;
};

struct PointShadowMapSet
{
    std::array<glm::mat4, kPointShadowFaceCount> view_proj{};
    int resolution = 1024;
    float sample_depth_bias = 0.00075f;
    float normal_bias = 0.004f;
    bool valid = false;
};

[[nodiscard]] DirectionalShadowCascadeSet build_directional_shadow_cascades(
    const CodeVizCameraData& camera,
    int resolution = 4096);

[[nodiscard]] PointShadowMapSet build_point_shadow_map(
    const CodeVizCameraData& camera,
    int resolution = 1024);

[[nodiscard]] glm::mat4 shadow_texture_matrix(const glm::mat4& world_to_clip);

} // namespace draxul
