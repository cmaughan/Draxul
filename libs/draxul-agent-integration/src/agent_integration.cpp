#include <draxul/agent_integration.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string_view>
#include <toml++/toml.hpp>
#include <vector>

namespace draxul
{

namespace
{

constexpr uint32_t kCodexIntegrationVersion = 2;
constexpr uint32_t kClaudeIntegrationVersion = 2;
#ifdef _WIN32
constexpr std::string_view kHookFileName = "draxul-agent-session.ps1";
constexpr std::string_view kCodexHook = R"HOOK(# managed by Draxul; reinstalling updates this file.
# DRAXUL_INTEGRATION_ID=codex
# DRAXUL_INTEGRATION_VERSION=2
param([string]$Action = "")
if ($Action -ne "session" -or $env:DRAXUL_ENV -ne "1") { exit 0 }
if ([string]::IsNullOrWhiteSpace($env:DRAXUL_PANE_ID) -or
    [string]::IsNullOrWhiteSpace($env:DRAXUL_AGENT_INSTANCE_ID) -or
    [string]::IsNullOrWhiteSpace($env:DRAXUL_SESSION_ID)) { exit 0 }
try { $payload = [Console]::In.ReadToEnd() | ConvertFrom-Json } catch { exit 0 }
if ($payload.hook_event_name -and $payload.hook_event_name -ne "SessionStart") { exit 0 }
$sessionRef = $payload.session_id
if ([string]::IsNullOrWhiteSpace($sessionRef)) { exit 0 }
$sequence = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
try {
  $reportArgs = @("pane", "report-agent-session", $env:DRAXUL_PANE_ID,
    "--agent-instance", $env:DRAXUL_AGENT_INSTANCE_ID, "--source", "draxul:codex",
    "--agent", "codex", "--integration-version", "2", "--sequence", "$sequence",
    "--session-ref", $sessionRef, "--session", $env:DRAXUL_SESSION_ID)
  if (-not [string]::IsNullOrWhiteSpace($env:DRAXUL_SERVER_EPOCH) -and
      -not [string]::IsNullOrWhiteSpace($env:DRAXUL_RUNTIME_GENERATION) -and
      -not [string]::IsNullOrWhiteSpace($env:DRAXUL_SERVER_RUNTIME_DIR)) {
    $reportArgs += @("--server-epoch", $env:DRAXUL_SERVER_EPOCH,
      "--runtime-generation", $env:DRAXUL_RUNTIME_GENERATION,
      "--server-runtime-dir", $env:DRAXUL_SERVER_RUNTIME_DIR)
  }
  & draxul @reportArgs 2>$null | Out-Null
} catch {}
)HOOK";
constexpr std::string_view kClaudeHook = R"HOOK(# managed by Draxul; reinstalling updates this file.
# DRAXUL_INTEGRATION_ID=claude
# DRAXUL_INTEGRATION_VERSION=2
param([string]$Action = "")
if ($Action -ne "session" -or $env:DRAXUL_ENV -ne "1") { exit 0 }
if ([string]::IsNullOrWhiteSpace($env:DRAXUL_PANE_ID) -or
    [string]::IsNullOrWhiteSpace($env:DRAXUL_AGENT_INSTANCE_ID) -or
    [string]::IsNullOrWhiteSpace($env:DRAXUL_SESSION_ID)) { exit 0 }
try { $payload = [Console]::In.ReadToEnd() | ConvertFrom-Json } catch { exit 0 }
if ($payload.agent_id -or
    ($payload.hook_event_name -and $payload.hook_event_name -ne "SessionStart")) {
  exit 0
}
$sessionRef = $payload.session_id
if ([string]::IsNullOrWhiteSpace($sessionRef)) { exit 0 }
$sequence = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
try {
  $reportArgs = @("pane", "report-agent-session", $env:DRAXUL_PANE_ID,
    "--agent-instance", $env:DRAXUL_AGENT_INSTANCE_ID, "--source", "draxul:claude",
    "--agent", "claude", "--integration-version", "2", "--sequence", "$sequence",
    "--session-ref", $sessionRef, "--ref-kind", "id",
    "--session", $env:DRAXUL_SESSION_ID)
  if (-not [string]::IsNullOrWhiteSpace($env:DRAXUL_SERVER_EPOCH) -and
      -not [string]::IsNullOrWhiteSpace($env:DRAXUL_RUNTIME_GENERATION) -and
      -not [string]::IsNullOrWhiteSpace($env:DRAXUL_SERVER_RUNTIME_DIR)) {
    $reportArgs += @("--server-epoch", $env:DRAXUL_SERVER_EPOCH,
      "--runtime-generation", $env:DRAXUL_RUNTIME_GENERATION,
      "--server-runtime-dir", $env:DRAXUL_SERVER_RUNTIME_DIR)
  }
  & draxul @reportArgs 2>$null | Out-Null
} catch {}
)HOOK";
#else
constexpr std::string_view kHookFileName = "draxul-agent-session.sh";
constexpr std::string_view kCodexHook = R"HOOK(#!/bin/sh
# managed by Draxul; reinstalling updates this file.
# DRAXUL_INTEGRATION_ID=codex
# DRAXUL_INTEGRATION_VERSION=2
[ "${1:-}" = "session" ] || exit 0
[ "${DRAXUL_ENV:-}" = "1" ] || exit 0
[ -n "${DRAXUL_PANE_ID:-}" ] || exit 0
[ -n "${DRAXUL_AGENT_INSTANCE_ID:-}" ] || exit 0
[ -n "${DRAXUL_SESSION_ID:-}" ] || exit 0
command -v python3 >/dev/null 2>&1 || exit 0
input_file="$(mktemp "${TMPDIR:-/tmp}/draxul-codex-hook.XXXXXX")" || exit 0
trap 'rm -f "$input_file"' EXIT HUP INT TERM
cat >"$input_file" 2>/dev/null || exit 0
python3 - "$DRAXUL_PANE_ID" "$DRAXUL_AGENT_INSTANCE_ID" "$DRAXUL_SESSION_ID" "$input_file" <<'PY'
import json, os, subprocess, sys, time
try:
    with open(sys.argv[4], encoding="utf-8") as handle:
        payload = json.load(handle)
