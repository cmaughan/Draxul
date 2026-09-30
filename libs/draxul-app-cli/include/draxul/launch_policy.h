#pragma once

#include <draxul/cli_args.h>
#include <draxul/server_client.h>

namespace draxul
{

std::filesystem::path cli_server_runtime_directory(const ParsedArgs& parsed);
bool is_server_control_launch(const ParsedArgs& parsed);
std::string describe_server_startup_failure(const ServerProbeResult& result,
    const std::filesystem::path& runtime);
std::optional<std::string> validate_server_launch_capabilities(
    const ParsedArgs& parsed, const ServerWelcome& welcome);
std::optional<std::string> resolve_new_session_id(ParsedArgs& parsed,
    const ServerStatusSnapshot& status, int64_t unix_seconds);

} // namespace draxul
