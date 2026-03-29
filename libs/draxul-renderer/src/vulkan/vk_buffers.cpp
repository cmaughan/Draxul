#include "vk_buffers.h"
#include "vk_context.h"
#include <draxul/log.h>
#include <draxul/perf_timing.h>

namespace draxul
{

bool VkGridBuffer::initialize(VkContext& ctx, size_t initial_size)
{
    return ensure_size(ctx.allocator(), initial_size) != BufferResizeResult::Failed;
}

void VkGridBuffer::shutdown(VmaAllocator allocator)
{
    PERF_MEASURE();
    if (buffer_.buffer)
    {
        vmaDestroyBuffer(allocator, buffer_.buffer, buffer_.allocation);
        buffer_ = {};
    }
}

BufferResizeResult VkGridBuffer::ensure_size(VmaAllocator allocator, size_t required_size)
{
    PERF_MEASURE();
    return ensure_buffer_size(buffer_, required_size, [&](size_t requested_size, BufferState& replacement) {
            VkBufferCreateInfo buf_ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            buf_ci.size = requested_size;
            buf_ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo alloc_ci = {};
            alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;
            alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VmaAllocationInfo alloc_info = {};
            if (vmaCreateBuffer(allocator, &buf_ci, &alloc_ci, &replacement.buffer, &replacement.allocation, &alloc_info) != VK_SUCCESS)
            {
                DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create grid SSBO");
                return false;
            }

            replacement.mapped = alloc_info.pMappedData;
            replacement.size = requested_size;
            return true; }, [&](const BufferState& existing) { vmaDestroyBuffer(allocator, existing.buffer, existing.allocation); });
}

void VkGridBuffer::flush_range(VmaAllocator allocator, VkDeviceSize offset, VkDeviceSize size) const
{
    PERF_MEASURE();
    if (buffer_.buffer == VK_NULL_HANDLE)
        return;

    vmaFlushAllocation(allocator, buffer_.allocation, offset, size);
}

bool VkStagingBuffer::initialize(VmaAllocator allocator, size_t size)
{
    PERF_MEASURE();
    VkBufferCreateInfo buf_ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    buf_ci.size = size;
    buf_ci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo alloc_ci = {};
    alloc_ci.usage = VMA_MEMORY_USAGE_AUTO;
    alloc_ci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo alloc_info;
    if (vmaCreateBuffer(allocator, &buf_ci, &alloc_ci, &buffer_, &allocation_, &alloc_info) != VK_SUCCESS)
    {
        DRAXUL_LOG_ERROR(LogCategory::Renderer, "Failed to create staging buffer");
        return false;
    }

    mapped_ = alloc_info.pMappedData;
    return true;
}

void VkStagingBuffer::shutdown(VmaAllocator allocator)
{
    PERF_MEASURE();
    if (buffer_)
    {
        vmaDestroyBuffer(allocator, buffer_, allocation_);
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
        mapped_ = nullptr;
    }
}

} // namespace draxul
