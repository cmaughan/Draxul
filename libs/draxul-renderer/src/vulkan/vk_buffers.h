#pragma once
#include <draxul/vulkan/vk_resource_ownership.h>

#include <cstdint>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace draxul
{

class VkContext;

// Host-visible coherent SSBO for grid cell data
class VkGridBuffer
{
public:
    using BufferState = OwnedMappedBuffer<VkBuffer, VmaAllocation>;

    bool initialize(VkContext& ctx, size_t initial_size);
    void shutdown(VmaAllocator allocator);

    BufferResizeResult ensure_size(VmaAllocator allocator, size_t required_size);
    void flush_range(VmaAllocator allocator, VkDeviceSize offset, VkDeviceSize size) const;
    BufferState release()
    {
        BufferState released = buffer_;
        buffer_ = {};
        return released;
    }
    static void destroy_buffer_state(VmaAllocator allocator, BufferState& state)
    {
        if (state.buffer == VK_NULL_HANDLE)
            return;

        vmaDestroyBuffer(allocator, state.buffer, state.allocation);
        state = {};
    }

    void* mapped() const
    {
        return buffer_.mapped;
    }
    VkBuffer buffer() const
    {
        return buffer_.buffer;
    }
    size_t size() const
    {
        return buffer_.size;
    }

private:
    BufferState buffer_;
};

// Staging buffer for atlas uploads
class VkStagingBuffer
{
public:
    bool initialize(VmaAllocator allocator, size_t size);
    void shutdown(VmaAllocator allocator);

    void* mapped() const
    {
        return mapped_;
    }
    VkBuffer buffer() const
    {
        return buffer_;
    }

private:
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
};

} // namespace draxul
