#pragma once

#include <draxul/types.h>
#include <memory>

namespace draxul
{

class IWindow;

// ---------------------------------------------------------------------------
// IBaseRenderer — minimal swapchain/device/frame lifecycle contract.
// Satisfied by any renderer backend (Metal, Vulkan, future headless/test).
// ---------------------------------------------------------------------------
class IBaseRenderer
{
public:
    virtual ~IBaseRenderer() = default;
    virtual bool initialize(IWindow& window) = 0;
    virtual void shutdown() = 0;
    virtual bool begin_frame() = 0;
    virtual void end_frame() = 0;
    virtual void resize(int pixel_w, int pixel_h) = 0;
    virtual void set_default_background(Color bg) = 0;
};

// ---------------------------------------------------------------------------
// IRenderContext — per-frame platform handles handed to each IRenderPass.
//   macOS:   native_command_buffer() → id<MTLCommandBuffer> (bridge-cast)
//            native_render_encoder() → id<MTLRenderCommandEncoder> (bridge-cast)
//   Windows: native_command_buffer() → VkCommandBuffer (cast)
//            native_render_encoder() → nullptr (Vulkan has no encoder object)
// ---------------------------------------------------------------------------
class IRenderContext
{
public:
    virtual ~IRenderContext() = default;
    virtual void* native_command_buffer() const = 0; // NOSONAR cpp:S5008
    virtual void* native_render_encoder() const = 0; // NOSONAR cpp:S5008
    virtual int width() const = 0;
    virtual int height() const = 0;
};

// ---------------------------------------------------------------------------
// IRenderPass — a single draw pass registered with the renderer.
// Invoked inside end_frame() after the grid pass completes.
// ---------------------------------------------------------------------------
class IRenderPass
{
public:
    virtual ~IRenderPass() = default;
    virtual void record(IRenderContext& ctx) = 0;
};

// ---------------------------------------------------------------------------
// I3DRenderer — extends IBaseRenderer with render pass registration.
// Replaces the I3DPassProvider void* callback hack.
// Implemented by MetalRenderer and VkRenderer (transitively via IGridRenderer).
// ---------------------------------------------------------------------------
class I3DRenderer : public IBaseRenderer
{
public:
    virtual void register_render_pass(std::shared_ptr<IRenderPass> pass) = 0;
    virtual void unregister_render_pass() = 0;
};

} // namespace draxul
