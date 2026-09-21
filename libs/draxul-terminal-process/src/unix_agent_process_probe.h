#pragma once

#include <draxul/agent_model.h>
#include <optional>
#include <sys/types.h>

namespace draxul::detail
{

pid_t unix_foreground_process_group(int master_fd);
std::optional<AgentProcessObservation> capture_unix_agent_processes(
    int master_fd);

} // namespace draxul::detail
