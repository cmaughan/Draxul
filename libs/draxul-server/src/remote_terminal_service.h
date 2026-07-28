#pragma once

#include "remote_terminal_runtime.h"

#include <draxul/control_plane.h>

#include <deque>
#include <string>
#include <unordered_map>

namespace draxul
{

struct RemoteTerminalServiceOptions
{
    std::string method_prefix;
    std::string server_epoch;
    std::string pane_id;
    std::string terminal_id;
    std::string name;
};

class RemoteTerminalService
{
public:
    RemoteTerminalService(
        RemoteTerminalServiceOptions options, IRemoteTerminalRuntime& runtime);

    bool handles(std::string_view method) const;
    ControlMethodResult handle(
        std::string_view method, const nlohmann::json& params);
    void pump();
    bool started() const;

private:
    struct Subscriber
    {
        std::deque<RemoteTerminalEvent> events;
        bool needs_resync = false;
    };

    bool read_client_id(
        const nlohmann::json& params, std::string& client_id) const;
    bool read_version(
        const nlohmann::json& params, RemoteTerminalVersion& version) const;
    bool ensure_runtime_started(std::string& error);
    RemoteTerminalVersion version() const;
    RemoteTerminalEvent snapshot_event() const;
    RemoteTerminalEvent make_delta_event();
    RemoteTerminalEvent make_controller_event();
    void broadcast(const RemoteTerminalEvent& event);

    ControlMethodResult attach(const nlohmann::json& params);
    ControlMethodResult poll(const nlohmann::json& params);
    ControlMethodResult input(const nlohmann::json& params);
    ControlMethodResult resize(const nlohmann::json& params);
    ControlMethodResult take_control(const nlohmann::json& params);
    ControlMethodResult disconnect(const nlohmann::json& params);
    ControlMethodResult restart(const nlohmann::json& params);

    RemoteTerminalServiceOptions options_;
    IRemoteTerminalRuntime& runtime_;
    uint64_t generation_ = 1;
    uint64_t sequence_ = 0;
    bool started_ = false;
    std::string controller_client_id_;
    std::unordered_map<std::string, Subscriber> subscribers_;
};

} // namespace draxul
