#pragma once
#include <concepts>
#include <draxul/base_renderer.h>
#include <draxul/capture_renderer.h>
#include <draxul/imgui_host.h>
#include <draxul/pane_descriptor.h>
#include <draxul/types.h>
#include <memory>
#include <span>
#include <utility>

namespace draxul
{

class IWindow;

// ---------------------------------------------------------------------------
// IGridHandle — per-host grid rendering handle.
// Each host that renders a grid owns one IGridHandle. The handle encapsulates
// the host's GPU buffer, CPU-side cell state, and screen clip rect
// (PaneDescriptor). All per-host grid operations go through this handle;
// the renderer itself is only responsible for the swapchain and global
// resources (atlas texture, cell metrics, frame lifecycle).
// ---------------------------------------------------------------------------
class IGridHandle
{
public:
    virtual ~IGridHandle() = default;
    virtual void set_grid_size(int cols, int rows) = 0;
    virtual void update_cells(std::span<const CellUpdate> updates) = 0;
    virtual void set_overlay_cells(std::span<const CellUpdate> updates) = 0;
    virtual void set_cursor(int col, int row, const CursorStyle& style) = 0;
    virtual void set_cursor_visible(bool visible) = 0;
    virtual void set_default_background(Color bg) = 0;
    virtual void set_scroll_offset(float px) = 0;
    // Update the screen region (scissor rect) for this host's draw calls.
    // desc is in physical pixels; pass {0,0,0,0} to use the full window.
    virtual void set_viewport(const PaneDescriptor& desc) = 0;
};

// ---------------------------------------------------------------------------
// IGridRenderer — grid rendering contract.
// Concrete backends (MetalRenderer, VkRenderer) implement IGridRenderer.
//
// Each host calls create_grid_handle() once during initialisation to obtain
// its own IGridHandle. All per-cell state and GPU memory live in the handle;
// IGridRenderer exposes only global, shared resources.
// ---------------------------------------------------------------------------
class IGridRenderer : public IBaseRenderer
{
public:
    ~IGridRenderer() override = default;

    // Create a per-host grid handle that owns its own GPU buffer and state.
    // The caller (GridHostBase) owns the returned unique_ptr.
    virtual std::unique_ptr<IGridHandle> create_grid_handle() = 0;

    // Global (shared across all handles) — atlas texture and cell metrics.
    virtual void set_atlas_texture(const uint8_t* data, int w, int h) = 0;
    virtual void update_atlas_region(int x, int y, int w, int h, const uint8_t* data) = 0;
    virtual std::pair<int, int> cell_size_pixels() const = 0;
    virtual void set_cell_size(int w, int h) = 0;
    virtual void set_ascender(int a) = 0;
    virtual int padding() const = 0;
};

// ---------------------------------------------------------------------------
// RendererBundle — owning wrapper returned by create_renderer().
// Holds the concrete grid renderer plus any optional side capabilities so App
// code never needs to cast or store a widened renderer interface directly.
// ---------------------------------------------------------------------------
struct RendererBundle
{
    std::unique_ptr<IGridRenderer> impl;
    IImGuiHost* imgui_host = nullptr;
    ICaptureRenderer* capture_renderer = nullptr;

    RendererBundle() = default;

    template <typename T>
        requires std::derived_from<T, IGridRenderer>
    explicit RendererBundle(std::unique_ptr<T> renderer)
    {
        reset(std::move(renderer));
    }

    template <typename T>
        requires std::derived_from<T, IGridRenderer>
    void reset(std::unique_ptr<T> renderer)
    {
        T* raw = renderer.get();
        impl = std::move(renderer);
        if constexpr (std::derived_from<T, IImGuiHost>)
            imgui_host = raw;
        else
            imgui_host = nullptr;
        if constexpr (std::derived_from<T, ICaptureRenderer>)
            capture_renderer = raw;
        else
            capture_renderer = nullptr;
    }

    void reset()
    {
        impl.reset();
        imgui_host = nullptr;
        capture_renderer = nullptr;
    }

    IGridRenderer* grid() const
    {
        return impl.get();
    }
    IImGuiHost* imgui() const
    {
        return imgui_host;
    }
    ICaptureRenderer* capture() const
    {
        return capture_renderer;
    }

    explicit operator bool() const
    {
        return impl != nullptr;
    }
};

struct RendererOptions
{
    bool wait_for_vblank = true;
};

RendererBundle create_renderer(int atlas_size = kAtlasSize, RendererOptions options = {});

} // namespace draxul
