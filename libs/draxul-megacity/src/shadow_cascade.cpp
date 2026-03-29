#include "shadow_cascade.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <draxul/perf_timing.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>

namespace draxul
{

namespace
{

constexpr std::array<float, kShadowCascadeCount> kCascadeSplitDepths = {
    0.12f,
    0.32f,
    1.0f,
};

struct PointShadowFaceOrientation
{
    glm::vec3 direction;
    glm::vec3 up;
};

constexpr std::array<PointShadowFaceOrientation, kPointShadowFaceCount> kPointShadowFaces = { {
    { glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f) },
    { glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f) },
    { glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f) },
    { glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f) },
    { glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f) },
    { glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f) },
} };

glm::vec3 unproject_corner(const glm::mat4& inv_view_proj, float ndc_x, float ndc_y, float ndc_z)
{
    glm::vec4 world = inv_view_proj * glm::vec4(ndc_x, ndc_y, ndc_z, 1.0f);
    return glm::vec3(world) / std::max(world.w, 1e-6f);
}

glm::vec3 choose_light_up(const glm::vec3& light_forward)
{
    const glm::vec3 world_up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(light_forward, world_up)) < 0.98f)
        return world_up;
    return glm::vec3(0.0f, 0.0f, 1.0f);
}

DirectionalShadowCascade build_cascade(
    const SceneCameraData& camera,
    float split_start,
    float split_end,
    int resolution)
{
    PERF_MEASURE();
    std::array<glm::vec3, 8> corners{};
    size_t corner_index = 0;
    for (float ndc_z : { split_start, split_end })
    {
        for (float ndc_y : { -1.0f, 1.0f })
        {
            for (float ndc_x : { -1.0f, 1.0f })
                corners[corner_index++] = unproject_corner(camera.inv_view_proj, ndc_x, ndc_y, ndc_z);
        }
    }

    glm::vec3 center(0.0f);
    for (const glm::vec3& corner : corners)
        center += corner;
    center /= static_cast<float>(corners.size());

    float radius2 = 0.0f;
    for (const glm::vec3& corner : corners)
        radius2 = std::max(radius2, glm::distance2(corner, center));
    float radius = std::sqrt(std::max(radius2, 1e-4f));
    radius = std::ceil(radius * 16.0f) / 16.0f;

    glm::vec3 light_forward = glm::vec3(camera.light_dir);
    if (glm::length2(light_forward) <= 1e-6f)
        light_forward = glm::vec3(-0.5f, -1.0f, -0.3f);
    light_forward = glm::normalize(light_forward);
    const glm::vec3 light_up = choose_light_up(light_forward);
    const glm::vec3 light_eye = center - light_forward * (radius * 4.0f + 32.0f);
    const glm::mat4 light_view = glm::lookAtRH(light_eye, center, light_up);
    const glm::vec3 center_ls = glm::vec3(light_view * glm::vec4(center, 1.0f));

    const float diameter = radius * 2.0f;
    const float texel_size = diameter / static_cast<float>(std::max(resolution, 1));
    glm::vec2 snapped_center(center_ls.x, center_ls.y);
    if (texel_size > 0.0f)
        snapped_center = glm::floor(snapped_center / texel_size) * texel_size;

    const float min_x = snapped_center.x - radius;
    const float max_x = snapped_center.x + radius;
    const float min_y = snapped_center.y - radius;
    const float max_y = snapped_center.y + radius;

    const float depth_pad = radius * 2.0f + 16.0f;
    const float near_plane = std::max(0.1f, -(center_ls.z + radius) - depth_pad);
    const float far_plane = std::max(near_plane + 1.0f, -(center_ls.z - radius) + depth_pad);
    const glm::mat4 light_proj = glm::orthoRH_ZO(min_x, max_x, min_y, max_y, near_plane, far_plane);

    return DirectionalShadowCascade{
        .view = light_view,
        .proj = light_proj,
        .split_depth = split_end,
    };
}

} // namespace

DirectionalShadowCascadeSet build_directional_shadow_cascades(const SceneCameraData& camera, int resolution)
{
    PERF_MEASURE();
    DirectionalShadowCascadeSet cascade_set;
    cascade_set.resolution = std::max(resolution, 256);

    float split_start = 0.0f;
    for (size_t cascade_index = 0; cascade_index < cascade_set.cascades.size(); ++cascade_index)
    {
        const float split_end = kCascadeSplitDepths[cascade_index];
        cascade_set.cascades[cascade_index] = build_cascade(camera, split_start, split_end, cascade_set.resolution);
        split_start = split_end;
    }

    return cascade_set;
}

PointShadowMapSet build_point_shadow_map(const SceneCameraData& camera, int resolution)
{
    PERF_MEASURE();
    PointShadowMapSet shadow_map;
    shadow_map.resolution = std::max(resolution, 256);

    const glm::vec3 light_position(camera.point_light_pos);
    const float requested_radius = camera.point_light_pos.w;
    if (requested_radius <= 0.0f)
        return shadow_map;
    const float radius = std::max(requested_radius, 1.0f);

    const float near_plane = 0.1f;
    const float far_plane = std::max(radius, near_plane + 1.0f);
    const glm::mat4 proj = glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, near_plane, far_plane);

    for (size_t face_index = 0; face_index < kPointShadowFaces.size(); ++face_index)
    {
        const auto& face = kPointShadowFaces[face_index];
        const glm::mat4 view = glm::lookAtRH(light_position, light_position + face.direction, face.up);
        shadow_map.view_proj[face_index] = proj * view;
    }

    shadow_map.valid = true;
    return shadow_map;
}

glm::mat4 shadow_texture_matrix(const glm::mat4& world_to_clip)
{
    PERF_MEASURE();
    glm::mat4 bias(1.0f);
    bias[0][0] = 0.5f;
    // Clip-space Y points up; sampled texture V points down.
    bias[1][1] = -0.5f;
    bias[3][0] = 0.5f;
    bias[3][1] = 0.5f;
    return bias * world_to_clip;
}

} // namespace draxul
