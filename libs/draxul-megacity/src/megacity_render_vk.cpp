#include "isometric_scene_pass.h"

#include "megacity_material_assets.h"
#include "mesh_library.h"
#include "vk_render_context.h"
#include <algorithm>
#include <array>
#include <backends/imgui_impl_vulkan.h>
#include <cstring>
#include <draxul/log.h>
#include <draxul/runtime_path.h>
#include <fstream>
#include <imgui.h>
#include <vector>

namespace draxul
{

namespace
{

struct alignas(16) FrameUniforms
{
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f };
    glm::mat4 inv_view_proj{ 1.0f };
    glm::vec4 camera_pos{ 0.0f, 8.0f, 0.0f, 1.0f };
    glm::vec4 light_dir{ -0.5f, -1.0f, -0.3f, 0.0f };
    glm::vec4 point_light_pos{ 0.0f, 8.0f, 0.0f, 24.0f };
    glm::vec4 label_fade_px{ 1.0f, 15.0f, 0.0f, 0.0f };
    glm::vec4 render_tuning{ 1.0f, 1.0f, 0.45f, 0.0f };
    glm::vec4 screen_params{ 0.0f, 0.0f, 1.0f, 1.0f }; // x = viewport origin x, y = viewport origin y, z = 1 / viewport width, w = 1 / viewport height
    glm::vec4 ao_params{ 1.6f, 1.0f, 0.12f, 1.35f }; // x = radius world, y = radius pixels, z = bias, w = power
    glm::vec4 debug_view{ 0.0f, 1.0f, 0.0f, 0.0f }; // x = AO debug mode, y = AO denoise enabled
    glm::vec4 world_debug_bounds{ -5.0f, 5.0f, -5.0f, 5.0f }; // x = min x, y = max x, z = min z, w = max z
};

struct alignas(16) ObjectPushConstants
{
    glm::mat4 world{ 1.0f };
    glm::vec4 color{ 1.0f };
    glm::uvec4 material_data{ 0u };
    glm::vec4 uv_rect{ 0.0f, 0.0f, 1.0f, 1.0f };
    glm::vec4 label_metrics{ 0.0f };
};

struct alignas(16) MaterialInstanceUniform
{
    glm::vec4 scalar_params{ 1.0f, 1.0f, 1.0f, 0.0f };
    glm::uvec4 texture_indices{ 0u };
    glm::uvec4 metadata{ 0u };
};

struct alignas(16) MaterialUniforms
{
    std::array<MaterialInstanceUniform, kMaxSceneMaterials> materials{};
};

struct Buffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
    size_t size = 0;
};

struct MeshBuffers
{
    Buffer vertices;
    Buffer indices;
    uint32_t index_count = 0;
};

struct ImageResource
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;
    int size = 0;
    uint32_t mip_levels = 1;
};

struct BufferSlice
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
    size_t offset = 0;
};

struct MeshSlice
{
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceSize vertex_offset = 0;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceSize index_offset = 0;
    uint32_t index_count = 0;
};

struct TransientBufferArena
{
    Buffer buffer;
    size_t head = 0;

    void reset()
    {
        head = 0;
    }
};

struct TransientGeometryArena
{
    TransientBufferArena vertices;
    TransientBufferArena indices;

    void reset()
    {
        vertices.reset();
        indices.reset();
    }
};

struct FrameResources
{
    Buffer frame_uniforms;
    Buffer material_uniforms;
    TransientGeometryArena geometry_arena;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkDescriptorSet prepass_descriptor_set = VK_NULL_HANDLE;
};

struct GBufferTargets
{
    VkImage normal_image = VK_NULL_HANDLE; // RGBA8Unorm — RG octahedral normal, BA reserved
    VmaAllocation normal_alloc = VK_NULL_HANDLE;
    VkImageView normal_view = VK_NULL_HANDLE;

    VkImage ao_raw_image = VK_NULL_HANDLE; // RGBA8Unorm — raw AO before denoise
    VmaAllocation ao_raw_alloc = VK_NULL_HANDLE;
    VkImageView ao_raw_view = VK_NULL_HANDLE;

    VkImage ao_image = VK_NULL_HANDLE; // RGBA8Unorm — R ambient occlusion, GBA reserved
    VmaAllocation ao_alloc = VK_NULL_HANDLE;
    VkImageView ao_view = VK_NULL_HANDLE;

    VkImage depth_image = VK_NULL_HANDLE; // D32Float
    VmaAllocation depth_alloc = VK_NULL_HANDLE;
    VkImageView depth_view = VK_NULL_HANDLE;

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkFramebuffer ao_raw_framebuffer = VK_NULL_HANDLE;
    VkFramebuffer ao_framebuffer = VK_NULL_HANDLE;

    // ImGui debug visualization descriptor sets (lazily registered)
    VkDescriptorSet imgui_normal_ds = VK_NULL_HANDLE;
    VkDescriptorSet imgui_ao_raw_ds = VK_NULL_HANDLE;
    VkDescriptorSet imgui_ao_ds = VK_NULL_HANDLE;
    VkDescriptorSet imgui_depth_ds = VK_NULL_HANDLE;

    int width = 0;
    int height = 0;
};

bool same_grid_spec(const FloorGridSpec& a, const FloorGridSpec& b)
{
    return a.enabled == b.enabled
        && a.min_x == b.min_x
        && a.max_x == b.max_x
        && a.min_z == b.min_z
        && a.max_z == b.max_z
        && a.tile_size == b.tile_size
        && a.line_width == b.line_width
        && a.y == b.y
        && a.color.x == b.color.x
        && a.color.y == b.color.y
        && a.color.z == b.color.z
        && a.color.w == b.color.w;
}

MaterialUniforms build_material_uniforms(const SceneSnapshot& scene)
{
    MaterialUniforms uniforms{};
    const size_t material_count = std::min(scene.materials.size(), uniforms.materials.size());
    for (size_t index = 0; index < material_count; ++index)
    {
        const SceneMaterial& material = scene.materials[index];
        uniforms.materials[index].scalar_params = material.scalar_params;
        uniforms.materials[index].texture_indices = material.texture_indices;
        uniforms.materials[index].metadata = glm::uvec4(
            static_cast<uint32_t>(material.shading_model),
            material.metadata.y,
            material.metadata.z,
            material.metadata.w);
    }
    return uniforms;
}

float compute_ao_radius_pixels(const glm::mat4& proj, float radius_world, int viewport_h)
{
    if (viewport_h <= 0)
        return 1.0f;
    const float ortho_scale_y = std::abs(proj[1][1]);
    return std::max(1.0f, radius_world * ortho_scale_y * 0.5f * static_cast<float>(viewport_h));
}

void destroy_buffer(VmaAllocator allocator, Buffer& buffer)
{
    if (buffer.buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    buffer = {};
}

bool create_mapped_buffer(VmaAllocator allocator, size_t size, VkBufferUsageFlags usage, Buffer& buffer)
{
    VkBufferCreateInfo buf_ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buf_ci.size = size;
    buf_ci.usage = usage;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_info = {};
    if (vmaCreateBuffer(allocator, &buf_ci, &alloc_ci, &buffer.buffer, &buffer.allocation, &alloc_info) != VK_SUCCESS)
        return false;

    buffer.mapped = alloc_info.pMappedData;
    buffer.size = size;
    return true;
}

bool upload_mesh(VmaAllocator allocator, const MeshData& mesh, MeshBuffers& gpu_mesh)
{
    const size_t vertex_bytes = mesh.vertices.size() * sizeof(SceneVertex);
    const size_t index_bytes = mesh.indices.size() * sizeof(uint16_t);

    if (!create_mapped_buffer(allocator, vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, gpu_mesh.vertices))
        return false;
    if (!create_mapped_buffer(allocator, index_bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, gpu_mesh.indices))
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

bool ensure_mapped_buffer_capacity(VmaAllocator allocator, size_t required_size,
    VkBufferUsageFlags usage, Buffer& buffer, size_t minimum_size)
{
    if (required_size == 0 || required_size <= buffer.size)
        return true;

    Buffer replacement;
    if (!create_mapped_buffer(allocator, grow_capacity(buffer.size, required_size, minimum_size), usage, replacement))
        return false;

    destroy_buffer(allocator, buffer);
    buffer = std::move(replacement);
    return true;
}

bool reserve_transient_buffer(VmaAllocator allocator, TransientBufferArena& arena, size_t size,
    size_t alignment, VkBufferUsageFlags usage, size_t minimum_size, BufferSlice& slice)
{
    if (size == 0)
    {
        slice = {};
        return true;
    }

    const size_t offset = align_up(arena.head, alignment);
    const size_t required_size = offset + size;
    if (!ensure_mapped_buffer_capacity(allocator, required_size, usage, arena.buffer, minimum_size))
        return false;

    slice.buffer = arena.buffer.buffer;
    slice.allocation = arena.buffer.allocation;
    slice.offset = offset;
    slice.mapped = static_cast<char*>(arena.buffer.mapped) + offset;
    arena.head = required_size;
    return true;
}

bool stream_transient_mesh(VmaAllocator allocator, const MeshData& mesh,
    TransientGeometryArena& arena, MeshSlice& slice)
{
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
    if (!reserve_transient_buffer(allocator, arena.vertices, vertex_bytes, alignof(SceneVertex),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, kMinimumVertexArenaBytes, vertex_slice))
    {
        return false;
    }

    BufferSlice index_slice;
    if (!reserve_transient_buffer(allocator, arena.indices, index_bytes, alignof(uint16_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT, kMinimumIndexArenaBytes, index_slice))
    {
        return false;
    }

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
    int width, int height, VkFormat format, VkSamplerAddressMode address_mode, bool generate_mips, ImageResource& image)
{
    const uint32_t mip_levels = generate_mips
        ? static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(std::max(width, height))))) + 1
        : 1u;
    VkImageCreateInfo img_ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    img_ci.imageType = VK_IMAGE_TYPE_2D;
    img_ci.format = format;
    img_ci.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    img_ci.mipLevels = mip_levels;
    img_ci.arrayLayers = 1;
    img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (mip_levels > 1)
        img_ci.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    img_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(allocator, &img_ci, &alloc_ci, &image.image, &image.allocation, nullptr) != VK_SUCCESS)
        return false;

    VkImageViewCreateInfo view_ci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view_ci.image = image.image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = img_ci.format;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.levelCount = mip_levels;
    view_ci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &view_ci, nullptr, &image.view) != VK_SUCCESS)
        return false;

    VkSamplerCreateInfo sampler_ci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampler_ci.magFilter = VK_FILTER_LINEAR;
    sampler_ci.minFilter = VK_FILTER_LINEAR;
    sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_ci.addressModeU = address_mode;
    sampler_ci.addressModeV = address_mode;
    sampler_ci.addressModeW = address_mode;
    sampler_ci.maxLod = static_cast<float>(mip_levels - 1);
    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(physical_device, &features);
    if (features.samplerAnisotropy)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_device, &properties);
        sampler_ci.anisotropyEnable = VK_TRUE;
        sampler_ci.maxAnisotropy = std::min(8.0f, properties.limits.maxSamplerAnisotropy);
    }
    if (vkCreateSampler(device, &sampler_ci, nullptr, &image.sampler) != VK_SUCCESS)
        return false;

    image.width = width;
    image.height = height;
    image.size = width;
    image.mip_levels = mip_levels;
    return true;
}

