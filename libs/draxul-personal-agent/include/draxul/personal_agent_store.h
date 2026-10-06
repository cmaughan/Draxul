#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace draxul
{

inline constexpr int kPersonalAgentSchemaVersion = 3;
inline constexpr size_t kPersonalAgentLimit = 32;
inline constexpr size_t kPersonalInstructionsLimit = 16384;

struct PersonalAgentDefinition
{
    std::string id;
    std::string name;
    std::string profile;
    uint64_t revision = 0;
    std::string instructions;
    std::string error;
    bool operator==(const PersonalAgentDefinition&) const = default;
};

struct PersonalAgentCommand
{
    std::string action;
    PersonalAgentDefinition definition;
    std::optional<PersonalAgentDefinition> expected;
    bool confirmed = false;
};
struct PersonalAgentCommandResult
{
    bool done = false;
    bool ok = false;
    std::string message;
    std::optional<PersonalAgentDefinition> definition;
};

struct PersonalAgentSnapshot
{
    std::string authority;
    std::string collection_id;
    std::string name;
    std::string root;
    std::string error;
    std::vector<PersonalAgentDefinition> agents;
    std::vector<std::string> profiles;
    bool operator==(const PersonalAgentSnapshot&) const = default;
};

bool valid_personal_agent_id(std::string_view value);
PersonalAgentSnapshot load_personal_agents(const std::filesystem::path& root,
    const PersonalAgentSnapshot& previous = {});

// The caller serializes writes. Expected includes instructions so external edits
// without a revision bump still conflict. A null expected means create-only.
PersonalAgentDefinition save_personal_agent(const std::filesystem::path& root,
    std::string_view collection_id, PersonalAgentDefinition candidate,
    const std::optional<PersonalAgentDefinition>& expected);
void delete_personal_agent(const std::filesystem::path& root, std::string_view collection_id,
    const PersonalAgentDefinition& expected);
void validate_personal_agent(const PersonalAgentDefinition& definition);
std::string personal_unique_id();
std::string personal_bootstrap_prompt(const std::filesystem::path& root, std::string_view id);
void personal_write_atomic(const std::filesystem::path& path, std::string_view text);
std::string personal_read_bounded(const std::filesystem::path& root,
    const std::filesystem::path& path, size_t limit);
}
