#include "diagnostics_panel_host.h"

#include <draxul/base_renderer.h>
#include <draxul/imgui_host.h>

namespace draxul
{

bool DiagnosticsPanelHost::initialize(const HostContext&, IHostCallbacks& callbacks)
{
    callbacks_ = &callbacks;
    return panel_.initialize();
}

void DiagnosticsPanelHost::shutdown()
{
    panel_.shutdown();
    callbacks_ = nullptr;
    last_draw_time_.reset();
    last_render_time_.reset();
}

bool DiagnosticsPanelHost::is_running() const
{
    return true;
}

std::string DiagnosticsPanelHost::init_error() const
{
    return {};
}

void DiagnosticsPanelHost::set_viewport(const HostViewport& viewport)
{
    viewport_ = viewport;
}

void DiagnosticsPanelHost::pump()
{
}

void DiagnosticsPanelHost::draw(IFrameContext& frame)
{
    if (!panel_.visible() || panel_.layout().panel_height <= 0)
        return;

    const auto now = std::chrono::steady_clock::now();
    const bool first_visible_draw = !last_draw_time_.has_value();
    float delta_seconds = 1.0f / 60.0f;
    if (last_draw_time_)
        delta_seconds = std::chrono::duration<float>(now - *last_draw_time_).count();

    panel_.render(frame, delta_seconds);
    last_draw_time_ = now;
    last_render_time_ = now;
    frame.flush_submit_chunk();
    if (first_visible_draw && callbacks_)
        callbacks_->request_frame();
}

std::optional<std::chrono::steady_clock::time_point> DiagnosticsPanelHost::next_deadline() const
{
    return std::nullopt;
}

void DiagnosticsPanelHost::on_key(const KeyEvent& event)
{
    panel_.on_key(event);
}

void DiagnosticsPanelHost::on_text_input(const TextInputEvent& event)
{
    panel_.on_text_input(event);
}

void DiagnosticsPanelHost::on_mouse_button(const MouseButtonEvent& event)
{
    panel_.on_mouse_button(event);
}

void DiagnosticsPanelHost::on_mouse_move(const MouseMoveEvent& event)
{
    panel_.on_mouse_move(event);
}

void DiagnosticsPanelHost::on_mouse_wheel(const MouseWheelEvent& event)
{
    panel_.on_mouse_wheel(event);
}

bool DiagnosticsPanelHost::dispatch_action(std::string_view action)
{
    if (action != "toggle")
        return false;

    panel_.toggle_visible();
    if (callbacks_)
        callbacks_->request_frame();
    return true;
}

void DiagnosticsPanelHost::request_close()
{
    set_visible(false);
}

Color DiagnosticsPanelHost::default_background() const
{
    return { 0.0f, 0.0f, 0.0f, 0.0f };
}

HostRuntimeState DiagnosticsPanelHost::runtime_state() const
{
    return { .content_ready = true };
}

HostDebugState DiagnosticsPanelHost::debug_state() const
{
    return { .name = "Diagnostics" };
}

void DiagnosticsPanelHost::attach_imgui_host(IImGuiHost& host)
{
    panel_.set_imgui_backend(&host);
}

void DiagnosticsPanelHost::set_imgui_font(const std::string& path, float size_pixels)
{
    panel_.set_font(path, size_pixels);
}

void DiagnosticsPanelHost::set_visible(bool visible)
{
    panel_.set_visible(visible);
    if (!visible)
    {
        last_draw_time_.reset();
        last_render_time_.reset();
    }
    if (callbacks_)
        callbacks_->request_frame();
}

bool DiagnosticsPanelHost::visible() const
{
    return panel_.visible();
}

void DiagnosticsPanelHost::set_window_metrics(int pixel_w, int pixel_h, int cell_w, int cell_h, int padding, float pixel_scale)
{
    panel_.set_window_metrics(pixel_w, pixel_h, cell_w, cell_h, padding, pixel_scale);
}

const PanelLayout& DiagnosticsPanelHost::layout() const
{
    return panel_.layout();
}

void DiagnosticsPanelHost::update_diagnostic_state(const DiagnosticPanelState& state)
{
    panel_.update_diagnostic_state(state);
}

} // namespace draxul
