#pragma once
#include <draxul/personal_chat.h>
#include <nlohmann/json.hpp>
namespace draxul
{
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PersonalChatMessage, id, role, text)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PersonalChatSnapshot, agent_id, state, error, approval_id, approval_text, last_command_id, last_command_error, messages, revision, tool_count, schedule_checks, last_schedule_check)
}
