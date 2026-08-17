#include <draxul/vulkan/vk_resource_helpers.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <draxul/log.h>
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

bool create_attachment(VkDevice device, VmaAllocator allocator, const AttachmentRequest& request,
    AttachmentResource& output, std::string& error)
{
    VkImageCreateInfo image_info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_info.flags = request.flags;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = request.format;
    image_info.extent = {
        static_cast<uint32_t>(request.width),
        static_cast<uint32_t>(request.height),
        1u
    };
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = request.samples;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = request.usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    ScopedImage created_image;
    if (!create_image(device, allocator,
            ImageRequest(image_info, VK_IMAGE_VIEW_TYPE_2D, request.aspect,
                MemoryPolicy::DevicePreferred, request.final_layout, request.debug_name,
                request.lifetime),
            created_image, error))
        return false;

    const ImageResource created = created_image.release();
    output.image = created.image;
    output.allocation = created.allocation;
    output.view = created.view;
    error.clear();
    return true;
}

bool create_attachment_view(VkDevice device, VkImage image, VkFormat format,
    VkImageAspectFlags aspect, std::string_view debug_name, VkImageView& output,
    std::string& error)
{
    VkImageSubresourceRange range{};
    range.aspectMask = aspect;
    range.levelCount = 1;
    range.layerCount = 1;
    return create_image_view(device, image, format, VK_IMAGE_VIEW_TYPE_2D, range,
        debug_name, output, error);
}

void destroy_attachment(VkDevice device, VmaAllocator allocator, AttachmentResource& resource)
{
    if (device != VK_NULL_HANDLE && resource.view != VK_NULL_HANDLE)
        vkDestroyImageView(device, resource.view, nullptr);
    if (allocator != VK_NULL_HANDLE && resource.image != VK_NULL_HANDLE)
        vmaDestroyImage(allocator, resource.image, resource.allocation);
    resource = {};
}

bool format_supports_samples(VkPhysicalDevice physical_device, VkFormat format,
    VkImageUsageFlags usage, VkSampleCountFlagBits samples)
{
    VkImageFormatProperties properties{};
    if (vkGetPhysicalDeviceImageFormatProperties(physical_device, format,
            VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0, &properties)
        != VK_SUCCESS)
        return false;
    return (properties.sampleCounts & samples) != 0;
}

VkSampleCountFlagBits choose_scene_sample_count(VkPhysicalDevice physical_device,
    VkFormat color_format, VkFormat depth_format)
{
    constexpr std::array candidates = {
        VK_SAMPLE_COUNT_4_BIT,
        VK_SAMPLE_COUNT_2_BIT,
        VK_SAMPLE_COUNT_1_BIT,
    };
    for (const VkSampleCountFlagBits samples : candidates)
    {
        if (format_supports_samples(physical_device, color_format,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, samples)
            && format_supports_samples(physical_device, depth_format,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, samples))
            return samples;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

VkShaderModule load_shader(VkDevice device, const std::filesystem::path& path,
    std::string_view log_prefix, bool required)
{
    const std::string debug_name = std::string(log_prefix) + ".shader."
        + path.filename().string();
    std::string error;
    const VkShaderModule module = load_shader_module(device, path, debug_name, error);
    if (module == VK_NULL_HANDLE)
    {
        if (required)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "%.*s: %s",
                static_cast<int>(log_prefix.size()), log_prefix.data(), error.c_str());
        }
        else
        {
            DRAXUL_LOG_WARN(LogCategory::Renderer, "%.*s: optional shader unavailable: %s",
                static_cast<int>(log_prefix.size()), log_prefix.data(), error.c_str());
        }
    }
    return module;
}

bool create_sampled_image(VkPhysicalDevice physical_device, VkDevice device, VmaAllocator allocator,
    const SampledImageRequest& request, SampledImageResource& output, std::string& error)
{
    if (request.width <= 0 || request.height <= 0)
    {
        error = "sampled image requires a positive extent";
        return false;
    }

    const uint32_t mip_levels = request.generate_mips
        ? static_cast<uint32_t>(std::floor(std::log2(
              static_cast<double>(std::max(request.width, request.height)))))
            + 1u
        : 1u;

    VkImageCreateInfo image_info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = request.format;
    image_info.extent = {
        static_cast<uint32_t>(request.width),
        static_cast<uint32_t>(request.height),
        1u
    };
    image_info.mipLevels = mip_levels;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (mip_levels > 1)
        image_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    ScopedImage created_image;
    if (!create_image(device, allocator,
            ImageRequest(image_info, VK_IMAGE_VIEW_TYPE_2D, VK_IMAGE_ASPECT_COLOR_BIT,
                MemoryPolicy::DevicePreferred, request.final_layout, request.debug_name,
                request.lifetime),
            created_image, error))
        return false;

    VkSamplerCreateInfo sampler_info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = mip_levels > 1
        ? VK_SAMPLER_MIPMAP_MODE_LINEAR
        : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler_info.addressModeU = request.address_mode;
    sampler_info.addressModeV = request.address_mode;
    // Both products clamp W: these are 2D images and the third axis is never
    // sampled, but REPEAT there trips validation on some drivers.
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.maxLod = static_cast<float>(mip_levels - 1);

    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(physical_device, &features);
    if (features.samplerAnisotropy)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_device, &properties);
        sampler_info.anisotropyEnable = VK_TRUE;
        sampler_info.maxAnisotropy = std::min(8.0f, properties.limits.maxSamplerAnisotropy);
    }

    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device, &sampler_info, nullptr, &sampler) != VK_SUCCESS)
    {
        error = "Vulkan sampler creation failed for " + std::string(request.debug_name);
        return false;
    }

    const ImageResource created = created_image.release();
    output.image = created.image;
    output.allocation = created.allocation;
    output.view = created.view;
    output.sampler = sampler;
    output.width = request.width;
    output.height = request.height;
    output.mip_levels = mip_levels;
    error.clear();
    return true;
}

