#include "session_stream_service.h"

#include <draxul/async_frame_stream.h>
#include <draxul/server_protocol.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace draxul
{
namespace
{

constexpr auto kTicketLifetime = std::chrono::seconds(10);
constexpr auto kConnectFrameTimeout = std::chrono::seconds(5);
constexpr size_t kFrameEnvelopeReserve = 128 * 1024;
constexpr size_t kFrameSerialReserve = 32;
constexpr size_t kCommandsPerPump = 8;

enum class OutboundPriority
{
    Control,
    Events,
};

std::string random_ticket()
{
    std::random_device source;
    static constexpr char hex[] = "0123456789abcdef";
    std::string ticket(64, '0');
    for (char& value : ticket)
        value = hex[source() & 0xfu];
    return ticket;
}

bool has_events(const SessionPollResponse& response)
{
    return response.topology.snapshot.has_value()
        || response.topology.deferred
        || !response.topology.error_code.empty()
        || response.agents.snapshot.has_value()
        || response.agents.deferred
        || !response.agents.error_code.empty()
        || !response.terminals.empty() || response.more;
}

std::string encode_frame(const SessionStreamServerFrame& frame)
{
    return session_stream_server_frame_to_json(frame).dump(
        -1, ' ', false, nlohmann::detail::error_handler_t::replace);
}

} // namespace

class SessionStreamService::Impl
{
public:
    struct Ticket
    {
        std::string client_id;
        std::string session_id;
        SessionPollRequest poll;
        std::chrono::steady_clock::time_point expires_at;
    };

    struct ConnectionCore
    {
        struct PendingCommand
        {
            SessionStreamCommand command;
            size_t bytes = 0;
        };

        struct QueuedFrame
        {
            std::string encoded;
            size_t bytes = 0;
            OutboundPriority priority = OutboundPriority::Events;
        };

        explicit ConnectionCore(
            std::unique_ptr<AsyncFrameStreamConnection> value,
            uint64_t id_value)
            : stream(std::move(value))
            , id(id_value)
        {
        }

        std::unique_ptr<AsyncFrameStreamConnection> stream;
        uint64_t id = 0;
        std::mutex mutex;
        std::condition_variable outbound_ready;
        std::deque<QueuedFrame> control_outbound;
        std::deque<QueuedFrame> event_outbound;
        std::deque<PendingCommand> commands;
        size_t queued_bytes = 0;
        size_t queued_control_bytes = 0;
        size_t queued_event_bytes = 0;
        size_t peak_control_bytes = 0;
        size_t peak_event_bytes = 0;
        size_t command_bytes = 0;
        bool authenticated = false;
        bool awaiting_update = false;
        bool closed = false;
        bool close_after_flush = false;
        std::string client_id;
        std::string session_id;
        SessionPollRequest poll;
        uint64_t next_frame_serial = 1;
        std::chrono::steady_clock::time_point accepted_at
            = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point last_enqueued_at
            = accepted_at;
    };

    struct Connection
    {
        std::shared_ptr<ConnectionCore> core;
        std::jthread reader;
        std::jthread writer;
    };

    struct CompletedCommand
    {
        std::string fingerprint;
        SessionStreamCommandResult result;
        size_t bytes = 0;
    };

    explicit Impl(SessionStreamServiceOptions value)
        : options(std::move(value))
    {
        options.heartbeat_interval = std::clamp(options.heartbeat_interval,
            std::chrono::milliseconds(kSessionStreamMinHeartbeatIntervalMs),
            std::chrono::milliseconds(kSessionStreamMaxHeartbeatIntervalMs));
        options.max_queue_bytes = std::clamp<size_t>(
            options.max_queue_bytes, kSessionStreamMinQueueBytes,
            kSessionStreamMaxQueueBytes);
    }

    ~Impl() = default;

    void wake() const
    {
        if (options.wake_state_thread)
            options.wake_state_thread();
    }

    size_t control_reserve_bytes() const
    {
        return std::clamp<size_t>(options.max_queue_bytes / 16,
            kSessionStreamMinControlReserveBytes,
            std::min(options.max_queue_bytes,
                kSessionStreamMaxControlReserveBytes));
    }

    void close_core(const std::shared_ptr<ConnectionCore>& core)
    {
        {
            std::lock_guard guard(core->mutex);
            if (core->closed)
                return;
            core->closed = true;
        }
        core->outbound_ready.notify_all();
        core->stream->close();
    }

    bool enqueue(const std::shared_ptr<ConnectionCore>& core,
        SessionStreamServerFrame frame, OutboundPriority priority)
    {
        // frame_serial is assigned only when the writer selects the next
        // priority queue entry. Reserving its maximum encoded width keeps
        // byte accounting conservative without numbering frames before a
        // high-priority result can overtake queued presentation output.
        std::string encoded = encode_frame(frame);
        const size_t frame_bytes = encoded.size() + kFrameSerialReserve;
        std::lock_guard guard(core->mutex);
        if (core->closed)
            return false;
        const size_t byte_limit = std::min(
            priority == OutboundPriority::Events
                ? options.max_queue_bytes - control_reserve_bytes()
                : options.max_queue_bytes,
            kSessionStreamMaxFrameBytes);
        const bool control_cardinality_full
            = priority == OutboundPriority::Control
            && core->control_outbound.size()
                >= kSessionStreamMaxPendingCommands;
        if (control_cardinality_full || frame_bytes > byte_limit
            || core->queued_bytes
                    > byte_limit - frame_bytes)
        {
            core->closed = true;
            core->outbound_ready.notify_all();
            core->stream->close();
            return false;
        }
        core->queued_bytes += frame_bytes;
        if (priority == OutboundPriority::Control)
        {
            core->queued_control_bytes += frame_bytes;
            core->peak_control_bytes = std::max(
                core->peak_control_bytes, core->queued_control_bytes);
            core->control_outbound.push_back({
                .encoded = std::move(encoded),
                .bytes = frame_bytes,
                .priority = priority,
            });
        }
        else
        {
            core->queued_event_bytes += frame_bytes;
            core->peak_event_bytes = std::max(
                core->peak_event_bytes, core->queued_event_bytes);
            core->event_outbound.push_back({
                .encoded = std::move(encoded),
                .bytes = frame_bytes,
                .priority = priority,
            });
        }
        core->last_enqueued_at = std::chrono::steady_clock::now();
        core->outbound_ready.notify_one();
        return true;
    }

    std::optional<Ticket> consume_ticket(
        std::string_view ticket, std::string_view epoch)
    {
        std::lock_guard guard(mutex);
        const auto found = tickets.find(std::string(ticket));
        if (found == tickets.end())
            return std::nullopt;
        Ticket binding = std::move(found->second);
        tickets.erase(found);
        if (epoch != options.server_epoch
            || binding.expires_at < std::chrono::steady_clock::now())
            return std::nullopt;
        return binding;
    }

    static std::string command_key(std::string_view client_id,
        std::string_view session_id, uint64_t request_id)
    {
        return std::string(client_id) + '\n' + std::string(session_id)
            + '\n' + std::to_string(request_id);
    }

    static std::string command_fingerprint(
        const SessionStreamCommand& command)
    {
        return command.method + '\n'
            + command.params.dump(-1, ' ', false,
                nlohmann::detail::error_handler_t::replace);
    }

    SessionStreamCommandResult execute_command(
        std::string_view session_id, std::string_view client_id,
        const SessionStreamCommand& command,
        const SessionStreamService::Dispatch& dispatch)
    {
        const std::string key = command_key(
            client_id, session_id, command.request_id);
        const std::string fingerprint = command_fingerprint(command);
        if (const auto found = completed_commands.find(key);
            found != completed_commands.end())
        {
            if (found->second.fingerprint != fingerprint)
            {
                ++command_conflicts;
                ++command_rejections;
                return {
                    .request_id = command.request_id,
                    .error_code = "request_id_conflict",
                    .error_message = "The Session stream request ID was reused with a different command.",
                };
            }
            ++command_replays;
            SessionStreamCommandResult replay = found->second.result;
            replay.replayed = true;
            return replay;
        }

        SessionStreamCommandResult result{
            .request_id = command.request_id,
        };
        if (!dispatch)
        {
            result.error_code = "command_unavailable";
            result.error_message
                = "Session stream commands are unavailable.";
        }
        else
        {
            ++commands_dispatched;
            ControlMethodResult dispatched = dispatch(
                session_id, client_id, command);
            result.ok = dispatched.ok;
            if (dispatched.ok)
                result.result = std::move(dispatched.value);
            else
            {
                result.error_code = std::move(dispatched.error_code);
                result.error_message = std::move(dispatched.error_message);
            }
        }

        const size_t reserve = control_reserve_bytes();
        const size_t encoded_size = session_stream_command_result_to_json(
            result).dump(-1, ' ', false,
                nlohmann::detail::error_handler_t::replace).size();
        if (encoded_size > reserve - std::min<size_t>(reserve, 1024))
        {
            result = {
                .request_id = command.request_id,
                .error_code = "command_result_too_large",
                .error_message = "The command result exceeds the reserved stream control capacity; retry it on the short control endpoint.",
            };
        }
        if (!result.ok)
            ++command_rejections;

        const size_t cached_bytes = key.size() + fingerprint.size()
            + session_stream_command_result_to_json(result)
                  .dump(-1, ' ', false,
                      nlohmann::detail::error_handler_t::replace)
                  .size();
        completed_command_order.push_back(key);
        completed_commands.emplace(key, CompletedCommand{
            .fingerprint = fingerprint,
            .result = result,
            .bytes = cached_bytes,
        });
        completed_command_bytes += cached_bytes;
        while (completed_command_order.size()
                > kSessionStreamMaxCompletedCommands
            || completed_command_bytes
                > kSessionStreamMaxCompletedCommandBytes)
        {
            const auto oldest = completed_commands.find(
                completed_command_order.front());
            if (oldest != completed_commands.end())
            {
                completed_command_bytes -= std::min(
                    completed_command_bytes, oldest->second.bytes);
                completed_commands.erase(oldest);
            }
            completed_command_order.pop_front();
        }
        return result;
    }

    void reader_loop(const std::shared_ptr<ConnectionCore>& core,
        std::stop_token stop_token)
    {
        bool first = true;
        while (!stop_token.stop_requested())
        {
            std::string bytes;
            AsyncFrameStreamError io_error;
            if (!core->stream->read_frame(bytes, stop_token, io_error))
                break;
            const auto encoded
                = nlohmann::json::parse(bytes, nullptr, false);
            std::string parse_error;
            auto frame = session_stream_client_frame_from_json(
                encoded, parse_error);
            if (!frame)
                break;
            if (first)
            {
                first = false;
                if (frame->kind != SessionStreamClientFrameKind::Connect
                    || !frame->connect)
                    break;
                auto ticket = consume_ticket(
                    frame->connect->ticket, frame->connect->server_epoch);
                if (!ticket)
                    break;
                {
                    std::lock_guard guard(core->mutex);
                    core->authenticated = true;
                    core->client_id = std::move(ticket->client_id);
                    core->session_id = std::move(ticket->session_id);
                    core->poll = std::move(ticket->poll);
                    core->awaiting_update = false;
                }
                wake();
                continue;
            }
            if (frame->kind == SessionStreamClientFrameKind::Close)
                break;
            if (frame->kind == SessionStreamClientFrameKind::Command
                && frame->command)
            {
                if (frame->command->server_epoch != options.server_epoch)
                    break;
                bool full = false;
                {
                    std::lock_guard guard(core->mutex);
                    const size_t command_limit = std::min<size_t>(
                        options.max_queue_bytes / 2,
                        kSessionStreamMaxControlReserveBytes);
                    full = core->closed
                        || core->commands.size()
                            >= kSessionStreamMaxPendingCommands
                        || bytes.size() > command_limit
                        || core->command_bytes
                            > command_limit - bytes.size();
                    if (!full)
                    {
                        core->command_bytes += bytes.size();
                        core->commands.push_back({
                            .command = std::move(*frame->command),
                            .bytes = bytes.size(),
                        });
                    }
                }
                if (full)
                    break;
                wake();
                continue;
            }
            if (frame->kind != SessionStreamClientFrameKind::Update
                || !frame->update
                || frame->update->poll.server_epoch != options.server_epoch)
                break;
            bool stale_update = false;
            {
                std::lock_guard guard(core->mutex);
                if (core->closed)
                    break;
                if (frame->update->poll.request_serial
                    <= core->poll.request_serial)
                {
                    stale_update = true;
                }
                else
                {
                    core->poll = std::move(frame->update->poll);
                    core->awaiting_update = false;
                }
            }
            if (stale_update)
                break;
            wake();
        }
        close_core(core);
        wake();
    }

    void writer_loop(const std::shared_ptr<ConnectionCore>& core,
        std::stop_token stop_token)
    {
        while (!stop_token.stop_requested())
        {
            ConnectionCore::QueuedFrame queued;
            uint64_t frame_serial = 0;
            {
                std::unique_lock lock(core->mutex);
                core->outbound_ready.wait(lock, [&] {
                    return core->closed
                        || !core->control_outbound.empty()
                        || !core->event_outbound.empty()
                        || stop_token.stop_requested();
                });
                if ((core->closed && core->control_outbound.empty()
                        && core->event_outbound.empty())
                    || stop_token.stop_requested())
                    break;
                if (!core->control_outbound.empty())
                {
                    queued = std::move(core->control_outbound.front());
                    core->control_outbound.pop_front();
                }
                else
                {
                    queued = std::move(core->event_outbound.front());
                    core->event_outbound.pop_front();
                }
                frame_serial = core->next_frame_serial++;
            }
            auto encoded = nlohmann::json::parse(
                queued.encoded, nullptr, false);
            if (encoded.is_discarded())
                break;
            encoded["frame_serial"] = frame_serial;
            queued.encoded = encoded.dump(-1, ' ', false,
                nlohmann::detail::error_handler_t::replace);
            AsyncFrameStreamError error;
            const bool written = core->stream->write_frame(
                queued.encoded, stop_token, error);
            {
                std::lock_guard guard(core->mutex);
                core->queued_bytes -= std::min(
                    core->queued_bytes, queued.bytes);
                if (queued.priority == OutboundPriority::Control)
                {
                    core->queued_control_bytes -= std::min(
                        core->queued_control_bytes, queued.bytes);
                }
                else
                {
                    core->queued_event_bytes -= std::min(
                        core->queued_event_bytes, queued.bytes);
                }
            }
            if (!written)
                break;
            bool close_after_flush = false;
            {
                std::lock_guard guard(core->mutex);
                close_after_flush = core->close_after_flush
                    && core->control_outbound.empty()
                    && core->event_outbound.empty();
            }
            if (close_after_flush)
                break;
        }
        close_core(core);
        wake();
    }

    void adopt_connections()
    {
        std::deque<std::unique_ptr<AsyncFrameStreamConnection>> ready;
        {
            std::lock_guard guard(mutex);
            ready.swap(accepted);
        }
        while (!ready.empty())
        {
            auto core = std::make_shared<ConnectionCore>(
                std::move(ready.front()), next_connection_id++);
            ready.pop_front();
            auto connection = std::make_unique<Connection>();
            connection->core = core;
            connection->reader = std::jthread(
                [this, core](std::stop_token token) {
                    reader_loop(core, token);
                });
            connection->writer = std::jthread(
                [this, core](std::stop_token token) {
                    writer_loop(core, token);
                });
            connections.push_back(std::move(connection));
        }
    }

    void retire_connections()
    {
        const auto now = std::chrono::steady_clock::now();
        for (auto& connection : connections)
        {
            std::lock_guard guard(connection->core->mutex);
            if (!connection->core->authenticated
                && now - connection->core->accepted_at
                    >= kConnectFrameTimeout)
            {
                connection->core->closed = true;
                connection->core->stream->close();
                connection->core->outbound_ready.notify_all();
            }
        }
        for (auto it = connections.begin(); it != connections.end();)
        {
            bool closed = false;
            {
                std::lock_guard guard((*it)->core->mutex);
                closed = (*it)->core->closed;
            }
            if (!closed)
            {
                ++it;
                continue;
            }
            {
                std::lock_guard guard(reaper_mutex);
                retired.push_back(std::move(*it));
            }
            reaper_ready.notify_one();
            it = connections.erase(it);
        }
    }

    SessionStreamServiceOptions options;
    AsyncFrameStreamListener listener;
    std::jthread acceptor;
    mutable std::mutex mutex;
    std::deque<std::unique_ptr<AsyncFrameStreamConnection>> accepted;
    std::unordered_map<std::string, Ticket> tickets;
    std::unordered_map<std::string, CompletedCommand> completed_commands;
    std::deque<std::string> completed_command_order;
    size_t completed_command_bytes = 0;
    std::vector<std::unique_ptr<Connection>> connections;
    std::jthread reaper;
    std::mutex reaper_mutex;
    std::condition_variable reaper_ready;
    std::deque<std::unique_ptr<Connection>> retired;
    std::atomic<size_t> raw_connection_count = 0;
    uint64_t commands_dispatched = 0;
    uint64_t command_replays = 0;
    uint64_t command_conflicts = 0;
    uint64_t command_rejections = 0;
    uint64_t next_connection_id = 1;
};

SessionStreamService::SessionStreamService(
    SessionStreamServiceOptions options)
    : impl_(std::make_unique<Impl>(std::move(options)))
{
}

SessionStreamService::~SessionStreamService()
{
    stop();
}

bool SessionStreamService::start(std::string& error)
{
    AsyncFrameStreamError transport_error;
    if (!impl_->listener.start("server-" + impl_->options.server_epoch,
            impl_->options.runtime_directory, transport_error))
    {
        error = transport_error.message;
        return false;
    }
    impl_->reaper = std::jthread([this](std::stop_token stop_token) {
        for (;;)
        {
            std::unique_ptr<Impl::Connection> retired;
            {
                std::unique_lock lock(impl_->reaper_mutex);
                impl_->reaper_ready.wait(lock, [&] {
                    return stop_token.stop_requested()
                        || !impl_->retired.empty();
                });
                if (impl_->retired.empty())
                {
                    if (stop_token.stop_requested())
                        break;
                    continue;
                }
                retired = std::move(impl_->retired.front());
                impl_->retired.pop_front();
            }
            retired->reader.request_stop();
            retired->writer.request_stop();
            impl_->close_core(retired->core);
            retired.reset();
            impl_->raw_connection_count.fetch_sub(
                1, std::memory_order_relaxed);
        }
    });
    impl_->acceptor = std::jthread([this](std::stop_token stop_token) {
        while (!stop_token.stop_requested())
        {
            AsyncFrameStreamError error;
            auto connection = impl_->listener.accept(stop_token, error);
            if (!connection)
            {
                if (error.code == "cancelled")
                    break;
                continue;
            }
            if (impl_->raw_connection_count.fetch_add(
                    1, std::memory_order_relaxed)
                >= kServerMaxConnectedClients)
            {
                impl_->raw_connection_count.fetch_sub(
                    1, std::memory_order_relaxed);
                connection->close();
                continue;
            }
            {
                std::lock_guard guard(impl_->mutex);
                impl_->accepted.push_back(std::move(connection));
            }
            impl_->wake();
        }
    });
    error.clear();
    return true;
}

void SessionStreamService::stop()
{
    impl_->listener.stop();
    if (impl_->acceptor.joinable())
    {
        impl_->acceptor.request_stop();
        impl_->acceptor.join();
    }
    for (auto& connection : impl_->connections)
    {
        connection->reader.request_stop();
        connection->writer.request_stop();
        impl_->close_core(connection->core);
    }
    {
        std::lock_guard guard(impl_->reaper_mutex);
        for (auto& connection : impl_->connections)
            impl_->retired.push_back(std::move(connection));
    }
    impl_->connections.clear();
    impl_->reaper_ready.notify_all();
    if (impl_->reaper.joinable())
    {
        impl_->reaper.request_stop();
        impl_->reaper_ready.notify_all();
        impl_->reaper.join();
    }
    {
        std::lock_guard guard(impl_->mutex);
        for (auto& connection : impl_->accepted)
        {
            connection->close();
            impl_->raw_connection_count.fetch_sub(
                1, std::memory_order_relaxed);
        }
        impl_->accepted.clear();
        impl_->tickets.clear();
    }
}

ControlMethodResult SessionStreamService::open(
    const nlohmann::json& params,
    std::string_view authenticated_client_id)
{
    if (authenticated_client_id.empty())
    {
        return ControlMethodResult::error("invalid_client",
            "Session streaming requires an authenticated client identity.");
    }
    std::string parse_error;
    auto request = session_stream_open_request_from_json(params, parse_error);
    if (!request)
        return ControlMethodResult::error("invalid_params", parse_error);
    if (request->server_epoch != impl_->options.server_epoch)
    {
        return ControlMethodResult::error("stale_epoch",
            "The Session stream targets an old server epoch.");
    }
    std::string ticket;
    {
        std::lock_guard guard(impl_->mutex);
        for (auto it = impl_->tickets.begin(); it != impl_->tickets.end();)
        {
            if (it->second.client_id == authenticated_client_id
                && it->second.session_id == request->session_id)
                it = impl_->tickets.erase(it);
            else
                ++it;
        }
        if (impl_->tickets.size() >= kServerMaxConnectedClients)
        {
            return ControlMethodResult::error(
                "stream_limit_reached",
                "The Session stream ticket limit has been reached.");
        }
        do
        {
            ticket = random_ticket();
        } while (impl_->tickets.contains(ticket));
        impl_->tickets.emplace(ticket, Impl::Ticket{
            .client_id = std::string(authenticated_client_id),
            .session_id = request->session_id,
            .poll = std::move(request->poll),
            .expires_at = std::chrono::steady_clock::now()
                + kTicketLifetime,
        });
    }
    for (auto& connection : impl_->connections)
    {
        bool replaces = false;
        {
            std::lock_guard guard(connection->core->mutex);
            replaces = connection->core->authenticated
                && connection->core->client_id == authenticated_client_id
                && connection->core->session_id == request->session_id;
        }
        if (replaces)
            impl_->close_core(connection->core);
    }
    return ControlMethodResult::success(
        session_stream_open_response_to_json({
            .server_epoch = impl_->options.server_epoch,
            .endpoint = impl_->listener.endpoint(),
            .ticket = std::move(ticket),
            .heartbeat_interval_ms = static_cast<uint32_t>(
                impl_->options.heartbeat_interval.count()),
            .max_frame_bytes = std::min(
                kSessionStreamMaxFrameBytes,
                impl_->options.max_queue_bytes),
            .max_queue_bytes = impl_->options.max_queue_bytes,
        }));
}

void SessionStreamService::pump(const Poll& poll, const Touch& touch,
    const Dispatch& dispatch)
{
    impl_->adopt_connections();
    const auto now = std::chrono::steady_clock::now();
    for (auto& connection : impl_->connections)
    {
        auto core = connection->core;
        std::string client_id;
        std::string session_id;
        SessionPollRequest request;
        bool awaiting_update = false;
        bool authenticated = false;
        bool closed = false;
        size_t queued_bytes = 0;
        std::vector<Impl::ConnectionCore::PendingCommand> commands;
        std::chrono::steady_clock::time_point last_enqueued;
        {
            std::lock_guard guard(core->mutex);
            authenticated = core->authenticated;
            awaiting_update = core->awaiting_update;
            closed = core->closed;
            client_id = core->client_id;
            session_id = core->session_id;
            request = core->poll;
            last_enqueued = core->last_enqueued_at;
            queued_bytes = core->queued_bytes;
            while (!core->commands.empty()
                && commands.size() < kCommandsPerPump)
            {
                core->command_bytes -= std::min(core->command_bytes,
                    core->commands.front().bytes);
                commands.push_back(std::move(core->commands.front()));
                core->commands.pop_front();
            }
        }
        if (closed || !authenticated)
            continue;
        if (touch)
            touch(client_id);

        for (const auto& pending : commands)
        {
            SessionStreamServerFrame frame{
                .kind = SessionStreamServerFrameKind::CommandResult,
                .server_epoch = impl_->options.server_epoch,
                .command_result = impl_->execute_command(
                    session_id, client_id, pending.command, dispatch),
            };
            if (!impl_->enqueue(core, std::move(frame),
                    OutboundPriority::Control))
                break;
        }
        {
            std::lock_guard guard(core->mutex);
            if (core->closed)
                continue;
            queued_bytes = core->queued_bytes;
        }

        // A successful enqueue is not an acknowledgement. Do not ask the
        // poll scheduler to consume or rebuild from the same cursor until the
        // client sends an Update with its applied cursors.
        if (!awaiting_update)
        {
            const size_t frame_budget = std::min(
                impl_->options.max_queue_bytes
                    - impl_->control_reserve_bytes(),
                kSessionStreamMaxFrameBytes);
            const size_t payload_budget
                = frame_budget > kFrameEnvelopeReserve
                ? frame_budget - kFrameEnvelopeReserve
                : 1;
            const auto result = poll(
                session_id, client_id, request, payload_budget);
            if (!result.ok())
            {
                SessionStreamServerFrame frame{
                    .kind = SessionStreamServerFrameKind::Error,
                    .server_epoch = impl_->options.server_epoch,
                    .error_code = result.error_code,
                    .error_message = result.error_message,
                };
                {
                    std::lock_guard guard(core->mutex);
                    core->close_after_flush = true;
                }
                impl_->enqueue(core, std::move(frame),
                    OutboundPriority::Control);
                continue;
            }
            auto response = std::move(result.response);
            if (has_events(*response))
            {
                SessionStreamServerFrame frame{
                    .kind = SessionStreamServerFrameKind::Events,
                    .server_epoch = impl_->options.server_epoch,
                    .events = std::move(*response),
                };
                {
                    std::lock_guard guard(core->mutex);
                    core->awaiting_update = true;
                }
                impl_->enqueue(core, std::move(frame),
                    OutboundPriority::Events);
                continue;
            }
        }
        if (queued_bytes == 0
            && now - last_enqueued >= impl_->options.heartbeat_interval)
        {
            SessionStreamServerFrame frame{
                .kind = SessionStreamServerFrameKind::Heartbeat,
                .server_epoch = impl_->options.server_epoch,
            };
            impl_->enqueue(core, std::move(frame),
                OutboundPriority::Control);
        }
    }
    impl_->retire_connections();
    {
        std::lock_guard guard(impl_->mutex);
        for (auto it = impl_->tickets.begin(); it != impl_->tickets.end();)
        {
            if (it->second.expires_at < now)
                it = impl_->tickets.erase(it);
            else
                ++it;
        }
    }
}

void SessionStreamService::disconnect_client(std::string_view client_id)
{
    for (auto& connection : impl_->connections)
    {
        bool matches = false;
        {
            std::lock_guard guard(connection->core->mutex);
            matches = connection->core->client_id == client_id;
        }
        if (matches)
            impl_->close_core(connection->core);
    }
    std::lock_guard guard(impl_->mutex);
    for (auto it = impl_->tickets.begin(); it != impl_->tickets.end();)
    {
        if (it->second.client_id == client_id)
            it = impl_->tickets.erase(it);
        else
            ++it;
    }
    const std::string prefix = std::string(client_id) + '\n';
    for (const auto& [key, command] : impl_->completed_commands)
    {
        if (key.starts_with(prefix))
        {
            impl_->completed_command_bytes -= std::min(
                impl_->completed_command_bytes, command.bytes);
        }
    }
    std::erase_if(impl_->completed_commands,
        [&](const auto& entry) { return entry.first.starts_with(prefix); });
    std::erase_if(impl_->completed_command_order,
        [&](const std::string& key) { return key.starts_with(prefix); });
}

const std::string& SessionStreamService::endpoint() const
{
    return impl_->listener.endpoint();
}

size_t SessionStreamService::connection_count() const
{
    size_t count = 0;
    for (const auto& connection : impl_->connections)
    {
        std::lock_guard guard(connection->core->mutex);
        count += connection->core->authenticated
            && !connection->core->closed;
    }
    return count;
}

SessionStreamServiceStats SessionStreamService::stats() const
{
    SessionStreamServiceStats result{
        .control_reserve_bytes = impl_->control_reserve_bytes(),
        .completed_command_count = impl_->completed_commands.size(),
        .completed_command_bytes = impl_->completed_command_bytes,
        .commands_dispatched = impl_->commands_dispatched,
        .command_replays = impl_->command_replays,
        .command_conflicts = impl_->command_conflicts,
        .command_rejections = impl_->command_rejections,
    };
    for (const auto& connection : impl_->connections)
    {
        std::lock_guard guard(connection->core->mutex);
        result.pending_command_count += connection->core->commands.size();
        result.pending_command_bytes += connection->core->command_bytes;
        result.queued_control_bytes
            += connection->core->queued_control_bytes;
        result.queued_event_bytes += connection->core->queued_event_bytes;
        result.peak_control_bytes = std::max(result.peak_control_bytes,
            connection->core->peak_control_bytes);
        result.peak_event_bytes = std::max(result.peak_event_bytes,
            connection->core->peak_event_bytes);
    }
    return result;
}

} // namespace draxul
