#include <draxul/vulkan/vk_plugin_allocator.h>

#include <vulkan/vulkan.h>

namespace draxul::plugin_support
{

bool ensure_allocator(VmaAllocator& allocator,
    const DraxulPluginVulkanFrameV2& frame, const char* display_name,
    std::string& error)
{
    if (allocator != VK_NULL_HANDLE)
        return true;
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = &vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = &vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo info{};
    info.instance = static_cast<VkInstance>(frame.instance);
    info.physicalDevice = static_cast<VkPhysicalDevice>(frame.physical_device);
    info.device = static_cast<VkDevice>(frame.device);
    info.pVulkanFunctions = &functions;
    info.vulkanApiVersion = VK_API_VERSION_1_2;
    const VkResult result = vmaCreateAllocator(&info, &allocator);
    if (result != VK_SUCCESS)
    {
        error = std::string(display_name)
            + " could not create its Vulkan allocator ("
            + std::to_string(result) + ")";
        return false;
    }
    return true;
}

void destroy_allocator(VmaAllocator& allocator)
{
    if (allocator != VK_NULL_HANDLE)
    {
        vmaDestroyAllocator(allocator);
        allocator = VK_NULL_HANDLE;
    }
}

} // namespace draxul::plugin_support
