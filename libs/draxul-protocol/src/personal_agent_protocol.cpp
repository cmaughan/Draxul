#include <draxul/personal_agent_protocol.h>

#include <set>
#include <limits>
#include <draxul/unicode.h>
#include <stdexcept>

namespace draxul
{
nlohmann::json personal_agents_to_json(const PersonalAgentSnapshot& snapshot)
{
    nlohmann::json definitions = nlohmann::json::array();
    for (const auto& definition : snapshot.agents)
        definitions.push_back(personal_definition_to_json(definition));
    return {
        { "schema_version", kPersonalAgentSchemaVersion },
        { "authority", snapshot.authority }, { "collection_id", snapshot.collection_id }, { "name", snapshot.name },
        { "root", snapshot.root }, { "error", snapshot.error },
        { "profiles", snapshot.profiles }, { "agents", std::move(definitions) },
    };
}

std::optional<PersonalAgentSnapshot> personal_agents_from_json(const nlohmann::json& value,
    std::string& error)
{
    try
    {
        if (!value.at("schema_version").is_number_integer()
            || value.at("schema_version") != kPersonalAgentSchemaVersion
            || !value.at("agents").is_array() || value.at("agents").size() > kPersonalAgentLimit)
            throw std::runtime_error("Invalid personal collection projection.");
        auto read_text = [](const nlohmann::json& object, const char* key, size_t limit) {
            auto text = object.at(key).get<std::string>();
            if (text.size() > limit || text.find('\0') != std::string::npos)
                throw std::runtime_error("Personal collection text exceeds its limits.");
            return text;
        };
        PersonalAgentSnapshot result;
        result.authority = read_text(value, "authority", 64);
        result.collection_id = read_text(value, "collection_id", 64);
        result.name = read_text(value, "name", 160);
        result.root = read_text(value, "root", 4096);
        result.error = read_text(value, "error", 512);
        std::set<std::string> seen;
        for (const auto& entry : value.at("agents"))
        {
            auto definition = personal_definition_from_json(entry);
            if (!seen.insert(definition.id).second)
                throw std::runtime_error("Duplicate personal agent identity.");
            result.agents.push_back(std::move(definition));
        }
        result.profiles = value.at("profiles").get<std::vector<std::string>>();
        if (result.profiles.size() > 32) throw std::runtime_error("Too many profiles.");
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

namespace draxul
{
nlohmann::json personal_definition_to_json(const PersonalAgentDefinition& d)
{
    return {{"id", d.id}, {"name", d.name}, {"profile", d.profile},
        {"revision", d.revision}, {"instructions", d.instructions}, {"error", d.error}};
}
PersonalAgentDefinition personal_definition_from_json(const nlohmann::json& v)
{
    PersonalAgentDefinition d;
    d.id = v.at("id").get<std::string>();
    d.name = v.at("name").get<std::string>();
    d.profile = v.at("profile").get<std::string>();
    d.instructions = v.at("instructions").get<std::string>();
    d.error = v.at("error").get<std::string>();
    if (!v.at("revision").is_number_unsigned()) throw std::runtime_error("Invalid personal revision.");
    d.revision = v.at("revision").get<uint64_t>();
    if (d.id.size()>1024 || d.name.size()>160 || d.profile.size()>160
        || d.instructions.size()>kPersonalInstructionsLimit || d.error.size()>512)
        throw std::runtime_error("Personal metadata exceeds bounds.");
    for (const auto* text : {&d.id,&d.name,&d.profile,&d.instructions,&d.error})
        if (text->find('\0') != std::string::npos || utf8_validated_prefix_length(*text,text->size()) != text->size())
            throw std::runtime_error("Invalid personal metadata text.");
    return d;
}
}
