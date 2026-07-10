#import "nanovg_mtl.h"
#import <draxul/nanovg_pass.h>

// Include the Metal render context for typed access to command buffer / drawable.
#import <draxul/metal/metal_render_context.h>

#include "nanovg.h"

namespace draxul
{

class MetalNanoVGPass final : public INanoVGPass
{
public:
    ~MetalNanoVGPass() override
    {
        if (vg_)
            nvgDeleteMtl(vg_);
    }

    void set_draw_callback(NanoVGDrawFn fn) override
    {
        draw_fn_ = std::move(fn);
    }

    void record_prepass(IRenderContext& ctx) override
    {
        if (!draw_fn_)
            return;

        auto& mtl_ctx = static_cast<MetalRenderContext&>(ctx);

        // Lazy init: create NanoVG context on first use
        if (!vg_)
        {
            id<MTLDevice> device = mtl_ctx.device();
            if (!device)
                return;
            vg_ = nvgCreateMtl(device, NVG_ANTIALIAS);
            if (!vg_)
                return;
        }

        id<MTLTexture> drawable = mtl_ctx.drawable_texture();
        if (!drawable)
            return;

        // Set per-frame Metal state
        nvgMtlSetFrameState(vg_, mtl_ctx.command_buffer(), drawable, mtl_ctx.frame_index());

        // NanoVG's coordinate space spans the full surface so offset pane
        // viewports land where they belong (the backend's viewport/projection
        // is anchored at the surface origin). Content is scissored to the
        // pane; the callback keeps drawing in pane-local coordinates.
        const int w = mtl_ctx.viewport_w();
        const int h = mtl_ctx.viewport_h();
        const int vx = mtl_ctx.viewport_x();
        const int vy = mtl_ctx.viewport_y();
        const float pixel_ratio = 1.0f;

        nvgBeginFrame(vg_, static_cast<float>(mtl_ctx.width()),
            static_cast<float>(mtl_ctx.height()), pixel_ratio);
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
        // All work done in record_prepass — NanoVG manages its own encoder.
    }

private:
    NVGcontext* vg_ = nullptr;
    NanoVGDrawFn draw_fn_;
};

std::unique_ptr<INanoVGPass> create_nanovg_pass()
{
    return std::make_unique<MetalNanoVGPass>();
}

} // namespace draxul
