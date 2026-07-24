#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

struct AgentDefinition
{
    std::string profile_id;
    std::string kind;
    std::string display_name;
    std::string executable;
    std::vector<std::string> default_args;
    AgentRestorePolicy restore_policy = AgentRestorePolicy::ResumeIfAvailable;

    bool operator==(const AgentDefinition&) const = default;
};

struct AgentLaunchRequest
{
    std::string profile_id;
    std::vector<std::string> additional_args;
};

class AgentDefinitionRegistry
{
public:
    AgentDefinitionRegistry();

    bool register_definition(AgentDefinition definition);
    const AgentDefinition* find(std::string_view profile_id) const;
    const std::vector<AgentDefinition>& definitions() const noexcept
    {
        return definitions_;
    }

private:
    std::vector<AgentDefinition> definitions_;
};

std::string_view to_string(AgentLifecycle value) noexcept;
std::string_view to_string(AgentStatus value) noexcept;
std::string_view to_string(AgentStateAuthority value) noexcept;
std::string_view to_string(AgentRestorePolicy value) noexcept;

} // namespace draxul
