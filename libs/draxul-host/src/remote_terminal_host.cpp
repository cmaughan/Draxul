#include <draxul/remote_terminal_host.h>

#include <draxul/client_recovery.h>
#include <draxul/input_types.h>
#include <draxul/log.h>
#include <draxul/remote_terminal_client.h>
#include <draxul/terminal_key_encoder.h>
#include <draxul/window.h>

#include <SDL3/SDL_keycode.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace draxul
{

namespace
{

constexpr size_t kRemoteHostCommandLimit = 128;
constexpr size_t kRemoteCommandsPerPoll = 8;
constexpr size_t kRemoteInputBatchBytes = 48 * 1024;
constexpr auto kRemotePollInterval = std::chrono::milliseconds(25);
constexpr auto kRemoteShutdownJoinBudget = std::chrono::milliseconds(250);
std::atomic<uint64_t> g_next_remote_request_id{ 1 };

uint64_t next_remote_request_id()
{
    uint64_t result
        = g_next_remote_request_id.fetch_add(1);
    if (result == 0)
        result = g_next_remote_request_id.fetch_add(1);
    return result;
}

bool is_expected_command_error(std::string_view code)
{
    return code == "not_controller"
        || code == "invalid_resize"
        || code == "invalid_input"
        || code == "backpressure"
        || code == "process_write_failed";
}

bool is_removed_remote_terminal(std::string_view code)
{
    return code == "terminal_not_found";
}

bool is_transient_remote_error(std::string_view code)
{
    return is_transient_client_error(code);
}

bool needs_identity_refresh(std::string_view code)
{
    return code == "stale_epoch"
        || code == "invalid_connection_token";
}

bool is_selection_copy_shortcut(const KeyEvent& event)
{
    return event.pressed && event.keycode == SDLK_C
        && has_only_modifiers(event.mod, kModCtrl);
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
    uint64_t request_id = 0;
    bool attempted = false;
    bool mergeable = true;
};

struct RemotePublishedState
{
    TerminalSemanticSnapshot snapshot;
    std::optional<TerminalDirtySnapshot> grid_update;
    std::optional<RemoteTerminalScrollbackPage> scrollback_page;
    uint64_t scroll_offset = 0;
    uint64_t scrollback_total = 0;
    std::string controller_client_id;
    std::string display_name;
    bool process_running = true;
    std::optional<int> exit_code;
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

TerminalDirtySnapshot full_grid_update(
    const TerminalSemanticSnapshot& snapshot)
{
    TerminalDirtySnapshot update{
        .cols = snapshot.cols,
        .rows = snapshot.rows,
        .full = true,
        .metadata = snapshot.metadata,
    };
    update.cells.reserve(snapshot.cells.size());
    for (int row = 0; row < snapshot.rows; ++row)
    {
        for (int col = 0; col < snapshot.cols; ++col)
        {
            update.cells.push_back({
                .col = col,
                .row = row,
                .cell = snapshot.cells[static_cast<size_t>(row) * snapshot.cols + col],
            });
        }
    }
    return update;
}

} // namespace

class RemoteTerminalHost::Impl
    : public std::enable_shared_from_this<RemoteTerminalHost::Impl>
{
public:
    Impl(RemoteTerminalHost& owner, RemoteTerminalHostOptions options)
        : owner_(owner)
        , options_(std::move(options))
        , suspend_available_(options_.presentation_suspend_supported)
    {
    }

    ~Impl()
    {
        request_stop();
        join_worker();
    }

    bool start(std::string&)
    {
        if (!options_.recovery)
        {
            options_.recovery = std::make_shared<ClientRecoveryState>(
                options_.client_id);
            options_.recovery->set_server_epoch(options_.server_epoch);
        }
        client_ = std::make_unique<RemoteTerminalClient>(
            RemoteTerminalClientOptions{
                .runtime_directory = options_.runtime_directory,
                .client_id = options_.client_id,
                .session_id = options_.session_id,
                .expected_server_epoch = options_.server_epoch,
                .method_prefix = options_.method_prefix,
                .terminal_id = options_.terminal_id,
                .recovery = options_.recovery,
            });
        running_ = true;
        {
            std::lock_guard lock(worker_exit_mutex_);
            worker_exited_ = false;
        }
        worker_ = std::jthread([this] { worker_main(); });
        return true;
    }

    void stop()
    {
        request_stop();
        if (reaper_started_)
        {
            running_ = false;
            return;
        }
        bool worker_exited = false;
        {
            std::unique_lock lock(worker_exit_mutex_);
            if (!worker_exited_)
            {
                worker_exited_changed_.wait_for(
                    lock, kRemoteShutdownJoinBudget,
                    [this] { return worker_exited_; });
            }
            worker_exited = worker_exited_;
        }
        if (worker_exited)
        {
            join_worker();
            running_ = false;
            return;
        }

        bool expected = false;
        if (reaper_started_.compare_exchange_strong(expected, true))
        {
            std::thread([self = shared_from_this()] {
                self->join_worker();
            }).detach();
        }
        running_ = false;
    }

    void request_stop()
    {
        stopping_ = true;
        command_wake_.notify_all();
        // Wait for any callback already in progress. Once this lock has been
        // acquired after setting stopping_, the worker can no longer touch
        // the owning host even if its transport call needs to be reaped in
        // the background.
        std::lock_guard owner_guard(owner_callback_mutex_);
    }

    bool enqueue(RemoteHostCommand command)
    {
        std::lock_guard guard(mutex_);
        if (stopping_)
            return false;
        if (command.request_id == 0)
            command.request_id = next_remote_request_id();
        if (!commands_.empty()
            && command.kind == RemoteHostCommand::Kind::Input
            && commands_.back().kind == RemoteHostCommand::Kind::Input
            && !commands_.back().attempted
            && commands_.back().mergeable
            && command.mergeable
            && commands_.back().text.size() <= kRemoteInputBatchBytes
            && command.text.size() <= kRemoteInputBatchBytes
            && commands_.back().text.size() + command.text.size()
                <= kRemoteInputBatchBytes)
        {
            commands_.back().text.append(command.text);
            command_wake_.notify_one();
            return true;
        }
        if (!commands_.empty()
            && command.kind == RemoteHostCommand::Kind::Resize
            && commands_.back().kind == RemoteHostCommand::Kind::Resize
            && !commands_.back().attempted)
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

    bool enqueue_input(std::string_view text)
    {
        if (text.empty())
            return true;

        std::lock_guard guard(mutex_);
        if (stopping_)
            return false;

        const size_t maximum_new_commands
            = (text.size() + kRemoteInputBatchBytes - 1)
            / kRemoteInputBatchBytes;
        if (maximum_new_commands
            > kRemoteHostCommandLimit - std::min(commands_.size(), kRemoteHostCommandLimit))
        {
            return false;
        }

        // Stage the complete input so a full queue cannot leave a bracketed
        // paste half-enqueued (and therefore leave the shell in paste mode).
        auto staged = commands_;
        size_t offset = 0;
        while (offset < text.size())
        {
            const size_t count = std::min(
                kRemoteInputBatchBytes, text.size() - offset);
            const std::string_view chunk = text.substr(offset, count);
            if (!staged.empty()
                && staged.back().kind
                    == RemoteHostCommand::Kind::Input
                && !staged.back().attempted
                && staged.back().mergeable
                && staged.back().text.size()
                    <= kRemoteInputBatchBytes
                && chunk.size() <= kRemoteInputBatchBytes
                && staged.back().text.size() + chunk.size()
                    <= kRemoteInputBatchBytes)
            {
                staged.back().text.append(chunk);
            }
            else
            {
                if (staged.size() >= kRemoteHostCommandLimit)
                    return false;
                staged.push_back({
                    .kind = RemoteHostCommand::Kind::Input,
                    .text = std::string(chunk),
                    .request_id = next_remote_request_id(),
                });
            }
            offset += count;
        }
        commands_.swap(staged);
        command_wake_.notify_one();
        return true;
    }

    bool enqueue_input_chunks(std::vector<std::string> chunks)
    {
        if (chunks.empty())
            return true;

        std::lock_guard guard(mutex_);
        if (stopping_
            || chunks.size()
                > kRemoteHostCommandLimit
                    - std::min(commands_.size(),
                        kRemoteHostCommandLimit))
        {
            return false;
        }

        // Keep each caller-provided frame as one server request. Bracketed
        // paste uses this to ensure a rejected request cannot deliver an
        // opening marker without its matching closing marker.
        auto staged = commands_;
        for (auto& chunk : chunks)
        {
            if (chunk.empty() || chunk.size() > kRemoteInputBatchBytes)
                return false;
            staged.push_back({
                .kind = RemoteHostCommand::Kind::Input,
                .text = std::move(chunk),
                .request_id = next_remote_request_id(),
                .mergeable = false,
            });
        }
        commands_.swap(staged);
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

    void set_presentation_visible(bool visible)
    {
        if (presentation_visible_.exchange(visible) == visible)
            return;
        if (!visible)
        {
            std::lock_guard guard(mutex_);
            published_state_.reset();
        }
        command_wake_.notify_one();
    }

    bool presentation_visible() const noexcept
    {
        return presentation_visible_;
    }

private:
    bool execute_command(
        const RemoteHostCommand& command, std::string& error)
    {
        switch (command.kind)
        {
        case RemoteHostCommand::Kind::Input:
            return client_->send_input(
                command.text, error, command.request_id);
        case RemoteHostCommand::Kind::Resize:
            return client_->resize(
                command.cols, command.rows, error, command.request_id);
        case RemoteHostCommand::Kind::TakeControl:
            return client_->take_control(error, command.request_id);
        case RemoteHostCommand::Kind::Scroll:
            return scroll_by(command.scroll_rows, error);
        case RemoteHostCommand::Kind::ScrollToLive:
            scroll_offset_ = 0;
            scrollback_page_.reset();
            publish_projection();
            return true;
        }
        return false;
    }

    bool recover_attachment(std::string& error)
    {
        if (!client_->attach(error))
            return false;
        options_.terminal_id
            = client_->projection().version().terminal_id;
        options_.recovery->note_connected(recovery_channel());
        scroll_offset_ = 0;
        scrollback_total_ = 0;
        scrollback_page_.reset();
        if (presentation_visible_)
            publish_projection();
        else
        {
            client_->take_grid_update();
            client_->take_clipboard_write();
        }
        DRAXUL_LOG_INFO(LogCategory::App,
            "Remote terminal client %s attached to the server terminal",
            options_.client_id.c_str());
        return true;
    }

    void wait_for_retry(std::chrono::milliseconds delay)
    {
        std::unique_lock lock(mutex_);
        command_wake_.wait_for(lock, delay,
            [this] { return stopping_.load(); });
    }

    std::chrono::milliseconds note_recoverable_failure(
        std::string_view error_code, const std::string& error)
    {
        const auto delay
            = options_.recovery->note_failure(recovery_channel());
        const auto recovery
            = options_.recovery->snapshot(recovery_channel());
        DRAXUL_LOG_WARN(LogCategory::App,
            "Remote terminal interruption (%.*s), recovery attempt %u in %lld ms: %s",
            static_cast<int>(error_code.size()), error_code.data(),
            recovery.attempts,
            static_cast<long long>(delay.count()), error.c_str());
        if (recovery.attempts == 1)
            publish_error_once(error_code, error);
        return delay;
    }

    void requeue_commands(
        std::deque<RemoteHostCommand>& batch, size_t from)
    {
        std::lock_guard guard(mutex_);
        for (size_t index = batch.size(); index > from; --index)
            commands_.push_front(std::move(batch[index - 1]));
        command_wake_.notify_one();
    }

    bool refresh_epoch(std::string& error)
    {
        return options_.recovery->refresh_server_epoch(
            options_.runtime_directory, options_.client_id, error);
    }

    std::string recovery_channel() const
    {
        return "terminal:"
            + (options_.terminal_id.empty()
                    ? options_.method_prefix
                    : options_.terminal_id);
    }

    void worker_main()
    {
        bool terminal_removed = false;
        bool attached = false;
        bool suspended = false;
        while (!stopping_ && !terminal_removed)
        {
            if (!attached)
            {
                std::string error;
                if (recover_attachment(error))
                {
                    attached = true;
                }
                else
                {
                    std::string error_code = client_->last_error_code();
                    if (is_removed_remote_terminal(error_code))
                    {
                        publish_error(std::move(error));
                        terminal_removed = true;
                        break;
                    }
                    if (needs_identity_refresh(error_code))
                    {
                        std::string refresh_error;
                        if (refresh_epoch(refresh_error))
                            continue;
                        error = std::move(refresh_error);
                    }
                    const auto delay
                        = note_recoverable_failure(error_code, error);
                    wait_for_retry(delay);
                    continue;
                }
            }

            std::deque<RemoteHostCommand> commands;
            bool presentation_visible = true;
            {
                std::unique_lock lock(mutex_);
                if (suspended && !presentation_visible_
                    && commands_.empty())
                {
                    command_wake_.wait(lock, [this] {
                        return stopping_ || presentation_visible_
                            || !commands_.empty();
                    });
                }
                else
                {
                    command_wake_.wait_for(lock, kRemotePollInterval,
                        [this] {
                            return stopping_ || !commands_.empty()
                                || (suspend_available_
                                    && presentation_visible_
                                        == presentation_suspended_);
                        });
                }
                presentation_visible = presentation_visible_;
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

            if (suspended
                && (presentation_visible || !commands.empty()))
            {
                std::string error;
                if (client_->resume(error))
                {
                    suspended = false;
                    presentation_suspended_ = false;
                    options_.recovery->note_connected(
                        recovery_channel());
                    scroll_offset_ = 0;
                    scrollback_total_ = 0;
                    scrollback_page_.reset();
                    if (presentation_visible_)
                        publish_projection();
                    else
                    {
                        client_->take_grid_update();
                        client_->take_clipboard_write();
                    }
                }
                else
                {
                    std::string error_code
                        = client_->last_error_code();
                    if (error_code == "not_attached")
                    {
                        if (recover_attachment(error))
                        {
                            attached = true;
                            suspended = false;
                            presentation_suspended_ = false;
                            continue;
                        }
                        error_code = client_->last_error_code();
                    }
                    if (is_removed_remote_terminal(error_code))
                    {
                        terminal_removed = true;
                        break;
                    }
                    if (needs_identity_refresh(error_code))
                    {
                        std::string refresh_error;
                        if (!refresh_epoch(refresh_error))
                            error = std::move(refresh_error);
                        attached = false;
                        suspended = false;
                        presentation_suspended_ = false;
                        continue;
                    }
                    const auto delay
                        = note_recoverable_failure(error_code, error);
                    wait_for_retry(delay);
                    continue;
                }
            }

            if (!suspended && !presentation_visible
                && commands.empty() && suspend_available_)
            {
                std::string error;
                if (client_->suspend(
                        error, next_remote_request_id()))
                {
                    suspended = true;
                    presentation_suspended_ = true;
                    options_.recovery->note_connected(
                        recovery_channel());
                    client_->take_grid_update();
                    client_->take_clipboard_write();
                    continue;
                }
                const std::string error_code
                    = client_->last_error_code();
                if (error_code == "unknown_method")
                {
                    suspend_available_ = false;
                    DRAXUL_LOG_INFO(LogCategory::App,
                        "Remote terminal presentation suspension is unavailable; retaining foreground polling");
                }
                else if (error_code == "not_attached")
                {
                    attached = false;
                    continue;
                }
                else if (is_removed_remote_terminal(error_code))
                {
                    terminal_removed = true;
                    break;
                }
                else if (is_transient_remote_error(error_code)
                    || is_resynchronizing_client_error(error_code))
                {
                    const auto delay
                        = note_recoverable_failure(error_code, error);
                    wait_for_retry(delay);
                    continue;
                }
                else
                {
                    suspend_available_ = false;
                    publish_error_once(error_code, error);
                }
            }

            bool retry_batch = false;
            for (size_t command_index = 0;
                command_index < commands.size(); ++command_index)
            {
                auto& command = commands[command_index];
                if (stopping_)
                    break;
                std::string error;
                command.attempted = true;
                bool ok = execute_command(command, error);
                if (stopping_)
                    break;
                if (!ok)
                {
                    std::string error_code
                        = client_->last_error_code();
                    if (error_code == "not_attached")
                    {
                        std::string attach_error;
                        if (recover_attachment(attach_error))
                        {
                            attached = true;
                            error.clear();
                            ok = execute_command(command, error);
                            if (ok)
                            {
                                options_.recovery->note_connected(
                                    recovery_channel());
                                continue;
                            }
                            error_code = client_->last_error_code();
                        }
                        else
                        {
                            error = std::move(attach_error);
                            error_code = client_->last_error_code();
                        }
                    }
                    if (is_removed_remote_terminal(error_code))
                    {
                        terminal_removed = true;
                        break;
                    }
                    if (is_expected_command_error(error_code))
                    {
                        publish_error_once(error_code, error);
                        continue;
                    }
                    if (error_code == "unknown_method")
                    {
                        // Optional capabilities (notably scrollback on an
                        // older server) degrade gently rather than killing
                        // the pane.
                        publish_error_once(error_code, error);
                        continue;
                    }
                    if (needs_identity_refresh(error_code))
                    {
                        std::string refresh_error;
                        if (!refresh_epoch(refresh_error))
                            error = std::move(refresh_error);
                        attached = false;
                    }
                    if (is_transient_remote_error(error_code)
                        || is_resynchronizing_client_error(error_code))
                    {
                        requeue_commands(commands, command_index);
                        const auto delay
                            = note_recoverable_failure(error_code, error);
                        wait_for_retry(delay);
                        retry_batch = true;
                        break;
                    }
                    // A request-level failure must not orphan a live server
                    // terminal behind a stopped UI host. Drop this command,
                    // surface the error once, and keep polling so a later
                    // command can prove the pane is usable.
                    publish_error_once(error_code, error);
                    DRAXUL_LOG_WARN(LogCategory::App,
                        "Remote terminal command was dropped (%s): %s",
                        error_code.c_str(), error.c_str());
                    continue;
                }
                options_.recovery->note_connected(
                    recovery_channel());
            }
            if (stopping_ || terminal_removed)
                break;
            if (retry_batch)
                continue;

            if (!presentation_visible_ && suspend_available_)
                continue;

            bool changed = false;
            std::string error;
            if (!client_->poll(changed, error))
            {
                if (stopping_)
                    break;
                std::string error_code = client_->last_error_code();
                if (needs_identity_refresh(error_code))
                {
                    std::string refresh_error;
                    if (refresh_epoch(refresh_error))
                    {
                        attached = false;
                        continue;
                    }
                    error = std::move(refresh_error);
                    attached = false;
                }
                if (error_code == "not_attached"
                    || is_resynchronizing_client_error(error_code))
                {
                    attached = false;
                    std::string attach_error;
                    if (recover_attachment(attach_error))
                    {
                        attached = true;
                        options_.recovery->note_connected(
                            recovery_channel());
                        continue;
                    }
                    error = std::move(attach_error);
                    error_code = client_->last_error_code();
                }
                if (is_removed_remote_terminal(error_code))
                    break;
                if (error_code == "unknown_method")
                {
                    publish_error_once(error_code, error);
                    wait_for_retry(
                        options_.recovery->note_failure(
                            recovery_channel()));
                    continue;
                }
                if (is_transient_remote_error(error_code)
                    || is_resynchronizing_client_error(error_code))
                {
                    const auto delay
                        = note_recoverable_failure(error_code, error);
                    wait_for_retry(delay);
                    continue;
                }
                // Unknown poll failures are treated as a bounded
                // resynchronization attempt. The next loop reattaches and
                // obtains a full snapshot instead of permanently stopping
                // the pane.
                publish_error_once(error_code, error);
                attached = false;
                const auto delay
                    = note_recoverable_failure(error_code, error);
                wait_for_retry(delay);
                continue;
            }
            if (stopping_)
                break;
            options_.recovery->note_connected(
                recovery_channel());
            if (changed)
            {
                if (scroll_offset_ > 0
                    && !refresh_scrollback_after_output(error))
                {
                    const std::string error_code
                        = client_->last_error_code();
                    if (error_code == "unknown_method")
                    {
                        scroll_offset_ = 0;
                        scrollback_page_.reset();
                        publish_error(std::move(error));
                    }
                    else if (is_transient_remote_error(error_code)
                        || is_resynchronizing_client_error(error_code))
                    {
                        if (is_resynchronizing_client_error(error_code))
                        {
                            std::string attach_error;
                            if (recover_attachment(attach_error))
                                continue;
                            error = std::move(attach_error);
                        }
                        const auto delay
                            = note_recoverable_failure(
                                error_code, error);
                        wait_for_retry(delay);
                        continue;
                    }
                    else
                    {
                        // Scrollback is optional client-local presentation.
                        // If it fails unexpectedly, return to the live view
                        // and keep the terminal worker healthy.
                        publish_error_once(error_code, error);
                        scroll_offset_ = 0;
                        scrollback_total_ = 0;
                        scrollback_page_.reset();
                    }
                }
                if (presentation_visible_)
                    publish_projection();
                else
                {
                    client_->take_grid_update();
                    client_->take_clipboard_write();
                }
            }
        }

        // The server expires inactive clients. A synchronous goodbye during
        // shutdown merely adds another full transport timeout to window
        // teardown, so only send it after an unexpected worker exit.
        if (!stopping_ && attached)
        {
            std::string disconnect_error;
            client_->disconnect(disconnect_error);
        }
        running_ = false;
        {
            std::lock_guard lock(worker_exit_mutex_);
            worker_exited_ = true;
        }
        worker_exited_changed_.notify_all();
    }

    void join_worker()
    {
        std::lock_guard lock(worker_join_mutex_);
        if (worker_.joinable())
            worker_.join();
    }

    void publish_projection()
    {
        const auto& live = client_->projection().snapshot();
        if (scroll_offset_ > 0 && scrollback_page_
            && scrollback_page_->snapshot
            && scrollback_page_->snapshot->cols != live.cols)
        {
            scroll_offset_ = 0;
            scrollback_total_ = 0;
            scrollback_page_.reset();
        }
        RemotePublishedState state{
            .snapshot = live,
            .grid_update = client_->take_grid_update(),
            .scrollback_page = scrollback_page_,
            .scroll_offset = scroll_offset_,
            .scrollback_total = scrollback_total_,
            .controller_client_id
            = client_->projection().controller_client_id(),
            .display_name = client_->projection().pane().name,
            .process_running
            = client_->projection().pane().process_running,
            .exit_code = client_->projection().pane().exit_code,
            .clipboard_write = client_->take_clipboard_write(),
            .attach_latency = client_->last_attach_latency(),
        };
        std::lock_guard guard(mutex_);
        if (!presentation_visible_)
            return;
        if (published_state_)
        {
            // The protocol projection has already advanced past the
            // pending state's sequence. Preserve every unconsumed grid
            // mutation by publishing the latest projection as a full
            // update; otherwise the next poll can ACK cells the UI never
            // observed.
            if (published_state_->grid_update)
                state.grid_update = full_grid_update(state.snapshot);
            if (!state.clipboard_write
                && published_state_->clipboard_write)
            {
                state.clipboard_write
                    = std::move(published_state_->clipboard_write);
            }
        }
        published_state_ = std::move(state);
        std::lock_guard owner_guard(owner_callback_mutex_);
        if (stopping_)
            return;
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
        std::lock_guard owner_guard(owner_callback_mutex_);
        if (stopping_)
            return;
        owner_.callbacks().wake_window();
    }

    void publish_error_once(
        std::string_view error_code, const std::string& error)
    {
        const std::string key = error_code.empty()
            ? error
            : std::string(error_code);
        if (!published_error_keys_.insert(key).second)
            return;
        publish_error(error);
    }

    RemoteTerminalHost& owner_;
    RemoteTerminalHostOptions options_;
    std::unique_ptr<RemoteTerminalClient> client_;
    std::jthread worker_;
    std::atomic<bool> running_ = false;
    std::atomic<bool> stopping_ = false;
    std::atomic<bool> reaper_started_ = false;
    std::atomic<bool> presentation_visible_ = true;
    std::atomic<bool> presentation_suspended_ = false;
    std::atomic<bool> suspend_available_ = false;
    mutable std::mutex mutex_;
    std::mutex owner_callback_mutex_;
    std::mutex worker_exit_mutex_;
    std::condition_variable worker_exited_changed_;
    bool worker_exited_ = true;
    std::mutex worker_join_mutex_;
    std::condition_variable command_wake_;
    std::deque<RemoteHostCommand> commands_;
    std::optional<RemotePublishedState> published_state_;
    std::optional<RemoteTerminalScrollbackPage> scrollback_page_;
    uint64_t scroll_offset_ = 0;
    uint64_t scrollback_total_ = 0;
    std::string pending_error_;
    std::unordered_set<std::string> published_error_keys_;
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
    , impl_(std::make_shared<Impl>(*this, std::move(options)))
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

std::optional<int> RemoteTerminalHost::exit_code() const
{
    return remote_exit_code_;
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

        const bool dimensions_changed
            = display_snapshot.cols != grid_cols()
            || display_snapshot.rows != grid_rows();
        if (dimensions_changed)
            apply_grid_size(display_snapshot.cols, display_snapshot.rows);

        bool rebuild_grid = dimensions_changed
            || viewport_changed
            || state->scroll_offset > 0
            || (state->grid_update && state->grid_update->full);
        if (!rebuild_grid && state->grid_update)
        {
            std::unordered_map<HlAttr, bool, HlAttrHash> missing;
            for (const auto& dirty : state->grid_update->cells)
            {
                if (dirty.cell.attr != HlAttr{}
                    && !remote_attr_ids_.contains(dirty.cell.attr))
                {
                    missing.emplace(dirty.cell.attr, true);
                }
            }
            rebuild_grid
                = next_remote_attr_id_ + missing.size()
                > std::numeric_limits<uint16_t>::max();
        }

        const auto reset_highlights = [this] {
            highlights().clear();
            highlights().set_default_fg(
                launch_options().terminal_fg.value_or(
                    Color(0.92f, 0.92f, 0.92f, 1.0f)));
            highlights().set_default_bg(
                launch_options().terminal_bg.value_or(
                    Color(0.08f, 0.09f, 0.10f, 1.0f)));
            remote_attr_ids_.clear();
            next_remote_attr_id_ = 1;
        };
        const auto apply_cell = [this](
                                    int col, int row,
                                    const TerminalCellSnapshot& cell) {
            if (cell.double_width_continuation)
                return;
            uint16_t attr_id = 0;
            if (cell.attr != HlAttr{})
            {
                auto [it, inserted] = remote_attr_ids_.try_emplace(
                    cell.attr,
                    static_cast<uint16_t>(next_remote_attr_id_));
                if (inserted)
                {
                    highlights().set(it->second, cell.attr);
                    ++next_remote_attr_id_;
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
        };

        if (rebuild_grid)
        {
            reset_highlights();
            grid().clear();
            for (int row = 0; row < display_snapshot.rows; ++row)
            {
                for (int col = 0; col < display_snapshot.cols; ++col)
                {
                    apply_cell(col, row, display_snapshot.cells[static_cast<size_t>(row) * display_snapshot.cols + col]);
                }
            }
        }
        else if (state->grid_update)
        {
            for (const auto& dirty : state->grid_update->cells)
                apply_cell(dirty.col, dirty.row, dirty.cell);
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
        if (metadata.title
            != published_window_title_)
        {
            callbacks().set_window_title(
                metadata.title.empty()
                    ? "Draxul"
                    : metadata.title);
            published_window_title_ = metadata.title;
        }
        const bool became_controller
            = controller_client_id_ != impl_->options().client_id
            && state->controller_client_id == impl_->options().client_id;
        controller_client_id_ = std::move(state->controller_client_id);
        remote_display_name_ = std::move(state->display_name);
        const bool process_exited
            = remote_process_running_ && !state->process_running;
        const bool exit_code_became_known
            = !remote_exit_code_ && state->exit_code.has_value();
        remote_process_running_ = state->process_running;
        remote_exit_code_ = state->process_running
            ? std::nullopt
            : state->exit_code;
        if (!remote_process_running_ && remote_exit_code_
            && *remote_exit_code_ != 0
            && (process_exited || exit_code_became_known))
        {
            callbacks().push_toast(
                1, "Shell exited unexpectedly. Use restart_host to start it again.");
        }
        if (became_controller)
            observer_input_hint_shown_ = false;
        if (state->clipboard_write
            && controller_client_id_ == impl_->options().client_id)
        {
            window().set_clipboard_text(*state->clipboard_write);
        }
        apply_mouse_modes(state->snapshot.metadata.modes.mouse);
        metadata_ = metadata;
        if (copy_mode_.active)
        {
            copy_mode_.cursor.x = std::clamp(
                copy_mode_.cursor.x, 0, std::max(0, grid_cols() - 1));
            copy_mode_.cursor.y = std::clamp(
                copy_mode_.cursor.y, 0, std::max(0, grid_rows() - 1));
            update_copy_mode_overlay();
        }
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

void RemoteTerminalHost::set_presentation_visible(bool visible)
{
    impl_->set_presentation_visible(visible);
}

bool RemoteTerminalHost::requires_periodic_wake() const
{
    return impl_->presentation_visible() && is_running();
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
    if (!event.pressed)
        return;

    if (suppress_next_selection_copy_text_input_)
        suppress_next_selection_copy_text_input_ = false;

    if (copy_mode_.active)
    {
        (void)handle_copy_mode_key(event);
        return;
    }

    if (selection_.is_active() && is_selection_copy_shortcut(event))
    {
        copy_active_selection_to_clipboard();
        selection_.clear();
        suppress_next_selection_copy_text_input_ = true;
        return;
    }

    if (handle_scrollback_key(event))
        return;

    if (controller_client_id_ != impl_->options().client_id)
    {
        show_observer_input_hint();
        return;
    }

    if (scroll_offset_ > 0)
        scroll_to_live();

    VtState state;
    state.cursor_app_mode = metadata_.modes.cursor_application;
    const std::string sequence = encode_terminal_key(event, state);
    if (!sequence.empty())
        send_remote_input(sequence);
}

void RemoteTerminalHost::on_text_input(const TextInputEvent& event)
{
    if (suppress_next_selection_copy_text_input_)
    {
        suppress_next_selection_copy_text_input_ = false;
        return;
    }
    if (event.text.empty() || copy_mode_.active)
        return;
    if (controller_client_id_ != impl_->options().client_id)
    {
        show_observer_input_hint();
        return;
    }
    if (scroll_offset_ > 0)
        scroll_to_live();
    send_remote_input(event.text);
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
    if (action == "toggle_copy_mode")
    {
        if (copy_mode_.active)
            exit_copy_mode(false);
        else
            enter_copy_mode();
        return true;
    }
    if (action == "paste")
    {
        if (controller_client_id_ != impl_->options().client_id)
        {
            show_observer_input_hint();
            return true;
        }
        const std::string clipboard = window().clipboard_text();
        const int threshold = launch_options().paste_confirm_lines;
        if (threshold > 0 && !clipboard.empty())
        {
            const int lines = 1 + static_cast<int>(std::count(clipboard.begin(), clipboard.end(), '\n'));
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

void RemoteTerminalHost::enter_copy_mode()
{
    pending_selection_copy_click_.reset();
    suppress_next_selection_copy_text_input_ = false;
    selection_.clear();
    copy_mode_.active = true;
    copy_mode_.selecting = false;
    copy_mode_.line_mode = false;
    copy_mode_.cursor = {
        std::clamp(metadata_.cursor.col, 0, std::max(0, grid_cols() - 1)),
        std::clamp(metadata_.cursor.row, 0, std::max(0, grid_rows() - 1)),
    };
    copy_mode_.anchor = copy_mode_.cursor;
    update_copy_mode_overlay();
    callbacks().push_toast(0,
        "Copy mode: hjkl/arrows to move, v/V select, y yank, q/Esc exit");
}

void RemoteTerminalHost::exit_copy_mode(bool yank)
{
    if (yank && selection_.is_active())
        copy_active_selection_to_clipboard();
    pending_selection_copy_click_.reset();
    suppress_next_selection_copy_text_input_ = false;
    selection_.clear();
    copy_mode_ = {};
    callbacks().request_frame();
}

void RemoteTerminalHost::update_copy_mode_overlay()
{
    if (copy_mode_.selecting)
    {
        if (copy_mode_.line_mode)
        {
            const int first_row
                = std::min(copy_mode_.anchor.y, copy_mode_.cursor.y);
            const int last_row
                = std::max(copy_mode_.anchor.y, copy_mode_.cursor.y);
            selection_.begin_drag({ { 0, first_row } });
            selection_.end_drag(
                { { std::max(0, grid_cols() - 1), last_row } });
        }
        else
        {
            selection_.begin_drag(
                { { copy_mode_.anchor.x, copy_mode_.anchor.y } });
            selection_.end_drag(
                { { copy_mode_.cursor.x, copy_mode_.cursor.y } });
        }
    }
    else
    {
        selection_.begin_drag(
            { { copy_mode_.cursor.x, copy_mode_.cursor.y } });
        selection_.end_drag(
            { { copy_mode_.cursor.x, copy_mode_.cursor.y } });
    }
    callbacks().request_frame();
}

bool RemoteTerminalHost::handle_copy_mode_key(const KeyEvent& event)
{
    const int cols = grid_cols();
    const int rows = grid_rows();
    auto clamp_cursor = [&] {
        copy_mode_.cursor.x = std::clamp(
            copy_mode_.cursor.x, 0, std::max(0, cols - 1));
        copy_mode_.cursor.y = std::clamp(
            copy_mode_.cursor.y, 0, std::max(0, rows - 1));
    };

    switch (event.keycode)
    {
    case SDLK_ESCAPE:
    case SDLK_Q:
        exit_copy_mode(false);
        return true;
    case SDLK_Y:
        exit_copy_mode(true);
        return true;
    case SDLK_V:
        if ((event.mod & kModShift) != 0)
        {
            copy_mode_.selecting = true;
            copy_mode_.line_mode = true;
            copy_mode_.anchor = copy_mode_.cursor;
        }
        else
        {
            copy_mode_.selecting = !copy_mode_.selecting;
            copy_mode_.line_mode = false;
            if (copy_mode_.selecting)
                copy_mode_.anchor = copy_mode_.cursor;
            else
                selection_.clear();
        }
        update_copy_mode_overlay();
        return true;
    case SDLK_H:
    case SDLK_LEFT:
        --copy_mode_.cursor.x;
        clamp_cursor();
        update_copy_mode_overlay();
        return true;
    case SDLK_L:
    case SDLK_RIGHT:
        ++copy_mode_.cursor.x;
        clamp_cursor();
        update_copy_mode_overlay();
        return true;
    case SDLK_K:
    case SDLK_UP:
        --copy_mode_.cursor.y;
        clamp_cursor();
        update_copy_mode_overlay();
        return true;
    case SDLK_J:
    case SDLK_DOWN:
        ++copy_mode_.cursor.y;
        clamp_cursor();
        update_copy_mode_overlay();
        return true;
    case SDLK_0:
    case SDLK_HOME:
        copy_mode_.cursor.x = 0;
        update_copy_mode_overlay();
        return true;
    case SDLK_END:
        copy_mode_.cursor.x = std::max(0, cols - 1);
        update_copy_mode_overlay();
        return true;
    case SDLK_G:
        copy_mode_.cursor.y = (event.mod & kModShift) != 0
            ? std::max(0, rows - 1)
            : 0;
        update_copy_mode_overlay();
        return true;
    default:
        return true;
    }
}

bool RemoteTerminalHost::copy_active_selection_to_clipboard()
{
    if (!selection_.is_active())
        return false;
    const std::string text = selection_.extract_text();
    return !text.empty() && window().set_clipboard_text(text);
}

bool RemoteTerminalHost::handle_scrollback_key(const KeyEvent& event)
{
    if ((event.mod & kModShift) == 0)
        return false;

    RemoteHostCommand command;
    if (event.keycode == SDLK_PAGEUP)
    {
        command.kind = RemoteHostCommand::Kind::Scroll;
        command.scroll_rows = std::max(1, grid_rows());
    }
    else if (event.keycode == SDLK_PAGEDOWN)
    {
        command.kind = RemoteHostCommand::Kind::Scroll;
        command.scroll_rows = -std::max(1, grid_rows());
    }
    else if (event.keycode == SDLK_HOME)
    {
        command.kind = RemoteHostCommand::Kind::Scroll;
        command.scroll_rows = std::numeric_limits<int>::max();
    }
    else if (event.keycode == SDLK_END)
    {
        command.kind = RemoteHostCommand::Kind::ScrollToLive;
    }
    else
    {
        return false;
    }

    selection_.clear();
    pending_selection_copy_click_.reset();
    impl_->enqueue(std::move(command));
    return true;
}

void RemoteTerminalHost::scroll_to_live()
{
    selection_.clear();
    pending_selection_copy_click_.reset();
    impl_->enqueue({
        .kind = RemoteHostCommand::Kind::ScrollToLive,
    });
}

void RemoteTerminalHost::show_observer_input_hint()
{
    if (observer_input_hint_shown_)
        return;
    observer_input_hint_shown_ = true;
    callbacks().push_toast(
        0, "This terminal is read-only. Use Take Terminal Control to type.");
}

void RemoteTerminalHost::send_remote_input(std::string text)
{
    if (text.empty())
        return;
    if (controller_client_id_ != impl_->options().client_id)
    {
        show_observer_input_hint();
        return;
    }
    if (!impl_->enqueue_input(text))
    {
        callbacks().push_toast(
            1, "Terminal input queue is full; input was not sent.");
    }
}

void RemoteTerminalHost::send_paste(std::string_view text)
{
    if (controller_client_id_ != impl_->options().client_id)
    {
        show_observer_input_hint();
        return;
    }
    if (scroll_offset_ > 0)
        scroll_to_live();
    if (metadata_.modes.bracketed_paste)
    {
        constexpr std::string_view begin = "\x1B[200~";
        constexpr std::string_view end = "\x1B[201~";
        constexpr size_t framing_bytes = begin.size() + end.size();
        constexpr size_t payload_bytes
            = kRemoteInputBatchBytes - framing_bytes;
        std::vector<std::string> chunks;
        chunks.reserve(std::max<size_t>(
            1, (text.size() + payload_bytes - 1) / payload_bytes));
        size_t offset = 0;
        do
        {
            const size_t count
                = std::min(payload_bytes, text.size() - offset);
            std::string chunk;
            chunk.reserve(count + framing_bytes);
            chunk.append(begin);
            chunk.append(text.substr(offset, count));
            chunk.append(end);
            chunks.push_back(std::move(chunk));
            offset += count;
        } while (offset < text.size());
        if (!impl_->enqueue_input_chunks(std::move(chunks)))
        {
            callbacks().push_toast(
                1, "Terminal input queue is full; paste was not sent.");
        }
    }
    else
    {
        send_remote_input(std::string(text));
    }
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

std::string RemoteTerminalHost::display_name() const
{
    return remote_display_name_
        + (remote_process_running_ ? "" : " [exited]");
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
    if (!impl_->presentation_visible())
        return std::nullopt;
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
