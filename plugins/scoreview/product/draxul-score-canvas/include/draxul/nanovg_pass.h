#pragma once

#include <draxul/plugin_api.h>

#include <filesystem>
#include <functional>
#include <memory>

struct NVGcontext;

namespace draxul
{

using NanoVGDrawFn = std::function<void(NVGcontext*, int, int)>;

class INanoVGPass
{
public:
    virtual ~INanoVGPass() = default;
    virtual void set_draw_callback(NanoVGDrawFn fn) = 0;
    virtual bool render_vulkan(const DraxulPluginVulkanFrameV2& frame) = 0;
    virtual bool render_metal(const DraxulPluginMetalFrameV2& frame) = 0;
};

void set_nanovg_asset_root(const std::filesystem::path& root);
std::unique_ptr<INanoVGPass> create_nanovg_pass();

} // namespace draxul
