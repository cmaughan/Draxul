#include <draxul/grid_host_base.h>

#include <algorithm>
#include <draxul/log.h>
#include <draxul/pane_descriptor.h>
#include <draxul/perf_timing.h>
#include <draxul/text_service.h>
#include <draxul/window.h>
#include <memory>

namespace draxul
{

bool GridHostBase::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    PERF_MEASURE();
    owner_lifetime_ = context.owner_lifetime;
    track_owner_lifetime_ = !owner_lifetime_.expired();
    window_ = context.window;
    renderer_ = context.grid_renderer;
    text_service_ = context.text_service;
    launch_options_ = context.launch_options;
    viewport_ = context.initial_viewport;
    callbacks_ = &callbacks;

    // Create a per-host GPU handle. The handle owns its own buffer and state.
    grid_handle_ = renderer_->create_grid_handle();

    // Apply the initial viewport as the scissor rect.
    const auto& vp = context.initial_viewport;
    grid_handle_->set_viewport(PaneDescriptor{ vp.pixel_pos, vp.pixel_size });

    grid_pipeline_ = std::make_unique<GridRenderingPipeline>(grid_, highlights_, *text_service_);
    grid_pipeline_->set_renderer(renderer_);
    grid_pipeline_->set_grid_handle(grid_handle_.get());
    grid_pipeline_->set_enable_ligatures(launch_options_.enable_ligatures);
    refresh_renderer_metrics();
    on_viewport_changed();
    return initialize_host();
}

void GridHostBase::set_viewport(const HostViewport& viewport)
{
    PERF_MEASURE();
    viewport_ = viewport;
    if (!dependencies_available("set_viewport"))
        return;
    if (grid_handle_)
        grid_handle_->set_viewport(PaneDescriptor{ viewport.pixel_pos, viewport.pixel_size });
    on_viewport_changed();
    update_text_input_area();
}

void GridHostBase::set_scroll_offset(float px)
{
    if (grid_handle_)
        grid_handle_->set_scroll_offset(px);
}

void GridHostBase::draw(IFrameContext& frame)
{
    if (grid_handle_)
        frame.draw_grid_handle(*grid_handle_);
}

void GridHostBase::on_focus_gained()
{
    if (grid_handle_)
        grid_handle_->set_cursor_visible(true);
}

void GridHostBase::on_focus_lost()
{
    if (grid_handle_)
        grid_handle_->set_cursor_visible(false);
}

void GridHostBase::on_font_metrics_changed()
{
    PERF_MEASURE();
    if (!dependencies_available("on_font_metrics_changed"))
        return;
    refresh_renderer_metrics();
    force_full_redraw();
    on_font_metrics_changed_impl();
    update_text_input_area();
}

void GridHostBase::on_config_reloaded(const HostReloadConfig& config)
{
    launch_options_.enable_ligatures = config.enable_ligatures;
    if (grid_pipeline_)
        grid_pipeline_->set_enable_ligatures(config.enable_ligatures);
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
    assert(dependencies_available("window"));
    return *window_;
}

IGridRenderer& GridHostBase::renderer() const
{
    assert(dependencies_available("renderer"));
    return *renderer_;
}

TextService& GridHostBase::text_service() const
{
    assert(dependencies_available("text_service"));
    return *text_service_;
}

void GridHostBase::apply_grid_size(int cols, int rows)
{
    PERF_MEASURE();
    if (!dependencies_available("apply_grid_size"))
        return;
    cols = std::max(1, cols);
    rows = std::max(1, rows);
    grid_cols_ = cols;
    grid_rows_ = rows;
    grid_.resize(cols, rows);
    if (grid_handle_)
        grid_handle_->set_grid_size(cols, rows);
    grid_pipeline_->force_full_atlas_upload();
    update_text_input_area();
    callbacks().request_frame();
}

void GridHostBase::force_full_redraw()
{
    PERF_MEASURE();
    if (!dependencies_available("force_full_redraw"))
        return;
    grid_.mark_all_dirty();
    grid_pipeline_->force_full_atlas_upload();
}

