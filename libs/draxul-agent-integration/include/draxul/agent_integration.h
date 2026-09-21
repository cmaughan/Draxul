#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace draxul
{

enum class AgentIntegrationProvider
{
    Codex,
    Claude,
};

enum class AgentIntegrationAction
{
    Install,
    Uninstall,
};

enum class AgentIntegrationState
{
    Unavailable,
    NotInstalled,
    Current,
    Outdated,
    Invalid,
};

// Every path used by an operation is supplied by the caller. Environment and
// user-home discovery belong to the CLI/application boundary.
struct AgentIntegrationPaths
{
    std::filesystem::path directory;
    std::filesystem::path hook;
    std::filesystem::path registration;
    std::filesystem::path features;
};

struct AgentIntegrationRequest
{
    AgentIntegrationProvider provider = AgentIntegrationProvider::Codex;
    AgentIntegrationAction action = AgentIntegrationAction::Install;
    AgentIntegrationPaths paths;
};

struct AgentIntegrationStatus
{
    AgentIntegrationProvider provider = AgentIntegrationProvider::Codex;
    AgentIntegrationState state = AgentIntegrationState::Invalid;
    uint32_t expected_version = 0;
    std::filesystem::path path;
    std::string reason;
};

struct AgentIntegrationResult
{
    bool success = false;
    AgentIntegrationStatus status;
    std::string error;
};

AgentIntegrationPaths agent_integration_paths(
    AgentIntegrationProvider provider,
    const std::filesystem::path& configuration_directory);
AgentIntegrationStatus inspect_agent_integration(
    AgentIntegrationProvider provider, const AgentIntegrationPaths& paths);
AgentIntegrationResult apply_agent_integration(
    const AgentIntegrationRequest& request);

const char* agent_integration_provider_name(AgentIntegrationProvider provider);
const char* agent_integration_state_name(AgentIntegrationState state);

} // namespace draxul
