#pragma once
// Internal Vulkan header — only included from Vulkan (.cpp) translation units.
#include <draxul/base_renderer.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace draxul
{

// Platform-specific IRenderContext for the Vulkan backend.
// Passed to IRenderPass::record() during live frame encoding.
//
// Render passes static_cast<VkRenderContext*>(&ctx) to access command_buffer(),
// device(), allocator(), and render_pass() with full type safety.
class VkRenderContext : public IRenderContext
{
public:
    VkRenderContext(VkCommandBuffer cmd, VkPhysicalDevice physical_device, VkDevice device, VmaAllocator allocator, VkRenderPass render_pass,
        uint32_t frame_index, uint32_t buffered_frame_count,
        int w, int h, int viewport_x = 0, int viewport_y = 0, int viewport_w = 0, int viewport_h = 0,
        VkImage swapchain_image = VK_NULL_HANDLE, VkImageView swapchain_image_view = VK_NULL_HANDLE,
        VkFormat swapchain_format = VK_FORMAT_UNDEFINED,
        VkQueue graphics_queue = VK_NULL_HANDLE, uint32_t graphics_queue_family = 0)
        : cmd_(cmd)
        , physical_device_(physical_device)
        , device_(device)
        , allocator_(allocator)
        , render_pass_(render_pass)
        , frame_index_(frame_index)
        , buffered_frame_count_(buffered_frame_count > 0 ? buffered_frame_count : 1)
        , w_(w)
        , h_(h)
        , viewport_x_(viewport_x)
        , viewport_y_(viewport_y)
        , viewport_w_(viewport_w > 0 ? viewport_w : w)
        , viewport_h_(viewport_h > 0 ? viewport_h : h)
        , swapchain_image_(swapchain_image)
        , swapchain_image_view_(swapchain_image_view)
        , swapchain_format_(swapchain_format)
        , graphics_queue_(graphics_queue)
        , graphics_queue_family_(graphics_queue_family)
    {
    }

    // Typed Vulkan accessor — no void* casts needed.
    VkCommandBuffer command_buffer() const
    {
        return cmd_;
    }

    int width() const override
    {
        return w_;
    }
    int height() const override
    {
        return h_;
    }
    int viewport_x() const override
    {
        return viewport_x_;
    }
    int viewport_y() const override
    {
        return viewport_y_;
    }
    int viewport_w() const override
    {
        return viewport_w_;
    }
    int viewport_h() const override
    {
        return viewport_h_;
    }

    // Vulkan-specific extensions — cast from IRenderContext in Vk-specific code
    VkPhysicalDevice physical_device() const
    {
        return physical_device_;
    }
    VkDevice device() const
    {
        return device_;
    }
    VmaAllocator allocator() const
    {
        return allocator_;
    }
    VkRenderPass render_pass() const
    {
        return render_pass_;
    }
    VkImage swapchain_image() const
    {
        return swapchain_image_;
    }
    VkImageView swapchain_image_view() const
    {
        return swapchain_image_view_;
    }
    VkFormat swapchain_format() const
    {
        return swapchain_format_;
    }
    VkQueue graphics_queue() const
    {
        return graphics_queue_;
    }
    uint32_t graphics_queue_family() const
    {
        return graphics_queue_family_;
    }
    uint32_t frame_index() const override
    {
        return frame_index_;
    }
    uint32_t buffered_frame_count() const override
    {
        return buffered_frame_count_;
    }

private:
    VkCommandBuffer cmd_;
    VkPhysicalDevice physical_device_;
    VkDevice device_;
    VmaAllocator allocator_;
    VkRenderPass render_pass_;
    uint32_t frame_index_;
    uint32_t buffered_frame_count_;
    int w_;
    int h_;
    int viewport_x_;
    int viewport_y_;
    int viewport_w_;
    int viewport_h_;
    VkImage swapchain_image_ = VK_NULL_HANDLE;
    VkImageView swapchain_image_view_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    uint32_t graphics_queue_family_ = 0;
};

} // namespace draxul