void GridHostBase::flush_grid()
{
    PERF_MEASURE();
    if (!dependencies_available("flush_grid"))
        return;
    content_ready_ = true;
    last_flush_dirty_cells_ = grid_.dirty_cell_count();
    last_activity_time_ = std::chrono::steady_clock::now();
    grid_pipeline_->flush();
    const Color bg = highlights_.default_bg();
    if (grid_handle_)
        grid_handle_->set_default_background(bg);
    if (bg != last_title_bar_color_)
    {
        last_title_bar_color_ = bg;
        window_->set_title_bar_color(bg);
    }
    apply_cursor_visibility();
    callbacks().request_frame();
}

void GridHostBase::set_overlay_cells(std::span<const CellUpdate> cells)
{
    if (grid_handle_)
        grid_handle_->set_overlay_cells(cells);
}

void GridHostBase::mark_activity()
{
    last_activity_time_ = std::chrono::steady_clock::now();
}

bool GridHostBase::advance_cursor_blink(std::chrono::steady_clock::time_point now)
{
    PERF_MEASURE();
    if (!cursor_blinker_.advance(now))
        return false;
    apply_cursor_visibility();
    return true;
}

void GridHostBase::set_cursor_position(int col, int row)
{
    PERF_MEASURE();
    if (!dependencies_available("set_cursor_position"))
        return;
    cursor_col_ = std::max(0, col);
    cursor_row_ = std::max(0, row);
    restart_cursor_blink(std::chrono::steady_clock::now());
    update_text_input_area();
}

void GridHostBase::set_cursor_style(const CursorStyle& style, const BlinkTiming& timing, bool busy)
{
    PERF_MEASURE();
    if (!dependencies_available("set_cursor_style"))
        return;
    cursor_style_ = style;
    blink_timing_ = timing;
    cursor_busy_ = busy;
    restart_cursor_blink(std::chrono::steady_clock::now());
}

void GridHostBase::set_cursor_busy(bool busy)
{
    PERF_MEASURE();
    if (!dependencies_available("set_cursor_busy"))
        return;
    cursor_busy_ = busy;
    restart_cursor_blink(std::chrono::steady_clock::now());
}

bool GridHostBase::dependencies_available(std::string_view operation) const
{
    if (!window_ || !renderer_ || !text_service_ || !callbacks_)
        return false;
    if (!track_owner_lifetime_ || !owner_lifetime_.expired())
        return true;

    DRAXUL_LOG_WARN(LogCategory::App,
        "GridHostBase ignored '%.*s' after host owner teardown.",
        static_cast<int>(operation.size()), operation.data());
    return false;
}

void GridHostBase::apply_cursor_visibility()
{
    PERF_MEASURE();
    if (!dependencies_available("apply_cursor_visibility"))
        return;
    const int visible_col = cursor_blinker_.visible() ? cursor_col_ : -1;
    const int visible_row = cursor_blinker_.visible() ? cursor_row_ : -1;
    if (grid_handle_)
        grid_handle_->set_cursor(visible_col, visible_row, cursor_style_);
    callbacks().request_frame();
}

void GridHostBase::restart_cursor_blink(std::chrono::steady_clock::time_point now)
{
    PERF_MEASURE();
    cursor_blinker_.restart(now, cursor_busy_, blink_timing_);
    apply_cursor_visibility();
}

void GridHostBase::update_text_input_area() const
{
    PERF_MEASURE();
    if (!dependencies_available("update_text_input_area"))
        return;
    auto [cell_w, cell_h] = renderer_->cell_size_pixels();
    const int x = viewport_.pixel_pos.x + renderer_->padding() + cursor_col_ * cell_w;
    const int y = viewport_.pixel_pos.y + renderer_->padding() + cursor_row_ * cell_h;
    callbacks().set_text_input_area(x, y, cell_w, cell_h);
}

void GridHostBase::refresh_renderer_metrics()
{
    PERF_MEASURE();
    if (!dependencies_available("refresh_renderer_metrics"))
        return;
    const auto& metrics = text_service_->metrics();
    renderer_->set_cell_size(metrics.cell_width, metrics.cell_height);
    renderer_->set_ascender(metrics.ascender);
}

} // namespace draxul
