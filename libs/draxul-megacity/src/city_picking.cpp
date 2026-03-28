#include "city_picking.h"
#include "isometric_camera.h"
#include "semantic_city_layout.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace draxul
{

namespace
{

// Ray-AABB intersection test. Returns the distance along the ray (t), or negative if no hit.
float ray_aabb_intersect(
    const glm::vec3& ray_origin, const glm::vec3& ray_dir,
    const glm::vec3& aabb_min, const glm::vec3& aabb_max)
{
    float t_min = -std::numeric_limits<float>::max();
    float t_max = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; ++axis)
    {
        if (std::abs(ray_dir[axis]) < 1e-8f)
        {
            if (ray_origin[axis] < aabb_min[axis] || ray_origin[axis] > aabb_max[axis])
                return -1.0f;
        }
        else
        {
            float inv_d = 1.0f / ray_dir[axis];
            float t1 = (aabb_min[axis] - ray_origin[axis]) * inv_d;
            float t2 = (aabb_max[axis] - ray_origin[axis]) * inv_d;
            if (t1 > t2)
                std::swap(t1, t2);
            t_min = std::max(t_min, t1);
            t_max = std::min(t_max, t2);
            if (t_min > t_max)
                return -1.0f;
        }
    }

    return t_min;
}

} // namespace

std::optional<PickResult> pick_building(
    const glm::ivec2& screen_pos,
    int viewport_width, int viewport_height,
    const IsometricCamera& camera,
    const SemanticMegacityLayout& layout,
    const std::function<bool(const std::string&, const std::string&, const std::string&)>& filter)
{
    if (viewport_width <= 0 || viewport_height <= 0)
        return std::nullopt;

    // Convert screen pixel to NDC [-1, 1]
    const float ndc_x = 2.0f * static_cast<float>(screen_pos.x) / static_cast<float>(viewport_width) - 1.0f;
    const float ndc_y = 1.0f - 2.0f * static_cast<float>(screen_pos.y) / static_cast<float>(viewport_height);

    const glm::mat4 inv_vp = glm::inverse(camera.proj_matrix() * camera.view_matrix());

    // Unproject near and far points
    glm::vec4 near_h = inv_vp * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
    glm::vec4 far_h = inv_vp * glm::vec4(ndc_x, ndc_y, 1.0f, 1.0f);
    near_h /= near_h.w;
    far_h /= far_h.w;

    const glm::vec3 ray_origin(near_h);
    const glm::vec3 ray_dir = glm::normalize(glm::vec3(far_h) - glm::vec3(near_h));

    float best_t = std::numeric_limits<float>::max();
    std::optional<PickResult> best;

    for (const auto& module_layout : layout.modules)
    {
        for (const auto& building : module_layout.buildings)
        {
            if (filter
                && !filter(
                    building.source_file_path,
                    building.module_path,
                    building.qualified_name))
            {
                continue;
            }

            const float half_fp = building.metrics.footprint * 0.5f;
            const glm::vec3 aabb_min(
                building.center.x - half_fp,
                0.0f,
                building.center.y - half_fp);
            const glm::vec3 aabb_max(
                building.center.x + half_fp,
                building.metrics.height,
                building.center.y + half_fp);

            const float t = ray_aabb_intersect(ray_origin, ray_dir, aabb_min, aabb_max);
            if (t >= 0.0f && t < best_t)
            {
                best_t = t;
                const glm::vec3 hit_point = ray_origin + t * ray_dir;
                best = PickResult{
                    building.qualified_name,
                    building.module_path,
                    building.source_file_path,
                    building.center,
                    hit_point.y,
                };
            }
        }
    }

    return best;
}

} // namespace draxul