except Exception:
    raise SystemExit(0)
if payload.get("hook_event_name") not in (None, "SessionStart"):
    raise SystemExit(0)
session_ref = payload.get("session_id")
if not isinstance(session_ref, str) or not session_ref:
    raise SystemExit(0)
command = ["draxul", "pane", "report-agent-session", sys.argv[1],
    "--agent-instance", sys.argv[2], "--source", "draxul:codex",
    "--agent", "codex", "--integration-version", "2",
    "--sequence", str(time.time_ns()), "--session-ref", session_ref,
    "--session", sys.argv[3]]
if all(os.environ.get(name) for name in (
    "DRAXUL_SERVER_EPOCH", "DRAXUL_RUNTIME_GENERATION",
    "DRAXUL_SERVER_RUNTIME_DIR")):
    command.extend(["--server-epoch", os.environ["DRAXUL_SERVER_EPOCH"],
        "--runtime-generation", os.environ["DRAXUL_RUNTIME_GENERATION"],
        "--server-runtime-dir", os.environ["DRAXUL_SERVER_RUNTIME_DIR"]])
subprocess.run(command, stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL, check=False)
PY
)HOOK";
constexpr std::string_view kClaudeHook = R"HOOK(#!/bin/sh
# managed by Draxul; reinstalling updates this file.
# DRAXUL_INTEGRATION_ID=claude
# DRAXUL_INTEGRATION_VERSION=2
[ "${1:-}" = "session" ] || exit 0
[ "${DRAXUL_ENV:-}" = "1" ] || exit 0
[ -n "${DRAXUL_PANE_ID:-}" ] || exit 0
[ -n "${DRAXUL_AGENT_INSTANCE_ID:-}" ] || exit 0
[ -n "${DRAXUL_SESSION_ID:-}" ] || exit 0
command -v python3 >/dev/null 2>&1 || exit 0
input_file="$(mktemp "${TMPDIR:-/tmp}/draxul-claude-hook.XXXXXX")" || exit 0
trap 'rm -f "$input_file"' EXIT HUP INT TERM
cat >"$input_file" 2>/dev/null || exit 0
python3 - "$DRAXUL_PANE_ID" "$DRAXUL_AGENT_INSTANCE_ID" "$DRAXUL_SESSION_ID" "$input_file" <<'PY'
import json, os, subprocess, sys, time
try:
    with open(sys.argv[4], encoding="utf-8") as handle:
        payload = json.load(handle)
