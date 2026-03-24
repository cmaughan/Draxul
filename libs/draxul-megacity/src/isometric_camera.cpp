#include "isometric_camera.h"

#include <algorithm>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace draxul
{

void IsometricCamera::set_viewport(int pixel_w, int pixel_h)
{
    aspect_ = (pixel_h > 0) ? static_cast<float>(pixel_w) / static_cast<float>(pixel_h) : 1.0f;
}

void IsometricCamera::look_at_world_center(float world_w, float world_h)
{
    target_ = { world_w * 0.5f, 0.0f, world_h * 0.5f };

    const float max_dim = std::max(world_w, world_h);
    ortho_half_height_ = std::max(4.0f, max_dim * 0.8f);
    position_ = target_ + glm::vec3(-max_dim, max_dim * 1.25f, -max_dim);
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

} // namespace draxul
