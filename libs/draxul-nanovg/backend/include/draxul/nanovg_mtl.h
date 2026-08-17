#pragma once

// Custom Metal backend for NanoVG.
// Implements the NVGparams interface, rendering into an externally-provided
// command buffer and drawable texture (no CAMetalLayer dependency).

// NanoVG create flags (defined per-backend, not in core nanovg.h)
enum NVGcreateFlags
{
    NVG_ANTIALIAS = 1 << 0,
    NVG_STENCIL_STROKES = 1 << 1,
    NVG_DEBUG = 1 << 2,
};

#ifdef __OBJC__
#import <Metal/Metal.h>

struct NVGcontext;

namespace draxul
{

// Create a NanoVG context with the custom Metal backend.
// The device is used for pipeline/buffer/texture creation.
// flags: NVG_ANTIALIAS, NVG_STENCIL_STROKES, NVG_DEBUG
NVGcontext* nvgCreateMtl(id<MTLDevice> device, int flags);

// Destroy a NanoVG context created with nvgCreateMtl.
void nvgDeleteMtl(NVGcontext* ctx);

// Must be called each frame before nvgBeginFrame.
// Sets the command buffer and render target for the current frame.
void nvgMtlSetFrameState(NVGcontext* ctx,
    id<MTLCommandBuffer> commandBuffer,
    id<MTLTexture> drawableTexture,
    uint32_t frameIndex);

} // namespace draxul

#endif // __OBJC__
