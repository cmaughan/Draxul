#include <draxul/agent_model.h>

namespace draxul
{

std::string_view to_string(AgentLifecycle value) noexcept
{
    switch (value)
    {
    case AgentLifecycle::Starting:
        return "starting";
    case AgentLifecycle::Running:
        return "running";
    case AgentLifecycle::Exited:
        return "exited";
    case AgentLifecycle::Failed:
        return "failed";
    }
    return "unknown";
}

std::string_view to_string(AgentStatus value) noexcept
{
    switch (value)
    {
    case AgentStatus::Unknown:
        return "unknown";
    case AgentStatus::Idle:
        return "idle";
    case AgentStatus::Working:
        return "working";
    case AgentStatus::Blocked:
        return "blocked";
    case AgentStatus::Done:
        return "done";
    }
    return "unknown";
}

std::string_view to_string(AgentStateAuthority value) noexcept
{
    switch (value)
    {
    case AgentStateAuthority::None:
        return "none";
    case AgentStateAuthority::DirectHost:
        return "direct_host";
    case AgentStateAuthority::ScreenManifest:
        return "screen_manifest";
    case AgentStateAuthority::OfficialIntegration:
        return "official_integration";
    }
    return "none";
}

std::string_view to_string(AgentRestorePolicy value) noexcept
{
    switch (value)
    {
    case AgentRestorePolicy::Fresh:
        return "fresh";
    case AgentRestorePolicy::ResumeIfAvailable:
        return "resume_if_available";
    case AgentRestorePolicy::ShellOnly:
        return "shell_only";
    }
    return "shell_only";
}

} // namespace draxul
