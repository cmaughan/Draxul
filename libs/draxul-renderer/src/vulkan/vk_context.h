#pragma once

#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

struct SDL_Window;

namespace draxul
{

struct SwapchainInfo
{
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkFormat depth_format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent = {};
    std::vector<VkImage> images;
    std::vector<VkImageView> image_views;
    std::vector<VkImage> depth_images;
    std::vector<VmaAllocation> depth_allocations;
    std::vector<VkImageView> depth_image_views;
    std::vector<VkFramebuffer> framebuffers;
};

struct PendingSwapchainResources
{
    SwapchainInfo swapchain;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkRenderPass load_render_pass = VK_NULL_HANDLE;
    VkRenderPass load_color_clear_depth_render_pass = VK_NULL_HANDLE;
};

class VkContext
{
public:
    explicit VkContext(bool wait_for_vblank = true)
        : wait_for_vblank_(wait_for_vblank)
    {
    }

    bool initialize(SDL_Window* window);
    void shutdown();

    bool recreate_swapchain(int width, int height);
    bool build_swapchain_resources(int width, int height, PendingSwapchainResources& pending);
    void commit_swapchain_resources(PendingSwapchainResources&& pending);
    void destroy_pending_swapchain_resources(PendingSwapchainResources& pending);

    VkInstance instance() const
    {
        return instance_;
    }
    VkPhysicalDevice physical_device() const
    {
        return physical_device_;
    }
    VkDevice device() const
    {
        return device_;
    }
    VkQueue graphics_queue() const
    {
        return graphics_queue_;
    }
    uint32_t graphics_queue_family() const
    {
        return graphics_queue_family_;
    }
    VmaAllocator allocator() const
    {
        return allocator_;
    }
    VkRenderPass render_pass() const
    {
        return render_pass_;
    }
    VkRenderPass load_render_pass() const
    {
        return load_render_pass_;
    }
    VkRenderPass load_color_clear_depth_render_pass() const
    {
        return load_color_clear_depth_render_pass_;
    }
    const SwapchainInfo& swapchain() const
    {
        return swapchain_;
    }
    VkSurfaceKHR surface() const
    {
        return surface_;
    }

private:
    VkFormat choose_depth_format() const;
    bool create_render_pass(VkFormat color_format, VkFormat depth_format,
        VkAttachmentLoadOp color_load_op, VkAttachmentLoadOp depth_load_op,
        VkRenderPass& render_pass);
    bool create_depth_resources(SwapchainInfo& swapchain);
    bool create_framebuffers(SwapchainInfo& swapchain, VkRenderPass render_pass);
    void destroy_swapchain();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    uint32_t graphics_queue_family_ = 0;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkRenderPass load_render_pass_ = VK_NULL_HANDLE;
    VkRenderPass load_color_clear_depth_render_pass_ = VK_NULL_HANDLE;
    SwapchainInfo swapchain_;
    SDL_Window* window_ = nullptr;
    bool wait_for_vblank_ = true;
};

} // namespace draxul
