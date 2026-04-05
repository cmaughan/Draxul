#include "command_palette_host.h"

#include "gui_action_handler.h"
#include <algorithm>
#include <cstring>
#include <draxul/gui/palette_renderer.h>
#include <draxul/log.h>
#include <draxul/text_service.h>

namespace draxul
{

CommandPaletteHost::CommandPaletteHost(Deps deps)
    : deps_(std::move(deps))
{
}

bool CommandPaletteHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    callbacks_ = &callbacks;
    renderer_ = context.grid_renderer;
    text_service_ = context.text_service;

    if (!renderer_)
        return false;

    // Apply the initial viewport so pixel dimensions are known before the first pump().
    set_viewport(context.initial_viewport);

    // Grid handle is created on-demand when the palette opens, so it's always
    // the last handle in the renderer's draw order.

    // Wire the internal CommandPalette deps.
    CommandPalette::Deps palette_deps;
    palette_deps.gui_action_handler = deps_.gui_action_handler;
    palette_deps.keybindings = deps_.keybindings;
    palette_deps.request_frame = [this]() {
        if (callbacks_)
            callbacks_->request_frame();
    };
    palette_deps.on_closed = [this]() {
        handle_.reset(); // destroy handle — removed from renderer draw list
    };
    palette_ = CommandPalette(std::move(palette_deps));

    return true;
}

void CommandPaletteHost::shutdown()
{
    palette_.close();
    if (handle_)
        handle_.reset();
}

bool CommandPaletteHost::is_running() const
{
    return true;
}

std::string CommandPaletteHost::init_error() const
{
    return {};
}

void CommandPaletteHost::set_viewport(const HostViewport& viewport)
{
    pixel_w_ = std::max(1, viewport.pixel_size.x / 2);
    pixel_h_ = std::max(1, viewport.pixel_size.y / 2);
    pixel_x_ = viewport.pixel_pos.x + std::max(0, (viewport.pixel_size.x - pixel_w_) / 2);
    pixel_y_ = viewport.pixel_pos.y + std::max(0, (viewport.pixel_size.y - pixel_h_) / 2);
    if (handle_)
        handle_->set_viewport(palette_pane_descriptor());
}

void CommandPaletteHost::pump()
{
    if (!handle_ || !renderer_ || !text_service_ || !palette_.is_open())
        return;

    refresh_open_palette();
}

void CommandPaletteHost::refresh_open_palette()
{
    if (!handle_ || !renderer_ || !text_service_ || !palette_.is_open())
        return;

    const auto [cw, ch] = renderer_->cell_size_pixels();
    const int grid_cols = cw > 0 ? pixel_w_ / cw : 0;
    const int grid_rows = ch > 0 ? pixel_h_ / ch : 0;
    handle_->set_grid_size(std::max(1, grid_cols), std::max(1, grid_rows));
    handle_->set_cursor(-1, -1, CursorStyle{});

    const float bg_alpha = deps_.palette_bg_alpha ? *deps_.palette_bg_alpha : 0.9f;
    auto vs = palette_.view_state(grid_cols, grid_rows, bg_alpha);
    auto cells = gui::render_palette(vs, *text_service_);
    handle_->update_cells(cells);

    flush_atlas_if_dirty();
}

void CommandPaletteHost::draw(IFrameContext& frame)
{
    if (handle_ && palette_.is_open())
        frame.draw_grid_handle(*handle_);
    frame.flush_submit_chunk();
}

PaneDescriptor CommandPaletteHost::palette_pane_descriptor() const
{
    PaneDescriptor desc;
    desc.pixel_pos = { pixel_x_, pixel_y_ };
    desc.pixel_size = { pixel_w_, pixel_h_ };
    return desc;
}

std::optional<std::chrono::steady_clock::time_point> CommandPaletteHost::next_deadline() const
{
    return std::nullopt;
}

void CommandPaletteHost::on_key(const KeyEvent& event)
{
    palette_.on_key(event);
}

void CommandPaletteHost::on_text_input(const TextInputEvent& event)
{
    palette_.on_text_input(event);
}

bool CommandPaletteHost::dispatch_action(std::string_view action)
{
    if (action == "toggle")
    {
        if (palette_.is_open())
        {
            palette_.close(); // on_closed callback destroys the handle
        }
        else
        {
            handle_ = renderer_->create_grid_handle();
            handle_->set_viewport(palette_pane_descriptor());
            palette_.open();
            refresh_open_palette();
        }
        if (callbacks_)
            callbacks_->request_frame();
        return true;
    }
    return false;
}

void CommandPaletteHost::request_close()
{
    palette_.close(); // on_closed callback destroys the handle
    if (callbacks_)
        callbacks_->request_frame();
}

Color CommandPaletteHost::default_background() const
{
    return { 0.0f, 0.0f, 0.0f, 0.0f };
}

HostRuntimeState CommandPaletteHost::runtime_state() const
{
    return { .content_ready = true };
}

HostDebugState CommandPaletteHost::debug_state() const
{
    return { .name = "CommandPalette" };
}

bool CommandPaletteHost::is_active() const
{
    return palette_.is_open();
}

void CommandPaletteHost::flush_atlas_if_dirty()
{
    if (!text_service_->atlas_dirty())
        return;

    const auto dirty = text_service_->atlas_dirty_rect();
    if (dirty.size.x <= 0 || dirty.size.y <= 0)
        return;

    constexpr size_t kPixelSize = 4;
    const size_t row_bytes = static_cast<size_t>(dirty.size.x) * kPixelSize;
    std::vector<uint8_t> scratch(row_bytes * dirty.size.y);
    const uint8_t* atlas = text_service_->atlas_data();
    const int atlas_w = text_service_->atlas_width();
    for (int r = 0; r < dirty.size.y; ++r)
    {
        const uint8_t* src = atlas
            + (static_cast<size_t>(dirty.pos.y + r) * atlas_w + dirty.pos.x) * kPixelSize;
        std::memcpy(scratch.data() + static_cast<size_t>(r) * row_bytes, src, row_bytes);
    }
    renderer_->update_atlas_region(
        dirty.pos.x, dirty.pos.y, dirty.size.x, dirty.size.y, scratch.data());
    text_service_->clear_atlas_dirty();

    // Atlas uploads are recorded in begin_frame(), so glyphs resolved during this
    // pump need one follow-up frame unless the palette was primed before frame start.
    if (callbacks_ && palette_.is_open())
        callbacks_->request_frame();
}

} // namespace draxul
