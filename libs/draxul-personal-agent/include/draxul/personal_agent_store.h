#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{

inline constexpr int kPersonalAgentSchemaVersion = 1;
inline constexpr size_t kPersonalAgentLimit = 32;
inline constexpr size_t kPersonalInstructionsLimit = 16384;

struct PersonalAgentDefinition
{
    std::string id;
    std::string name;
    std::string profile;
    std::string model;
    uint64_t revision = 0;
    bool enabled = false;
    int interval_seconds = 0;
    std::string instructions;
    std::string error;
    bool operator==(const PersonalAgentDefinition&) const = default;
};

struct PersonalAgentSnapshot
{
    std::string collection_id;
    std::string name;
    std::string root;
    std::string error;
    std::vector<PersonalAgentDefinition> agents;
    bool operator==(const PersonalAgentSnapshot&) const = default;
};

bool valid_personal_agent_id(std::string_view value);
PersonalAgentSnapshot load_personal_agents(const std::filesystem::path& root,
    const PersonalAgentSnapshot& previous = {});

}
