#include "isometric_camera.h"

#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace draxul
{

void IsometricCamera::set_viewport(int pixel_w, int pixel_h)
{
    aspect_ = (pixel_h > 0) ? static_cast<float>(pixel_w) / static_cast<float>(pixel_h) : 1.0f;
}

void IsometricCamera::look_at_world_center(float world_w, float world_h)
{
    const float max_dim = std::max(world_w, world_h);
    ortho_half_height_ = std::max(4.0f, max_dim * 0.8f);
    follow_offset_ = glm::vec3(-max_dim, max_dim * 1.25f, -max_dim);
    set_target({ world_w * 0.5f, 0.0f, world_h * 0.5f });
}

void IsometricCamera::set_target(const glm::vec3& target)
{
    target_ = target;
    position_ = target_ + follow_offset_;
}

glm::mat4 IsometricCamera::view_matrix() const
{
    return glm::lookAtRH(position_, target_, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 IsometricCamera::proj_matrix() const
{
    const float half_width = ortho_half_height_ * aspect_;
    return glm::orthoRH_ZO(-half_width, half_width, -ortho_half_height_, ortho_half_height_, 0.1f, 100.0f);
}

GroundFootprint IsometricCamera::visible_ground_footprint(float plane_y) const
{
    const glm::mat4 inv_view_proj = glm::inverse(proj_matrix() * view_matrix());

    GroundFootprint footprint;
    footprint.min_x = std::numeric_limits<float>::max();
    footprint.max_x = std::numeric_limits<float>::lowest();
    footprint.min_z = std::numeric_limits<float>::max();
    footprint.max_z = std::numeric_limits<float>::lowest();

    const glm::vec2 corners[4] = {
        { -1.0f, -1.0f },
        { 1.0f, -1.0f },
        { 1.0f, 1.0f },
        { -1.0f, 1.0f },
    };

    for (const glm::vec2& corner : corners)
    {
        glm::vec4 near_world = inv_view_proj * glm::vec4(corner, 0.0f, 1.0f);
        glm::vec4 far_world = inv_view_proj * glm::vec4(corner, 1.0f, 1.0f);
        near_world /= near_world.w;
        far_world /= far_world.w;

        const glm::vec3 near_pos = glm::vec3(near_world);
        const glm::vec3 far_pos = glm::vec3(far_world);
        const glm::vec3 ray = far_pos - near_pos;
        if (std::abs(ray.y) < 1e-5f)
            continue;

        const float t = (plane_y - near_pos.y) / ray.y;
        const glm::vec3 point = near_pos + ray * t;
        footprint.min_x = std::min(footprint.min_x, point.x);
        footprint.max_x = std::max(footprint.max_x, point.x);
        footprint.min_z = std::min(footprint.min_z, point.z);
        footprint.max_z = std::max(footprint.max_z, point.z);
    }

    if (footprint.min_x > footprint.max_x || footprint.min_z > footprint.max_z)
    {
        footprint.min_x = target_.x - ortho_half_height_;
        footprint.max_x = target_.x + ortho_half_height_;
        footprint.min_z = target_.z - ortho_half_height_;
        footprint.max_z = target_.z + ortho_half_height_;
    }

    return footprint;
}

} // namespace draxul
