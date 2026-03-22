#pragma once
// Internal header — only included from ObjC++ (.mm) translation units.
#include <draxul/base_renderer.h>

#ifdef __OBJC__
#import <Metal/Metal.h>

namespace draxul
{

// Platform-specific IRenderContext for the Metal backend.
// Passed to IRenderPass::record() during end_frame().
// Callers that know they are on Metal may cast and use the typed accessors.
class MetalRenderContext : public IRenderContext
{
public:
    MetalRenderContext(id<MTLCommandBuffer> cmd, id<MTLRenderCommandEncoder> encoder,
        int w, int h, int vx, int vy, int vw, int vh)
        : cmd_(cmd)
        , encoder_(encoder)
        , w_(w)
        , h_(h)
        , vx_(vx)
        , vy_(vy)
        , vw_(vw)
        , vh_(vh)
    {
    }

    void* native_command_buffer() const override
    {
        return (__bridge void*)cmd_;
    }
    void* native_render_encoder() const override
    {
        return (__bridge void*)encoder_;
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

private:
    id<MTLCommandBuffer> cmd_;
    id<MTLRenderCommandEncoder> encoder_;
    int w_;
    int h_;
    int vx_;
    int vy_;
    int vw_;
    int vh_;
};

} // namespace draxul

#endif // __OBJC__
