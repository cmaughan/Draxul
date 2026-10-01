#include <draxul/personal_agent_client.h>
#include <draxul/personal_agent_protocol.h>

namespace draxul
{
PersonalAgentClient::PersonalAgentClient(ServerControlChannelOptions options, std::function<void()> wake)
{
    worker_ = std::jthread([this, options = std::move(options), wake = std::move(wake)](std::stop_token stop) {
        ServerControlChannel channel(options);
        PersonalAgentSnapshot previous;
        while (!stop.stop_requested())
        {
            const auto response = channel.request_with_recovery("personal.snapshot", nlohmann::json::object());
            std::string error;
            auto parsed = response.ok ? personal_agents_from_json(response.result, error) : std::nullopt;
            auto current = parsed.value_or(previous);
            if (!parsed)
                current.error = "Disconnected; last-known data only. " + (response.ok ? error : response.error_message);
            bool changed = false;
            {
                std::lock_guard lock(mutex_);
                changed = !snapshot_ || *snapshot_ != current;
                if (changed)
                    snapshot_ = std::make_shared<PersonalAgentSnapshot>(current);
            }
            previous = std::move(current);
            if (changed && wake)
                wake();
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, stop, std::chrono::seconds(1), [] { return false; });
        }
    });
}

PersonalAgentClient::~PersonalAgentClient()
{
    worker_.request_stop();
    wake_.notify_all();
}

std::shared_ptr<const PersonalAgentSnapshot> PersonalAgentClient::snapshot() const
{
    std::lock_guard lock(mutex_);
    return snapshot_;
}
}
