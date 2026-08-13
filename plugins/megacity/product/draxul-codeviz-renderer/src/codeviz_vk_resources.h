#pragma once

#include "codeviz_material_library.h"
#include "vk_resource_helpers.h"

#include <array>
#include <cstddef>
#include <draxul/codeviz_scene_types.h>
#include <string>
#include <string_view>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace draxul::codeviz_vk
{

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

    void reset() { head = 0; }
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

void destroy_buffer(VmaAllocator allocator, Buffer& buffer);
bool create_mapped_buffer(VkDevice device, VmaAllocator allocator, size_t size,
    VkBufferUsageFlags usage, vkresources::LifetimeScope lifetime,
    std::string_view debug_name, Buffer& buffer);
bool upload_mesh(VkDevice device, VmaAllocator allocator, const MeshData& mesh,
    std::string_view debug_name, MeshBuffers& gpu_mesh);
size_t align_up(size_t value, size_t alignment);
size_t grow_capacity(size_t current_size, size_t required_size, size_t minimum_size);
bool ensure_mapped_buffer_capacity(VkDevice device, VmaAllocator allocator, size_t required_size,
    VkBufferUsageFlags usage, vkresources::LifetimeScope lifetime,
    std::string_view debug_name, Buffer& buffer, size_t minimum_size);
bool reserve_transient_buffer(VkDevice device, VmaAllocator allocator,
    TransientBufferArena& arena, size_t size, size_t alignment,
    VkBufferUsageFlags usage, size_t minimum_size, std::string_view debug_name,
    BufferSlice& slice);
bool stream_transient_mesh(VkDevice device, VmaAllocator allocator, const MeshData& mesh,
    TransientGeometryArena& arena, MeshSlice& slice);
void destroy_mesh(VmaAllocator allocator, MeshBuffers& mesh);

void destroy_image(VkDevice device, VmaAllocator allocator, ImageResource& image);
bool create_sampled_image(VkPhysicalDevice physical_device, VkDevice device, VmaAllocator allocator,
    int width, int height, VkFormat format, VkSamplerAddressMode address_mode,
    bool generate_mips, VkImageLayout final_layout,
    vkresources::LifetimeScope lifetime, std::string_view debug_name,
    ImageResource& image);
bool create_label_image(VkPhysicalDevice physical_device, VkDevice device, VmaAllocator allocator,
    int width, int height, ImageResource& image);
VkSampleCountFlagBits choose_scene_sample_count(VkPhysicalDevice physical_device);
bool create_attachment_image(VkDevice device, VmaAllocator allocator, int width, int height,
    VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect_mask,
    VkSampleCountFlagBits samples, VkImageCreateFlags flags, VkImageLayout final_layout,
    vkresources::LifetimeScope lifetime, std::string_view debug_name,
    VkImage& image, VmaAllocation& allocation, VkImageView& view);
bool create_attachment_view(VkDevice device, VkImage image, VkFormat format,
    VkImageAspectFlags aspect_mask, std::string_view debug_name, VkImageView& view);
bool create_cube_attachment_image(VkDevice device, VmaAllocator allocator, int size,
    VkFormat format, VkImageUsageFlags usage, VkImageLayout final_layout,
    vkresources::LifetimeScope lifetime, std::string_view debug_name,
    VkImage& image, VmaAllocation& allocation, VkImageView& cube_view,
    std::array<VkImageView, kPointShadowFaceCount>& face_views);
void transition_image(VkCommandBuffer command_buffer, VkImage image,
    VkImageLayout old_layout, VkImageLayout new_layout,
    VkAccessFlags src_access, VkAccessFlags dst_access,
    VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
    uint32_t base_mip_level, uint32_t level_count);
bool generate_mipmaps(VkCommandBuffer command_buffer, const ImageResource& target);
bool upload_rgba_texture(VkDevice device, VmaAllocator allocator,
    VkCommandBuffer command_buffer, Buffer& staging, size_t staging_offset,
    const LoadedTextureImage& image, ImageResource& target);
VkShaderModule load_shader(VkDevice device, const std::string& path);

} // namespace draxul::codeviz_vk
