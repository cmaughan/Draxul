#include "vk_resource_helpers.h"

#include <fstream>
#include <vector>

namespace draxul::vkresources
{
namespace
{

void set_debug_name(VkDevice device, VkObjectType type, uint64_t handle, std::string_view name)
{
    if (device == VK_NULL_HANDLE || handle == 0 || name.empty())
        return;
    const auto set_name = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
        vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT"));
    if (!set_name)
        return;

    const std::string owned_name(name);
    VkDebugUtilsObjectNameInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT };
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = owned_name.c_str();
    set_name(device, &info);
}

VmaAllocationCreateInfo allocation_info(MemoryPolicy policy)
{
    VmaAllocationCreateInfo info{};
    switch (policy)
    {
    case MemoryPolicy::DevicePreferred:
        info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case MemoryPolicy::HostSequentialWrite:
        info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
            | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case MemoryPolicy::HostRandomAccess:
        info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
            | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    }
    return info;
}

} // namespace

ScopedBuffer::ScopedBuffer(ScopedBuffer&& other) noexcept
    : allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE))
    , resource_(std::exchange(other.resource_, {}))
{
}

ScopedBuffer& ScopedBuffer::operator=(ScopedBuffer&& other) noexcept
{
    if (this != &other)
    {
        reset();
        allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
        resource_ = std::exchange(other.resource_, {});
    }
    return *this;
}

ScopedBuffer::~ScopedBuffer()
{
    reset();
}

void ScopedBuffer::reset()
{
    destroy_buffer(allocator_, resource_);
    allocator_ = VK_NULL_HANDLE;
}

BufferResource ScopedBuffer::release()
{
    allocator_ = VK_NULL_HANDLE;
    return std::exchange(resource_, {});
}

ScopedImage::ScopedImage(ScopedImage&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE))
    , allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE))
    , resource_(std::exchange(other.resource_, {}))
{
}

ScopedImage& ScopedImage::operator=(ScopedImage&& other) noexcept
{
    if (this != &other)
    {
        reset();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
        resource_ = std::exchange(other.resource_, {});
    }
    return *this;
}

ScopedImage::~ScopedImage()
{
    reset();
}

void ScopedImage::reset()
{
    destroy_image(device_, allocator_, resource_);
    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
}

ImageResource ScopedImage::release()
{
    device_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
    return std::exchange(resource_, {});
}

