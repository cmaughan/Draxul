#pragma once

#include "isometric_scene_types.h"
#include <draxul/base_renderer.h>
#include <memory>
#include <utility>

namespace draxul
{

class IsometricScenePass : public IRenderPass
{
public:
    IsometricScenePass(int grid_width, int grid_height, float tile_size);
    ~IsometricScenePass() override;

    void set_scene(SceneSnapshot snapshot)
    {
        scene_ = std::move(snapshot);
    }

    void record(IRenderContext& ctx) override;

    int grid_width() const
    {
        return grid_width_;
    }
    int grid_height() const
    {
        return grid_height_;
    }
    float tile_size() const
    {
        return tile_size_;
    }

    struct State;

private:
    int grid_width_ = 0;
    int grid_height_ = 0;
    float tile_size_ = 1.0f;
    SceneSnapshot scene_;
    std::unique_ptr<State> state_;
};

} // namespace draxul
