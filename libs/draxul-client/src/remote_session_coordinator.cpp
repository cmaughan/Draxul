#include <draxul/remote_session_coordinator.h>

#include <draxul/remote_terminal_client.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace draxul
{

namespace
{

constexpr size_t kCommandLimit = 128;
constexpr size_t kCommandsPerPoll = 8;
constexpr size_t kInputBatchBytes = 48 * 1024;
constexpr auto kPollInterval = std::chrono::milliseconds(25);
constexpr auto kShutdownJoinBudget = std::chrono::milliseconds(250);
std::atomic<uint64_t> g_next_request_id{ 1 };

uint64_t next_request_id()
{
    uint64_t result = g_next_request_id.fetch_add(1);
    if (result == 0)
        result = g_next_request_id.fetch_add(1);
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

bool is_removed_terminal(std::string_view code)
{
    return code == "terminal_not_found";
}

bool needs_identity_refresh(std::string_view code)
{
    return code == "stale_epoch"
        || code == "invalid_connection_token";
}

struct CoordinatorCommand
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

} // namespace

class RemoteSessionCoordinator::Impl
    : public std::enable_shared_from_this<RemoteSessionCoordinator::Impl>
{
public:
    class Entry final : public std::enable_shared_from_this<Entry>
    {
    public:
        Entry(std::weak_ptr<Impl> coordinator, uint64_t registration_id,
            RemoteSessionCoordinatorOptions options,
            std::string terminal_id)
            : coordinator_(std::move(coordinator))
            , registration_id_(registration_id)
            , options_(std::move(options))
            , terminal_id_(std::move(terminal_id))
            , suspend_available_(
                  options_.presentation_suspend_supported)
        {
        }

        ~Entry()
        {
            request_stop();
            join_worker();
        }

        bool start()
        {
            if (!options_.recovery)
            {
                options_.recovery
                    = std::make_shared<ClientRecoveryState>(
                        options_.client_id);
                options_.recovery->set_server_epoch(
                    options_.expected_server_epoch);
            }
            client_ = std::make_unique<RemoteTerminalClient>(
                RemoteTerminalClientOptions{
                    .runtime_directory = options_.runtime_directory,
                    .client_id = options_.client_id,
                    .session_id = options_.session_id,
                    .expected_server_epoch
                    = options_.expected_server_epoch,
                    .method_prefix = options_.method_prefix,
                    .terminal_id = terminal_id_,
                    .recovery = options_.recovery,
                });
            bool expected = false;
            if (!running_.compare_exchange_strong(expected, true))
                return false;
            stopping_ = false;
            {
                std::lock_guard lock(worker_exit_mutex_);
                worker_exited_ = false;
            }
            worker_ = std::jthread([this] { worker_main(); });
            return true;
        }

        void request_stop()
        {
            stopping_ = true;
            command_wake_.notify_all();
        }

        void stop_until(
            std::chrono::steady_clock::time_point deadline)
        {
            request_stop();
            bool exited = false;
            {
                std::unique_lock lock(worker_exit_mutex_);
                if (!worker_exited_)
                {
                    worker_exited_changed_.wait_until(
                        lock, deadline,
                        [this] { return worker_exited_; });
                }
                exited = worker_exited_;
            }
            if (exited)
            {
                join_worker();
                running_ = false;
                return;
            }

            bool expected = false;
            if (reaper_started_.compare_exchange_strong(
                    expected, true))
            {
                std::thread([self = shared_from_this()] {
                    self->join_worker();
                }).detach();
            }
            running_ = false;
        }

        bool enqueue_input(std::string_view text)
        {
            if (text.empty())
                return true;

            std::lock_guard guard(mutex_);
            if (stopping_)
                return false;
            const size_t maximum_new_commands
                = (text.size() + kInputBatchBytes - 1)
                / kInputBatchBytes;
            if (maximum_new_commands
                > kCommandLimit
                    - std::min(commands_.size(), kCommandLimit))
            {
                return false;
            }

            auto staged = commands_;
            size_t offset = 0;
            while (offset < text.size())
            {
                const size_t count = std::min(
                    kInputBatchBytes, text.size() - offset);
                const std::string_view chunk
                    = text.substr(offset, count);
                if (!staged.empty()
                    && staged.back().kind
                        == CoordinatorCommand::Kind::Input
                    && !staged.back().attempted
                    && staged.back().mergeable
                    && staged.back().text.size()
                        <= kInputBatchBytes
                    && staged.back().text.size() + chunk.size()
                        <= kInputBatchBytes)
                {
                    staged.back().text.append(chunk);
                }
                else
                {
                    if (staged.size() >= kCommandLimit)
                        return false;
                    staged.push_back({
                        .kind = CoordinatorCommand::Kind::Input,
                        .text = std::string(chunk),
                        .request_id = next_request_id(),
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
                    > kCommandLimit
                        - std::min(commands_.size(), kCommandLimit))
            {
                return false;
            }
            auto staged = commands_;
            for (auto& chunk : chunks)
            {
                if (chunk.empty() || chunk.size() > kInputBatchBytes)
                    return false;
                staged.push_back({
                    .kind = CoordinatorCommand::Kind::Input,
                    .text = std::move(chunk),
                    .request_id = next_request_id(),
                    .mergeable = false,
                });
            }
            commands_.swap(staged);
            command_wake_.notify_one();
            return true;
        }

        bool enqueue(CoordinatorCommand command)
        {
            std::lock_guard guard(mutex_);
            if (stopping_)
                return false;
            if (command.request_id == 0)
                command.request_id = next_request_id();
            if (!commands_.empty()
                && command.kind == CoordinatorCommand::Kind::Resize
                && commands_.back().kind
                    == CoordinatorCommand::Kind::Resize
                && !commands_.back().attempted)
            {
                commands_.back() = std::move(command);
                command_wake_.notify_one();
                return true;
            }
            if (!commands_.empty()
                && command.kind == CoordinatorCommand::Kind::Scroll
                && commands_.back().kind
                    == CoordinatorCommand::Kind::Scroll)
            {
                const int64_t combined
                    = static_cast<int64_t>(
                          commands_.back().scroll_rows)
                    + command.scroll_rows;
                commands_.back().scroll_rows
                    = static_cast<int>(std::clamp(
                        combined,
                        static_cast<int64_t>(
                            std::numeric_limits<int>::min()),
                        static_cast<int64_t>(
                            std::numeric_limits<int>::max())));
                command_wake_.notify_one();
                return true;
            }
            if (commands_.size() >= kCommandLimit)
                return false;
            commands_.push_back(std::move(command));
            command_wake_.notify_one();
            return true;
        }

        uint64_t set_presentation_visible(bool visible)
        {
            {
                std::lock_guard guard(mutex_);
                if (presentation_visible_ == visible)
                    return visibility_generation_;
                ++visibility_generation_;
                presentation_visible_ = visible;
                if (!visible)
                {
                    published_state_.reset();
                    acknowledge_if_idle_locked();
                }
            }
            command_wake_.notify_one();
            return visibility_generation_;
        }

        bool presentation_visible() const noexcept
        {
            return presentation_visible_;
        }

        uint64_t visibility_generation() const noexcept
        {
            return visibility_generation_;
        }

        std::optional<RemoteTerminalPublishedState>
        take_published_state()
        {
            std::optional<RemoteTerminalPublishedState> result;
            uint64_t serial = 0;
            bool idle = false;
            {
                std::lock_guard guard(mutex_);
                result = std::move(published_state_);
                published_state_.reset();
                serial = publication_serial_;
                idle = pending_error_.empty();
            }
            if (idle)
                acknowledge_consumed(serial);
            return result;
        }

        std::string take_error()
        {
            std::string result;
            uint64_t serial = 0;
            bool idle = false;
            {
                std::lock_guard guard(mutex_);
                result = std::exchange(pending_error_, {});
                serial = publication_serial_;
                idle = !published_state_;
            }
            if (idle)
                acknowledge_consumed(serial);
            return result;
        }

        bool running() const noexcept
        {
            return running_;
        }

        std::string last_error_code() const
        {
            std::lock_guard guard(mutex_);
            return last_error_code_;
        }

    private:
        void acknowledge_if_idle_locked()
        {
            if (pending_error_.empty())
            {
                const uint64_t serial = publication_serial_;
                if (auto coordinator = coordinator_.lock())
                    coordinator->mark_consumed(
                        registration_id_, serial);
            }
        }

        void acknowledge_consumed(uint64_t serial)
        {
            if (auto coordinator = coordinator_.lock())
                coordinator->mark_consumed(
                    registration_id_, serial);
        }

        std::function<void()> reserve_ready(uint64_t serial)
        {
            if (auto coordinator = coordinator_.lock())
                return coordinator->mark_ready(registration_id_, serial);
            return {};
        }

        void remember_error_code(std::string code)
        {
            std::lock_guard guard(mutex_);
            last_error_code_ = std::move(code);
        }

        std::string client_error_code()
        {
            std::string result = client_->last_error_code();
            remember_error_code(result);
            return result;
        }

        void note_connected(std::string_view channel)
        {
            options_.recovery->note_connected(channel);
            remember_error_code({});
        }

        bool execute_command(
            const CoordinatorCommand& command, std::string& error)
        {
            switch (command.kind)
            {
            case CoordinatorCommand::Kind::Input:
                return client_->send_input(
                    command.text, error, command.request_id);
            case CoordinatorCommand::Kind::Resize:
                return client_->resize(command.cols, command.rows,
                    error, command.request_id);
            case CoordinatorCommand::Kind::TakeControl:
                return client_->take_control(
                    error, command.request_id);
            case CoordinatorCommand::Kind::Scroll:
                return scroll_by(command.scroll_rows, error);
            case CoordinatorCommand::Kind::ScrollToLive:
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
            {
                client_error_code();
                return false;
            }
            terminal_id_
                = client_->projection().version().terminal_id;
            note_connected(recovery_channel());
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
            const auto delay = options_.recovery->note_failure(
                recovery_channel());
            const auto recovery = options_.recovery->snapshot(
                recovery_channel());
            if (recovery.attempts == 1)
                publish_error_once(error_code, error);
            return delay;
        }

        void requeue_commands(
            std::deque<CoordinatorCommand>& batch, size_t from)
        {
            std::lock_guard guard(mutex_);
            for (size_t index = batch.size(); index > from; --index)
                commands_.push_front(std::move(batch[index - 1]));
            command_wake_.notify_one();
        }

        bool refresh_epoch(std::string& error)
        {
            return options_.recovery->refresh_server_epoch(
                options_.runtime_directory,
                options_.client_id, error);
        }

        std::string recovery_channel() const
        {
            return "terminal:"
                + (terminal_id_.empty()
                        ? options_.method_prefix
                        : terminal_id_);
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
                        std::string error_code
                            = client_error_code();
                        if (is_removed_terminal(error_code))
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
                        const auto delay = note_recoverable_failure(
                            error_code, error);
                        wait_for_retry(delay);
                        continue;
                    }
                }

                std::deque<CoordinatorCommand> commands;
                bool visible = true;
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
                        command_wake_.wait_for(lock, kPollInterval,
                            [this] {
                                return stopping_ || !commands_.empty()
                                    || (suspend_available_
                                        && presentation_visible_
                                            == presentation_suspended_);
                            });
                    }
                    visible = presentation_visible_;
                    for (size_t count = 0;
                        count < kCommandsPerPoll
                        && !commands_.empty(); ++count)
                    {
                        commands.push_back(
                            std::move(commands_.front()));
                        commands_.pop_front();
                    }
                }
                if (stopping_)
                    break;

                if (suspended && (visible || !commands.empty()))
                {
                    std::string error;
                    if (client_->resume(error))
                    {
                        suspended = false;
                        presentation_suspended_ = false;
                        note_connected(recovery_channel());
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
                            = client_error_code();
                        if (error_code == "not_attached")
                        {
                            if (recover_attachment(error))
                            {
                                attached = true;
                                suspended = false;
                                presentation_suspended_ = false;
                                continue;
                            }
                            error_code = client_error_code();
                        }
                        if (is_removed_terminal(error_code))
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
                        const auto delay = note_recoverable_failure(
                            error_code, error);
                        wait_for_retry(delay);
                        continue;
                    }
                }

                if (!suspended && !visible && commands.empty()
                    && suspend_available_)
                {
                    std::string error;
                    if (client_->suspend(error, next_request_id()))
                    {
                        suspended = true;
                        presentation_suspended_ = true;
                        note_connected(recovery_channel());
                        client_->take_grid_update();
                        client_->take_clipboard_write();
                        continue;
                    }
                    const std::string error_code
                        = client_error_code();
                    if (error_code == "unknown_method")
                        suspend_available_ = false;
                    else if (error_code == "not_attached")
                    {
                        attached = false;
                        continue;
                    }
                    else if (is_removed_terminal(error_code))
                    {
                        terminal_removed = true;
                        break;
                    }
                    else if (is_transient_client_error(error_code)
                        || is_resynchronizing_client_error(error_code))
                    {
                        const auto delay = note_recoverable_failure(
                            error_code, error);
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
                            = client_error_code();
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
                                    note_connected(recovery_channel());
                                    continue;
                                }
                                error_code = client_error_code();
                            }
                            else
                            {
                                error = std::move(attach_error);
                                error_code = client_error_code();
                            }
                        }
                        if (is_removed_terminal(error_code))
                        {
                            terminal_removed = true;
                            break;
                        }
                        if (is_expected_command_error(error_code)
                            || error_code == "unknown_method")
                        {
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
                        if (is_transient_client_error(error_code)
                            || is_resynchronizing_client_error(
                                error_code))
                        {
                            requeue_commands(commands, command_index);
                            const auto delay = note_recoverable_failure(
                                error_code, error);
                            wait_for_retry(delay);
                            retry_batch = true;
                            break;
                        }
                        publish_error_once(error_code, error);
                        continue;
                    }
                    note_connected(recovery_channel());
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
                    std::string error_code = client_error_code();
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
                            note_connected(recovery_channel());
                            continue;
                        }
                        error = std::move(attach_error);
                        error_code = client_error_code();
                    }
                    if (is_removed_terminal(error_code))
                        break;
                    if (error_code == "unknown_method")
                    {
                        publish_error_once(error_code, error);
                        wait_for_retry(options_.recovery->note_failure(
                            recovery_channel()));
                        continue;
                    }
                    if (is_transient_client_error(error_code)
                        || is_resynchronizing_client_error(error_code))
                    {
                        const auto delay = note_recoverable_failure(
                            error_code, error);
                        wait_for_retry(delay);
                        continue;
                    }
                    publish_error_once(error_code, error);
                    attached = false;
                    const auto delay = note_recoverable_failure(
                        error_code, error);
                    wait_for_retry(delay);
                    continue;
                }
                if (stopping_)
                    break;
                note_connected(recovery_channel());
                if (changed)
                {
                    if (scroll_offset_ > 0
                        && !refresh_scrollback_after_output(error))
                    {
                        const std::string error_code
                            = client_error_code();
                        if (error_code == "unknown_method")
                        {
                            scroll_offset_ = 0;
                            scrollback_page_.reset();
                            publish_error(std::move(error));
                        }
                        else if (is_transient_client_error(error_code)
                            || is_resynchronizing_client_error(
                                error_code))
                        {
                            const auto delay
                                = note_recoverable_failure(
                                    error_code, error);
                            wait_for_retry(delay);
                            continue;
                        }
                        else
                        {
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

            if (!stopping_ && attached)
            {
                std::string ignored;
                client_->disconnect(ignored);
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
            RemoteTerminalPublishedState state{
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
                .visibility_generation = visibility_generation_,
            };
            std::function<void()> wake;
            {
                std::lock_guard guard(mutex_);
                if (!presentation_visible_
                    || state.visibility_generation
                        != visibility_generation_)
                {
                    return;
                }
                if (published_state_)
                {
                    if (published_state_->grid_update)
                    {
                        state.grid_update
                            = full_grid_update(state.snapshot);
                    }
                    if (!state.clipboard_write
                        && published_state_->clipboard_write)
                    {
                        state.clipboard_write = std::move(
                            published_state_->clipboard_write);
                    }
                }
                published_state_ = std::move(state);
                wake = reserve_ready(++publication_serial_);
            }
            if (wake)
                wake();
        }

        bool read_scrollback(uint64_t offset,
            RemoteTerminalScrollbackPage& page, std::string& error)
        {
            const auto& snapshot = client_->projection().snapshot();
            const size_t rows = static_cast<size_t>(
                std::max(1, snapshot.rows));
            return client_->read_scrollback(offset,
                std::min(rows,
                    kRemoteTerminalMaxScrollbackPageRows),
                page, error);
        }

        bool scroll_by(int rows, std::string& error)
        {
            if (rows == 0)
                return true;
            const int64_t requested = std::max<int64_t>(0,
                static_cast<int64_t>(scroll_offset_) + rows);
            if (requested == 0)
            {
                scroll_offset_ = 0;
                scrollback_page_.reset();
                publish_projection();
                return true;
            }
            RemoteTerminalScrollbackPage page;
            if (!read_scrollback(
                    static_cast<uint64_t>(requested), page, error))
            {
                return false;
            }
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
            if (anchored_offset != scroll_offset_
                && !read_scrollback(anchored_offset, page, error))
            {
                return false;
            }
            scrollback_total_ = page.total_rows;
            scroll_offset_ = anchored_offset;
            scrollback_page_ = std::move(page);
            return true;
        }

        void publish_error(std::string error)
        {
            std::function<void()> wake;
            {
                std::lock_guard guard(mutex_);
                pending_error_ = std::move(error);
                wake = reserve_ready(++publication_serial_);
            }
            if (wake)
                wake();
        }

        void publish_error_once(
            std::string_view error_code, const std::string& error)
        {
            const std::string key = error_code.empty()
                ? error
                : std::string(error_code);
            {
                std::lock_guard guard(mutex_);
                if (!published_error_keys_.insert(key).second)
                    return;
            }
            publish_error(error);
        }

        std::weak_ptr<Impl> coordinator_;
        uint64_t registration_id_ = 0;
        RemoteSessionCoordinatorOptions options_;
        std::string terminal_id_;
        std::unique_ptr<RemoteTerminalClient> client_;
        std::jthread worker_;
        std::atomic<bool> running_ = false;
        std::atomic<bool> stopping_ = false;
        std::atomic<bool> reaper_started_ = false;
        std::atomic<bool> presentation_visible_ = true;
        std::atomic<uint64_t> visibility_generation_ = 1;
        std::atomic<bool> presentation_suspended_ = false;
        std::atomic<bool> suspend_available_ = false;
        mutable std::mutex mutex_;
        std::mutex worker_exit_mutex_;
        std::condition_variable worker_exited_changed_;
        bool worker_exited_ = true;
        std::mutex worker_join_mutex_;
        std::condition_variable command_wake_;
        std::deque<CoordinatorCommand> commands_;
        std::optional<RemoteTerminalPublishedState> published_state_;
        std::optional<RemoteTerminalScrollbackPage> scrollback_page_;
        uint64_t scroll_offset_ = 0;
        uint64_t scrollback_total_ = 0;
        std::string pending_error_;
        std::string last_error_code_;
        std::unordered_set<std::string> published_error_keys_;
        uint64_t publication_serial_ = 0;
    };

    explicit Impl(RemoteSessionCoordinatorOptions options)
        : options_(std::move(options))
    {
        if (!options_.recovery)
        {
            options_.recovery
                = std::make_shared<ClientRecoveryState>(
                    options_.client_id);
            options_.recovery->set_server_epoch(
                options_.expected_server_epoch);
        }
    }

    ~Impl()
    {
        stop();
    }

    bool start()
    {
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true))
            return false;
        stopping_ = false;
        return true;
    }

    void stop()
    {
        if (!running_.exchange(false) && stopping_)
            return;
        stopping_ = true;
        std::vector<std::shared_ptr<Entry>> entries;
        {
            std::lock_guard guard(mutex_);
            entries.reserve(entries_.size());
            for (auto& [id, entry] : entries_)
                entries.push_back(std::move(entry));
            entries_.clear();
            ready_.clear();
            wake_pending_ = false;
        }
        for (const auto& entry : entries)
            entry->request_stop();
        const auto deadline
            = std::chrono::steady_clock::now()
            + kShutdownJoinBudget;
        for (const auto& entry : entries)
            entry->stop_until(deadline);
    }

    uint64_t register_terminal(
        std::string terminal_id)
    {
        std::shared_ptr<Entry> entry;
        uint64_t id = 0;
        {
            std::lock_guard guard(mutex_);
            if (!running_ || stopping_)
                return 0;
            do
            {
                id = next_registration_id_++;
            } while (id == 0 || entries_.contains(id));
            entry = std::make_shared<Entry>(weak_from_this(), id,
                options_, std::move(terminal_id));
            entries_.emplace(id, entry);
        }
        if (entry->start())
            return id;
        unregister_terminal(id);
        return 0;
    }

    void unregister_terminal(uint64_t id)
    {
        std::shared_ptr<Entry> entry;
        {
            std::lock_guard guard(mutex_);
            const auto found = entries_.find(id);
            if (found == entries_.end())
                return;
            entry = std::move(found->second);
            entries_.erase(found);
            ready_.erase(id);
        }
        entry->stop_until(
            std::chrono::steady_clock::now()
            + kShutdownJoinBudget);
    }

    std::shared_ptr<Entry> entry(uint64_t id) const
    {
        std::lock_guard guard(mutex_);
        const auto found = entries_.find(id);
        return found == entries_.end() ? nullptr : found->second;
    }

    std::function<void()> mark_ready(uint64_t id, uint64_t serial)
    {
        std::lock_guard guard(mutex_);
        if (stopping_ || !entries_.contains(id))
            return {};
        auto& ready_serial = ready_[id];
        ready_serial = std::max(ready_serial, serial);
        if (wake_pending_)
            return {};
        wake_pending_ = true;
        return options_.wake_consumer;
    }

    void mark_consumed(uint64_t id, uint64_t serial)
    {
        std::lock_guard guard(mutex_);
        const auto found = ready_.find(id);
        if (found != ready_.end() && found->second <= serial)
            ready_.erase(found);
    }

    void acknowledge_wake()
    {
        std::function<void()> wake;
        {
            std::lock_guard guard(mutex_);
            wake_pending_ = false;
            if (!stopping_ && !ready_.empty())
            {
                wake_pending_ = true;
                wake = options_.wake_consumer;
            }
        }
        if (wake)
            wake();
    }

private:
    RemoteSessionCoordinatorOptions options_;
    std::atomic<bool> running_ = false;
    std::atomic<bool> stopping_ = true;
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<Entry>> entries_;
    std::unordered_map<uint64_t, uint64_t> ready_;
    uint64_t next_registration_id_ = 1;
    bool wake_pending_ = false;
};

