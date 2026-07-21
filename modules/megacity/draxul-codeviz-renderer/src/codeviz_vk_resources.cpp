#include "codeviz_vk_resources.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <filesystem>
#include <string>
#include <vector>

namespace draxul::codeviz_vk
{

void destroy_buffer(VmaAllocator allocator, Buffer& buffer)
{
    if (buffer.mapped_by_vma_api && buffer.allocation != VK_NULL_HANDLE)
        vmaUnmapMemory(allocator, buffer.allocation);
    if (buffer.buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    buffer = {};
}

bool create_mapped_buffer(VkDevice device, VmaAllocator allocator, size_t size,
    VkBufferUsageFlags usage, vkresources::LifetimeScope lifetime,
    std::string_view debug_name, Buffer& buffer)
{
    PERF_MEASURE();
    vkresources::ScopedBuffer created_buffer;
    std::string error;
    if (!vkresources::create_buffer(device, allocator,
            vkresources::BufferRequest(size, usage,
                vkresources::MemoryPolicy::HostSequentialWrite, debug_name, lifetime),
            created_buffer, error))
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer,
            "MegaCity scene: failed to create mapped Vulkan buffer %.*s: %s",
            static_cast<int>(debug_name.size()), debug_name.data(), error.c_str());
        return false;
    }

    const vkresources::BufferResource created = created_buffer.release();
    buffer.buffer = created.buffer;
    buffer.allocation = created.allocation;
    buffer.mapped = created.mapped;
    buffer.size = static_cast<size_t>(created.size);
    buffer.mapped_by_vma_api = false;
    return true;
}

bool upload_mesh(VkDevice device, VmaAllocator allocator, const MeshData& mesh,
    std::string_view debug_name, MeshBuffers& gpu_mesh)
{
    PERF_MEASURE();
    if (mesh.vertices.empty() || mesh.indices.empty())
    {
        gpu_mesh = {};
        return true;
    }

    const size_t vertex_bytes = mesh.vertices.size() * sizeof(SceneVertex);
    const size_t index_bytes = mesh.indices.size() * sizeof(uint16_t);
    const std::string vertex_name = std::string(debug_name) + ".vertices";
    const std::string index_name = std::string(debug_name) + ".indices";
    if (!create_mapped_buffer(device, allocator, vertex_bytes,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vkresources::LifetimeScope::Persistent,
            vertex_name, gpu_mesh.vertices))
        return false;
    if (!create_mapped_buffer(device, allocator, index_bytes,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, vkresources::LifetimeScope::Persistent,
            index_name, gpu_mesh.indices))
        return false;

    std::memcpy(gpu_mesh.vertices.mapped, mesh.vertices.data(), vertex_bytes);
    std::memcpy(gpu_mesh.indices.mapped, mesh.indices.data(), index_bytes);
    vmaFlushAllocation(allocator, gpu_mesh.vertices.allocation, 0, vertex_bytes);
    vmaFlushAllocation(allocator, gpu_mesh.indices.allocation, 0, index_bytes);
    gpu_mesh.index_count = static_cast<uint32_t>(mesh.indices.size());
    return true;
}

