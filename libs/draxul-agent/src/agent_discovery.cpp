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
    // This matcher is a platform-neutral library: a macOS build still parses
    // Windows-shaped evidence (cmd.exe command text with backslash paths), and
    // std::filesystem::path only splits on the HOST's separators. Normalize
    // first — discovery is heuristic, so mis-splitting the vanishingly rare
    // POSIX filename that contains a literal backslash is an acceptable trade.
    std::string portable(value);
    std::replace(portable.begin(), portable.end(), '\\', '/');
    std::string name = lowercase(std::filesystem::path(portable).filename().string());
    for (const std::string_view suffix :
        { ".exe", ".cmd", ".bat", ".ps1", ".js" })
    {
        if (name.ends_with(suffix))
        {
            name.resize(name.size() - suffix.size());
            break;
        }
    }
    return name;
}

std::optional<AgentDiscoveryMatch> kind_from_name(std::string_view value)
{
    const std::string name = executable_name(value);
    const bool codex_platform_binary = name.starts_with("codex-")
        && (name.ends_with("-pc-windows-msvc")
            || name.ends_with("-apple-darwin")
            || name.ends_with("-unknown-linux-gnu")
            || name.ends_with("-unknown-linux-musl"));
    if (name == "codex" || name == "codex-cli" || codex_platform_binary)
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

std::optional<AgentDiscoveryMatch> kind_from_path_token(std::string_view value)
{
    while (!value.empty() && (value.front() == '"' || value.front() == '\''))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == '"' || value.back() == '\''))
        value.remove_suffix(1);
    if (value.empty() || value.starts_with("-"))
        return std::nullopt;

    auto match = kind_from_name(value);
    if (!match)
    {
        std::string portable = lowercase(std::string(value));
        std::replace(portable.begin(), portable.end(), '\\', '/');
        if (portable == "@openai/codex"
            || portable.ends_with("/@openai/codex/bin/codex.js")
            || portable.ends_with("/codex/bin/codex.js"))
        {
            match = AgentDiscoveryMatch{ "codex", "Codex", {}, true };
        }
        else if (portable == "@anthropic-ai/claude-code"
            || portable.find("/@anthropic-ai/claude-code/")
                != std::string::npos)
        {
            match = AgentDiscoveryMatch{ "claude", "Claude", {}, true };
        }
    }
    if (match)
        match->evidence_category = "structured_wrapper";
    return match;
}

std::optional<AgentDiscoveryMatch> kind_from_command_text(
    std::string_view command)
{
    while (!command.empty()
        && std::isspace(static_cast<unsigned char>(command.front())))
        command.remove_prefix(1);
    while (!command.empty())
    {
        if ((command.front() == '&' || command.front() == '.')
            && command.size() > 1
            && std::isspace(static_cast<unsigned char>(command[1])))
        {
            command.remove_prefix(1);
        }
        else if (command.size() >= 5
            && lowercase(std::string(command.substr(0, 5))) == "call ")
        {
            command.remove_prefix(4);
        }
        else
        {
            break;
        }
        while (!command.empty()
            && std::isspace(static_cast<unsigned char>(command.front())))
            command.remove_prefix(1);
    }
    if (command.empty())
        return std::nullopt;

    const char quote = command.front() == '"' || command.front() == '\'' ? command.front() : 0;
    if (quote != 0)
    {
        command.remove_prefix(1);
        const size_t end = command.find(quote);
        return kind_from_path_token(command.substr(0, end));
    }
    const size_t end = command.find_first_of(" \t");
    return kind_from_path_token(command.substr(0, end));
}

std::optional<AgentDiscoveryMatch> kind_from_wrapper(
    const AgentProcessInfo& process)
{
    const std::string wrapper = executable_name(process.executable);
    const auto& arguments = process.arguments;
    if (arguments.size() < 2)
        return std::nullopt;

    if (wrapper == "cmd")
    {
        for (size_t index = 1; index + 1 < arguments.size(); ++index)
        {
            const std::string flag = lowercase(arguments[index]);
            if (flag == "/c" || flag == "/k")
                return kind_from_command_text(arguments[index + 1]);
        }
        return std::nullopt;
    }

    if (wrapper == "powershell" || wrapper == "pwsh")
    {
        for (size_t index = 1; index < arguments.size(); ++index)
        {
            const std::string flag = lowercase(arguments[index]);
            if ((flag == "-file" || flag == "-f" || flag == "/file")
                && index + 1 < arguments.size())
                return kind_from_path_token(arguments[index + 1]);
            if ((flag == "-command" || flag == "-c" || flag == "/command"
                    || flag == "/c")
                && index + 1 < arguments.size())
                return kind_from_command_text(arguments[index + 1]);
            if (flag == "-encodedcommand" || flag == "-enc"
                || flag == "/encodedcommand" || flag == "/enc")
                return std::nullopt;
            if (!flag.starts_with("-") && !flag.starts_with("/"))
                return kind_from_path_token(arguments[index]);
        }
        return std::nullopt;
    }

    const bool node_runtime = wrapper == "node" || wrapper == "nodejs" || wrapper == "bun";
    const bool python_runtime = wrapper == "python" || wrapper == "python3";
    const bool shell_runtime = wrapper == "sh" || wrapper == "bash"
        || wrapper == "zsh" || wrapper == "fish";
    const bool package_runner = wrapper == "npm" || wrapper == "npx";
    if (!node_runtime && !python_runtime && !shell_runtime && !package_runner)
        return std::nullopt;

    for (size_t index = 1; index < arguments.size(); ++index)
    {
        const std::string& argument = arguments[index];
        const std::string flag = lowercase(argument);
        if (argument == "--")
        {
            return index + 1 < arguments.size()
                ? kind_from_path_token(arguments[index + 1])
                : std::nullopt;
        }
        if ((node_runtime
                && (flag == "-e" || flag == "--eval" || flag == "-p"
                    || flag == "--print"))
            || (python_runtime && (flag == "-c" || flag == "-m"))
            || (shell_runtime && flag == "-c"))
        {
            return std::nullopt;
        }
        if (package_runner && (flag == "exec" || flag == "run"))
            continue;
        if (flag.starts_with("-"))
            continue;
        return kind_from_path_token(argument);
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
        // argv[0] preserves the name the user invoked when exec() resolves a
        // symlink to a version-numbered binary.
        if (!candidate && !process.arguments.empty())
            candidate = kind_from_name(process.arguments.front());
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
