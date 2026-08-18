#pragma once

#include "control_deadline.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>

namespace draxul::control_detail
{

enum class TransportStage
{
    RuntimeSecurity,
    MetadataCreate,
    MetadataWrite,
    MetadataFlush,
    MetadataReplace,
    MetadataDirectoryFlush,
    EndpointPrepare,
    EndpointClaim,
    EndpointConfigure,
    ConnectWait,
    Connect,
    ReadPrefix,
    ReadPayload,
    WritePrefix,
    WritePayload,
    Flush,
    Cancel,
    ListenerCreate,
    ListenerWait,
    Accept,
};

enum class NativeDomain
{
    None,
    Win32,
    Posix,
};

enum class FailureClass
{
    EndpointUnavailable,
    IoError,
    DeadlineExceeded,
};

struct TransportError
{
    TransportStage stage = TransportStage::EndpointPrepare;
    NativeDomain domain = NativeDomain::None;
    uint32_t native_code = 0;
    FailureClass classification = FailureClass::IoError;
    std::string message;
};

struct TransportStatus
{
    bool ok = false;
    TransportError error;

    static TransportStatus success()
    {
        return { true, {} };
    }

    static TransportStatus failure(TransportError error)
    {
        return { false, std::move(error) };
    }
};

struct ClientExchangeResult
{
    bool ok = false;
    std::string response_bytes;
    TransportError error;
};

TransportStatus secure_runtime_directory(
    const std::filesystem::path& runtime_directory);
TransportStatus write_current_user_metadata(
    const std::filesystem::path& path, std::string_view contents);
ClientExchangeResult client_exchange(std::string_view endpoint,
    std::string_view request_bytes, ControlDeadline deadline);

using ServerFrameHandler
    = std::function<std::string(std::optional<std::string>)>;
using StartupReporter = std::function<void(std::string)>;

class ServerTransport
{
public:
    virtual ~ServerTransport() = default;

    virtual TransportStatus prepare(std::string_view session_id,
        const std::filesystem::path& runtime_directory)
        = 0;
    virtual void run(std::stop_token stop_token,
        const ServerFrameHandler& handle_frame,
        const StartupReporter& report_startup)
        = 0;
    virtual const std::string& endpoint() const = 0;
    virtual bool endpoint_in_use() const = 0;
    virtual void abandon_endpoint() = 0;
    virtual void cleanup() = 0;
    virtual uint32_t take_listener_error() = 0;
};

std::unique_ptr<ServerTransport> make_server_transport();

} // namespace draxul::control_detail