class RemoteSessionCoordinator::Registration::State
{
public:
    State(std::weak_ptr<Impl> coordinator, uint64_t id)
        : coordinator(std::move(coordinator))
        , id(id)
    {
    }

    std::shared_ptr<Impl::Entry> entry() const
    {
        if (auto owner = coordinator.lock())
            return owner->entry(id);
        return nullptr;
    }

    std::weak_ptr<Impl> coordinator;
    uint64_t id = 0;
};

RemoteSessionCoordinator::Registration::Registration(
    std::unique_ptr<State> state)
    : state_(std::move(state))
{
}

RemoteSessionCoordinator::Registration::Registration() = default;

RemoteSessionCoordinator::Registration::~Registration()
{
    reset();
}

RemoteSessionCoordinator::Registration::Registration(
    Registration&& other) noexcept = default;

RemoteSessionCoordinator::Registration&
RemoteSessionCoordinator::Registration::operator=(
    Registration&& other) noexcept
{
    if (this != &other)
    {
        reset();
        state_ = std::move(other.state_);
    }
    return *this;
}

RemoteSessionCoordinator::Registration::operator bool() const noexcept
{
    return state_ && state_->id != 0
        && !state_->coordinator.expired();
}

uint64_t RemoteSessionCoordinator::Registration::id() const noexcept
{
    return state_ ? state_->id : 0;
}

