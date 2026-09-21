#import "plugin_nanovg_pass_internal.h"

#import <draxul/nanovg_mtl.h>

#include "nanovg.h"

namespace draxul::plugin_support::detail
{
namespace
{

class MetalPluginNanoVGBackend final : public PluginNanoVGNativeBackend
{
public:
    ~MetalPluginNanoVGBackend() override
    {
        if (context_)
            nvgDeleteMtl(context_);
    }

    NVGcontext* prepare_vulkan(const DraxulPluginVulkanFrameV2&) override
    {
        return nullptr;
    }

    NVGcontext* prepare_metal(const DraxulPluginMetalFrameV2& frame) override
    {
        // Match the existing product adapters: Metal initializes once from the
        // first borrowed device, while Vulkan explicitly rebuilds for device or
        // target-format changes.
        if (!context_)
            context_ = nvgCreateMtl(
                (__bridge id<MTLDevice>)frame.device, NVG_ANTIALIAS);
        if (!context_)
            return nullptr;

        nvgMtlSetFrameState(context_,
            (__bridge id<MTLCommandBuffer>)frame.command_buffer,
            (__bridge id<MTLTexture>)frame.drawable_texture,
            frame.frame_index);
        return context_;
    }

private:
    NVGcontext* context_ = nullptr;
};

} // namespace

std::unique_ptr<PluginNanoVGNativeBackend> create_native_backend(
    const PluginNanoVGPassOptions&)
{
    return std::make_unique<MetalPluginNanoVGBackend>();
}

} // namespace draxul::plugin_support::detail
