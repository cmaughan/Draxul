#pragma once
#include "vk_resource_ownership.h"
#include <draxul/pending_atlas_upload.h>

#include <draxul/types.h>
#include <span>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace draxul
{

class VkContext;

class VkAtlas
{
public:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    bool initialize(VkContext& ctx, int atlas_size = kAtlasSize);
    void shutdown(VkContext& ctx);
    bool record_uploads(VkContext& ctx, VkCommandBuffer cmd, uint32_t frame_index,
        std::span<const PendingAtlasUpload> uploads);

    VkImageView image_view() const
    {
        return image_view_;
    }
    VkSampler sampler() const
    {
        return sampler_;
    }

private:
    using BufferState = OwnedMappedBuffer<VkBuffer, VmaAllocation>;

    bool ensure_staging_capacity(VkContext& ctx, uint32_t frame_index, size_t required_size);
    bool transition_to_shader_read(VkContext& ctx);
    VkCommandBuffer begin_single_command(VkContext& ctx);
    bool end_single_command(VkContext& ctx, VkCommandBuffer cmd);

    int atlas_size_ = kAtlasSize;
    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
    BufferState staging_[MAX_FRAMES_IN_FLIGHT];
    VkImageLayout current_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
};

} // namespace draxul
