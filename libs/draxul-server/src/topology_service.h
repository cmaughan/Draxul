#pragma once

#include <draxul/control_plane.h>
#include <draxul/topology_protocol.h>

#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

namespace draxul
{

class TopologyService
{
public:
    explicit TopologyService(std::string session_id = "default");

    bool handles(std::string_view method) const;
    ControlMethodResult handle(
        std::string_view method, const nlohmann::json& params);

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
    uint64_t next_serial_ = 1;
    std::unordered_map<std::string, TopologyCommandResult> completed_;
    std::deque<std::string> completed_order_;
};

} // namespace draxul
