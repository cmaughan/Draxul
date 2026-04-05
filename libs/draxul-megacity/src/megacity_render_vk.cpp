#include "isometric_scene_pass.h"

#include "megacity_material_assets.h"
#include "mesh_library.h"
#include "shadow_cascade.h"
#include "vk_render_context.h"
#include <algorithm>
#include <array>
#include <backends/imgui_impl_vulkan.h>
#include <cstring>
#include <draxul/log.h>
#include <draxul/perf_timing.h>
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
    glm::vec4 render_tuning{ 1.0f, 1.0f, 0.45f, 4.0f };
    glm::vec4 perf_tuning{ 0.0f };
    glm::vec4 screen_params{ 0.0f, 0.0f, 1.0f, 1.0f }; // x = viewport origin x, y = viewport origin y, z = 1 / viewport width, w = 1 / viewport height
    glm::vec4 ao_params{ 1.6f, 1.0f, 0.12f, 1.35f }; // x = radius world, y = radius pixels, z = bias, w = power
    glm::vec4 debug_view{ 0.0f, 1.0f, 0.0f, 0.0f }; // x = AO debug mode, y = AO denoise enabled
    glm::vec4 world_debug_bounds{ -5.0f, 5.0f, -5.0f, 5.0f }; // x = min x, y = max x, z = min z, w = max z
    std::array<glm::mat4, kShadowCascadeCount> shadow_view_proj{};
    std::array<glm::mat4, kShadowCascadeCount> shadow_texture_matrix{};
    glm::vec4 shadow_split_depths{ 1.0f };
    glm::vec4 shadow_params{ static_cast<float>(kShadowCascadeCount), 0.0015f, 0.04f, 1.0f / 2048.0f };
    std::array<glm::mat4, kPointShadowFaceCount> point_shadow_view_proj{};
    std::array<glm::mat4, kPointShadowFaceCount> point_shadow_texture_matrix{};
    glm::vec4 point_shadow_params{ 0.00075f, 0.004f, 1.0f / 1024.0f, 0.0f };
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
    bool mapped_by_vma_api = false;
};

struct MeshBuffers
{
    Buffer vertices;
    Buffer indices;
    uint32_t index_count = 0;
    uint32_t first_index = 0;
    int32_t vertex_offset = 0;
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
    Buffer performance_heat_values;
    TransientGeometryArena geometry_arena;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkDescriptorSet post_descriptor_set = VK_NULL_HANDLE;
    VkDescriptorSet prepass_descriptor_set = VK_NULL_HANDLE;
    VkDescriptorSet tooltip_descriptor_set = VK_NULL_HANDLE;
    ImageResource tooltip_image;
    Buffer tooltip_staging;
    VkImageLayout tooltip_image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint64_t tooltip_texture_revision = 0;
};

struct RetiredBufferResource
{
    Buffer buffer;
    uint32_t reclaim_frame_index = 0;
};

struct RetiredMeshResource
{
    MeshBuffers mesh;
    uint32_t reclaim_frame_index = 0;
};

struct RetiredImageResource
{
    ImageResource image;
    uint32_t reclaim_frame_index = 0;
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
    std::array<VkImage, kShadowCascadeCount> shadow_images{};
    std::array<VmaAllocation, kShadowCascadeCount> shadow_allocs{};
    std::array<VkImageView, kShadowCascadeCount> shadow_views{};
    std::array<VkFramebuffer, kShadowCascadeCount> shadow_framebuffers{};
    VkImage point_shadow_image = VK_NULL_HANDLE; // R32Float cubemap with normalized radial depth
    VmaAllocation point_shadow_alloc = VK_NULL_HANDLE;
    VkImageView point_shadow_cube_view = VK_NULL_HANDLE;
    std::array<VkImageView, kPointShadowFaceCount> point_shadow_face_views{};
    std::array<VkFramebuffer, kPointShadowFaceCount> point_shadow_framebuffers{};
    VkImage point_shadow_depth_image = VK_NULL_HANDLE;
    VmaAllocation point_shadow_depth_alloc = VK_NULL_HANDLE;
    VkImageView point_shadow_depth_view = VK_NULL_HANDLE;

    VkImage scene_color_msaa_image = VK_NULL_HANDLE; // RGBA16Float MSAA scene color
    VmaAllocation scene_color_msaa_alloc = VK_NULL_HANDLE;
    VkImageView scene_color_msaa_view = VK_NULL_HANDLE;

    VkImage scene_depth_msaa_image = VK_NULL_HANDLE; // D32Float MSAA scene depth
    VmaAllocation scene_depth_msaa_alloc = VK_NULL_HANDLE;
    VkImageView scene_depth_msaa_view = VK_NULL_HANDLE;

    VkImage scene_hdr_image = VK_NULL_HANDLE; // RGBA16Float resolved HDR scene
    VmaAllocation scene_hdr_alloc = VK_NULL_HANDLE;
    VkImageView scene_hdr_view = VK_NULL_HANDLE;

    VkImage scene_final_image = VK_NULL_HANDLE; // BGRA8 sRGB encoded scene
    VmaAllocation scene_final_alloc = VK_NULL_HANDLE;
    VkImageView scene_final_srgb_view = VK_NULL_HANDLE;
    VkImageView scene_final_unorm_view = VK_NULL_HANDLE;

    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkFramebuffer ao_raw_framebuffer = VK_NULL_HANDLE;
    VkFramebuffer ao_framebuffer = VK_NULL_HANDLE;
    VkFramebuffer scene_framebuffer = VK_NULL_HANDLE;
    VkFramebuffer scene_post_framebuffer = VK_NULL_HANDLE;

    // ImGui debug visualization descriptor sets (lazily registered)
    VkDescriptorSet imgui_normal_ds = VK_NULL_HANDLE;
    VkDescriptorSet imgui_ao_raw_ds = VK_NULL_HANDLE;
    VkDescriptorSet imgui_ao_ds = VK_NULL_HANDLE;
    VkDescriptorSet imgui_depth_ds = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, kShadowCascadeCount> imgui_shadow_ds{};
    std::array<VkDescriptorSet, kPointShadowFaceCount> imgui_point_shadow_ds{};
    VkDescriptorSet imgui_scene_hdr_ds = VK_NULL_HANDLE;
    VkDescriptorSet imgui_scene_final_ds = VK_NULL_HANDLE;

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
    PERF_MEASURE();
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
    if (buffer.mapped_by_vma_api && buffer.allocation != VK_NULL_HANDLE)
        vmaUnmapMemory(allocator, buffer.allocation);
    if (buffer.buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    buffer = {};
}

bool create_mapped_buffer(VmaAllocator allocator, size_t size, VkBufferUsageFlags usage, Buffer& buffer)
{
    PERF_MEASURE();
    VkBufferCreateInfo buf_ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buf_ci.size = size;
    buf_ci.usage = usage;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_info = {};
    if (vmaCreateBuffer(allocator, &buf_ci, &alloc_ci, &buffer.buffer, &buffer.allocation, &alloc_info) != VK_SUCCESS)
        return false;

    buffer.mapped = alloc_info.pMappedData;
    if (!buffer.mapped)
    {
        if (vmaMapMemory(allocator, buffer.allocation, &buffer.mapped) != VK_SUCCESS || !buffer.mapped)
        {
            vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
            buffer = {};
            DRAXUL_LOG_ERROR(LogCategory::Renderer,
                "MegaCity scene: failed to map CPU-visible Vulkan buffer (size=%zu, usage=0x%x)",
                size,
                static_cast<unsigned>(usage));
            return false;
        }
        buffer.mapped_by_vma_api = true;
    }
    buffer.size = size;
    return true;
}

bool upload_mesh(VmaAllocator allocator, const MeshData& mesh, MeshBuffers& gpu_mesh)
{
    PERF_MEASURE();
    if (mesh.vertices.empty() || mesh.indices.empty())
    {
        gpu_mesh = {};
        return true;
    }

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
    PERF_MEASURE();
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
    PERF_MEASURE();
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
    PERF_MEASURE();
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
    {
        vmaDestroyImage(allocator, image.image, image.allocation);
        image.image = VK_NULL_HANDLE;
        image.allocation = VK_NULL_HANDLE;
        return false;
    }

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
    {
        vkDestroyImageView(device, image.view, nullptr);
        image.view = VK_NULL_HANDLE;
        vmaDestroyImage(allocator, image.image, image.allocation);
        image.image = VK_NULL_HANDLE;
        image.allocation = VK_NULL_HANDLE;
        return false;
    }

    image.width = width;
    image.height = height;
    image.size = static_cast<VkDeviceSize>(width) * height * 4;
    image.mip_levels = mip_levels;
    return true;
}

bool create_label_image(VkPhysicalDevice physical_device, VkDevice device, VmaAllocator allocator,
    int width, int height, ImageResource& image)
{
    return create_sampled_image(
        physical_device,
        device,
        allocator,
        width,
        height,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        false,
        image);
}

VkSampleCountFlagBits choose_scene_sample_count(VkPhysicalDevice physical_device)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_device, &properties);
    const VkSampleCountFlags counts = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;
    if ((counts & VK_SAMPLE_COUNT_4_BIT) != 0)
        return VK_SAMPLE_COUNT_4_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

bool create_attachment_image(VkDevice device, VmaAllocator allocator, int width, int height,
    VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect_mask,
    VkSampleCountFlagBits samples, VkImage& image, VmaAllocation& allocation, VkImageView& view,
    VkImageCreateFlags flags = 0)
{
    PERF_MEASURE();
    VkImageCreateInfo image_ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_ci.flags = flags;
    image_ci.imageType = VK_IMAGE_TYPE_2D;
    image_ci.format = format;
    image_ci.extent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        1u
    };
    image_ci.mipLevels = 1;
    image_ci.arrayLayers = 1;
    image_ci.samples = samples;
    image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_ci.usage = usage;
    image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(allocator, &image_ci, &alloc_ci, &image, &allocation, nullptr) != VK_SUCCESS)
        return false;

    VkImageViewCreateInfo view_ci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view_ci.image = image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = format;
    view_ci.subresourceRange.aspectMask = aspect_mask;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &view_ci, nullptr, &view) != VK_SUCCESS)
        return false;

    return true;
}

bool create_attachment_view(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspect_mask,
    VkImageView& view)
{
    VkImageViewCreateInfo view_ci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view_ci.image = image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = format;
    view_ci.subresourceRange.aspectMask = aspect_mask;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;
    return vkCreateImageView(device, &view_ci, nullptr, &view) == VK_SUCCESS;
}

bool create_cube_attachment_image(
    VkDevice device,
    VmaAllocator allocator,
    int size,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImage& image,
    VmaAllocation& allocation,
    VkImageView& cube_view,
    std::array<VkImageView, kPointShadowFaceCount>& face_views)
{
    PERF_MEASURE();
    VkImageCreateInfo image_ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_ci.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    image_ci.imageType = VK_IMAGE_TYPE_2D;
    image_ci.format = format;
    image_ci.extent = {
        static_cast<uint32_t>(size),
        static_cast<uint32_t>(size),
        1u
    };
    image_ci.mipLevels = 1;
    image_ci.arrayLayers = kPointShadowFaceCount;
    image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_ci.usage = usage;
    image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(allocator, &image_ci, &alloc_ci, &image, &allocation, nullptr) != VK_SUCCESS)
        return false;

    VkImageViewCreateInfo cube_view_ci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    cube_view_ci.image = image;
    cube_view_ci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    cube_view_ci.format = format;
    cube_view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    cube_view_ci.subresourceRange.levelCount = 1;
    cube_view_ci.subresourceRange.layerCount = kPointShadowFaceCount;
    if (vkCreateImageView(device, &cube_view_ci, nullptr, &cube_view) != VK_SUCCESS)
        return false;

    for (uint32_t face_index = 0; face_index < kPointShadowFaceCount; ++face_index)
    {
        VkImageViewCreateInfo face_view_ci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        face_view_ci.image = image;
        face_view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        face_view_ci.format = format;
        face_view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        face_view_ci.subresourceRange.levelCount = 1;
        face_view_ci.subresourceRange.baseArrayLayer = face_index;
        face_view_ci.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &face_view_ci, nullptr, &face_views[face_index]) != VK_SUCCESS)
            return false;
    }

    return true;
}

