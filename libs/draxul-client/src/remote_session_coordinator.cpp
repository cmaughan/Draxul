#include <draxul/remote_session_coordinator.h>

#include <draxul/async_frame_stream.h>
#include <draxul/remote_session_client.h>
#include <draxul/remote_terminal_client.h>
#include <draxul/server_client.h>
#include <draxul/server_control_channel.h>
#include <draxul/session_protocol.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
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
constexpr auto kIntermediatePollInterval = std::chrono::milliseconds(50);
constexpr auto kIdlePollInterval = std::chrono::milliseconds(100);
constexpr auto kSessionPollRequestBudget = std::chrono::milliseconds(100);
constexpr auto kSessionStreamConnectBudget = std::chrono::milliseconds(250);
constexpr auto kShutdownJoinBudget = std::chrono::milliseconds(250);
constexpr size_t kSessionStreamInboxFrameLimit = 64;
std::atomic<uint64_t> g_next_request_id{ 1 };

enum class SessionTransportMode
{
    StreamOpening,
    StreamActive,
    SessionPoll,
    Legacy,
};

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

        bool start(bool legacy_worker)
        {
            if (!options_.recovery)
            {
                options_.recovery
                    = std::make_shared<ClientRecoveryState>(
                        options_.client_id);
                options_.recovery->set_server_epoch(
                    options_.expected_server_epoch);
            }
            reset_client(legacy_worker
                    ? std::optional<std::chrono::milliseconds>{}
                    : std::optional<std::chrono::milliseconds>{
                        kSessionPollRequestBudget });
            bool expected = false;
            if (!running_.compare_exchange_strong(expected, true))
                return false;
            stopping_ = false;
            if (legacy_worker)
            {
                std::lock_guard lock(worker_exit_mutex_);
                worker_exited_ = false;
                worker_ = std::jthread([this] { worker_main(); });
            }
            return true;
        }

        void reset_client(
            std::optional<std::chrono::milliseconds> request_timeout)
        {
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
                    .request_timeout = request_timeout,
                });
        }

        void start_legacy_worker()
        {
            if (stopping_ || worker_.joinable())
                return;
            reset_client(std::nullopt);
            {
                std::lock_guard lock(worker_exit_mutex_);
                worker_exited_ = false;
            }
            worker_ = std::jthread([this] { worker_main(); });
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
            wake_coordinator();
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
            wake_coordinator();
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
                wake_coordinator();
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
                wake_coordinator();
                return true;
            }
            if (commands_.size() >= kCommandLimit)
                return false;
            commands_.push_back(std::move(command));
            command_wake_.notify_one();
            wake_coordinator();
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
                batch_needs_attach_ = true;
                if (!visible)
                {
                    published_state_.reset();
                    acknowledge_if_idle_locked();
                }
            }
            command_wake_.notify_one();
            wake_coordinator();
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

        SessionTerminalSubscription batch_subscription() const
        {
            std::lock_guard guard(mutex_);
            SessionTerminalSubscription result{
                .subscription_id = registration_id_,
                .terminal_id = terminal_id_,
                .visibility_generation = visibility_generation_,
                .visible = presentation_visible_,
            };
            if (!batch_needs_attach_ && client_->projection().attached())
            {
                const auto& version = client_->projection().version();
                result.cursor = SessionTerminalCursor{
                    .generation = version.generation,
                    .after_sequence = version.sequence,
                };
            }
            return result;
        }

        bool batch_active() const noexcept
        {
            return running_ && !stopping_;
        }

        bool process_batch_commands()
        {
            std::deque<CoordinatorCommand> commands;
            {
                std::lock_guard guard(mutex_);
                for (size_t count = 0;
                    count < 1 && !commands_.empty(); ++count)
                {
                    commands.push_back(std::move(commands_.front()));
                    commands_.pop_front();
                }
            }
            for (size_t index = 0; index < commands.size(); ++index)
            {
                auto& command = commands[index];
                command.attempted = true;
                std::string error;
                if (execute_command(command, error))
                {
                    note_connected(recovery_channel());
                    continue;
                }
                const std::string error_code = client_error_code();
                if (is_expected_command_error(error_code)
                    || error_code == "unknown_method")
                {
                    publish_error_once(error_code, error);
                    continue;
                }
                if (error_code == "not_attached"
                    || is_resynchronizing_client_error(error_code))
                {
                    std::lock_guard guard(mutex_);
                    batch_needs_attach_ = true;
                }
                if (is_transient_client_error(error_code)
                    || is_resynchronizing_client_error(error_code)
                    || needs_identity_refresh(error_code))
                {
                    requeue_commands(commands, index);
                    break;
                }
                publish_error_once(error_code, error);
            }
            return !commands.empty();
        }

        bool accept_batch(const SessionTerminalPollBatch& batch,
            std::chrono::microseconds latency)
        {
            if (batch.subscription_id != registration_id_)
                return false;
            bool suspension_changed = false;
            {
                std::lock_guard guard(mutex_);
                if (batch.visibility_generation
                    != visibility_generation_)
                {
                    return false;
                }
                suspension_changed
                    = presentation_suspended_ != batch.suspended;
                presentation_suspended_ = batch.suspended;
            }
            if (!batch.error_code.empty())
            {
                remember_error_code(batch.error_code);
                const bool published = publish_error_once(
                    batch.error_code, batch.error_message);
                if (is_removed_terminal(batch.error_code))
                {
                    stopping_ = true;
                    running_ = false;
                }
                return published;
            }
            std::string error;
            if (batch.attach)
            {
                if (!client_->accept_attach(*batch.attach, error, latency))
                {
                    publish_error_once(client_error_code(), error);
                    std::lock_guard guard(mutex_);
                    batch_needs_attach_ = true;
                    return true;
                }
                terminal_id_ = client_->projection().version().terminal_id;
                scroll_offset_ = 0;
                scrollback_total_ = 0;
                scrollback_page_.reset();
                std::lock_guard guard(mutex_);
                batch_needs_attach_ = false;
            }
            if (batch.resync && !batch.attach)
            {
                std::lock_guard guard(mutex_);
                batch_needs_attach_ = true;
            }
            bool changed = false;
            if (!batch.events.empty()
                && !client_->accept_events(batch.events, changed, error))
            {
                publish_error_once(client_error_code(), error);
                std::lock_guard guard(mutex_);
                batch_needs_attach_ = true;
                return true;
            }
            note_connected(recovery_channel());
            if (batch.attach || changed)
            {
                if (changed && scroll_offset_ > 0
                    && !refresh_scrollback_after_output(error))
                {
                    publish_error_once(client_error_code(), error);
                    scroll_offset_ = 0;
                    scrollback_total_ = 0;
                    scrollback_page_.reset();
                }
                if (presentation_visible_)
                    publish_projection();
                else
                {
                    client_->take_grid_update();
                    client_->take_clipboard_write();
                }
            }
            return batch.attach.has_value() || changed
                || suspension_changed || batch.resync
                || !batch.error_code.empty();
        }

        void invalidate_batch_cursor()
        {
            std::lock_guard guard(mutex_);
            batch_needs_attach_ = true;
        }

    private:
        void wake_coordinator()
        {
            if (auto coordinator = coordinator_.lock())
                coordinator->wake_worker();
        }
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

        bool publish_error_once(
            std::string_view error_code, const std::string& error)
        {
            const std::string key = error_code.empty()
                ? error
                : std::string(error_code);
            {
                std::lock_guard guard(mutex_);
                if (!published_error_keys_.insert(key).second)
                    return false;
            }
            publish_error(error);
            return true;
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
        bool batch_needs_attach_ = true;
    };

    explicit Impl(RemoteSessionCoordinatorOptions options)
        : options_(std::move(options))
        , session_poll_supported_(options_.session_poll_supported
              && options_.session_client != nullptr)
        , transport_mode_(options_.session_stream_supported
                  && options_.session_client != nullptr
              ? SessionTransportMode::StreamOpening
              : session_poll_supported_
              ? SessionTransportMode::SessionPoll
              : SessionTransportMode::Legacy)
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
        if (transport_mode_ != SessionTransportMode::Legacy)
        {
            session_worker_ = std::jthread(
                [this](std::stop_token stop_token) {
                    session_worker_main(stop_token);
                });
        }
        return true;
    }

    void stop()
    {
        if (!running_.exchange(false) && stopping_)
            return;
        stopping_ = true;
        close_stream_connection();
        wake_worker();
        if (session_worker_.joinable())
        {
            session_worker_.request_stop();
            session_worker_.join();
        }
        finish_stream_after_worker();
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
            if (!entry->start(
                    transport_mode_ == SessionTransportMode::Legacy))
            {
                entries_.erase(id);
                return 0;
            }
        }
        wake_worker();
        return id;
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
        wake_worker();
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

    void wake_worker()
    {
        {
            std::lock_guard guard(worker_mutex_);
            worker_pending_ = true;
        }
        worker_wake_.notify_one();
    }

