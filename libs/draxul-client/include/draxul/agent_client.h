#pragma once

#include <draxul/agent_protocol.h>

#include <filesystem>
#include <memory>
#include <string>

namespace draxul
{

class ClientRecoveryState;

struct AgentClientOptions
{
    std::filesystem::path runtime_directory;
    std::string client_id;
    std::string session_id = "default";
    std::shared_ptr<ClientRecoveryState> recovery;
};

class AgentClient
{
public:
    explicit AgentClient(AgentClientOptions options);

    bool refresh(std::string& error);
    bool poll(bool& changed, std::string& error);

    const ServerAgentSnapshot& snapshot() const noexcept
    {
        return snapshot_;
    }
    const std::string& last_error_code() const noexcept
    {
        return last_error_code_;
    }

private:
    bool request(std::string_view method, nlohmann::json params,
        nlohmann::json& result, std::string& error);

    AgentClientOptions options_;
    ServerAgentSnapshot snapshot_;
    std::string last_error_code_;
};

} // namespace draxul
