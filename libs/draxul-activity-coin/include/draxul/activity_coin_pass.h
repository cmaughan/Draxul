#pragma once

#include <draxul/base_renderer.h>
#include <memory>
#include <vector>

namespace draxul
{

// Provider styling for one coin. Values are shader constants; keep them in
// sync with activity_coin.frag and the embedded Metal source.
enum class ActivityCoinStyle : int
{
    Codex = 0,
    Claude = 1,
    Grok = 2,
    Neutral = 3,
};

// One coin, in pixels relative to the RenderViewport the pass is recorded
// with. The coin spins about its vertical axis; angle 0 is face-on.
struct ActivityCoinInstance
{
    float center_x = 0.0f;
    float center_y = 0.0f;
    float radius = 0.0f;
    float angle = 0.0f;
    float brightness = 1.0f;
    ActivityCoinStyle style = ActivityCoinStyle::Neutral;
};

// IActivityCoinPass draws the TokenFu activity coin inside the main render
// pass. The mesh is generated procedurally in the vertex shader and facets
// facing away from the viewer are culled per-triangle, so the pass needs no
// vertex buffers and no depth attachment.
//
// Usage:
//   coin_pass_->set_coins(std::move(instances));
//   frame.record_render_pass(*coin_pass_, viewport);
class IActivityCoinPass : public IRenderPass
{
public:
    ~IActivityCoinPass() override = default;

    // Replace the coins drawn by the next record(). The list is consumed by
    // that record so stale coins never persist into a later frame.
    virtual void set_coins(std::vector<ActivityCoinInstance> coins) = 0;
};

// Factory — returns the platform-appropriate pass. GPU resources are created
// lazily on the first record() with coins.
std::unique_ptr<IActivityCoinPass> create_activity_coin_pass();

} // namespace draxul
