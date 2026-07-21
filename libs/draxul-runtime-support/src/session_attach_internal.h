#pragma once

#include <draxul/session_attach.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace draxul::session_attach_detail
{

constexpr size_t kMaxRequestBytes = 1024;
constexpr std::chrono::milliseconds kClientDeadline{ 5000 };

enum class TransportStatus
{
    Ok,
    NoServer,
    Error,
};

struct TransportResult
{
    TransportStatus status = TransportStatus::Ok;
    std::string message;
};

class SessionConnection
{
public:
    virtual ~SessionConnection() = default;

    virtual bool read_request(std::string* request, std::string* error) = 0;
    virtual bool write_all(std::string_view payload, std::string* error) = 0;
    virtual bool read_response(std::string* response, std::string* error) = 0;
};

class SessionTransport
{
public:
    virtual ~SessionTransport() = default;

    virtual bool start(std::string* error) = 0;
    virtual std::unique_ptr<SessionConnection> accept(std::string* error) = 0;
    virtual TransportResult connect(
        std::unique_ptr<SessionConnection>* connection,
        std::chrono::milliseconds timeout) = 0;
    virtual TransportResult probe(std::chrono::milliseconds timeout) = 0;
    virtual void wake(std::string_view request) = 0;
    virtual void close() = 0;
};

std::unique_ptr<SessionTransport> make_session_transport(std::string_view session_id);
std::string endpoint_suffix(std::string_view session_id);

constexpr std::string_view kRenameSessionPrefix = "rename-session:";
constexpr std::string_view kStopInternalCommand = "stop-internal";

enum class RequestKind
{
    Command,
    Rename,
    QueryLiveSession,
    Stop,
    Unknown,
};

struct ParsedRequest
{
    RequestKind kind = RequestKind::Unknown;
    SessionAttachServer::Command command = SessionAttachServer::Command::Activate;
    std::string_view argument;
};

std::string_view command_text(SessionAttachServer::Command command);
std::string rename_request(std::string_view session_name);
ParsedRequest parse_request(std::string_view request);
std::string serialize_live_session_info(const SessionAttachServer::LiveSessionInfo& info);
bool parse_live_session_info(std::string_view payload,
    SessionAttachServer::LiveSessionInfo* info, std::string* error);

// Client operations are expressed against the transport interface so the
// platform-neutral protocol can be tested without opening a pipe or socket.
SessionAttachServer::AttachStatus send_command_via_transport(SessionTransport& transport,
    SessionAttachServer::Command command, std::string* error = nullptr);
bool query_live_session_via_transport(SessionTransport& transport,
    SessionAttachServer::LiveSessionInfo* info, std::string* error = nullptr);

} // namespace draxul::session_attach_detail
