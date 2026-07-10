#include "nanovg_vk.h"
#include <draxul/nanovg_pass.h>

// Include the Vulkan render context for typed access to command buffer / swapchain.
#include <draxul/vulkan/vk_render_context.h>

#include "nanovg.h"

namespace draxul
{

class VulkanNanoVGPass final : public INanoVGPass
{
public:
    ~VulkanNanoVGPass() override
    {
        if (vg_)
            nvgDeleteVk(vg_);
    }

    void set_draw_callback(NanoVGDrawFn fn) override
    {
        draw_fn_ = std::move(fn);
    }

    void record_prepass(IRenderContext& ctx) override
    {
        if (!draw_fn_)
            return;

        auto& vk_ctx = static_cast<VkRenderContext&>(ctx);

        // Lazy init: create NanoVG context on first use
        if (!vg_)
        {
            VkDevice device = vk_ctx.device();
            if (device == VK_NULL_HANDLE)
                return;
            vg_ = nvgCreateVk(vk_ctx.physical_device(), device, vk_ctx.allocator(),
                vk_ctx.swapchain_format(), NVG_ANTIALIAS);
            if (!vg_)
                return;
        }

        VkImageView imageView = vk_ctx.swapchain_image_view();
        if (imageView == VK_NULL_HANDLE)
            return;

        // Set per-frame Vulkan state
        nvgVkSetFrameState(vg_, vk_ctx.command_buffer(),
            vk_ctx.swapchain_image(), imageView, vk_ctx.frame_index());

        // NanoVG's coordinate space spans the full surface so offset pane
        // viewports land where they belong (the backend's viewport/projection
        // is anchored at the surface origin). Content is scissored to the
        // pane; the callback keeps drawing in pane-local coordinates.
        const int w = vk_ctx.viewport_w();
        const int h = vk_ctx.viewport_h();
        const int vx = vk_ctx.viewport_x();
        const int vy = vk_ctx.viewport_y();
        const float pixel_ratio = 1.0f;

        nvgBeginFrame(vg_, static_cast<float>(vk_ctx.width()),
            static_cast<float>(vk_ctx.height()), pixel_ratio);
        nvgScissor(vg_, static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(w),
            static_cast<float>(h));
        if (vx != 0 || vy != 0)
            nvgTranslate(vg_, static_cast<float>(vx), static_cast<float>(vy));

        draw_fn_(vg_, w, h);
        nvgEndFrame(vg_);

        // Clear callback to avoid holding captures across frames
        draw_fn_ = nullptr;
    }

    void record(IRenderContext& /*ctx*/) override
    {
        // All work done in record_prepass — NanoVG manages its own render pass.
    }

private:
    NVGcontext* vg_ = nullptr;
    NanoVGDrawFn draw_fn_;
};

std::unique_ptr<INanoVGPass> create_nanovg_pass()
{
    return std::make_unique<VulkanNanoVGPass>();
}

} // namespace draxul
