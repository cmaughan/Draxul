#include <draxul/session_attach.h>

#include <draxul/log.h>
#include <draxul/perf_timing.h>

#include "session_attach_internal.h"

#include <utility>

namespace draxul
{

using namespace session_attach_detail;

SessionAttachServer::SessionAttachServer() = default;

SessionAttachServer::~SessionAttachServer()
{
    stop();
}

bool SessionAttachServer::start(
    std::string_view session_id, CommandHandler on_command_requested, std::string* error)
{
    return start(session_id, std::move(on_command_requested), QueryHandler{}, RenameHandler{}, error);
}

bool SessionAttachServer::start(std::string_view session_id, CommandHandler on_command_requested,
    QueryHandler on_query_requested, std::string* error)
{
    return start(session_id,
        std::move(on_command_requested),
        std::move(on_query_requested),
        RenameHandler{},
        error);
}

bool SessionAttachServer::start(std::string_view session_id, CommandHandler on_command_requested,
    QueryHandler on_query_requested, RenameHandler on_rename_requested, std::string* error)
{
    PERF_MEASURE();
    stop();

    session_id_ = session_id.empty() ? "default" : std::string(session_id);
    on_command_requested_ = std::move(on_command_requested);
    on_query_requested_ = std::move(on_query_requested);
    on_rename_requested_ = std::move(on_rename_requested);
    stop_requested_ = false;

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Starting session attach server for '%s'",
        session_id_.c_str());

    transport_ = make_session_transport(session_id_);
    if (!transport_ || !transport_->start(error))
    {
        transport_.reset();
        return false;
    }

    running_ = true;
    server_thread_ = std::thread([this]() {
        while (!stop_requested_.load())
        {
            std::string accept_error;
            auto connection = transport_->accept(&accept_error);
            if (!connection)
            {
                if (!stop_requested_.load())
                {
                    DRAXUL_LOG_WARN(LogCategory::App,
                        "Session attach server accept failed: %s",
                        accept_error.c_str());
                }
                continue;
            }

            std::string request;
            std::string read_error;
            if (!connection->read_request(&request, &read_error) || request.empty())
                continue;

            DRAXUL_LOG_DEBUG(LogCategory::App,
                "Session attach server for '%s' received command '%.*s'",
                session_id_.c_str(),
                static_cast<int>(request.size()),
                request.data());

            std::string response = "ok";
            const ParsedRequest parsed = parse_request(request);
            if (parsed.kind == RequestKind::Stop)
            {
                stop_requested_ = true;
            }
            else if (!stop_requested_.load())
            {
                switch (parsed.kind)
                {
                case RequestKind::Command:
                    if (on_command_requested_)
                        on_command_requested_(parsed.command);
                    if (parsed.command == Command::Shutdown)
                        stop_requested_ = true;
                    break;
                case RequestKind::Rename:
                    if (on_rename_requested_)
                        on_rename_requested_(parsed.argument);
                    break;
                case RequestKind::QueryLiveSession:
                    response = serialize_live_session_info(
                        on_query_requested_ ? on_query_requested_() : LiveSessionInfo{});
                    break;
                case RequestKind::Stop:
                case RequestKind::Unknown:
                    break;
                }
            }

            std::string write_error;
            (void)connection->write_all(response, &write_error);
        }

        running_ = false;
    });

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Session attach server started for '%s'",
        session_id_.c_str());
    return true;
}

void SessionAttachServer::stop()
{
    PERF_MEASURE();
    stop_requested_ = true;

    if (transport_ && server_thread_.joinable() && running())
        transport_->wake(kStopInternalCommand);

    if (server_thread_.joinable())
        server_thread_.join();

    if (transport_)
    {
        transport_->close();
        transport_.reset();
    }
    running_ = false;
}

SessionAttachServer::ProbeStatus SessionAttachServer::probe(
    std::string_view session_id, std::string* error)
{
    PERF_MEASURE();
    auto transport = make_session_transport(session_id.empty() ? "default" : session_id);
    const TransportResult result = transport->probe(kClientDeadline);
    if (result.status == TransportStatus::Ok)
        return ProbeStatus::Running;
    if (result.status == TransportStatus::NoServer)
        return ProbeStatus::NoServer;
    if (error)
        *error = result.message;
    return ProbeStatus::Error;
}