bool create_label_image(VkPhysicalDevice physical_device, VkDevice device, VmaAllocator allocator, int size,
    ImageResource& image)
{
    return create_sampled_image(
        physical_device,
        device,
        allocator,
        size,
        size,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        false,
        image);
}

void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
    uint32_t base_mip_level = 0, uint32_t level_count = 1)
{
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = base_mip_level;
    barrier.subresourceRange.levelCount = level_count;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    if (new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

bool generate_mipmaps(VkCommandBuffer cmd, const ImageResource& target)
{
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

bool upload_rgba_texture(VmaAllocator allocator, VkCommandBuffer cmd, Buffer& staging,
    const LoadedTextureImage& image, ImageResource& target)
{
    const size_t bytes = static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4;
    if (!ensure_mapped_buffer_capacity(allocator, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, bytes))
        return false;

    std::memcpy(staging.mapped, image.rgba.data(), bytes);
    vmaFlushAllocation(allocator, staging.allocation, 0, bytes);

    transition_image(
        cmd,
        target.image,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0,
        target.mip_levels);
    VkBufferImageCopy copy = {};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = { static_cast<uint32_t>(image.width), static_cast<uint32_t>(image.height), 1 };
    vkCmdCopyBufferToImage(cmd, staging.buffer, target.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    if (target.mip_levels <= 1)
    {
        transition_image(cmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return true;
    }
    return generate_mipmaps(cmd, target);
}

VkShaderModule load_shader(VkDevice device, const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to open shader %s", path.c_str());
        return VK_NULL_HANDLE;
    }

    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(size));

    VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = size;
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shader_module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &shader_module) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create shader module %s", path.c_str());
        return VK_NULL_HANDLE;
    }
    return shader_module;
}

glm::mat4 make_vulkan_projection(glm::mat4 proj)
{
    proj[1][1] *= -1.0f;
    return proj;
}

} // namespace

struct IsometricScenePass::State
{
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout prepass_descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool prepass_descriptor_pool = VK_NULL_HANDLE;
    VkPipelineLayout prepass_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline debug_pipeline = VK_NULL_HANDLE;
    MeshBuffers cube_mesh;
    MeshBuffers floor_mesh;
    MeshBuffers tree_bark_mesh;
    MeshBuffers tree_leaf_mesh;
    MeshBuffers road_surface_mesh;
    MeshBuffers roof_sign_mesh;
    MeshBuffers wall_sign_mesh;
    const MeshData* tree_bark_mesh_source = nullptr;
    const MeshData* tree_leaf_mesh_source = nullptr;
    MeshData cached_grid_mesh;
    FloorGridSpec cached_grid_spec;
    bool has_cached_grid_mesh = false;
    ImageResource label_atlas;
    Buffer label_staging;
    Buffer material_staging;
    VkImageLayout label_atlas_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint64_t label_atlas_revision = 0;
    std::vector<FrameResources> frame_resources;
    uint32_t buffered_frame_count = 1;

    // GBuffer pre-pass resources
    VkRenderPass gbuffer_render_pass = VK_NULL_HANDLE;
    VkPipeline gbuffer_pipeline = VK_NULL_HANDLE;
    VkRenderPass ao_render_pass = VK_NULL_HANDLE;
    VkPipeline ao_pipeline = VK_NULL_HANDLE;
    VkPipeline ao_blur_pipeline = VK_NULL_HANDLE;
    VkSampler gbuffer_sampler = VK_NULL_HANDLE;
    VkSampler gbuffer_point_sampler = VK_NULL_HANDLE;
    std::array<ImageResource, kSceneMaterialTextureCount> material_textures;
    std::vector<GBufferTargets> gbuffer_targets;
    bool gbuffer_initialized = false;
    uint32_t last_prepass_frame = 0;

    bool create_device_resources(uint32_t frame_count)
    {
        VkDescriptorSetLayoutBinding scene_bindings[5] = {};
        scene_bindings[0].binding = 0;
        scene_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        scene_bindings[0].descriptorCount = 1;
        scene_bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        scene_bindings[1].binding = 1;
        scene_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        scene_bindings[1].descriptorCount = 1;
        scene_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        scene_bindings[2].binding = 2;
        scene_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        scene_bindings[2].descriptorCount = 1;
        scene_bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        scene_bindings[3].binding = 3;
        scene_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        scene_bindings[3].descriptorCount = 1;
        scene_bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        scene_bindings[4].binding = 4;
        scene_bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        scene_bindings[4].descriptorCount = kSceneMaterialTextureCount;
        scene_bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layout_ci.bindingCount = 5;
        layout_ci.pBindings = scene_bindings;
        if (vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &descriptor_set_layout) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create descriptor set layout");
            return false;
        }

        VkDescriptorSetLayoutBinding prepass_bindings[4] = {};
        prepass_bindings[0].binding = 0;
        prepass_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        prepass_bindings[0].descriptorCount = 1;
        prepass_bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        prepass_bindings[1].binding = 1;
        prepass_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        prepass_bindings[1].descriptorCount = 1;
        prepass_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        prepass_bindings[2].binding = 2;
        prepass_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        prepass_bindings[2].descriptorCount = 1;
        prepass_bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        prepass_bindings[3].binding = 3;
        prepass_bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        prepass_bindings[3].descriptorCount = 1;
        prepass_bindings[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo prepass_layout_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        prepass_layout_ci.bindingCount = 4;
        prepass_layout_ci.pBindings = prepass_bindings;
        if (vkCreateDescriptorSetLayout(device, &prepass_layout_ci, nullptr, &prepass_descriptor_set_layout)
            != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create prepass descriptor set layout");
            return false;
        }

        VkDescriptorPoolSize pool_sizes[2] = {};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = frame_count * 2;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = frame_count * (2 + kSceneMaterialTextureCount);

        VkDescriptorPoolCreateInfo pool_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool_ci.maxSets = frame_count;
        pool_ci.poolSizeCount = 2;
        pool_ci.pPoolSizes = pool_sizes;
        if (vkCreateDescriptorPool(device, &pool_ci, nullptr, &descriptor_pool) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create descriptor pool");
            return false;
        }

        VkDescriptorPoolSize prepass_pool_sizes[2] = {};
        prepass_pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        prepass_pool_sizes[0].descriptorCount = frame_count;
        prepass_pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        prepass_pool_sizes[1].descriptorCount = frame_count * 3;

        VkDescriptorPoolCreateInfo prepass_pool_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        prepass_pool_ci.maxSets = frame_count;
        prepass_pool_ci.poolSizeCount = 2;
        prepass_pool_ci.pPoolSizes = prepass_pool_sizes;
        if (vkCreateDescriptorPool(device, &prepass_pool_ci, nullptr, &prepass_descriptor_pool) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create prepass descriptor pool");
            return false;
        }

        frame_resources.assign(frame_count, {});
        std::vector<VkDescriptorSetLayout> layouts(frame_count, descriptor_set_layout);
        std::vector<VkDescriptorSet> descriptor_sets(frame_count, VK_NULL_HANDLE);
        VkDescriptorSetAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc_info.descriptorPool = descriptor_pool;
        alloc_info.descriptorSetCount = frame_count;
        alloc_info.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(device, &alloc_info, descriptor_sets.data()) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to allocate descriptor set");
            return false;
        }

        std::vector<VkDescriptorSetLayout> prepass_layouts(frame_count, prepass_descriptor_set_layout);
        std::vector<VkDescriptorSet> prepass_descriptor_sets(frame_count, VK_NULL_HANDLE);
        VkDescriptorSetAllocateInfo prepass_alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        prepass_alloc_info.descriptorPool = prepass_descriptor_pool;
        prepass_alloc_info.descriptorSetCount = frame_count;
        prepass_alloc_info.pSetLayouts = prepass_layouts.data();
        if (vkAllocateDescriptorSets(device, &prepass_alloc_info, prepass_descriptor_sets.data()) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to allocate prepass descriptor set");
            return false;
        }

