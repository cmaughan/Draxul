#include <draxul/agent_model.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>

namespace draxul
{

namespace
{

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string executable_name(std::string_view value)
{
    std::string name = lowercase(std::filesystem::path(value).filename().string());
    if (name.ends_with(".exe"))
        name.resize(name.size() - 4);
    return name;
}

std::optional<AgentDiscoveryMatch> kind_from_name(std::string_view value)
{
    const std::string name = executable_name(value);
    if (name == "codex" || name == "codex-cli")
        return AgentDiscoveryMatch{
            .kind = "codex",
            .display_name = "Codex",
            .evidence_category = "direct_executable",
            .high_confidence = true,
        };
    if (name == "claude" || name == "claude-code")
        return AgentDiscoveryMatch{
            .kind = "claude",
            .display_name = "Claude",
            .evidence_category = "direct_executable",
            .high_confidence = true,
        };
    return std::nullopt;
}

// Native installs put the real binary in a VERSIONED file and expose it through
// a name-bearing symlink — `~/.local/bin/claude` ->
// `~/.local/share/claude/versions/2.1.220`. exec() resolves the symlink, so the
// kernel reports the target: the executable's basename is "2.1.220", which
// carries no product identity at all. Recover it from the install layout, where
// the directory above `versions/` is the product name.
std::optional<AgentDiscoveryMatch> kind_from_versioned_install(std::string_view value)
{
    const std::filesystem::path path(value);
    const std::filesystem::path parent = path.parent_path();
    if (lowercase(parent.filename().string()) != "versions")
        return std::nullopt;
    const std::string product = lowercase(parent.parent_path().filename().string());
    if (product == "codex")
        return AgentDiscoveryMatch{
            .kind = "codex",
            .display_name = "Codex",
            .evidence_category = "versioned_install",
            .high_confidence = true,
        };
    if (product == "claude" || product == "claude-code")
        return AgentDiscoveryMatch{
            .kind = "claude",
            .display_name = "Claude",
            .evidence_category = "versioned_install",
            .high_confidence = true,
        };
    return std::nullopt;
}

std::optional<AgentDiscoveryMatch> kind_from_hint(std::string_view hint)
{
    const std::string normalized = lowercase(std::string(hint));
    if (normalized == "codex")
        return AgentDiscoveryMatch{ "codex", "Codex", "environment_hint", true };
    if (normalized == "claude" || normalized == "claude-code")
        return AgentDiscoveryMatch{ "claude", "Claude", "environment_hint", true };
    return std::nullopt;
}

std::optional<AgentDiscoveryMatch> kind_from_wrapper(
    const AgentProcessInfo& process)
{
    const std::string wrapper = executable_name(process.executable);
    if (wrapper != "node" && wrapper != "nodejs" && wrapper != "bun"
        && wrapper != "npm" && wrapper != "npx")
        return std::nullopt;

    for (const std::string& argument : process.arguments)
    {
        const std::string normalized = lowercase(argument);
        const std::string portable = [&] {
            std::string result = normalized;
            std::replace(result.begin(), result.end(), '\\', '/');
            return result;
        }();
        if (portable == "codex" || portable == "@openai/codex"
            || portable.ends_with("/@openai/codex/bin/codex.js")
            || portable.ends_with("/codex/bin/codex.js"))
        {
            return AgentDiscoveryMatch{
                "codex", "Codex", "structured_wrapper", true
            };
        }
        if (portable == "claude" || portable == "claude-code"
            || portable == "@anthropic-ai/claude-code"
            || portable.find("/@anthropic-ai/claude-code/") != std::string::npos)
        {
            return AgentDiscoveryMatch{
                "claude", "Claude", "structured_wrapper", true
            };
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<AgentDiscoveryMatch> discover_agent_process(
    const AgentProcessObservation& observation)
{
    std::optional<AgentDiscoveryMatch> result;
    std::vector<const AgentProcessInfo*> candidates;
    for (const AgentProcessInfo& process : observation.processes)
    {
        auto candidate = kind_from_hint(process.agent_hint);
        if (!candidate)
            candidate = kind_from_name(process.executable);
        if (!candidate)
        {
            // argv[0] is the name the user actually invoked ("claude"), which
            // survives when exec() resolved a symlink to a version-numbered
            // file and the executable path no longer names the product.
            if (!process.arguments.empty())
                candidate = kind_from_name(process.arguments.front());
        }
        if (!candidate)
            candidate = kind_from_versioned_install(process.executable);
        if (!candidate)
            candidate = kind_from_wrapper(process);
        if (!candidate)
            continue;
        if (result && result->kind != candidate->kind)
            return std::nullopt;
        candidates.push_back(&process);
        if (!result || candidate->evidence_category == "environment_hint")
            result = std::move(candidate);
    }
    if (result && !observation.foreground_reliable && candidates.size() > 1)
    {
        std::unordered_map<uint64_t, uint64_t> parents;
        for (const auto& process : observation.processes)
            parents[process.process_id] = process.parent_process_id;
        const auto is_ancestor = [&](uint64_t ancestor, uint64_t process) {
            for (size_t depth = 0; depth < 64 && process != 0; ++depth)
            {
                if (process == ancestor)
                    return true;
                const auto parent = parents.find(process);
                if (parent == parents.end() || parent->second == process)
                    return false;
                process = parent->second;
            }
            return false;
        };
        for (size_t left = 0; left < candidates.size(); ++left)
        {
            for (size_t right = left + 1; right < candidates.size(); ++right)
            {
                if (!is_ancestor(candidates[left]->process_id,
                        candidates[right]->process_id)
                    && !is_ancestor(candidates[right]->process_id,
                        candidates[left]->process_id))
                {
                    return std::nullopt;
                }
            }
        }
    }
    if (result)
        result->high_confidence = result->high_confidence && observation.foreground_reliable;
    return result;
}

} // namespace draxul
