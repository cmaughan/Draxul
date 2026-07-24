#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace draxul
{

struct ControlCliCommand
{
    std::string session_id = "default";
    std::string method;
    std::string value;
    std::string text;
    std::string working_directory;
    std::vector<std::string> values;
    std::vector<std::string> arguments;
    int lines = 50;
    int timeout_ms = 0;
    uint32_t integration_version = 0;
    uint64_t sequence = 0;
    std::string source;
    std::string agent_kind;
    std::string agent_instance_id;
    std::string reference_kind = "id";
    std::string reference_value;
    std::optional<int> space_id;
    bool json = false;
};

struct ParseControlCliResult
{
    bool recognized = false;
    std::optional<ControlCliCommand> command;
    std::optional<std::string> error;
};

ParseControlCliResult parse_control_cli(const std::vector<std::string>& args);
int run_control_cli(const ControlCliCommand& command);

} // namespace draxul