except Exception:
    raise SystemExit(0)
if payload.get("agent_id") or payload.get("hook_event_name") not in (None, "SessionStart"):
    raise SystemExit(0)
session_ref = payload.get("session_id")
if not isinstance(session_ref, str) or not session_ref:
    raise SystemExit(0)
command = ["draxul", "pane", "report-agent-session", sys.argv[1],
    "--agent-instance", sys.argv[2], "--source", "draxul:claude",
    "--agent", "claude", "--integration-version", "2",
    "--sequence", str(time.time_ns()), "--session-ref", session_ref,
    "--ref-kind", "id", "--session", sys.argv[3]]
if all(os.environ.get(name) for name in (
    "DRAXUL_SERVER_EPOCH", "DRAXUL_RUNTIME_GENERATION",
    "DRAXUL_SERVER_RUNTIME_DIR")):
    command.extend(["--server-epoch", os.environ["DRAXUL_SERVER_EPOCH"],
        "--runtime-generation", os.environ["DRAXUL_RUNTIME_GENERATION"],
        "--server-runtime-dir", os.environ["DRAXUL_SERVER_RUNTIME_DIR"]])
subprocess.run(command,
    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
PY
)HOOK";
#endif

struct DocumentUpdate
{
    bool success = false;
    std::string contents;
    std::string error;
};

