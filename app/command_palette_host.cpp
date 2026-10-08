#include "command_palette_host.h"

#include "gui_action_handler.h"
#include <algorithm>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_keycode.h>
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
    palette_deps.action_visible = deps_.action_visible;
    palette_deps.focused_host = deps_.focused_host;
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
    full_viewport_=viewport;
    if (context_menu_) return;
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
    const int padding = context_menu_ ? renderer_->padding() : 0;
    const int grid_cols = cw > 0 ? (pixel_w_ - 2 * padding) / cw : 0;
    const int grid_rows = ch > 0 ? (pixel_h_ - 2 * padding) / ch : 0;
    handle_->set_grid_size(std::max(1, grid_cols), std::max(1, grid_rows));
    handle_->set_cursor(-1, -1, CursorStyle{});

    const float bg_alpha = deps_.palette_bg_alpha ? *deps_.palette_bg_alpha : 0.9f;
    auto vs = palette_.view_state(grid_cols, grid_rows, bg_alpha);
    if (context_menu_) vs.mode=gui::PaletteMode::Menu;
    auto cells = gui::render_palette(vs, *text_service_);
    handle_->update_cells(cells);
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
    if (!context_menu_ || event.keycode==SDLK_ESCAPE || event.keycode==SDLK_RETURN
        || event.keycode==SDLK_UP || event.keycode==SDLK_DOWN) palette_.on_key(event);
}

void CommandPaletteHost::on_text_input(const TextInputEvent& event)
{
    if (!context_menu_) palette_.on_text_input(event);
}

void CommandPaletteHost::on_mouse_button(const MouseButtonEvent& event)
{
    if (!context_menu_ || !event.pressed || !renderer_) return;
    if (event.button!=SDL_BUTTON_LEFT || event.pos.x<pixel_x_ || event.pos.y<pixel_y_
        || event.pos.x>=pixel_x_+pixel_w_ || event.pos.y>=pixel_y_+pixel_h_)
    { request_close(); return; }
    const auto [cw,ch]=renderer_->cell_size_pixels();
    const int local_y = event.pos.y - pixel_y_ - renderer_->padding();
    if (ch > 0 && local_y >= ch)
        palette_.click_choice(local_y / ch - 1);
}

bool CommandPaletteHost::open_context_menu(CommandPalette::ChoiceRequest request,int px,int py)
{
    if (!renderer_) return false;
    const auto [cw,ch]=renderer_->cell_size_pixels();
    if (cw<=0 || ch<=0 || request.entries.empty()) return false;
    size_t columns=request.title.size()+2;
    for (const auto& entry:request.entries) columns=std::max(columns,entry.name.size()+entry.shortcut_hint.size()+5);
    context_menu_=true;
    const int padding = renderer_->padding();
    pixel_w_=std::min(full_viewport_.pixel_size.x,static_cast<int>(std::clamp(columns,size_t{24},size_t{72}))*cw+2*padding);
    pixel_h_=std::min(full_viewport_.pixel_size.y,static_cast<int>(request.entries.size()+1)*ch+2*padding);
    pixel_x_=std::clamp(px,full_viewport_.pixel_pos.x,full_viewport_.pixel_pos.x+full_viewport_.pixel_size.x-pixel_w_);
    pixel_y_=std::clamp(py,full_viewport_.pixel_pos.y,full_viewport_.pixel_pos.y+full_viewport_.pixel_size.y-pixel_h_);
    if (!ensure_palette_handle()) return false;
    handle_->set_viewport(palette_pane_descriptor());
    palette_.open_choices(std::move(request));
    refresh_open_palette();
    if (callbacks_) callbacks_->request_frame();
    return true;
}

bool CommandPaletteHost::ensure_palette_handle()
{
    if (handle_)
        return true;
    if (!renderer_)
        return false;

    handle_ = renderer_->create_grid_handle();
    if (!handle_)
    {
        DRAXUL_LOG_ERROR(LogCategory::App,
            "CommandPaletteHost: create_grid_handle() returned null, cannot open palette");
        return false;
    }
    handle_->set_viewport(palette_pane_descriptor());
    return true;
}

bool CommandPaletteHost::open_prompt(CommandPalette::PromptRequest request)
{
    context_menu_=false; set_viewport(full_viewport_);
    if (!ensure_palette_handle())
        return false;

    palette_.open_prompt(std::move(request));
    refresh_open_palette();
    if (callbacks_)
        callbacks_->request_frame();
    return true;
}

bool CommandPaletteHost::open_choices(CommandPalette::ChoiceRequest request)
{
    context_menu_=false; set_viewport(full_viewport_);
    if (!ensure_palette_handle())
        return false;

    palette_.open_choices(std::move(request));
    refresh_open_palette();
    if (callbacks_)
        callbacks_->request_frame();
    return true;
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
            if (!ensure_palette_handle())
                return true;
            context_menu_=false; set_viewport(full_viewport_);
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

} // namespace draxul
