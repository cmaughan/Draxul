#pragma once

#include <draxul/personal_agent_store.h>
#include <nlohmann/json.hpp>
#include <optional>

namespace draxul
{
nlohmann::json personal_definition_to_json(const PersonalAgentDefinition& definition);
PersonalAgentDefinition personal_definition_from_json(const nlohmann::json& value);
nlohmann::json personal_agents_to_json(const PersonalAgentSnapshot& snapshot);
std::optional<PersonalAgentSnapshot> personal_agents_from_json(const nlohmann::json& value,
    std::string& error);
}