        for (uint32_t i = 0; i < frame_count; ++i)
        {
            auto& frame = frame_resources[i];
            frame.descriptor_set = descriptor_sets[i];
            frame.prepass_descriptor_set = prepass_descriptor_sets[i];

            if (!create_mapped_buffer(allocator, sizeof(FrameUniforms),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, frame.frame_uniforms))
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create frame uniform buffer");
                return false;
            }
            if (!create_mapped_buffer(allocator, sizeof(MaterialUniforms),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, frame.material_uniforms))
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create material uniform buffer");
                return false;
            }

            VkDescriptorBufferInfo buffer_info = {};
            buffer_info.buffer = frame.frame_uniforms.buffer;
            buffer_info.range = sizeof(FrameUniforms);
            VkDescriptorBufferInfo material_buffer_info = {};
            material_buffer_info.buffer = frame.material_uniforms.buffer;
            material_buffer_info.range = sizeof(MaterialUniforms);

            VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = frame.descriptor_set;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &buffer_info;
            VkWriteDescriptorSet material_write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            material_write.dstSet = frame.descriptor_set;
            material_write.dstBinding = 1;
            material_write.descriptorCount = 1;
            material_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            material_write.pBufferInfo = &material_buffer_info;
            VkWriteDescriptorSet prepass_write = write;
            prepass_write.dstSet = frame.prepass_descriptor_set;
            VkWriteDescriptorSet writes[3] = { write, material_write, prepass_write };
            vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
        }

        buffered_frame_count = frame_count;
        refresh_label_descriptors();
        refresh_gbuffer_descriptors();
        refresh_prepass_descriptors();

        if (!upload_mesh(allocator, build_unit_cube_mesh(), cube_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to upload cube mesh");
            return false;
        }
        if (!upload_mesh(allocator, build_floor_box_mesh(), floor_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to upload floor mesh");
            return false;
        }
        if (!upload_mesh(allocator, build_tree_bark_mesh(), tree_bark_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to upload tree bark mesh");
            return false;
        }
        if (!upload_mesh(allocator, build_tree_leaf_mesh(), tree_leaf_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to upload tree leaf mesh");
            return false;
        }
        if (!upload_mesh(allocator, build_road_surface_mesh(), road_surface_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to upload road surface mesh");
            return false;
        }
        if (!upload_mesh(allocator, build_roof_sign_mesh(), roof_sign_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to upload roof sign mesh");
            return false;
        }
        if (!upload_mesh(allocator, build_wall_sign_mesh(), wall_sign_mesh))
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to upload wall sign mesh");
            return false;
        }
        return true;
    }

    bool ensure_floor_grid(const FloorGridSpec& spec)
    {
        if (!spec.enabled)
        {
            cached_grid_mesh = {};
            cached_grid_spec = {};
            has_cached_grid_mesh = false;
            return true;
        }
        if (has_cached_grid_mesh && same_grid_spec(cached_grid_spec, spec))
            return true;

        cached_grid_mesh = build_outline_grid_mesh(spec);
        cached_grid_spec = spec;
        has_cached_grid_mesh = true;
        return true;
    }

    bool ensure_road_materials(const VkRenderContext& ctx, VkCommandBuffer cmd)
    {
        if (std::all_of(material_textures.begin(), material_textures.end(),
                [](const ImageResource& texture) { return texture.image != VK_NULL_HANDLE; }))
        {
            return true;
        }

        const AsphaltRoadMaterialImages road_images = load_asphalt_road_material_images();
        const PavingSidewalkMaterialImages sidewalk_images = load_paving_sidewalk_material_images();
        const WoodBuildingMaterialImages wood_images = load_wood_building_material_images();
        const BarkTreeMaterialImages bark_images = load_bark_tree_material_images();
        const LeafAtlasMaterialImages leaf_images = load_leaf_atlas_material_images();
        if (!road_images.valid() || !sidewalk_images.valid() || !wood_images.valid()
            || !bark_images.valid() || !leaf_images.valid())
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to load Megacity material images");
            return false;
        }

        for (auto& texture : material_textures)
            destroy_image(device, allocator, texture);

        LoadedTextureImage fallback_albedo;
        fallback_albedo.width = 1;
        fallback_albedo.height = 1;
        fallback_albedo.rgba = { 255, 255, 255, 255 };
        LoadedTextureImage fallback_scalar = fallback_albedo;
        LoadedTextureImage fallback_normal;
        fallback_normal.width = 1;
        fallback_normal.height = 1;
        fallback_normal.rgba = { 128, 128, 255, 255 };

        struct TextureLoadSpec
        {
            SceneTextureId id;
            const LoadedTextureImage* image = nullptr;
            VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        };

        const std::array<TextureLoadSpec, kSceneMaterialTextureCount> load_specs = { {
            { SceneTextureId::FallbackAlbedoSrgb, &fallback_albedo, VK_FORMAT_R8G8B8A8_SRGB },
            { SceneTextureId::FallbackScalar, &fallback_scalar, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::FallbackNormal, &fallback_normal, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::AsphaltAlbedo, &road_images.albedo, VK_FORMAT_R8G8B8A8_SRGB },
            { SceneTextureId::AsphaltNormal, &road_images.normal, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::AsphaltRoughness, &road_images.roughness, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::AsphaltAo, &road_images.ao, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::SidewalkAlbedo, &sidewalk_images.albedo, VK_FORMAT_R8G8B8A8_SRGB },
            { SceneTextureId::SidewalkNormal, &sidewalk_images.normal, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::SidewalkRoughness, &sidewalk_images.roughness, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::SidewalkAo, &sidewalk_images.ao, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::WoodAlbedo, &wood_images.albedo, VK_FORMAT_R8G8B8A8_SRGB },
            { SceneTextureId::WoodNormal, &wood_images.normal, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::WoodRoughness, &wood_images.roughness, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::WoodAo, &wood_images.ao, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::BarkAlbedo, &bark_images.albedo, VK_FORMAT_R8G8B8A8_SRGB },
            { SceneTextureId::BarkNormal, &bark_images.normal, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::BarkRoughness, &bark_images.roughness, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::BarkAo, &bark_images.ao, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::LeafAlbedo, &leaf_images.albedo, VK_FORMAT_R8G8B8A8_SRGB },
            { SceneTextureId::LeafNormal, &leaf_images.normal, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::LeafRoughness, &leaf_images.roughness, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::LeafOpacity, &leaf_images.opacity, VK_FORMAT_R8G8B8A8_UNORM },
            { SceneTextureId::LeafScattering, &leaf_images.scattering, VK_FORMAT_R8G8B8A8_UNORM },
        } };

        for (const TextureLoadSpec& spec : load_specs)
        {
            ImageResource& texture = material_textures[static_cast<size_t>(spec.id)];
            if (!create_sampled_image(
                    ctx.physical_device(),
                    device,
                    allocator,
                    spec.image->width,
                    spec.image->height,
                    spec.format,
                    VK_SAMPLER_ADDRESS_MODE_REPEAT,
                    true,
                    texture)
                || !upload_rgba_texture(allocator, cmd, material_staging, *spec.image, texture))
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create or upload material texture");
                for (auto& cleanup : material_textures)
                    destroy_image(device, allocator, cleanup);
                return false;
            }
        }

        refresh_gbuffer_descriptors();
        refresh_prepass_descriptors();
        return true;
    }

    bool ensure_tree_mesh(
        const std::shared_ptr<const MeshData>& tree_bark_mesh_data,
        const std::shared_ptr<const MeshData>& tree_leaf_mesh_data)
    {
        if (tree_bark_mesh_data
            && !(tree_bark_mesh_source == tree_bark_mesh_data.get() && tree_bark_mesh.index_count > 0))
        {
            MeshBuffers replacement;
            if (!upload_mesh(allocator, *tree_bark_mesh_data, replacement))
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to upload procedural tree bark mesh");
                return false;
            }
            destroy_mesh(allocator, tree_bark_mesh);
            tree_bark_mesh = std::move(replacement);
            tree_bark_mesh_source = tree_bark_mesh_data.get();
        }
        if (tree_leaf_mesh_data
            && !(tree_leaf_mesh_source == tree_leaf_mesh_data.get() && tree_leaf_mesh.index_count > 0))
        {
            MeshBuffers replacement;
            if (!upload_mesh(allocator, *tree_leaf_mesh_data, replacement))
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to upload procedural tree leaf mesh");
                return false;
            }
            destroy_mesh(allocator, tree_leaf_mesh);
            tree_leaf_mesh = std::move(replacement);
            tree_leaf_mesh_source = tree_leaf_mesh_data.get();
        }
        return true;
    }

    void refresh_label_descriptors()
    {
        if (label_atlas.view == VK_NULL_HANDLE || label_atlas.sampler == VK_NULL_HANDLE)
            return;

        for (auto& frame : frame_resources)
        {
            VkDescriptorImageInfo image_info = {};
            image_info.sampler = label_atlas.sampler;
            image_info.imageView = label_atlas.view;
            image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = frame.descriptor_set;
            write.dstBinding = 2;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &image_info;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }

    void refresh_gbuffer_descriptors()
    {
        if (gbuffer_sampler == VK_NULL_HANDLE || gbuffer_targets.empty())
            return;

        const bool have_material_textures = std::all_of(material_textures.begin(), material_textures.end(),
            [](const ImageResource& texture) {
                return texture.view != VK_NULL_HANDLE
                    && texture.sampler != VK_NULL_HANDLE;
            });

        for (size_t i = 0; i < frame_resources.size() && i < gbuffer_targets.size(); ++i)
        {
            const auto& gbuffer = gbuffer_targets[i];
            auto& frame = frame_resources[i];
            if (frame.descriptor_set == VK_NULL_HANDLE
                || gbuffer.ao_view == VK_NULL_HANDLE)
            {
                continue;
            }

            VkDescriptorImageInfo ao_info = {};
            ao_info.sampler = gbuffer_sampler;
            ao_info.imageView = gbuffer.ao_view;
            ao_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            std::array<VkDescriptorImageInfo, kSceneMaterialTextureCount> material_infos{};
            for (size_t texture_index = 0; texture_index < material_textures.size(); ++texture_index)
            {
                material_infos[texture_index].sampler = material_textures[texture_index].sampler;
                material_infos[texture_index].imageView = material_textures[texture_index].view;
                material_infos[texture_index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            VkWriteDescriptorSet writes[2] = {};
            uint32_t write_count = 0;

            writes[write_count].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[write_count].dstSet = frame.descriptor_set;
            writes[write_count].dstBinding = 3;
            writes[write_count].descriptorCount = 1;
            writes[write_count].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[write_count].pImageInfo = &ao_info;
            ++write_count;

            if (have_material_textures)
            {
                writes[write_count].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[write_count].dstSet = frame.descriptor_set;
                writes[write_count].dstBinding = 4;
                writes[write_count].descriptorCount = static_cast<uint32_t>(material_infos.size());
                writes[write_count].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[write_count].pImageInfo = material_infos.data();
                ++write_count;
            }

            vkUpdateDescriptorSets(device, write_count, writes, 0, nullptr);
        }
    }

    void refresh_prepass_descriptors()
    {
        if (gbuffer_point_sampler == VK_NULL_HANDLE || gbuffer_targets.empty())
            return;

        for (size_t i = 0; i < frame_resources.size() && i < gbuffer_targets.size(); ++i)
        {
            const auto& gbuffer = gbuffer_targets[i];
            auto& frame = frame_resources[i];
            if (frame.prepass_descriptor_set == VK_NULL_HANDLE
                || gbuffer.ao_raw_view == VK_NULL_HANDLE
                || gbuffer.normal_view == VK_NULL_HANDLE
                || gbuffer.depth_view == VK_NULL_HANDLE)
            {
                continue;
            }

            VkDescriptorImageInfo raw_ao_info = {};
            raw_ao_info.sampler = gbuffer_point_sampler;
            raw_ao_info.imageView = gbuffer.ao_raw_view;
            raw_ao_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo normal_info = {};
            normal_info.sampler = gbuffer_point_sampler;
            normal_info.imageView = gbuffer.normal_view;
            normal_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo depth_info = {};
            depth_info.sampler = gbuffer_point_sampler;
            depth_info.imageView = gbuffer.depth_view;
            depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet writes[3] = {};
            uint32_t write_count = 0;

            writes[write_count].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[write_count].dstSet = frame.prepass_descriptor_set;
            writes[write_count].dstBinding = 1;
            writes[write_count].descriptorCount = 1;
            writes[write_count].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[write_count].pImageInfo = &raw_ao_info;
            ++write_count;

            writes[write_count].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[write_count].dstSet = frame.prepass_descriptor_set;
            writes[write_count].dstBinding = 2;
            writes[write_count].descriptorCount = 1;
            writes[write_count].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[write_count].pImageInfo = &normal_info;
            ++write_count;

            writes[write_count].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[write_count].dstSet = frame.prepass_descriptor_set;
            writes[write_count].dstBinding = 3;
            writes[write_count].descriptorCount = 1;
            writes[write_count].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[write_count].pImageInfo = &depth_info;
            ++write_count;

            vkUpdateDescriptorSets(device, write_count, writes, 0, nullptr);
        }
    }

    bool ensure_label_atlas(const VkRenderContext& ctx, VkCommandBuffer cmd,
        const std::shared_ptr<const LabelAtlasData>& atlas)
    {
        const bool has_atlas = atlas && atlas->valid();
        const int desired_size = has_atlas ? atlas->width : 1;

        if (label_atlas.size != desired_size || label_atlas.image == VK_NULL_HANDLE)
        {
            destroy_image(device, allocator, label_atlas);
            if (!create_label_image(physical_device, device, allocator, desired_size, label_atlas))
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create label atlas image");
                return false;
            }
            label_atlas_layout = VK_IMAGE_LAYOUT_UNDEFINED;
            label_atlas_revision = std::numeric_limits<uint64_t>::max();
            refresh_label_descriptors();
        }

        if (has_atlas && label_atlas_revision == atlas->revision)
            return true;
        if (!has_atlas && label_atlas_revision == 0)
            return true;

        const uint8_t clear_pixel[4] = { 0, 0, 0, 0 };
        const uint8_t* pixels = has_atlas ? atlas->rgba.data() : clear_pixel;
        const size_t bytes = static_cast<size_t>(desired_size) * static_cast<size_t>(desired_size) * 4;
        if (!ensure_mapped_buffer_capacity(allocator, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, label_staging, bytes))
            return false;

        std::memcpy(label_staging.mapped, pixels, bytes);
        vmaFlushAllocation(allocator, label_staging.allocation, 0, bytes);

        transition_image(cmd, label_atlas.image, label_atlas_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy = {};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = { static_cast<uint32_t>(desired_size), static_cast<uint32_t>(desired_size), 1 };
        vkCmdCopyBufferToImage(cmd, label_staging.buffer, label_atlas.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transition_image(cmd, label_atlas.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        label_atlas_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        label_atlas_revision = has_atlas ? atlas->revision : 0;
        return true;
    }

    bool create_pipeline()
    {
        const auto shader_dir = bundled_asset_path("shaders");
        auto vert = load_shader(device, (shader_dir / "megacity_scene.vert.spv").string());
        auto frag = load_shader(device, (shader_dir / "megacity_scene.frag.spv").string());
        if (!vert || !frag)
        {
            if (vert)
                vkDestroyShaderModule(device, vert, nullptr);
            if (frag)
                vkDestroyShaderModule(device, frag, nullptr);
            return false;
        }

        VkPushConstantRange push_range = {};
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        push_range.offset = 0;
        push_range.size = sizeof(ObjectPushConstants);

        VkPipelineLayoutCreateInfo layout_ci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        layout_ci.setLayoutCount = 1;
        layout_ci.pSetLayouts = &descriptor_set_layout;
        layout_ci.pushConstantRangeCount = 1;
        layout_ci.pPushConstantRanges = &push_range;
        if (vkCreatePipelineLayout(device, &layout_ci, nullptr, &pipeline_layout) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create pipeline layout");
            vkDestroyShaderModule(device, vert, nullptr);
            vkDestroyShaderModule(device, frag, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";

        VkVertexInputBindingDescription binding = {};
        binding.binding = 0;
        binding.stride = sizeof(SceneVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attributes[6] = {};
        attributes[0].location = 0;
        attributes[0].binding = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(SceneVertex, position);
        attributes[1].location = 1;
        attributes[1].binding = 0;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(SceneVertex, normal);
        attributes[2].location = 2;
        attributes[2].binding = 0;
        attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[2].offset = offsetof(SceneVertex, color);
        attributes[3].location = 3;
        attributes[3].binding = 0;
        attributes[3].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[3].offset = offsetof(SceneVertex, uv);
        attributes[4].location = 4;
        attributes[4].binding = 0;
        attributes[4].format = VK_FORMAT_R32_SFLOAT;
        attributes[4].offset = offsetof(SceneVertex, tex_blend);
        attributes[5].location = 5;
        attributes[5].binding = 0;
        attributes[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[5].offset = offsetof(SceneVertex, tangent);

        VkPipelineVertexInputStateCreateInfo vertex_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = 6;
        vertex_input.pVertexAttributeDescriptions = attributes;

        VkPipelineInputAssemblyStateCreateInfo input_assembly = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport_state = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState blend_attachment = {};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blend = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;

        VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;

        VkGraphicsPipelineCreateInfo pipeline_ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeline_ci.stageCount = 2;
        pipeline_ci.pStages = stages;
        pipeline_ci.pVertexInputState = &vertex_input;
        pipeline_ci.pInputAssemblyState = &input_assembly;
        pipeline_ci.pViewportState = &viewport_state;
        pipeline_ci.pRasterizationState = &raster;
        pipeline_ci.pMultisampleState = &multisample;
        pipeline_ci.pDepthStencilState = &depth;
        pipeline_ci.pColorBlendState = &blend;
        pipeline_ci.pDynamicState = &dynamic;
        pipeline_ci.layout = pipeline_layout;
        pipeline_ci.renderPass = render_pass;
        pipeline_ci.subpass = 0;

        const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &pipeline);
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        if (result != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create graphics pipeline");
            return false;
        }

        // Create debug pipeline (same layout, same vertex shader, different fragment shader)
        auto debug_frag = load_shader(device, (shader_dir / "megacity_debug.frag.spv").string());
        if (debug_frag)
        {
            auto debug_vert = load_shader(device, (shader_dir / "megacity_scene.vert.spv").string());
            if (debug_vert)
            {
                VkPipelineShaderStageCreateInfo debug_stages[2] = {};
                debug_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                debug_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                debug_stages[0].module = debug_vert;
                debug_stages[0].pName = "main";
                debug_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                debug_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                debug_stages[1].module = debug_frag;
                debug_stages[1].pName = "main";
                pipeline_ci.pStages = debug_stages;
                const VkResult debug_result = vkCreateGraphicsPipelines(
                    device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &debug_pipeline);
                vkDestroyShaderModule(device, debug_vert, nullptr);
                if (debug_result != VK_SUCCESS)
                    DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create debug pipeline");
            }
            vkDestroyShaderModule(device, debug_frag, nullptr);
        }

        return true;
    }

    bool ensure(const VkRenderContext& ctx)
    {
        const uint32_t frame_count = std::max(1u, ctx.buffered_frame_count());
        if (pipeline != VK_NULL_HANDLE
            && render_pass == ctx.render_pass()
            && buffered_frame_count == frame_count)
            return true;

        destroy();
        physical_device = ctx.physical_device();
        device = ctx.device();
        allocator = ctx.allocator();
        render_pass = ctx.render_pass();
        buffered_frame_count = frame_count;

        return create_device_resources(frame_count) && create_pipeline();
    }

    void destroy_gbuffer_targets()
    {
        for (auto& t : gbuffer_targets)
        {
            if (t.imgui_normal_ds != VK_NULL_HANDLE)
                ImGui_ImplVulkan_RemoveTexture(t.imgui_normal_ds);
            if (t.imgui_ao_raw_ds != VK_NULL_HANDLE)
                ImGui_ImplVulkan_RemoveTexture(t.imgui_ao_raw_ds);
            if (t.imgui_ao_ds != VK_NULL_HANDLE)
                ImGui_ImplVulkan_RemoveTexture(t.imgui_ao_ds);
            if (t.imgui_depth_ds != VK_NULL_HANDLE)
                ImGui_ImplVulkan_RemoveTexture(t.imgui_depth_ds);
            if (t.framebuffer != VK_NULL_HANDLE)
                vkDestroyFramebuffer(device, t.framebuffer, nullptr);
            if (t.ao_raw_framebuffer != VK_NULL_HANDLE)
                vkDestroyFramebuffer(device, t.ao_raw_framebuffer, nullptr);
            if (t.ao_framebuffer != VK_NULL_HANDLE)
                vkDestroyFramebuffer(device, t.ao_framebuffer, nullptr);
            if (t.normal_view != VK_NULL_HANDLE)
                vkDestroyImageView(device, t.normal_view, nullptr);
            if (t.normal_image != VK_NULL_HANDLE)
                vmaDestroyImage(allocator, t.normal_image, t.normal_alloc);
            if (t.ao_raw_view != VK_NULL_HANDLE)
                vkDestroyImageView(device, t.ao_raw_view, nullptr);
            if (t.ao_raw_image != VK_NULL_HANDLE)
                vmaDestroyImage(allocator, t.ao_raw_image, t.ao_raw_alloc);
            if (t.ao_view != VK_NULL_HANDLE)
                vkDestroyImageView(device, t.ao_view, nullptr);
            if (t.ao_image != VK_NULL_HANDLE)
                vmaDestroyImage(allocator, t.ao_image, t.ao_alloc);
            if (t.depth_view != VK_NULL_HANDLE)
                vkDestroyImageView(device, t.depth_view, nullptr);
            if (t.depth_image != VK_NULL_HANDLE)
                vmaDestroyImage(allocator, t.depth_image, t.depth_alloc);
        }
        gbuffer_targets.clear();
    }

    void destroy_gbuffer()
    {
        if (device == VK_NULL_HANDLE)
            return;
        destroy_gbuffer_targets();
        if (gbuffer_sampler != VK_NULL_HANDLE)
            vkDestroySampler(device, gbuffer_sampler, nullptr);
        if (gbuffer_point_sampler != VK_NULL_HANDLE)
            vkDestroySampler(device, gbuffer_point_sampler, nullptr);
        if (ao_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, ao_pipeline, nullptr);
        if (ao_blur_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, ao_blur_pipeline, nullptr);
        if (ao_render_pass != VK_NULL_HANDLE)
            vkDestroyRenderPass(device, ao_render_pass, nullptr);
        if (gbuffer_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, gbuffer_pipeline, nullptr);
        if (gbuffer_render_pass != VK_NULL_HANDLE)
            vkDestroyRenderPass(device, gbuffer_render_pass, nullptr);
        gbuffer_sampler = VK_NULL_HANDLE;
        gbuffer_point_sampler = VK_NULL_HANDLE;
        ao_pipeline = VK_NULL_HANDLE;
        ao_blur_pipeline = VK_NULL_HANDLE;
        ao_render_pass = VK_NULL_HANDLE;
        gbuffer_pipeline = VK_NULL_HANDLE;
        gbuffer_render_pass = VK_NULL_HANDLE;
        gbuffer_initialized = false;
    }

    bool init_gbuffer()
    {
        if (gbuffer_initialized)
            return gbuffer_pipeline != VK_NULL_HANDLE
                && ao_pipeline != VK_NULL_HANDLE
                && ao_blur_pipeline != VK_NULL_HANDLE
                && gbuffer_sampler != VK_NULL_HANDLE
                && gbuffer_point_sampler != VK_NULL_HANDLE;
        gbuffer_initialized = true;

        // Create GBuffer render pass: 1 color + 1 depth attachment
        VkAttachmentDescription attachments[2] = {};
        // Attachment 0: normal (RGBA8Unorm — RG octahedral normal, BA reserved)
        attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        // Attachment 1: depth (D32Float)
        attachments[1].format = VK_FORMAT_D32_SFLOAT;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference color_ref = {};
        color_ref.attachment = 0;
        color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depth_ref = {};
        depth_ref.attachment = 1;
        depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_ref;
        subpass.pDepthStencilAttachment = &depth_ref;

        VkSubpassDependency deps[2] = {};
        // External → subpass: wait for previous frame reads to finish
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
            | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].srcAccessMask = 0;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
            | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        // Subpass → external: make writes visible for later shader reads
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
            | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
            | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rp_ci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        rp_ci.attachmentCount = 2;
        rp_ci.pAttachments = attachments;
        rp_ci.subpassCount = 1;
        rp_ci.pSubpasses = &subpass;
        rp_ci.dependencyCount = 2;
        rp_ci.pDependencies = deps;

        if (vkCreateRenderPass(device, &rp_ci, nullptr, &gbuffer_render_pass) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create GBuffer render pass");
            return false;
        }

        VkPushConstantRange prepass_push_range = {};
        prepass_push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        prepass_push_range.offset = 0;
        prepass_push_range.size = sizeof(ObjectPushConstants);

        VkPipelineLayoutCreateInfo prepass_layout_ci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        prepass_layout_ci.setLayoutCount = 1;
        prepass_layout_ci.pSetLayouts = &prepass_descriptor_set_layout;
        prepass_layout_ci.pushConstantRangeCount = 1;
        prepass_layout_ci.pPushConstantRanges = &prepass_push_range;
        if (vkCreatePipelineLayout(device, &prepass_layout_ci, nullptr, &prepass_pipeline_layout) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create prepass pipeline layout");
            return false;
        }

        // Load GBuffer shaders
        const auto shader_dir = bundled_asset_path("shaders");
        auto vert = load_shader(device, (shader_dir / "megacity_gbuffer.vert.spv").string());
        auto frag = load_shader(device, (shader_dir / "megacity_gbuffer.frag.spv").string());
        if (!vert || !frag)
        {
            if (vert)
                vkDestroyShaderModule(device, vert, nullptr);
            if (frag)
                vkDestroyShaderModule(device, frag, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";

        // Same vertex layout as the scene pipeline
        VkVertexInputBindingDescription binding = {};
        binding.stride = sizeof(SceneVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attributes[6] = {};
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(SceneVertex, position);
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(SceneVertex, normal);
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[2].offset = offsetof(SceneVertex, color);
        attributes[3].location = 3;
        attributes[3].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[3].offset = offsetof(SceneVertex, uv);
        attributes[4].location = 4;
        attributes[4].format = VK_FORMAT_R32_SFLOAT;
        attributes[4].offset = offsetof(SceneVertex, tex_blend);
        attributes[5].location = 5;
        attributes[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[5].offset = offsetof(SceneVertex, tangent);

        VkPipelineVertexInputStateCreateInfo vertex_input = {
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = 6;
        vertex_input.pVertexAttributeDescriptions = attributes;

        VkPipelineInputAssemblyStateCreateInfo input_assembly = {
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
        };
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport_state = {
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
        };
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster = {
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
        };
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample = {
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
        };
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth_stencil = {
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
        };
        depth_stencil.depthTestEnable = VK_TRUE;
        depth_stencil.depthWriteEnable = VK_TRUE;
        depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        // One color blend attachment (no blending, just write all channels)
        VkPipelineColorBlendAttachmentState blend_attachments[1] = {};
        const VkColorComponentFlags rgba_mask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blend_attachments[0].colorWriteMask = rgba_mask;

        VkPipelineColorBlendStateCreateInfo blend = {
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
        };
        blend.attachmentCount = 1;
        blend.pAttachments = blend_attachments;

        VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic = {
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
        };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;

        VkGraphicsPipelineCreateInfo pipeline_ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        pipeline_ci.stageCount = 2;
        pipeline_ci.pStages = stages;
        pipeline_ci.pVertexInputState = &vertex_input;
        pipeline_ci.pInputAssemblyState = &input_assembly;
        pipeline_ci.pViewportState = &viewport_state;
        pipeline_ci.pRasterizationState = &raster;
        pipeline_ci.pMultisampleState = &multisample;
        pipeline_ci.pDepthStencilState = &depth_stencil;
        pipeline_ci.pColorBlendState = &blend;
        pipeline_ci.pDynamicState = &dynamic;
        pipeline_ci.layout = pipeline_layout;
        pipeline_ci.renderPass = gbuffer_render_pass;
        pipeline_ci.subpass = 0;

        const VkResult result = vkCreateGraphicsPipelines(
            device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &gbuffer_pipeline);
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        if (result != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create GBuffer pipeline");
            return false;
        }

        // Create sampler for debug visualization
        VkSamplerCreateInfo sampler_ci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        sampler_ci.magFilter = VK_FILTER_LINEAR;
        sampler_ci.minFilter = VK_FILTER_LINEAR;
        sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_ci.maxLod = 1.0f;
        if (vkCreateSampler(device, &sampler_ci, nullptr, &gbuffer_sampler) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create GBuffer sampler");
            return false;
        }

        VkSamplerCreateInfo point_sampler_ci = sampler_ci;
        point_sampler_ci.magFilter = VK_FILTER_NEAREST;
        point_sampler_ci.minFilter = VK_FILTER_NEAREST;
        if (vkCreateSampler(device, &point_sampler_ci, nullptr, &gbuffer_point_sampler) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create point-sampled GBuffer sampler");
            return false;
        }

        VkAttachmentDescription ao_attachment = {};
        ao_attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
        ao_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        ao_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        ao_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        ao_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        ao_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        ao_attachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ao_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference ao_color_ref = {};
        ao_color_ref.attachment = 0;
        ao_color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription ao_subpass = {};
        ao_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        ao_subpass.colorAttachmentCount = 1;
        ao_subpass.pColorAttachments = &ao_color_ref;

        VkSubpassDependency ao_deps[2] = {};
        ao_deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        ao_deps[0].dstSubpass = 0;
        ao_deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        ao_deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        ao_deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        ao_deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        ao_deps[1].srcSubpass = 0;
        ao_deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        ao_deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        ao_deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        ao_deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        ao_deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo ao_rp_ci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        ao_rp_ci.attachmentCount = 1;
        ao_rp_ci.pAttachments = &ao_attachment;
        ao_rp_ci.subpassCount = 1;
        ao_rp_ci.pSubpasses = &ao_subpass;
        ao_rp_ci.dependencyCount = 2;
        ao_rp_ci.pDependencies = ao_deps;
        if (vkCreateRenderPass(device, &ao_rp_ci, nullptr, &ao_render_pass) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create AO render pass");
            return false;
        }

        auto ao_vert = load_shader(device, (shader_dir / "megacity_ao.vert.spv").string());
        auto ao_frag = load_shader(device, (shader_dir / "megacity_ao.frag.spv").string());
        auto ao_blur_frag = load_shader(device, (shader_dir / "megacity_ao_blur.frag.spv").string());
        if (!ao_vert || !ao_frag || !ao_blur_frag)
        {
            if (ao_vert)
                vkDestroyShaderModule(device, ao_vert, nullptr);
            if (ao_frag)
                vkDestroyShaderModule(device, ao_frag, nullptr);
            if (ao_blur_frag)
                vkDestroyShaderModule(device, ao_blur_frag, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo ao_stages[2] = {};
        ao_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ao_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        ao_stages[0].module = ao_vert;
        ao_stages[0].pName = "main";
        ao_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ao_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        ao_stages[1].module = ao_frag;
        ao_stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo ao_vertex_input = {
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };

        VkPipelineInputAssemblyStateCreateInfo ao_input_assembly = {
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
        };
        ao_input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo ao_viewport_state = {
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
        };
        ao_viewport_state.viewportCount = 1;
        ao_viewport_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo ao_raster = {
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
        };
        ao_raster.polygonMode = VK_POLYGON_MODE_FILL;
        ao_raster.cullMode = VK_CULL_MODE_NONE;
        ao_raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        ao_raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo ao_multisample = {
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
        };
        ao_multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState ao_blend_attachment = {};
        ao_blend_attachment.colorWriteMask = rgba_mask;

        VkPipelineColorBlendStateCreateInfo ao_blend = {
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
        };
        ao_blend.attachmentCount = 1;
        ao_blend.pAttachments = &ao_blend_attachment;

        VkPipelineDynamicStateCreateInfo ao_dynamic = {
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
        };
        ao_dynamic.dynamicStateCount = 2;
        ao_dynamic.pDynamicStates = dynamic_states;

        VkGraphicsPipelineCreateInfo ao_pipeline_ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        ao_pipeline_ci.stageCount = 2;
        ao_pipeline_ci.pStages = ao_stages;
        ao_pipeline_ci.pVertexInputState = &ao_vertex_input;
        ao_pipeline_ci.pInputAssemblyState = &ao_input_assembly;
        ao_pipeline_ci.pViewportState = &ao_viewport_state;
        ao_pipeline_ci.pRasterizationState = &ao_raster;
        ao_pipeline_ci.pMultisampleState = &ao_multisample;
        ao_pipeline_ci.pColorBlendState = &ao_blend;
        ao_pipeline_ci.pDynamicState = &ao_dynamic;
        ao_pipeline_ci.layout = prepass_pipeline_layout;
        ao_pipeline_ci.renderPass = ao_render_pass;
        ao_pipeline_ci.subpass = 0;

        const VkResult ao_result = vkCreateGraphicsPipelines(
            device, VK_NULL_HANDLE, 1, &ao_pipeline_ci, nullptr, &ao_pipeline);
        if (ao_result != VK_SUCCESS)
        {
            vkDestroyShaderModule(device, ao_vert, nullptr);
            vkDestroyShaderModule(device, ao_frag, nullptr);
            vkDestroyShaderModule(device, ao_blur_frag, nullptr);
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create AO pipeline");
            return false;
        }

        VkPipelineShaderStageCreateInfo blur_stages[2] = {};
        blur_stages[0] = ao_stages[0];
        blur_stages[1] = ao_stages[1];
        blur_stages[1].module = ao_blur_frag;

        VkGraphicsPipelineCreateInfo blur_pipeline_ci = ao_pipeline_ci;
        blur_pipeline_ci.pStages = blur_stages;

        const VkResult blur_result = vkCreateGraphicsPipelines(
            device, VK_NULL_HANDLE, 1, &blur_pipeline_ci, nullptr, &ao_blur_pipeline);

        vkDestroyShaderModule(device, ao_vert, nullptr);
        vkDestroyShaderModule(device, ao_frag, nullptr);
        vkDestroyShaderModule(device, ao_blur_frag, nullptr);
        if (blur_result != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create AO blur pipeline");
            return false;
        }

        DRAXUL_LOG_INFO(LogCategory::Renderer, "MegaCity: GBuffer pipeline initialized");
        return true;
    }

    bool ensure_gbuffer_targets(uint32_t frame_count, int width, int height)
    {
        if (width <= 0 || height <= 0)
            return false;

        frame_count = std::max(1u, frame_count);
        if (gbuffer_targets.size() == frame_count
            && !gbuffer_targets.empty()
            && gbuffer_targets[0].width == width
            && gbuffer_targets[0].height == height)
            return true;

        destroy_gbuffer_targets();
        gbuffer_targets.resize(frame_count);

        VmaAllocationCreateInfo alloc_ci = {};
        alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        for (auto& t : gbuffer_targets)
        {
            // Normal image (RGBA8Unorm — RG octahedral normal, BA reserved)
            VkImageCreateInfo img_ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            img_ci.imageType = VK_IMAGE_TYPE_2D;
            img_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
            img_ci.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
            img_ci.mipLevels = 1;
            img_ci.arrayLayers = 1;
            img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
            img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
            img_ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            if (vmaCreateImage(allocator, &img_ci, &alloc_ci,
                    &t.normal_image, &t.normal_alloc, nullptr)
                != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create GBuffer normal image");
                destroy_gbuffer_targets();
                return false;
            }

            VkImageViewCreateInfo view_ci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            view_ci.image = t.normal_image;
            view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
            view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            view_ci.subresourceRange.levelCount = 1;
            view_ci.subresourceRange.layerCount = 1;
            if (vkCreateImageView(device, &view_ci, nullptr, &t.normal_view) != VK_SUCCESS)
            {
                destroy_gbuffer_targets();
                return false;
            }

            // Raw AO image (RGBA8Unorm — R ambient occlusion before denoise)
            if (vmaCreateImage(allocator, &img_ci, &alloc_ci,
                    &t.ao_raw_image, &t.ao_raw_alloc, nullptr)
                != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create raw AO image");
                destroy_gbuffer_targets();
                return false;
            }

            view_ci.image = t.ao_raw_image;
            if (vkCreateImageView(device, &view_ci, nullptr, &t.ao_raw_view) != VK_SUCCESS)
            {
                destroy_gbuffer_targets();
                return false;
            }

            // AO image (RGBA8Unorm — R ambient occlusion, GBA reserved)
            if (vmaCreateImage(allocator, &img_ci, &alloc_ci,
                    &t.ao_image, &t.ao_alloc, nullptr)
                != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create GBuffer AO image");
                destroy_gbuffer_targets();
                return false;
            }

            view_ci.image = t.ao_image;
            if (vkCreateImageView(device, &view_ci, nullptr, &t.ao_view) != VK_SUCCESS)
            {
                destroy_gbuffer_targets();
                return false;
            }

            // Depth image (D32Float)
            img_ci.format = VK_FORMAT_D32_SFLOAT;
            img_ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            if (vmaCreateImage(allocator, &img_ci, &alloc_ci,
                    &t.depth_image, &t.depth_alloc, nullptr)
                != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create GBuffer depth image");
                destroy_gbuffer_targets();
                return false;
            }

            view_ci.image = t.depth_image;
            view_ci.format = VK_FORMAT_D32_SFLOAT;
            view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (vkCreateImageView(device, &view_ci, nullptr, &t.depth_view) != VK_SUCCESS)
            {
                destroy_gbuffer_targets();
                return false;
            }

            // Framebuffer
            VkImageView fb_views[] = { t.normal_view, t.depth_view };
            VkFramebufferCreateInfo fb_ci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            fb_ci.renderPass = gbuffer_render_pass;
            fb_ci.attachmentCount = 2;
            fb_ci.pAttachments = fb_views;
            fb_ci.width = static_cast<uint32_t>(width);
            fb_ci.height = static_cast<uint32_t>(height);
            fb_ci.layers = 1;
            if (vkCreateFramebuffer(device, &fb_ci, nullptr, &t.framebuffer) != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create GBuffer framebuffer");
                destroy_gbuffer_targets();
                return false;
            }

            VkFramebufferCreateInfo ao_fb_ci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            ao_fb_ci.renderPass = ao_render_pass;
            ao_fb_ci.attachmentCount = 1;
            ao_fb_ci.width = static_cast<uint32_t>(width);
            ao_fb_ci.height = static_cast<uint32_t>(height);
            ao_fb_ci.layers = 1;

            VkImageView ao_raw_fb_views[] = { t.ao_raw_view };
            ao_fb_ci.pAttachments = ao_raw_fb_views;
            if (vkCreateFramebuffer(device, &ao_fb_ci, nullptr, &t.ao_raw_framebuffer) != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create raw AO framebuffer");
                destroy_gbuffer_targets();
                return false;
            }

            VkImageView ao_fb_views[] = { t.ao_view };
            ao_fb_ci.pAttachments = ao_fb_views;
            if (vkCreateFramebuffer(device, &ao_fb_ci, nullptr, &t.ao_framebuffer) != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create AO framebuffer");
                destroy_gbuffer_targets();
                return false;
            }

            t.width = width;
            t.height = height;
        }
        refresh_gbuffer_descriptors();
        refresh_prepass_descriptors();
        return true;
    }

    void destroy()
    {
        if (allocator != VK_NULL_HANDLE)
        {
            destroy_mesh(allocator, cube_mesh);
            destroy_mesh(allocator, floor_mesh);
            destroy_mesh(allocator, tree_bark_mesh);
            destroy_mesh(allocator, tree_leaf_mesh);
            destroy_mesh(allocator, road_surface_mesh);
            destroy_mesh(allocator, roof_sign_mesh);
            destroy_mesh(allocator, wall_sign_mesh);
            destroy_image(device, allocator, label_atlas);
            for (auto& texture : material_textures)
                destroy_image(device, allocator, texture);
            destroy_buffer(allocator, label_staging);
            destroy_buffer(allocator, material_staging);
            for (auto& frame : frame_resources)
            {
                destroy_buffer(allocator, frame.geometry_arena.vertices.buffer);
                destroy_buffer(allocator, frame.geometry_arena.indices.buffer);
                destroy_buffer(allocator, frame.frame_uniforms);
                destroy_buffer(allocator, frame.material_uniforms);
            }
        }
        if (debug_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, debug_pipeline, nullptr);
        if (pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, pipeline, nullptr);
        if (pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        if (prepass_pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, prepass_pipeline_layout, nullptr);
        if (descriptor_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (prepass_descriptor_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, prepass_descriptor_pool, nullptr);
        if (descriptor_set_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
        if (prepass_descriptor_set_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, prepass_descriptor_set_layout, nullptr);

        device = VK_NULL_HANDLE;
        allocator = VK_NULL_HANDLE;
        render_pass = VK_NULL_HANDLE;
        descriptor_set_layout = VK_NULL_HANDLE;
        descriptor_pool = VK_NULL_HANDLE;
        pipeline_layout = VK_NULL_HANDLE;
        prepass_descriptor_set_layout = VK_NULL_HANDLE;
        prepass_descriptor_pool = VK_NULL_HANDLE;
        prepass_pipeline_layout = VK_NULL_HANDLE;
        pipeline = VK_NULL_HANDLE;
        frame_resources.clear();
        buffered_frame_count = 1;
        cached_grid_mesh = {};
        cached_grid_spec = {};
        has_cached_grid_mesh = false;
    }
};

IsometricScenePass::IsometricScenePass(int grid_width, int grid_height, float tile_size)
    : grid_width_(grid_width)
    , grid_height_(grid_height)
    , tile_size_(tile_size)
    , state_(std::make_unique<State>())
{
}

IsometricScenePass::~IsometricScenePass()
{
    state_->destroy_gbuffer();
    state_->destroy();
}

void IsometricScenePass::record_prepass(IRenderContext& ctx)
{
    auto* vk_ctx = static_cast<VkRenderContext*>(&ctx);
    auto cmd = static_cast<VkCommandBuffer>(ctx.native_command_buffer());
    if (!cmd)
        return;
    if (!state_->ensure(*vk_ctx))
        return;

    const int vw = ctx.viewport_w();
    const int vh = ctx.viewport_h();
    if (vw <= 0 || vh <= 0)
        return;

    const uint32_t frame_index = vk_ctx->frame_index();
    const uint32_t frame_count = std::max(1u, vk_ctx->buffered_frame_count());

    if (!state_->init_gbuffer())
        return;
    if (!state_->ensure_gbuffer_targets(frame_count, vw, vh))
        return;
    if (frame_index >= state_->gbuffer_targets.size())
        return;
    if (frame_index >= state_->frame_resources.size())
        return;

    state_->last_prepass_frame = frame_index;
    auto& gbuffer = state_->gbuffer_targets[frame_index];
    auto& frame_res = state_->frame_resources[frame_index];
    frame_res.geometry_arena.reset();

    // Ensure floor grid mesh
    if (!state_->ensure_floor_grid(scene_.floor_grid))
        return;
    if (!state_->ensure_tree_mesh(scene_.tree_bark_mesh, scene_.tree_leaf_mesh))
        return;
    MeshSlice grid_slice;
    if (state_->has_cached_grid_mesh
        && !stream_transient_mesh(vk_ctx->allocator(), state_->cached_grid_mesh,
            frame_res.geometry_arena, grid_slice))
    {
        return;
    }

    // Update frame uniforms
    FrameUniforms frame;
    frame.view = scene_.camera.view;
    frame.proj = make_vulkan_projection(scene_.camera.proj);
    frame.inv_view_proj = glm::inverse(frame.proj * frame.view);
    frame.camera_pos = scene_.camera.camera_pos;
    frame.light_dir = scene_.camera.light_dir;
    frame.point_light_pos = scene_.camera.point_light_pos;
    frame.label_fade_px = scene_.camera.label_fade_px;
    frame.render_tuning = scene_.camera.render_tuning;
    frame.screen_params = glm::vec4(0.0f, 0.0f, 1.0f / std::max(vw, 1), 1.0f / std::max(vh, 1));
    frame.ao_params = glm::vec4(
        scene_.camera.ao_settings.x,
        compute_ao_radius_pixels(scene_.camera.proj, scene_.camera.ao_settings.x, vh),
        scene_.camera.ao_settings.y,
        scene_.camera.ao_settings.z);
    frame.debug_view = scene_.camera.debug_view;
    frame.world_debug_bounds = scene_.camera.world_debug_bounds;
    std::memcpy(frame_res.frame_uniforms.mapped, &frame, sizeof(frame));
    vmaFlushAllocation(vk_ctx->allocator(), frame_res.frame_uniforms.allocation, 0, sizeof(frame));
    const MaterialUniforms material_uniforms = build_material_uniforms(scene_);
    std::memcpy(frame_res.material_uniforms.mapped, &material_uniforms, sizeof(material_uniforms));
    vmaFlushAllocation(vk_ctx->allocator(), frame_res.material_uniforms.allocation, 0, sizeof(material_uniforms));

    // Begin GBuffer render pass
    VkClearValue clear_values[2] = {};
    clear_values[0].color = { { 0.5f, 0.5f, 0.0f, 1.0f } }; // normal
    clear_values[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpbi.renderPass = state_->gbuffer_render_pass;
    rpbi.framebuffer = gbuffer.framebuffer;
    rpbi.renderArea.extent = { static_cast<uint32_t>(vw), static_cast<uint32_t>(vh) };
    rpbi.clearValueCount = 2;
    rpbi.pClearValues = clear_values;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.width = static_cast<float>(vw);
    viewport.height = static_cast<float>(vh);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.extent = { static_cast<uint32_t>(vw), static_cast<uint32_t>(vh) };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->gbuffer_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->pipeline_layout,
        0, 1, &frame_res.descriptor_set, 0, nullptr);

    // Draw scene objects into GBuffer
    const MeshBuffers* last_mesh = nullptr;
    for (const SceneObject& obj : scene_.objects)
    {
        const MeshBuffers* mesh = nullptr;
        switch (obj.mesh)
        {
        case MeshId::Floor:
            mesh = &state_->floor_mesh;
            break;
        case MeshId::Cube:
            mesh = &state_->cube_mesh;
            break;
        case MeshId::TreeBark:
            mesh = &state_->tree_bark_mesh;
            break;
        case MeshId::TreeLeaves:
            mesh = &state_->tree_leaf_mesh;
            break;
        case MeshId::RoadSurface:
            mesh = &state_->road_surface_mesh;
            break;
        case MeshId::RoofSign:
            mesh = &state_->roof_sign_mesh;
            break;
        case MeshId::WallSign:
            mesh = &state_->wall_sign_mesh;
            break;
        case MeshId::Grid:
            continue;
        }
        if (!mesh || mesh->index_count == 0)
            continue;

        if (mesh != last_mesh)
        {
            VkBuffer vertex_buffer = mesh->vertices.buffer;
            VkDeviceSize vertex_offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &vertex_offset);
            vkCmdBindIndexBuffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT16);
            last_mesh = mesh;
        }

        ObjectPushConstants push;
        push.world = obj.world;
        push.color = obj.color;
        push.material_data = glm::uvec4(obj.material_index, 0u, 0u, 0u);
        push.uv_rect = obj.uv_rect;
        push.label_metrics = glm::vec4(0.0f);
        vkCmdPushConstants(cmd, state_->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, mesh->index_count, 1, 0, 0, 0);
    }

    // Draw floor grid
    if (grid_slice.index_count > 0)
    {
        ObjectPushConstants push;
        push.world = glm::mat4(1.0f);
        push.color = scene_.floor_grid.color;
        push.material_data = glm::uvec4(0u);
        push.uv_rect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        push.label_metrics = glm::vec4(0.0f);

        VkBuffer vertex_buffer = grid_slice.vertex_buffer;
        VkDeviceSize vertex_offset = grid_slice.vertex_offset;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &vertex_offset);
        vkCmdBindIndexBuffer(cmd, grid_slice.index_buffer, grid_slice.index_offset, VK_INDEX_TYPE_UINT16);
        vkCmdPushConstants(cmd, state_->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, grid_slice.index_count, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmd);

    if (gbuffer.ao_raw_framebuffer == VK_NULL_HANDLE || gbuffer.ao_framebuffer == VK_NULL_HANDLE)
        return;

    VkClearValue ao_clear = {};
    ao_clear.color = { { 1.0f, 0.0f, 0.0f, 1.0f } };

    VkRenderPassBeginInfo ao_rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    ao_rpbi.renderPass = state_->ao_render_pass;
    ao_rpbi.framebuffer = gbuffer.ao_raw_framebuffer;
    ao_rpbi.renderArea.extent = { static_cast<uint32_t>(vw), static_cast<uint32_t>(vh) };
    ao_rpbi.clearValueCount = 1;
    ao_rpbi.pClearValues = &ao_clear;

    vkCmdBeginRenderPass(cmd, &ao_rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->ao_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->prepass_pipeline_layout,
        0, 1, &frame_res.prepass_descriptor_set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    ao_rpbi.framebuffer = gbuffer.ao_framebuffer;
    vkCmdBeginRenderPass(cmd, &ao_rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->ao_blur_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->prepass_pipeline_layout,
        0, 1, &frame_res.prepass_descriptor_set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);
}

void IsometricScenePass::record(IRenderContext& ctx)
{
    auto* vk_ctx = static_cast<VkRenderContext*>(&ctx);
    auto cmd = static_cast<VkCommandBuffer>(ctx.native_command_buffer());
    if (!cmd)
        return;

    const uint32_t frame_index = vk_ctx->frame_index();
    if (!state_->ensure(*vk_ctx))
        return;
    if (frame_index >= state_->frame_resources.size())
        return;

    auto& frame_resources = state_->frame_resources[frame_index];
    frame_resources.geometry_arena.reset();

    MeshSlice grid_slice;
    if (!state_->ensure_floor_grid(scene_.floor_grid))
        return;
    if (!state_->ensure_road_materials(*vk_ctx, cmd))
        return;
    if (!state_->ensure_tree_mesh(scene_.tree_bark_mesh, scene_.tree_leaf_mesh))
        return;
    if (!state_->ensure_label_atlas(*vk_ctx, cmd, scene_.label_atlas))
        return;
    if (state_->has_cached_grid_mesh
        && !stream_transient_mesh(vk_ctx->allocator(), state_->cached_grid_mesh,
            frame_resources.geometry_arena, grid_slice))
    {
        return;
    }

    FrameUniforms frame;
    frame.view = scene_.camera.view;
    frame.proj = make_vulkan_projection(scene_.camera.proj);
    frame.inv_view_proj = glm::inverse(frame.proj * frame.view);
    frame.camera_pos = scene_.camera.camera_pos;
    frame.light_dir = scene_.camera.light_dir;
    frame.point_light_pos = scene_.camera.point_light_pos;
    frame.label_fade_px = scene_.camera.label_fade_px;
    frame.render_tuning = scene_.camera.render_tuning;
    frame.screen_params = glm::vec4(
        static_cast<float>(ctx.viewport_x()),
        static_cast<float>(ctx.viewport_y()),
        1.0f / std::max(ctx.viewport_w(), 1),
        1.0f / std::max(ctx.viewport_h(), 1));
    frame.ao_params = glm::vec4(
        scene_.camera.ao_settings.x,
        compute_ao_radius_pixels(scene_.camera.proj, scene_.camera.ao_settings.x, ctx.viewport_h()),
        scene_.camera.ao_settings.y,
        scene_.camera.ao_settings.z);
    frame.debug_view = scene_.camera.debug_view;
    frame.world_debug_bounds = scene_.camera.world_debug_bounds;
    std::memcpy(frame_resources.frame_uniforms.mapped, &frame, sizeof(frame));
    vmaFlushAllocation(vk_ctx->allocator(), frame_resources.frame_uniforms.allocation, 0, sizeof(frame));
    const MaterialUniforms material_uniforms = build_material_uniforms(scene_);
    std::memcpy(frame_resources.material_uniforms.mapped, &material_uniforms, sizeof(material_uniforms));
    vmaFlushAllocation(vk_ctx->allocator(), frame_resources.material_uniforms.allocation, 0, sizeof(material_uniforms));

    const int debug_mode = static_cast<int>(scene_.camera.debug_view.x + 0.5f);
    const bool use_debug = debug_mode > 0 && state_->debug_pipeline != VK_NULL_HANDLE;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, use_debug ? state_->debug_pipeline : state_->pipeline);
    // For raw AO debug (mode 1), temporarily rebind the raw AO texture to the descriptor
    if (debug_mode == 1 && frame_index < state_->gbuffer_targets.size())
    {
        const auto& gbuffer = state_->gbuffer_targets[frame_index];
        if (gbuffer.ao_raw_view != VK_NULL_HANDLE)
        {
            VkDescriptorImageInfo raw_ao_info = {};
            raw_ao_info.sampler = state_->gbuffer_sampler;
            raw_ao_info.imageView = gbuffer.ao_raw_view;
            raw_ao_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = frame_resources.descriptor_set;
            write.dstBinding = 3;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &raw_ao_info;
            vkUpdateDescriptorSets(vk_ctx->device(), 1, &write, 0, nullptr);
        }
    }
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->pipeline_layout,
        0, 1, &frame_resources.descriptor_set, 0, nullptr);

    const MeshBuffers* last_mesh = nullptr;
    for (const SceneObject& obj : scene_.objects)
    {
        const MeshBuffers* mesh = nullptr;
        switch (obj.mesh)
        {
        case MeshId::Floor:
            mesh = &state_->floor_mesh;
            break;
        case MeshId::Cube:
            mesh = &state_->cube_mesh;
            break;
        case MeshId::TreeBark:
            mesh = &state_->tree_bark_mesh;
            break;
        case MeshId::TreeLeaves:
            mesh = &state_->tree_leaf_mesh;
            break;
        case MeshId::RoadSurface:
            mesh = &state_->road_surface_mesh;
            break;
        case MeshId::RoofSign:
            mesh = &state_->roof_sign_mesh;
            break;
        case MeshId::WallSign:
            mesh = &state_->wall_sign_mesh;
            break;
        case MeshId::Grid:
            continue;
        }
        if (!mesh || mesh->index_count == 0)
            continue;

        if (mesh != last_mesh)
        {
            VkBuffer vertex_buffer = mesh->vertices.buffer;
            VkDeviceSize vertex_offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &vertex_offset);
            vkCmdBindIndexBuffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT16);
            last_mesh = mesh;
        }

        ObjectPushConstants push;
        push.world = obj.world;
        push.color = obj.color;
        push.material_data = glm::uvec4(obj.material_index, 0u, 0u, 0u);
        push.uv_rect = obj.uv_rect;
        push.label_metrics = glm::vec4(obj.label_ink_pixel_size, 0.0f, 0.0f);
        vkCmdPushConstants(cmd, state_->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, mesh->index_count, 1, 0, 0, 0);
    }

    if (grid_slice.index_count > 0)
    {
        ObjectPushConstants push;
        push.world = glm::mat4(1.0f);
        push.color = scene_.floor_grid.color;
        push.material_data = glm::uvec4(0u);
        push.uv_rect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        push.label_metrics = glm::vec4(0.0f);

        VkBuffer vertex_buffer = grid_slice.vertex_buffer;
        VkDeviceSize vertex_offset = grid_slice.vertex_offset;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &vertex_offset);
        vkCmdBindIndexBuffer(cmd, grid_slice.index_buffer, grid_slice.index_offset, VK_INDEX_TYPE_UINT16);
        vkCmdPushConstants(cmd, state_->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, grid_slice.index_count, 1, 0, 0, 0);
    }

    // Restore denoised AO binding if we switched to raw for debug
    if (debug_mode == 1 && frame_index < state_->gbuffer_targets.size())
    {
        const auto& gbuffer = state_->gbuffer_targets[frame_index];
        if (gbuffer.ao_view != VK_NULL_HANDLE)
        {
            VkDescriptorImageInfo ao_info = {};
            ao_info.sampler = state_->gbuffer_sampler;
            ao_info.imageView = gbuffer.ao_view;
            ao_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = frame_resources.descriptor_set;
            write.dstBinding = 3;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &ao_info;
            vkUpdateDescriptorSets(vk_ctx->device(), 1, &write, 0, nullptr);
        }
    }
}

void IsometricScenePass::render_gbuffer_debug_ui()
{
    if (state_->gbuffer_targets.empty() || state_->gbuffer_sampler == VK_NULL_HANDLE)
        return;

    const uint32_t fi = state_->last_prepass_frame % static_cast<uint32_t>(state_->gbuffer_targets.size());
    auto& t = state_->gbuffer_targets[fi];
    if (t.normal_view == VK_NULL_HANDLE)
        return;

    // Lazily register GBuffer textures with ImGui Vulkan backend
    if (t.imgui_normal_ds == VK_NULL_HANDLE)
    {
        t.imgui_normal_ds = ImGui_ImplVulkan_AddTexture(
            state_->gbuffer_sampler, t.normal_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    if (t.imgui_ao_raw_ds == VK_NULL_HANDLE)
    {
        t.imgui_ao_raw_ds = ImGui_ImplVulkan_AddTexture(
            state_->gbuffer_sampler, t.ao_raw_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    if (t.imgui_ao_ds == VK_NULL_HANDLE)
    {
        t.imgui_ao_ds = ImGui_ImplVulkan_AddTexture(
            state_->gbuffer_sampler, t.ao_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    if (t.imgui_depth_ds == VK_NULL_HANDLE)
    {
        t.imgui_depth_ds = ImGui_ImplVulkan_AddTexture(
            state_->gbuffer_sampler, t.depth_view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
    }

    if (!ImGui::Begin("GBuffer Debug"))
    {
        ImGui::End();
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float aspect = t.height > 0 ? static_cast<float>(t.width) / static_cast<float>(t.height) : 1.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float text_h = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const float cell_w = std::max(64.0f, (avail.x - spacing) * 0.5f);
    const float cell_h = std::max(32.0f, (avail.y - text_h * 3.0f) * 0.5f);
    const float img_w = std::min(cell_w, cell_h * aspect);
    const float img_h = img_w / aspect;
    const ImVec2 size(img_w, img_h);

    if (ImGui::BeginTable("##gbuffer_grid", 2))
    {
        ImGui::TableNextColumn();
        ImGui::Text("Normals");
        ImGui::Image(static_cast<ImTextureID>(t.imgui_normal_ds), size);

        ImGui::TableNextColumn();
        ImGui::Text("Raw AO");
        ImGui::Image(static_cast<ImTextureID>(t.imgui_ao_raw_ds), size);

        ImGui::TableNextColumn();
        ImGui::Text("Depth");
        ImGui::Image(static_cast<ImTextureID>(t.imgui_depth_ds), size);

        ImGui::TableNextColumn();
        ImGui::Text("Ambient Occlusion");
        ImGui::Image(static_cast<ImTextureID>(t.imgui_ao_ds), size);

        ImGui::EndTable();
    }

    ImGui::Text("Size: %dx%d", t.width, t.height);
    ImGui::End();
}

} // namespace draxul
