#pragma once

#include <cstdint>
#include <draxul/types.h>
#include <memory>

struct ImDrawData;
struct ImGuiContext;

namespace draxul
{

class IWindow;
class IGridHandle;
struct RenderViewport
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

class IRenderPass;

// ---------------------------------------------------------------------------
// IFrameContext — live per-frame draw surface returned by begin_frame().
// Hosts encode their grid / 3D / ImGui work against this context immediately.
// end_frame() closes the frame and submits / presents it.
// ---------------------------------------------------------------------------
class IFrameContext
{
public:
    virtual ~IFrameContext() = default;
    virtual void draw_grid_handle(IGridHandle& handle) = 0;
    virtual void record_render_pass(IRenderPass& pass, const RenderViewport& viewport) = 0;
    virtual void render_imgui(const ImDrawData* draw_data, ImGuiContext* context) = 0;
    virtual void flush_submit_chunk() = 0;
};

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
    virtual IFrameContext* begin_frame() = 0;
    virtual void end_frame() = 0;
    virtual void resize(int pixel_w, int pixel_h) = 0;
    virtual void set_default_background(Color bg) = 0;
    // Synchronize outstanding GPU work before externally-owned render
    // resources are destroyed. Dynamic plugin teardown uses this conservative
    // v1 boundary; ordinary hosts should not call it per frame.
    virtual void wait_idle() {}
};

// ---------------------------------------------------------------------------
// IRenderContext — per-frame platform handles handed to each IRenderPass.
// Platform-specific accessors live on the concrete subclass (MetalRenderContext
// or VkRenderContext).  Render passes that need native handles static_cast to
// the platform-specific type — a wrong cast is a compile error, not a silent
// crash.
// ---------------------------------------------------------------------------
class IRenderContext
{
public:
    virtual ~IRenderContext() = default;
    virtual int width() const = 0;
    virtual int height() const = 0;

    // Pane viewport within the framebuffer. Defaults to the full framebuffer
    // so existing render passes work unmodified. Override to confine a pass
    // to a split-pane region.
    virtual int viewport_x() const
    {
        return 0;
    }
    virtual int viewport_y() const
    {
        return 0;
    }
    virtual int viewport_w() const
    {
        return width();
    }
    virtual int viewport_h() const
    {
        return height();
    }

    // Buffered frame slot information for passes that need per-frame transient
    // resources. Backends that do not expose multiple frame slots can keep the
    // defaults of frame 0 / one buffered frame.
    virtual uint32_t frame_index() const
    {
        return 0;
    }
    virtual uint32_t buffered_frame_count() const
    {
        return 1;
    }
};

// ---------------------------------------------------------------------------
// IRenderPass — a single draw pass registered with the renderer.
// record_prepass() is called before the main render pass begins (no active
// encoder/render pass), allowing implementations to create their own render
// targets and render pass. record() is called inside end_frame() after the
// grid pass completes, within the main render pass.
// ---------------------------------------------------------------------------
class IRenderPass
{
public:
    virtual ~IRenderPass() = default;
    virtual bool requires_main_depth_attachment() const
    {
        return false;
    }
    virtual void record_prepass(IRenderContext& ctx)
    {
        (void)ctx;
    }
    virtual void record(IRenderContext& ctx) = 0;
};

} // namespace draxul