void destroy_sampled_image(VkDevice device, VmaAllocator allocator, SampledImageResource& resource)
{
    if (device != VK_NULL_HANDLE && resource.sampler != VK_NULL_HANDLE)
        vkDestroySampler(device, resource.sampler, nullptr);
    if (device != VK_NULL_HANDLE && resource.view != VK_NULL_HANDLE)
        vkDestroyImageView(device, resource.view, nullptr);
    if (allocator != VK_NULL_HANDLE && resource.image != VK_NULL_HANDLE)
        vmaDestroyImage(allocator, resource.image, resource.allocation);
    resource = {};
}

namespace
{

void transition_color_levels(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout,
    VkImageLayout new_layout, VkAccessFlags src_access, VkAccessFlags dst_access,
    VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
    uint32_t base_mip_level, uint32_t level_count)
{
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = base_mip_level;
    range.levelCount = level_count;
    range.layerCount = 1;
    transition_image(cmd, ImageTransition{ image, old_layout, new_layout, src_access, dst_access, src_stage, dst_stage, range });
}

// Blits the mip chain from level 0, leaving every level SHADER_READ_ONLY_OPTIMAL.
// Level 0 must be TRANSFER_DST_OPTIMAL and levels 1..n UNDEFINED on entry.
void generate_mipmaps(VkCommandBuffer cmd, const SampledImageResource& target)
{
    int32_t mip_width = target.width;
    int32_t mip_height = target.height;

    for (uint32_t level = 1; level < target.mip_levels; ++level)
    {
        transition_color_levels(cmd, target.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            level - 1, 1);

        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = level - 1;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[1] = { mip_width, mip_height, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = level;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[1] = {
            std::max(mip_width / 2, 1),
            std::max(mip_height / 2, 1),
            1
        };
        vkCmdBlitImage(cmd,
            target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            target.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit, VK_FILTER_LINEAR);

        transition_color_levels(cmd, target.image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            level - 1, 1);

        mip_width = std::max(mip_width / 2, 1);
        mip_height = std::max(mip_height / 2, 1);
    }

    transition_color_levels(cmd, target.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        target.mip_levels - 1, 1);
}

} // namespace

bool upload_image_immediate(const ImmediateUploadContext& ctx, const void* pixels,
    size_t byte_size, SampledImageResource& target, VkImageLayout old_layout,
    std::string& error)
{
    if (pixels == nullptr || byte_size == 0 || target.image == VK_NULL_HANDLE
        || ctx.device == VK_NULL_HANDLE || ctx.allocator == VK_NULL_HANDLE
        || ctx.queue == VK_NULL_HANDLE)
    {
        error = "immediate image upload requires pixels, a target image, and a graphics queue";
        return false;
    }

    ScopedBuffer staging;
    if (!create_buffer(ctx.device, ctx.allocator,
            BufferRequest(byte_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                MemoryPolicy::HostSequentialWrite, "vkresources.upload-staging",
                LifetimeScope::Upload),
            staging, error))
        return false;
    std::memcpy(staging.get().mapped, pixels, byte_size);
    vmaFlushAllocation(ctx.allocator, staging.get().allocation, 0, byte_size);

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pool_info{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = ctx.queue_family;
    if (vkCreateCommandPool(ctx.device, &pool_info, nullptr, &command_pool) != VK_SUCCESS)
    {
        error = "immediate upload command-pool creation failed";
        return false;
    }

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo alloc_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    VkResult result = vkAllocateCommandBuffers(ctx.device, &alloc_info, &cmd);
    if (result == VK_SUCCESS)
    {
        VkCommandBufferBeginInfo begin_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result = vkBeginCommandBuffer(cmd, &begin_info);
    }
    if (result == VK_SUCCESS)
    {
        const VkAccessFlags src_access = old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            ? VK_ACCESS_SHADER_READ_BIT
            : 0;
        const VkPipelineStageFlags src_stage
            = old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        transition_color_levels(cmd, target.image, old_layout,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, src_access, VK_ACCESS_TRANSFER_WRITE_BIT,
            src_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, target.mip_levels);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = 0;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {
            static_cast<uint32_t>(target.width),
            static_cast<uint32_t>(target.height),
            1u
        };
        vkCmdCopyBufferToImage(cmd, staging.get().buffer, target.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        if (target.mip_levels > 1)
        {
            generate_mipmaps(cmd, target);
        }
        else
        {
            transition_color_levels(cmd, target.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1);
        }
        result = vkEndCommandBuffer(cmd);
    }
    if (result == VK_SUCCESS)
    {
        VkSubmitInfo submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &cmd;
        result = vkQueueSubmit(ctx.queue, 1, &submit_info, VK_NULL_HANDLE);
    }
    if (result == VK_SUCCESS)
        result = vkQueueWaitIdle(ctx.queue);

    vkDestroyCommandPool(ctx.device, command_pool, nullptr);
    if (result != VK_SUCCESS)
    {
        error = "immediate image upload failed (" + std::to_string(result) + ")";
        return false;
    }
    error.clear();
    return true;
}

} // namespace draxul::vkresources