bool write_atomic(const std::filesystem::path& path,
    std::string_view contents, std::string& error)
{
    const auto temporary = path.string() + ".draxul.tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output || !(output << contents) || !output.flush())
        {
            error = "Unable to write " + path.filename().string() + ".";
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec)
    {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temporary, path, ec);
    }
    if (ec)
    {
        error = "Unable to replace " + path.filename().string() + ".";
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

std::string hook_command(const std::filesystem::path& hook_path)
{
#ifdef _WIN32
    return "powershell.exe -NoProfile -ExecutionPolicy Bypass -File \""
        + hook_path.string() + "\" session";
#else
    return "\"" + hook_path.string() + "\" session";
#endif
}

bool is_draxul_hook(const nlohmann::json& hook, const std::filesystem::path& path)
{
    if (!hook.is_object())
        return false;
    const auto type = hook.find("type");
    const auto command = hook.find("command");
    return type != hook.end() && type->is_string()
        && type->get_ref<const std::string&>() == "command"
        && command != hook.end() && command->is_string()
        && command->get_ref<const std::string&>().find(path.string())
        != std::string::npos;
}

DocumentUpdate transform_hook_document(std::string_view document,
    const std::filesystem::path& hook_path, AgentIntegrationProvider provider,
    bool install)
{
    nlohmann::json root = nlohmann::json::object();
    if (!document.empty())
    {
        root = nlohmann::json::parse(document, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            return { false, {}, provider == AgentIntegrationProvider::Codex ? "Codex hooks.json is not a JSON object." : "Claude settings.json is not a JSON object." };
        }
    }
    if (!root.contains("hooks"))
        root["hooks"] = nlohmann::json::object();
    if (!root["hooks"].is_object())
    {
        return { false, {}, provider == AgentIntegrationProvider::Codex ? "Codex hooks.json 'hooks' value is not an object." : "Claude settings.json 'hooks' value is not an object." };
    }
    auto& events = root["hooks"];
    if (!events.contains("SessionStart"))
        events["SessionStart"] = nlohmann::json::array();
    if (!events["SessionStart"].is_array())
    {
        return { false, {}, provider == AgentIntegrationProvider::Codex ? "Codex SessionStart hooks are not an array." : "Claude SessionStart hooks are not an array." };
    }
    auto& groups = events["SessionStart"];
    for (auto group_it = groups.begin(); group_it != groups.end();)
    {
        if (!group_it->is_object() || !group_it->contains("hooks")
            || !(*group_it)["hooks"].is_array())
        {
            ++group_it;
            continue;
        }
        auto& hooks = (*group_it)["hooks"];
        for (auto hook_it = hooks.begin(); hook_it != hooks.end();)
        {
            if (is_draxul_hook(*hook_it, hook_path))
                hook_it = hooks.erase(hook_it);
            else
                ++hook_it;
        }
        if (hooks.empty())
            group_it = groups.erase(group_it);
        else
            ++group_it;
    }
    if (install)
    {
        nlohmann::json group = {
            { "hooks",
                nlohmann::json::array({ {
                    { "type", "command" },
                    { "command", hook_command(hook_path) },
                    { "timeout", 10 },
                } }) },
        };
        if (provider == AgentIntegrationProvider::Claude)
            group["matcher"] = "*";
        groups.push_back(std::move(group));
    }
    return { true, root.dump(2) + "\n", {} };
}

bool is_features_header(std::string_view line)
{
    try
    {
        // Parsing this one line recognizes comments and quoted TOML keys, while
        // distinguishing [features] from [features.some_child].
        const auto header = toml::parse(line);
        const auto* features = header["features"].as_table();
        return header.size() == 1 && features && features->empty();
    }
    catch (const toml::parse_error&)
    {
        return false;
    }
}

DocumentUpdate transform_codex_features(std::string_view document)
{
    toml::table parsed;
    try
    {
        if (!document.empty())
            parsed = toml::parse(document);
    }
    catch (const toml::parse_error&)
    {
        return { false, {}, "Codex config.toml is invalid TOML." };
    }

    auto* features = parsed["features"].as_table();
    if (parsed.contains("features") && !features)
        return { false, {}, "Codex config.toml 'features' must be a table." };
    if (features && features->contains("hooks")
        && !(*features)["hooks"].is_boolean())
        return { false, {}, "Codex config.toml 'features.hooks' must be a boolean." };

    std::vector<std::string> lines;
    std::istringstream stream{ std::string(document) };
    for (std::string line; std::getline(stream, line);)
        lines.push_back(std::move(line));
    std::string updated;
    if (features && features->contains("hooks"))
    {
        const auto* hooks = features->get("hooks");
        if (hooks->value<bool>().value_or(false))
            return { true, std::string(document), {} };
        const auto& source = hooks->source().begin;
        if (source.line == 0 || source.line > lines.size()
            || source.column == 0 || source.column > lines[source.line - 1].size()
            || lines[source.line - 1].compare(source.column - 1, 5, "false") != 0)
        {
            return { false, {}, "Unable to locate Codex features.hooks value." };
        }
        lines[source.line - 1].replace(source.column - 1, 5, "true");
    }
    else if (!features)
    {
        if (!lines.empty() && !lines.back().empty())
            lines.emplace_back();
        lines.emplace_back("[features]");
        lines.emplace_back("hooks = true");
    }
    else
    {
        const auto& source = features->source().begin;
        if (source.line > 0 && source.line <= lines.size()
            && is_features_header(lines[source.line - 1]))
        {
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(source.line),
                "hooks = true");
        }
        else
        {
            // An inline or implicit table has no standalone [features] header
            // into which an assignment can be inserted. Reprint the parsed tree
            // to preserve every unrelated value while producing valid TOML.
            features->insert_or_assign("hooks", true);
            std::ostringstream output;
            output << parsed << '\n';
            updated = output.str();
        }
    }
    if (updated.empty())
    {
        std::ostringstream output;
        for (const auto& line : lines)
            output << line << '\n';
        updated = output.str();
    }
    try
    {
        const auto checked = toml::parse(updated);
        const auto* checked_features = checked["features"].as_table();
        if (checked_features && (*checked_features)["hooks"].value<bool>() == true)
            return { true, updated, {} };
    }
    catch (const toml::parse_error&)
    {
    }
    return { false, {}, "Unable to produce a valid Codex config.toml." };
}

