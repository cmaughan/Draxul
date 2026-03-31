#pragma once

#include <chrono>
#include <draxul/cursor_blinker.h>
#include <draxul/grid.h>
#include <draxul/grid_rendering_pipeline.h>
#include <draxul/highlight.h>
#include <draxul/host.h>
#include <memory>
#include <span>
#include <string_view>

namespace draxul
{

class GridHostBase : public IGridHost
{
public:
    bool initialize(const HostContext& context, IHostCallbacks& callbacks) override;
    void set_viewport(const HostViewport& viewport) override;
    void on_font_metrics_changed() override;
    void set_scroll_offset(float px) override;
    std::optional<std::chrono::steady_clock::time_point> next_deadline() const override;
    // I3DHost — grid hosts ignore 3D by default; override to add a background pass.
    void attach_3d_renderer(I3DRenderer&) override
    {
        // Intentionally empty — grid hosts have no 3D content; MegaCityHost overrides.
    }
    void detach_3d_renderer() override
    {
        // Intentionally empty — grid hosts have no 3D content; MegaCityHost overrides.
    }

    Color default_background() const override;
    HostRuntimeState runtime_state() const override;
    HostDebugState debug_state() const override;

    // Public accessors for grid dimensions and overlay cell pushing.
    int grid_cols() const
    {
        return grid_cols_;
    }
    int grid_rows() const
    {
        return grid_rows_;
    }
    void set_overlay_cells(std::span<const CellUpdate> cells);

protected:
    virtual bool initialize_host() = 0;
    virtual void on_viewport_changed() = 0;
    virtual void on_font_metrics_changed_impl() = 0;
    virtual std::string_view host_name() const = 0;

    IWindow& window() const;
    IGridRenderer& renderer() const;
    TextService& text_service() const;
    const HostLaunchOptions& launch_options() const
    {
        return launch_options_;
    }
    const HostViewport& viewport() const
    {
        return viewport_;
    }
    Grid& grid()
    {
        return grid_;
    }
    HighlightTable& highlights()
    {
        return highlights_;
    }
    const HighlightTable& highlights() const
    {
        return highlights_;
    }
    GridRenderingPipeline& grid_pipeline()
    {
        return *grid_pipeline_;
    }
    IHostCallbacks& callbacks() const
    {
        assert(callbacks_ != nullptr);
        return *callbacks_;
    }
    int cursor_col() const
    {
        return cursor_col_;
    }
    int cursor_row() const
    {
        return cursor_row_;
    }

    void apply_grid_size(int cols, int rows);
    void force_full_redraw();
    void flush_grid();
    void mark_activity();
    bool advance_cursor_blink(std::chrono::steady_clock::time_point now);
    void set_cursor_position(int col, int row);
    void set_cursor_style(const CursorStyle& style, const BlinkTiming& timing, bool busy = false);
    void set_cursor_busy(bool busy);
    void set_content_ready(bool ready)
    {
        content_ready_ = ready;
    }

private:
    bool dependencies_available(std::string_view operation) const;
    void apply_cursor_visibility();
    void restart_cursor_blink(std::chrono::steady_clock::time_point now);
    void update_text_input_area() const;
    void refresh_renderer_metrics();

    bool track_owner_lifetime_ = false;
    std::weak_ptr<void> owner_lifetime_;
    IWindow* window_ = nullptr;
    IGridRenderer* renderer_ = nullptr;
    TextService* text_service_ = nullptr;
    std::unique_ptr<IGridHandle> grid_handle_;
    HostLaunchOptions launch_options_;
    HostViewport viewport_ = {};
    IHostCallbacks* callbacks_ = nullptr;

    Grid grid_;
    HighlightTable highlights_;
    std::unique_ptr<GridRenderingPipeline> grid_pipeline_;
    CursorBlinker cursor_blinker_;
    CursorStyle cursor_style_ = {};
    BlinkTiming blink_timing_ = {};
    bool cursor_busy_ = false;
    int cursor_col_ = 0;
    int cursor_row_ = 0;
    int grid_cols_ = 0;
    int grid_rows_ = 0;
    size_t last_flush_dirty_cells_ = 0;
    bool content_ready_ = false;
    std::chrono::steady_clock::time_point last_activity_time_ = std::chrono::steady_clock::now();
    std::optional<Color> last_title_bar_color_;
};

} // namespace draxul
