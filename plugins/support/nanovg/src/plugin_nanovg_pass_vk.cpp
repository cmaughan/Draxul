#include "plugin_nanovg_pass_internal.h"

#include <draxul/nanovg_vk.h>

#include "nanovg.h"

#include <cstdint>
#include <utility>

namespace draxul::plugin_support::detail
{
namespace
{

class VulkanPluginNanoVGBackend final : public PluginNanoVGNativeBackend
{
public:
    explicit VulkanPluginNanoVGBackend(std::filesystem::path asset_root)
        : shader_root_(std::move(asset_root) / "shaders")
    {
    }

    ~VulkanPluginNanoVGBackend() override { reset(); }

    NVGcontext* prepare_vulkan(
        const DraxulPluginVulkanFrameV2& frame) override
    {
        const auto device = static_cast<VkDevice>(frame.device);
        const auto format = static_cast<VkFormat>(frame.target_format);
        if (context_ && identity_.requires_reset(
                            reinterpret_cast<std::uintptr_t>(device),
                            frame.target_format))
            reset();
        if (!context_ && !initialize(frame))
            return nullptr;

        nvgVkSetFrameState(context_,
            static_cast<VkCommandBuffer>(frame.command_buffer),
            reinterpret_cast<VkImage>(static_cast<std::uintptr_t>(frame.target_image)),
            reinterpret_cast<VkImageView>(
                static_cast<std::uintptr_t>(frame.target_image_view)),
            frame.frame_index, frame.buffered_frame_count);
        return context_;
    }

    NVGcontext* prepare_metal(const DraxulPluginMetalFrameV2&) override
    {
        return nullptr;
    }

private:
    bool initialize(const DraxulPluginVulkanFrameV2& frame)
    {
        nvgVkSetShaderRoot(shader_root_);
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
        context_ = nvgCreateVk(allocator_info.physicalDevice, device_, allocator_,
            color_format_, NVG_ANTIALIAS);
        if (!context_)
        {
            reset();
            return false;
        }
        identity_.set(
            reinterpret_cast<std::uintptr_t>(device_), frame.target_format);
        return true;
    }

    void reset()
    {
        if (context_)
            nvgDeleteVk(context_);
        context_ = nullptr;
        if (allocator_)
            vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
        device_ = VK_NULL_HANDLE;
        color_format_ = VK_FORMAT_UNDEFINED;
        identity_.clear();
    }

    std::filesystem::path shader_root_;
    NVGcontext* context_ = nullptr;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkFormat color_format_ = VK_FORMAT_UNDEFINED;
    VulkanTargetIdentity identity_;
};

} // namespace

std::unique_ptr<PluginNanoVGNativeBackend> create_native_backend(
    const PluginNanoVGPassOptions& options)
{
    return std::make_unique<VulkanPluginNanoVGBackend>(options.asset_root);
}

} // namespace draxul::plugin_support::detail
