#include <draxul/agent_model.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace draxul
{

std::string_view to_string(AgentIdentityOrigin value) noexcept
{
    switch (value)
    {
    case AgentIdentityOrigin::Managed:
        return "managed";
    case AgentIdentityOrigin::Discovered:
        return "discovered";
    }
    return "managed";
}

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

std::optional<AgentRestorePolicy> parse_agent_restore_policy(
    std::string_view value) noexcept
{
    if (value == "fresh")
        return AgentRestorePolicy::Fresh;
    if (value == "resume_if_available")
        return AgentRestorePolicy::ResumeIfAvailable;
    if (value == "shell_only")
        return AgentRestorePolicy::ShellOnly;
    return std::nullopt;
}

std::string_view to_string(AgentSessionRefKind value) noexcept
{
    switch (value)
    {
    case AgentSessionRefKind::Id:
        return "id";
    case AgentSessionRefKind::Path:
        return "path";
    }
    return "id";
}

std::optional<AgentSessionRefKind> parse_agent_session_ref_kind(
    std::string_view value) noexcept
{
    if (value == "id")
        return AgentSessionRefKind::Id;
    if (value == "path")
        return AgentSessionRefKind::Path;
    return std::nullopt;
}

bool is_official_agent_session_source(
    std::string_view source, std::string_view agent_kind) noexcept
{
    return (source == "draxul:codex" && agent_kind == "codex")
        || (source == "draxul:claude" && agent_kind == "claude");
}

bool validate_agent_session_ref(const AgentSessionRef& value, std::string* error)
{
    const auto invalid_text = [](std::string_view text, size_t limit) {
        return text.empty() || text.size() > limit
            || std::any_of(text.begin(), text.end(), [](unsigned char ch) {
                   return ch < 0x20 || ch == 0x7f;
               });
    };
    if (!is_official_agent_session_source(value.source, value.agent_kind))
    {
        if (error)
            *error = "Agent session reference source is not official.";
        return false;
    }
    if (value.integration_version == 0)
    {
        if (error)
            *error = "Agent session integration version is invalid.";
        return false;
    }
    if (invalid_text(value.value, 4096))
    {
        if (error)
            *error = "Agent session reference value is invalid.";
        return false;
    }
    if (value.kind == AgentSessionRefKind::Path)
    {
        const std::filesystem::path path(value.value);
        if (!path.is_absolute() || path.lexically_normal() != path)
        {
            if (error)
                *error = "Agent session path must be absolute and normalized.";
            return false;
        }
    }
    if (error)
        error->clear();
    return true;
}

} // namespace draxul
