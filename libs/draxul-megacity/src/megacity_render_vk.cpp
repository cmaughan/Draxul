#include "isometric_scene_pass.h"

#include "mesh_library.h"
#include "vk_render_context.h"
#include <algorithm>
#include <cstring>
#include <draxul/log.h>
#include <draxul/runtime_path.h>
#include <fstream>
#include <vector>

namespace draxul
{

namespace
{

struct alignas(16) FrameUniforms
{
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f };
    glm::vec4 light_dir{ -0.5f, -1.0f, -0.3f, 0.0f };
    glm::vec4 point_light_pos{ 0.0f, 8.0f, 0.0f, 24.0f };
    glm::vec4 label_fade_px{ 1.0f, 15.0f, 0.0f, 0.0f };
    glm::vec4 render_tuning{ 1.0f, 1.0f, 0.45f, 0.0f };
};

struct alignas(16) ObjectPushConstants
{
    glm::mat4 world{ 1.0f };
    glm::vec4 color{ 1.0f };
    glm::vec4 uv_rect{ 0.0f, 0.0f, 1.0f, 1.0f };
    glm::vec4 label_metrics{ 0.0f };
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
    int size = 0;
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
    TransientGeometryArena geometry_arena;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
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

bool create_label_image(VkPhysicalDevice physical_device, VkDevice device, VmaAllocator allocator, int size,
    ImageResource& image)
{
    VkImageCreateInfo img_ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    img_ci.imageType = VK_IMAGE_TYPE_2D;
    img_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    img_ci.extent = { static_cast<uint32_t>(size), static_cast<uint32_t>(size), 1 };
    img_ci.mipLevels = 1;
    img_ci.arrayLayers = 1;
    img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &view_ci, nullptr, &image.view) != VK_SUCCESS)
        return false;

    VkSamplerCreateInfo sampler_ci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampler_ci.magFilter = VK_FILTER_LINEAR;
    sampler_ci.minFilter = VK_FILTER_LINEAR;
    sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.maxLod = 1.0f;
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

    image.size = size;
    return true;
}

void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout)
{
    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
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

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create shader module %s", path.c_str());
        return VK_NULL_HANDLE;
    }
    return module;
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
    VkPipeline pipeline = VK_NULL_HANDLE;
    MeshBuffers cube_mesh;
    MeshBuffers floor_mesh;
    MeshBuffers roof_sign_mesh;
    MeshBuffers wall_sign_mesh;
    MeshData cached_grid_mesh;
    FloorGridSpec cached_grid_spec;
    bool has_cached_grid_mesh = false;
    ImageResource label_atlas;
    Buffer label_staging;
    VkImageLayout label_atlas_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint64_t label_atlas_revision = 0;
    std::vector<FrameResources> frame_resources;
    uint32_t buffered_frame_count = 1;

    bool create_device_resources(uint32_t frame_count)
    {
        VkDescriptorSetLayoutBinding bindings[2] = {};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layout_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layout_ci.bindingCount = 2;
        layout_ci.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &descriptor_set_layout) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create descriptor set layout");
            return false;
        }

        VkDescriptorPoolSize pool_sizes[2] = {};
        pool_sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount = frame_count;
        pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_sizes[1].descriptorCount = frame_count;

        VkDescriptorPoolCreateInfo pool_ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool_ci.maxSets = frame_count;
        pool_ci.poolSizeCount = 2;
        pool_ci.pPoolSizes = pool_sizes;
        if (vkCreateDescriptorPool(device, &pool_ci, nullptr, &descriptor_pool) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create descriptor pool");
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

        for (uint32_t i = 0; i < frame_count; ++i)
        {
            auto& frame = frame_resources[i];
            frame.descriptor_set = descriptor_sets[i];

            if (!create_mapped_buffer(allocator, sizeof(FrameUniforms),
                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, frame.frame_uniforms))
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "MegaCity scene: failed to create frame uniform buffer");
                return false;
            }

            VkDescriptorBufferInfo buffer_info = {};
            buffer_info.buffer = frame.frame_uniforms.buffer;
            buffer_info.range = sizeof(FrameUniforms);

            VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
            write.dstSet = frame.descriptor_set;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.pBufferInfo = &buffer_info;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }

        buffered_frame_count = frame_count;

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
            write.dstBinding = 1;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &image_info;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
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

        VkVertexInputAttributeDescription attributes[5] = {};
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

        VkPipelineVertexInputStateCreateInfo vertex_input = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &binding;
        vertex_input.vertexAttributeDescriptionCount = 5;
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

    void destroy()
    {
        if (allocator != VK_NULL_HANDLE)
        {
            destroy_mesh(allocator, cube_mesh);
            destroy_mesh(allocator, floor_mesh);
            destroy_mesh(allocator, roof_sign_mesh);
            destroy_mesh(allocator, wall_sign_mesh);
            destroy_image(device, allocator, label_atlas);
            destroy_buffer(allocator, label_staging);
            for (auto& frame : frame_resources)
            {
                destroy_buffer(allocator, frame.geometry_arena.vertices.buffer);
                destroy_buffer(allocator, frame.geometry_arena.indices.buffer);
                destroy_buffer(allocator, frame.frame_uniforms);
            }
        }
        if (pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, pipeline, nullptr);
        if (pipeline_layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        if (descriptor_pool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
        if (descriptor_set_layout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);

        device = VK_NULL_HANDLE;
        allocator = VK_NULL_HANDLE;
        render_pass = VK_NULL_HANDLE;
        descriptor_set_layout = VK_NULL_HANDLE;
        descriptor_pool = VK_NULL_HANDLE;
        pipeline_layout = VK_NULL_HANDLE;
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
    state_->destroy();
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
    frame.light_dir = scene_.camera.light_dir;
    frame.point_light_pos = scene_.camera.point_light_pos;
    frame.label_fade_px = scene_.camera.label_fade_px;
    frame.render_tuning = scene_.camera.render_tuning;
    std::memcpy(frame_resources.frame_uniforms.mapped, &frame, sizeof(frame));
    vmaFlushAllocation(vk_ctx->allocator(), frame_resources.frame_uniforms.allocation, 0, sizeof(frame));

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->pipeline);
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
        push.uv_rect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);

        VkBuffer vertex_buffer = grid_slice.vertex_buffer;
        VkDeviceSize vertex_offset = grid_slice.vertex_offset;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffer, &vertex_offset);
        vkCmdBindIndexBuffer(cmd, grid_slice.index_buffer, grid_slice.index_offset, VK_INDEX_TYPE_UINT16);
        vkCmdPushConstants(cmd, state_->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
        vkCmdDrawIndexed(cmd, grid_slice.index_count, 1, 0, 0, 0);
    }
}

} // namespace draxul
