#include "satview_scene_pass.h"
#include "satview_render_vk_math.h"
#include "satview_texture_assets.h"

#include <draxul/log.h>
#include <draxul/perf_timing.h>
#include <draxul/runtime_path.h>
#include <draxul/vulkan/vk_render_context.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

namespace draxul::satview
{

namespace
{

constexpr size_t kEarthTextureCount = 3;
constexpr uint32_t kSatViewSamplerCount = 5;
constexpr uint32_t kBundledCloudTextureIndex = 2;
constexpr uint32_t kLiveCloudTextureBinding = 3;
constexpr uint32_t kMoonTextureBinding = 4;

struct BufferResource
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mapped = nullptr;
    size_t size = 0;
};

struct TextureResource
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    int width = 0;
    int height = 0;
};

VkShaderModule load_shader(VkDevice device, const std::string& path, bool required = true)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
    {
        if (required)
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to open shader %s", path.c_str());
        else
            DRAXUL_LOG_WARN(LogCategory::Renderer, "SatView: optional shader not available %s", path.c_str());
        return VK_NULL_HANDLE;
    }

    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(size));

    VkShaderModuleCreateInfo ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    ci.codeSize = size;
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &module) != VK_SUCCESS)
    {
        if (required)
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create shader module %s", path.c_str());
        else
            DRAXUL_LOG_WARN(LogCategory::Renderer, "SatView: optional shader module unavailable %s", path.c_str());
        return VK_NULL_HANDLE;
    }
    return module;
}

void destroy_buffer(VmaAllocator allocator, BufferResource& buffer)
{
    if (buffer.buffer != VK_NULL_HANDLE)
        vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    buffer = {};
}

void destroy_texture(VkDevice device, VmaAllocator allocator, TextureResource& texture)
{
    if (texture.sampler != VK_NULL_HANDLE)
        vkDestroySampler(device, texture.sampler, nullptr);
    if (texture.view != VK_NULL_HANDLE)
        vkDestroyImageView(device, texture.view, nullptr);
    if (texture.image != VK_NULL_HANDLE)
        vmaDestroyImage(allocator, texture.image, texture.allocation);
    texture = {};
}

bool create_staging_buffer(VmaAllocator allocator, const LoadedTextureImage& image, BufferResource& buffer)
{
    PERF_MEASURE();
    if (!image.valid())
        return false;

    const size_t byte_size = image.rgba.size();
    VkBufferCreateInfo buf_ci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buf_ci.size = byte_size;
    buf_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_info{};
    if (vmaCreateBuffer(allocator, &buf_ci, &alloc_ci, &buffer.buffer, &buffer.allocation, &alloc_info) != VK_SUCCESS)
        return false;

    buffer.mapped = alloc_info.pMappedData;
    buffer.size = byte_size;
    if (!buffer.mapped)
    {
        destroy_buffer(allocator, buffer);
        return false;
    }

    std::memcpy(buffer.mapped, image.rgba.data(), byte_size);
    vmaFlushAllocation(allocator, buffer.allocation, 0, byte_size);
    return true;
}

bool create_texture_resource(VkPhysicalDevice physical_device, VkDevice device, VmaAllocator allocator,
    const LoadedTextureImage& image, TextureResource& texture)
{
    PERF_MEASURE();
    VkImageCreateInfo image_ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_ci.imageType = VK_IMAGE_TYPE_2D;
    image_ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    image_ci.extent = {
        static_cast<uint32_t>(image.width),
        static_cast<uint32_t>(image.height),
        1u
    };
    image_ci.mipLevels = 1;
    image_ci.arrayLayers = 1;
    image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_ci{};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(allocator, &image_ci, &alloc_ci, &texture.image, &texture.allocation, nullptr) != VK_SUCCESS)
        return false;

    VkImageViewCreateInfo view_ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view_ci.image = texture.image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = image_ci.format;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &view_ci, nullptr, &texture.view) != VK_SUCCESS)
    {
        destroy_texture(device, allocator, texture);
        return false;
    }

    VkSamplerCreateInfo sampler_ci{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    sampler_ci.magFilter = VK_FILTER_LINEAR;
    sampler_ci.minFilter = VK_FILTER_LINEAR;
    sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_ci.maxLod = 0.0f;

    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(physical_device, &features);
    if (features.samplerAnisotropy)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_device, &properties);
        sampler_ci.anisotropyEnable = VK_TRUE;
        sampler_ci.maxAnisotropy = std::min(8.0f, properties.limits.maxSamplerAnisotropy);
    }

    if (vkCreateSampler(device, &sampler_ci, nullptr, &texture.sampler) != VK_SUCCESS)
    {
        destroy_texture(device, allocator, texture);
        return false;
    }

    texture.width = image.width;
    texture.height = image.height;
    return true;
}

