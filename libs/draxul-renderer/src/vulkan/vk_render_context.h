#pragma once
// Internal Vulkan header — only included from Vulkan (.cpp) translation units.
#include <draxul/base_renderer.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace draxul
{

// Platform-specific IRenderContext for the Vulkan backend.
// Passed to IRenderPass::record() during end_frame() / record_command_buffer().
//
// native_render_encoder() returns nullptr — Vulkan has no encoder object.
// native_command_buffer() returns the VkCommandBuffer (cast to void*).
//
// Platform-specific code that knows it is running on Vulkan may static_cast
// to VkRenderContext to access device() and render_pass() for lazy pipeline
// creation and recreation detection.
class VkRenderContext : public IRenderContext
{
public:
    VkRenderContext(VkCommandBuffer cmd, VkDevice device, VmaAllocator allocator, VkRenderPass render_pass,
        int w, int h, int viewport_x = 0, int viewport_y = 0, int viewport_w = 0, int viewport_h = 0)
        : cmd_(cmd)
        , device_(device)
        , allocator_(allocator)
        , render_pass_(render_pass)
        , w_(w)
        , h_(h)
        , viewport_x_(viewport_x)
        , viewport_y_(viewport_y)
        , viewport_w_(viewport_w > 0 ? viewport_w : w)
        , viewport_h_(viewport_h > 0 ? viewport_h : h)
    {
    }

    void* native_command_buffer() const override
    {
        return cmd_;
    }
    void* native_render_encoder() const override
    {
        return nullptr;
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

private:
    VkCommandBuffer cmd_;
    VkDevice device_;
    VmaAllocator allocator_;
    VkRenderPass render_pass_;
    int w_;
    int h_;
    int viewport_x_;
    int viewport_y_;
    int viewport_w_;
    int viewport_h_;
};

} // namespace draxul
