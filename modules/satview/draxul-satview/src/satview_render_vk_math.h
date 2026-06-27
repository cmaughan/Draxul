#pragma once

#include <glm/mat4x4.hpp>

namespace draxul::satview
{

inline glm::mat4 make_satview_vulkan_clip_matrix(glm::mat4 world_to_clip)
{
    world_to_clip[0][1] *= -1.0f;
    world_to_clip[1][1] *= -1.0f;
    world_to_clip[2][1] *= -1.0f;
    world_to_clip[3][1] *= -1.0f;
    return world_to_clip;
}

} // namespace draxul::satview