void transition_texture(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout)
{
    VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
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
    if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED
        && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

bool upload_textures_immediate(const VkRenderContext& ctx,
    const std::array<LoadedTextureImage, kEarthTextureCount>& images,
    std::array<TextureResource, kEarthTextureCount>& textures)
{
    PERF_MEASURE();
    VkDevice device = ctx.device();
    VmaAllocator allocator = ctx.allocator();
    if (ctx.graphics_queue() == VK_NULL_HANDLE)
        return false;

    std::array<BufferResource, kEarthTextureCount> staging{};
    for (size_t index = 0; index < images.size(); ++index)
    {
        if (!create_staging_buffer(allocator, images[index], staging[index])
            || !create_texture_resource(ctx.physical_device(), device, allocator, images[index], textures[index]))
        {
            for (auto& buffer : staging)
                destroy_buffer(allocator, buffer);
            for (auto& texture : textures)
                destroy_texture(device, allocator, texture);
            return false;
        }
    }

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pool_ci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pool_ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_ci.queueFamilyIndex = ctx.graphics_queue_family();
    if (vkCreateCommandPool(device, &pool_ci, nullptr, &command_pool) != VK_SUCCESS)
    {
        for (auto& buffer : staging)
            destroy_buffer(allocator, buffer);
        for (auto& texture : textures)
            destroy_texture(device, allocator, texture);
        return false;
    }

    VkCommandBuffer upload_cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo alloc_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &alloc_info, &upload_cmd) != VK_SUCCESS)
    {
        vkDestroyCommandPool(device, command_pool, nullptr);
        for (auto& buffer : staging)
            destroy_buffer(allocator, buffer);
        for (auto& texture : textures)
            destroy_texture(device, allocator, texture);
        return false;
    }

    VkCommandBufferBeginInfo begin_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(upload_cmd, &begin_info) != VK_SUCCESS)
    {
        vkDestroyCommandPool(device, command_pool, nullptr);
        for (auto& buffer : staging)
            destroy_buffer(allocator, buffer);
        for (auto& texture : textures)
            destroy_texture(device, allocator, texture);
        return false;
    }

    for (size_t index = 0; index < textures.size(); ++index)
    {
        transition_texture(upload_cmd, textures[index].image,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {
            static_cast<uint32_t>(images[index].width),
            static_cast<uint32_t>(images[index].height),
            1u
        };
        vkCmdCopyBufferToImage(upload_cmd, staging[index].buffer, textures[index].image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        transition_texture(upload_cmd, textures[index].image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    VkResult result = vkEndCommandBuffer(upload_cmd);
    if (result == VK_SUCCESS)
    {
        VkSubmitInfo submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &upload_cmd;
        result = vkQueueSubmit(ctx.graphics_queue(), 1, &submit_info, VK_NULL_HANDLE);
    }
    if (result == VK_SUCCESS)
        result = vkQueueWaitIdle(ctx.graphics_queue());

    vkDestroyCommandPool(device, command_pool, nullptr);
    for (auto& buffer : staging)
        destroy_buffer(allocator, buffer);
    if (result != VK_SUCCESS)
    {
        for (auto& texture : textures)
            destroy_texture(device, allocator, texture);
        return false;
    }
    return true;
}

bool upload_texture_immediate(const VkRenderContext& ctx,
    const LoadedTextureImage& image,
    TextureResource& texture,
    VkImageLayout old_layout)
{
    PERF_MEASURE();
    if (!image.valid() || texture.image == VK_NULL_HANDLE
        || image.width != texture.width || image.height != texture.height
        || ctx.graphics_queue() == VK_NULL_HANDLE)
    {
        return false;
    }

    VkDevice device = ctx.device();
    VmaAllocator allocator = ctx.allocator();
    BufferResource staging;
    if (!create_staging_buffer(allocator, image, staging))
        return false;

    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo pool_ci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pool_ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_ci.queueFamilyIndex = ctx.graphics_queue_family();
    if (vkCreateCommandPool(device, &pool_ci, nullptr, &command_pool) != VK_SUCCESS)
    {
        destroy_buffer(allocator, staging);
        return false;
    }

    VkCommandBuffer upload_cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo alloc_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    alloc_info.commandPool = command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    VkResult result = vkAllocateCommandBuffers(device, &alloc_info, &upload_cmd);
    if (result == VK_SUCCESS)
    {
        VkCommandBufferBeginInfo begin_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        result = vkBeginCommandBuffer(upload_cmd, &begin_info);
    }
    if (result == VK_SUCCESS)
    {
        transition_texture(upload_cmd, texture.image, old_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {
            static_cast<uint32_t>(image.width),
            static_cast<uint32_t>(image.height),
            1u
        };
        vkCmdCopyBufferToImage(upload_cmd, staging.buffer, texture.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        transition_texture(upload_cmd, texture.image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        result = vkEndCommandBuffer(upload_cmd);
    }
    if (result == VK_SUCCESS)
    {
        VkSubmitInfo submit_info{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &upload_cmd;
        result = vkQueueSubmit(ctx.graphics_queue(), 1, &submit_info, VK_NULL_HANDLE);
    }
    if (result == VK_SUCCESS)
        result = vkQueueWaitIdle(ctx.graphics_queue());

    vkDestroyCommandPool(device, command_pool, nullptr);
    destroy_buffer(allocator, staging);
    return result == VK_SUCCESS;
}

bool create_texture_immediate(
    const VkRenderContext& ctx, const LoadedTextureImage& image, TextureResource& texture)
{
    if (!create_texture_resource(ctx.physical_device(), ctx.device(), ctx.allocator(), image, texture))
        return false;
    if (upload_texture_immediate(ctx, image, texture, VK_IMAGE_LAYOUT_UNDEFINED))
        return true;
    destroy_texture(ctx.device(), ctx.allocator(), texture);
    return false;
}

bool update_texture_immediate(
    const VkRenderContext& ctx, const LoadedTextureImage& image, TextureResource& texture)
{
    return upload_texture_immediate(
        ctx, image, texture, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

} // namespace

struct SatViewScenePass::State
{
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
    VkPipeline earth_pipeline = VK_NULL_HANDLE;
    VkPipeline moon_pipeline = VK_NULL_HANDLE;
    VkPipeline cloud_pipeline = VK_NULL_HANDLE;
    VkPipeline atmosphere_pipeline = VK_NULL_HANDLE;
    VkPipeline ground_atmosphere_pipeline = VK_NULL_HANDLE;
    VkPipeline ground_surface_pipeline = VK_NULL_HANDLE;
    VkPipeline star_pipeline = VK_NULL_HANDLE;
    VkPipeline orbit_pipeline = VK_NULL_HANDLE;
    VkPipeline marker_pipeline = VK_NULL_HANDLE;
    std::array<TextureResource, kEarthTextureCount> earth_textures{};
    TextureResource moon_texture;
    TextureResource live_cloud_texture;
    BufferResource track_vertex_buffer;
    BufferResource marker_buffer;
    BufferResource star_buffer;
    uint32_t track_vertex_count = 0;
    uint32_t marker_count = 0;
    uint32_t star_count = 0;
    uint64_t uploaded_track_revision = 0;
    uint64_t uploaded_marker_revision = 0;
    uint64_t uploaded_star_revision = 0;
    uint64_t uploaded_cloud_revision = 0;

    ~State()
    {
        destroy();
    }

    void destroy_pipelines()
    {
        if (device != VK_NULL_HANDLE)
        {
            if (earth_pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(device, earth_pipeline, nullptr);
            if (moon_pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(device, moon_pipeline, nullptr);
            if (cloud_pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(device, cloud_pipeline, nullptr);
            if (atmosphere_pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(device, atmosphere_pipeline, nullptr);
            if (ground_atmosphere_pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(device, ground_atmosphere_pipeline, nullptr);
            if (ground_surface_pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(device, ground_surface_pipeline, nullptr);
            if (star_pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(device, star_pipeline, nullptr);
            if (orbit_pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(device, orbit_pipeline, nullptr);
            if (marker_pipeline != VK_NULL_HANDLE)
                vkDestroyPipeline(device, marker_pipeline, nullptr);
        }
        earth_pipeline = VK_NULL_HANDLE;
        moon_pipeline = VK_NULL_HANDLE;
        cloud_pipeline = VK_NULL_HANDLE;
        atmosphere_pipeline = VK_NULL_HANDLE;
        ground_atmosphere_pipeline = VK_NULL_HANDLE;
        ground_surface_pipeline = VK_NULL_HANDLE;
        star_pipeline = VK_NULL_HANDLE;
        orbit_pipeline = VK_NULL_HANDLE;
        marker_pipeline = VK_NULL_HANDLE;
    }

    void destroy()
    {
        if (device != VK_NULL_HANDLE)
        {
            destroy_pipelines();
            if (layout != VK_NULL_HANDLE)
                vkDestroyPipelineLayout(device, layout, nullptr);
            if (descriptor_pool != VK_NULL_HANDLE)
                vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
            if (descriptor_set_layout != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(device, descriptor_set_layout, nullptr);
            if (allocator != VK_NULL_HANDLE)
            {
                for (auto& texture : earth_textures)
                    destroy_texture(device, allocator, texture);
                destroy_texture(device, allocator, moon_texture);
                destroy_texture(device, allocator, live_cloud_texture);
                destroy_buffer(allocator, track_vertex_buffer);
                destroy_buffer(allocator, marker_buffer);
                destroy_buffer(allocator, star_buffer);
            }
        }
        layout = VK_NULL_HANDLE;
        descriptor_set_layout = VK_NULL_HANDLE;
        descriptor_pool = VK_NULL_HANDLE;
        descriptor_set = VK_NULL_HANDLE;
        render_pass = VK_NULL_HANDLE;
        allocator = VK_NULL_HANDLE;
        device = VK_NULL_HANDLE;
        track_vertex_count = 0;
        marker_count = 0;
        star_count = 0;
        uploaded_track_revision = 0;
        uploaded_marker_revision = 0;
        uploaded_star_revision = 0;
        uploaded_cloud_revision = 0;
    }

    bool ensure_texture_descriptors(const VkRenderContext& ctx)
    {
        if (descriptor_set != VK_NULL_HANDLE && device == ctx.device() && allocator == ctx.allocator())
            return true;

        if (device != VK_NULL_HANDLE && (device != ctx.device() || allocator != ctx.allocator()))
            destroy();

        device = ctx.device();
        allocator = ctx.allocator();
        if (device == VK_NULL_HANDLE || allocator == VK_NULL_HANDLE)
            return false;

        EarthTextureImages images = load_earth_texture_images();
        std::array<LoadedTextureImage, kEarthTextureCount> image_array = {
            std::move(images.day),
            std::move(images.night),
            std::move(images.clouds),
        };
        if (!upload_textures_immediate(ctx, image_array, earth_textures))
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to upload Earth textures");
            destroy();
            return false;
        }
        const LoadedTextureImage moon_image = load_moon_texture_image();
        if (!create_texture_immediate(ctx, moon_image, moon_texture))
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to upload Moon texture");
            destroy();
            return false;
        }

        VkDescriptorSetLayoutBinding bindings[kSatViewSamplerCount]{};
        for (uint32_t index = 0; index < kSatViewSamplerCount; ++index)
        {
            bindings[index].binding = index;
            bindings[index].descriptorCount = 1;
            bindings[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }

        VkDescriptorSetLayoutCreateInfo layout_ci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layout_ci.bindingCount = static_cast<uint32_t>(std::size(bindings));
        layout_ci.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &descriptor_set_layout) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create texture descriptor layout");
            destroy();
            return false;
        }

        VkPushConstantRange push_range{};
        push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        push_range.offset = 0;
        push_range.size = sizeof(SatViewFrameUniforms);

        VkPipelineLayoutCreateInfo pipeline_layout_ci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pipeline_layout_ci.setLayoutCount = 1;
        pipeline_layout_ci.pSetLayouts = &descriptor_set_layout;
        pipeline_layout_ci.pushConstantRangeCount = 1;
        pipeline_layout_ci.pPushConstantRanges = &push_range;
        if (vkCreatePipelineLayout(device, &pipeline_layout_ci, nullptr, &layout) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create pipeline layout");
            destroy();
            return false;
        }

        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = kSatViewSamplerCount;

        VkDescriptorPoolCreateInfo pool_ci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pool_ci.maxSets = 1;
        pool_ci.poolSizeCount = 1;
        pool_ci.pPoolSizes = &pool_size;
        if (vkCreateDescriptorPool(device, &pool_ci, nullptr, &descriptor_pool) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create texture descriptor pool");
            destroy();
            return false;
        }

        VkDescriptorSetAllocateInfo alloc_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc_info.descriptorPool = descriptor_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &descriptor_set_layout;
        if (vkAllocateDescriptorSets(device, &alloc_info, &descriptor_set) != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to allocate texture descriptor set");
            destroy();
            return false;
        }

        std::array<VkDescriptorImageInfo, kSatViewSamplerCount> image_infos{};
        std::array<VkWriteDescriptorSet, kSatViewSamplerCount> writes{};
        for (uint32_t index = 0; index < kSatViewSamplerCount; ++index)
        {
            const TextureResource& texture = index == kMoonTextureBinding
                ? moon_texture
                : (index == kLiveCloudTextureBinding
                        ? earth_textures[kBundledCloudTextureIndex]
                        : earth_textures[index]);
            image_infos[index].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            image_infos[index].imageView = texture.view;
            image_infos[index].sampler = texture.sampler;

            writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[index].dstSet = descriptor_set;
            writes[index].dstBinding = index;
            writes[index].descriptorCount = 1;
            writes[index].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[index].pImageInfo = &image_infos[index];
        }
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        return true;
    }

    bool ensure_cloud_texture(
        const VkRenderContext& ctx, const LoadedTextureImage& image, uint64_t revision)
    {
        if (revision == uploaded_cloud_revision)
            return true;

        bool uploaded = false;
        if (live_cloud_texture.image == VK_NULL_HANDLE
            || image.width != live_cloud_texture.width
            || image.height != live_cloud_texture.height)
        {
            destroy_texture(ctx.device(), ctx.allocator(), live_cloud_texture);
            uploaded = create_texture_immediate(ctx, image, live_cloud_texture);
        }
        else
        {
            uploaded = update_texture_immediate(ctx, image, live_cloud_texture);
        }
        if (!uploaded)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to upload live cloud texture");
            return false;
        }

        VkDescriptorImageInfo image_info{};
        image_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        image_info.imageView = live_cloud_texture.view;
        image_info.sampler = live_cloud_texture.sampler;
        VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        write.dstSet = descriptor_set;
        write.dstBinding = kLiveCloudTextureBinding;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &image_info;
        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        uploaded_cloud_revision = revision;
        return true;
    }

    template <typename T>
    bool ensure_vertex_buffer(const VkRenderContext& ctx,
        const std::vector<T>& items,
        uint64_t revision,
        BufferResource& buffer,
        uint32_t& item_count,
        uint64_t& uploaded_revision)
    {
        if (revision == uploaded_revision)
            return true;

        item_count = static_cast<uint32_t>(
            std::min<std::size_t>(items.size(), std::numeric_limits<uint32_t>::max()));
        if (item_count == 0)
        {
            uploaded_revision = revision;
            return true;
        }

        const size_t byte_size = static_cast<size_t>(item_count) * sizeof(T);
        if (buffer.buffer == VK_NULL_HANDLE || buffer.size < byte_size)
        {
            if (buffer.buffer != VK_NULL_HANDLE)
                destroy_buffer(ctx.allocator(), buffer);

            VkBufferCreateInfo buf_ci{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            buf_ci.size = byte_size;
            buf_ci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
            buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo alloc_ci{};
            alloc_ci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VmaAllocationInfo alloc_info{};
            if (vmaCreateBuffer(ctx.allocator(), &buf_ci, &alloc_ci,
                    &buffer.buffer, &buffer.allocation, &alloc_info) != VK_SUCCESS)
            {
                buffer = {};
                item_count = 0;
                return false;
            }

            buffer.mapped = alloc_info.pMappedData;
            buffer.size = byte_size;
        }

        if (!buffer.mapped)
        {
            item_count = 0;
            return false;
        }

        std::memcpy(buffer.mapped, items.data(), byte_size);
        vmaFlushAllocation(ctx.allocator(), buffer.allocation, 0, byte_size);
        uploaded_revision = revision;
        return true;
    }

    bool ensure_pipelines(VkDevice new_device, VkRenderPass new_render_pass)
    {
        if (earth_pipeline != VK_NULL_HANDLE && moon_pipeline != VK_NULL_HANDLE
            && cloud_pipeline != VK_NULL_HANDLE
            && atmosphere_pipeline != VK_NULL_HANDLE
            && ground_atmosphere_pipeline != VK_NULL_HANDLE
            && ground_surface_pipeline != VK_NULL_HANDLE
            && star_pipeline != VK_NULL_HANDLE
            && orbit_pipeline != VK_NULL_HANDLE
            && device == new_device && render_pass == new_render_pass)
            return true;

        destroy_pipelines();
        device = new_device;
        render_pass = new_render_pass;
        if (device == VK_NULL_HANDLE || render_pass == VK_NULL_HANDLE || layout == VK_NULL_HANDLE)
            return false;

        const auto shader_dir = bundled_asset_path("shaders");
        VkShaderModule vert = load_shader(device, (shader_dir / "satview_earth.vert.spv").string());
        VkShaderModule frag = load_shader(device, (shader_dir / "satview_earth.frag.spv").string());
        VkShaderModule moon_vert = load_shader(device, (shader_dir / "satview_moon.vert.spv").string());
        VkShaderModule moon_frag = load_shader(device, (shader_dir / "satview_moon.frag.spv").string());
        VkShaderModule cloud_vert = load_shader(device, (shader_dir / "satview_cloud.vert.spv").string());
        VkShaderModule cloud_frag = load_shader(device, (shader_dir / "satview_cloud.frag.spv").string());
        VkShaderModule atmosphere_vert =
            load_shader(device, (shader_dir / "satview_atmosphere.vert.spv").string());
        VkShaderModule atmosphere_frag =
            load_shader(device, (shader_dir / "satview_atmosphere.frag.spv").string());
        VkShaderModule ground_atmosphere_vert =
            load_shader(device, (shader_dir / "satview_ground_atmosphere.vert.spv").string());
        VkShaderModule ground_atmosphere_frag =
            load_shader(device, (shader_dir / "satview_ground_atmosphere.frag.spv").string());
        VkShaderModule ground_surface_frag =
            load_shader(device, (shader_dir / "satview_ground_surface.frag.spv").string());
        VkShaderModule star_vert = load_shader(device, (shader_dir / "satview_star.vert.spv").string());
        VkShaderModule star_frag = load_shader(device, (shader_dir / "satview_star.frag.spv").string());
        VkShaderModule orbit_vert = load_shader(device, (shader_dir / "satview_orbit.vert.spv").string());
        VkShaderModule marker_vert = load_shader(device, (shader_dir / "satview_marker.vert.spv").string(), false);
        VkShaderModule orbit_frag = load_shader(device, (shader_dir / "satview_orbit.frag.spv").string());
        if (vert == VK_NULL_HANDLE || frag == VK_NULL_HANDLE
            || moon_vert == VK_NULL_HANDLE || moon_frag == VK_NULL_HANDLE
            || cloud_vert == VK_NULL_HANDLE || cloud_frag == VK_NULL_HANDLE
            || atmosphere_vert == VK_NULL_HANDLE || atmosphere_frag == VK_NULL_HANDLE
            || ground_atmosphere_vert == VK_NULL_HANDLE || ground_atmosphere_frag == VK_NULL_HANDLE
            || ground_surface_frag == VK_NULL_HANDLE
            || star_vert == VK_NULL_HANDLE || star_frag == VK_NULL_HANDLE
            || orbit_vert == VK_NULL_HANDLE || orbit_frag == VK_NULL_HANDLE)
        {
            if (vert != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, vert, nullptr);
            if (frag != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, frag, nullptr);
            if (moon_vert != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, moon_vert, nullptr);
            if (moon_frag != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, moon_frag, nullptr);
            if (cloud_vert != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, cloud_vert, nullptr);
            if (cloud_frag != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, cloud_frag, nullptr);
            if (atmosphere_vert != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, atmosphere_vert, nullptr);
            if (atmosphere_frag != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, atmosphere_frag, nullptr);
            if (ground_atmosphere_vert != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, ground_atmosphere_vert, nullptr);
            if (ground_atmosphere_frag != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, ground_atmosphere_frag, nullptr);
            if (ground_surface_frag != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, ground_surface_frag, nullptr);
            if (star_vert != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, star_vert, nullptr);
            if (star_frag != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, star_frag, nullptr);
            if (orbit_vert != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, orbit_vert, nullptr);
            if (marker_vert != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, marker_vert, nullptr);
            if (orbit_frag != VK_NULL_HANDLE)
                vkDestroyShaderModule(device, orbit_frag, nullptr);
            return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vert;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = frag;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertex_input{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

        VkPipelineInputAssemblyStateCreateInfo input_assembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewport_state{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
        viewport_state.viewportCount = 1;
        viewport_state.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depth{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState blend_attachment{};
        blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blend{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
        blend.attachmentCount = 1;
        blend.pAttachments = &blend_attachment;

        VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;

        VkGraphicsPipelineCreateInfo pipeline_ci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
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
        pipeline_ci.layout = layout;
        pipeline_ci.renderPass = render_pass;
        pipeline_ci.subpass = 0;

        VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &earth_pipeline);
        if (result == VK_SUCCESS)
        {
            stages[0].module = moon_vert;
            stages[1].module = moon_frag;
            result = vkCreateGraphicsPipelines(
                device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &moon_pipeline);
        }
        if (result == VK_SUCCESS)
        {
            stages[0].module = cloud_vert;
            stages[1].module = cloud_frag;
            depth.depthWriteEnable = VK_FALSE;
            blend_attachment.blendEnable = VK_TRUE;
            blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
            blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            result = vkCreateGraphicsPipelines(
                device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &cloud_pipeline);
        }
        if (result == VK_SUCCESS)
        {
            stages[0].module = atmosphere_vert;
            stages[1].module = atmosphere_frag;
            result = vkCreateGraphicsPipelines(
                device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &atmosphere_pipeline);
        }
        if (result == VK_SUCCESS)
        {
            stages[0].module = ground_atmosphere_vert;
            stages[1].module = ground_atmosphere_frag;
            result = vkCreateGraphicsPipelines(
                device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &ground_atmosphere_pipeline);
        }
        if (result == VK_SUCCESS)
        {
            stages[0].module = ground_atmosphere_vert;
            stages[1].module = ground_surface_frag;
            depth.depthTestEnable = VK_FALSE;
            depth.depthWriteEnable = VK_FALSE;
            result = vkCreateGraphicsPipelines(
                device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &ground_surface_pipeline);
            depth.depthTestEnable = VK_TRUE;
        }
        if (result == VK_SUCCESS)
        {
            VkVertexInputBindingDescription star_binding{};
            star_binding.binding = 0;
            star_binding.stride = sizeof(SatViewStarInstance);
            star_binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

            VkVertexInputAttributeDescription star_attributes[2]{};
            star_attributes[0].binding = 0;
            star_attributes[0].location = 0;
            star_attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            star_attributes[0].offset = offsetof(SatViewStarInstance, direction_magnitude);
            star_attributes[1].binding = 0;
            star_attributes[1].location = 1;
            star_attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            star_attributes[1].offset = offsetof(SatViewStarInstance, color_size);

            vertex_input.vertexBindingDescriptionCount = 1;
            vertex_input.pVertexBindingDescriptions = &star_binding;
            vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(star_attributes));
            vertex_input.pVertexAttributeDescriptions = star_attributes;

            stages[0].module = star_vert;
            stages[1].module = star_frag;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            depth.depthTestEnable = VK_FALSE;
            depth.depthWriteEnable = VK_FALSE;
            blend_attachment.blendEnable = VK_TRUE;
            blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
            blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &star_pipeline);
            depth.depthTestEnable = VK_TRUE;
        }
        if (result == VK_SUCCESS)
        {
            VkVertexInputBindingDescription orbit_binding{};
            orbit_binding.binding = 0;
            orbit_binding.stride = sizeof(SatViewSceneVertex);
            orbit_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            VkVertexInputAttributeDescription orbit_attributes[3]{};
            orbit_attributes[0].binding = 0;
            orbit_attributes[0].location = 0;
            orbit_attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            orbit_attributes[0].offset = offsetof(SatViewSceneVertex, position);
            orbit_attributes[1].binding = 0;
            orbit_attributes[1].location = 1;
            orbit_attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            orbit_attributes[1].offset = offsetof(SatViewSceneVertex, color);
            orbit_attributes[2].binding = 0;
            orbit_attributes[2].location = 2;
            orbit_attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            orbit_attributes[2].offset = offsetof(SatViewSceneVertex, paired_position);

            vertex_input.vertexBindingDescriptionCount = 1;
            vertex_input.pVertexBindingDescriptions = &orbit_binding;
            vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(orbit_attributes));
            vertex_input.pVertexAttributeDescriptions = orbit_attributes;

            stages[0].module = orbit_vert;
            stages[1].module = orbit_frag;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            depth.depthWriteEnable = VK_FALSE;
            blend_attachment.blendEnable = VK_TRUE;
            blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
            blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &orbit_pipeline);
        }
        if (result == VK_SUCCESS && marker_vert != VK_NULL_HANDLE)
        {
            VkVertexInputBindingDescription marker_binding{};
            marker_binding.binding = 0;
            marker_binding.stride = sizeof(SatViewMarkerInstance);
            marker_binding.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

            VkVertexInputAttributeDescription marker_attributes[3]{};
            marker_attributes[0].binding = 0;
            marker_attributes[0].location = 0;
            marker_attributes[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            marker_attributes[0].offset = offsetof(SatViewMarkerInstance, position0_size);
            marker_attributes[1].binding = 0;
            marker_attributes[1].location = 1;
            marker_attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            marker_attributes[1].offset = offsetof(SatViewMarkerInstance, position1_selected);
            marker_attributes[2].binding = 0;
            marker_attributes[2].location = 2;
            marker_attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
            marker_attributes[2].offset = offsetof(SatViewMarkerInstance, color);

            vertex_input.vertexBindingDescriptionCount = 1;
            vertex_input.pVertexBindingDescriptions = &marker_binding;
            vertex_input.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(marker_attributes));
            vertex_input.pVertexAttributeDescriptions = marker_attributes;

            stages[0].module = marker_vert;
            stages[1].module = orbit_frag;
            input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            depth.depthWriteEnable = VK_FALSE;
            const VkResult marker_result =
                vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_ci, nullptr, &marker_pipeline);
            if (marker_result != VK_SUCCESS)
            {
                marker_pipeline = VK_NULL_HANDLE;
                DRAXUL_LOG_WARN(LogCategory::Renderer, "SatView: failed to create optional marker pipeline");
            }
        }

        vkDestroyShaderModule(device, vert, nullptr);
        vkDestroyShaderModule(device, frag, nullptr);
        vkDestroyShaderModule(device, moon_vert, nullptr);
        vkDestroyShaderModule(device, moon_frag, nullptr);
        vkDestroyShaderModule(device, cloud_vert, nullptr);
        vkDestroyShaderModule(device, cloud_frag, nullptr);
        vkDestroyShaderModule(device, atmosphere_vert, nullptr);
        vkDestroyShaderModule(device, atmosphere_frag, nullptr);
        vkDestroyShaderModule(device, ground_atmosphere_vert, nullptr);
        vkDestroyShaderModule(device, ground_atmosphere_frag, nullptr);
        vkDestroyShaderModule(device, ground_surface_frag, nullptr);
        vkDestroyShaderModule(device, star_vert, nullptr);
        vkDestroyShaderModule(device, star_frag, nullptr);
        vkDestroyShaderModule(device, orbit_vert, nullptr);
        if (marker_vert != VK_NULL_HANDLE)
            vkDestroyShaderModule(device, marker_vert, nullptr);
        vkDestroyShaderModule(device, orbit_frag, nullptr);
        if (result != VK_SUCCESS)
        {
            DRAXUL_LOG_ERROR(LogCategory::Renderer, "SatView: failed to create graphics pipeline");
            destroy_pipelines();
            return false;
        }
        return true;
    }
};

SatViewScenePass::SatViewScenePass()
    : state_(std::make_unique<State>())
{
}

SatViewScenePass::~SatViewScenePass() = default;

void SatViewScenePass::record_prepass(IRenderContext& ctx)
{
    PERF_MEASURE();
    auto* vk_ctx = static_cast<VkRenderContext*>(&ctx);
    if (state_->ensure_texture_descriptors(*vk_ctx))
    {
        if (pending_cloud_image_
            && state_->ensure_cloud_texture(*vk_ctx, *pending_cloud_image_, cloud_revision_))
        {
            pending_cloud_image_.reset();
        }
        state_->ensure_vertex_buffer(
            *vk_ctx,
            track_vertices_,
            track_revision_,
            state_->track_vertex_buffer,
            state_->track_vertex_count,
            state_->uploaded_track_revision);
        state_->ensure_vertex_buffer(
            *vk_ctx,
            markers_,
            marker_revision_,
            state_->marker_buffer,
            state_->marker_count,
            state_->uploaded_marker_revision);
        state_->ensure_vertex_buffer(
            *vk_ctx,
            stars_,
            star_revision_,
            state_->star_buffer,
            state_->star_count,
            state_->uploaded_star_revision);
    }
}

void SatViewScenePass::record(IRenderContext& ctx)
{
    PERF_MEASURE();
    auto* vk_ctx = static_cast<VkRenderContext*>(&ctx);
    if (state_->descriptor_set == VK_NULL_HANDLE
        || !state_->ensure_pipelines(vk_ctx->device(), vk_ctx->render_pass()))
        return;

    SatViewFrameUniforms frame = frame_;
    frame.view_proj = make_satview_vulkan_clip_matrix(frame.view_proj);

    VkCommandBuffer cmd = vk_ctx->command_buffer();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->layout,
        0, 1, &state_->descriptor_set, 0, nullptr);
    auto draw_stars = [&]() {
        const uint32_t draw_count = state_->star_count;
        if (draw_count == 0
            || state_->star_buffer.buffer == VK_NULL_HANDLE
            || state_->star_pipeline == VK_NULL_HANDLE)
            return;

        VkDeviceSize offset = 0;
        SatViewFrameUniforms star_frame = frame;
        star_frame.render_params.x = star_min_magnitude_;
        star_frame.render_params.y = star_max_magnitude_;
        star_frame.render_params.z = star_projection_aspect_scale_;
        star_frame.render_params.w = star_brightness_scale_;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->star_pipeline);
        vkCmdBindVertexBuffers(cmd, 0, 1, &state_->star_buffer.buffer, &offset);
        vkCmdPushConstants(cmd, state_->layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(SatViewFrameUniforms), &star_frame);
        vkCmdDraw(cmd, kSatViewStarVerticesPerInstance, draw_count, 0, 0);
    };
    if (map_projection_)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            moon_map_projection_ ? state_->moon_pipeline : state_->earth_pipeline);
        vkCmdPushConstants(cmd, state_->layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(SatViewFrameUniforms), &frame);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }
    else if (ground_projection_)
    {
        if (atmosphere_enabled_)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->ground_atmosphere_pipeline);
            vkCmdPushConstants(cmd, state_->layout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(SatViewFrameUniforms), &frame);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        }

        draw_stars();

        if (moon_enabled_ && moon_position_radius_.w > 0.0f)
        {
            SatViewFrameUniforms moon_frame = frame;
            moon_frame.camera_orientation = moon_position_radius_;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->moon_pipeline);
            vkCmdPushConstants(cmd, state_->layout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(SatViewFrameUniforms), &moon_frame);
            vkCmdDraw(cmd, kSatViewSphereVertexCount, 1, 0, 0);
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->ground_surface_pipeline);
        vkCmdPushConstants(cmd, state_->layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(SatViewFrameUniforms), &frame);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    else
    {
        draw_stars();

        if (moon_enabled_ && moon_position_radius_.w > 0.0f)
        {
            SatViewFrameUniforms moon_frame = frame;
            moon_frame.camera_orientation = moon_position_radius_;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->moon_pipeline);
            vkCmdPushConstants(cmd, state_->layout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(SatViewFrameUniforms), &moon_frame);
            vkCmdDraw(cmd, kSatViewSphereVertexCount, 1, 0, 0);
        }

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->earth_pipeline);
        vkCmdPushConstants(cmd, state_->layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(SatViewFrameUniforms), &frame);
        vkCmdDraw(cmd, kSatViewSphereVertexCount, 1, 0, 0);

        if (frame.sun_dir_time.w > 0.5f)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->cloud_pipeline);
            vkCmdDraw(cmd, kSatViewSphereVertexCount, 1, 0, 0);
        }

        if (atmosphere_enabled_)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->atmosphere_pipeline);
            vkCmdDraw(cmd, kSatViewSphereVertexCount, 1, 0, 0);
        }
    }

    if (state_->track_vertex_count != 0 && state_->track_vertex_buffer.buffer != VK_NULL_HANDLE)
    {
        VkDeviceSize offset = 0;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->orbit_pipeline);
        vkCmdBindVertexBuffers(cmd, 0, 1, &state_->track_vertex_buffer.buffer, &offset);
        if (map_projection_)
        {
            for (int copy = -1; copy <= 1; ++copy)
            {
                SatViewFrameUniforms track_frame = frame;
                track_frame.camera_orientation.w = static_cast<float>(copy * 2);
                vkCmdPushConstants(cmd, state_->layout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(SatViewFrameUniforms), &track_frame);
                vkCmdDraw(cmd, state_->track_vertex_count, 1, 0, 0);
            }
            vkCmdPushConstants(cmd, state_->layout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(SatViewFrameUniforms), &frame);
        }
        else
        {
            vkCmdDraw(cmd, state_->track_vertex_count, 1, 0, 0);
        }
    }

    if (state_->marker_count != 0
        && state_->marker_buffer.buffer != VK_NULL_HANDLE
        && state_->marker_pipeline != VK_NULL_HANDLE)
    {
        VkDeviceSize offset = 0;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, state_->marker_pipeline);
        vkCmdBindVertexBuffers(cmd, 0, 1, &state_->marker_buffer.buffer, &offset);
        vkCmdDraw(cmd, kSatViewMarkerVerticesPerInstance, state_->marker_count, 0, 0);
    }
}

} // namespace draxul::satview
