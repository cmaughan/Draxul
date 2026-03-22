#pragma once

#include <draxul/pane_descriptor.h>
#include <draxul/renderer.h>
#include <draxul/types.h>

#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace draxul::tests
{

// Shared fake IRenderer implementation for use across all test files.
// Provides the union of all capabilities observed across per-file fake
// declarations. Call reset() between test cases to clear recorded state.
class FakeTermRenderer final : public IRenderer
{
public:
    bool initialize(IWindow&) override
    {
        return true;
    }
    void shutdown() override {}
    bool begin_frame() override
    {
        return true;
    }
    void end_frame() override {}
    void set_grid_size(int, int) override {}
    void update_cells(std::span<const CellUpdate>) override {}
    void set_overlay_cells(std::span<const CellUpdate> updates) override
    {
        last_overlay.assign(updates.begin(), updates.end());
    }
    void set_atlas_texture(const uint8_t*, int, int) override {}
    void update_atlas_region(int, int, int, int, const uint8_t*) override {}
    void set_cursor(int, int, const CursorStyle&) override {}
    void resize(int, int) override {}
    std::pair<int, int> cell_size_pixels() const override
    {
        return { 8, 16 };
    }
    void set_cell_size(int, int) override {}
    void set_ascender(int) override {}
    int padding() const override
    {
        return 0;
    }
    void set_default_background(Color) override {}
    void set_scroll_offset(float) override {}
    void register_render_pass(std::shared_ptr<IRenderPass>) override {}
    void unregister_render_pass() override {}

    // Multi-pane API stubs
    int alloc_pane() override
    {
        return 0;
    }
    void free_pane(int) override {}
    void set_pane_viewport(int, const PaneDescriptor&) override {}
    void set_grid_size(int, int, int) override {}
    void update_cells(int, std::span<const CellUpdate>) override {}
    void set_overlay_cells(int, std::span<const CellUpdate>) override {}
    void set_cursor(int, int, int, const CursorStyle&) override {}
    void set_default_background(int, Color) override {}
    void set_scroll_offset(int, float) override {}
    bool initialize_imgui_backend() override
    {
        return true;
    }
    void shutdown_imgui_backend() override {}
    void rebuild_imgui_font_texture() override {}
    void begin_imgui_frame() override {}
    void set_imgui_draw_data(const ImDrawData*) override {}
    void request_frame_capture() override {}
    std::optional<CapturedFrame> take_captured_frame() override
    {
        return std::nullopt;
    }

    // Recorded state — read by tests.
    std::vector<CellUpdate> last_overlay;

    void reset()
    {
        last_overlay.clear();
    }
};

} // namespace draxul::tests
