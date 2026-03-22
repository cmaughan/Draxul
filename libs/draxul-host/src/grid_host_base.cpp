#include <draxul/grid_host_base.h>

#include <algorithm>
#include <draxul/text_service.h>
#include <draxul/window.h>
#include <memory>

namespace draxul
{

bool GridHostBase::initialize(const HostContext& context, HostCallbacks callbacks)
{
    window_ = &context.window;
    renderer_ = &context.grid_renderer;
    text_service_ = &context.text_service;
    launch_options_ = context.launch_options;
    viewport_ = context.initial_viewport;
    callbacks_ = std::move(callbacks);
    grid_pipeline_ = std::make_unique<GridRenderingPipeline>(grid_, highlights_, *text_service_);
    grid_pipeline_->set_renderer(renderer_);
    grid_pipeline_->set_pane_id(pane_id_);
    grid_pipeline_->set_enable_ligatures(launch_options_.enable_ligatures);
    refresh_renderer_metrics();
    on_viewport_changed();
    return initialize_host();
}

void GridHostBase::set_viewport(const HostViewport& viewport)
{
    viewport_ = viewport;
    on_viewport_changed();
    update_text_input_area();
}

void GridHostBase::on_font_metrics_changed()
{
    refresh_renderer_metrics();
    force_full_redraw();
    on_font_metrics_changed_impl();
    update_text_input_area();
}

std::optional<std::chrono::steady_clock::time_point> GridHostBase::next_deadline() const
{
    return cursor_blinker_.next_deadline();
}

Color GridHostBase::default_background() const
{
    return highlights_.default_bg();
}

HostRuntimeState GridHostBase::runtime_state() const
{
    HostRuntimeState state;
    state.content_ready = content_ready_;
    state.last_activity_time = last_activity_time_;
    return state;
}

HostDebugState GridHostBase::debug_state() const
{
    HostDebugState state;
    state.name = std::string(host_name());
    state.grid_cols = grid_cols_;
    state.grid_rows = grid_rows_;
    state.dirty_cells = last_flush_dirty_cells_;
    return state;
}

IWindow& GridHostBase::window() const
{
    return *window_;
}

IGridRenderer& GridHostBase::renderer() const
{
    return *renderer_;
}

TextService& GridHostBase::text_service() const
{
    return *text_service_;
}

void GridHostBase::set_pane_id(int id)
{
    pane_id_ = id;
    if (grid_pipeline_)
        grid_pipeline_->set_pane_id(id);
}

void GridHostBase::apply_grid_size(int cols, int rows)
{
    cols = std::max(1, cols);
    rows = std::max(1, rows);
    grid_cols_ = cols;
    grid_rows_ = rows;
    grid_.resize(cols, rows);
    renderer_->set_grid_size(pane_id_, cols, rows);
    grid_pipeline_->force_full_atlas_upload();
    update_text_input_area();
    callbacks_.request_frame();
}

void GridHostBase::force_full_redraw()
{
    grid_.mark_all_dirty();
    grid_pipeline_->force_full_atlas_upload();
}

void GridHostBase::flush_grid()
{
    content_ready_ = true;
    last_flush_dirty_cells_ = grid_.dirty_cell_count();
    last_activity_time_ = std::chrono::steady_clock::now();
    grid_pipeline_->flush();
    const Color bg = highlights_.default_bg();
    renderer_->set_default_background(pane_id_, bg);
    if (bg != last_title_bar_color_)
    {
        last_title_bar_color_ = bg;
        window_->set_title_bar_color(bg);
    }
    apply_cursor_visibility();
    callbacks_.request_frame();
}

void GridHostBase::mark_activity()
{
    last_activity_time_ = std::chrono::steady_clock::now();
}

bool GridHostBase::advance_cursor_blink(std::chrono::steady_clock::time_point now)
{
    if (!cursor_blinker_.advance(now))
        return false;
    apply_cursor_visibility();
    return true;
}

void GridHostBase::set_cursor_position(int col, int row)
{
    cursor_col_ = std::max(0, col);
    cursor_row_ = std::max(0, row);
    restart_cursor_blink(std::chrono::steady_clock::now());
    update_text_input_area();
}

void GridHostBase::set_cursor_style(const CursorStyle& style, const BlinkTiming& timing, bool busy)
{
    cursor_style_ = style;
    blink_timing_ = timing;
    cursor_busy_ = busy;
    restart_cursor_blink(std::chrono::steady_clock::now());
}

void GridHostBase::set_cursor_busy(bool busy)
{
    cursor_busy_ = busy;
    restart_cursor_blink(std::chrono::steady_clock::now());
}

void GridHostBase::apply_cursor_visibility()
{
    const int visible_col = cursor_blinker_.visible() ? cursor_col_ : -1;
    const int visible_row = cursor_blinker_.visible() ? cursor_row_ : -1;
    renderer_->set_cursor(pane_id_, visible_col, visible_row, cursor_style_);
    callbacks_.request_frame();
}

void GridHostBase::restart_cursor_blink(std::chrono::steady_clock::time_point now)
{
    cursor_blinker_.restart(now, cursor_busy_, blink_timing_);
    apply_cursor_visibility();
}

void GridHostBase::update_text_input_area() const
{
    auto [cell_w, cell_h] = renderer_->cell_size_pixels();
    const int x = viewport_.pixel_x + renderer_->padding() + cursor_col_ * cell_w;
    const int y = viewport_.pixel_y + renderer_->padding() + cursor_row_ * cell_h;
    callbacks_.set_text_input_area(x, y, cell_w, cell_h);
}

void GridHostBase::refresh_renderer_metrics()
{
    const auto& metrics = text_service_->metrics();
    renderer_->set_cell_size(metrics.cell_width, metrics.cell_height);
    renderer_->set_ascender(metrics.ascender);
}

} // namespace draxul
