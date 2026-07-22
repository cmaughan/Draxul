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

namespace
{

long long millis_until(std::chrono::steady_clock::time_point target)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(target - std::chrono::steady_clock::now()).count();
}

bool same_color(const Color& a, const Color& b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

bool same_cursor_style(const CursorStyle& a, const CursorStyle& b)
{
    return a.shape == b.shape
        && same_color(a.fg, b.fg)
        && same_color(a.bg, b.bg)
        && a.cell_percentage == b.cell_percentage
        && a.use_explicit_colors == b.use_explicit_colors;
}

} // namespace

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
    if (!grid_handle_)
    {
        DRAXUL_LOG_ERROR(LogCategory::App, "GridHostBase::initialize: create_grid_handle() returned null");
        return false;
    }

    // Apply the initial viewport as the scissor rect.
    const auto& vp = context.initial_viewport;
    grid_handle_->set_viewport(PaneDescriptor{ vp.pixel_pos, vp.pixel_size });

    grid_pipeline_ = std::make_unique<GridRenderingPipeline>(grid_, highlights_, *text_service_);
    grid_pipeline_->set_renderer(renderer_);
    grid_pipeline_->set_grid_handle(grid_handle_.get());
    grid_pipeline_->set_enable_ligatures(launch_options_.enable_ligatures);
    grid_pipeline_->set_url_detection_enabled(launch_options_.url_detection);
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
    frame.flush_submit_chunk();
}

void GridHostBase::on_focus_gained()
{
    if (grid_handle_)
        grid_handle_->set_cursor_visible(true);
    // Restart the blink cycle so the cursor starts in the visible phase.
    // Without this, the cursor blinker may be in a hidden phase from before
    // the tab switch, leaving the cursor invisible until the next
    // blink transition.
    restart_cursor_blink(std::chrono::steady_clock::now());
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
    launch_options_.url_detection = config.url_detection;
    if (grid_pipeline_)
    {
        grid_pipeline_->set_enable_ligatures(config.enable_ligatures);
        grid_pipeline_->set_url_detection_enabled(config.url_detection);
    }
}

std::optional<std::chrono::steady_clock::time_point> GridHostBase::next_deadline() const
{
    const auto blink_deadline = cursor_blinker_.next_deadline();
    if (!cursor_suppressed_until_)
        return blink_deadline;
    if (!blink_deadline || *cursor_suppressed_until_ < *blink_deadline)
        return cursor_suppressed_until_;
    return blink_deadline;
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
    bool suppression_changed = false;
    if (cursor_suppressed_until_ && now >= *cursor_suppressed_until_)
    {
        if (log_would_emit(LogLevel::Trace, LogCategory::Input))
            log_printf(LogLevel::Trace, LogCategory::Input, "cursor trace: suppression expired");
        cursor_suppressed_until_.reset();
        suppression_changed = true;
    }

    const bool blink_changed = cursor_blinker_.advance(now);
    if (blink_changed && log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: blink advance changed visibility=%d next_deadline=%lldms",
            cursor_blinker_.visible() ? 1 : 0,
            cursor_blinker_.next_deadline()
                ? static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                      *cursor_blinker_.next_deadline() - now)
                          .count())
                : -1LL);
    }
    if (!blink_changed && !suppression_changed)
        return false;
    apply_cursor_visibility();
    return true;
}

void GridHostBase::begin_cursor_publish_batch()
{
    PERF_MEASURE();
    ++cursor_publish_batch_depth_;
}

void GridHostBase::end_cursor_publish_batch()
{
    PERF_MEASURE();
    if (cursor_publish_batch_depth_ <= 0)
        return;
    --cursor_publish_batch_depth_;
    if (cursor_publish_batch_depth_ != 0)
        return;
    if (cursor_publish_dirty_)
        apply_cursor_visibility();
    if (text_input_area_dirty_)
        update_text_input_area();
}

void GridHostBase::set_cursor_position(int col, int row, CursorBlinkUpdate blink_update)
{
    PERF_MEASURE();
    if (!dependencies_available("set_cursor_position"))
        return;
    const int new_col = std::max(0, col);
    const int new_row = std::max(0, row);
    const bool changed = cursor_col_ != new_col || cursor_row_ != new_row;
    cursor_col_ = new_col;
    cursor_row_ = new_row;
    if (blink_update == CursorBlinkUpdate::Restart)
        restart_cursor_blink(std::chrono::steady_clock::now());
    else if (changed)
        apply_cursor_visibility();
    update_text_input_area();
}

void GridHostBase::set_cursor_display_override(std::optional<std::pair<int, int>> override)
{
    PERF_MEASURE();
    if (!dependencies_available("set_cursor_display_override"))
        return;
    if (cursor_display_override_ == override)
        return;
    cursor_display_override_ = override;
    apply_cursor_visibility();
    update_text_input_area();
}

