#pragma once

#include <draxul/cli_context.h>

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
int run_integration_cli(const IntegrationCliCommand& command, const CliContext& io = {});

} // namespace draxul
