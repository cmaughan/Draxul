#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace draxul
{
struct PersonalChatMessage
{
    std::string id, role, text;
    bool operator==(const PersonalChatMessage&) const = default;
};
struct PersonalChatSnapshot
{
    std::string agent_id, state = "connecting", error;
    std::string approval_id, approval_text;
    std::string last_command_id, last_command_error;
    std::vector<PersonalChatMessage> messages;
    uint64_t revision = 0;
    unsigned tool_count = 0;
    uint64_t schedule_checks = 0;
    std::string last_schedule_check;
    bool operator==(const PersonalChatSnapshot&) const = default;
};
}
