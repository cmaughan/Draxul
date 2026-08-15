#include "plugin_render_pass.h"

#include <draxul/plugin_host.h>
#include <draxul/plugin_manager.h>
#include <draxul/vulkan/vk_render_context.h>

#include <type_traits>

namespace draxul
{
namespace
{

template <typename T>
uint64_t handle_bits(T value)
{
    if constexpr (std::is_pointer_v<T>)
        return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
    else
        return static_cast<uint64_t>(value);
}

class VulkanPluginRenderPass final : public IRenderPass
{
public:
    VulkanPluginRenderPass(std::shared_ptr<LoadedPlugin> plugin, void* instance,
        PluginHost& host, std::chrono::steady_clock::time_point started_at)
        : plugin_(std::move(plugin)), instance_(instance), host_(host), started_at_(started_at) {}

    void record_prepass(IRenderContext& context) override
    {
        auto& vk = static_cast<VkRenderContext&>(context);
        DraxulPluginVulkanFrameV2 frame{};
        frame.struct_size = sizeof(frame);
        frame.instance = vk.instance();
        frame.physical_device = vk.physical_device();
        frame.device = vk.device();
        frame.graphics_queue = vk.graphics_queue();
        frame.graphics_queue_family = vk.graphics_queue_family();
        frame.command_buffer = vk.command_buffer();
        frame.target_image = handle_bits(vk.swapchain_image());
        frame.target_image_view = handle_bits(vk.swapchain_image_view());
        frame.target_format = static_cast<uint64_t>(vk.swapchain_format());
        frame.depth_format = static_cast<uint64_t>(vk.depth_format());
        frame.continuation_render_pass = handle_bits(vk.continuation_render_pass());
        frame.continuation_framebuffer = handle_bits(vk.continuation_framebuffer());
        frame.frame_index = vk.frame_index();
        frame.buffered_frame_count = vk.buffered_frame_count();
        frame.target_generation = vk.target_generation();
        frame.framebuffer_width = vk.width();
        frame.framebuffer_height = vk.height();
        frame.viewport = { sizeof(DraxulPluginViewportV2), vk.viewport_x(), vk.viewport_y(), vk.viewport_w(), vk.viewport_h(), 1.0f, 96.0f };
        frame.monotonic_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started_at_).count();
        host_.accept_render_result(plugin_->api().render_vulkan(instance_, &frame));
    }

    void record(IRenderContext&) override {}

private:
    std::shared_ptr<LoadedPlugin> plugin_;
    void* instance_ = nullptr;
    PluginHost& host_;
    std::chrono::steady_clock::time_point started_at_;
};

} // namespace

std::unique_ptr<IRenderPass> create_plugin_render_pass(
    std::shared_ptr<LoadedPlugin> plugin, void* instance,
    PluginHost& host, std::chrono::steady_clock::time_point started_at)
{
    if (!(plugin->api().supported_backends & DRAXUL_PLUGIN_BACKEND_VULKAN)
        || !plugin->api().render_vulkan)
        return {};
    return std::make_unique<VulkanPluginRenderPass>(
        std::move(plugin), instance, host, started_at);
}

} // namespace draxul