SessionAttachServer::AttachStatus SessionAttachServer::try_attach(
    std::string_view session_id, std::string* error)
{
    return send_command(session_id, Command::Activate, error);
}

SessionAttachServer::AttachStatus SessionAttachServer::send_command(
    std::string_view session_id, Command command, std::string* error)
{
    PERF_MEASURE();
    const std::string actual_session_id = session_id.empty() ? "default" : std::string(session_id);
    DRAXUL_LOG_DEBUG(LogCategory::App,
        "Sending session attach command '%.*s' to '%s'",
        static_cast<int>(command_text(command).size()),
        command_text(command).data(),
        actual_session_id.c_str());

    auto transport = make_session_transport(actual_session_id);
    return send_command_via_transport(*transport, command, error);
}

SessionAttachServer::AttachStatus session_attach_detail::send_command_via_transport(
    SessionTransport& transport, SessionAttachServer::Command command, std::string* error)
{
    std::unique_ptr<SessionConnection> connection;
    const TransportResult connected = transport.connect(&connection, kClientDeadline);
    if (connected.status == TransportStatus::NoServer)
        return SessionAttachServer::AttachStatus::NoServer;
    if (connected.status != TransportStatus::Ok)
    {
        if (error)
            *error = connected.message;
        return SessionAttachServer::AttachStatus::Error;
    }

    std::string write_error;
    if (!connection->write_all(command_text(command), &write_error))
    {
        if (error)
            *error = write_error;
        return SessionAttachServer::AttachStatus::Error;
    }

    // Keep the client end alive until the server has consumed and acknowledged
    // the command. Closing immediately after WriteFile is racy for Windows
    // message-mode pipes: the server can observe a broken pipe before it has
    // dispatched the buffered request.
    std::string response;
    if (!connection->read_response(&response, error))
        return SessionAttachServer::AttachStatus::Error;
    if (!response.empty() && response != "ok")
    {
        if (error)
            *error = response;
        return SessionAttachServer::AttachStatus::Error;
    }

    return SessionAttachServer::AttachStatus::Attached;
}

bool SessionAttachServer::query_live_session(
    std::string_view session_id, LiveSessionInfo* info, std::string* error)
{
    PERF_MEASURE();
    auto transport = make_session_transport(session_id.empty() ? "default" : session_id);
    return query_live_session_via_transport(*transport, info, error);
}

bool session_attach_detail::query_live_session_via_transport(
    SessionTransport& transport, SessionAttachServer::LiveSessionInfo* info, std::string* error)
{
    std::unique_ptr<SessionConnection> connection;
    const TransportResult connected = transport.connect(&connection, kClientDeadline);
    if (connected.status != TransportStatus::Ok)
    {
        if (connected.status == TransportStatus::Error && error)
            *error = connected.message;
        return false;
    }

    std::string operation_error;
    if (!connection->write_all(
            command_text(SessionAttachServer::Command::QueryLiveSession), &operation_error)
        || !connection->read_response(&operation_error, error))
    {
        if (error && error->empty())
            *error = operation_error;
        return false;
    }

    if (!parse_live_session_info(operation_error, info, error))
    {
        if (error && error->empty())
            *error = "Failed to parse the live-session response.";
        return false;
    }
    return true;
}

bool SessionAttachServer::rename_session(
    std::string_view session_id, std::string_view session_name, std::string* error)
{
    PERF_MEASURE();
    if (session_name.empty())
    {
        if (error)
            *error = "Session name must not be empty.";
        return false;
    }

    auto transport = make_session_transport(session_id.empty() ? "default" : session_id);
    std::unique_ptr<SessionConnection> connection;
    const TransportResult connected = transport->connect(&connection, kClientDeadline);
    if (connected.status != TransportStatus::Ok)
    {
        if (error)
            *error = connected.status == TransportStatus::NoServer
                ? "No running session."
                : connected.message;
        return false;
    }

    std::string response;
    const std::string request = rename_request(session_name);
    if (!connection->write_all(request, error) || !connection->read_response(&response, error))
        return false;
    if (!response.empty() && response != "ok")
    {
        if (error)
            *error = response;
        return false;
    }
    return true;
}

} // namespace draxul
