#pragma once

#include <draxul/personal_agent_store.h>
#include <draxul/server_control_channel.h>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <deque>
#include <map>

namespace draxul
{
class PersonalAgentClient
{
public:
    PersonalAgentClient(ServerControlChannelOptions options, std::function<void()> wake);
    ~PersonalAgentClient();
    std::shared_ptr<const PersonalAgentSnapshot> snapshot() const;
    std::string submit(const PersonalAgentCommand& command);
    std::optional<PersonalAgentCommandResult> result(std::string_view id) const;

private:
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::shared_ptr<const PersonalAgentSnapshot> snapshot_;
    std::deque<nlohmann::json> commands_;
    std::map<std::string, PersonalAgentCommandResult> results_;
    std::jthread worker_;
};
}
