#pragma once

// Custom Vulkan backend for NanoVG.
// Implements the NVGparams interface, rendering into an externally-provided
// command buffer and swapchain image (no swapchain ownership).

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

// NanoVG create flags (defined per-backend, not in core nanovg.h)
#ifndef NVG_ANTIALIAS
enum NVGcreateFlags
{
    NVG_ANTIALIAS = 1 << 0,
    NVG_STENCIL_STROKES = 1 << 1,
    NVG_DEBUG = 1 << 2,
};
#endif

struct NVGcontext;

namespace draxul
{

// Create a NanoVG context with the custom Vulkan backend.
NVGcontext* nvgCreateVk(VkPhysicalDevice physicalDevice, VkDevice device, VmaAllocator allocator,
    VkFormat colorFormat, int flags);

// Destroy a NanoVG context created with nvgCreateVk.
void nvgDeleteVk(NVGcontext* ctx);

// Must be called each frame before nvgBeginFrame.
// Sets the command buffer and render target for the current frame.
void nvgVkSetFrameState(NVGcontext* ctx,
    VkCommandBuffer commandBuffer,
    VkImage swapchainImage,
    VkImageView swapchainImageView,
    uint32_t frameIndex);

} // namespace draxul