void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout,
    uint32_t base_mip_level = 0, uint32_t level_count = 1)
{
    PERF_MEASURE();
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

bool upload_rgba_texture(VmaAllocator allocator, VkCommandBuffer cmd, Buffer& staging,
    size_t staging_offset, const LoadedTextureImage& image, ImageResource& target)
{
    PERF_MEASURE();
    const size_t bytes = static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4;
    const size_t required_bytes = staging_offset + bytes;
    if (!ensure_mapped_buffer_capacity(
            allocator, required_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, required_bytes))
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
        transition_image(cmd, target.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        return true;
    }
    return generate_mipmaps(cmd, target);
}

VkShaderModule load_shader(VkDevice device, const std::string& path)
{
    PERF_MEASURE();
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

glm::mat4 make_vulkan_shadow_texture_matrix(const glm::mat4& world_to_clip)
{
    glm::mat4 bias(1.0f);
    bias[0][0] = 0.5f;
    // Vulkan shadow clip space is already Y-flipped before rasterization.
    bias[1][1] = 0.5f;
    bias[3][0] = 0.5f;
    bias[3][1] = 0.5f;
    return bias * world_to_clip;
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
    VkDescriptorSetLayout post_descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool post_descriptor_pool = VK_NULL_HANDLE;
    VkPipelineLayout post_pipeline_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout prepass_descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool prepass_descriptor_pool = VK_NULL_HANDLE;
    VkPipelineLayout prepass_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipeline pipeline_no_depth_write = VK_NULL_HANDLE;
    VkPipeline debug_pipeline = VK_NULL_HANDLE;
    VkPipeline wireframe_pipeline = VK_NULL_HANDLE;
    VkPipeline debug_wireframe_pipeline = VK_NULL_HANDLE;
    VkPipeline post_pipeline = VK_NULL_HANDLE;
    VkPipeline present_pipeline = VK_NULL_HANDLE;
    MeshBuffers cube_mesh;
    MeshBuffers floor_mesh;
    MeshBuffers tree_bark_mesh;
    MeshBuffers tree_leaf_mesh;
    MeshBuffers road_surface_mesh;
    MeshBuffers roof_sign_mesh;
    MeshBuffers wall_sign_mesh;
    const MeshData* tree_bark_mesh_source = nullptr;
    const MeshData* tree_leaf_mesh_source = nullptr;
    std::vector<MeshBuffers> custom_meshes;
    Buffer custom_vertex_pool;
    Buffer custom_index_pool;
    MeshData cached_grid_mesh;
    FloorGridSpec cached_grid_spec;
    bool has_cached_grid_mesh = false;
    ImageResource label_atlas;
    Buffer label_staging;
    Buffer material_staging;
    VkImageLayout label_atlas_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint64_t label_atlas_revision = 0;
    std::vector<FrameResources> frame_resources;
    std::vector<bool> label_descriptor_dirty;
    std::vector<RetiredBufferResource> retired_buffers;
    std::vector<RetiredMeshResource> retired_meshes;
    std::vector<RetiredImageResource> retired_images;
    uint32_t buffered_frame_count = 1;

    // GBuffer pre-pass resources
    VkRenderPass gbuffer_render_pass = VK_NULL_HANDLE;
    VkPipeline gbuffer_pipeline = VK_NULL_HANDLE;
    VkRenderPass shadow_render_pass = VK_NULL_HANDLE;
    VkPipeline shadow_pipeline = VK_NULL_HANDLE;
    VkRenderPass point_shadow_render_pass = VK_NULL_HANDLE;
    VkPipeline point_shadow_pipeline = VK_NULL_HANDLE;
    VkRenderPass ao_render_pass = VK_NULL_HANDLE;
    VkPipeline ao_pipeline = VK_NULL_HANDLE;
    VkPipeline ao_blur_pipeline = VK_NULL_HANDLE;
    VkRenderPass scene_render_pass = VK_NULL_HANDLE;
    VkRenderPass scene_post_render_pass = VK_NULL_HANDLE;
    VkSampler gbuffer_sampler = VK_NULL_HANDLE;
    VkSampler gbuffer_point_sampler = VK_NULL_HANDLE;
    std::array<ImageResource, kSceneMaterialTextureCount> material_textures;
    std::vector<GBufferTargets> gbuffer_targets;
    bool gbuffer_initialized = false;
    uint32_t last_prepass_frame = 0;
    VkSampleCountFlagBits scene_sample_count = VK_SAMPLE_COUNT_1_BIT;
    int shadow_map_resolution = 2048;
    int point_shadow_map_resolution = 1024;

    // Tooltip overlay resources
    VkDescriptorSetLayout tooltip_descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool tooltip_descriptor_pool = VK_NULL_HANDLE;
    VkPipelineLayout tooltip_pipeline_layout = VK_NULL_HANDLE;
    VkPipeline tooltip_pipeline = VK_NULL_HANDLE;
    VkSampler tooltip_sampler = VK_NULL_HANDLE;
    bool tooltip_initialized = false;

    void wait_for_device_idle() const
    {
        if (device == VK_NULL_HANDLE)
            return;

        const VkResult result = vkDeviceWaitIdle(device);
        if (result != VK_SUCCESS)
        {
            DRAXUL_LOG_WARN(
                LogCategory::Renderer,
                "MegaCity: vkDeviceWaitIdle before resource teardown returned %d",
                static_cast<int>(result));
        }
    }

    static Buffer take_buffer(Buffer& buffer)
    {
        Buffer taken = buffer;
        buffer = {};
        return taken;
    }

    static MeshBuffers take_mesh(MeshBuffers& mesh)
    {
        MeshBuffers taken = mesh;
        mesh = {};
        return taken;
    }

    static ImageResource take_image(ImageResource& image)
    {
        ImageResource taken = image;
        image = {};
        return taken;
    }

    uint32_t reclaim_frame_index(uint32_t current_frame_index) const
    {
        const uint32_t frame_count = std::max(buffered_frame_count, 1u);
        return (current_frame_index + frame_count - 1u) % frame_count;
    }

    void retire_buffer(Buffer& buffer, uint32_t current_frame_index)
    {
        if (buffer.buffer == VK_NULL_HANDLE)
        {
            buffer = {};
            return;
        }
        retired_buffers.push_back({ take_buffer(buffer), reclaim_frame_index(current_frame_index) });
    }

    void retire_mesh(MeshBuffers& mesh, uint32_t current_frame_index)
    {
        if (mesh.vertices.buffer == VK_NULL_HANDLE && mesh.indices.buffer == VK_NULL_HANDLE)
        {
            mesh = {};
            return;
        }
        retired_meshes.push_back({ take_mesh(mesh), reclaim_frame_index(current_frame_index) });
    }

    void retire_image(ImageResource& image, uint32_t current_frame_index)
    {
        if (image.image == VK_NULL_HANDLE && image.view == VK_NULL_HANDLE && image.sampler == VK_NULL_HANDLE)
        {
            image = {};
            return;
        }
        retired_images.push_back({ take_image(image), reclaim_frame_index(current_frame_index) });
    }

    void reclaim_retired_resources(uint32_t current_frame_index)
    {
        if (allocator == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
            return;

        retired_buffers.erase(std::remove_if(retired_buffers.begin(), retired_buffers.end(),
                                 [&](RetiredBufferResource& retired) {
                                     if (retired.reclaim_frame_index != current_frame_index)
                                         return false;
                                     destroy_buffer(allocator, retired.buffer);
                                     return true;
                                 }),
            retired_buffers.end());

        retired_meshes.erase(std::remove_if(retired_meshes.begin(), retired_meshes.end(),
                                [&](RetiredMeshResource& retired) {
                                    if (retired.reclaim_frame_index != current_frame_index)
                                        return false;
                                    destroy_mesh(allocator, retired.mesh);
                                    return true;
                                }),
            retired_meshes.end());

        retired_images.erase(std::remove_if(retired_images.begin(), retired_images.end(),
                                [&](RetiredImageResource& retired) {
                                    if (retired.reclaim_frame_index != current_frame_index)
                                        return false;
                                    destroy_image(device, allocator, retired.image);
                                    return true;
                                }),
            retired_images.end());
    }

    bool ensure_retired_mapped_buffer_capacity(
        size_t required_size, VkBufferUsageFlags usage, Buffer& buffer, size_t minimum_size, uint32_t current_frame_index)
    {
        if (required_size == 0 || required_size <= buffer.size)
            return true;

        Buffer replacement;
        if (!create_mapped_buffer(allocator, grow_capacity(buffer.size, required_size, minimum_size), usage, replacement))
            return false;

        retire_buffer(buffer, current_frame_index);
        buffer = std::move(replacement);
        return true;
    }

    void mark_all_label_descriptors_dirty()
    {
        label_descriptor_dirty.assign(frame_resources.size(), true);
    }

    void refresh_label_descriptor(uint32_t frame_index)
    {
        if (frame_index >= frame_resources.size())
            return;
        if (frame_index >= label_descriptor_dirty.size())
            mark_all_label_descriptors_dirty();
        if (!label_descriptor_dirty[frame_index])
            return;
        if (label_atlas.view == VK_NULL_HANDLE || label_atlas.sampler == VK_NULL_HANDLE)
            return;

        auto& frame = frame_resources[frame_index];
        if (frame.descriptor_set == VK_NULL_HANDLE)
            return;

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
        label_descriptor_dirty[frame_index] = false;
    }

    bool create_device_resources(uint32_t frame_count)
    {
        PERF_MEASURE();
        VkDescriptorSetLayoutBinding scene_bindings[8] = {};
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
        scene_bindings[5].binding = 5;
        scene_bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        scene_bindings[5].descriptorCount = kShadowCascadeCount;
        scene_bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        scene_bindings[6].binding = 6;
        scene_bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        scene_bindings[6].descriptorCount = kPointShadowFaceCount;
        scene_bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        scene_bindings[7].binding = 7;
        scene_bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        scene_bindings[7].descriptorCount = 1;
        scene_bindings[7].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layout_ci.bindingCount = 8;
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

        VkDescriptorSetLayoutBinding post_bindings[3] = {};
        post_bindings[0].binding = 0;
        post_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        post_bindings[0].descriptorCount = 1;
        post_bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        post_bindings[1].binding = 1;
        post_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        post_bindings[1].descriptorCount = 1;
        post_bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        post_bindings[2].binding = 2;
        post_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        post_bindings[2].descriptorCount = 1;
        post_bindings[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo post_layout_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        post_layout_ci.bindingCount = 3;
        post_layout_ci.pBindings = post_bindings;
        if (vkCreateDescriptorSetLayout(device, &post_layout_ci, nullptr, &post_descriptor_set_layout)
            != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create post descriptor set layout");
            return false;
        }

        VkDescriptorPoolSize pool_sizes[3] = {};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = frame_count * 2;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount
            = frame_count * (3 + kSceneMaterialTextureCount + kShadowCascadeCount + kPointShadowFaceCount);
        pool_sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_sizes[2].descriptorCount = frame_count;

        VkDescriptorPoolCreateInfo pool_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool_ci.maxSets = frame_count;
        pool_ci.poolSizeCount = 3;
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

        VkDescriptorPoolSize post_pool_sizes[2] = {};
        post_pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        post_pool_sizes[0].descriptorCount = frame_count;
        post_pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        post_pool_sizes[1].descriptorCount = frame_count * 2;

        VkDescriptorPoolCreateInfo post_pool_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        post_pool_ci.maxSets = frame_count;
        post_pool_ci.poolSizeCount = 2;
        post_pool_ci.pPoolSizes = post_pool_sizes;
        if (vkCreateDescriptorPool(device, &post_pool_ci, nullptr, &post_descriptor_pool) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create post descriptor pool");
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

        std::vector<VkDescriptorSetLayout> post_layouts(frame_count, post_descriptor_set_layout);
        std::vector<VkDescriptorSet> post_descriptor_sets(frame_count, VK_NULL_HANDLE);
        VkDescriptorSetAllocateInfo post_alloc_info = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        post_alloc_info.descriptorPool = post_descriptor_pool;
        post_alloc_info.descriptorSetCount = frame_count;
        post_alloc_info.pSetLayouts = post_layouts.data();
        if (vkAllocateDescriptorSets(device, &post_alloc_info, post_descriptor_sets.data()) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to allocate post descriptor set");
            return false;
        }

        for (uint32_t i = 0; i < frame_count; ++i)
        {
            auto& frame = frame_resources[i];
            frame.descriptor_set = descriptor_sets[i];
            frame.post_descriptor_set = post_descriptor_sets[i];
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
            if (!create_mapped_buffer(allocator, sizeof(float),
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, frame.performance_heat_values))
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create performance heat buffer");
                return false;
            }

            VkDescriptorBufferInfo buffer_info = {};
            buffer_info.buffer = frame.frame_uniforms.buffer;
            buffer_info.range = sizeof(FrameUniforms);
            VkDescriptorBufferInfo material_buffer_info = {};
            material_buffer_info.buffer = frame.material_uniforms.buffer;
            material_buffer_info.range = sizeof(MaterialUniforms);
            VkDescriptorBufferInfo performance_heat_buffer_info = {};
            performance_heat_buffer_info.buffer = frame.performance_heat_values.buffer;
            performance_heat_buffer_info.range = sizeof(float);

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
            VkWriteDescriptorSet performance_heat_write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            performance_heat_write.dstSet = frame.descriptor_set;
            performance_heat_write.dstBinding = 7;
            performance_heat_write.descriptorCount = 1;
            performance_heat_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            performance_heat_write.pBufferInfo = &performance_heat_buffer_info;
            VkWriteDescriptorSet prepass_write = write;
            prepass_write.dstSet = frame.prepass_descriptor_set;
            VkWriteDescriptorSet post_write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            post_write.dstSet = frame.post_descriptor_set;
            post_write.dstBinding = 0;
            post_write.descriptorCount = 1;
            post_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            post_write.pBufferInfo = &buffer_info;
            VkWriteDescriptorSet writes[5] = { write, material_write, performance_heat_write, prepass_write, post_write };
            vkUpdateDescriptorSets(device, 5, writes, 0, nullptr);
        }

        buffered_frame_count = frame_count;
        mark_all_label_descriptors_dirty();
        refresh_gbuffer_descriptors();
        refresh_prepass_descriptors();
        refresh_post_descriptors();

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
        PERF_MEASURE();
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
        PERF_MEASURE();
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
            { SceneTextureId::WoodMetalness, &wood_images.metalness, VK_FORMAT_R8G8B8A8_UNORM },
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

        std::array<size_t, kSceneMaterialTextureCount> upload_offsets{};
        size_t total_upload_bytes = 0;
        for (size_t spec_index = 0; spec_index < load_specs.size(); ++spec_index)
        {
            const TextureLoadSpec& spec = load_specs[spec_index];
            upload_offsets[spec_index] = total_upload_bytes;
            total_upload_bytes += static_cast<size_t>(spec.image->width) * static_cast<size_t>(spec.image->height) * 4;
            total_upload_bytes = (total_upload_bytes + 3u) & ~size_t(3u);
        }
        if (!ensure_mapped_buffer_capacity(
                allocator, total_upload_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, material_staging, total_upload_bytes))
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to reserve material staging buffer");
            return false;
        }

        for (size_t spec_index = 0; spec_index < load_specs.size(); ++spec_index)
        {
            const TextureLoadSpec& spec = load_specs[spec_index];
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
                || !upload_rgba_texture(
                    allocator, cmd, material_staging, upload_offsets[spec_index], *spec.image, texture))
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
        const std::shared_ptr<const MeshData>& tree_leaf_mesh_data,
        uint32_t current_frame_index)
    {
        PERF_MEASURE();
        if (tree_bark_mesh_data
            && !(tree_bark_mesh_source == tree_bark_mesh_data.get() && tree_bark_mesh.index_count > 0))
        {
            MeshBuffers replacement;
            if (!upload_mesh(allocator, *tree_bark_mesh_data, replacement))
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to upload procedural tree bark mesh");
                return false;
            }
            retire_mesh(tree_bark_mesh, current_frame_index);
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
            retire_mesh(tree_leaf_mesh, current_frame_index);
            tree_leaf_mesh = std::move(replacement);
            tree_leaf_mesh_source = tree_leaf_mesh_data.get();
        }
        return true;
    }

    bool ensure_custom_meshes(const std::vector<std::shared_ptr<const MeshData>>& custom_mesh_data, uint32_t current_frame_index)
    {
        PERF_MEASURE();

        // Compute total vertex and index bytes across all custom meshes.
        size_t total_vertex_bytes = 0;
        size_t total_index_bytes = 0;
        for (const auto& mesh_data : custom_mesh_data)
        {
            if (!mesh_data || mesh_data->vertices.empty() || mesh_data->indices.empty())
                continue;
            total_vertex_bytes = align_up(total_vertex_bytes, sizeof(SceneVertex));
            total_vertex_bytes += mesh_data->vertices.size() * sizeof(SceneVertex);
            total_index_bytes = align_up(total_index_bytes, sizeof(uint16_t));
            total_index_bytes += mesh_data->indices.size() * sizeof(uint16_t);
        }

        // Clear non-owning MeshBuffers before touching pool buffers.
        custom_meshes.clear();

        if (total_vertex_bytes == 0 || total_index_bytes == 0)
        {
            retire_buffer(custom_vertex_pool, current_frame_index);
            retire_buffer(custom_index_pool, current_frame_index);
            return true;
        }

        // Ensure pool buffers are large enough.
        if (total_vertex_bytes > custom_vertex_pool.size)
        {
            if (!ensure_retired_mapped_buffer_capacity(
                    total_vertex_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    custom_vertex_pool, total_vertex_bytes, current_frame_index))
                return false;
        }
        if (total_index_bytes > custom_index_pool.size)
        {
            if (!ensure_retired_mapped_buffer_capacity(
                    total_index_bytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    custom_index_pool, total_index_bytes, current_frame_index))
                return false;
        }

        // Pack all mesh data into the consolidated buffers and build per-mesh slices.
        custom_meshes.resize(custom_mesh_data.size());
        size_t vertex_cursor = 0;
        size_t index_cursor = 0;
        for (size_t i = 0; i < custom_mesh_data.size(); ++i)
        {
            const auto& mesh_data = custom_mesh_data[i];
            if (!mesh_data || mesh_data->vertices.empty() || mesh_data->indices.empty())
                continue;

            vertex_cursor = align_up(vertex_cursor, sizeof(SceneVertex));
            index_cursor = align_up(index_cursor, sizeof(uint16_t));

            const size_t vbytes = mesh_data->vertices.size() * sizeof(SceneVertex);
            const size_t ibytes = mesh_data->indices.size() * sizeof(uint16_t);
            std::memcpy(static_cast<uint8_t*>(custom_vertex_pool.mapped) + vertex_cursor,
                mesh_data->vertices.data(), vbytes);
            std::memcpy(static_cast<uint8_t*>(custom_index_pool.mapped) + index_cursor,
                mesh_data->indices.data(), ibytes);

            MeshBuffers& entry = custom_meshes[i];
            entry.vertices.buffer = custom_vertex_pool.buffer;
            entry.indices.buffer = custom_index_pool.buffer;
            entry.index_count = static_cast<uint32_t>(mesh_data->indices.size());
            entry.first_index = static_cast<uint32_t>(index_cursor / sizeof(uint16_t));
            entry.vertex_offset = static_cast<int32_t>(vertex_cursor / sizeof(SceneVertex));

            vertex_cursor += vbytes;
            index_cursor += ibytes;
        }

        vmaFlushAllocation(allocator, custom_vertex_pool.allocation, 0, vertex_cursor);
        vmaFlushAllocation(allocator, custom_index_pool.allocation, 0, index_cursor);
        return true;
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

            std::array<VkDescriptorImageInfo, kShadowCascadeCount> shadow_infos{};
            bool have_shadow_maps = true;
            for (size_t cascade_index = 0; cascade_index < kShadowCascadeCount; ++cascade_index)
            {
                if (gbuffer.shadow_views[cascade_index] == VK_NULL_HANDLE)
                {
                    have_shadow_maps = false;
                    break;
                }
                shadow_infos[cascade_index].sampler = gbuffer_point_sampler;
                shadow_infos[cascade_index].imageView = gbuffer.shadow_views[cascade_index];
                shadow_infos[cascade_index].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            }

            std::array<VkDescriptorImageInfo, kPointShadowFaceCount> point_shadow_infos{};
            bool have_point_shadow = true;
            for (size_t face_index = 0; face_index < kPointShadowFaceCount; ++face_index)
            {
                if (gbuffer.point_shadow_face_views[face_index] == VK_NULL_HANDLE)
                {
                    have_point_shadow = false;
                    break;
                }
                point_shadow_infos[face_index].sampler = gbuffer_point_sampler;
                point_shadow_infos[face_index].imageView = gbuffer.point_shadow_face_views[face_index];
                point_shadow_infos[face_index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            std::array<VkDescriptorImageInfo, kSceneMaterialTextureCount> material_infos{};
            for (size_t texture_index = 0; texture_index < material_textures.size(); ++texture_index)
            {
                material_infos[texture_index].sampler = material_textures[texture_index].sampler;
                material_infos[texture_index].imageView = material_textures[texture_index].view;
                material_infos[texture_index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            }

            VkWriteDescriptorSet writes[4] = {};
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

            if (have_shadow_maps)
            {
                writes[write_count].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[write_count].dstSet = frame.descriptor_set;
                writes[write_count].dstBinding = 5;
                writes[write_count].descriptorCount = static_cast<uint32_t>(shadow_infos.size());
                writes[write_count].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[write_count].pImageInfo = shadow_infos.data();
                ++write_count;
            }

            if (have_point_shadow)
            {
                writes[write_count].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[write_count].dstSet = frame.descriptor_set;
                writes[write_count].dstBinding = 6;
                writes[write_count].descriptorCount = static_cast<uint32_t>(point_shadow_infos.size());
                writes[write_count].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[write_count].pImageInfo = point_shadow_infos.data();
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

    void refresh_post_descriptors()
    {
        if (gbuffer_sampler == VK_NULL_HANDLE || gbuffer_targets.empty())
            return;

        for (size_t i = 0; i < frame_resources.size() && i < gbuffer_targets.size(); ++i)
        {
            const auto& gbuffer = gbuffer_targets[i];
            auto& frame = frame_resources[i];
            if (frame.post_descriptor_set == VK_NULL_HANDLE
                || gbuffer.scene_hdr_view == VK_NULL_HANDLE
                || gbuffer.scene_final_unorm_view == VK_NULL_HANDLE)
            {
                continue;
            }

            VkDescriptorImageInfo hdr_info = {};
            hdr_info.sampler = gbuffer_sampler;
            hdr_info.imageView = gbuffer.scene_hdr_view;
            hdr_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkDescriptorImageInfo final_info = {};
            final_info.sampler = gbuffer_sampler;
            final_info.imageView = gbuffer.scene_final_unorm_view;
            final_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet writes[2] = {};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = frame.post_descriptor_set;
            writes[0].dstBinding = 1;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[0].pImageInfo = &hdr_info;

            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = frame.post_descriptor_set;
            writes[1].dstBinding = 2;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].pImageInfo = &final_info;

            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        }
    }

    bool ensure_label_atlas(const VkRenderContext& ctx, VkCommandBuffer cmd,
        const std::shared_ptr<const LabelAtlasData>& atlas, uint32_t current_frame_index)
    {
        PERF_MEASURE();
        const bool has_atlas = atlas && atlas->valid();
        const int desired_width = has_atlas ? atlas->width : 1;
        const int desired_height = has_atlas ? atlas->height : 1;

        if (label_atlas.width != desired_width || label_atlas.height != desired_height
            || label_atlas.image == VK_NULL_HANDLE)
        {
            retire_image(label_atlas, current_frame_index);
            if (!create_label_image(physical_device, device, allocator, desired_width, desired_height, label_atlas))
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create label atlas image");
                return false;
            }
            label_atlas_layout = VK_IMAGE_LAYOUT_UNDEFINED;
            label_atlas_revision = std::numeric_limits<uint64_t>::max();
            mark_all_label_descriptors_dirty();
        }

        if (has_atlas && label_atlas_revision == atlas->revision)
            return true;
        if (!has_atlas && label_atlas_revision == 0)
            return true;

        const uint8_t clear_pixel[4] = { 0, 0, 0, 0 };
        const uint8_t* pixels = has_atlas ? atlas->rgba.data() : clear_pixel;
        const size_t bytes = static_cast<size_t>(desired_width) * static_cast<size_t>(desired_height) * 4;
        if (!ensure_retired_mapped_buffer_capacity(
                bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, label_staging, bytes, current_frame_index))
            return false;

        std::memcpy(label_staging.mapped, pixels, bytes);
        vmaFlushAllocation(allocator, label_staging.allocation, 0, bytes);

        transition_image(cmd, label_atlas.image, label_atlas_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy = {};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = { static_cast<uint32_t>(desired_width), static_cast<uint32_t>(desired_height), 1 };
        vkCmdCopyBufferToImage(cmd, label_staging.buffer, label_atlas.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transition_image(cmd, label_atlas.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        label_atlas_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        label_atlas_revision = has_atlas ? atlas->revision : 0;
        return true;
    }

    void destroy_present_resources()
    {
        PERF_MEASURE();
        if (tooltip_sampler != VK_NULL_HANDLE)
            vkDestroySampler(device, tooltip_sampler, nullptr);
        for (auto& frame : frame_resources)
        {
            frame.tooltip_image.sampler = VK_NULL_HANDLE;
            destroy_image(device, allocator, frame.tooltip_image);
            destroy_buffer(allocator, frame.tooltip_staging);
        }
        if (tooltip_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, tooltip_pipeline, nullptr);
        if (tooltip_pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, tooltip_pipeline_layout, nullptr);
        if (tooltip_descriptor_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, tooltip_descriptor_pool, nullptr);
        if (tooltip_descriptor_set_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, tooltip_descriptor_set_layout, nullptr);
        if (present_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, present_pipeline, nullptr);

        tooltip_pipeline = VK_NULL_HANDLE;
        tooltip_pipeline_layout = VK_NULL_HANDLE;
        tooltip_descriptor_pool = VK_NULL_HANDLE;
        tooltip_descriptor_set_layout = VK_NULL_HANDLE;
        tooltip_sampler = VK_NULL_HANDLE;
        for (auto& frame : frame_resources)
        {
            frame.tooltip_descriptor_set = VK_NULL_HANDLE;
            frame.tooltip_image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
            frame.tooltip_texture_revision = 0;
        }
        tooltip_initialized = false;
        present_pipeline = VK_NULL_HANDLE;
    }

    bool create_present_resources()
    {
        PERF_MEASURE();
        if (render_pass == VK_NULL_HANDLE || present_pipeline != VK_NULL_HANDLE)
            return true;

        const auto shader_dir = bundled_asset_path("shaders");
        auto present_vert = load_shader(device, (shader_dir / "megacity_post.vert.spv").string());
        auto present_frag = load_shader(device, (shader_dir / "megacity_present.frag.spv").string());
        if (!present_vert || !present_frag)
        {
            if (present_vert)
                vkDestroyShaderModule(device, present_vert, nullptr);
            if (present_frag)
                vkDestroyShaderModule(device, present_frag, nullptr);
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to load present shaders");
            return false;
        }

        VkPipelineShaderStageCreateInfo present_stages[2] = {};
        present_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        present_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        present_stages[0].module = present_vert;
        present_stages[0].pName = "main";
        present_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        present_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        present_stages[1].module = present_frag;
        present_stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo present_vertex_input = {
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };
        VkPipelineInputAssemblyStateCreateInfo present_input_assembly = {
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
        };
        present_input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo present_viewport_state = {
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
        };
        present_viewport_state.viewportCount = 1;
        present_viewport_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo present_raster = {
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
        };
        present_raster.polygonMode = VK_POLYGON_MODE_FILL;
        present_raster.cullMode = VK_CULL_MODE_NONE;
        present_raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        present_raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo present_multisample = {
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
        };
        present_multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo present_depth = {
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
        };
        present_depth.depthTestEnable = VK_FALSE;
        present_depth.depthWriteEnable = VK_FALSE;
        present_depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState present_blend_attachment = {};
        present_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo present_blend = {
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
        };
        present_blend.attachmentCount = 1;
        present_blend.pAttachments = &present_blend_attachment;

        VkDynamicState present_dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo present_dynamic = {
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
        };
        present_dynamic.dynamicStateCount = 2;
        present_dynamic.pDynamicStates = present_dynamic_states;

        VkGraphicsPipelineCreateInfo present_ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        present_ci.stageCount = 2;
        present_ci.pStages = present_stages;
        present_ci.pVertexInputState = &present_vertex_input;
        present_ci.pInputAssemblyState = &present_input_assembly;
        present_ci.pViewportState = &present_viewport_state;
        present_ci.pRasterizationState = &present_raster;
        present_ci.pMultisampleState = &present_multisample;
        present_ci.pDepthStencilState = &present_depth;
        present_ci.pColorBlendState = &present_blend;
        present_ci.pDynamicState = &present_dynamic;
        present_ci.layout = post_pipeline_layout;
        present_ci.renderPass = render_pass;
        present_ci.subpass = 0;

        const VkResult present_result = vkCreateGraphicsPipelines(
            device, VK_NULL_HANDLE, 1, &present_ci, nullptr, &present_pipeline);
        vkDestroyShaderModule(device, present_vert, nullptr);
        vkDestroyShaderModule(device, present_frag, nullptr);
        if (present_result != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create present pipeline");
            return false;
        }

        auto tooltip_vert = load_shader(device, (shader_dir / "gui_tooltip.vert.spv").string());
        auto tooltip_frag = load_shader(device, (shader_dir / "gui_tooltip.frag.spv").string());
        if (!tooltip_vert || !tooltip_frag)
        {
            if (tooltip_vert)
                vkDestroyShaderModule(device, tooltip_vert, nullptr);
            if (tooltip_frag)
                vkDestroyShaderModule(device, tooltip_frag, nullptr);
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to load tooltip shaders");
            return true;
        }

        VkDescriptorSetLayoutBinding tooltip_binding = {};
        tooltip_binding.binding = 0;
        tooltip_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tooltip_binding.descriptorCount = 1;
        tooltip_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo tooltip_layout_ci = {
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
        };
        tooltip_layout_ci.bindingCount = 1;
        tooltip_layout_ci.pBindings = &tooltip_binding;
        if (vkCreateDescriptorSetLayout(device, &tooltip_layout_ci, nullptr, &tooltip_descriptor_set_layout)
            != VK_SUCCESS)
        {
            vkDestroyShaderModule(device, tooltip_vert, nullptr);
            vkDestroyShaderModule(device, tooltip_frag, nullptr);
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create tooltip descriptor set layout");
            return true;
        }

        VkDescriptorPoolSize tooltip_pool_size = {};
        tooltip_pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tooltip_pool_size.descriptorCount = static_cast<uint32_t>(std::max<size_t>(frame_resources.size(), 1u));

        VkDescriptorPoolCreateInfo tooltip_pool_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        tooltip_pool_ci.maxSets = static_cast<uint32_t>(std::max<size_t>(frame_resources.size(), 1u));
        tooltip_pool_ci.poolSizeCount = 1;
        tooltip_pool_ci.pPoolSizes = &tooltip_pool_size;
        if (vkCreateDescriptorPool(device, &tooltip_pool_ci, nullptr, &tooltip_descriptor_pool) != VK_SUCCESS)
        {
            vkDestroyShaderModule(device, tooltip_vert, nullptr);
            vkDestroyShaderModule(device, tooltip_frag, nullptr);
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create tooltip descriptor pool");
            return true;
        }

        std::vector<VkDescriptorSetLayout> tooltip_layouts(frame_resources.size(), tooltip_descriptor_set_layout);
        std::vector<VkDescriptorSet> tooltip_sets(frame_resources.size(), VK_NULL_HANDLE);
        VkDescriptorSetAllocateInfo tooltip_alloc = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        tooltip_alloc.descriptorPool = tooltip_descriptor_pool;
        tooltip_alloc.descriptorSetCount = static_cast<uint32_t>(tooltip_layouts.size());
        tooltip_alloc.pSetLayouts = tooltip_layouts.data();
        if (tooltip_layouts.empty()
            || vkAllocateDescriptorSets(device, &tooltip_alloc, tooltip_sets.data()) != VK_SUCCESS)
        {
            vkDestroyShaderModule(device, tooltip_vert, nullptr);
            vkDestroyShaderModule(device, tooltip_frag, nullptr);
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to allocate tooltip descriptor set");
            return true;
        }
        for (size_t i = 0; i < tooltip_sets.size() && i < frame_resources.size(); ++i)
            frame_resources[i].tooltip_descriptor_set = tooltip_sets[i];

        VkPushConstantRange tooltip_push = {};
        tooltip_push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        tooltip_push.offset = 0;
        tooltip_push.size = sizeof(float) * 8;

        VkPipelineLayoutCreateInfo tooltip_pl_ci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        tooltip_pl_ci.setLayoutCount = 1;
        tooltip_pl_ci.pSetLayouts = &tooltip_descriptor_set_layout;
        tooltip_pl_ci.pushConstantRangeCount = 1;
        tooltip_pl_ci.pPushConstantRanges = &tooltip_push;
        if (vkCreatePipelineLayout(device, &tooltip_pl_ci, nullptr, &tooltip_pipeline_layout) != VK_SUCCESS)
        {
            vkDestroyShaderModule(device, tooltip_vert, nullptr);
            vkDestroyShaderModule(device, tooltip_frag, nullptr);
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create tooltip pipeline layout");
            return true;
        }

        VkPipelineShaderStageCreateInfo tooltip_stages[2] = {};
        tooltip_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        tooltip_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        tooltip_stages[0].module = tooltip_vert;
        tooltip_stages[0].pName = "main";
        tooltip_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        tooltip_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        tooltip_stages[1].module = tooltip_frag;
        tooltip_stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo tooltip_vi = {
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };
        VkPipelineInputAssemblyStateCreateInfo tooltip_ia = {
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
        };
        tooltip_ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo tooltip_vp = {
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
        };
        tooltip_vp.viewportCount = 1;
        tooltip_vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo tooltip_raster = {
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
        };
        tooltip_raster.polygonMode = VK_POLYGON_MODE_FILL;
        tooltip_raster.cullMode = VK_CULL_MODE_NONE;
        tooltip_raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        tooltip_raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo tooltip_ms = {
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
        };
        tooltip_ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo tooltip_depth = {
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO
        };
        tooltip_depth.depthTestEnable = VK_FALSE;
        tooltip_depth.depthWriteEnable = VK_FALSE;
        tooltip_depth.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        VkPipelineColorBlendAttachmentState tooltip_blend_att = {};
        tooltip_blend_att.blendEnable = VK_TRUE;
        tooltip_blend_att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        tooltip_blend_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        tooltip_blend_att.colorBlendOp = VK_BLEND_OP_ADD;
        tooltip_blend_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        tooltip_blend_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        tooltip_blend_att.alphaBlendOp = VK_BLEND_OP_ADD;
        tooltip_blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo tooltip_blend = {
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
        };
        tooltip_blend.attachmentCount = 1;
        tooltip_blend.pAttachments = &tooltip_blend_att;

        VkPipelineDynamicStateCreateInfo tooltip_dyn = {
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
        };
        tooltip_dyn.dynamicStateCount = 2;
        tooltip_dyn.pDynamicStates = present_dynamic_states;

        VkGraphicsPipelineCreateInfo tooltip_ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        tooltip_ci.stageCount = 2;
        tooltip_ci.pStages = tooltip_stages;
        tooltip_ci.pVertexInputState = &tooltip_vi;
        tooltip_ci.pInputAssemblyState = &tooltip_ia;
        tooltip_ci.pViewportState = &tooltip_vp;
        tooltip_ci.pRasterizationState = &tooltip_raster;
        tooltip_ci.pMultisampleState = &tooltip_ms;
        tooltip_ci.pDepthStencilState = &tooltip_depth;
        tooltip_ci.pColorBlendState = &tooltip_blend;
        tooltip_ci.pDynamicState = &tooltip_dyn;
        tooltip_ci.layout = tooltip_pipeline_layout;
        tooltip_ci.renderPass = render_pass;
        tooltip_ci.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &tooltip_ci, nullptr, &tooltip_pipeline)
            != VK_SUCCESS)
        {
            tooltip_pipeline = VK_NULL_HANDLE;
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create tooltip pipeline");
        }

        VkSamplerCreateInfo tooltip_sampler_ci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        tooltip_sampler_ci.magFilter = VK_FILTER_NEAREST;
        tooltip_sampler_ci.minFilter = VK_FILTER_NEAREST;
        tooltip_sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        tooltip_sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        tooltip_sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        if (vkCreateSampler(device, &tooltip_sampler_ci, nullptr, &tooltip_sampler) != VK_SUCCESS)
        {
            tooltip_sampler = VK_NULL_HANDLE;
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create tooltip sampler");
        }

        tooltip_initialized = tooltip_pipeline != VK_NULL_HANDLE
            && tooltip_pipeline_layout != VK_NULL_HANDLE
            && !frame_resources.empty()
            && tooltip_sampler != VK_NULL_HANDLE;
        vkDestroyShaderModule(device, tooltip_vert, nullptr);
        vkDestroyShaderModule(device, tooltip_frag, nullptr);
        return true;
    }

    bool create_pipeline()
    {
        PERF_MEASURE();
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

        if (pipeline_layout == VK_NULL_HANDLE)
        {
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
        }

        VkPipelineLayoutCreateInfo post_layout_ci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        post_layout_ci.setLayoutCount = 1;
        post_layout_ci.pSetLayouts = &post_descriptor_set_layout;
        if (vkCreatePipelineLayout(device, &post_layout_ci, nullptr, &post_pipeline_layout) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create post pipeline layout");
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

        VkVertexInputAttributeDescription attributes[7] = {};
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
        attributes[6].location = 6;
        attributes[6].binding = 0;
        attributes[6].format = VK_FORMAT_R32_SFLOAT;
        attributes[6].offset = offsetof(SceneVertex, layer_id);

        VkPipelineVertexInputStateCreateInfo vertex_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = 7;
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
        multisample.rasterizationSamples = scene_sample_count;

        VkPipelineDepthStencilStateCreateInfo depth = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState blend_attachment = {};
        blend_attachment.blendEnable = VK_TRUE;
        blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
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
        pipeline_ci.renderPass = scene_render_pass;
        pipeline_ci.subpass = 0;

        const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &pipeline);
        if (result != VK_SUCCESS)
        {
            vkDestroyShaderModule(device, vert, nullptr);
            vkDestroyShaderModule(device, frag, nullptr);
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create graphics pipeline");
            return false;
        }

        // Create a variant with depth test but no depth write for transparent objects.
        VkPipelineDepthStencilStateCreateInfo depth_no_write = depth;
        depth_no_write.depthWriteEnable = VK_FALSE;
        pipeline_ci.pDepthStencilState = &depth_no_write;
        const VkResult ndw_result = vkCreateGraphicsPipelines(
            device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &pipeline_no_depth_write);
        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        if (ndw_result != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer,
                "MegaCity scene: failed to create no-depth-write pipeline");
            return false;
        }

        auto post_vert = load_shader(device, (shader_dir / "megacity_post.vert.spv").string());
        auto post_frag = load_shader(device, (shader_dir / "megacity_post.frag.spv").string());
        if (!post_vert || !post_frag)
        {
            if (post_vert)
                vkDestroyShaderModule(device, post_vert, nullptr);
            if (post_frag)
                vkDestroyShaderModule(device, post_frag, nullptr);
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to load post shaders");
            return false;
        }

        VkPipelineShaderStageCreateInfo post_stages[2] = {};
        post_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        post_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        post_stages[0].module = post_vert;
        post_stages[0].pName = "main";
        post_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        post_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        post_stages[1].module = post_frag;
        post_stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo post_vertex_input = {
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };
        VkPipelineInputAssemblyStateCreateInfo post_input_assembly = {
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO
        };
        post_input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo post_viewport_state = {
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO
        };
        post_viewport_state.viewportCount = 1;
        post_viewport_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo post_raster = {
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO
        };
        post_raster.polygonMode = VK_POLYGON_MODE_FILL;
        post_raster.cullMode = VK_CULL_MODE_NONE;
        post_raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        post_raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo post_multisample = {
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO
        };
        post_multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState post_blend_attachment = {};
        post_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo post_blend = {
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
        };
        post_blend.attachmentCount = 1;
        post_blend.pAttachments = &post_blend_attachment;

        VkPipelineDynamicStateCreateInfo post_dynamic = {
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
        };
        post_dynamic.dynamicStateCount = 2;
        post_dynamic.pDynamicStates = dynamic_states;

        VkGraphicsPipelineCreateInfo post_pipeline_ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        post_pipeline_ci.stageCount = 2;
        post_pipeline_ci.pStages = post_stages;
        post_pipeline_ci.pVertexInputState = &post_vertex_input;
        post_pipeline_ci.pInputAssemblyState = &post_input_assembly;
        post_pipeline_ci.pViewportState = &post_viewport_state;
        post_pipeline_ci.pRasterizationState = &post_raster;
        post_pipeline_ci.pMultisampleState = &post_multisample;
        post_pipeline_ci.pColorBlendState = &post_blend;
        post_pipeline_ci.pDynamicState = &post_dynamic;
        post_pipeline_ci.layout = post_pipeline_layout;
        post_pipeline_ci.renderPass = scene_post_render_pass;
        post_pipeline_ci.subpass = 0;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &post_pipeline_ci, nullptr, &post_pipeline)
            != VK_SUCCESS)
        {
            vkDestroyShaderModule(device, post_vert, nullptr);
            vkDestroyShaderModule(device, post_frag, nullptr);
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create post pipeline");
            return false;
        }

        vkDestroyShaderModule(device, post_vert, nullptr);
        vkDestroyShaderModule(device, post_frag, nullptr);

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

        // Create wireframe variants (requires fillModeNonSolid device feature)
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(physical_device, &features);
        if (features.fillModeNonSolid)
        {
            raster.polygonMode = VK_POLYGON_MODE_LINE;

            // Wireframe scene pipeline
            stages[0].module = load_shader(device, (shader_dir / "megacity_scene.vert.spv").string());
            stages[1].module = load_shader(device, (shader_dir / "megacity_scene.frag.spv").string());
            if (stages[0].module && stages[1].module)
            {
                pipeline_ci.pStages = stages;
                vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &wireframe_pipeline);
            }
            if (stages[0].module)
                vkDestroyShaderModule(device, stages[0].module, nullptr);
            if (stages[1].module)
                vkDestroyShaderModule(device, stages[1].module, nullptr);

            // Wireframe debug pipeline
            auto wf_debug_vert = load_shader(device, (shader_dir / "megacity_scene.vert.spv").string());
            auto wf_debug_frag = load_shader(device, (shader_dir / "megacity_debug.frag.spv").string());
            if (wf_debug_vert && wf_debug_frag)
            {
                VkPipelineShaderStageCreateInfo wf_stages[2] = {};
                wf_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                wf_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
                wf_stages[0].module = wf_debug_vert;
                wf_stages[0].pName = "main";
                wf_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                wf_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                wf_stages[1].module = wf_debug_frag;
                wf_stages[1].pName = "main";
                pipeline_ci.pStages = wf_stages;
                vkCreateGraphicsPipelines(
                    device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &debug_wireframe_pipeline);
            }
            if (wf_debug_vert)
                vkDestroyShaderModule(device, wf_debug_vert, nullptr);
            if (wf_debug_frag)
                vkDestroyShaderModule(device, wf_debug_frag, nullptr);
        }

        return true;
    }

    bool ensure_tooltip_texture(uint32_t frame_index, VkCommandBuffer cmd, const TooltipOverlay& tooltip)
    {
        PERF_MEASURE();
        if (!tooltip_initialized || !tooltip.valid() || frame_index >= frame_resources.size())
            return false;
        auto& frame = frame_resources[frame_index];
        if (frame.tooltip_texture_revision == tooltip.revision && frame.tooltip_image.image != VK_NULL_HANDLE)
            return true;

        // Recreate tooltip image if size changed.
        if (frame.tooltip_image.width != tooltip.width || frame.tooltip_image.height != tooltip.height
            || frame.tooltip_image.image == VK_NULL_HANDLE)
        {
            frame.tooltip_image.sampler = VK_NULL_HANDLE;
            destroy_image(device, allocator, frame.tooltip_image);
            if (!create_sampled_image(physical_device, device, allocator,
                    tooltip.width, tooltip.height,
                    VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                    false, frame.tooltip_image))
            {
                return false;
            }
            // Replace the sampler on the image with our shared nearest sampler.
            if (frame.tooltip_image.sampler != VK_NULL_HANDLE)
                vkDestroySampler(device, frame.tooltip_image.sampler, nullptr);
            frame.tooltip_image.sampler = VK_NULL_HANDLE;
            frame.tooltip_image_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        }

        // Upload RGBA data via staging buffer.
        const size_t bytes = static_cast<size_t>(tooltip.width) * tooltip.height * 4;
        if (!ensure_mapped_buffer_capacity(
                allocator, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, frame.tooltip_staging, bytes))
            return false;

        std::memcpy(frame.tooltip_staging.mapped, tooltip.rgba.data(), bytes);
        vmaFlushAllocation(allocator, frame.tooltip_staging.allocation, 0, bytes);

        transition_image(cmd, frame.tooltip_image.image, frame.tooltip_image_layout,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy = {};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {
            static_cast<uint32_t>(tooltip.width),
            static_cast<uint32_t>(tooltip.height), 1
        };
        vkCmdCopyBufferToImage(cmd, frame.tooltip_staging.buffer, frame.tooltip_image.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transition_image(cmd, frame.tooltip_image.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        frame.tooltip_image_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        frame.tooltip_texture_revision = tooltip.revision;

        // Update descriptor set.
        VkDescriptorImageInfo img_info = {};
        img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        img_info.imageView = frame.tooltip_image.view;
        img_info.sampler = tooltip_sampler;

        VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = frame.tooltip_descriptor_set;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &img_info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

        return true;
    }

    bool ensure(const VkRenderContext& ctx)
    {
        PERF_MEASURE();
        const uint32_t frame_count = std::max(1u, ctx.buffered_frame_count());
        const bool base_resources_ready = pipeline != VK_NULL_HANDLE
            && physical_device == ctx.physical_device()
            && device == ctx.device()
            && allocator == ctx.allocator()
            && buffered_frame_count == frame_count;
        if (!base_resources_ready)
        {
            wait_for_device_idle();
            destroy_gbuffer();
            destroy();
            physical_device = ctx.physical_device();
            device = ctx.device();
            allocator = ctx.allocator();
            render_pass = VK_NULL_HANDLE;
            buffered_frame_count = frame_count;
            scene_sample_count = choose_scene_sample_count(physical_device);

            if (!(create_device_resources(frame_count) && init_gbuffer() && create_pipeline()))
                return false;
        }

        if (ctx.render_pass() != VK_NULL_HANDLE && render_pass != ctx.render_pass())
        {
            wait_for_device_idle();
            destroy_present_resources();
            render_pass = ctx.render_pass();
        }

        return create_present_resources();
    }

    void destroy_gbuffer_targets()
    {
        PERF_MEASURE();
        const bool can_remove_imgui_textures = ImGui::GetCurrentContext() != nullptr
            && ImGui::GetIO().BackendRendererUserData != nullptr;
        for (auto& t : gbuffer_targets)
        {
            if (t.imgui_normal_ds != VK_NULL_HANDLE)
            {
                if (can_remove_imgui_textures)
                    ImGui_ImplVulkan_RemoveTexture(t.imgui_normal_ds);
                t.imgui_normal_ds = VK_NULL_HANDLE;
            }
            if (t.imgui_ao_raw_ds != VK_NULL_HANDLE)
            {
                if (can_remove_imgui_textures)
                    ImGui_ImplVulkan_RemoveTexture(t.imgui_ao_raw_ds);
                t.imgui_ao_raw_ds = VK_NULL_HANDLE;
            }
            if (t.imgui_ao_ds != VK_NULL_HANDLE)
            {
                if (can_remove_imgui_textures)
                    ImGui_ImplVulkan_RemoveTexture(t.imgui_ao_ds);
                t.imgui_ao_ds = VK_NULL_HANDLE;
            }
            if (t.imgui_depth_ds != VK_NULL_HANDLE)
            {
                if (can_remove_imgui_textures)
                    ImGui_ImplVulkan_RemoveTexture(t.imgui_depth_ds);
                t.imgui_depth_ds = VK_NULL_HANDLE;
            }
            for (VkDescriptorSet& shadow_ds : t.imgui_shadow_ds)
            {
                if (shadow_ds != VK_NULL_HANDLE)
                {
                    if (can_remove_imgui_textures)
                        ImGui_ImplVulkan_RemoveTexture(shadow_ds);
                    shadow_ds = VK_NULL_HANDLE;
                }
            }
            for (VkDescriptorSet& point_shadow_ds : t.imgui_point_shadow_ds)
            {
                if (point_shadow_ds != VK_NULL_HANDLE)
                {
                    if (can_remove_imgui_textures)
                        ImGui_ImplVulkan_RemoveTexture(point_shadow_ds);
                    point_shadow_ds = VK_NULL_HANDLE;
                }
            }
            if (t.imgui_scene_hdr_ds != VK_NULL_HANDLE)
            {
                if (can_remove_imgui_textures)
                    ImGui_ImplVulkan_RemoveTexture(t.imgui_scene_hdr_ds);
                t.imgui_scene_hdr_ds = VK_NULL_HANDLE;
            }
            if (t.imgui_scene_final_ds != VK_NULL_HANDLE)
            {
                if (can_remove_imgui_textures)
                    ImGui_ImplVulkan_RemoveTexture(t.imgui_scene_final_ds);
                t.imgui_scene_final_ds = VK_NULL_HANDLE;
            }
            if (t.framebuffer != VK_NULL_HANDLE)
                vkDestroyFramebuffer(device, t.framebuffer, nullptr);
            if (t.ao_raw_framebuffer != VK_NULL_HANDLE)
                vkDestroyFramebuffer(device, t.ao_raw_framebuffer, nullptr);
            if (t.ao_framebuffer != VK_NULL_HANDLE)
                vkDestroyFramebuffer(device, t.ao_framebuffer, nullptr);
            for (VkFramebuffer shadow_fb : t.shadow_framebuffers)
            {
                if (shadow_fb != VK_NULL_HANDLE)
                    vkDestroyFramebuffer(device, shadow_fb, nullptr);
            }
            for (VkFramebuffer point_shadow_fb : t.point_shadow_framebuffers)
            {
                if (point_shadow_fb != VK_NULL_HANDLE)
                    vkDestroyFramebuffer(device, point_shadow_fb, nullptr);
            }
            if (t.scene_framebuffer != VK_NULL_HANDLE)
                vkDestroyFramebuffer(device, t.scene_framebuffer, nullptr);
            if (t.scene_post_framebuffer != VK_NULL_HANDLE)
                vkDestroyFramebuffer(device, t.scene_post_framebuffer, nullptr);
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
            for (size_t cascade_index = 0; cascade_index < kShadowCascadeCount; ++cascade_index)
            {
                if (t.shadow_views[cascade_index] != VK_NULL_HANDLE)
                    vkDestroyImageView(device, t.shadow_views[cascade_index], nullptr);
                if (t.shadow_images[cascade_index] != VK_NULL_HANDLE)
                    vmaDestroyImage(allocator, t.shadow_images[cascade_index], t.shadow_allocs[cascade_index]);
            }
            for (VkImageView point_shadow_face_view : t.point_shadow_face_views)
            {
                if (point_shadow_face_view != VK_NULL_HANDLE)
                    vkDestroyImageView(device, point_shadow_face_view, nullptr);
            }
            if (t.point_shadow_cube_view != VK_NULL_HANDLE)
                vkDestroyImageView(device, t.point_shadow_cube_view, nullptr);
            if (t.point_shadow_image != VK_NULL_HANDLE)
                vmaDestroyImage(allocator, t.point_shadow_image, t.point_shadow_alloc);
            if (t.point_shadow_depth_view != VK_NULL_HANDLE)
                vkDestroyImageView(device, t.point_shadow_depth_view, nullptr);
            if (t.point_shadow_depth_image != VK_NULL_HANDLE)
                vmaDestroyImage(allocator, t.point_shadow_depth_image, t.point_shadow_depth_alloc);
            if (t.scene_color_msaa_view != VK_NULL_HANDLE)
                vkDestroyImageView(device, t.scene_color_msaa_view, nullptr);
            if (t.scene_color_msaa_image != VK_NULL_HANDLE)
                vmaDestroyImage(allocator, t.scene_color_msaa_image, t.scene_color_msaa_alloc);
            if (t.scene_depth_msaa_view != VK_NULL_HANDLE)
                vkDestroyImageView(device, t.scene_depth_msaa_view, nullptr);
            if (t.scene_depth_msaa_image != VK_NULL_HANDLE)
                vmaDestroyImage(allocator, t.scene_depth_msaa_image, t.scene_depth_msaa_alloc);
            if (t.scene_hdr_view != VK_NULL_HANDLE)
                vkDestroyImageView(device, t.scene_hdr_view, nullptr);
            if (t.scene_hdr_image != VK_NULL_HANDLE)
                vmaDestroyImage(allocator, t.scene_hdr_image, t.scene_hdr_alloc);
            if (t.scene_final_srgb_view != VK_NULL_HANDLE)
                vkDestroyImageView(device, t.scene_final_srgb_view, nullptr);
            if (t.scene_final_unorm_view != VK_NULL_HANDLE)
                vkDestroyImageView(device, t.scene_final_unorm_view, nullptr);
            if (t.scene_final_image != VK_NULL_HANDLE)
                vmaDestroyImage(allocator, t.scene_final_image, t.scene_final_alloc);
        }
        gbuffer_targets.clear();
    }

    void destroy_gbuffer()
    {
        PERF_MEASURE();
        if (device == VK_NULL_HANDLE)
            return;
        destroy_gbuffer_targets();
        if (gbuffer_sampler != VK_NULL_HANDLE)
            vkDestroySampler(device, gbuffer_sampler, nullptr);
        if (gbuffer_point_sampler != VK_NULL_HANDLE)
            vkDestroySampler(device, gbuffer_point_sampler, nullptr);
        if (shadow_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, shadow_pipeline, nullptr);
        if (shadow_render_pass != VK_NULL_HANDLE)
            vkDestroyRenderPass(device, shadow_render_pass, nullptr);
        if (point_shadow_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, point_shadow_pipeline, nullptr);
        if (point_shadow_render_pass != VK_NULL_HANDLE)
            vkDestroyRenderPass(device, point_shadow_render_pass, nullptr);
        if (ao_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, ao_pipeline, nullptr);
        if (ao_blur_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, ao_blur_pipeline, nullptr);
        if (ao_render_pass != VK_NULL_HANDLE)
            vkDestroyRenderPass(device, ao_render_pass, nullptr);
        if (scene_post_render_pass != VK_NULL_HANDLE)
            vkDestroyRenderPass(device, scene_post_render_pass, nullptr);
        if (scene_render_pass != VK_NULL_HANDLE)
            vkDestroyRenderPass(device, scene_render_pass, nullptr);
        if (gbuffer_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, gbuffer_pipeline, nullptr);
        if (gbuffer_render_pass != VK_NULL_HANDLE)
            vkDestroyRenderPass(device, gbuffer_render_pass, nullptr);
        gbuffer_sampler = VK_NULL_HANDLE;
        gbuffer_point_sampler = VK_NULL_HANDLE;
        shadow_pipeline = VK_NULL_HANDLE;
        shadow_render_pass = VK_NULL_HANDLE;
        point_shadow_pipeline = VK_NULL_HANDLE;
        point_shadow_render_pass = VK_NULL_HANDLE;
        ao_pipeline = VK_NULL_HANDLE;
        ao_blur_pipeline = VK_NULL_HANDLE;
        ao_render_pass = VK_NULL_HANDLE;
        scene_post_render_pass = VK_NULL_HANDLE;
        scene_render_pass = VK_NULL_HANDLE;
        gbuffer_pipeline = VK_NULL_HANDLE;
        gbuffer_render_pass = VK_NULL_HANDLE;
        gbuffer_initialized = false;
    }

    bool init_gbuffer()
    {
        if (gbuffer_initialized)
            return shadow_pipeline != VK_NULL_HANDLE
                && shadow_render_pass != VK_NULL_HANDLE
                && point_shadow_pipeline != VK_NULL_HANDLE
                && point_shadow_render_pass != VK_NULL_HANDLE
                && gbuffer_pipeline != VK_NULL_HANDLE
                && ao_pipeline != VK_NULL_HANDLE
                && ao_blur_pipeline != VK_NULL_HANDLE
                && scene_render_pass != VK_NULL_HANDLE
                && scene_post_render_pass != VK_NULL_HANDLE
                && gbuffer_sampler != VK_NULL_HANDLE
                && gbuffer_point_sampler != VK_NULL_HANDLE;
        gbuffer_initialized = true;

        VkAttachmentDescription shadow_attachment = {};
        shadow_attachment.format = VK_FORMAT_D32_SFLOAT;
        shadow_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        shadow_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        shadow_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        shadow_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        shadow_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        shadow_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        shadow_attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference shadow_depth_ref = {};
        shadow_depth_ref.attachment = 0;
        shadow_depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription shadow_subpass = {};
        shadow_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        shadow_subpass.pDepthStencilAttachment = &shadow_depth_ref;

        VkSubpassDependency shadow_deps[2] = {};
        shadow_deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        shadow_deps[0].dstSubpass = 0;
        shadow_deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        shadow_deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        shadow_deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        shadow_deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        shadow_deps[1].srcSubpass = 0;
        shadow_deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        shadow_deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        shadow_deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        shadow_deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        shadow_deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo shadow_rp_ci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        shadow_rp_ci.attachmentCount = 1;
        shadow_rp_ci.pAttachments = &shadow_attachment;
        shadow_rp_ci.subpassCount = 1;
        shadow_rp_ci.pSubpasses = &shadow_subpass;
        shadow_rp_ci.dependencyCount = 2;
        shadow_rp_ci.pDependencies = shadow_deps;
        if (vkCreateRenderPass(device, &shadow_rp_ci, nullptr, &shadow_render_pass) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create shadow render pass");
            return false;
        }

        VkAttachmentDescription point_shadow_attachments[2] = {};
        point_shadow_attachments[0].format = VK_FORMAT_R32_SFLOAT;
        point_shadow_attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        point_shadow_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        point_shadow_attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        point_shadow_attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        point_shadow_attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        point_shadow_attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        point_shadow_attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        point_shadow_attachments[1].format = VK_FORMAT_D32_SFLOAT;
        point_shadow_attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        point_shadow_attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        point_shadow_attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        point_shadow_attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        point_shadow_attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        point_shadow_attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        point_shadow_attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference point_shadow_color_ref = {};
        point_shadow_color_ref.attachment = 0;
        point_shadow_color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference point_shadow_depth_ref = {};
        point_shadow_depth_ref.attachment = 1;
        point_shadow_depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription point_shadow_subpass = {};
        point_shadow_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        point_shadow_subpass.colorAttachmentCount = 1;
        point_shadow_subpass.pColorAttachments = &point_shadow_color_ref;
        point_shadow_subpass.pDepthStencilAttachment = &point_shadow_depth_ref;

        VkSubpassDependency point_shadow_deps[2] = {};
        point_shadow_deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        point_shadow_deps[0].dstSubpass = 0;
        point_shadow_deps[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        point_shadow_deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
            | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        point_shadow_deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
            | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        point_shadow_deps[1].srcSubpass = 0;
        point_shadow_deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        point_shadow_deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
            | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        point_shadow_deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        point_shadow_deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        point_shadow_deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo point_shadow_rp_ci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        point_shadow_rp_ci.attachmentCount = 2;
        point_shadow_rp_ci.pAttachments = point_shadow_attachments;
        point_shadow_rp_ci.subpassCount = 1;
        point_shadow_rp_ci.pSubpasses = &point_shadow_subpass;
        point_shadow_rp_ci.dependencyCount = 2;
        point_shadow_rp_ci.pDependencies = point_shadow_deps;
        if (vkCreateRenderPass(device, &point_shadow_rp_ci, nullptr, &point_shadow_render_pass) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create point shadow render pass");
            return false;
        }

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

        VkVertexInputAttributeDescription attributes[7] = {};
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
        attributes[6].location = 6;
        attributes[6].format = VK_FORMAT_R32_SFLOAT;
        attributes[6].offset = offsetof(SceneVertex, layer_id);

        VkPipelineVertexInputStateCreateInfo vertex_input = {
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
        };
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = 7;
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

        auto shadow_vert = load_shader(device, (shader_dir / "megacity_shadow.vert.spv").string());
        if (!shadow_vert)
            return false;

        VkPipelineShaderStageCreateInfo shadow_stage = {};
        shadow_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shadow_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        shadow_stage.module = shadow_vert;
        shadow_stage.pName = "main";

        VkPipelineRasterizationStateCreateInfo shadow_raster = raster;
        shadow_raster.cullMode = VK_CULL_MODE_NONE;
        shadow_raster.depthBiasEnable = VK_TRUE;

        VkPipelineColorBlendStateCreateInfo shadow_blend = {
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
        };

        VkDynamicState shadow_dynamic_states[] = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
            VK_DYNAMIC_STATE_DEPTH_BIAS,
        };
        VkPipelineDynamicStateCreateInfo shadow_dynamic = {
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO
        };
        shadow_dynamic.dynamicStateCount = 3;
        shadow_dynamic.pDynamicStates = shadow_dynamic_states;

        VkGraphicsPipelineCreateInfo shadow_pipeline_ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        shadow_pipeline_ci.stageCount = 1;
        shadow_pipeline_ci.pStages = &shadow_stage;
        shadow_pipeline_ci.pVertexInputState = &vertex_input;
        shadow_pipeline_ci.pInputAssemblyState = &input_assembly;
        shadow_pipeline_ci.pViewportState = &viewport_state;
        shadow_pipeline_ci.pRasterizationState = &shadow_raster;
        shadow_pipeline_ci.pMultisampleState = &multisample;
        shadow_pipeline_ci.pDepthStencilState = &depth_stencil;
        shadow_pipeline_ci.pColorBlendState = &shadow_blend;
        shadow_pipeline_ci.pDynamicState = &shadow_dynamic;
        shadow_pipeline_ci.layout = prepass_pipeline_layout;
        shadow_pipeline_ci.renderPass = shadow_render_pass;
        shadow_pipeline_ci.subpass = 0;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &shadow_pipeline_ci, nullptr, &shadow_pipeline)
            != VK_SUCCESS)
        {
            vkDestroyShaderModule(device, shadow_vert, nullptr);
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create shadow pipeline");
            return false;
        }
        vkDestroyShaderModule(device, shadow_vert, nullptr);

        auto point_shadow_vert = load_shader(device, (shader_dir / "megacity_point_shadow.vert.spv").string());
        auto point_shadow_frag = load_shader(device, (shader_dir / "megacity_point_shadow.frag.spv").string());
        if (!point_shadow_vert || !point_shadow_frag)
        {
            if (point_shadow_vert != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, point_shadow_vert, nullptr);
            if (point_shadow_frag != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, point_shadow_frag, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo point_shadow_stages[2] = {};
        point_shadow_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        point_shadow_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        point_shadow_stages[0].module = point_shadow_vert;
        point_shadow_stages[0].pName = "main";
        point_shadow_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        point_shadow_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        point_shadow_stages[1].module = point_shadow_frag;
        point_shadow_stages[1].pName = "main";

        VkPipelineColorBlendAttachmentState point_shadow_blend_attachment = {};
        point_shadow_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;

        VkPipelineColorBlendStateCreateInfo point_shadow_blend = {
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO
        };
        point_shadow_blend.attachmentCount = 1;
        point_shadow_blend.pAttachments = &point_shadow_blend_attachment;

        VkGraphicsPipelineCreateInfo point_shadow_pipeline_ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
        point_shadow_pipeline_ci.stageCount = 2;
        point_shadow_pipeline_ci.pStages = point_shadow_stages;
        point_shadow_pipeline_ci.pVertexInputState = &vertex_input;
        point_shadow_pipeline_ci.pInputAssemblyState = &input_assembly;
        point_shadow_pipeline_ci.pViewportState = &viewport_state;
        point_shadow_pipeline_ci.pRasterizationState = &shadow_raster;
        point_shadow_pipeline_ci.pMultisampleState = &multisample;
        point_shadow_pipeline_ci.pDepthStencilState = &depth_stencil;
        point_shadow_pipeline_ci.pColorBlendState = &point_shadow_blend;
        point_shadow_pipeline_ci.pDynamicState = &shadow_dynamic;
        point_shadow_pipeline_ci.layout = prepass_pipeline_layout;
        point_shadow_pipeline_ci.renderPass = point_shadow_render_pass;
        point_shadow_pipeline_ci.subpass = 0;
        if (vkCreateGraphicsPipelines(
                device, VK_NULL_HANDLE, 1, &point_shadow_pipeline_ci, nullptr, &point_shadow_pipeline)
            != VK_SUCCESS)
        {
            vkDestroyShaderModule(device, point_shadow_vert, nullptr);
            vkDestroyShaderModule(device, point_shadow_frag, nullptr);
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create point shadow pipeline");
            return false;
        }
        vkDestroyShaderModule(device, point_shadow_vert, nullptr);
        vkDestroyShaderModule(device, point_shadow_frag, nullptr);

        if (pipeline_layout == VK_NULL_HANDLE)
        {
            VkPushConstantRange scene_push_range = {};
            scene_push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            scene_push_range.offset = 0;
            scene_push_range.size = sizeof(ObjectPushConstants);

            VkPipelineLayoutCreateInfo scene_layout_ci = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            scene_layout_ci.setLayoutCount = 1;
            scene_layout_ci.pSetLayouts = &descriptor_set_layout;
            scene_layout_ci.pushConstantRangeCount = 1;
            scene_layout_ci.pPushConstantRanges = &scene_push_range;
            if (vkCreatePipelineLayout(device, &scene_layout_ci, nullptr, &pipeline_layout) != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create GBuffer pipeline layout");
                return false;
            }
        }

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
        ao_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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

        VkAttachmentDescription scene_attachments[3] = {};
        scene_attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
        scene_attachments[0].samples = scene_sample_count;
        scene_attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        scene_attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        scene_attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        scene_attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        scene_attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        scene_attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        scene_attachments[1].format = VK_FORMAT_D32_SFLOAT;
        scene_attachments[1].samples = scene_sample_count;
        scene_attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        scene_attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        scene_attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        scene_attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        scene_attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        scene_attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        scene_attachments[2].format = VK_FORMAT_R16G16B16A16_SFLOAT;
        scene_attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
        scene_attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        scene_attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        scene_attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        scene_attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        scene_attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        scene_attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference scene_color_ref = {};
        scene_color_ref.attachment = 0;
        scene_color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference scene_depth_ref = {};
        scene_depth_ref.attachment = 1;
        scene_depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        VkAttachmentReference scene_resolve_ref = {};
        scene_resolve_ref.attachment = 2;
        scene_resolve_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription scene_subpass = {};
        scene_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        scene_subpass.colorAttachmentCount = 1;
        scene_subpass.pColorAttachments = &scene_color_ref;
        scene_subpass.pDepthStencilAttachment = &scene_depth_ref;
        scene_subpass.pResolveAttachments = &scene_resolve_ref;

        VkSubpassDependency scene_deps[2] = {};
        scene_deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        scene_deps[0].dstSubpass = 0;
        scene_deps[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        scene_deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
            | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        scene_deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
            | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        scene_deps[1].srcSubpass = 0;
        scene_deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        scene_deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
            | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        scene_deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        scene_deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        scene_deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo scene_rp_ci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        scene_rp_ci.attachmentCount = 3;
        scene_rp_ci.pAttachments = scene_attachments;
        scene_rp_ci.subpassCount = 1;
        scene_rp_ci.pSubpasses = &scene_subpass;
        scene_rp_ci.dependencyCount = 2;
        scene_rp_ci.pDependencies = scene_deps;
        if (vkCreateRenderPass(device, &scene_rp_ci, nullptr, &scene_render_pass) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create scene render pass");
            return false;
        }

        VkAttachmentDescription scene_post_attachment = {};
        scene_post_attachment.format = VK_FORMAT_B8G8R8A8_SRGB;
        scene_post_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        scene_post_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        scene_post_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        scene_post_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        scene_post_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        scene_post_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        scene_post_attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference scene_post_color_ref = {};
        scene_post_color_ref.attachment = 0;
        scene_post_color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription scene_post_subpass = {};
        scene_post_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        scene_post_subpass.colorAttachmentCount = 1;
        scene_post_subpass.pColorAttachments = &scene_post_color_ref;

        VkSubpassDependency scene_post_deps[2] = {};
        scene_post_deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        scene_post_deps[0].dstSubpass = 0;
        scene_post_deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        scene_post_deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        scene_post_deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        scene_post_deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        scene_post_deps[1].srcSubpass = 0;
        scene_post_deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        scene_post_deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        scene_post_deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        scene_post_deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        scene_post_deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo scene_post_rp_ci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        scene_post_rp_ci.attachmentCount = 1;
        scene_post_rp_ci.pAttachments = &scene_post_attachment;
        scene_post_rp_ci.subpassCount = 1;
        scene_post_rp_ci.pSubpasses = &scene_post_subpass;
        scene_post_rp_ci.dependencyCount = 2;
        scene_post_rp_ci.pDependencies = scene_post_deps;
        if (vkCreateRenderPass(device, &scene_post_rp_ci, nullptr, &scene_post_render_pass) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create scene post render pass");
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
        PERF_MEASURE();
        if (width <= 0 || height <= 0)
            return false;

        frame_count = std::max(1u, frame_count);
        if (gbuffer_targets.size() == frame_count
            && !gbuffer_targets.empty()
            && gbuffer_targets[0].width == width
            && gbuffer_targets[0].height == height)
            return true;

        if (!gbuffer_targets.empty())
            wait_for_device_idle();
        destroy_gbuffer_targets();
        gbuffer_targets.resize(frame_count);

        for (auto& t : gbuffer_targets)
        {
            bool shadow_ok = true;
            for (size_t cascade_index = 0; cascade_index < kShadowCascadeCount; ++cascade_index)
            {
                shadow_ok = shadow_ok
                    && create_attachment_image(
                        device,
                        allocator,
                        shadow_map_resolution,
                        shadow_map_resolution,
                        VK_FORMAT_D32_SFLOAT,
                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        VK_IMAGE_ASPECT_DEPTH_BIT,
                        VK_SAMPLE_COUNT_1_BIT,
                        t.shadow_images[cascade_index],
                        t.shadow_allocs[cascade_index],
                        t.shadow_views[cascade_index]);
            }

            if (!shadow_ok
                || !create_cube_attachment_image(
                    device,
                    allocator,
                    point_shadow_map_resolution,
                    VK_FORMAT_R32_SFLOAT,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    t.point_shadow_image,
                    t.point_shadow_alloc,
                    t.point_shadow_cube_view,
                    t.point_shadow_face_views)
                || !create_attachment_image(
                    device,
                    allocator,
                    point_shadow_map_resolution,
                    point_shadow_map_resolution,
                    VK_FORMAT_D32_SFLOAT,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT,
                    VK_SAMPLE_COUNT_1_BIT,
                    t.point_shadow_depth_image,
                    t.point_shadow_depth_alloc,
                    t.point_shadow_depth_view)
                || !create_attachment_image(
                    device,
                    allocator,
                    width,
                    height,
                    VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_SAMPLE_COUNT_1_BIT,
                    t.normal_image,
                    t.normal_alloc,
                    t.normal_view)
                || !create_attachment_image(
                    device,
                    allocator,
                    width,
                    height,
                    VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_SAMPLE_COUNT_1_BIT,
                    t.ao_raw_image,
                    t.ao_raw_alloc,
                    t.ao_raw_view)
                || !create_attachment_image(
                    device,
                    allocator,
                    width,
                    height,
                    VK_FORMAT_R8G8B8A8_UNORM,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_SAMPLE_COUNT_1_BIT,
                    t.ao_image,
                    t.ao_alloc,
                    t.ao_view)
                || !create_attachment_image(
                    device,
                    allocator,
                    width,
                    height,
                    VK_FORMAT_D32_SFLOAT,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT,
                    VK_SAMPLE_COUNT_1_BIT,
                    t.depth_image,
                    t.depth_alloc,
                    t.depth_view)
                || !create_attachment_image(
                    device,
                    allocator,
                    width,
                    height,
                    VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    scene_sample_count,
                    t.scene_color_msaa_image,
                    t.scene_color_msaa_alloc,
                    t.scene_color_msaa_view)
                || !create_attachment_image(
                    device,
                    allocator,
                    width,
                    height,
                    VK_FORMAT_D32_SFLOAT,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                    VK_IMAGE_ASPECT_DEPTH_BIT,
                    scene_sample_count,
                    t.scene_depth_msaa_image,
                    t.scene_depth_msaa_alloc,
                    t.scene_depth_msaa_view)
                || !create_attachment_image(
                    device,
                    allocator,
                    width,
                    height,
                    VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_SAMPLE_COUNT_1_BIT,
                    t.scene_hdr_image,
                    t.scene_hdr_alloc,
                    t.scene_hdr_view)
                || !create_attachment_image(
                    device,
                    allocator,
                    width,
                    height,
                    VK_FORMAT_B8G8R8A8_SRGB,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    VK_SAMPLE_COUNT_1_BIT,
                    t.scene_final_image,
                    t.scene_final_alloc,
                    t.scene_final_srgb_view,
                    VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT)
                || !create_attachment_view(
                    device,
                    t.scene_final_image,
                    VK_FORMAT_B8G8R8A8_UNORM,
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    t.scene_final_unorm_view))
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create offscreen scene targets");
                destroy_gbuffer_targets();
                return false;
            }

            for (size_t cascade_index = 0; cascade_index < kShadowCascadeCount; ++cascade_index)
            {
                VkImageView shadow_views[] = { t.shadow_views[cascade_index] };
                VkFramebufferCreateInfo shadow_fb_ci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
                shadow_fb_ci.renderPass = shadow_render_pass;
                shadow_fb_ci.attachmentCount = 1;
                shadow_fb_ci.pAttachments = shadow_views;
                shadow_fb_ci.width = static_cast<uint32_t>(shadow_map_resolution);
                shadow_fb_ci.height = static_cast<uint32_t>(shadow_map_resolution);
                shadow_fb_ci.layers = 1;
                if (vkCreateFramebuffer(device, &shadow_fb_ci, nullptr, &t.shadow_framebuffers[cascade_index])
                    != VK_SUCCESS)
                {
                    DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create shadow framebuffer");
                    destroy_gbuffer_targets();
                    return false;
                }
            }

            for (size_t face_index = 0; face_index < kPointShadowFaceCount; ++face_index)
            {
                VkImageView point_shadow_fb_views[] = {
                    t.point_shadow_face_views[face_index],
                    t.point_shadow_depth_view
                };
                VkFramebufferCreateInfo point_shadow_fb_ci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
                point_shadow_fb_ci.renderPass = point_shadow_render_pass;
                point_shadow_fb_ci.attachmentCount = 2;
                point_shadow_fb_ci.pAttachments = point_shadow_fb_views;
                point_shadow_fb_ci.width = static_cast<uint32_t>(point_shadow_map_resolution);
                point_shadow_fb_ci.height = static_cast<uint32_t>(point_shadow_map_resolution);
                point_shadow_fb_ci.layers = 1;
                if (vkCreateFramebuffer(
                        device, &point_shadow_fb_ci, nullptr, &t.point_shadow_framebuffers[face_index])
                    != VK_SUCCESS)
                {
                    DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create point shadow framebuffer");
                    destroy_gbuffer_targets();
                    return false;
                }
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

            VkImageView scene_fb_views[] = {
                t.scene_color_msaa_view,
                t.scene_depth_msaa_view,
                t.scene_hdr_view
            };
            VkFramebufferCreateInfo scene_fb_ci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            scene_fb_ci.renderPass = scene_render_pass;
            scene_fb_ci.attachmentCount = 3;
            scene_fb_ci.pAttachments = scene_fb_views;
            scene_fb_ci.width = static_cast<uint32_t>(width);
            scene_fb_ci.height = static_cast<uint32_t>(height);
            scene_fb_ci.layers = 1;
            if (vkCreateFramebuffer(device, &scene_fb_ci, nullptr, &t.scene_framebuffer) != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create scene framebuffer");
                destroy_gbuffer_targets();
                return false;
            }

            VkImageView scene_post_views[] = { t.scene_final_srgb_view };
            VkFramebufferCreateInfo scene_post_fb_ci = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
            scene_post_fb_ci.renderPass = scene_post_render_pass;
            scene_post_fb_ci.attachmentCount = 1;
            scene_post_fb_ci.pAttachments = scene_post_views;
            scene_post_fb_ci.width = static_cast<uint32_t>(width);
            scene_post_fb_ci.height = static_cast<uint32_t>(height);
            scene_post_fb_ci.layers = 1;
            if (vkCreateFramebuffer(device, &scene_post_fb_ci, nullptr, &t.scene_post_framebuffer) != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity: failed to create scene post framebuffer");
                destroy_gbuffer_targets();
                return false;
            }

            t.width = width;
            t.height = height;
        }
        refresh_gbuffer_descriptors();
        refresh_prepass_descriptors();
        refresh_post_descriptors();
        return true;
    }

    void destroy()
    {
        PERF_MEASURE();
        if (allocator != VK_NULL_HANDLE)
        {
            destroy_mesh(allocator, cube_mesh);
            destroy_mesh(allocator, floor_mesh);
            destroy_mesh(allocator, tree_bark_mesh);
            destroy_mesh(allocator, tree_leaf_mesh);
            custom_meshes.clear(); // Non-owning views — don't destroy individually
            destroy_buffer(allocator, custom_vertex_pool);
            destroy_buffer(allocator, custom_index_pool);
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
                destroy_buffer(allocator, frame.performance_heat_values);
            }
            for (auto& retired : retired_buffers)
                destroy_buffer(allocator, retired.buffer);
            for (auto& retired : retired_meshes)
                destroy_mesh(allocator, retired.mesh);
            for (auto& retired : retired_images)
                destroy_image(device, allocator, retired.image);
        }
        destroy_present_resources();

        if (debug_wireframe_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, debug_wireframe_pipeline, nullptr);
        if (wireframe_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, wireframe_pipeline, nullptr);
        if (debug_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, debug_pipeline, nullptr);
        if (post_pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, post_pipeline, nullptr);
        if (pipeline_no_depth_write != VK_NULL_HANDLE)
            vkDestroyPipeline(device, pipeline_no_depth_write, nullptr);
        if (pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, pipeline, nullptr);
        if (post_pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, post_pipeline_layout, nullptr);
        if (pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        if (prepass_pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, prepass_pipeline_layout, nullptr);
        if (post_descriptor_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, post_descriptor_pool, nullptr);
        if (descriptor_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (prepass_descriptor_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, prepass_descriptor_pool, nullptr);
        if (post_descriptor_set_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, post_descriptor_set_layout, nullptr);
        if (descriptor_set_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
        if (prepass_descriptor_set_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, prepass_descriptor_set_layout, nullptr);

        device = VK_NULL_HANDLE;
        allocator = VK_NULL_HANDLE;
        render_pass = VK_NULL_HANDLE;
        post_descriptor_set_layout = VK_NULL_HANDLE;
        post_descriptor_pool = VK_NULL_HANDLE;
        post_pipeline_layout = VK_NULL_HANDLE;
        descriptor_set_layout = VK_NULL_HANDLE;
        descriptor_pool = VK_NULL_HANDLE;
        pipeline_layout = VK_NULL_HANDLE;
        prepass_descriptor_set_layout = VK_NULL_HANDLE;
        prepass_descriptor_pool = VK_NULL_HANDLE;
        prepass_pipeline_layout = VK_NULL_HANDLE;
        pipeline = VK_NULL_HANDLE;
        pipeline_no_depth_write = VK_NULL_HANDLE;
        post_pipeline = VK_NULL_HANDLE;
        present_pipeline = VK_NULL_HANDLE;
        frame_resources.clear();
        label_descriptor_dirty.clear();
        retired_buffers.clear();
        retired_meshes.clear();
        retired_images.clear();
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
    PERF_MEASURE();
}

IsometricScenePass::~IsometricScenePass()
{
    PERF_MEASURE();
    state_->wait_for_device_idle();
    state_->destroy_gbuffer();
    state_->destroy();
}

void IsometricScenePass::record_prepass(IRenderContext& ctx)
{
    PERF_MEASURE();
    auto* vk_ctx = static_cast<VkRenderContext*>(&ctx);
    auto cmd = vk_ctx->command_buffer();
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

    state_->reclaim_retired_resources(frame_index);
    state_->last_prepass_frame = frame_index;
    auto& gbuffer = state_->gbuffer_targets[frame_index];
    auto& frame_res = state_->frame_resources[frame_index];
    frame_res.geometry_arena.reset();

    if (!state_->ensure_road_materials(*vk_ctx, cmd))
        return;
    if (!state_->ensure_label_atlas(*vk_ctx, cmd, scene_.label_atlas, frame_index))
        return;
    state_->refresh_label_descriptor(frame_index);

    // Upload tooltip texture if needed.
        if (scene_.tooltip.valid())
            state_->ensure_tooltip_texture(frame_index, cmd, scene_.tooltip);

    // Ensure floor grid mesh
    if (!state_->ensure_floor_grid(scene_.floor_grid))
        return;
    if (!state_->ensure_tree_mesh(scene_.tree_bark_mesh, scene_.tree_leaf_mesh, frame_index)
        || !state_->ensure_custom_meshes(scene_.custom_meshes, frame_index))
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
    frame.perf_tuning = scene_.camera.perf_tuning;
    frame.screen_params = glm::vec4(0.0f, 0.0f, 1.0f / std::max(vw, 1), 1.0f / std::max(vh, 1));
    frame.ao_params = glm::vec4(
        scene_.camera.ao_settings.x,
        compute_ao_radius_pixels(scene_.camera.proj, scene_.camera.ao_settings.x, vh),
        scene_.camera.ao_settings.y,
        scene_.camera.ao_settings.z);
    frame.debug_view = scene_.camera.debug_view;
    frame.world_debug_bounds = scene_.camera.world_debug_bounds;
    const DirectionalShadowCascadeSet shadow_cascades = build_directional_shadow_cascades(scene_.camera, state_->shadow_map_resolution);
    const PointShadowMapSet point_shadow = build_point_shadow_map(scene_.camera, state_->point_shadow_map_resolution);
    for (size_t cascade_index = 0; cascade_index < kShadowCascadeCount; ++cascade_index)
    {
        const glm::mat4 shadow_proj = make_vulkan_projection(shadow_cascades.cascades[cascade_index].proj);
        const glm::mat4 world_to_clip = shadow_proj * shadow_cascades.cascades[cascade_index].view;
        frame.shadow_view_proj[cascade_index] = world_to_clip;
        frame.shadow_texture_matrix[cascade_index] = make_vulkan_shadow_texture_matrix(world_to_clip);
    }
    frame.shadow_split_depths = glm::vec4(
        shadow_cascades.cascades[0].split_depth,
        shadow_cascades.cascades[1].split_depth,
        shadow_cascades.cascades[2].split_depth,
        1.0f);
    frame.shadow_params = glm::vec4(
        static_cast<float>(shadow_cascades.cascade_count),
        shadow_cascades.sample_depth_bias,
        shadow_cascades.normal_bias,
        1.0f / static_cast<float>(std::max(shadow_cascades.resolution, 1)));
    for (size_t face_index = 0; face_index < kPointShadowFaceCount; ++face_index)
    {
        const glm::mat4 point_world_to_clip = make_vulkan_projection(point_shadow.view_proj[face_index]);
        frame.point_shadow_view_proj[face_index] = point_world_to_clip;
        frame.point_shadow_texture_matrix[face_index] = make_vulkan_shadow_texture_matrix(point_world_to_clip);
    }
    frame.point_shadow_params = glm::vec4(
        point_shadow.sample_depth_bias,
        point_shadow.normal_bias,
        1.0f / static_cast<float>(std::max(point_shadow.resolution, 1)),
        point_shadow.valid ? 1.0f : 0.0f);
    std::memcpy(frame_res.frame_uniforms.mapped, &frame, sizeof(frame));
    vmaFlushAllocation(vk_ctx->allocator(), frame_res.frame_uniforms.allocation, 0, sizeof(frame));
    const MaterialUniforms material_uniforms = build_material_uniforms(scene_);
    std::memcpy(frame_res.material_uniforms.mapped, &material_uniforms, sizeof(material_uniforms));
    vmaFlushAllocation(vk_ctx->allocator(), frame_res.material_uniforms.allocation, 0, sizeof(material_uniforms));
    const size_t performance_heat_bytes = std::max<size_t>(scene_.performance_heat_values.size(), 1u) * sizeof(float);
    if (!ensure_mapped_buffer_capacity(
            vk_ctx->allocator(),
            performance_heat_bytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            frame_res.performance_heat_values,
            sizeof(float)))
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to grow performance heat buffer");
        return;
    }
    if (scene_.performance_heat_values.empty())
    {
        static_cast<float*>(frame_res.performance_heat_values.mapped)[0] = 0.0f;
    }
    else
    {
        std::memcpy(
            frame_res.performance_heat_values.mapped,
            scene_.performance_heat_values.data(),
            scene_.performance_heat_values.size() * sizeof(float));
    }
    vmaFlushAllocation(vk_ctx->allocator(), frame_res.performance_heat_values.allocation, 0, performance_heat_bytes);
    VkDescriptorBufferInfo performance_heat_buffer_info = {};
    performance_heat_buffer_info.buffer = frame_res.performance_heat_values.buffer;
    performance_heat_buffer_info.range = performance_heat_bytes;
    VkWriteDescriptorSet performance_heat_write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    performance_heat_write.dstSet = frame_res.descriptor_set;
    performance_heat_write.dstBinding = 7;
    performance_heat_write.descriptorCount = 1;
    performance_heat_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    performance_heat_write.pBufferInfo = &performance_heat_buffer_info;
    vkUpdateDescriptorSets(
        vk_ctx->device(), 1, &performance_heat_write, 0, nullptr);

    const uint32_t shadow_opaque_count = std::min(scene_.opaque_count,
        static_cast<uint32_t>(scene_.objects.size()));
    auto object_casts_shadow = [&](const SceneObject& obj) {
        if (obj.mesh == MeshId::Grid || obj.color.a < 1.0f)
            return false;
        if (!obj.route_source.empty() || !obj.route_target.empty())
            return false;
        if (obj.material_index < scene_.materials.size()
            && scene_.materials[obj.material_index].shading_model == MaterialShadingModel::LeafCutoutPbr)
        {
            return false;
        }
        return true;
    };

    VkViewport shadow_viewport = {};
    shadow_viewport.width = static_cast<float>(state_->shadow_map_resolution);
    shadow_viewport.height = static_cast<float>(state_->shadow_map_resolution);
    shadow_viewport.maxDepth = 1.0f;

    VkRect2D shadow_scissor = {};
    shadow_scissor.extent = {
        static_cast<uint32_t>(state_->shadow_map_resolution),
        static_cast<uint32_t>(state_->shadow_map_resolution)
    };

    for (uint32_t cascade_index = 0; cascade_index < kShadowCascadeCount; ++cascade_index)
    {
        if (gbuffer.shadow_framebuffers[cascade_index] == VK_NULL_HANDLE)
            continue;

        VkClearValue shadow_clear = {};
        shadow_clear.depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo shadow_rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        shadow_rpbi.renderPass = state_->shadow_render_pass;
        shadow_rpbi.framebuffer = gbuffer.shadow_framebuffers[cascade_index];
        shadow_rpbi.renderArea.extent = {
            static_cast<uint32_t>(state_->shadow_map_resolution),
            static_cast<uint32_t>(state_->shadow_map_resolution)
        };
        shadow_rpbi.clearValueCount = 1;
        shadow_rpbi.pClearValues = &shadow_clear;

        vkCmdBeginRenderPass(cmd, &shadow_rpbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(cmd, 0, 1, &shadow_viewport);
        vkCmdSetScissor(cmd, 0, 1, &shadow_scissor);
        vkCmdSetDepthBias(cmd, 2.0f, 0.0f, 2.0f);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->shadow_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->prepass_pipeline_layout,
            0, 1, &frame_res.prepass_descriptor_set, 0, nullptr);

        const MeshBuffers* last_shadow_mesh = nullptr;
        for (uint32_t shadow_index = 0; shadow_index < shadow_opaque_count; ++shadow_index)
        {
            const SceneObject& obj = scene_.objects[shadow_index];
            if (!object_casts_shadow(obj))
                continue;

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
            case MeshId::Custom:
                if (obj.custom_mesh_index < state_->custom_meshes.size())
                    mesh = &state_->custom_meshes[obj.custom_mesh_index];
                break;
            case MeshId::Grid:
                break;
            }
            if (!mesh || mesh->index_count == 0)
                continue;

            if (mesh != last_shadow_mesh)
            {
                VkBuffer vertex_buffer = mesh->vertices.buffer;
                VkDeviceSize vertex_offset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &vertex_offset);
                vkCmdBindIndexBuffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT16);
                last_shadow_mesh = mesh;
            }

            ObjectPushConstants push;
            push.world = obj.world;
            push.color = obj.color;
            push.material_data = glm::uvec4(obj.material_index, cascade_index, 0u, 0u);
            push.uv_rect = obj.uv_rect;
            push.label_metrics = glm::vec4(0.0f);
            vkCmdPushConstants(cmd, state_->prepass_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
            vkCmdDrawIndexed(cmd, mesh->index_count, 1, mesh->first_index, mesh->vertex_offset, 0);
        }

        vkCmdEndRenderPass(cmd);
    }

    if (point_shadow.valid)
    {
        VkViewport point_shadow_viewport = {};
        point_shadow_viewport.width = static_cast<float>(state_->point_shadow_map_resolution);
        point_shadow_viewport.height = static_cast<float>(state_->point_shadow_map_resolution);
        point_shadow_viewport.maxDepth = 1.0f;

        VkRect2D point_shadow_scissor = {};
        point_shadow_scissor.extent = {
            static_cast<uint32_t>(state_->point_shadow_map_resolution),
            static_cast<uint32_t>(state_->point_shadow_map_resolution)
        };

        VkClearValue point_shadow_clears[2] = {};
        point_shadow_clears[0].color = { { 1.0f, 0.0f, 0.0f, 0.0f } };
        point_shadow_clears[1].depthStencil = { 1.0f, 0 };

        for (uint32_t face_index = 0; face_index < kPointShadowFaceCount; ++face_index)
        {
            if (gbuffer.point_shadow_framebuffers[face_index] == VK_NULL_HANDLE)
                continue;

            VkRenderPassBeginInfo point_shadow_rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            point_shadow_rpbi.renderPass = state_->point_shadow_render_pass;
            point_shadow_rpbi.framebuffer = gbuffer.point_shadow_framebuffers[face_index];
            point_shadow_rpbi.renderArea.extent = {
                static_cast<uint32_t>(state_->point_shadow_map_resolution),
                static_cast<uint32_t>(state_->point_shadow_map_resolution)
            };
            point_shadow_rpbi.clearValueCount = 2;
            point_shadow_rpbi.pClearValues = point_shadow_clears;

            vkCmdBeginRenderPass(cmd, &point_shadow_rpbi, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdSetViewport(cmd, 0, 1, &point_shadow_viewport);
            vkCmdSetScissor(cmd, 0, 1, &point_shadow_scissor);
            vkCmdSetDepthBias(cmd, 2.0f, 0.0f, 2.0f);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->point_shadow_pipeline);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->prepass_pipeline_layout,
                0, 1, &frame_res.prepass_descriptor_set, 0, nullptr);

            const MeshBuffers* last_point_shadow_mesh = nullptr;
            for (uint32_t shadow_index = 0; shadow_index < shadow_opaque_count; ++shadow_index)
            {
                const SceneObject& obj = scene_.objects[shadow_index];
                if (!object_casts_shadow(obj))
                    continue;

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
                case MeshId::Custom:
                    if (obj.custom_mesh_index < state_->custom_meshes.size())
                        mesh = &state_->custom_meshes[obj.custom_mesh_index];
                    break;
                case MeshId::Grid:
                    break;
                }
                if (!mesh || mesh->index_count == 0)
                    continue;

                if (mesh != last_point_shadow_mesh)
                {
                    VkBuffer vertex_buffer = mesh->vertices.buffer;
                    VkDeviceSize vertex_offset = 0;
                    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &vertex_offset);
                    vkCmdBindIndexBuffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT16);
                    last_point_shadow_mesh = mesh;
                }

                ObjectPushConstants push;
                push.world = obj.world;
                push.color = obj.color;
                push.material_data = glm::uvec4(obj.material_index, 0u, face_index, 0u);
                push.uv_rect = obj.uv_rect;
                push.label_metrics = glm::vec4(0.0f);
                vkCmdPushConstants(
                    cmd, state_->prepass_pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
                vkCmdDrawIndexed(cmd, mesh->index_count, 1, mesh->first_index, mesh->vertex_offset, 0);
            }

            vkCmdEndRenderPass(cmd);
        }
    }

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

    // Draw scene objects into GBuffer (opaque only — transparent objects must not
    // contribute to depth/normals so that AO ignores faded geometry).
    const uint32_t gbuffer_opaque_count = std::min(scene_.opaque_count,
        static_cast<uint32_t>(scene_.objects.size()));
    const MeshBuffers* last_mesh = nullptr;
    for (uint32_t gi = 0; gi < gbuffer_opaque_count; ++gi)
    {
        const SceneObject& obj = scene_.objects[gi];
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
        case MeshId::Custom:
            if (obj.custom_mesh_index < state_->custom_meshes.size())
                mesh = &state_->custom_meshes[obj.custom_mesh_index];
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
        vkCmdDrawIndexed(cmd, mesh->index_count, 1, mesh->first_index, mesh->vertex_offset, 0);
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

    if (gbuffer.scene_framebuffer == VK_NULL_HANDLE || gbuffer.scene_post_framebuffer == VK_NULL_HANDLE)
        return;

    const int debug_mode = static_cast<int>(scene_.camera.debug_view.x + 0.5f);
    if (debug_mode == 1 && gbuffer.ao_raw_view != VK_NULL_HANDLE)
    {
        VkDescriptorImageInfo raw_ao_info = {};
        raw_ao_info.sampler = state_->gbuffer_sampler;
        raw_ao_info.imageView = gbuffer.ao_raw_view;
        raw_ao_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = frame_res.descriptor_set;
        write.dstBinding = 3;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &raw_ao_info;
        vkUpdateDescriptorSets(vk_ctx->device(), 1, &write, 0, nullptr);
    }

    VkClearValue scene_clears[3] = {};
    scene_clears[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
    scene_clears[1].depthStencil = { 1.0f, 0u };
    scene_clears[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

    VkRenderPassBeginInfo scene_rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    scene_rpbi.renderPass = state_->scene_render_pass;
    scene_rpbi.framebuffer = gbuffer.scene_framebuffer;
    scene_rpbi.renderArea.extent = { static_cast<uint32_t>(vw), static_cast<uint32_t>(vh) };
    scene_rpbi.clearValueCount = 3;
    scene_rpbi.pClearValues = scene_clears;
    vkCmdBeginRenderPass(cmd, &scene_rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    const bool use_debug = debug_mode > 0 && state_->debug_pipeline != VK_NULL_HANDLE;
    const bool use_wireframe = scene_.camera.debug_view.w > 0.5f;
    VkPipeline bound_pipeline = state_->pipeline;
    if (use_debug && use_wireframe && state_->debug_wireframe_pipeline != VK_NULL_HANDLE)
        bound_pipeline = state_->debug_wireframe_pipeline;
    else if (use_debug)
        bound_pipeline = state_->debug_pipeline;
    else if (use_wireframe && state_->wireframe_pipeline != VK_NULL_HANDLE)
        bound_pipeline = state_->wireframe_pipeline;
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bound_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->pipeline_layout,
        0, 1, &frame_res.descriptor_set, 0, nullptr);

    const MeshBuffers* last_scene_mesh = nullptr;
    auto draw_scene_object = [&](const SceneObject& obj) {
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
        case MeshId::Custom:
            if (obj.custom_mesh_index < state_->custom_meshes.size())
                mesh = &state_->custom_meshes[obj.custom_mesh_index];
            break;
        case MeshId::Grid:
            return;
        }
        if (!mesh || mesh->index_count == 0)
            return;

        if (mesh != last_scene_mesh)
        {
            VkBuffer vertex_buffer = mesh->vertices.buffer;
            VkDeviceSize vertex_offset = 0;
            vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &vertex_offset);
            vkCmdBindIndexBuffer(cmd, mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT16);
            last_scene_mesh = mesh;
        }

        ObjectPushConstants push;
        push.world = obj.world;
        push.color = obj.color;
        push.material_data = glm::uvec4(obj.material_index, 0u, 0u, 0u);
        push.uv_rect = obj.uv_rect;
        push.label_metrics = glm::vec4(
            obj.label_ink_pixel_size.x,
            obj.label_ink_pixel_size.y,
            static_cast<float>(obj.performance_heat_offset),
            static_cast<float>(obj.performance_heat_count));
        vkCmdPushConstants(cmd, state_->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, mesh->index_count, 1, mesh->first_index, mesh->vertex_offset, 0);
    };

    // Draw opaque objects with depth write enabled.
    const uint32_t scene_opaque_count = std::min(scene_.opaque_count,
        static_cast<uint32_t>(scene_.objects.size()));
    for (uint32_t i = 0; i < scene_opaque_count; ++i)
        draw_scene_object(scene_.objects[i]);

    // Draw transparent objects with depth test but no depth write (back-to-front).
    if (scene_opaque_count < scene_.objects.size()
        && state_->pipeline_no_depth_write != VK_NULL_HANDLE && !use_debug && !use_wireframe)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->pipeline_no_depth_write);
        for (uint32_t i = scene_opaque_count; i < scene_.objects.size(); ++i)
            draw_scene_object(scene_.objects[i]);
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

    vkCmdEndRenderPass(cmd);

    VkClearValue scene_post_clear = {};
    scene_post_clear.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

    VkRenderPassBeginInfo scene_post_rpbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    scene_post_rpbi.renderPass = state_->scene_post_render_pass;
    scene_post_rpbi.framebuffer = gbuffer.scene_post_framebuffer;
    scene_post_rpbi.renderArea.extent = { static_cast<uint32_t>(vw), static_cast<uint32_t>(vh) };
    scene_post_rpbi.clearValueCount = 1;
    scene_post_rpbi.pClearValues = &scene_post_clear;
    vkCmdBeginRenderPass(cmd, &scene_post_rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->post_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->post_pipeline_layout,
        0, 1, &frame_res.post_descriptor_set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    if (debug_mode == 1 && gbuffer.ao_view != VK_NULL_HANDLE)
    {
        VkDescriptorImageInfo ao_info = {};
        ao_info.sampler = state_->gbuffer_sampler;
        ao_info.imageView = gbuffer.ao_view;
        ao_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = frame_res.descriptor_set;
        write.dstBinding = 3;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &ao_info;
        vkUpdateDescriptorSets(vk_ctx->device(), 1, &write, 0, nullptr);
    }
}

void IsometricScenePass::record(IRenderContext& ctx)
{
    PERF_MEASURE();
    auto* vk_ctx = static_cast<VkRenderContext*>(&ctx);
    auto cmd = vk_ctx->command_buffer();
    if (!cmd)
        return;

    const uint32_t frame_index = vk_ctx->frame_index();
    if (!state_->ensure(*vk_ctx))
        return;
    if (!state_->ensure_gbuffer_targets(std::max(1u, vk_ctx->buffered_frame_count()), ctx.viewport_w(), ctx.viewport_h()))
        return;
    if (frame_index >= state_->frame_resources.size() || frame_index >= state_->gbuffer_targets.size())
        return;
    const auto& gbuffer = state_->gbuffer_targets[frame_index];
    auto& frame_resources = state_->frame_resources[frame_index];
    if (gbuffer.scene_final_unorm_view == VK_NULL_HANDLE)
        return;

    VkViewport viewport = {};
    viewport.x = static_cast<float>(ctx.viewport_x());
    viewport.y = static_cast<float>(ctx.viewport_y());
    viewport.width = static_cast<float>(ctx.viewport_w());
    viewport.height = static_cast<float>(ctx.viewport_h());
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = { ctx.viewport_x(), ctx.viewport_y() };
    scissor.extent = {
        static_cast<uint32_t>(std::max(ctx.viewport_w(), 0)),
        static_cast<uint32_t>(std::max(ctx.viewport_h(), 0))
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->present_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->post_pipeline_layout,
        0, 1, &frame_resources.post_descriptor_set, 0, nullptr);
    vkCmdDraw(cmd, 3, 1, 0, 0);

    // Draw tooltip overlay if visible.
    if (scene_.tooltip.valid() && state_->tooltip_initialized
        && state_->tooltip_pipeline != VK_NULL_HANDLE
        && frame_resources.tooltip_descriptor_set != VK_NULL_HANDLE
        && frame_resources.tooltip_image.view != VK_NULL_HANDLE
        && frame_resources.tooltip_texture_revision == scene_.tooltip.revision)
    {
        struct TooltipPush
        {
            glm::vec4 rect;
            glm::vec4 viewport;
        };

        const float full_w = static_cast<float>(ctx.width());
        const float full_h = static_cast<float>(ctx.height());

        TooltipPush push;
        push.rect = glm::vec4(
            scene_.tooltip.screen_pos.x,
            scene_.tooltip.screen_pos.y,
            static_cast<float>(scene_.tooltip.width),
            static_cast<float>(scene_.tooltip.height));
        push.viewport = glm::vec4(full_w, full_h, 0.0f, 0.0f);

        // Use full-window viewport for screen-space positioning.
        VkViewport full_viewport = {};
        full_viewport.width = full_w;
        full_viewport.height = full_h;
        full_viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &full_viewport);

        VkRect2D full_scissor = {};
        full_scissor.extent = {
            static_cast<uint32_t>(ctx.width()),
            static_cast<uint32_t>(ctx.height())
        };
        vkCmdSetScissor(cmd, 0, 1, &full_scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->tooltip_pipeline);
        vkCmdPushConstants(cmd, state_->tooltip_pipeline_layout,
            VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->tooltip_pipeline_layout,
            0, 1, &frame_resources.tooltip_descriptor_set, 0, nullptr);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }
}

void IsometricScenePass::render_gbuffer_debug_ui()
{
    PERF_MEASURE();
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
    for (size_t cascade_index = 0; cascade_index < kShadowCascadeCount; ++cascade_index)
    {
        if (t.imgui_shadow_ds[cascade_index] == VK_NULL_HANDLE && t.shadow_views[cascade_index] != VK_NULL_HANDLE)
        {
            t.imgui_shadow_ds[cascade_index] = ImGui_ImplVulkan_AddTexture(
                state_->gbuffer_point_sampler,
                t.shadow_views[cascade_index],
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
        }
    }
    for (size_t face_index = 0; face_index < kPointShadowFaceCount; ++face_index)
    {
        if (t.imgui_point_shadow_ds[face_index] == VK_NULL_HANDLE
            && t.point_shadow_face_views[face_index] != VK_NULL_HANDLE)
        {
            t.imgui_point_shadow_ds[face_index] = ImGui_ImplVulkan_AddTexture(
                state_->gbuffer_sampler,
                t.point_shadow_face_views[face_index],
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }
    if (t.imgui_scene_hdr_ds == VK_NULL_HANDLE && t.scene_hdr_view != VK_NULL_HANDLE)
    {
        t.imgui_scene_hdr_ds = ImGui_ImplVulkan_AddTexture(
            state_->gbuffer_sampler, t.scene_hdr_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    if (t.imgui_scene_final_ds == VK_NULL_HANDLE && t.scene_final_unorm_view != VK_NULL_HANDLE)
    {
        t.imgui_scene_final_ds = ImGui_ImplVulkan_AddTexture(
            state_->gbuffer_sampler, t.scene_final_unorm_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
    const float cell_h = std::max(32.0f, (avail.y - text_h * 5.0f) * 0.5f);
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

        ImGui::TableNextColumn();
        ImGui::Text("Scene HDR");
        if (t.imgui_scene_hdr_ds != VK_NULL_HANDLE)
            ImGui::Image(static_cast<ImTextureID>(t.imgui_scene_hdr_ds), size);

        ImGui::TableNextColumn();
        ImGui::Text("Scene Final");
        if (t.imgui_scene_final_ds != VK_NULL_HANDLE)
            ImGui::Image(static_cast<ImTextureID>(t.imgui_scene_final_ds), size, ImVec2(0, 1), ImVec2(1, 0));

        for (size_t cascade_index = 0; cascade_index < kShadowCascadeCount; ++cascade_index)
        {
            ImGui::TableNextColumn();
            ImGui::Text("Shadow %u", static_cast<unsigned>(cascade_index));
            if (t.imgui_shadow_ds[cascade_index] != VK_NULL_HANDLE)
                ImGui::Image(static_cast<ImTextureID>(t.imgui_shadow_ds[cascade_index]), size);
        }

        for (size_t face_index = 0; face_index < kPointShadowFaceCount; ++face_index)
        {
            ImGui::TableNextColumn();
            ImGui::Text("Point %u", static_cast<unsigned>(face_index));
            if (t.imgui_point_shadow_ds[face_index] != VK_NULL_HANDLE)
                ImGui::Image(static_cast<ImTextureID>(t.imgui_point_shadow_ds[face_index]), size);
        }

        ImGui::EndTable();
    }

    ImGui::Text("Size: %dx%d", t.width, t.height);
    ImGui::End();
}

} // namespace draxul