private:
    std::vector<std::shared_ptr<Entry>> entries_snapshot() const
    {
        std::vector<std::shared_ptr<Entry>> result;
        {
            std::lock_guard guard(mutex_);
            result.reserve(entries_.size());
            for (const auto& [id, entry] : entries_)
                result.push_back(entry);
        }
        std::ranges::sort(result, {}, [](const auto& entry) {
            return entry->batch_subscription().subscription_id;
        });
        return result;
    }

    void fall_back_to_legacy()
    {
        std::vector<std::shared_ptr<Entry>> entries;
        {
            std::lock_guard guard(mutex_);
            transport_mode_ = SessionTransportMode::Legacy;
            entries.reserve(entries_.size());
            for (const auto& [id, entry] : entries_)
                entries.push_back(entry);
        }
        if (options_.session_client)
            options_.session_client->enable_legacy_polling();
        for (const auto& entry : entries)
        {
            entry->invalidate_batch_cursor();
            entry->start_legacy_worker();
        }
    }

    bool refresh_epoch_bounded()
    {
        const auto identity = options_.recovery->server_identity();
        const auto probe = ServerClient::probe({
            .runtime_directory = options_.runtime_directory,
            .client_id = options_.client_id,
            .connection_token = identity.connection_token,
            .registration_nonce
            = options_.recovery->registration_nonce(),
            .timeout = kSessionPollRequestBudget,
            .request_timeout = kSessionPollRequestBudget,
            .launch_if_missing = false,
        });
        return probe.ready() && probe.welcome
            && options_.recovery->set_server_identity(
                probe.welcome->server_epoch,
                probe.welcome->connection_token);
    }

    bool accept_session_response(SessionPollResponse& response,
        const std::vector<std::shared_ptr<Entry>>& entries,
        std::chrono::microseconds latency,
        std::string_view recovery_channel,
        bool commands_processed)
    {
        options_.recovery->note_connected(recovery_channel);
        bool changed = response.more || commands_processed;
        if (options_.session_client)
        {
            options_.session_client->accept_session_poll_epoch(
                response.server_epoch, recovery_channel);
            if (response.topology.snapshot)
            {
                options_.session_client->accept_session_poll_topology(
                    response.server_epoch,
                    std::move(*response.topology.snapshot),
                    recovery_channel);
                changed = true;
            }
            if (!response.topology.error_code.empty())
            {
                options_.session_client->accept_session_poll_error(
                    "topology", response.topology.error_message,
                    recovery_channel);
            }
            if (response.agents.snapshot)
            {
                options_.session_client->accept_session_poll_agents(
                    response.server_epoch,
                    std::move(*response.agents.snapshot),
                    recovery_channel);
                changed = true;
            }
            if (!response.agents.error_code.empty())
            {
                options_.session_client->accept_session_poll_error(
                    "agents", response.agents.error_message,
                    recovery_channel);
            }
        }
        for (const auto& batch : response.terminals)
        {
            const auto found = std::ranges::find(entries,
                batch.subscription_id,
                [](const auto& entry) {
                    return entry->batch_subscription()
                        .subscription_id;
                });
            if (found != entries.end())
                changed = (*found)->accept_batch(batch, latency)
                    || changed;
        }
        return changed;
    }

    SessionPollRequest make_session_request(
        const std::vector<std::shared_ptr<Entry>>& entries,
        uint64_t request_serial) const
    {
        const auto revisions = options_.session_client
            ? options_.session_client->session_poll_revisions()
            : RemoteSessionPollRevisions{};
        SessionPollRequest request{
            .request_serial = request_serial,
            .server_epoch = options_.recovery->server_epoch(),
            .topology_after_revision = revisions.topology,
            .agent_after_revision = revisions.agents,
        };
        request.terminals.reserve(entries.size());
        for (const auto& entry : entries)
        {
            if (entry->batch_active())
                request.terminals.push_back(entry->batch_subscription());
        }
        return request;
    }

    static bool same_session_state(
        const SessionPollRequest& lhs,
        const SessionPollRequest& rhs)
    {
        return lhs.server_epoch == rhs.server_epoch
            && lhs.topology_after_revision
                == rhs.topology_after_revision
            && lhs.agent_after_revision == rhs.agent_after_revision
            && lhs.terminals == rhs.terminals;
    }

    uint64_t next_session_request_serial()
    {
        const uint64_t result = stream_request_serial_;
        if (++stream_request_serial_ == 0)
            stream_request_serial_ = 1;
        return result;
    }

    void close_stream_connection()
    {
        std::lock_guard guard(stream_connection_mutex_);
        if (stream_connection_)
            stream_connection_->close();
    }

    void finish_stream_after_worker()
    {
        close_stream_connection();
        if (stream_reader_.joinable())
        {
            stream_reader_.request_stop();
            stream_reader_.join();
        }
        {
            std::lock_guard guard(stream_connection_mutex_);
            stream_connection_.reset();
        }
        std::lock_guard guard(worker_mutex_);
        stream_inbox_.clear();
        stream_inbox_bytes_ = 0;
        stream_reader_done_ = false;
        stream_reader_error_ = {};
    }

    bool write_stream_frame(const SessionStreamClientFrame& frame,
        std::stop_token stop_token, std::string& error)
    {
        const std::string bytes
            = session_stream_client_frame_to_json(frame).dump();
        if (bytes.size() > stream_max_frame_bytes_)
        {
            error = "The Session event stream client frame exceeds its negotiated budget.";
            return false;
        }
        AsyncFrameStreamConnection* connection = nullptr;
        {
            std::lock_guard guard(stream_connection_mutex_);
            connection = stream_connection_.get();
        }
        if (!connection)
        {
            error = "The Session event stream is not connected.";
            return false;
        }
        AsyncFrameStreamError transport_error;
        if (!connection->write_frame(bytes, stop_token, transport_error))
        {
            error = transport_error.message.empty()
                ? "The Session event stream write failed."
                : std::move(transport_error.message);
            return false;
        }
        return true;
    }

    void stream_reader_main(std::stop_token stop_token)
    {
        AsyncFrameStreamConnection* connection = nullptr;
        {
            std::lock_guard guard(stream_connection_mutex_);
            connection = stream_connection_.get();
        }
        AsyncFrameStreamError terminal_error;
        while (connection && !stop_token.stop_requested()
            && !stopping_)
        {
            std::string bytes;
            AsyncFrameStreamError read_error;
            if (!connection->read_frame(bytes, stop_token, read_error))
            {
                terminal_error = std::move(read_error);
                break;
            }
            bool accepted = false;
            {
                std::lock_guard guard(worker_mutex_);
                if (bytes.size() <= stream_max_frame_bytes_
                    && stream_inbox_.size()
                        < kSessionStreamInboxFrameLimit
                    && bytes.size()
                        <= stream_max_queue_bytes_
                            - std::min(stream_inbox_bytes_,
                                stream_max_queue_bytes_))
                {
                    stream_inbox_bytes_ += bytes.size();
                    stream_inbox_.push_back(std::move(bytes));
                    worker_pending_ = true;
                    accepted = true;
                }
                else
                {
                    terminal_error = {
                        .code = "backpressure",
                        .message = "The Session event stream client inbox exceeded its negotiated budget.",
                    };
                }
            }
            worker_wake_.notify_one();
            if (!accepted)
            {
                connection->close();
                break;
            }
        }
        {
            std::lock_guard guard(worker_mutex_);
            stream_reader_done_ = true;
            stream_reader_error_ = std::move(terminal_error);
            worker_pending_ = true;
        }
        worker_wake_.notify_one();
    }

    bool begin_stream(std::stop_token stop_token)
    {
        const auto entries = entries_snapshot();
        SessionPollRequest initial = make_session_request(
            entries, next_session_request_serial());
        ServerControlChannel channel({
            .runtime_directory = options_.runtime_directory,
            .client_id = options_.client_id,
            .session_id = options_.session_id,
            .recovery = options_.recovery,
        });
        const auto opened = channel.request("session.stream.open",
            session_stream_open_request_to_json({
                .server_epoch = initial.server_epoch,
                .session_id = options_.session_id,
                .poll = initial,
            }),
            kSessionStreamConnectBudget);
        if (!opened.ok)
        {
            stream_failure_code_ = opened.error_code;
            stream_failure_message_ = opened.error_message;
            return false;
        }
        std::string parse_error;
        auto response = session_stream_open_response_from_json(
            opened.result, parse_error);
        if (!response)
        {
            stream_failure_code_ = "invalid_session_stream";
            stream_failure_message_ = std::move(parse_error);
            return false;
        }
        if (response->server_epoch != initial.server_epoch)
        {
            stream_failure_code_ = "stale_epoch";
            stream_failure_message_
                = "The Session stream opened for a different server epoch.";
            return false;
        }
        AsyncFrameStreamError connect_error;
        auto connection = AsyncFrameStreamClient::connect(
            response->endpoint, kSessionStreamConnectBudget,
            connect_error);
        if (!connection)
        {
            stream_failure_code_ = connect_error.code;
            stream_failure_message_ = connect_error.message;
            return false;
        }
        {
            std::lock_guard guard(stream_connection_mutex_);
            if (stopping_ || stop_token.stop_requested())
                return false;
            stream_connection_ = std::move(connection);
        }
        stream_max_frame_bytes_ = std::min(
            response->max_frame_bytes, kControlMaxMessageBytes);
        stream_max_queue_bytes_ = response->max_queue_bytes;
        stream_heartbeat_timeout_ = std::chrono::milliseconds(
            std::clamp<uint64_t>(
                static_cast<uint64_t>(response->heartbeat_interval_ms)
                    * 3,
                250, 60'000));
        std::string write_error;
        if (!write_stream_frame({
                .kind = SessionStreamClientFrameKind::Connect,
                .connect = SessionStreamConnectRequest{
                    .server_epoch = response->server_epoch,
                    .ticket = std::move(response->ticket),
                },
            },
                stop_token, write_error))
        {
            stream_failure_code_ = "io_error";
            stream_failure_message_ = std::move(write_error);
            finish_stream_after_worker();
            return false;
        }
        last_stream_poll_ = std::move(initial);
        stream_update_required_ = false;
        last_stream_frame_serial_ = 0;
        last_stream_event_request_serial_ = 0;
        stream_last_frame_at_ = std::chrono::steady_clock::now();
        {
            std::lock_guard guard(worker_mutex_);
            stream_reader_done_ = false;
            stream_reader_error_ = {};
        }
        stream_reader_ = std::jthread(
            [this](std::stop_token reader_stop) {
                stream_reader_main(reader_stop);
            });
        transport_mode_ = SessionTransportMode::StreamActive;
        return true;
    }

    void invalidate_stream_cursors_after_epoch_change()
    {
        refresh_epoch_bounded();
        const auto entries = entries_snapshot();
        for (const auto& entry : entries)
            entry->invalidate_batch_cursor();
        if (options_.session_client)
        {
            options_.session_client->invalidate_session_poll_cursors(
                options_.recovery->server_epoch(), "session.stream");
        }
    }

    bool fall_back_from_stream()
    {
        if (stopping_)
            return false;
        options_.recovery->note_failure("session.stream");
        if (needs_identity_refresh(stream_failure_code_))
            invalidate_stream_cursors_after_epoch_change();
        finish_stream_after_worker();
        last_stream_poll_.reset();
        if (session_poll_supported_)
        {
            transport_mode_ = SessionTransportMode::SessionPoll;
            return true;
        }
        fall_back_to_legacy();
        return false;
    }

    bool accept_stream_frames(
        const std::vector<std::shared_ptr<Entry>>& entries)
    {
        std::deque<std::string> frames;
        bool reader_done = false;
        AsyncFrameStreamError reader_error;
        {
            std::lock_guard guard(worker_mutex_);
            frames.swap(stream_inbox_);
            stream_inbox_bytes_ = 0;
            reader_done = stream_reader_done_;
            reader_error = stream_reader_error_;
        }
        for (auto& bytes : frames)
        {
            auto encoded = nlohmann::json::parse(
                bytes, nullptr, false);
            std::string parse_error;
            auto frame = encoded.is_discarded()
                ? std::nullopt
                : session_stream_server_frame_from_json(
                    encoded, parse_error);
            if (!frame)
            {
                stream_failure_code_ = "invalid_session_stream";
                stream_failure_message_ = parse_error.empty()
                    ? "The Session event stream frame is not valid JSON."
                    : std::move(parse_error);
                return false;
            }
            if (frame->server_epoch
                    != options_.recovery->server_epoch()
                || frame->frame_serial
                    != last_stream_frame_serial_ + 1)
            {
                stream_failure_code_ = frame->server_epoch
                        != options_.recovery->server_epoch()
                    ? "stale_epoch"
                    : "invalid_session_stream";
                stream_failure_message_
                    = "The Session event stream identity or ordering changed.";
                return false;
            }
            last_stream_frame_serial_ = frame->frame_serial;
            stream_last_frame_at_ = std::chrono::steady_clock::now();
            if (frame->kind == SessionStreamServerFrameKind::Error)
            {
                stream_failure_code_ = frame->error_code;
                stream_failure_message_ = frame->error_message;
                return false;
            }
            if (frame->kind == SessionStreamServerFrameKind::Events)
            {
                if (!frame->events || !last_stream_poll_
                    || frame->events->server_epoch
                        != frame->server_epoch
                    || frame->events->request_serial
                        > last_stream_poll_->request_serial
                    || frame->events->request_serial
                        <= last_stream_event_request_serial_)
                {
                    stream_failure_code_ = "invalid_session_stream";
                    stream_failure_message_
                        = "The Session event batch does not match the active stream state.";
                    return false;
                }
                accept_session_response(*frame->events, entries,
                    std::chrono::microseconds::zero(),
                    "session.stream", false);
                last_stream_event_request_serial_
                    = frame->events->request_serial;
                // Every Events frame is flow-controlled by the server, even
                // if it contains only a deferred/error channel and advances
                // no cursor. A fresh Update is therefore also its ack.
                stream_update_required_ = true;
            }
            else
            {
                options_.recovery->note_connected("session.stream");
            }
        }
        if (reader_done)
        {
            stream_failure_code_ = reader_error.code.empty()
                ? "closed"
                : std::move(reader_error.code);
            stream_failure_message_ = reader_error.message.empty()
                ? "The Session event stream closed."
                : std::move(reader_error.message);
            return false;
        }
        return true;
    }

    bool update_stream(std::stop_token stop_token)
    {
        const auto entries = entries_snapshot();
        SessionPollRequest current = make_session_request(
            entries, stream_request_serial_);
        if (!stream_update_required_ && last_stream_poll_
            && same_session_state(current, *last_stream_poll_))
        {
            return true;
        }
        current.request_serial = next_session_request_serial();
        std::string error;
        if (!write_stream_frame({
                .kind = SessionStreamClientFrameKind::Update,
                .update = SessionStreamUpdate{ .poll = current },
            },
                stop_token, error))
        {
            stream_failure_code_ = "io_error";
            stream_failure_message_ = std::move(error);
            return false;
        }
        last_stream_poll_ = std::move(current);
        stream_update_required_ = false;
        return true;
    }

    void wait_for_worker(std::optional<std::chrono::milliseconds> timeout)
    {
        std::unique_lock lock(worker_mutex_);
        if (!worker_pending_ && !stopping_)
        {
            if (timeout)
            {
                worker_wake_.wait_for(lock, *timeout, [this] {
                    return worker_pending_ || stopping_.load();
                });
            }
            else
            {
                worker_wake_.wait(lock, [this] {
                    return worker_pending_ || stopping_.load();
                });
            }
        }
        worker_pending_ = false;
    }

    void stream_worker_main(std::stop_token stop_token)
    {
        while (!stopping_ && !stop_token.stop_requested()
            && transport_mode_ == SessionTransportMode::StreamActive)
        {
            const auto entries = entries_snapshot();
            bool commands_processed = false;
            for (const auto& entry : entries)
            {
                commands_processed
                    = entry->process_batch_commands()
                    || commands_processed;
            }
            if (!accept_stream_frames(entries)
                || !update_stream(stop_token))
            {
                fall_back_from_stream();
                return;
            }
            const auto elapsed = std::chrono::steady_clock::now()
                - stream_last_frame_at_;
            if (elapsed >= stream_heartbeat_timeout_)
            {
                stream_failure_code_ = "deadline_exceeded";
                stream_failure_message_
                    = "The Session event stream heartbeat timed out.";
                fall_back_from_stream();
                return;
            }
            auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(
                stream_heartbeat_timeout_ - elapsed);
            if (commands_processed)
                wait = std::min(wait, kPollInterval);
            wait_for_worker(wait);
        }
    }

    void session_worker_main(std::stop_token stop_token)
    {
        if (transport_mode_ == SessionTransportMode::StreamOpening)
        {
            if (!begin_stream(stop_token))
            {
                if (!stopping_ && !stop_token.stop_requested())
                    fall_back_from_stream();
            }
        }
        if (transport_mode_ == SessionTransportMode::StreamActive)
            stream_worker_main(stop_token);
        if (transport_mode_ == SessionTransportMode::SessionPoll
            && !stopping_ && !stop_token.stop_requested())
        {
            poll_worker_main(stop_token);
        }
    }

    void poll_worker_main(std::stop_token stop_token)
    {
        ServerControlChannel channel({
            .runtime_directory = options_.runtime_directory,
            .client_id = options_.client_id,
            .session_id = options_.session_id,
            .recovery = options_.recovery,
        });
        auto interval = kPollInterval;
        size_t idle_polls = 0;
        uint64_t request_serial = 1;
        while (!stopping_
            && !stop_token.stop_requested()
            && transport_mode_ == SessionTransportMode::SessionPoll)
        {
            const auto entries = entries_snapshot();
            bool commands_processed = false;
            for (const auto& entry : entries)
                commands_processed
                    = entry->process_batch_commands()
                    || commands_processed;
            if (stopping_)
                break;

            const auto revisions = options_.session_client
                ? options_.session_client->session_poll_revisions()
                : RemoteSessionPollRevisions{};
            SessionPollRequest request{
                .request_serial = request_serial,
                .server_epoch = options_.recovery->server_epoch(),
                .topology_after_revision = revisions.topology,
                .agent_after_revision = revisions.agents,
            };
            request.terminals.reserve(entries.size());
            for (const auto& entry : entries)
            {
                if (entry->batch_active())
                {
                    request.terminals.push_back(
                        entry->batch_subscription());
                }
            }

            const auto started_at = std::chrono::steady_clock::now();
            const auto result = channel.request("session.poll",
                session_poll_request_to_json(request),
                kSessionPollRequestBudget);
            if (stopping_)
                break;
            if (!result.ok)
            {
                if (result.error_code == "unknown_method")
                {
                    fall_back_to_legacy();
                    break;
                }
                if (needs_identity_refresh(result.error_code))
                {
                    refresh_epoch_bounded();
                    for (const auto& entry : entries)
                        entry->invalidate_batch_cursor();
                    if (options_.session_client)
                    {
                        options_.session_client->invalidate_session_poll_cursors(
                            options_.recovery->server_epoch());
                    }
                }
                const auto delay = options_.recovery->note_failure(
                    "session.poll");
                if (options_.session_client)
                {
                    options_.session_client->accept_session_poll_error(
                        "topology", result.error_message);
                    options_.session_client->accept_session_poll_error(
                        "agents", result.error_message);
                }
                std::unique_lock lock(worker_mutex_);
                worker_wake_.wait_for(lock, delay);
                continue;
            }

            std::string parse_error;
            auto response = session_poll_response_from_json(
                result.result, parse_error);
            if (response
                && response->request_serial != request.request_serial)
            {
                parse_error
                    = "Session poll response serial does not match its request.";
                response.reset();
            }
            if (++request_serial == 0)
                request_serial = 1;
            if (!response)
            {
                if (options_.session_client)
                {
                    options_.session_client->accept_session_poll_error(
                        "topology", parse_error);
                    options_.session_client->accept_session_poll_error(
                        "agents", parse_error);
                }
                interval = kIdlePollInterval;
                idle_polls = 2;
            }
            else
            {
                const auto latency
                    = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - started_at);
                const bool changed = accept_session_response(
                    *response, entries, latency,
                    "session.poll", commands_processed);
                if (changed)
                {
                    idle_polls = 0;
                    interval = kPollInterval;
                }
                else if (idle_polls++ == 0)
                {
                    interval = kIntermediatePollInterval;
                }
                else
                {
                    interval = kIdlePollInterval;
                }
                if (response->more)
                    continue;
            }
            std::unique_lock lock(worker_mutex_);
            worker_wake_.wait_for(lock, interval);
        }
    }

    RemoteSessionCoordinatorOptions options_;
    const bool session_poll_supported_ = false;
    std::atomic<bool> running_ = false;
    std::atomic<bool> stopping_ = true;
    mutable std::mutex mutex_;
    std::unordered_map<uint64_t, std::shared_ptr<Entry>> entries_;
    std::unordered_map<uint64_t, uint64_t> ready_;
    uint64_t next_registration_id_ = 1;
    bool wake_pending_ = false;
    std::atomic<SessionTransportMode> transport_mode_
        = SessionTransportMode::Legacy;
    std::jthread session_worker_;
    std::mutex worker_mutex_;
    std::condition_variable worker_wake_;
    bool worker_pending_ = false;
    std::mutex stream_connection_mutex_;
    std::unique_ptr<AsyncFrameStreamConnection> stream_connection_;
    std::jthread stream_reader_;
    std::deque<std::string> stream_inbox_;
    size_t stream_inbox_bytes_ = 0;
    size_t stream_max_frame_bytes_ = kControlMaxMessageBytes;
    size_t stream_max_queue_bytes_ = kSessionStreamDefaultQueueBytes;
    bool stream_reader_done_ = false;
    AsyncFrameStreamError stream_reader_error_;
    uint64_t stream_request_serial_ = 1;
    uint64_t last_stream_frame_serial_ = 0;
    uint64_t last_stream_event_request_serial_ = 0;
    std::optional<SessionPollRequest> last_stream_poll_;
    bool stream_update_required_ = false;
    std::chrono::milliseconds stream_heartbeat_timeout_{
        kSessionStreamDefaultHeartbeatIntervalMs * 3
    };
    std::chrono::steady_clock::time_point stream_last_frame_at_{};
    std::string stream_failure_code_;
    std::string stream_failure_message_;
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
