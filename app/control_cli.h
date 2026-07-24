#pragma once

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