bool RemoteSessionCoordinator::Registration::enqueue_input(
    std::string_view text)
{
    const auto entry = state_ ? state_->entry() : nullptr;
    return entry && entry->enqueue_input(text);
}

bool RemoteSessionCoordinator::Registration::enqueue_input_chunks(
    std::vector<std::string> chunks)
{
    const auto entry = state_ ? state_->entry() : nullptr;
    return entry
        && entry->enqueue_input_chunks(std::move(chunks));
}

bool RemoteSessionCoordinator::Registration::enqueue_resize(
    int cols, int rows)
{
    const auto entry = state_ ? state_->entry() : nullptr;
    return entry && entry->enqueue({
                        .kind = CoordinatorCommand::Kind::Resize,
                        .cols = cols,
                        .rows = rows,
                    });
}

bool RemoteSessionCoordinator::Registration::enqueue_take_control()
{
    const auto entry = state_ ? state_->entry() : nullptr;
    return entry && entry->enqueue({
                        .kind
                        = CoordinatorCommand::Kind::TakeControl,
                    });
}

bool RemoteSessionCoordinator::Registration::enqueue_scroll(int rows)
{
    const auto entry = state_ ? state_->entry() : nullptr;
    return entry && entry->enqueue({
                        .kind = CoordinatorCommand::Kind::Scroll,
                        .scroll_rows = rows,
                    });
}