void GridHostBase::set_cursor_style(const CursorStyle& style, const BlinkTiming& timing, bool busy)
{
    PERF_MEASURE();
    if (!dependencies_available("set_cursor_style"))
        return;
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: set_cursor_style shape=%d busy=%d blinkwait=%d blinkon=%d blinkoff=%d",
            static_cast<int>(style.shape),
            busy ? 1 : 0,
            timing.blinkwait,
            timing.blinkon,
            timing.blinkoff);
    }
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
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
        log_printf(LogLevel::Trace, LogCategory::Input, "cursor trace: set_cursor_busy busy=%d", busy ? 1 : 0);
    cursor_busy_ = busy;
    restart_cursor_blink(std::chrono::steady_clock::now());
}

void GridHostBase::suppress_cursor_until(std::chrono::steady_clock::time_point until)
{
    PERF_MEASURE();
    if (!dependencies_available("suppress_cursor_until"))
        return;
    if (!cursor_suppressed_until_ || until > *cursor_suppressed_until_)
        cursor_suppressed_until_ = until;
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: suppress_cursor_until remaining=%lldms",
            millis_until(*cursor_suppressed_until_));
    }
    apply_cursor_visibility();
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
    if (cursor_publish_batch_depth_ > 0)
    {
        cursor_publish_dirty_ = true;
        return;
    }
    cursor_publish_dirty_ = false;
    const auto now = std::chrono::steady_clock::now();
    if (cursor_suppressed_until_ && now >= *cursor_suppressed_until_)
        cursor_suppressed_until_.reset();

    const bool visible = cursor_blinker_.visible() && !cursor_suppressed_until_;
    int display_col = cursor_col_;
    int display_row = cursor_row_;
    if (cursor_display_override_)
    {
        display_col = cursor_display_override_->first;
        display_row = cursor_display_override_->second;
    }
    display_col = std::clamp(display_col, 0, std::max(0, grid_cols_ - 1));
    display_row = std::clamp(display_row, 0, std::max(0, grid_rows_ - 1));

    const int visible_col = visible ? display_col : -1;
    const int visible_row = visible ? display_row : -1;
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: apply visible=%d blink_visible=%d suppressed=%d emitted=(%d,%d) logical=(%d,%d) display=(%d,%d) override=%d",
            visible ? 1 : 0,
            cursor_blinker_.visible() ? 1 : 0,
            cursor_suppressed_until_ ? 1 : 0,
            visible_col,
            visible_row,
            cursor_col_,
            cursor_row_,
            display_col,
            display_row,
            cursor_display_override_ ? 1 : 0);
    }
    if (grid_handle_)
    {
        const bool changed = !cursor_emission_valid_
            || last_emitted_cursor_col_ != visible_col
            || last_emitted_cursor_row_ != visible_row
            || !same_cursor_style(last_emitted_cursor_style_, cursor_style_);
        if (changed)
        {
            grid_handle_->set_cursor(visible_col, visible_row, cursor_style_);
            cursor_emission_valid_ = true;
            last_emitted_cursor_col_ = visible_col;
            last_emitted_cursor_row_ = visible_row;
            last_emitted_cursor_style_ = cursor_style_;
            callbacks().request_frame();
        }
    }
}

void GridHostBase::restart_cursor_blink(std::chrono::steady_clock::time_point now)
{
    PERF_MEASURE();
    cursor_blinker_.restart(now, cursor_busy_, blink_timing_);
    if (log_would_emit(LogLevel::Trace, LogCategory::Input))
    {
        log_printf(LogLevel::Trace,
            LogCategory::Input,
            "cursor trace: restart_blink busy=%d visible=%d next_deadline=%lldms",
            cursor_busy_ ? 1 : 0,
            cursor_blinker_.visible() ? 1 : 0,
            cursor_blinker_.next_deadline()
                ? static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                      *cursor_blinker_.next_deadline() - now)
                          .count())
                : -1LL);
    }
    apply_cursor_visibility();
}

void GridHostBase::update_text_input_area() const
{
    PERF_MEASURE();
    if (!dependencies_available("update_text_input_area"))
        return;
    if (cursor_publish_batch_depth_ > 0)
    {
        const_cast<GridHostBase*>(this)->text_input_area_dirty_ = true;
        return;
    }
    const_cast<GridHostBase*>(this)->text_input_area_dirty_ = false;
    auto [cell_w, cell_h] = renderer_->cell_size_pixels();
    int display_col = cursor_col_;
    int display_row = cursor_row_;
    if (cursor_display_override_)
    {
        display_col = cursor_display_override_->first;
        display_row = cursor_display_override_->second;
    }
    display_col = std::clamp(display_col, 0, std::max(0, grid_cols_ - 1));
    display_row = std::clamp(display_row, 0, std::max(0, grid_rows_ - 1));
    const int x = viewport_.pixel_pos.x + renderer_->padding() + display_col * cell_w;
    const int y = viewport_.pixel_pos.y + renderer_->padding() + display_row * cell_h;
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
