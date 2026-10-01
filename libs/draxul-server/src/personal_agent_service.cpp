#include "personal_agent_service.h"

namespace draxul
{
PersonalAgentService::PersonalAgentService(std::filesystem::path root)
{
    if (root.empty())
        return;
    const auto utf8 = root.u8string();
    snapshot_.root.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    snapshot_.error = "Loading personal collection.";
    worker_ = std::jthread([this, root = std::move(root)](std::stop_token stop) {
        PersonalAgentSnapshot previous;
        while (!stop.stop_requested())
        {
            auto current = load_personal_agents(root, previous);
            previous = current;
            std::unique_lock lock(mutex_);
            snapshot_ = std::move(current);
            wake_.wait_for(lock, stop, std::chrono::seconds(1), [] { return false; });
        }
    });
}

PersonalAgentService::~PersonalAgentService()
{
    worker_.request_stop();
    wake_.notify_all();
}

PersonalAgentSnapshot PersonalAgentService::snapshot() const
{
    std::lock_guard lock(mutex_);
    return snapshot_;
}
}