bool RemoteSessionCoordinator::Registration::enqueue_scroll_to_live()
{
    const auto entry = state_ ? state_->entry() : nullptr;
    return entry && entry->enqueue({
                        .kind
                        = CoordinatorCommand::Kind::ScrollToLive,
                    });
}

uint64_t RemoteSessionCoordinator::Registration::set_presentation_visible(
    bool visible)
{
    const auto entry = state_ ? state_->entry() : nullptr;
    return entry ? entry->set_presentation_visible(visible) : 0;
}

bool RemoteSessionCoordinator::Registration::presentation_visible() const
{
    const auto entry = state_ ? state_->entry() : nullptr;
    return entry && entry->presentation_visible();
}

uint64_t RemoteSessionCoordinator::Registration::visibility_generation()
    const
{
    const auto entry = state_ ? state_->entry() : nullptr;
    return entry ? entry->visibility_generation() : 0;
}

std::optional<RemoteTerminalPublishedState>
RemoteSessionCoordinator::Registration::take_published_state()
{
    const auto entry = state_ ? state_->entry() : nullptr;
    return entry ? entry->take_published_state() : std::nullopt;
}

std::string RemoteSessionCoordinator::Registration::take_error()
{
    const auto entry = state_ ? state_->entry() : nullptr;
    return entry ? entry->take_error() : std::string{};
}

