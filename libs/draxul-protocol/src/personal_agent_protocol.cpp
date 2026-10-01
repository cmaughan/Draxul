#include <draxul/personal_agent_protocol.h>

#include <set>
#include <stdexcept>

namespace draxul
{
nlohmann::json personal_agents_to_json(const PersonalAgentSnapshot& snapshot)
{
    nlohmann::json definitions = nlohmann::json::array();
    for (const auto& definition : snapshot.agents)
    {
        definitions.push_back({
            { "id", definition.id }, { "name", definition.name },
            { "profile", definition.profile }, { "model", definition.model },
            { "revision", definition.revision }, { "enabled", definition.enabled },
            { "interval_seconds", definition.interval_seconds },
            { "instructions", definition.instructions }, { "error", definition.error },
        });
    }
    return {
        { "schema_version", kPersonalAgentSchemaVersion },
        { "collection_id", snapshot.collection_id }, { "name", snapshot.name },
        { "root", snapshot.root }, { "error", snapshot.error },
        { "execution_enabled", false }, { "agents", std::move(definitions) },
    };
}

std::optional<PersonalAgentSnapshot> personal_agents_from_json(const nlohmann::json& value,
    std::string& error)
{
    try
    {
        if (!value.at("schema_version").is_number_integer()
            || value.at("schema_version") != kPersonalAgentSchemaVersion
            || value.at("execution_enabled") != false
            || !value.at("agents").is_array() || value.at("agents").size() > kPersonalAgentLimit)
            throw std::runtime_error("Invalid personal collection projection.");
        auto read_text = [](const nlohmann::json& object, const char* key, size_t limit) {
            auto text = object.at(key).get<std::string>();
            if (text.size() > limit || text.find('\0') != std::string::npos)
                throw std::runtime_error("Personal collection text exceeds its limits.");
            return text;
        };
        PersonalAgentSnapshot result;
        result.collection_id = read_text(value, "collection_id", 64);
        result.name = read_text(value, "name", 160);
        result.root = read_text(value, "root", 4096);
        result.error = read_text(value, "error", 512);
        std::set<std::string> seen;
        for (const auto& entry : value.at("agents"))
        {
            PersonalAgentDefinition definition;
            definition.id = read_text(entry, "id", 1024);
            definition.name = read_text(entry, "name", 1024);
            definition.profile = read_text(entry, "profile", 160);
            definition.model = read_text(entry, "model", 160);
            definition.instructions = read_text(entry, "instructions", kPersonalInstructionsLimit);
            definition.error = read_text(entry, "error", 512);
            if (!entry.at("revision").is_number_unsigned()
                || !entry.at("interval_seconds").is_number_integer()
                || !seen.insert(definition.id).second)
                throw std::runtime_error("Invalid personal definition revision or identity.");
            definition.revision = entry.at("revision").get<uint64_t>();
            definition.enabled = entry.at("enabled").get<bool>();
            definition.interval_seconds = entry.at("interval_seconds").get<int>();
            result.agents.push_back(std::move(definition));
        }
        error.clear();
        return result;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return std::nullopt;
    }
}
}
