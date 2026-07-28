#include <draxul/remote_terminal_host.h>

#include <draxul/log.h>
#include <draxul/remote_terminal_client.h>
#include <draxul/terminal_key_encoder.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
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
    };

    Kind kind = Kind::Input;
    std::string text;
    int cols = 0;
    int rows = 0;
};

struct RemotePublishedState
{
    TerminalSemanticSnapshot snapshot;
    std::string controller_client_id;
};

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
                .expected_server_epoch = options_.server_epoch,
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
                }
                if (!ok)
                {
                    const std::string error_code
                        = client_->last_error_code();
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
                publish_projection();
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
            .controller_client_id
                = client_->projection().controller_client_id(),
        };
        {
            std::lock_guard guard(mutex_);
            published_state_ = std::move(state);
        }
        owner_.callbacks().wake_window();
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
    std::string pending_error_;
};

RemoteTerminalHost::RemoteTerminalHost(RemoteTerminalHostOptions options)
    : impl_(std::make_unique<Impl>(*this, std::move(options)))
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

void RemoteTerminalHost::pump()
{
    if (auto state = impl_->take_published_state())
    {
        if (state->snapshot.cols != grid_cols()
            || state->snapshot.rows != grid_rows())
        {
            apply_grid_size(state->snapshot.cols, state->snapshot.rows);
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
        for (int row = 0; row < state->snapshot.rows; ++row)
        {
            for (int col = 0; col < state->snapshot.cols; ++col)
            {
                const auto& cell = state->snapshot.cells[
                    static_cast<size_t>(row) * state->snapshot.cols + col];
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

        const auto& metadata = state->snapshot.metadata;
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

void RemoteTerminalHost::on_key(const KeyEvent& event)
{
    if (!event.pressed
        || controller_client_id_ != impl_->options().client_id)
        return;
    VtState state;
    state.cursor_app_mode = metadata_.modes.cursor_application;
    const std::string sequence = encode_terminal_key(event, state);
    if (!sequence.empty())
    {
        impl_->enqueue({
            .kind = RemoteHostCommand::Kind::Input,
            .text = sequence,
        });
    }
}

void RemoteTerminalHost::on_text_input(const TextInputEvent& event)
{
    if (!event.text.empty()
        && controller_client_id_ == impl_->options().client_id)
    {
        impl_->enqueue({
            .kind = RemoteHostCommand::Kind::Input,
            .text = event.text,
        });
    }
}

bool RemoteTerminalHost::dispatch_action(std::string_view action)
{
    if (action != "take_terminal_control")
        return false;
    const bool queued = impl_->enqueue({
        .kind = RemoteHostCommand::Kind::TakeControl,
    });
    return queued;
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
        + std::to_string(grid_rows());
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
