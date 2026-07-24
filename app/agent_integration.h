#pragma once

#include <optional>
#include <string>
#include <vector>

namespace draxul
{

struct IntegrationCliCommand
{
    std::string action;
    std::string target;
    bool json = false;
};

struct ParseIntegrationCliResult
{
    bool recognized = false;
    std::optional<IntegrationCliCommand> command;
    std::optional<std::string> error;
};

ParseIntegrationCliResult parse_integration_cli(
    const std::vector<std::string>& args);
int run_integration_cli(const IntegrationCliCommand& command);

} // namespace draxul
