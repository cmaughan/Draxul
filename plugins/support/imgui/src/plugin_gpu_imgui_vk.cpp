#include <draxul/plugin_gpu_imgui.h>

#include "imgui_impl_vulkan.h"

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>

namespace draxul::plugin_support
{

class VulkanGpuImGuiHost final : public GpuImGuiHost
{
public:
    ~VulkanGpuImGuiHost() override
    {
        shutdown_imgui_backend();
    }

    void set_vulkan_frame(const DraxulPluginVulkanFrameV2* frame) override
    {
        frame_ = frame;
    }

    void set_metal_frame(const DraxulPluginMetalFrameV2*) override {}

    bool initialize_imgui_backend() override
    {
        return true;
    }

    void shutdown_imgui_backend() override
    {
        if (!initialized_)
            return;
        ImGui_ImplVulkan_Shutdown();
        initialized_ = false;
        if (descriptor_pool_)
            vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
        render_pass_ = VK_NULL_HANDLE;
    }

    void rebuild_imgui_font_texture() override
    {
        if (initialized_)
            ImGui_ImplVulkan_RetireFontsTexture();
        // NewFrame uploads the replacement atlas into Draxul's borrowed
        // command buffer. No queue submission or wait is performed here.
    }

    void begin_imgui_frame() override
    {
        if (!frame_ || !ensure_initialized())
            return;
        ImGui_ImplVulkan_SetExternalCommandBuffer(
            static_cast<VkCommandBuffer>(frame_->command_buffer));
        ImGui_ImplVulkan_NewFrame();
    }

    bool render_imgui_draw_data(
        const ImDrawData* data, ImGuiContext* context) override
    {
        if (!frame_ || !data || !context || !initialized_)
            return false;
        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(context);
        const VkCommandBuffer command_buffer
            = static_cast<VkCommandBuffer>(frame_->command_buffer);
        VkRenderPassBeginInfo begin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        begin.renderPass = render_pass_;
        begin.framebuffer = reinterpret_cast<VkFramebuffer>(
            static_cast<uintptr_t>(frame_->continuation_framebuffer));
        begin.renderArea.extent = {
            static_cast<uint32_t>(frame_->framebuffer_width),
            static_cast<uint32_t>(frame_->framebuffer_height) };
        vkCmdBeginRenderPass(command_buffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
        ImGui_ImplVulkan_RenderDrawData(
            const_cast<ImDrawData*>(data), command_buffer);
        vkCmdEndRenderPass(command_buffer);
        ImGui::SetCurrentContext(previous);
        return true;
    }

private:
    bool ensure_initialized()
    {
        const auto device = static_cast<VkDevice>(frame_->device);
        const auto render_pass = reinterpret_cast<VkRenderPass>(
            static_cast<uintptr_t>(frame_->continuation_render_pass));
        if (initialized_ && device_ == device && render_pass_ == render_pass)
            return true;
        if (initialized_)
            shutdown_imgui_backend();

        VkDescriptorPoolSize pool_size{
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 };
        VkDescriptorPoolCreateInfo pool_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 64;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        if (vkCreateDescriptorPool(device, &pool_info, nullptr,
                &descriptor_pool_) != VK_SUCCESS)
            return false;

        ImGui_ImplVulkan_InitInfo info{};
        info.Instance = static_cast<VkInstance>(frame_->instance);
        info.PhysicalDevice
            = static_cast<VkPhysicalDevice>(frame_->physical_device);
        info.Device = device;
        info.QueueFamily = frame_->graphics_queue_family;
        info.Queue = static_cast<VkQueue>(frame_->graphics_queue);
        info.DescriptorPool = descriptor_pool_;
        info.RenderPass = render_pass;
        info.MinImageCount = std::max(2u, frame_->buffered_frame_count);
        info.ImageCount = std::max(2u, frame_->buffered_frame_count);
        info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        if (!ImGui_ImplVulkan_Init(&info))
        {
            vkDestroyDescriptorPool(device, descriptor_pool_, nullptr);
            descriptor_pool_ = VK_NULL_HANDLE;
            return false;
        }
        device_ = device;
        render_pass_ = render_pass;
        initialized_ = true;
        return true;
    }

    const DraxulPluginVulkanFrameV2* frame_ = nullptr;
    VkDevice device_ = VK_NULL_HANDLE;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    bool initialized_ = false;
};

std::unique_ptr<GpuImGuiHost> create_gpu_imgui_host()
{
    return std::make_unique<VulkanGpuImGuiHost>();
}

} // namespace draxul::plugin_support
