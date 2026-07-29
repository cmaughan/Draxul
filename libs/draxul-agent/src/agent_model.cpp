#include <draxul/agent_model.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <unordered_map>

namespace draxul
{

std::string_view to_string(AgentIdentityOrigin value) noexcept
{
    switch (value)
    {
    case AgentIdentityOrigin::Managed:
        return "managed";
    case AgentIdentityOrigin::Discovered:
        return "discovered";
    }
    return "managed";
}

std::string_view to_string(AgentLifecycle value) noexcept
{
    switch (value)
    {
    case AgentLifecycle::Starting:
        return "starting";
    case AgentLifecycle::Running:
        return "running";
    case AgentLifecycle::Exited:
        return "exited";
    case AgentLifecycle::Failed:
        return "failed";
    }
    return "unknown";
}

std::string_view to_string(AgentStatus value) noexcept
{
    switch (value)
    {
    case AgentStatus::Unknown:
        return "unknown";
    case AgentStatus::Idle:
        return "idle";
    case AgentStatus::Working:
        return "working";
    case AgentStatus::Blocked:
        return "blocked";
    case AgentStatus::Done:
        return "done";
    }
    return "unknown";
}

std::string_view to_string(AgentStateAuthority value) noexcept
{
    switch (value)
    {
    case AgentStateAuthority::None:
        return "none";
    case AgentStateAuthority::DirectHost:
        return "direct_host";
    case AgentStateAuthority::ScreenManifest:
        return "screen_manifest";
    case AgentStateAuthority::OfficialIntegration:
        return "official_integration";
    }
    return "none";
}

std::string_view to_string(AgentRestorePolicy value) noexcept
{
    switch (value)
    {
    case AgentRestorePolicy::Fresh:
        return "fresh";
    case AgentRestorePolicy::ResumeIfAvailable:
        return "resume_if_available";
    case AgentRestorePolicy::ShellOnly:
        return "shell_only";
    }
    return "shell_only";
}

std::optional<AgentRestorePolicy> parse_agent_restore_policy(
    std::string_view value) noexcept
{
    if (value == "fresh")
        return AgentRestorePolicy::Fresh;
    if (value == "resume_if_available")
        return AgentRestorePolicy::ResumeIfAvailable;
    if (value == "shell_only")
        return AgentRestorePolicy::ShellOnly;
    return std::nullopt;
}

std::string_view to_string(AgentSessionRefKind value) noexcept
{
    switch (value)
    {
    case AgentSessionRefKind::Id:
        return "id";
    case AgentSessionRefKind::Path:
        return "path";
    }
    return "id";
}

std::optional<AgentSessionRefKind> parse_agent_session_ref_kind(
    std::string_view value) noexcept
{
    if (value == "id")
        return AgentSessionRefKind::Id;
    if (value == "path")
        return AgentSessionRefKind::Path;
    return std::nullopt;
}

bool is_official_agent_session_source(
    std::string_view source, std::string_view agent_kind) noexcept
{
    return (source == "draxul:codex" && agent_kind == "codex")
        || (source == "draxul:claude" && agent_kind == "claude");
}

bool validate_agent_session_ref(const AgentSessionRef& value, std::string* error)
{
    const auto invalid_text = [](std::string_view text, size_t limit) {
        return text.empty() || text.size() > limit
            || std::any_of(text.begin(), text.end(), [](unsigned char ch) {
                   return ch < 0x20 || ch == 0x7f;
               });
    };
    if (!is_official_agent_session_source(value.source, value.agent_kind))
    {
        if (error)
            *error = "Agent session reference source is not official.";
        return false;
    }
    if (value.integration_version == 0)
    {
        if (error)
            *error = "Agent session integration version is invalid.";
        return false;
    }
    if (invalid_text(value.value, 4096))
    {
        if (error)
            *error = "Agent session reference value is invalid.";
        return false;
    }
    if (value.kind == AgentSessionRefKind::Path)
    {
        const std::filesystem::path path(value.value);
        if (!path.is_absolute() || path.lexically_normal() != path)
        {
            if (error)
                *error = "Agent session path must be absolute and normalized.";
            return false;
        }
    }
    if (error)
        error->clear();
    return true;
}

std::optional<std::string> encode_agent_keys(
    const std::vector<std::string>& keys, std::string& error)
{
    if (keys.size() > 64)
    {
        error = "Agent input accepts at most 64 keys.";
        return std::nullopt;
    }
    static const std::unordered_map<std::string, std::string>
        known_keys{
            { "enter", "\r" },
            { "tab", "\t" },
            { "escape", "\x1b" },
            { "backspace", "\x7f" },
            { "up", "\x1b[A" },
            { "down", "\x1b[B" },
            { "right", "\x1b[C" },
            { "left", "\x1b[D" },
            { "home", "\x1b[H" },
            { "end", "\x1b[F" },
            { "delete", "\x1b[3~" },
            { "insert", "\x1b[2~" },
            { "pageup", "\x1b[5~" },
            { "pagedown", "\x1b[6~" },
            { "f1", "\x1bOP" },
            { "f2", "\x1bOQ" },
            { "f3", "\x1bOR" },
            { "f4", "\x1bOS" },
            { "f5", "\x1b[15~" },
            { "f6", "\x1b[17~" },
            { "f7", "\x1b[18~" },
            { "f8", "\x1b[19~" },
            { "f9", "\x1b[20~" },
            { "f10", "\x1b[21~" },
            { "f11", "\x1b[23~" },
            { "f12", "\x1b[24~" },
        };
    std::string bytes;
    for (std::string key : keys)
    {
        std::ranges::transform(key, key.begin(),
            [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
        const auto known = known_keys.find(key);
        if (known != known_keys.end())
            bytes += known->second;
        else if (key.size() == 6
            && key.starts_with("ctrl+")
            && key[5] >= 'a' && key[5] <= 'z')
        {
            bytes.push_back(
                static_cast<char>(key[5] - 'a' + 1));
        }
        else
        {
            error = "Unsupported key: " + key;
            return std::nullopt;
        }
    }
    error.clear();
    return bytes;
}

} // namespace draxul
