#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace draxul
{

// Narrow Vulkan call seam for deterministic frame-transition tests. Production
// uses the pass-through implementation in vk_renderer.cpp; tests substitute a
// recorder so every failure edge can be exercised without a live GPU.
class VkRendererOperations
{
public:
    virtual ~VkRendererOperations() = default;

    virtual VkResult allocate_command_buffers(
        VkDevice device, const VkCommandBufferAllocateInfo* info,
        VkCommandBuffer* command_buffers) = 0;
    virtual VkResult reset_command_buffer(
        VkCommandBuffer command_buffer, VkCommandBufferResetFlags flags) = 0;
    virtual VkResult begin_command_buffer(
        VkCommandBuffer command_buffer, const VkCommandBufferBeginInfo* info) = 0;
    virtual VkResult end_command_buffer(VkCommandBuffer command_buffer) = 0;
    virtual VkResult queue_submit(VkQueue queue, uint32_t submit_count,
        const VkSubmitInfo* submits, VkFence fence) = 0;
    virtual VkResult reset_fences(
        VkDevice device, uint32_t fence_count, const VkFence* fences) = 0;
    virtual VkResult wait_for_fences(VkDevice device, uint32_t fence_count,
        const VkFence* fences, VkBool32 wait_all, uint64_t timeout) = 0;
    virtual VkResult device_wait_idle(VkDevice device) = 0;
    virtual VkResult create_semaphore(VkDevice device,
        const VkSemaphoreCreateInfo* info, const VkAllocationCallbacks* allocator,
        VkSemaphore* semaphore) = 0;
    virtual VkResult create_fence(VkDevice device,
        const VkFenceCreateInfo* info, const VkAllocationCallbacks* allocator,
        VkFence* fence) = 0;
    virtual void destroy_semaphore(VkDevice device, VkSemaphore semaphore,
        const VkAllocationCallbacks* allocator) = 0;
    virtual void destroy_fence(VkDevice device, VkFence fence,
        const VkAllocationCallbacks* allocator) = 0;
    virtual void cmd_pipeline_barrier(VkCommandBuffer command_buffer,
        VkPipelineStageFlags source_stage, VkPipelineStageFlags destination_stage,
        VkDependencyFlags dependency_flags, uint32_t memory_barrier_count,
        const VkMemoryBarrier* memory_barriers, uint32_t buffer_barrier_count,
        const VkBufferMemoryBarrier* buffer_barriers, uint32_t image_barrier_count,
        const VkImageMemoryBarrier* image_barriers) = 0;
    virtual void cmd_copy_image_to_buffer(VkCommandBuffer command_buffer,
        VkImage source_image, VkImageLayout source_layout,
        VkBuffer destination_buffer, uint32_t region_count,
        const VkBufferImageCopy* regions) = 0;
    virtual VkResult queue_present(
        VkQueue queue, const VkPresentInfoKHR* present_info) = 0;
    virtual VkResult invalidate_allocation(VmaAllocator allocator,
        VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size) = 0;
};

VkRendererOperations& default_vk_renderer_operations();

} // namespace draxul