bool RemoteSessionCoordinator::Registration::running() const
{
    const auto entry = state_ ? state_->entry() : nullptr;
    return entry && entry->running();
}

std::string RemoteSessionCoordinator::Registration::last_error_code() const
{
    const auto entry = state_ ? state_->entry() : nullptr;
    return entry ? entry->last_error_code() : std::string{};
}

void RemoteSessionCoordinator::Registration::reset()
{
    if (!state_)
        return;
    if (auto coordinator = state_->coordinator.lock())
        coordinator->unregister_terminal(state_->id);
    state_.reset();
}

RemoteSessionCoordinator::RemoteSessionCoordinator(
    RemoteSessionCoordinatorOptions options)
    : impl_(std::make_shared<Impl>(std::move(options)))
{
}

RemoteSessionCoordinator::~RemoteSessionCoordinator()
{
    stop();
}

bool RemoteSessionCoordinator::start()
{
    return impl_->start();
}

void RemoteSessionCoordinator::stop()
{
    if (impl_)
        impl_->stop();
}

RemoteSessionCoordinator::Registration
RemoteSessionCoordinator::register_terminal(std::string terminal_id)
{
    const uint64_t id
        = impl_->register_terminal(std::move(terminal_id));
    if (id == 0)
        return {};
    return Registration(std::make_unique<Registration::State>(
        impl_, id));
}

void RemoteSessionCoordinator::acknowledge_wake()
{
    impl_->acknowledge_wake();
}

} // namespace draxul
