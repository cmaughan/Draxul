#pragma once
// Public Metal-backend header — only includable from ObjC++ (.mm) translation
// units. Render passes targeting the Metal backend static_cast<MetalRenderContext*>(&ctx)
// inside their IRenderPass::record() implementation.
#include <draxul/base_renderer.h>

#ifdef __OBJC__
#import <Metal/Metal.h>

namespace draxul
{

// Platform-specific IRenderContext for the Metal backend.
// Passed to IRenderPass::record() during live frame encoding.
// Render passes static_cast<MetalRenderContext*>(&ctx) to access
// command_buffer() and encoder() with full type safety.
class MetalRenderContext : public IRenderContext
{
public:
    MetalRenderContext(id<MTLCommandBuffer> cmd, id<MTLRenderCommandEncoder> encoder,
        uint32_t frame_index, uint32_t buffered_frame_count,
        int w, int h, int vx, int vy, int vw, int vh,
        id<MTLDevice> device = nil, id<MTLTexture> drawable_texture = nil,
        MTLRenderPassDescriptor* continuation_descriptor = nil,
        uint64_t target_generation = 0)
        : cmd_(cmd)
        , encoder_(encoder)
        , frame_index_(frame_index)
        , buffered_frame_count_(buffered_frame_count > 0 ? buffered_frame_count : 1)
        , w_(w)
        , h_(h)
        , vx_(vx)
        , vy_(vy)
        , vw_(vw)
        , vh_(vh)
        , device_(device)
        , drawable_texture_(drawable_texture)
        , continuation_descriptor_(continuation_descriptor)
        , target_generation_(target_generation)
    {
    }

    // Typed Metal accessors — no void* casts needed.
    id<MTLCommandBuffer> command_buffer() const
    {
        return cmd_;
    }
    id<MTLRenderCommandEncoder> encoder() const
    {
        return encoder_;
    }
    id<MTLDevice> device() const
    {
        return device_;
    }
    id<MTLTexture> drawable_texture() const
    {
        return drawable_texture_;
    }
    MTLRenderPassDescriptor* continuation_render_pass_descriptor() const
    {
        return continuation_descriptor_;
    }
    uint64_t target_generation() const { return target_generation_; }

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
        return vx_;
    }
    int viewport_y() const override
    {
        return vy_;
    }
    int viewport_w() const override
    {
        return vw_;
    }
    int viewport_h() const override
    {
        return vh_;
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
    id<MTLCommandBuffer> cmd_;
    id<MTLRenderCommandEncoder> encoder_;
    uint32_t frame_index_;
    uint32_t buffered_frame_count_;
    int w_;
    int h_;
    int vx_;
    int vy_;
    int vw_;
    int vh_;
    id<MTLDevice> device_;
    id<MTLTexture> drawable_texture_;
    MTLRenderPassDescriptor* continuation_descriptor_;
    uint64_t target_generation_ = 0;
};

} // namespace draxul

#endif // __OBJC__