size_t align_up(size_t value, size_t alignment)
{
    if (alignment <= 1)
        return value;
    const size_t remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

size_t grow_capacity(size_t current_size, size_t required_size, size_t minimum_size)
{
    if (current_size == 0)
        return std::max(required_size, minimum_size);
    return std::max(required_size, std::max(current_size * 2, minimum_size));
}

bool ensure_mapped_buffer_capacity(VkDevice device, VmaAllocator allocator, size_t required_size,
    VkBufferUsageFlags usage, vkresources::LifetimeScope lifetime,
    std::string_view debug_name, Buffer& buffer, size_t minimum_size)
{
    PERF_MEASURE();
    if (required_size == 0 || required_size <= buffer.size)
        return true;

    Buffer replacement;
    if (!create_mapped_buffer(device, allocator,
            grow_capacity(buffer.size, required_size, minimum_size), usage,
            lifetime, debug_name, replacement))
        return false;

    destroy_buffer(allocator, buffer);
    buffer = std::move(replacement);
    return true;
}

bool reserve_transient_buffer(VkDevice device, VmaAllocator allocator,
    TransientBufferArena& arena, size_t size, size_t alignment,
    VkBufferUsageFlags usage, size_t minimum_size, std::string_view debug_name,
    BufferSlice& slice)
{
    PERF_MEASURE();
    if (size == 0)
    {
        slice = {};
        return true;
    }

    const size_t offset = align_up(arena.head, alignment);
    const size_t required_size = offset + size;
    if (!ensure_mapped_buffer_capacity(device, allocator, required_size, usage,
            vkresources::LifetimeScope::Frame, debug_name, arena.buffer, minimum_size))
        return false;

    slice.buffer = arena.buffer.buffer;
    slice.allocation = arena.buffer.allocation;
    slice.offset = offset;
    slice.mapped = static_cast<char*>(arena.buffer.mapped) + offset;
    arena.head = required_size;
    return true;
}

bool stream_transient_mesh(VkDevice device, VmaAllocator allocator, const MeshData& mesh,
    TransientGeometryArena& arena, MeshSlice& slice)
{
    PERF_MEASURE();
    constexpr size_t kMinimumVertexArenaBytes = 16 * 1024;
    constexpr size_t kMinimumIndexArenaBytes = 4 * 1024;
    const size_t vertex_bytes = mesh.vertices.size() * sizeof(SceneVertex);
    const size_t index_bytes = mesh.indices.size() * sizeof(uint16_t);
    if (vertex_bytes == 0 || index_bytes == 0)
    {
        slice = {};
        return true;
    }

    BufferSlice vertex_slice;
    if (!reserve_transient_buffer(device, allocator, arena.vertices, vertex_bytes,
            alignof(SceneVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            kMinimumVertexArenaBytes, "megacity.transient.vertices", vertex_slice))
        return false;
    BufferSlice index_slice;
    if (!reserve_transient_buffer(device, allocator, arena.indices, index_bytes,
            alignof(uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            kMinimumIndexArenaBytes, "megacity.transient.indices", index_slice))
        return false;

    std::memcpy(vertex_slice.mapped, mesh.vertices.data(), vertex_bytes);
    std::memcpy(index_slice.mapped, mesh.indices.data(), index_bytes);
    vmaFlushAllocation(allocator, vertex_slice.allocation, vertex_slice.offset, vertex_bytes);
    vmaFlushAllocation(allocator, index_slice.allocation, index_slice.offset, index_bytes);
    slice.vertex_buffer = vertex_slice.buffer;
    slice.vertex_offset = static_cast<VkDeviceSize>(vertex_slice.offset);
    slice.index_buffer = index_slice.buffer;
    slice.index_offset = static_cast<VkDeviceSize>(index_slice.offset);
    slice.index_count = static_cast<uint32_t>(mesh.indices.size());
    return true;
}

void destroy_mesh(VmaAllocator allocator, MeshBuffers& mesh)
{
    destroy_buffer(allocator, mesh.vertices);
    destroy_buffer(allocator, mesh.indices);
    mesh.index_count = 0;
}

void destroy_image(VkDevice device, VmaAllocator allocator, ImageResource& image)
{
    if (image.sampler != VK_NULL_HANDLE)
        vkDestroySampler(device, image.sampler, nullptr);
    if (image.view != VK_NULL_HANDLE)
        vkDestroyImageView(device, image.view, nullptr);
    if (image.image != VK_NULL_HANDLE)
        vmaDestroyImage(allocator, image.image, image.allocation);
    image = {};
}

bool create_sampled_image(VkPhysicalDevice physical_device, VkDevice device, VmaAllocator allocator,
    int width, int height, VkFormat format, VkSamplerAddressMode address_mode,
    bool generate_mips, VkImageLayout final_layout,
    vkresources::LifetimeScope lifetime, std::string_view debug_name,
    ImageResource& image)
{
    PERF_MEASURE();
    const uint32_t mip_levels = generate_mips
        ? static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(std::max(width, height))))) + 1
        : 1u;
    VkImageCreateInfo image_info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    image_info.mipLevels = mip_levels;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (mip_levels > 1)
        image_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    vkresources::ScopedImage created_image;
    std::string error;
    if (!vkresources::create_image(device, allocator,
            vkresources::ImageRequest(image_info, VK_IMAGE_VIEW_TYPE_2D,
                VK_IMAGE_ASPECT_COLOR_BIT, vkresources::MemoryPolicy::DevicePreferred,
                final_layout, debug_name, lifetime),
            created_image, error))
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer,
            "MegaCity scene: failed to create sampled image %.*s: %s",
            static_cast<int>(debug_name.size()), debug_name.data(), error.c_str());
        return false;
    }

    VkSamplerCreateInfo sampler_info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = address_mode;
    sampler_info.addressModeV = address_mode;
    sampler_info.addressModeW = address_mode;
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
        return false;

    const vkresources::ImageResource created = created_image.release();
    image.image = created.image;
    image.allocation = created.allocation;
    image.view = created.view;
    image.sampler = sampler;
    image.width = width;
    image.height = height;
    image.size = static_cast<VkDeviceSize>(width) * height * 4;
    image.mip_levels = mip_levels;
    return true;
}

