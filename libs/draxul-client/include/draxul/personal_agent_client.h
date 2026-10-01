#pragma once

#include <draxul/personal_agent_store.h>
#include <draxul/server_control_channel.h>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace draxul
{
class PersonalAgentClient
{
public:
    PersonalAgentClient(ServerControlChannelOptions options, std::function<void()> wake);
    ~PersonalAgentClient();
    std::shared_ptr<const PersonalAgentSnapshot> snapshot() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::shared_ptr<const PersonalAgentSnapshot> snapshot_;
    std::jthread worker_;
};
}