bool read_optional_document(const std::filesystem::path& path,
    std::string& contents, std::string& error)
{
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec)
    {
        error = "Unable to inspect " + path.filename().string() + ".";
        return false;
    }
    if (!exists)
    {
        contents.clear();
        return true;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        error = "Unable to read " + path.filename().string() + ".";
        return false;
    }
    contents.assign(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    if (input.bad())
    {
        error = "Unable to read " + path.filename().string() + ".";
        return false;
    }
    return true;
}

bool update_registration(const AgentIntegrationRequest& request,
    bool install, std::string& error)
{
    std::string document;
    if (!read_optional_document(request.paths.registration, document, error))
        return false;
    const auto update = transform_hook_document(document, request.paths.hook,
        request.provider, install);
    if (!update.success)
    {
        error = update.error;
        return false;
    }
    return write_atomic(request.paths.registration, update.contents, error);
}

bool enable_codex_hooks(const std::filesystem::path& config_path,
    std::string& error)
{
    std::string document;
    if (!read_optional_document(config_path, document, error))
        return false;
    const auto update = transform_codex_features(document);
    if (!update.success)
    {
        error = update.error;
        return false;
    }
    return write_atomic(config_path, update.contents, error);
}

uint32_t expected_version(AgentIntegrationProvider provider)
{
    return provider == AgentIntegrationProvider::Codex
        ? kCodexIntegrationVersion
        : kClaudeIntegrationVersion;
}

AgentIntegrationStatus make_status(AgentIntegrationProvider provider,
    AgentIntegrationState state, std::filesystem::path path = {},
    std::string reason = {}, bool with_version = false)
{
    return {
        .provider = provider,
        .state = state,
        .expected_version = with_version ? expected_version(provider) : 0,
        .path = std::move(path),
        .reason = std::move(reason),
    };
}

AgentIntegrationStatus inspect_codex(const AgentIntegrationPaths& paths)
{
    std::error_code ec;
    const bool directory_exists = std::filesystem::exists(paths.directory, ec);
    if (ec)
        return make_status(AgentIntegrationProvider::Codex,
            AgentIntegrationState::Invalid, {},
            "Codex config directory cannot be inspected.");
    if (!directory_exists)
        return make_status(AgentIntegrationProvider::Codex,
            AgentIntegrationState::Unavailable, paths.directory);
    const bool hook_exists = std::filesystem::exists(paths.hook, ec);
    if (ec)
        return make_status(AgentIntegrationProvider::Codex,
            AgentIntegrationState::Invalid, paths.hook,
            "Codex hook cannot be inspected.");
    if (!hook_exists)
        return make_status(AgentIntegrationProvider::Codex,
            AgentIntegrationState::NotInstalled, paths.hook);

    std::string hook;
    std::string read_error;
    if (!read_optional_document(paths.hook, hook, read_error))
        return make_status(AgentIntegrationProvider::Codex,
            AgentIntegrationState::Invalid, paths.hook, std::move(read_error));
    const std::string marker = "DRAXUL_INTEGRATION_VERSION="
        + std::to_string(kCodexIntegrationVersion);
    if (hook.find(marker) == std::string::npos)
        return make_status(AgentIntegrationProvider::Codex,
            AgentIntegrationState::Outdated, paths.hook, {}, true);

    bool registered = false;
    std::string document;
    if (!read_optional_document(paths.registration, document, read_error))
        return make_status(AgentIntegrationProvider::Codex,
            AgentIntegrationState::Invalid, paths.registration,
            std::move(read_error));
    if (!document.empty())
    {
        const auto root = nlohmann::json::parse(document, nullptr, false);
        if (root.is_discarded() || !root.is_object())
            return make_status(AgentIntegrationProvider::Codex,
                AgentIntegrationState::Invalid, paths.registration,
                "Codex hooks.json is invalid.");
        const auto session_start = root.contains("hooks") && root["hooks"].is_object()
                && root["hooks"].contains("SessionStart")
            ? &root["hooks"]["SessionStart"]
            : nullptr;
        if (session_start && session_start->is_array())
        {
            for (const auto& group : *session_start)
            {
                if (!group.is_object() || !group.contains("hooks")
                    || !group["hooks"].is_array())
                    continue;
                registered = std::any_of(group["hooks"].begin(),
                    group["hooks"].end(), [&](const nlohmann::json& entry) {
                        return is_draxul_hook(entry, paths.hook);
                    });
                if (registered)
                    break;
            }
        }
    }

    bool enabled = false;
    if (!read_optional_document(paths.features, document, read_error))
        return make_status(AgentIntegrationProvider::Codex,
            AgentIntegrationState::Invalid, paths.features,
            std::move(read_error));
    try
    {
        const toml::table config = document.empty() ? toml::table{} : toml::parse(document);
        if (const auto* features = config["features"].as_table())
            enabled = (*features)["hooks"].value<bool>().value_or(false);
    }
    catch (const toml::parse_error&)
    {
        return make_status(AgentIntegrationProvider::Codex,
            AgentIntegrationState::Invalid, paths.features,
            "Codex config.toml is invalid TOML.");
    }
    return make_status(AgentIntegrationProvider::Codex,
        registered && enabled ? AgentIntegrationState::Current
                              : AgentIntegrationState::Outdated,
        paths.hook, {}, true);
}