bool create_label_image(VkPhysicalDevice physical_device, VkDevice device, VmaAllocator allocator,
    int width, int height, ImageResource& image)
{
    return create_sampled_image(physical_device, device, allocator, width, height,
        VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, false,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, vkresources::LifetimeScope::Persistent,
        "megacity.label-atlas", image);
}

VkSampleCountFlagBits choose_scene_sample_count(VkPhysicalDevice physical_device)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    const VkSampleCountFlags counts = properties.limits.framebufferColorSampleCounts
        & properties.limits.framebufferDepthSampleCounts;
    return (counts & VK_SAMPLE_COUNT_4_BIT) != 0
        ? VK_SAMPLE_COUNT_4_BIT
        : VK_SAMPLE_COUNT_1_BIT;
}

bool create_attachment_image(VkDevice device, VmaAllocator allocator, int width, int height,
    VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect_mask,
    VkSampleCountFlagBits samples, VkImageCreateFlags flags, VkImageLayout final_layout,
    vkresources::LifetimeScope lifetime, std::string_view debug_name,
    VkImage& image, VmaAllocation& allocation, VkImageView& view)
{
    PERF_MEASURE();
    VkImageCreateInfo image_info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_info.flags = flags;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1u };
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = samples;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    vkresources::ScopedImage created_image;
    std::string error;
    if (!vkresources::create_image(device, allocator,
            vkresources::ImageRequest(image_info, VK_IMAGE_VIEW_TYPE_2D,
                aspect_mask, vkresources::MemoryPolicy::DevicePreferred,
                final_layout, debug_name, lifetime),
            created_image, error))
        return false;
    const vkresources::ImageResource created = created_image.release();
    image = created.image;
    allocation = created.allocation;
    view = created.view;
    return true;
}

bool create_attachment_view(VkDevice device, VkImage image, VkFormat format,
    VkImageAspectFlags aspect_mask, std::string_view debug_name, VkImageView& view)
{
    VkImageSubresourceRange range{};
    range.aspectMask = aspect_mask;
    range.levelCount = 1;
    range.layerCount = 1;
    std::string error;
    return vkresources::create_image_view(device, image, format,
        VK_IMAGE_VIEW_TYPE_2D, range, debug_name, view, error);
}

bool create_cube_attachment_image(VkDevice device, VmaAllocator allocator, int size,
    VkFormat format, VkImageUsageFlags usage, VkImageLayout final_layout,
    vkresources::LifetimeScope lifetime, std::string_view debug_name,
    VkImage& image, VmaAllocation& allocation, VkImageView& cube_view,
    std::array<VkImageView, kPointShadowFaceCount>& face_views)
{
    PERF_MEASURE();
    VkImageCreateInfo image_info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_info.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = format;
    image_info.extent = { static_cast<uint32_t>(size), static_cast<uint32_t>(size), 1u };
    image_info.mipLevels = 1;
    image_info.arrayLayers = kPointShadowFaceCount;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = usage;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    vkresources::ScopedImage created_image;
    std::string error;
    if (!vkresources::create_image(device, allocator,
            vkresources::ImageRequest(image_info, VK_IMAGE_VIEW_TYPE_CUBE,
                VK_IMAGE_ASPECT_COLOR_BIT, vkresources::MemoryPolicy::DevicePreferred,
                final_layout, debug_name, lifetime),
            created_image, error))
        return false;

    std::array<VkImageView, kPointShadowFaceCount> created_faces{};
    for (uint32_t face_index = 0; face_index < kPointShadowFaceCount; ++face_index)
    {
        VkImageSubresourceRange range{};
        range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        range.levelCount = 1;
        range.baseArrayLayer = face_index;
        range.layerCount = 1;
        const std::string face_name = std::string(debug_name) + ".face-" + std::to_string(face_index);
        if (!vkresources::create_image_view(device, created_image.get().image, format,
                VK_IMAGE_VIEW_TYPE_2D, range, face_name, created_faces[face_index], error))
        {
            for (VkImageView face : created_faces)
            {
                if (face != VK_NULL_HANDLE)
                    vkDestroyImageView(device, face, nullptr);
            }
            return false;
        }
    }

    const vkresources::ImageResource created = created_image.release();
    image = created.image;
    allocation = created.allocation;
    cube_view = created.view;
    face_views = created_faces;
    return true;
}

