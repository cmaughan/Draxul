#pragma once

#include <draxul/renderer.h>
#include <draxul/types.h>
#include <imgui.h>

#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace draxul::tests
{

// Minimal stub IGridHandle for use in tests.
class FakeGridHandle final : public IGridHandle
{
public:
    void set_grid_size(int, int) override {}
    void update_cells(std::span<const CellUpdate> updates) override
    {
        update_batches.emplace_back(updates.begin(), updates.end());
    }
    void set_overlay_cells(std::span<const CellUpdate> updates) override
    {
        last_overlay.assign(updates.begin(), updates.end());
    }
    void set_cursor(int, int, const CursorStyle&) override {}
    void set_default_background(Color) override {}
    void set_scroll_offset(float) override {}
    void set_viewport(const PaneDescriptor&) override {}

    std::vector<std::vector<CellUpdate>> update_batches;
    std::vector<CellUpdate> last_overlay;

    void reset()
    {
        update_batches.clear();
        last_overlay.clear();
    }
};

// Shared fake renderer implementation for use across all test files.
// Provides the union of all capabilities observed across per-file fake
// declarations. Call reset() between test cases to clear recorded state.
class FakeTermRenderer final : public IGridRenderer, public IImGuiHost, public ICaptureRenderer
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
    std::unique_ptr<IGridHandle> create_grid_handle() override
    {
        auto handle = std::make_unique<FakeGridHandle>();
        last_handle = handle.get();
        return handle;
    }
    std::unique_ptr<IGridHandle> create_overlay_handle() override
    {
        return std::make_unique<FakeGridHandle>();
    }
    void set_atlas_texture(const uint8_t*, int, int) override {}
    void update_atlas_region(int, int, int, int, const uint8_t*) override {}
    void resize(int, int) override {}
    std::pair<int, int> cell_size_pixels() const override
    {
        return { 8, 16 };
    }
    void set_cell_size(int w, int h) override
    {
        last_cell_w = w;
        last_cell_h = h;
        ++set_cell_size_calls;
    }
    void set_ascender(int a) override
    {
        last_ascender = a;
    }
    int padding() const override
    {
        return 0;
    }
    void set_default_background(Color) override {}
    void register_render_pass(std::shared_ptr<IRenderPass> pass) override
    {
        last_render_pass = std::move(pass);
    }
    void unregister_render_pass() override
    {
        last_render_pass.reset();
    }
    void set_3d_viewport(int, int, int, int) override {}
    bool initialize_imgui_backend() override
    {
        return true;
    }
    void shutdown_imgui_backend() override {}
    void rebuild_imgui_font_texture() override {}
    void begin_imgui_frame() override
    {
        if (ImGuiContext* context = ImGui::GetCurrentContext())
        {
            ImGui::SetCurrentContext(context);
            ImGuiIO& io = ImGui::GetIO();
            unsigned char* pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            (void)pixels;
            (void)width;
            (void)height;
        }
    }
    void set_imgui_draw_data(const ImDrawData* draw_data) override
    {
        last_imgui_draw_data = draw_data;
        ++set_imgui_draw_data_calls;
    }
    void request_frame_capture() override {}
    std::optional<CapturedFrame> take_captured_frame() override
    {
        return std::nullopt;
    }

    // The most recently created grid handle (non-owning pointer for test inspection).
    FakeGridHandle* last_handle = nullptr;
    std::shared_ptr<IRenderPass> last_render_pass;

    // Recorded state — read by tests (overlay forwarded from the default handle).
    std::vector<CellUpdate> last_overlay;
    const ImDrawData* last_imgui_draw_data = nullptr;
    int set_imgui_draw_data_calls = 0;
    int last_cell_w = 8;
    int last_cell_h = 16;
    int last_ascender = 0;
    int set_cell_size_calls = 0;

    void reset()
    {
        last_overlay.clear();
        last_handle = nullptr;
        last_render_pass.reset();
        last_imgui_draw_data = nullptr;
        set_imgui_draw_data_calls = 0;
        last_cell_w = 8;
        last_cell_h = 16;
        last_ascender = 0;
        set_cell_size_calls = 0;
    }
};

} // namespace draxul::tests
