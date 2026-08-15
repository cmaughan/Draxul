#include "nanovg_vk.h"

#include <draxul/nanovg_pass.h>

#include "nanovg.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace draxul
{

class VulkanNanoVGPass final : public INanoVGPass
{
public:
    ~VulkanNanoVGPass() override
    {
        reset();
    }

    void set_draw_callback(NanoVGDrawFn fn) override
    {
        draw_fn_ = std::move(fn);
    }

    bool render_vulkan(const DraxulPluginVulkanFrameV2& frame) override
    {
        if (!draw_fn_)
            return true;
        const auto device = static_cast<VkDevice>(frame.device);
        const auto format = static_cast<VkFormat>(frame.target_format);
        if (vg_ && (device_ != device || color_format_ != format))
            reset();
        if (!vg_ && !initialize(frame))
            return false;

        nvgVkSetFrameState(vg_,
            static_cast<VkCommandBuffer>(frame.command_buffer),
            reinterpret_cast<VkImage>(static_cast<uintptr_t>(frame.target_image)),
            reinterpret_cast<VkImageView>(static_cast<uintptr_t>(frame.target_image_view)),
            frame.frame_index, frame.buffered_frame_count);

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

    bool render_metal(const DraxulPluginMetalFrameV2&) override
    {
        return false;
    }

private:
    bool initialize(const DraxulPluginVulkanFrameV2& frame)
    {
        VmaVulkanFunctions functions{};
        functions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
        functions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;
        VmaAllocatorCreateInfo allocator_info{};
        allocator_info.instance = static_cast<VkInstance>(frame.instance);
        allocator_info.physicalDevice
            = static_cast<VkPhysicalDevice>(frame.physical_device);
        allocator_info.device = static_cast<VkDevice>(frame.device);
        allocator_info.pVulkanFunctions = &functions;
        allocator_info.vulkanApiVersion = VK_API_VERSION_1_2;
        if (vmaCreateAllocator(&allocator_info, &allocator_) != VK_SUCCESS)
            return false;
        device_ = allocator_info.device;
        color_format_ = static_cast<VkFormat>(frame.target_format);
        vg_ = nvgCreateVk(allocator_info.physicalDevice, device_, allocator_,
            color_format_, NVG_ANTIALIAS);
        if (!vg_)
        {
            reset();
            return false;
        }
        return true;
    }

    void reset()
    {
        if (vg_)
            nvgDeleteVk(vg_);
        vg_ = nullptr;
        if (allocator_)
            vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
        color_format_ = VK_FORMAT_UNDEFINED;
    }

    NVGcontext* vg_ = nullptr;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkFormat color_format_ = VK_FORMAT_UNDEFINED;
    NanoVGDrawFn draw_fn_;
};

std::unique_ptr<INanoVGPass> create_nanovg_pass()
{
    return std::make_unique<VulkanNanoVGPass>();
}

} // namespace draxul
