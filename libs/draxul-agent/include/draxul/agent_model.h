#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
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

// Immutable, bounded evidence captured from a terminal host. Observations are
// transient and must never be written to Session snapshots or ordinary logs.
struct AgentObservation
{
    uint64_t output_generation = 0;
    std::chrono::steady_clock::time_point captured_at{};
    std::optional<std::chrono::steady_clock::time_point> last_output_at;
    std::string terminal_title;
    std::vector<std::string> bottom_rows;
    bool cursor_visible = false;
    int cursor_col = 0;
    int cursor_row = 0;
    bool process_running = false;
    std::optional<int> exit_code;
};

// Sanitized explanation of a semantic-state decision. The rule metadata is
// safe to surface and retain in memory; matched terminal text is not.
struct AgentStatusExplanation
{
    AgentStatus status = AgentStatus::Unknown;
    AgentStateAuthority authority = AgentStateAuthority::None;
    std::string manifest_id;
    uint32_t manifest_version = 0;
    std::string rule_id;
    std::string evidence_category;
    std::string fallback_reason;
    uint64_t observation_generation = 0;
    std::chrono::steady_clock::time_point evaluated_at{};
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
    std::string working_directory;
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

AgentStatusExplanation evaluate_agent_observation(
    std::string_view agent_kind, const AgentObservation& observation);

} // namespace draxul