bool create_buffer(VkDevice device, VmaAllocator allocator, const BufferRequest& request,
    ScopedBuffer& output, std::string& error)
{
    if (request.size == 0 || request.usage == 0 || request.debug_name.empty())
    {
        error = "buffer request requires non-zero size/usage and a debug name";
        return false;
    }

    ScopedBuffer replacement;
    replacement.allocator_ = allocator;
    VkBufferCreateInfo buffer_info{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buffer_info.size = request.size;
    buffer_info.usage = request.usage;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    const VmaAllocationCreateInfo alloc_info = allocation_info(request.memory);
    VmaAllocationInfo created_info{};
    const VkResult result = vmaCreateBuffer(allocator, &buffer_info, &alloc_info,
        &replacement.resource_.buffer, &replacement.resource_.allocation, &created_info);
    if (result != VK_SUCCESS)
    {
        error = "VMA buffer allocation failed (" + std::to_string(result) + ")";
        return false;
    }

    replacement.resource_.mapped = created_info.pMappedData;
    replacement.resource_.size = request.size;
    replacement.resource_.lifetime = request.lifetime;
    if (request.memory != MemoryPolicy::DevicePreferred
        && replacement.resource_.mapped == nullptr)
    {
        error = "host-visible VMA buffer allocation was not mapped";
        return false;
    }
    set_debug_name(device, VK_OBJECT_TYPE_BUFFER,
        reinterpret_cast<uint64_t>(replacement.resource_.buffer), request.debug_name);
    output = std::move(replacement);
    error.clear();
    return true;
}

bool create_image_view(VkDevice device, VkImage image, VkFormat format,
    VkImageViewType view_type, const VkImageSubresourceRange& range,
    std::string_view debug_name, VkImageView& output, std::string& error)
{
    if (image == VK_NULL_HANDLE || debug_name.empty())
    {
        error = "image view request requires an image and debug name";
        return false;
    }
    VkImageViewCreateInfo view_info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view_info.image = image;
    view_info.viewType = view_type;
    view_info.format = format;
    view_info.subresourceRange = range;
    const VkResult result = vkCreateImageView(device, &view_info, nullptr, &output);
    if (result != VK_SUCCESS)
    {
        error = "Vulkan image view creation failed (" + std::to_string(result) + ")";
        return false;
    }
    set_debug_name(device, VK_OBJECT_TYPE_IMAGE_VIEW,
        reinterpret_cast<uint64_t>(output), debug_name);
    error.clear();
    return true;
}

bool create_image(VkDevice device, VmaAllocator allocator, const ImageRequest& request,
    ScopedImage& output, std::string& error)
{
    if (request.image.usage == 0 || request.debug_name.empty()
        || request.final_layout == VK_IMAGE_LAYOUT_UNDEFINED)
    {
        error = "image request requires usage, a final layout, and a debug name";
        return false;
    }

    ScopedImage replacement;
    replacement.device_ = device;
    replacement.allocator_ = allocator;
    const VmaAllocationCreateInfo alloc_info = allocation_info(request.memory);
    const VkResult result = vmaCreateImage(allocator, &request.image, &alloc_info,
        &replacement.resource_.image, &replacement.resource_.allocation, nullptr);
    if (result != VK_SUCCESS)
    {
        error = "VMA image allocation failed (" + std::to_string(result) + ")";
        return false;
    }
    set_debug_name(device, VK_OBJECT_TYPE_IMAGE,
        reinterpret_cast<uint64_t>(replacement.resource_.image), request.debug_name);

    VkImageSubresourceRange range{};
    range.aspectMask = request.aspect;
    range.levelCount = request.image.mipLevels;
    range.layerCount = request.image.arrayLayers;
    const std::string view_name = std::string(request.debug_name) + ".view";
    if (!create_image_view(device, replacement.resource_.image, request.image.format,
            request.view_type, range, view_name, replacement.resource_.view, error))
        return false;

    replacement.resource_.final_layout = request.final_layout;
    replacement.resource_.lifetime = request.lifetime;
    output = std::move(replacement);
    error.clear();
    return true;
}

VkShaderModule load_shader_module(VkDevice device, const std::filesystem::path& path,
    std::string_view debug_name, std::string& error)
{
    if (debug_name.empty())
    {
        error = "shader module requires a debug name";
        return VK_NULL_HANDLE;
    }
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        error = "failed to open shader " + path.string();
        return VK_NULL_HANDLE;
    }
    const std::streamsize end = file.tellg();
    if (end <= 0 || (end % static_cast<std::streamsize>(sizeof(uint32_t))) != 0)
    {
        error = "invalid SPIR-V bytecode in " + path.string();
        return VK_NULL_HANDLE;
    }
    std::vector<uint32_t> code(static_cast<size_t>(end) / sizeof(uint32_t));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(code.data()), end))
    {
        error = "failed to read shader " + path.string();
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo create_info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    create_info.codeSize = static_cast<size_t>(end);
    create_info.pCode = code.data();
    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult result = vkCreateShaderModule(device, &create_info, nullptr, &module);
    if (result != VK_SUCCESS)
    {
        error = "Vulkan shader module creation failed for " + path.string()
            + " (" + std::to_string(result) + ")";
        return VK_NULL_HANDLE;
    }
    set_debug_name(device, VK_OBJECT_TYPE_SHADER_MODULE,
        reinterpret_cast<uint64_t>(module), debug_name);
    error.clear();
    return module;
}

void transition_image(VkCommandBuffer command_buffer, const ImageTransition& transition)
{
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = transition.old_layout;
    barrier.newLayout = transition.new_layout;
    barrier.srcAccessMask = transition.src_access;
    barrier.dstAccessMask = transition.dst_access;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = transition.image;
    barrier.subresourceRange = transition.range;
    vkCmdPipelineBarrier(command_buffer, transition.src_stage, transition.dst_stage,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void record_buffer_to_image_upload(VkCommandBuffer command_buffer, const BufferImageUpload& upload)
{
    transition_image(command_buffer, upload.before_copy);
    vkCmdCopyBufferToImage(command_buffer, upload.source, upload.destination,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &upload.copy);
    transition_image(command_buffer, upload.after_copy);
}

void destroy_buffer(VmaAllocator allocator, BufferResource& resource)
{
    if (allocator != VK_NULL_HANDLE && resource.buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(allocator, resource.buffer, resource.allocation);
    resource = {};
}

void destroy_image(VkDevice device, VmaAllocator allocator, ImageResource& resource)
{
    if (device != VK_NULL_HANDLE && resource.view != VK_NULL_HANDLE)
        vkDestroyImageView(device, resource.view, nullptr);
    if (allocator != VK_NULL_HANDLE && resource.image != VK_NULL_HANDLE)
        vmaDestroyImage(allocator, resource.image, resource.allocation);
    resource = {};
}

} // namespace draxul::vkresources