void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
    VkAccessFlags src_access, VkAccessFlags dst_access,
    VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
    uint32_t base_mip_level, uint32_t level_count)
{
    PERF_MEASURE();
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = base_mip_level;
    range.levelCount = level_count;
    range.layerCount = 1;
    vkresources::transition_image(cmd, vkresources::ImageTransition{
        image, old_layout, new_layout, src_access, dst_access,
        src_stage, dst_stage, range });
}

bool generate_mipmaps(VkCommandBuffer cmd, const ImageResource& target)
{
    PERF_MEASURE();
    if (target.mip_levels <= 1)
        return true;

    int32_t mip_width = target.width;
    int32_t mip_height = target.height;

    for (uint32_t level = 1; level < target.mip_levels; ++level)
    {
        VkImageMemoryBarrier to_src = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        to_src.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        to_src.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        to_src.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        to_src.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        to_src.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_src.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_src.image = target.image;
        to_src.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_src.subresourceRange.baseMipLevel = level - 1;
        to_src.subresourceRange.levelCount = 1;
        to_src.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &to_src);

        VkImageBlit blit = {};
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

        VkImageMemoryBarrier to_shader = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        to_shader.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        to_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_shader.image = target.image;
        to_shader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        to_shader.subresourceRange.baseMipLevel = level - 1;
        to_shader.subresourceRange.levelCount = 1;
        to_shader.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &to_shader);

        mip_width = std::max(mip_width / 2, 1);
        mip_height = std::max(mip_height / 2, 1);
    }

    VkImageMemoryBarrier last_level_barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    last_level_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    last_level_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    last_level_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    last_level_barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    last_level_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    last_level_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    last_level_barrier.image = target.image;
    last_level_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    last_level_barrier.subresourceRange.baseMipLevel = target.mip_levels - 1;
    last_level_barrier.subresourceRange.levelCount = 1;
    last_level_barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &last_level_barrier);

    return true;
}

bool upload_rgba_texture(VkDevice device, VmaAllocator allocator, VkCommandBuffer cmd, Buffer& staging,
    size_t staging_offset, const LoadedTextureImage& image, ImageResource& target)
{
    PERF_MEASURE();
    const size_t bytes = static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4;
    const size_t required_bytes = staging_offset + bytes;
    if (!ensure_mapped_buffer_capacity(
            device, allocator, required_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            vkresources::LifetimeScope::Upload, "megacity.texture-staging",
            staging, required_bytes))
        return false;

    auto* staging_bytes = static_cast<std::byte*>(staging.mapped);
    std::memcpy(staging_bytes + staging_offset, image.rgba.data(), bytes);
    vmaFlushAllocation(allocator, staging.allocation, staging_offset, bytes);

    transition_image(
        cmd,
        target.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        target.mip_levels);
    VkBufferImageCopy copy = {};
    copy.bufferOffset = staging_offset;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = { static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height), 1 };
    vkCmdCopyBufferToImage(cmd, staging.buffer, target.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    if (target.mip_levels <= 1)
    {
        transition_image(cmd, target.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 1);
        return true;
    }
    return generate_mipmaps(cmd, target);
}

VkShaderModule load_shader(VkDevice device, const std::string& path)
{
    PERF_MEASURE();
    const std::filesystem::path shader_path(path);
    const std::string debug_name = "megacity.shader." + shader_path.filename().string();
    std::string error;
    const VkShaderModule shader_module = vkresources::load_shader_module(
        device, shader_path, debug_name, error);
    if (shader_module == VK_NULL_HANDLE)
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: %s", error.c_str());
    return shader_module;
}


} // namespace draxul::codeviz_vk
