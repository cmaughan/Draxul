#pragma once
#include <draxul/base_renderer.h>
#include <memory>

namespace draxul
{

// CubeRenderPass — renders the spinning cube 3D scene for MegaCityHost.
// Implements IRenderPass so it can be registered with I3DRenderer::register_render_pass().
//
// Platform-specific pipeline state is managed lazily inside record() using
// native handles obtained from the IRenderContext. On Vulkan, the pipeline is
// recreated automatically when the VkRenderPass handle changes (swapchain rebuild).
//
// MegaCityHost owns this object and calls set_angle() each pump() before
// requesting a frame, so record() always sees the current rotation.
class CubeRenderPass : public IRenderPass
{
public:
    CubeRenderPass();
    ~CubeRenderPass() override;

    void set_angle(float angle)
    {
        angle_ = angle;
    }

    // IRenderPass
    void record(IRenderContext& ctx) override;

    struct State; // platform-specific; defined in codeviz_render.mm / codeviz_render_vk.cpp

private:
    float angle_ = 0.0f;
    std::unique_ptr<State> state_;
};

} // namespace draxul
