#include <draxul/personal_agent_client.h>
#include <algorithm>
#include <draxul/client_recovery.h>
#include <draxul/personal_agent_protocol.h>
#include <draxul/personal_chat_protocol.h>

namespace draxul
{
PersonalAgentClient::PersonalAgentClient(ServerControlChannelOptions options, std::function<void()> wake)
{
    worker_ = std::jthread([this, options = std::move(options), wake = std::move(wake)](std::stop_token stop) {
        ServerControlChannel channel(options);
        PersonalAgentSnapshot previous;
        std::map<std::string,std::pair<std::string,std::string>> command_results;
        while (!stop.stop_requested())
        {
            nlohmann::json command;
            {
                std::lock_guard lock(mutex_);
                if (!commands_.empty()) { command = std::move(commands_.front()); commands_.pop_front(); }
            }
            if (!command.is_null())
            {
                const auto id = command.at("request_id").get<std::string>();
                // Never replay a mutation into a new server epoch after a lost response.
                auto reply = channel.request("personal.command", command);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
                while (reply.ok && !reply.result.value("done", false) && !stop.stop_requested()
                    && std::chrono::steady_clock::now() < deadline)
                {
                    std::unique_lock lock(mutex_);
                    wake_.wait_for(lock, stop, std::chrono::milliseconds(50), [] { return false; });
                    lock.unlock();
                    reply = channel.request_with_recovery("personal.result", {{"request_id", id}});
                }
                PersonalAgentCommandResult result;
                result.done = true;
                result.ok = reply.ok && reply.result.value("done", false) && reply.result.value("ok", false);
                result.message = result.ok ? reply.result.at("result").value("message", "Done.")
                    : reply.ok ? reply.result.value("error", "Result is unconfirmed. Reload before retrying.")
                    : "Result is unconfirmed: " + reply.error_message;
                if (result.ok && reply.result.at("result").contains("definition"))
                {
                    try { result.definition = personal_definition_from_json(reply.result["result"]["definition"]); }
                    catch (const std::exception& e) { result.ok=false; result.message=e.what(); }
                }
                {
                    std::lock_guard lock(mutex_);
                    results_[id] = std::move(result);
                }
                if (wake) wake();
            }
            std::deque<nlohmann::json> chat_commands;
            std::vector<std::string> chat_ids;
            {
                std::lock_guard lock(mutex_);
                chat_commands.swap(chat_commands_);
                for (const auto& [id, state] : chats_) chat_ids.push_back(id);
            }

            for (const auto& value : chat_commands)
            {
                const auto reply=channel.request(value.value("action","")=="reconnect"?"personal.chat.reconnect":"personal.chat.command",value);
                command_results[value.at("agent_id").get<std::string>()]={value.at("request_id").get<std::string>(),reply.ok ? "" : reply.error_message};
            }
            for (const auto& id : chat_ids)
            {
                // Open is idempotent: reconnecting the UI never resends a turn.
                const auto reply=channel.request_with_recovery("personal.chat.open",{{"agent_id",id}});
                PersonalChatSnapshot state;
                try
                {
                    if(!reply.ok) throw std::runtime_error(reply.error_message);
                    state=reply.result.get<PersonalChatSnapshot>();
                }
                catch(const std::exception& error)
                {
                    std::lock_guard lock(mutex_);
                    state=*chats_.at(id);
                    // The shared channel already reconnects in the background.
                    // Keep the conversation visible during a transport outage.
                    const bool temporary=!reply.ok && (is_transient_client_error(reply.error_code)
                        || is_resynchronizing_client_error(reply.error_code));
                    state.state=temporary?"connecting":"error"; state.error=temporary?"":error.what();
                }
                if(command_results.contains(id))
                { state.last_command_id=command_results.at(id).first;state.last_command_error=command_results.at(id).second; }
                bool updated=false;
                {
                    std::lock_guard lock(mutex_);
                    updated=*chats_.at(id)!=state;
                    if(updated) chats_[id]=std::make_shared<PersonalChatSnapshot>(std::move(state));
                }
                if(updated && wake) wake();
            }
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
                if(current.error.empty())
                    std::erase_if(chats_,[&](const auto& entry){return std::ranges::none_of(current.agents,[&](const auto& agent){return agent.id==entry.first;});});
                if (changed)
                    snapshot_ = std::make_shared<PersonalAgentSnapshot>(current);
            }
            previous = std::move(current);
            if (changed && wake)
                wake();
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, stop, std::chrono::milliseconds(250), [this] { return !commands_.empty() || !chat_commands_.empty(); });
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

namespace draxul
{
std::string PersonalAgentClient::submit(const PersonalAgentCommand& command)
{
    std::lock_guard lock(mutex_);
    if (!snapshot_ || !snapshot_->error.empty() || snapshot_->collection_id.empty() || commands_.size() >= 8)
        return {};
    const auto id = personal_unique_id();
    nlohmann::json params{{"request_id", id}, {"authority", snapshot_->authority},
        {"collection_id", snapshot_->collection_id}, {"action", command.action}, {"confirmed",command.confirmed},
        {"definition", personal_definition_to_json(command.definition)}};
    if (command.expected) params["expected"] = personal_definition_to_json(*command.expected);
    commands_.push_back(std::move(params));
    if (results_.size() >= 128)
        std::erase_if(results_, [](const auto& entry) { return entry.second.done; });
    results_[id] = {};
    wake_.notify_all();
    return id;
}
std::optional<PersonalAgentCommandResult> PersonalAgentClient::result(std::string_view id) const
{
    std::lock_guard lock(mutex_);
    const auto found = results_.find(std::string(id));
    return found == results_.end() ? std::nullopt : std::optional(found->second);
}
}

namespace draxul
{
std::shared_ptr<const PersonalChatSnapshot> PersonalAgentClient::chat(std::string_view id)
{
    if(!valid_personal_agent_id(id)) return {};
    std::lock_guard lock(mutex_);
    if(!chats_.contains(std::string(id)) && chats_.size()>=kPersonalAgentLimit) return {};
    auto& state=chats_[std::string(id)];
    if(!state) { auto initial=std::make_shared<PersonalChatSnapshot>(); initial->agent_id=id; state=initial; wake_.notify_all(); }
    return state;
}
std::string PersonalAgentClient::chat_command(std::string_view id,std::string_view action,std::string_view text,std::string_view approval_id)
{
    std::lock_guard lock(mutex_);
    if(!chats_.contains(std::string(id)) || chat_commands_.size()>=8) return {};
    const auto request_id=personal_unique_id();
    chat_commands_.push_back({{"agent_id",id},{"request_id",request_id},{"action",action},{"text",text},{"approval_id",approval_id}});
    wake_.notify_all(); return request_id;
}
}
