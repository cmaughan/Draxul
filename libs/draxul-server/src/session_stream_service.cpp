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
        std::deque<std::string> outbound;
        size_t queued_bytes = 0;
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
        std::string frame)
    {
        std::lock_guard guard(core->mutex);
        if (core->closed)
            return false;
        if (frame.size() > options.max_queue_bytes
            || core->queued_bytes
                    > options.max_queue_bytes - frame.size())
        {
            core->closed = true;
            core->outbound_ready.notify_all();
            core->stream->close();
            return false;
        }
        core->queued_bytes += frame.size();
        core->outbound.push_back(std::move(frame));
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
            std::string frame;
            {
                std::unique_lock lock(core->mutex);
                core->outbound_ready.wait(lock, [&] {
                    return core->closed || !core->outbound.empty()
                        || stop_token.stop_requested();
                });
                if ((core->closed && core->outbound.empty())
                    || stop_token.stop_requested())
                    break;
                frame = std::move(core->outbound.front());
                core->outbound.pop_front();
            }
            AsyncFrameStreamError error;
            const bool written = core->stream->write_frame(
                frame, stop_token, error);
            {
                std::lock_guard guard(core->mutex);
                core->queued_bytes -= std::min(
                    core->queued_bytes, frame.size());
            }
            if (!written)
                break;
            bool close_after_flush = false;
            {
                std::lock_guard guard(core->mutex);
                close_after_flush = core->close_after_flush
                    && core->outbound.empty();
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
    std::vector<std::unique_ptr<Connection>> connections;
    std::jthread reaper;
    std::mutex reaper_mutex;
    std::condition_variable reaper_ready;
    std::deque<std::unique_ptr<Connection>> retired;
    std::atomic<size_t> raw_connection_count = 0;
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

void SessionStreamService::pump(const Poll& poll, const Touch& touch)
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
        }
        if (closed || !authenticated)
            continue;
        if (touch)
            touch(client_id);

        // A successful enqueue is not an acknowledgement. Do not ask the
        // poll scheduler to consume or rebuild from the same cursor until the
        // client sends an Update with its applied cursors.
        if (!awaiting_update)
        {
            const size_t payload_budget
                = impl_->options.max_queue_bytes > kFrameEnvelopeReserve
                ? impl_->options.max_queue_bytes - kFrameEnvelopeReserve
                : 1;
            const auto result = poll(
                session_id, client_id, request, payload_budget);
            if (!result.ok)
            {
                SessionStreamServerFrame frame{
                    .kind = SessionStreamServerFrameKind::Error,
                    .server_epoch = impl_->options.server_epoch,
                    .error_code = result.error_code,
                    .error_message = result.error_message,
                };
                {
                    std::lock_guard guard(core->mutex);
                    frame.frame_serial = core->next_frame_serial++;
                    core->close_after_flush = true;
                }
                impl_->enqueue(core, encode_frame(frame));
                continue;
            }
            std::string parse_error;
            auto response = session_poll_response_from_json(
                result.value, parse_error);
            if (!response)
            {
                impl_->close_core(core);
                continue;
            }
            if (has_events(*response))
            {
                SessionStreamServerFrame frame{
                    .kind = SessionStreamServerFrameKind::Events,
                    .server_epoch = impl_->options.server_epoch,
                    .events = std::move(*response),
                };
                {
                    std::lock_guard guard(core->mutex);
                    frame.frame_serial = core->next_frame_serial++;
                    core->awaiting_update = true;
                }
                impl_->enqueue(core, encode_frame(frame));
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
            {
                std::lock_guard guard(core->mutex);
                frame.frame_serial = core->next_frame_serial++;
            }
            impl_->enqueue(core, encode_frame(frame));
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

} // namespace draxul
