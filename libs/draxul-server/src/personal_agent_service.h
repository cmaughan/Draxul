#pragma once

#include <draxul/personal_agent_store.h>

#include <condition_variable>
#include <mutex>
#include <thread>

namespace draxul
{
class PersonalAgentService
{
public:
    explicit PersonalAgentService(std::filesystem::path root);
    ~PersonalAgentService();
    PersonalAgentSnapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    PersonalAgentSnapshot snapshot_;
    std::jthread worker_;
};
}
