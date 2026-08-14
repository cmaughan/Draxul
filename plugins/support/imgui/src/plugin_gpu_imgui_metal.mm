#include <draxul/plugin_gpu_imgui.h>

#import <Metal/Metal.h>
#import <imgui_impl_metal.h>

#include <imgui.h>

namespace draxul::plugin_support
{

class MetalGpuImGuiHost final : public GpuImGuiHost
{
public:
    ~MetalGpuImGuiHost() override
    {
        shutdown_imgui_backend();
    }

    void set_vulkan_frame(const DraxulPluginVulkanFrameV2*) override {}

    void set_metal_frame(const DraxulPluginMetalFrameV2* frame) override
    {
        frame_ = frame;
    }

    bool initialize_imgui_backend() override
    {
        return true;
    }

    void shutdown_imgui_backend() override
    {
        if (initialized_)
            ImGui_ImplMetal_Shutdown();
        initialized_ = false;
        device_ = nil;
    }

    void rebuild_imgui_font_texture() override
    {
        rebuild_font_texture_ = initialized_;
    }

    void begin_imgui_frame() override
    {
        if (!frame_ || !ensure_initialized())
            return;
        if (rebuild_font_texture_)
        {
            ImGui_ImplMetal_DestroyFontsTexture();
            ImGui_ImplMetal_CreateFontsTexture(device_);
            rebuild_font_texture_ = false;
        }
        ImGui_ImplMetal_NewFrame(
            (__bridge MTLRenderPassDescriptor*)
                frame_->continuation_render_pass_descriptor);
    }

    bool render_imgui_draw_data(
        const ImDrawData* data, ImGuiContext* context) override
    {
        if (!frame_ || !data || !context || !initialized_)
            return false;
        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(context);
        id<MTLCommandBuffer> command_buffer
            = (__bridge id<MTLCommandBuffer>)frame_->command_buffer;
        MTLRenderPassDescriptor* descriptor
            = (__bridge MTLRenderPassDescriptor*)
                frame_->continuation_render_pass_descriptor;
        id<MTLRenderCommandEncoder> encoder
            = [command_buffer renderCommandEncoderWithDescriptor:descriptor];
        if (!encoder)
        {
            ImGui::SetCurrentContext(previous);
            return false;
        }
        ImGui_ImplMetal_RenderDrawData(
            const_cast<ImDrawData*>(data), command_buffer, encoder);
        [encoder endEncoding];
        ImGui::SetCurrentContext(previous);
        return true;
    }

private:
    bool ensure_initialized()
    {
        id<MTLDevice> device = (__bridge id<MTLDevice>)frame_->device;
        if (initialized_ && device_ == device)
            return true;
        if (initialized_)
            shutdown_imgui_backend();
        if (!ImGui_ImplMetal_Init(device))
            return false;
        device_ = device;
        initialized_ = true;
        rebuild_font_texture_ = false;
        return true;
    }

    const DraxulPluginMetalFrameV2* frame_ = nullptr;
    __unsafe_unretained id<MTLDevice> device_ = nil;
    bool initialized_ = false;
    bool rebuild_font_texture_ = false;
};

std::unique_ptr<GpuImGuiHost> create_gpu_imgui_host()
{
    return std::make_unique<MetalGpuImGuiHost>();
}

} // namespace draxul::plugin_support