AgentIntegrationStatus inspect_claude(const AgentIntegrationPaths& paths)
{
    std::error_code ec;
    const bool directory_exists = std::filesystem::exists(paths.directory, ec);
    if (ec)
        return make_status(AgentIntegrationProvider::Claude,
            AgentIntegrationState::Invalid, {},
            "Claude config directory cannot be inspected.");
    if (!directory_exists)
        return make_status(AgentIntegrationProvider::Claude,
            AgentIntegrationState::Unavailable, paths.directory);
    const bool hook_exists = std::filesystem::exists(paths.hook, ec);
    if (ec)
        return make_status(AgentIntegrationProvider::Claude,
            AgentIntegrationState::Invalid, paths.hook,
            "Claude hook cannot be inspected.");
    if (!hook_exists)
        return make_status(AgentIntegrationProvider::Claude,
            AgentIntegrationState::NotInstalled, paths.hook);

    std::string hook;
    std::string read_error;
    if (!read_optional_document(paths.hook, hook, read_error))
        return make_status(AgentIntegrationProvider::Claude,
            AgentIntegrationState::Invalid, paths.hook, std::move(read_error));
    const std::string marker = "DRAXUL_INTEGRATION_VERSION="
        + std::to_string(kClaudeIntegrationVersion);
    if (hook.find("DRAXUL_INTEGRATION_ID=claude") == std::string::npos
        || hook.find(marker) == std::string::npos)
        return make_status(AgentIntegrationProvider::Claude,
            AgentIntegrationState::Outdated, paths.hook, {}, true);

    bool registered = false;
    std::string document;
    if (!read_optional_document(paths.registration, document, read_error))
        return make_status(AgentIntegrationProvider::Claude,
            AgentIntegrationState::Invalid, paths.registration,
            std::move(read_error));
    if (!document.empty())
    {
        const auto root = nlohmann::json::parse(document, nullptr, false);
        if (root.is_discarded() || !root.is_object())
            return make_status(AgentIntegrationProvider::Claude,
                AgentIntegrationState::Invalid, paths.registration,
                "Claude settings.json is invalid.");
        const auto session_start = root.contains("hooks") && root["hooks"].is_object()
                && root["hooks"].contains("SessionStart")
            ? &root["hooks"]["SessionStart"]
            : nullptr;
        if (session_start && session_start->is_array())
        {
            for (const auto& group : *session_start)
            {
                if (!group.is_object() || !group.contains("hooks")
                    || !group["hooks"].is_array())
                    continue;
                registered = std::any_of(group["hooks"].begin(),
                    group["hooks"].end(), [&](const nlohmann::json& entry) {
                        return is_draxul_hook(entry, paths.hook);
                    });
                if (registered)
                    break;
            }
        }
    }
    return make_status(AgentIntegrationProvider::Claude,
        registered ? AgentIntegrationState::Current
                   : AgentIntegrationState::Outdated,
        paths.hook, {}, true);
}

