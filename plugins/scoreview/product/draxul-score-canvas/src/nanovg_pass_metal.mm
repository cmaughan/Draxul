#import "nanovg_mtl.h"

#import <draxul/nanovg_pass.h>

#include "nanovg.h"

#include <algorithm>
#include <utility>

namespace draxul
{

void set_nanovg_asset_root(const std::filesystem::path&) {}

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

    bool render_vulkan(const DraxulPluginVulkanFrameV2&) override
    {
        return false;
    }

    bool render_metal(const DraxulPluginMetalFrameV2& frame) override
    {
        if (!draw_fn_)
            return true;
        id<MTLDevice> device = (__bridge id<MTLDevice>)frame.device;
        if (!vg_)
            vg_ = nvgCreateMtl(device, NVG_ANTIALIAS);
        if (!vg_)
            return false;

        nvgMtlSetFrameState(vg_,
            (__bridge id<MTLCommandBuffer>)frame.command_buffer,
            (__bridge id<MTLTexture>)frame.drawable_texture,
            frame.frame_index);
        const int width = std::max(0, frame.viewport.width);
        const int height = std::max(0, frame.viewport.height);
        nvgBeginFrame(vg_, static_cast<float>(frame.framebuffer_width),
            static_cast<float>(frame.framebuffer_height), 1.0f);
        nvgScissor(vg_, static_cast<float>(frame.viewport.x),
            static_cast<float>(frame.viewport.y), static_cast<float>(width),
            static_cast<float>(height));
        if (frame.viewport.x != 0 || frame.viewport.y != 0)
            nvgTranslate(vg_, static_cast<float>(frame.viewport.x),
                static_cast<float>(frame.viewport.y));
        draw_fn_(vg_, width, height);
        nvgEndFrame(vg_);
        draw_fn_ = nullptr;
        return true;
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
