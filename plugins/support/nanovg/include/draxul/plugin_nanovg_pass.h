#pragma once

#include <draxul/plugin_api.h>

#include <filesystem>
#include <functional>
#include <memory>

struct NVGcontext;

namespace draxul::plugin_support
{

using PluginNanoVGDrawFn
    = std::function<void(NVGcontext* context, int width, int height)>;

// Backend-neutral NanoVG frame adapter for same-build native plugins. Native
// handles remain borrowed and platform-private; callers provide only the SDK
// frame received from the host and their product-owned drawing callback.
class IPluginNanoVGPass
{
public:
    virtual ~IPluginNanoVGPass() = default;

    virtual void set_draw_callback(PluginNanoVGDrawFn callback) = 0;
    virtual bool render_vulkan(const DraxulPluginVulkanFrameV2& frame) = 0;
    virtual bool render_metal(const DraxulPluginMetalFrameV2& frame) = 0;
};

struct PluginNanoVGPassOptions
{
    // Vulkan shaders are staged below <asset_root>/shaders. Metal compiles its
    // embedded shader source and ignores this path.
    std::filesystem::path asset_root;
};

std::unique_ptr<IPluginNanoVGPass> create_plugin_nanovg_pass(
    PluginNanoVGPassOptions options = {});

} // namespace draxul::plugin_support
