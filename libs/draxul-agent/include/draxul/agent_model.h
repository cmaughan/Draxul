#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace draxul
{

// Durable metadata for an agent intentionally launched into a pane. Runtime
// state is deliberately absent: it is projected from the live host.
struct AgentIdentity
{
    std::string kind;
    std::string display_name;
    std::string instance_id;

    bool operator==(const AgentIdentity&) const = default;
};

enum class AgentLifecycle
{
    Starting,
    Running,
    Exited,
    Failed,
};

enum class AgentStatus
{
    Unknown,
    Idle,
    Working,
    Blocked,
    Done,
};

enum class AgentStateAuthority
{
    None,
    DirectHost,
    ScreenManifest,
    OfficialIntegration,
};

enum class AgentRestorePolicy
{
    Fresh,
    ResumeIfAvailable,
    ShellOnly,
};

struct AgentRuntimeGeneration
{
    uint64_t value = 0;

    bool operator==(const AgentRuntimeGeneration&) const = default;
};

std::string_view to_string(AgentLifecycle value) noexcept;
std::string_view to_string(AgentStatus value) noexcept;
std::string_view to_string(AgentStateAuthority value) noexcept;
std::string_view to_string(AgentRestorePolicy value) noexcept;

} // namespace draxul
