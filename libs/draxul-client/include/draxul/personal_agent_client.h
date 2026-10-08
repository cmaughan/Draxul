#pragma once

#include <draxul/personal_agent_store.h>
#include <draxul/personal_chat.h>
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
    std::shared_ptr<const PersonalChatSnapshot> chat(std::string_view id);
    std::string chat_command(std::string_view id, std::string_view action, std::string_view text = {}, std::string_view approval_id = {});
    std::string submit(const PersonalAgentCommand& command);
    std::optional<PersonalAgentCommandResult> result(std::string_view id) const;

private:
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::shared_ptr<const PersonalAgentSnapshot> snapshot_;
    std::deque<nlohmann::json> commands_;
    std::map<std::string, PersonalAgentCommandResult> results_;
    std::map<std::string, std::shared_ptr<const PersonalChatSnapshot>> chats_;
    std::deque<nlohmann::json> chat_commands_;
    std::jthread worker_;
};
}
