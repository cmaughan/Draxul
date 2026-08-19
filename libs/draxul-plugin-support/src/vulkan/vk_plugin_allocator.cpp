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
    VkPhysicalDeviceBufferDeviceAddressFeatures address_features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES
    };
    VkPhysicalDeviceFeatures2 features{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2
    };
    features.pNext = &address_features;
    vkGetPhysicalDeviceFeatures2(info.physicalDevice, &features);
    if (address_features.bufferDeviceAddress)
        info.flags |= VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
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
