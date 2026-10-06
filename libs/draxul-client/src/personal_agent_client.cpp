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
            wake_.wait_for(lock, stop, std::chrono::milliseconds(250), [this] { return !commands_.empty(); });
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
