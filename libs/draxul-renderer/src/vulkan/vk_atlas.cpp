#include "vk_atlas.h"
#include "vk_context.h"

#include <cstring>
#include <draxul/log.h>
#include <draxul/perf_timing.h>

namespace draxul
{

namespace
{

constexpr VkFormat kAtlasFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr size_t kAtlasPixelSize = 4;

} // namespace

bool VkAtlas::initialize(VkContext& ctx, int atlas_size)
{
    PERF_MEASURE();
    atlas_size_ = atlas_size;
    VkDevice device = ctx.device();

    VkCommandPoolCreateInfo pool_ci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pool_ci.queueFamilyIndex = ctx.graphics_queue_family();
    pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(device, &pool_ci, nullptr, &cmd_pool_) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create atlas command pool");
        return false;
    }

    VkImageCreateInfo img_ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    img_ci.imageType = VK_IMAGE_TYPE_2D;
    img_ci.format = kAtlasFormat;
    img_ci.extent = { static_cast<uint32_t>(atlas_size_), static_cast<uint32_t>(atlas_size_), 1 };
    img_ci.mipLevels = 1;
    img_ci.arrayLayers = 1;
    img_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    img_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    img_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;

    if (vmaCreateImage(ctx.allocator(), &img_ci, &alloc_ci, &image_, &allocation_, nullptr) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create atlas image");
        return false;
    }

    VkImageViewCreateInfo view_ci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view_ci.image = image_;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = kAtlasFormat;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device, &view_ci, nullptr, &image_view_) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create atlas image view");
        return false;
    }

    VkSamplerCreateInfo samp_ci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    samp_ci.magFilter = VK_FILTER_LINEAR;
    samp_ci.minFilter = VK_FILTER_LINEAR;
    samp_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &samp_ci, nullptr, &sampler_) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create atlas sampler");
        return false;
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
    {
        if (!ensure_staging_capacity(ctx, i, static_cast<size_t>(atlas_size_) * atlas_size_ * kAtlasPixelSize))
            return false;
    }

    return transition_to_shader_read(ctx);
}

void VkAtlas::shutdown(VkContext& ctx)
{
    PERF_MEASURE();
    VkDevice device = ctx.device();
    for (auto& staging : staging_)
    {
        if (staging.buffer)
            vmaDestroyBuffer(ctx.allocator(), staging.buffer, staging.allocation);
        staging = {};
    }
    if (sampler_)
        vkDestroySampler(device, sampler_, nullptr);
    if (image_view_)
        vkDestroyImageView(device, image_view_, nullptr);
    if (image_)
        vmaDestroyImage(ctx.allocator(), image_, allocation_);
    if (cmd_pool_)
        vkDestroyCommandPool(device, cmd_pool_, nullptr);
}

bool VkAtlas::ensure_staging_capacity(VkContext& ctx, uint32_t frame_index, size_t required_size)
{
    PERF_MEASURE();
    return ensure_buffer_size(staging_[frame_index], required_size, [&](size_t requested_size, BufferState& replacement) {
            VkBufferCreateInfo buf_ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            buf_ci.size = requested_size;
            buf_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

            VmaAllocationCreateInfo staging_ci = {};
            staging_ci.usage = VMA_MEMORY_USAGE_AUTO;
            staging_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VmaAllocationInfo staging_info = {};
            if (vmaCreateBuffer(ctx.allocator(), &buf_ci, &staging_ci, &replacement.buffer, &replacement.allocation, &staging_info) != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create atlas staging buffer");
                return false;
            }

            replacement.mapped = staging_info.pMappedData;
            replacement.size = requested_size;
            return true; }, [&](const BufferState& existing) { vmaDestroyBuffer(ctx.allocator(), existing.buffer, existing.allocation); }) != BufferResizeResult::Failed;
}

bool VkAtlas::transition_to_shader_read(VkContext& ctx)
{
    PERF_MEASURE();
    VkCommandBuffer cmd = begin_single_command(ctx);
    if (cmd == VK_NULL_HANDLE)
        return false;

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.oldLayout = current_layout_;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    if (!end_single_command(ctx, cmd))
        return false;

    current_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return true;
}

VkCommandBuffer VkAtlas::begin_single_command(VkContext& ctx)
{
    PERF_MEASURE();
    VkCommandBufferAllocateInfo alloc_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    alloc_info.commandPool = cmd_pool_;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(ctx.device(), &alloc_info, &cmd) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    VkCommandBufferBeginInfo begin_info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(ctx.device(), cmd_pool_, 1, &cmd);
        return VK_NULL_HANDLE;
    }
    return cmd;
}

bool VkAtlas::end_single_command(VkContext& ctx, VkCommandBuffer cmd)
{
    PERF_MEASURE();
    if (vkEndCommandBuffer(cmd) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(ctx.device(), cmd_pool_, 1, &cmd);
        return false;
    }

    VkFence fence = VK_NULL_HANDLE;
    VkFenceCreateInfo fence_ci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    if (vkCreateFence(ctx.device(), &fence_ci, nullptr, &fence) != VK_SUCCESS)
    {
        vkFreeCommandBuffers(ctx.device(), cmd_pool_, 1, &cmd);
        return false;
    }

    VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    bool ok = vkQueueSubmit(ctx.graphics_queue(), 1, &submit, fence) == VK_SUCCESS
        && vkWaitForFences(ctx.device(), 1, &fence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;

    vkDestroyFence(ctx.device(), fence, nullptr);
    vkFreeCommandBuffers(ctx.device(), cmd_pool_, 1, &cmd);
    return ok;
}

bool VkAtlas::record_uploads(VkContext& ctx, VkCommandBuffer cmd, uint32_t frame_index,
    std::span<const PendingAtlasUpload> uploads)
{
    PERF_MEASURE();
    if (uploads.empty())
        return true;

    size_t total_bytes = 0;
    for (const auto& upload : uploads)
        total_bytes += upload.pixels.size();
    if (total_bytes == 0)
        return true;

    if (!ensure_staging_capacity(ctx, frame_index, total_bytes))
        return false;

    auto& staging = staging_[frame_index];
    auto* dst = static_cast<uint8_t*>(staging.mapped);
    size_t buffer_offset = 0;
    for (const auto& upload : uploads)
    {
        if (upload.pixels.empty())
            continue;
        std::memcpy(dst + buffer_offset, upload.pixels.data(), upload.pixels.size());
        buffer_offset += upload.pixels.size();
    }
    vmaFlushAllocation(ctx.allocator(), staging.allocation, 0, buffer_offset);

    VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image_;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.oldLayout = current_layout_;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = current_layout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? VK_ACCESS_SHADER_READ_BIT : 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        current_layout_ == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    buffer_offset = 0;
    for (const auto& upload : uploads)
    {
        if (upload.pixels.empty())
            continue;

        VkBufferImageCopy region = {};
        region.bufferOffset = buffer_offset;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { upload.x, upload.y, 0 };
        region.imageExtent = { static_cast<uint32_t>(upload.w), static_cast<uint32_t>(upload.h), 1 };
        vkCmdCopyBufferToImage(cmd, staging.buffer, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        buffer_offset += upload.pixels.size();
    }

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    current_layout_ = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return true;
}

} // namespace draxul
