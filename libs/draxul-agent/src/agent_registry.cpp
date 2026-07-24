#include <draxul/agent_model.h>

#include <algorithm>

namespace draxul
{

AgentDefinitionRegistry::AgentDefinitionRegistry()
{
    register_definition({
        .profile_id = "codex",
        .kind = "codex",
        .display_name = "Codex",
        .executable = "codex",
    });
    register_definition({
        .profile_id = "claude",
        .kind = "claude",
        .display_name = "Claude",
        .executable = "claude",
    });
}

bool AgentDefinitionRegistry::register_definition(AgentDefinition definition)
{
    if (definition.profile_id.empty() || definition.kind.empty()
        || definition.display_name.empty() || definition.executable.empty())
    {
        return false;
    }

    auto existing = std::find_if(definitions_.begin(), definitions_.end(),
        [&](const AgentDefinition& value) {
            return value.profile_id == definition.profile_id;
        });
    if (existing != definitions_.end())
        *existing = std::move(definition);
    else
        definitions_.push_back(std::move(definition));
    return true;
}

const AgentDefinition* AgentDefinitionRegistry::find(std::string_view profile_id) const
{
    const auto found = std::find_if(definitions_.begin(), definitions_.end(),
        [&](const AgentDefinition& definition) {
            return definition.profile_id == profile_id;
        });
    return found == definitions_.end() ? nullptr : &*found;
}

} // namespace draxul
