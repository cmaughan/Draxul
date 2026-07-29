#pragma once

#include <draxul/control_plane.h>
#include <draxul/topology_protocol.h>

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace draxul
{

struct ManagedAgentTopologyLaunch
{
    AgentIdentity identity;
    AgentRestorePolicy restore_policy
        = AgentRestorePolicy::ResumeIfAvailable;
    std::vector<std::string> additional_args;
    bool replace_default_args = false;
    std::string working_directory;
};

struct TopologyServiceCallbacks
{
    std::function<std::optional<std::string>(
        std::string_view pane_id, std::string_view name,
        std::string& error)>
        create_server_terminal;
    std::function<void(std::string_view terminal_id)>
        destroy_server_terminal;
    std::function<bool(std::string_view terminal_id,
        std::string& error)>
        restart_server_terminal;
    std::function<std::optional<std::string>(
        std::string_view space_id, std::string_view tab_id,
        std::string_view pane_id, std::string_view name,
        const ManagedAgentTopologyLaunch& launch,
        std::string& error)>
        create_managed_agent_terminal;
};

class TopologyService
{
public:
    explicit TopologyService(std::string session_id = "default",
        TopologyServiceCallbacks callbacks = {});
    TopologyService(TopologySnapshot snapshot,
        TopologyServiceCallbacks callbacks);

    bool handles(std::string_view method) const;
    ControlMethodResult handle(
        std::string_view method, const nlohmann::json& params);
    ControlMethodResult report_agent_session(
        std::string_view pane_id,
        std::string_view agent_instance_id,
        const AgentSessionRef& session_ref);
    ControlMethodResult launch_agent(
        std::string_view space_id,
        std::string_view tab_id,
        std::string_view target_pane_id,
        std::string_view name,
        const ManagedAgentTopologyLaunch& launch);

    const TopologySnapshot& snapshot() const noexcept
    {
        return snapshot_;
    }

private:
    ControlMethodResult read_snapshot(
        const nlohmann::json& params) const;
    ControlMethodResult poll(const nlohmann::json& params) const;
    ControlMethodResult command(const nlohmann::json& params);
    bool apply(const TopologyCommand& command, std::string& error_code,
        std::string& error);

    std::string next_id(std::string_view prefix);
    TopologyTab make_client_local_tab(std::string name);
    TopologyTab make_initial_server_tab();
    void remember(std::string key, TopologyCommandResult result);

    TopologySnapshot snapshot_;
    TopologyServiceCallbacks callbacks_;
    uint64_t next_serial_ = 1;
    std::unordered_map<std::string, TopologyCommandResult> completed_;
    std::deque<std::string> completed_order_;
};

} // namespace draxul
