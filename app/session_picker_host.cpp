#include "session_picker_host.h"

#include "session_listing.h"

#include <algorithm>
#include <cstring>
#include <draxul/gui/palette_renderer.h>
#include <draxul/log.h>
#include <draxul/session_attach.h>
#include <draxul/text_service.h>
#include <utility>
#include <vector>

namespace draxul
{

SessionPickerHost::SessionPickerHost(std::filesystem::path executable_path, LaunchFunction launch)
    : executable_path_(std::move(executable_path))
    , launch_(std::move(launch))
{
}

bool SessionPickerHost::initialize(const HostContext& context, IHostCallbacks& callbacks)
{
    callbacks_ = &callbacks;
    renderer_ = context.grid_renderer;
    text_service_ = context.text_service;
    if (!renderer_ || !text_service_)
        return false;

    handle_ = renderer_->create_grid_handle();
    if (!handle_)
        return false;

    picker_ = SessionPicker(SessionPicker::Deps{
        .list_sessions = [](std::string* error) { return list_known_sessions(error); },
        .activate_session = [this](std::string_view session_id, std::string* error) { return activate_or_restore_session(session_id, error); },
        .create_session = [this](std::string_view session_name, std::string* error) { return create_new_session(session_name, error); },
        .kill_session = [this](std::string_view session_id, std::string* error) { return kill_session(session_id, error); },
        .report_error = [this](std::string_view message) {
            if (callbacks_)
                callbacks_->push_toast(2, message); },
        .request_frame = [this]() {
            if (callbacks_)
                callbacks_->request_frame(); },
        .request_quit = [this]() {
            if (callbacks_)
                callbacks_->request_quit(); },
    });

    callbacks_->set_window_title("Draxul Session Picker");
    set_viewport(context.initial_viewport);
    picker_.refresh_sessions();
    refresh_grid();
    return true;
}

void SessionPickerHost::shutdown()
{
    running_ = false;
    handle_.reset();
}

bool SessionPickerHost::is_running() const
{
    return running_;
}

std::string SessionPickerHost::init_error() const
{
    return {};
}

void SessionPickerHost::set_viewport(const HostViewport& viewport)
{
    pixel_x_ = viewport.pixel_pos.x;
    pixel_y_ = viewport.pixel_pos.y;
    pixel_w_ = std::max(1, viewport.pixel_size.x);
    pixel_h_ = std::max(1, viewport.pixel_size.y);
    if (handle_)
    {
        PaneDescriptor desc;
        desc.pixel_pos = { pixel_x_, pixel_y_ };
        desc.pixel_size = { pixel_w_, pixel_h_ };
        handle_->set_viewport(desc);
    }
}

void SessionPickerHost::pump()
{
    if (!handle_ || !renderer_ || !text_service_)
        return;
    refresh_grid();
}

void SessionPickerHost::draw(IFrameContext& frame)
{
    if (handle_)
        frame.draw_grid_handle(*handle_);
    frame.flush_submit_chunk();
}

std::optional<std::chrono::steady_clock::time_point> SessionPickerHost::next_deadline() const
{
    return std::nullopt;
}

void SessionPickerHost::on_key(const KeyEvent& event)
{
    picker_.on_key(event);
}

void SessionPickerHost::on_text_input(const TextInputEvent& event)
{
    picker_.on_text_input(event);
}

bool SessionPickerHost::dispatch_action(std::string_view)
{
    return false;
}

void SessionPickerHost::request_close()
{
    running_ = false;
}

Color SessionPickerHost::default_background() const
{
    return { 0.0f, 0.0f, 0.0f, 1.0f };
}

HostRuntimeState SessionPickerHost::runtime_state() const
{
    return { .content_ready = true };
}

HostDebugState SessionPickerHost::debug_state() const
{
    HostDebugState state;
    state.name = "SessionPicker";
    return state;
}

bool SessionPickerHost::activate_or_restore_session(std::string_view session_id, std::string* error)
{
    std::string attach_error;
    const auto attach_status = SessionAttachServer::try_attach(session_id, &attach_error);
    if (attach_status == SessionAttachServer::AttachStatus::Attached)
        return true;
    if (attach_status == SessionAttachServer::AttachStatus::Error)
    {
        if (error)
            *error = attach_error.empty() ? "Failed to attach to session." : attach_error;
        return false;
    }

    std::vector<std::string> args = {
        "--session",
        std::string(session_id),
    };
    return spawn_draxul(std::move(args), error);
}

bool SessionPickerHost::create_new_session(std::string_view session_name, std::string* error)
{
    std::vector<std::string> args = {
        "--new-session",
    };
    if (!session_name.empty())
    {
        args.emplace_back("--session-name");
        args.emplace_back(session_name);
    }
    return spawn_draxul(std::move(args), error);
}

bool SessionPickerHost::kill_session(std::string_view session_id, std::string* error)
{
    std::string command_error;
    const auto kill_status = SessionAttachServer::send_command(
        session_id, SessionAttachServer::Command::Shutdown, &command_error);
    if (kill_status == SessionAttachServer::AttachStatus::Attached)
    {
        (void)delete_session_state(session_id);
        (void)delete_session_runtime_metadata(session_id);
        return true;
    }

    std::string delete_error;
    const bool deleted_saved_state = delete_session_state(session_id, &delete_error);
    const bool deleted_metadata = delete_session_runtime_metadata(session_id);
    if (kill_status == SessionAttachServer::AttachStatus::NoServer
        && (deleted_saved_state || deleted_metadata))
        return true;

    if (error)
    {
        if (!delete_error.empty())
            *error = delete_error;
        else if (!command_error.empty())
            *error = command_error;
        else
            *error = "No running or saved session was found.";
    }
    return false;
}

bool SessionPickerHost::spawn_draxul(std::vector<std::string> args, std::string* error) const
{
    if (executable_path_.empty())
    {
        if (error)
            *error = "Failed to resolve the Draxul executable path.";
        return false;
    }

    const auto launch_result = launch_(make_self_launch_command(executable_path_, std::move(args)));
    if (launch_result.launched())
        return true;
    if (error)
        *error = launch_result.error_message();
    return false;
}

void SessionPickerHost::refresh_grid()
{
    if (!handle_ || !renderer_ || !text_service_)
        return;

    const auto [cw, ch] = renderer_->cell_size_pixels();
    const int grid_cols = cw > 0 ? std::max(1, pixel_w_ / cw) : 1;
    const int grid_rows = ch > 0 ? std::max(1, pixel_h_ / ch) : 1;
    handle_->set_grid_size(grid_cols, grid_rows);
    handle_->set_cursor(-1, -1, CursorStyle{});
    auto state = picker_.view_state(grid_cols, grid_rows, 0.92f);
    auto cells = gui::render_palette(state, *text_service_);
    handle_->update_cells(cells);
    flush_atlas_if_dirty();
}

void SessionPickerHost::flush_atlas_if_dirty()
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

    if (callbacks_)
        callbacks_->request_frame();
}

} // namespace draxul
