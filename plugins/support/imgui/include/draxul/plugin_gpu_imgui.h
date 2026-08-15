#pragma once

#include <draxul/imgui_host.h>
#include <draxul/plugin_api.h>

#include <memory>
#include <optional>
#include <string>

namespace draxul::plugin_support
{

class GpuImGuiHost : public IImGuiHost
{
public:
    virtual void set_vulkan_frame(const DraxulPluginVulkanFrameV2* frame) = 0;
    virtual void set_metal_frame(const DraxulPluginMetalFrameV2* frame) = 0;
};

std::unique_ptr<GpuImGuiHost> create_gpu_imgui_host();

struct RecommendedUiFont
{
    std::string path;
    float size_pixels = 13.0f;
    float display_scale = 1.0f;
    uint64_t generation = 0;
};

// Small client for the public C service. It deliberately contains no Draxul
// renderer or application types, so product plugins can take it with them.
class UiStyleClient
{
public:
    bool discover(const DraxulPluginHostApiV2& host);
    std::optional<RecommendedUiFont> poll();

private:
    DraxulPluginUiStyleServiceV2 service_{};
    uint64_t applied_generation_ = 0;
};

} // namespace draxul::plugin_support
