#pragma once

#include "fake_grid_handle.h"

#include <draxul/renderer.h>
#include <draxul/types.h>
#include <imgui.h>

#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace draxul::tests
{

// Shared fake renderer implementation for use across all test files.
// Provides the union of all capabilities observed across per-file fake
// declarations. Call reset() between test cases to clear recorded state.
class FakeTermRenderer final : public IGridRenderer, public IImGuiHost, public ICaptureRenderer
{
public:
    class FakeFrameContext final : public IFrameContext
    {
    public:
        explicit FakeFrameContext(FakeTermRenderer& renderer)
            : renderer_(renderer)
        {
        }

        void draw_grid_handle(IGridHandle& handle) override
        {
            renderer_.last_drawn_handle = &handle;
            ++renderer_.draw_grid_handle_calls;
        }

        void record_render_pass(IRenderPass& pass,
            const RenderViewport& viewport) override
        {
            renderer_.last_recorded_render_pass = &pass;
            renderer_.recorded_render_viewports.push_back(viewport);
            ++renderer_.record_render_pass_calls;
        }

        void render_imgui(const ImDrawData* draw_data, ImGuiContext*) override
        {
            renderer_.last_imgui_draw_data = draw_data;
            ++renderer_.render_imgui_calls;
        }

        void flush_submit_chunk() override
        {
            ++renderer_.flush_submit_chunk_calls;
        }

    private:
        FakeTermRenderer& renderer_;
    };

    FakeTermRenderer()
        : frame_context(*this)
    {
    }

    bool initialize(IWindow&) override
    {
        return true;
    }
    void shutdown() override {}
    IFrameContext* begin_frame() override
    {
        ++begin_frame_calls;
        return &frame_context;
    }
    void end_frame() override
    {
        ++end_frame_calls;
    }
    std::unique_ptr<IGridHandle> create_grid_handle() override
    {
        ++create_grid_handle_calls;
        if (fail_create_grid_handle)
        {
            last_handle = nullptr;
            return nullptr;
        }
        auto handle = std::make_unique<FakeGridHandle>();
        last_handle = handle.get();
        return handle;
    }

    // When set, create_grid_handle() returns nullptr — used by tests that
    // exercise the WI 48 null-handle path in GridHostBase / CommandPaletteHost.
    bool fail_create_grid_handle = false;
    int create_grid_handle_calls = 0;

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
    void request_frame_capture() override {}
    std::optional<CapturedFrame> take_captured_frame() override
    {
        return std::nullopt;
    }

    // The most recently created grid handle (non-owning pointer for test inspection).
    FakeGridHandle* last_handle = nullptr;
    IGridHandle* last_drawn_handle = nullptr;
    IRenderPass* last_recorded_render_pass = nullptr;
    std::vector<RenderViewport> recorded_render_viewports;

    // Recorded state — read by tests (overlay forwarded from the default handle).
    std::vector<CellUpdate> last_overlay;
    const ImDrawData* last_imgui_draw_data = nullptr;
    int draw_grid_handle_calls = 0;
    int record_render_pass_calls = 0;
    int render_imgui_calls = 0;
    int flush_submit_chunk_calls = 0;
    int last_cell_w = 8;
    int last_cell_h = 16;
    int last_ascender = 0;
    int set_cell_size_calls = 0;
    int begin_frame_calls = 0;
    int end_frame_calls = 0;
    FakeFrameContext frame_context;

    void reset()
    {
        last_overlay.clear();
        last_handle = nullptr;
        last_drawn_handle = nullptr;
        last_recorded_render_pass = nullptr;
        recorded_render_viewports.clear();
        last_imgui_draw_data = nullptr;
        draw_grid_handle_calls = 0;
        record_render_pass_calls = 0;
        render_imgui_calls = 0;
        flush_submit_chunk_calls = 0;
        last_cell_w = 8;
        last_cell_h = 16;
        last_ascender = 0;
        set_cell_size_calls = 0;
        begin_frame_calls = 0;
        end_frame_calls = 0;
    }
};

} // namespace draxul::tests
