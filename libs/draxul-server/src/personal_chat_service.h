#pragma once
#include <draxul/control_plane.h>
#include <draxul/agent_model.h>
#include <draxul/agent_protocol.h>
#include <draxul/topology_protocol.h>
#include <filesystem>
#include <chrono>
#include <map>
#include <memory>

namespace draxul
{
class PersonalAgentService;
class PersonalChatSession;
// Kernel-owned registry; each provider owns a worker and its process tree.
class PersonalChatService
{
public:
    PersonalChatService(PersonalAgentService& metadata, std::filesystem::path local_state,
        std::vector<AgentDefinition> profiles);
    ~PersonalChatService();
    ControlMethodResult handle(std::string_view method, const nlohmann::json& params);
    void erase(std::string_view id);
    void tick(std::chrono::steady_clock::time_point now);
    std::vector<ServerAgentProjection> project(const TopologySnapshot& topology) const;
private:
    std::chrono::steady_clock::time_point next_wake_ = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    std::chrono::steady_clock::time_point next_scan_{};
    PersonalAgentService& metadata_;
    std::filesystem::path local_state_;
    AgentDefinitionRegistry profiles_;
    std::map<std::string,std::unique_ptr<PersonalChatSession>> sessions_;
};
}
