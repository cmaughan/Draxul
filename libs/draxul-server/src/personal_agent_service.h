#pragma once

#include <draxul/personal_agent_store.h>
#include <draxul/agent_model.h>
#include <draxul/control_plane.h>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace draxul
{
// Owns collection metadata IO on its worker. Interactive agents use the normal terminal runtime. Control requests
// only enqueue bounded commands/read results; no filesystem work on the kernel loop.
class PersonalAgentService
{
public:
    explicit PersonalAgentService(std::filesystem::path root,
        std::filesystem::path local_state = {}, std::vector<AgentDefinition> profiles = {});
    ~PersonalAgentService();
    PersonalAgentSnapshot snapshot() const;
    bool deleting(std::string_view id) const;
    ControlMethodResult handle(std::string_view method, const nlohmann::json& params);

private:
    void work(std::stop_token stop);
    nlohmann::json execute(const nlohmann::json& command);
    void refresh();
    std::filesystem::path root_;
    std::filesystem::path local_state_;
    AgentDefinitionRegistry profiles_;
    PersonalAgentSnapshot current_;
    std::string authority_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    PersonalAgentSnapshot snapshot_;
    std::deque<nlohmann::json> commands_;
    std::map<std::string, nlohmann::json> results_;
    std::map<std::string, nlohmann::json> submitted_;
    std::jthread worker_;
};
}
