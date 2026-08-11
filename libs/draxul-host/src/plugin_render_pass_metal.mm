#import "plugin_render_pass.h"

#import <draxul/metal/metal_render_context.h>
#include <draxul/plugin_host.h>
#include <draxul/plugin_manager.h>

namespace draxul
{
namespace
{

class MetalPluginRenderPass final : public IRenderPass
{
public:
    MetalPluginRenderPass(std::shared_ptr<LoadedPlugin> plugin, void* instance,
        PluginHost& host, std::chrono::steady_clock::time_point started_at)
        : plugin_(std::move(plugin)), instance_(instance), host_(host), started_at_(started_at) {}

    void record_prepass(IRenderContext& context) override
    {
        auto& metal = static_cast<MetalRenderContext&>(context);
        DraxulPluginMetalFrameV1 frame{};
        frame.struct_size = sizeof(frame);
        frame.device = (__bridge void*)metal.device();
        frame.command_buffer = (__bridge void*)metal.command_buffer();
        frame.drawable_texture = (__bridge void*)metal.drawable_texture();
        frame.continuation_render_pass_descriptor
            = (__bridge void*)metal.continuation_render_pass_descriptor();
        frame.target_generation = metal.target_generation();
        frame.frame_index = metal.frame_index();
        frame.buffered_frame_count = metal.buffered_frame_count();
        frame.framebuffer_width = metal.width();
        frame.framebuffer_height = metal.height();
        frame.viewport = { sizeof(DraxulPluginViewportV1), metal.viewport_x(), metal.viewport_y(), metal.viewport_w(), metal.viewport_h(), 1.0f, 96.0f };
        frame.monotonic_seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - started_at_).count();
        host_.accept_render_result(plugin_->api().render_metal(instance_, &frame));
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
    if (!(plugin->api().supported_backends & DRAXUL_PLUGIN_BACKEND_METAL)
        || !plugin->api().render_metal)
        return {};
    return std::make_unique<MetalPluginRenderPass>(
        std::move(plugin), instance, host, started_at);
}

} // namespace draxul
