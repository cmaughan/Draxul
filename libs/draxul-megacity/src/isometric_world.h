#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace draxul
{

struct GridTile
{
    bool occupied = false;
};

struct GridObject
{
    int x = 0;
    int y = 0;
    int elevation = 0;
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
};

class IsometricWorld
{
public:
    static constexpr int kWidth = 5;
    static constexpr int kHeight = 5;

    IsometricWorld();

    bool is_valid(int x, int y) const;
    glm::vec3 grid_to_world(int x, int y, float elevation = 0.0f) const;

    int width() const
    {
        return kWidth;
    }
    int height() const
    {
        return kHeight;
    }
    float tile_size() const
    {
        return tile_size_;
    }

    const std::vector<GridObject>& objects() const
    {
        return objects_;
    }
    std::vector<GridObject>& objects()
    {
        return objects_;
    }

private:
    float tile_size_ = 1.0f;
    GridTile tiles_[kWidth][kHeight]{};
    std::vector<GridObject> objects_;
};

} // namespace draxul
