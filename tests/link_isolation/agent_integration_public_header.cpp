#include <draxul/agent_integration.h>

int main()
{
    const auto paths = draxul::agent_integration_paths(
        draxul::AgentIntegrationProvider::Codex, "configuration");
    const draxul::AgentIntegrationRequest request{
        .provider = draxul::AgentIntegrationProvider::Codex,
        .action = draxul::AgentIntegrationAction::Install,
        .paths = paths,
    };
    return request.paths.directory == "configuration" ? 0 : 1;
}
