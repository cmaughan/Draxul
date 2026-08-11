#pragma once

#include <optional>
#include <string>
#include <vector>

namespace draxul
{

struct TopologyCliCommand
{
    std::string noun;
    std::string verb;
    std::string session_id = "default";
    std::string server_runtime_directory;
    std::string target_id;
    std::string secondary_id;
    std::string space_id;
    std::string tab_id;
    std::string name;
    std::string root_directory;
    std::string working_directory;
    std::string plugin_id;
    std::string plugin_config_json;
    std::string text;
    std::string direction = "right";
    float ratio = 0.5f;
    int move_delta = 0;
    int lines = 50;
    int timeout_ms = 30000;
    std::vector<std::string> values;
    bool json = false;
    bool dry_run = false;
};

struct ParseTopologyCliResult
{
    bool recognized = false;
    std::optional<TopologyCliCommand> command;
    std::optional<std::string> error;
};

ParseTopologyCliResult parse_topology_cli(
    const std::vector<std::string>& args);
int run_topology_cli(const TopologyCliCommand& command);

} // namespace draxul
