#pragma once

#include <glm/glm.hpp>

namespace draxul
{

// Neutral pixel-space pane bounds shared by shell layout and grid rendering.
struct PaneDescriptor
{
    glm::ivec2 pixel_pos{ 0 };
    glm::ivec2 pixel_size{ 0 };
};

} // namespace draxul