bool remove_owned_hook(AgentIntegrationProvider provider,
    const std::filesystem::path& path, std::string& error)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return !ec;
    std::string content;
    if (!read_optional_document(path, content, error))
        return false;
    const std::string marker = std::string("DRAXUL_INTEGRATION_ID=")
        + agent_integration_provider_name(provider);
    if (content.find(marker) == std::string::npos)
    {
        error = "Refusing to remove a hook not owned by Draxul.";
        return false;
    }
    std::filesystem::remove(path, ec);
    if (ec)
    {
        error = std::string("Unable to remove the Draxul ")
            + (provider == AgentIntegrationProvider::Codex ? "Codex" : "Claude")
            + " hook.";
        return false;
    }
    return true;
}

bool install_integration(const AgentIntegrationRequest& request,
    std::string& error)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(request.paths.directory, ec) || ec)
    {
        error = request.provider == AgentIntegrationProvider::Codex
            ? "Codex config directory was not found. Install Codex first."
            : "Claude config directory was not found. Install Claude first.";
        return false;
    }
    if (request.provider == AgentIntegrationProvider::Claude)
    {
        std::filesystem::create_directories(request.paths.hook.parent_path(), ec);
        if (ec)
        {
            error = "Unable to create the Claude hooks directory.";
            return false;
        }
    }
    const std::string_view hook = request.provider == AgentIntegrationProvider::Codex
        ? kCodexHook
        : kClaudeHook;
    if (!write_atomic(request.paths.hook, hook, error)
        || !update_registration(request, true, error)
        || (request.provider == AgentIntegrationProvider::Codex
            && !enable_codex_hooks(request.paths.features, error)))
        return false;
#ifndef _WIN32
    std::filesystem::permissions(request.paths.hook,
        std::filesystem::perms::owner_exec
            | std::filesystem::perms::owner_read
            | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::add, ec);
    if (ec)
    {
        error = request.provider == AgentIntegrationProvider::Codex
            ? "Unable to mark the Codex hook executable."
            : "Unable to mark the Claude hook executable.";
        return false;
    }
#endif
    return true;
}

bool uninstall_integration(const AgentIntegrationRequest& request,
    std::string& error)
{
    std::error_code ec;
    if (std::filesystem::exists(request.paths.registration, ec))
    {
        if (ec || !update_registration(request, false, error))
            return false;
    }
    return remove_owned_hook(request.provider, request.paths.hook, error);
}

} // namespace

AgentIntegrationPaths agent_integration_paths(
    AgentIntegrationProvider provider,
    const std::filesystem::path& configuration_directory)
{
    const auto hook_directory = provider == AgentIntegrationProvider::Claude
        ? configuration_directory / "hooks"
        : configuration_directory;
    return {
        .directory = configuration_directory,
        .hook = hook_directory / kHookFileName,
        .registration = configuration_directory
            / (provider == AgentIntegrationProvider::Codex
                    ? "hooks.json"
                    : "settings.json"),
        .features = provider == AgentIntegrationProvider::Codex
            ? configuration_directory / "config.toml"
            : std::filesystem::path{},
    };
}

AgentIntegrationStatus inspect_agent_integration(
    AgentIntegrationProvider provider, const AgentIntegrationPaths& paths)
{
    return provider == AgentIntegrationProvider::Codex
        ? inspect_codex(paths)
        : inspect_claude(paths);
}

AgentIntegrationResult apply_agent_integration(
    const AgentIntegrationRequest& request)
{
    std::string error;
    const bool success = request.action == AgentIntegrationAction::Install
        ? install_integration(request, error)
        : uninstall_integration(request, error);
    return {
        .success = success,
        .status = inspect_agent_integration(request.provider, request.paths),
        .error = std::move(error),
    };
}

const char* agent_integration_provider_name(AgentIntegrationProvider provider)
{
    return provider == AgentIntegrationProvider::Codex ? "codex" : "claude";
}

const char* agent_integration_state_name(AgentIntegrationState state)
{
    switch (state)
    {
    case AgentIntegrationState::Unavailable:
        return "unavailable";
    case AgentIntegrationState::NotInstalled:
        return "not_installed";
    case AgentIntegrationState::Current:
        return "current";
    case AgentIntegrationState::Outdated:
        return "outdated";
    case AgentIntegrationState::Invalid:
        return "invalid";
    }
    return "invalid";
}

} // namespace draxul
