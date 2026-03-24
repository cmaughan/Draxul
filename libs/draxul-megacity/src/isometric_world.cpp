#include "isometric_world.h"

namespace draxul
{

IsometricWorld::IsometricWorld()
{
    GridObject cube;
    cube.x = kWidth / 2;
    cube.y = kHeight / 2;
    cube.elevation = 0;
    cube.color = { 0.85f, 0.55f, 0.30f };
    objects_.push_back(cube);
    tiles_[cube.x][cube.y].occupied = true;
}

bool IsometricWorld::is_valid(int x, int y) const
{
    return x >= 0 && x < kWidth && y >= 0 && y < kHeight;
}

glm::vec3 IsometricWorld::grid_to_world(int x, int y, float elevation) const
{
    return {
        (static_cast<float>(x) + 0.5f) * tile_size_,
        elevation,
        (static_cast<float>(y) + 0.5f) * tile_size_,
    };
}

} // namespace draxul
