#include "isometric_world.h"

namespace draxul
{

IsometricWorld::IsometricWorld()
{
    GridObject cube;
    cube.x = static_cast<float>(kWidth) * 0.5f - 0.5f;
    cube.y = static_cast<float>(kHeight) * 0.5f - 0.5f;
    cube.elevation = 0.0f;
    cube.color = { 0.85f, 0.55f, 0.30f };
    objects_.push_back(cube);
}

glm::vec3 IsometricWorld::grid_to_world(float x, float y, float elevation) const
{
    return {
        (x + 0.5f) * tile_size_,
        elevation,
        (y + 0.5f) * tile_size_,
    };
}

} // namespace draxul
