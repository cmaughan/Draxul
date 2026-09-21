#pragma once

#include <draxul/plugin_nanovg_pass.h>

#include <cstdint>
#include <memory>

namespace draxul::plugin_support::detail
{

class VulkanTargetIdentity
{
public:
    bool requires_reset(std::uintptr_t device, std::uint64_t format) const
    {
        return initialized_ && (device_ != device || format_ != format);
    }

    void set(std::uintptr_t device, std::uint64_t format)
    {
        initialized_ = true;
        device_ = device;
        format_ = format;
    }

    void clear()
    {
        initialized_ = false;
        device_ = 0;
        format_ = 0;
    }

private:
    bool initialized_ = false;
    std::uintptr_t device_ = 0;
    std::uint64_t format_ = 0;
};

class PluginNanoVGNativeBackend
{
public:
    virtual ~PluginNanoVGNativeBackend() = default;

    virtual NVGcontext* prepare_vulkan(
        const DraxulPluginVulkanFrameV2& frame)
        = 0;
    virtual NVGcontext* prepare_metal(const DraxulPluginMetalFrameV2& frame)
        = 0;
};

class PluginNanoVGFrameOperations
{
public:
    virtual ~PluginNanoVGFrameOperations() = default;

    virtual void begin_frame(
        NVGcontext* context, int framebuffer_width, int framebuffer_height)
        = 0;
    virtual void scissor(NVGcontext* context, int x, int y, int width, int height)
        = 0;
    virtual void translate(NVGcontext* context, int x, int y) = 0;
    virtual void end_frame(NVGcontext* context) = 0;
};

std::unique_ptr<PluginNanoVGNativeBackend> create_native_backend(
    const PluginNanoVGPassOptions& options);

std::unique_ptr<IPluginNanoVGPass> create_plugin_nanovg_pass_for_test(
    std::unique_ptr<PluginNanoVGNativeBackend> backend,
    std::unique_ptr<PluginNanoVGFrameOperations> operations);

} // namespace draxul::plugin_support::detail
