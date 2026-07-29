#include <draxul/remote_terminal_host.h>

#include <draxul/log.h>
#include <draxul/remote_terminal_client.h>
#include <draxul/terminal_key_encoder.h>
#include <draxul/window.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace draxul
{

namespace
{

constexpr size_t kRemoteHostCommandLimit = 128;
constexpr size_t kRemoteCommandsPerPoll = 8;
constexpr size_t kRemoteInputBatchBytes = 64 * 1024;
constexpr auto kRemotePollInterval = std::chrono::milliseconds(25);
constexpr auto kRemoteTransientFailureGrace = std::chrono::seconds(5);

bool is_expected_command_error(std::string_view code)
{
    return code == "not_controller" || code == "invalid_resize";
}

bool is_removed_remote_terminal(std::string_view code)
{
    return code == "terminal_not_found";
}

bool is_transient_remote_error(std::string_view code)
{
    return code == "endpoint_unavailable"
        || code == "io_error"
        || code == "main_thread_timeout"
        || code == "server_stopping";
}

struct RemoteHostCommand
{
    enum class Kind
    {
        Input,
        Resize,
        TakeControl,
        Scroll,
        ScrollToLive,
    };

    Kind kind = Kind::Input;
    std::string text;
    int cols = 0;
    int rows = 0;
    int scroll_rows = 0;
};

struct RemotePublishedState
{
    TerminalSemanticSnapshot snapshot;
    std::optional<RemoteTerminalScrollbackPage> scrollback_page;
    uint64_t scroll_offset = 0;
    uint64_t scrollback_total = 0;
    std::string controller_client_id;
    std::optional<std::string> clipboard_write;
    std::chrono::microseconds attach_latency{ 0 };
};

TerminalSemanticSnapshot compose_scrollback_view(
    const TerminalSemanticSnapshot& live,
    const RemoteTerminalScrollbackPage& page)
{
    if (!page.snapshot || page.snapshot->cols != live.cols
        || live.cols <= 0 || live.rows <= 0)
    {
        return live;
    }

    TerminalSemanticSnapshot result = live;
    const int history_rows = std::min(page.snapshot->rows, live.rows);
    const int live_rows = live.rows - history_rows;
    for (int row = 0; row < history_rows; ++row)
    {
        const auto src = static_cast<size_t>(row) * live.cols;
        const auto dst = static_cast<size_t>(row) * live.cols;
        std::copy_n(page.snapshot->cells.begin() + src, live.cols,
            result.cells.begin() + dst);
    }
    for (int row = 0; row < live_rows; ++row)
    {
        const auto src = static_cast<size_t>(row) * live.cols;
        const auto dst
            = static_cast<size_t>(history_rows + row) * live.cols;
        std::copy_n(live.cells.begin() + src, live.cols,
            result.cells.begin() + dst);
    }
    result.metadata.cursor.visible = false;
    return result;
}

} // namespace

class RemoteTerminalHost::Impl
{
public:
    Impl(RemoteTerminalHost& owner, RemoteTerminalHostOptions options)
        : owner_(owner)
        , options_(std::move(options))
    {
    }

    ~Impl()
    {
        stop();
    }

    bool start(std::string& error)
    {
        client_ = std::make_unique<RemoteTerminalClient>(
            RemoteTerminalClientOptions{
                .runtime_directory = options_.runtime_directory,
                .client_id = options_.client_id,
                .session_id = options_.session_id,
                .expected_server_epoch = options_.server_epoch,
                .method_prefix = options_.method_prefix,
                .terminal_id = options_.terminal_id,
            });
        if (!client_->attach(error))
            return false;

        publish_projection();
        running_ = true;
        worker_ = std::jthread([this] { worker_main(); });
        return true;
    }

    void stop()
    {
        stopping_ = true;
        command_wake_.notify_all();
        if (worker_.joinable())
            worker_.join();
        running_ = false;
    }

    bool enqueue(RemoteHostCommand command)
    {
        std::lock_guard guard(mutex_);
        if (stopping_)
            return false;
        if (!commands_.empty()
            && command.kind == RemoteHostCommand::Kind::Input
            && commands_.back().kind == RemoteHostCommand::Kind::Input
            && command.text.size()
                <= kRemoteInputBatchBytes - commands_.back().text.size())
        {
            commands_.back().text.append(command.text);
            command_wake_.notify_one();
            return true;
        }
        if (!commands_.empty()
            && command.kind == RemoteHostCommand::Kind::Resize
            && commands_.back().kind == RemoteHostCommand::Kind::Resize)
        {
            commands_.back() = std::move(command);
            command_wake_.notify_one();
            return true;
        }
        if (!commands_.empty()
            && command.kind == RemoteHostCommand::Kind::Scroll
            && commands_.back().kind == RemoteHostCommand::Kind::Scroll)
        {
            const int64_t combined
                = static_cast<int64_t>(commands_.back().scroll_rows)
                + command.scroll_rows;
            commands_.back().scroll_rows = static_cast<int>(std::clamp(
                combined,
                static_cast<int64_t>(std::numeric_limits<int>::min()),
                static_cast<int64_t>(std::numeric_limits<int>::max())));
            command_wake_.notify_one();
            return true;
        }
        if (commands_.size() >= kRemoteHostCommandLimit)
            return false;
        commands_.push_back(std::move(command));
        command_wake_.notify_one();
        return true;
    }

    std::optional<RemotePublishedState> take_published_state()
    {
        std::lock_guard guard(mutex_);
        auto result = std::move(published_state_);
        published_state_.reset();
        return result;
    }

    std::string take_error()
    {
        std::lock_guard guard(mutex_);
        return std::exchange(pending_error_, {});
    }

    bool running() const
    {
        return running_;
    }

    const RemoteTerminalHostOptions& options() const
    {
        return options_;
    }

    std::string init_error_code() const
    {
        return client_ ? client_->last_error_code() : std::string{};
    }

    void set_terminal_id(std::string terminal_id)
    {
        options_.terminal_id = std::move(terminal_id);
    }

private:
    void worker_main()
    {
        bool fatal_error = false;
        std::optional<std::chrono::steady_clock::time_point>
            transient_failure_since;
        while (!stopping_ && !fatal_error)
        {
            std::deque<RemoteHostCommand> commands;
            {
                std::unique_lock lock(mutex_);
                command_wake_.wait_for(lock, kRemotePollInterval,
                    [this] { return stopping_ || !commands_.empty(); });
                for (size_t count = 0;
                     count < kRemoteCommandsPerPoll && !commands_.empty();
                     ++count)
                {
                    commands.push_back(std::move(commands_.front()));
                    commands_.pop_front();
                }
            }
            if (stopping_)
                break;

            for (const auto& command : commands)
            {
                std::string error;
                bool ok = false;
                switch (command.kind)
                {
                case RemoteHostCommand::Kind::Input:
                    ok = client_->send_input(command.text, error);
                    break;
                case RemoteHostCommand::Kind::Resize:
                    ok = client_->resize(command.cols, command.rows, error);
                    break;
                case RemoteHostCommand::Kind::TakeControl:
                    ok = client_->take_control(error);
                    break;
                case RemoteHostCommand::Kind::Scroll:
                    ok = scroll_by(command.scroll_rows, error);
                    break;
                case RemoteHostCommand::Kind::ScrollToLive:
                    scroll_offset_ = 0;
                    scrollback_page_.reset();
                    publish_projection();
                    ok = true;
                    break;
                }
                if (!ok)
                {
                    const std::string error_code
                        = client_->last_error_code();
                    if (is_removed_remote_terminal(error_code))
                    {
                        fatal_error = true;
                        break;
                    }
                    if (is_expected_command_error(error_code))
                    {
                        publish_error(std::move(error));
                        continue;
                    }
                    if (is_transient_remote_error(error_code))
                    {
                        note_transient_failure(transient_failure_since,
                            error_code, error);
                        if (std::chrono::steady_clock::now()
                                - *transient_failure_since
                            < kRemoteTransientFailureGrace)
                        {
                            break;
                        }
                    }
                    else
                    {
                        publish_error(error);
                    }
                    log_fatal_failure(error_code, error);
                    fatal_error = true;
                    break;
                }
                transient_failure_since.reset();
            }
            if (fatal_error)
                break;

            bool changed = false;
            std::string error;
            if (!client_->poll(changed, error))
            {
                const std::string error_code = client_->last_error_code();
                if (is_removed_remote_terminal(error_code))
                    break;
                if (is_transient_remote_error(error_code))
                {
                    note_transient_failure(transient_failure_since,
                        error_code, error);
                    if (std::chrono::steady_clock::now()
                            - *transient_failure_since
                        < kRemoteTransientFailureGrace)
                    {
                        continue;
                    }
                }
                else
                {
                    publish_error(error);
                }
                log_fatal_failure(error_code, error);
                fatal_error = true;
                break;
            }
            transient_failure_since.reset();
            if (changed)
            {
                if (scroll_offset_ > 0
                    && !refresh_scrollback_after_output(error))
                {
                    publish_error(error);
                    fatal_error = true;
                    break;
                }
                publish_projection();
            }
        }

        std::string disconnect_error;
        client_->disconnect(disconnect_error);
        running_ = false;
    }

    void note_transient_failure(
        std::optional<std::chrono::steady_clock::time_point>& failure_since,
        std::string_view error_code, const std::string& error)
    {
        if (failure_since)
            return;
        failure_since = std::chrono::steady_clock::now();
        DRAXUL_LOG_WARN(LogCategory::App,
            "Remote terminal transport interruption (%.*s); retrying for %lld ms: %s",
            static_cast<int>(error_code.size()), error_code.data(),
            static_cast<long long>(kRemoteTransientFailureGrace.count() * 1000),
            error.c_str());
        publish_error(error);
    }

    void log_fatal_failure(
        std::string_view error_code, const std::string& error)
    {
        DRAXUL_LOG_ERROR(LogCategory::App,
            "Remote terminal host stopped (%.*s): %s",
            static_cast<int>(error_code.size()), error_code.data(),
            error.c_str());
    }

    void publish_projection()
    {
        RemotePublishedState state{
            .snapshot = client_->projection().snapshot(),
            .scrollback_page = scrollback_page_,
            .scroll_offset = scroll_offset_,
            .scrollback_total = scrollback_total_,
            .controller_client_id
                = client_->projection().controller_client_id(),
            .clipboard_write = client_->take_clipboard_write(),
            .attach_latency = client_->last_attach_latency(),
        };
        {
            std::lock_guard guard(mutex_);
            published_state_ = std::move(state);
        }
        owner_.callbacks().wake_window();
    }

    bool read_scrollback(
        uint64_t offset, RemoteTerminalScrollbackPage& page,
        std::string& error)
    {
        const auto& snapshot = client_->projection().snapshot();
        const size_t rows = static_cast<size_t>(std::max(1, snapshot.rows));
        return client_->read_scrollback(offset,
            std::min(rows, kRemoteTerminalMaxScrollbackPageRows),
            page, error);
    }

    bool scroll_by(int rows, std::string& error)
    {
        if (rows == 0)
            return true;

        const int64_t requested = std::max<int64_t>(
            0, static_cast<int64_t>(scroll_offset_) + rows);
        if (requested == 0)
        {
            scroll_offset_ = 0;
            scrollback_page_.reset();
            publish_projection();
            return true;
        }

        RemoteTerminalScrollbackPage page;
        if (!read_scrollback(static_cast<uint64_t>(requested), page, error))
            return false;
        scrollback_total_ = page.total_rows;
        scroll_offset_ = std::min<uint64_t>(
            static_cast<uint64_t>(requested), page.total_rows);
        scrollback_page_ = std::move(page);
        publish_projection();
        return true;
    }

    bool refresh_scrollback_after_output(std::string& error)
    {
        RemoteTerminalScrollbackPage page;
        if (!read_scrollback(scroll_offset_, page, error))
            return false;

        const uint64_t appended_rows
            = page.total_rows > scrollback_total_
            ? page.total_rows - scrollback_total_
            : 0;
        const uint64_t anchored_offset = std::min<uint64_t>(
            page.total_rows, scroll_offset_ + appended_rows);
        if (anchored_offset != scroll_offset_)
        {
            if (!read_scrollback(anchored_offset, page, error))
                return false;
        }
        scrollback_total_ = page.total_rows;
        scroll_offset_ = anchored_offset;
        scrollback_page_ = std::move(page);
        return true;
    }

    void publish_error(std::string error)
    {
        {
            std::lock_guard guard(mutex_);
            pending_error_ = std::move(error);
        }
        owner_.callbacks().wake_window();
    }

    RemoteTerminalHost& owner_;
    RemoteTerminalHostOptions options_;
    std::unique_ptr<RemoteTerminalClient> client_;
    std::jthread worker_;
    std::atomic<bool> running_ = false;
    std::atomic<bool> stopping_ = false;
    mutable std::mutex mutex_;
    std::condition_variable command_wake_;
    std::deque<RemoteHostCommand> commands_;
    std::optional<RemotePublishedState> published_state_;
    std::optional<RemoteTerminalScrollbackPage> scrollback_page_;
    uint64_t scroll_offset_ = 0;
    uint64_t scrollback_total_ = 0;
    std::string pending_error_;
};

RemoteTerminalHost::RemoteTerminalHost(RemoteTerminalHostOptions options)
    : mouse_reporter_([this](std::string_view sequence) {
        if (controller_client_id_ == impl_->options().client_id)
            send_remote_input(std::string(sequence));
    })
    , selection_([this]() -> SelectionManager::Callbacks {
        SelectionManager::Callbacks cbs;
        cbs.set_overlay_cells
            = [this](std::vector<CellUpdate> cells) {
                  set_overlay_cells(cells);
              };
        cbs.get_cell = [this](int col, int row) -> const Cell& {
            return grid().get_cell(col, row);
        };
        cbs.grid_cols = [this] { return grid_cols(); };
        cbs.grid_rows = [this] { return grid_rows(); };
        cbs.request_frame = [this] { callbacks().request_frame(); };
        cbs.on_selection_truncated = [this](std::string_view message) {
            callbacks().push_toast(1, message);
        };
        return cbs;
    }())
    , impl_(std::make_unique<Impl>(*this, std::move(options)))
{
}

RemoteTerminalHost::~RemoteTerminalHost()
{
    shutdown();
}

void RemoteTerminalHost::shutdown()
{
    impl_->stop();
}

bool RemoteTerminalHost::is_running() const
{
    return impl_->running();
}

std::string RemoteTerminalHost::init_error() const
{
    return init_error_;
}

std::string RemoteTerminalHost::init_error_code() const
{
    return impl_->init_error_code();
}

void RemoteTerminalHost::pump()
{
    if (auto state = impl_->take_published_state())
    {
        const TerminalSemanticSnapshot display_snapshot
            = state->scroll_offset > 0 && state->scrollback_page
            ? compose_scrollback_view(
                  state->snapshot, *state->scrollback_page)
            : state->snapshot;
        const bool viewport_changed
            = state->scroll_offset != scroll_offset_
            || display_snapshot.cols != grid_cols()
            || display_snapshot.rows != grid_rows();
        if (viewport_changed)
        {
            selection_.clear();
            pending_selection_copy_click_.reset();
        }
        scroll_offset_ = state->scroll_offset;
        scrollback_total_ = state->scrollback_total;
        attach_latency_ = state->attach_latency;

        if (display_snapshot.cols != grid_cols()
            || display_snapshot.rows != grid_rows())
        {
            apply_grid_size(display_snapshot.cols, display_snapshot.rows);
        }
        highlights().clear();
        highlights().set_default_fg(
            launch_options().terminal_fg.value_or(
                Color(0.92f, 0.92f, 0.92f, 1.0f)));
        highlights().set_default_bg(
            launch_options().terminal_bg.value_or(
                Color(0.08f, 0.09f, 0.10f, 1.0f)));

        grid().clear();
        std::unordered_map<HlAttr, uint16_t, HlAttrHash> attr_ids;
        uint16_t next_attr_id = 1;
        for (int row = 0; row < display_snapshot.rows; ++row)
        {
            for (int col = 0; col < display_snapshot.cols; ++col)
            {
                const auto& cell = display_snapshot.cells[
                    static_cast<size_t>(row) * display_snapshot.cols + col];
                if (cell.double_width_continuation)
                    continue;

                uint16_t attr_id = 0;
                if (cell.attr != HlAttr{})
                {
                    const auto [it, inserted]
                        = attr_ids.try_emplace(cell.attr, next_attr_id);
                    if (inserted)
                    {
                        highlights().set(next_attr_id, cell.attr);
                        ++next_attr_id;
                    }
                    attr_id = it->second;
                }
                grid().set_cell(
                    col, row, cell.text, attr_id, cell.double_width);
                if (!cell.hyperlink.empty())
                {
                    const uint16_t link_id
                        = grid().link_id_for_uri(cell.hyperlink);
                    grid().set_cell_hyperlink_id(col, row, link_id);
                }
            }
        }

        const auto& metadata = display_snapshot.metadata;
        set_cursor_position(metadata.cursor.col, metadata.cursor.row,
            CursorBlinkUpdate::Preserve);
        CursorStyle style;
        style.shape = metadata.cursor.shape;
        style.bg = highlights().default_fg();
        style.fg = highlights().default_bg();
        BlinkTiming timing;
        if (metadata.cursor.blink)
            timing = { 530, 530, 530 };
        set_cursor_style(style, timing, !metadata.cursor.visible);
        if (!metadata.title.empty())
            callbacks().set_window_title(metadata.title);
        const bool became_controller
            = controller_client_id_ != impl_->options().client_id
            && state->controller_client_id == impl_->options().client_id;
        controller_client_id_ = std::move(state->controller_client_id);
        if (state->clipboard_write
            && controller_client_id_ == impl_->options().client_id)
        {
            window().set_clipboard_text(*state->clipboard_write);
        }
        apply_mouse_modes(state->snapshot.metadata.modes.mouse);
        metadata_ = metadata;
        flush_grid();
        if (became_controller
            && desired_cols_ > 0 && desired_rows_ > 0
            && (desired_cols_ != grid_cols() || desired_rows_ != grid_rows()))
        {
            impl_->enqueue({
                .kind = RemoteHostCommand::Kind::Resize,
                .cols = desired_cols_,
                .rows = desired_rows_,
            });
        }
    }

    if (std::string error = impl_->take_error(); !error.empty())
    {
        callbacks().push_toast(1, error);
        last_error_ = std::move(error);
    }
}

void RemoteTerminalHost::on_config_reloaded(
    const HostReloadConfig& config)
{
    GridHostBase::on_config_reloaded(config);
    launch_options().terminal_fg = config.terminal_fg;
    launch_options().terminal_bg = config.terminal_bg;
    launch_options().selection_max_cells = config.selection_max_cells;
    launch_options().copy_on_select = config.copy_on_select;
    launch_options().paste_confirm_lines = config.paste_confirm_lines;
    launch_options().url_detection = config.url_detection;
    launch_options().enable_osc8_hyperlinks
        = config.enable_osc8_hyperlinks;
    launch_options().enable_shell_integration_marks
        = config.enable_shell_integration_marks;
    if (config.selection_max_cells > 0)
        selection_.set_max_cells(config.selection_max_cells);
    highlights().set_default_fg(
        launch_options().terminal_fg.value_or(
            Color(0.92f, 0.92f, 0.92f, 1.0f)));
    highlights().set_default_bg(
        launch_options().terminal_bg.value_or(
            Color(0.08f, 0.09f, 0.10f, 1.0f)));
    force_full_redraw();
    flush_grid();
}

void RemoteTerminalHost::on_focus_gained()
{
    GridHostBase::on_focus_gained();
    if (controller_client_id_ == impl_->options().client_id
        && metadata_.modes.focus_reporting)
    {
        send_remote_input("\x1B[I");
    }
}

void RemoteTerminalHost::on_focus_lost()
{
    GridHostBase::on_focus_lost();
    pending_selection_copy_click_.reset();
    if (controller_client_id_ == impl_->options().client_id
        && metadata_.modes.focus_reporting)
    {
        send_remote_input("\x1B[O");
    }
}

void RemoteTerminalHost::on_key(const KeyEvent& event)
{
    if (!event.pressed
        || controller_client_id_ != impl_->options().client_id)
        return;
    if (scroll_offset_ > 0)
    {
        selection_.clear();
        impl_->enqueue({
            .kind = RemoteHostCommand::Kind::ScrollToLive,
        });
    }
    VtState state;
    state.cursor_app_mode = metadata_.modes.cursor_application;
    const std::string sequence = encode_terminal_key(event, state);
    if (!sequence.empty())
        send_remote_input(sequence);
}

void RemoteTerminalHost::on_text_input(const TextInputEvent& event)
{
    if (!event.text.empty()
        && controller_client_id_ == impl_->options().client_id)
    {
        if (scroll_offset_ > 0)
        {
            selection_.clear();
            impl_->enqueue({
                .kind = RemoteHostCommand::Kind::ScrollToLive,
            });
        }
        send_remote_input(event.text);
    }
}

void RemoteTerminalHost::on_mouse_button(const MouseButtonEvent& event)
{
    const GridPos pos = pixel_to_cell(event.pos.x, event.pos.y);
    if (event.button == 1 && event.pressed
        && open_link_at(pos, event.mod))
    {
        return;
    }

    if (controller_client_id_ == impl_->options().client_id
        && mouse_reporter_.on_button(event.button, event.pressed,
            event.mod, pos.col, pos.row))
    {
        return;
    }

    if (event.button == 1 && event.pressed)
    {
        pending_selection_copy_click_.reset();
        if (event.clicks == 2)
        {
            const bool active
                = selection_.select_word({ { pos.col, pos.row } });
            if (active && launch_options().copy_on_select)
                copy_active_selection_to_clipboard();
            return;
        }
        if (event.clicks >= 3)
        {
            const bool active
                = selection_.select_line({ { pos.col, pos.row } });
            if (active && launch_options().copy_on_select)
                copy_active_selection_to_clipboard();
            return;
        }
        if (selection_.is_active()
            && selection_.contains({ { pos.col, pos.row } }))
        {
            pending_selection_copy_click_ = pos;
            return;
        }
        selection_.begin_drag({ { pos.col, pos.row } });
        return;
    }

    if (event.button == 1)
    {
        if (pending_selection_copy_click_)
        {
            pending_selection_copy_click_.reset();
            copy_active_selection_to_clipboard();
            selection_.clear();
            return;
        }
        const bool active
            = selection_.end_drag({ { pos.col, pos.row } });
        if (active && launch_options().copy_on_select)
            copy_active_selection_to_clipboard();
    }
}

void RemoteTerminalHost::on_mouse_move(const MouseMoveEvent& event)
{
    const GridPos pos = pixel_to_cell(event.pos.x, event.pos.y);
    if (controller_client_id_ == impl_->options().client_id
        && mouse_reporter_.on_move(event.mod, pos.col, pos.row))
    {
        return;
    }

    if (pending_selection_copy_click_)
    {
        const GridPos anchor = *pending_selection_copy_click_;
        if (anchor.col == pos.col && anchor.row == pos.row)
            return;
        pending_selection_copy_click_.reset();
        selection_.begin_drag({ { anchor.col, anchor.row } });
    }
    selection_.update_drag({ { pos.col, pos.row } });
}

void RemoteTerminalHost::on_mouse_wheel(const MouseWheelEvent& event)
{
    if (controller_client_id_ == impl_->options().client_id
        && mouse_reporter_.mode() != MouseReporter::MouseMode::None)
    {
        const GridPos pos = pixel_to_cell(event.pos.x, event.pos.y);
        mouse_reporter_.on_wheel(
            event.delta.y > 0 ? 64 : 65, event.mod, pos.col, pos.row);
        return;
    }

    const int lines = std::max(
        1, static_cast<int>(std::abs(event.delta.y) * 3.0f + 0.5f));
    selection_.clear();
    pending_selection_copy_click_.reset();
    impl_->enqueue({
        .kind = RemoteHostCommand::Kind::Scroll,
        .scroll_rows = event.delta.y > 0 ? lines : -lines,
    });
}

std::optional<MouseCursor>
RemoteTerminalHost::mouse_cursor_at(int px, int py) const
{
    const GridPos pos = pixel_to_cell(px, py);
    return grid().effective_link_id(pos.col, pos.row) != 0
        ? std::optional<MouseCursor>(MouseCursor::Pointer)
        : std::nullopt;
}

void RemoteTerminalHost::set_scroll_offset(float)
{
    GridHostBase::set_scroll_offset(0.0f);
}

bool RemoteTerminalHost::dispatch_action(std::string_view action)
{
    if (action == "take_terminal_control")
    {
        return impl_->enqueue({
            .kind = RemoteHostCommand::Kind::TakeControl,
        });
    }
    if (action == "copy")
    {
        copy_active_selection_to_clipboard();
        return true;
    }
    if (action == "paste")
    {
        const std::string clipboard = window().clipboard_text();
        const int threshold = launch_options().paste_confirm_lines;
        if (threshold > 0 && !clipboard.empty())
        {
            const int lines = 1 + static_cast<int>(
                std::count(clipboard.begin(), clipboard.end(), '\n'));
            if (lines >= threshold)
            {
                if (!pending_paste_.empty())
                {
                    callbacks().push_toast(
                        1, "Previous pending paste was replaced by a new paste.");
                }
                pending_paste_ = clipboard;
                callbacks().push_toast(1,
                    "Large paste pending. Run confirm_paste to proceed "
                    "or cancel_paste to discard.");
                return true;
            }
        }
        send_paste(clipboard);
        return true;
    }
    if (action == "confirm_paste")
    {
        if (!pending_paste_.empty())
        {
            send_paste(pending_paste_);
            pending_paste_.clear();
        }
        return true;
    }
    if (action == "cancel_paste")
    {
        if (!pending_paste_.empty())
        {
            pending_paste_.clear();
            callbacks().push_toast(0, "Paste cancelled.");
        }
        return true;
    }
    return false;
}

RemoteTerminalHost::GridPos
RemoteTerminalHost::pixel_to_cell(int px, int py) const
{
    auto [cell_width, cell_height] = renderer().cell_size_pixels();
    const int padding = renderer().padding();
    cell_width = std::max(1, cell_width);
    cell_height = std::max(1, cell_height);
    return {
        std::clamp((px - viewport().pixel_pos.x - padding) / cell_width,
            0, std::max(0, grid_cols() - 1)),
        std::clamp((py - viewport().pixel_pos.y - padding) / cell_height,
            0, std::max(0, grid_rows() - 1)),
    };
}

bool RemoteTerminalHost::open_link_at(
    const GridPos& pos, ModifierFlags mod)
{
    const uint16_t link_id
        = grid().effective_link_id(pos.col, pos.row);
    if (link_id == 0)
        return false;
    const bool explicit_link
        = grid().cell_has_explicit_hyperlink(pos.col, pos.row);
    const bool url_modifier = (mod & kModCtrl) || (mod & kModSuper);
    if (!explicit_link && !url_modifier)
        return false;
    const std::string_view uri = grid().link_uri(link_id);
    if (uri.empty())
        return false;
    if (!window().open_url(uri))
        callbacks().push_toast(2, "Failed to open link.");
    return true;
}

bool RemoteTerminalHost::copy_active_selection_to_clipboard()
{
    if (!selection_.is_active())
        return false;
    const std::string text = selection_.extract_text();
    return !text.empty() && window().set_clipboard_text(text);
}

void RemoteTerminalHost::send_remote_input(std::string text)
{
    if (text.empty()
        || controller_client_id_ != impl_->options().client_id)
    {
        return;
    }
    impl_->enqueue({
        .kind = RemoteHostCommand::Kind::Input,
        .text = std::move(text),
    });
}

void RemoteTerminalHost::send_paste(std::string_view text)
{
    if (controller_client_id_ != impl_->options().client_id)
        return;
    if (scroll_offset_ > 0)
    {
        selection_.clear();
        impl_->enqueue({
            .kind = RemoteHostCommand::Kind::ScrollToLive,
        });
    }
    std::string input;
    if (metadata_.modes.bracketed_paste)
    {
        input.reserve(text.size() + 12);
        input += "\x1B[200~";
        input += text;
        input += "\x1B[201~";
    }
    else
    {
        input = text;
    }
    send_remote_input(std::move(input));
}

void RemoteTerminalHost::apply_mouse_modes(
    const TerminalMouseModeSnapshot& modes)
{
    if (modes == mouse_modes_)
        return;
    mouse_reporter_.reset();
    if (modes.normal_tracking)
        mouse_reporter_.set_mode(1000, true);
    if (modes.button_motion)
        mouse_reporter_.set_mode(1002, true);
    if (modes.any_motion)
        mouse_reporter_.set_mode(1003, true);
    if (modes.sgr_coordinates)
        mouse_reporter_.set_mode(1006, true);
    mouse_modes_ = modes;
}

void RemoteTerminalHost::request_close()
{
    shutdown();
}

std::string RemoteTerminalHost::status_text() const
{
    const bool controller
        = controller_client_id_ == impl_->options().client_id;
    return std::string("remote ")
        + (controller ? "controller" : "observer")
        + "  " + std::to_string(grid_cols()) + "x"
        + std::to_string(grid_rows())
        + " connect "
        + std::to_string(attach_latency_.count()) + "us"
        + (scroll_offset_ > 0
                ? " [" + std::to_string(scroll_offset_) + "/"
                    + std::to_string(scrollback_total_) + "]"
                : "");
}

std::string RemoteTerminalHost::current_working_directory() const
{
    return metadata_.working_directory;
}

std::optional<std::chrono::steady_clock::time_point>
RemoteTerminalHost::next_deadline() const
{
    const auto remote_deadline
        = std::chrono::steady_clock::now() + kRemotePollInterval;
    const auto base_deadline = GridHostBase::next_deadline();
    if (!base_deadline || remote_deadline < *base_deadline)
        return remote_deadline;
    return base_deadline;
}

bool RemoteTerminalHost::initialize_host()
{
    if (!launch_options().remote_terminal_id.empty())
        impl_->set_terminal_id(
            launch_options().remote_terminal_id);
    highlights().set_default_fg(
        launch_options().terminal_fg.value_or(
            Color(0.92f, 0.92f, 0.92f, 1.0f)));
    highlights().set_default_bg(
        launch_options().terminal_bg.value_or(
            Color(0.08f, 0.09f, 0.10f, 1.0f)));
    std::string error;
    if (!impl_->start(error))
    {
        init_error_ = std::move(error);
        return false;
    }
    pump();
    if (controller_client_id_ == impl_->options().client_id
        && desired_cols_ > 0 && desired_rows_ > 0
        && (desired_cols_ != grid_cols() || desired_rows_ != grid_rows()))
    {
        impl_->enqueue({
            .kind = RemoteHostCommand::Kind::Resize,
            .cols = desired_cols_,
            .rows = desired_rows_,
        });
    }
    return true;
}

void RemoteTerminalHost::on_viewport_changed()
{
    desired_cols_ = std::max(1, viewport().grid_size.x);
    desired_rows_ = std::max(1, viewport().grid_size.y);
    if (impl_->running()
        && controller_client_id_ == impl_->options().client_id)
    {
        impl_->enqueue({
            .kind = RemoteHostCommand::Kind::Resize,
            .cols = desired_cols_,
            .rows = desired_rows_,
        });
    }
}

void RemoteTerminalHost::on_font_metrics_changed_impl()
{
    force_full_redraw();
    flush_grid();
}

std::string_view RemoteTerminalHost::host_name() const
{
    return "remote-terminal";
}

} // namespace draxul
