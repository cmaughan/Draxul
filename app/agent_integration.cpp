#include "agent_integration.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string_view>
#include <vector>

namespace draxul
{

namespace
{

constexpr uint32_t kCodexIntegrationVersion = 1;
#ifdef _WIN32
constexpr std::string_view kHookFileName = "draxul-agent-session.ps1";
constexpr std::string_view kCodexHook = R"HOOK(# managed by Draxul; reinstalling updates this file.
# DRAXUL_INTEGRATION_ID=codex
# DRAXUL_INTEGRATION_VERSION=1
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
  & draxul pane report-agent-session $env:DRAXUL_PANE_ID `
    --agent-instance $env:DRAXUL_AGENT_INSTANCE_ID --source draxul:codex `
    --agent codex --integration-version 1 --sequence $sequence `
    --session-ref $sessionRef --session $env:DRAXUL_SESSION_ID 2>$null | Out-Null
} catch {}
)HOOK";
#else
constexpr std::string_view kHookFileName = "draxul-agent-session.sh";
constexpr std::string_view kCodexHook = R"HOOK(#!/bin/sh
# managed by Draxul; reinstalling updates this file.
# DRAXUL_INTEGRATION_ID=codex
# DRAXUL_INTEGRATION_VERSION=1
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
import json, subprocess, sys, time
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
subprocess.run(["draxul", "pane", "report-agent-session", sys.argv[1],
    "--agent-instance", sys.argv[2], "--source", "draxul:codex",
    "--agent", "codex", "--integration-version", "1",
    "--sequence", str(time.time_ns()), "--session-ref", session_ref,
    "--session", sys.argv[3]], stdout=subprocess.DEVNULL,
    stderr=subprocess.DEVNULL, check=False)
PY
)HOOK";
#endif

std::optional<std::filesystem::path> codex_directory()
{
    if (const char* explicit_home = std::getenv("CODEX_HOME");
        explicit_home && *explicit_home)
        return std::filesystem::path(explicit_home);
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (!home || !*home)
        return std::nullopt;
    return std::filesystem::path(home) / ".codex";
}

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
    return hook.is_object() && hook.value("type", "") == "command"
        && hook.value("command", "").find(path.string()) != std::string::npos;
}

bool update_hooks_file(const std::filesystem::path& hooks_path,
    const std::filesystem::path& hook_path, bool install, std::string& error)
{
    nlohmann::json root = nlohmann::json::object();
    if (std::filesystem::exists(hooks_path))
    {
        std::ifstream input(hooks_path);
        root = nlohmann::json::parse(input, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            error = "Codex hooks.json is not a JSON object.";
            return false;
        }
    }
    if (!root.contains("hooks"))
        root["hooks"] = nlohmann::json::object();
    if (!root["hooks"].is_object())
    {
        error = "Codex hooks.json 'hooks' value is not an object.";
        return false;
    }
    auto& events = root["hooks"];
    if (!events.contains("SessionStart"))
        events["SessionStart"] = nlohmann::json::array();
    if (!events["SessionStart"].is_array())
    {
        error = "Codex SessionStart hooks are not an array.";
        return false;
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
        groups.push_back({
            { "hooks",
                nlohmann::json::array({ {
                    { "type", "command" },
                    { "command", hook_command(hook_path) },
                    { "timeout", 10 },
                } }) },
        });
    }
    return write_atomic(hooks_path, root.dump(2) + "\n", error);
}

bool enable_codex_hooks(const std::filesystem::path& config_path,
    std::string& error)
{
    std::string content;
    if (std::filesystem::exists(config_path))
    {
        std::ifstream input(config_path);
        content.assign(std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    }
    std::vector<std::string> lines;
    std::istringstream stream(content);
    for (std::string line; std::getline(stream, line);)
        lines.push_back(std::move(line));
    bool in_features = false;
    bool saw_features = false;
    bool saw_hooks = false;
    for (auto& line : lines)
    {
        const size_t first = line.find_first_not_of(" \t");
        const std::string trimmed =
            first == std::string::npos ? std::string{} : line.substr(first);
        if (trimmed.starts_with("[") && trimmed.ends_with("]"))
        {
            in_features = trimmed == "[features]";
            saw_features = saw_features || in_features;
        }
        else if (in_features && trimmed.starts_with("hooks")
            && trimmed.find('=') != std::string::npos)
        {
            line = "hooks = true";
            saw_hooks = true;
        }
    }
    if (!saw_features)
    {
        if (!lines.empty() && !lines.back().empty())
            lines.emplace_back();
        lines.emplace_back("[features]");
        lines.emplace_back("hooks = true");
    }
    else if (!saw_hooks)
    {
        const auto features = std::find(lines.begin(), lines.end(), "[features]");
        lines.insert(features + 1, "hooks = true");
    }
    std::ostringstream output;
    for (const auto& line : lines)
        output << line << '\n';
    return write_atomic(config_path, output.str(), error);
}

nlohmann::json codex_status()
{
    const auto directory = codex_directory();
    if (!directory)
        return { { "target", "codex" }, { "state", "invalid" },
            { "reason", "Codex home cannot be resolved." } };
    const auto hook_path = *directory / kHookFileName;
    if (!std::filesystem::exists(*directory))
        return { { "target", "codex" }, { "state", "unavailable" },
            { "path", directory->string() } };
    if (!std::filesystem::exists(hook_path))
        return { { "target", "codex" }, { "state", "not_installed" },
            { "path", hook_path.string() } };
    std::ifstream input(hook_path);
    const std::string hook((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    const std::string marker =
        "DRAXUL_INTEGRATION_VERSION=" + std::to_string(kCodexIntegrationVersion);
    if (hook.find(marker) == std::string::npos)
    {
        return { { "target", "codex" }, { "state", "outdated" },
            { "expected_version", kCodexIntegrationVersion },
            { "path", hook_path.string() } };
    }

    bool registered = false;
    const auto hooks_path = *directory / "hooks.json";
    if (std::filesystem::exists(hooks_path))
    {
        std::ifstream hooks_input(hooks_path);
        const auto root = nlohmann::json::parse(hooks_input, nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            return { { "target", "codex" }, { "state", "invalid" },
                { "reason", "Codex hooks.json is invalid." },
                { "path", hooks_path.string() } };
        }
        const auto session_start =
            root.contains("hooks") && root["hooks"].is_object()
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
                        return is_draxul_hook(entry, hook_path);
                    });
                if (registered)
                    break;
            }
        }
    }

    bool enabled = false;
    const auto config_path = *directory / "config.toml";
    if (std::filesystem::exists(config_path))
    {
        std::ifstream config_input(config_path);
        std::string line;
        bool in_features = false;
        while (std::getline(config_input, line))
        {
            const size_t first = line.find_first_not_of(" \t");
            const std::string trimmed =
                first == std::string::npos ? std::string{} : line.substr(first);
            if (trimmed.starts_with("[") && trimmed.ends_with("]"))
                in_features = trimmed == "[features]";
            else if (in_features && trimmed.starts_with("hooks")
                && trimmed.find('=') != std::string::npos)
            {
                const auto equals = trimmed.find('=');
                const auto value = trimmed.substr(equals + 1);
                enabled = value.find("true") != std::string::npos;
            }
        }
    }
    return {
        { "target", "codex" },
        { "state", registered && enabled ? "current" : "outdated" },
        { "expected_version", kCodexIntegrationVersion },
        { "path", hook_path.string() },
    };
}

} // namespace

ParseIntegrationCliResult parse_integration_cli(
    const std::vector<std::string>& args)
{
    ParseIntegrationCliResult result;
    if (args.size() < 2 || args[1] != "integration")
        return result;
    result.recognized = true;
    if (args.size() < 3
        || (args[2] != "status" && args[2] != "install"
            && args[2] != "uninstall"))
    {
        result.error =
            "Usage: draxul integration <status|install|uninstall> [codex] [--json]";
        return result;
    }
    IntegrationCliCommand command;
    command.action = args[2];
    size_t position = 3;
    if (position < args.size() && !args[position].starts_with("--"))
        command.target = args[position++];
    if (command.action != "status" && command.target.empty())
    {
        result.error = "Integration install/uninstall requires a target.";
        return result;
    }
    if (!command.target.empty() && command.target != "codex")
    {
        result.error = "Only the Codex integration is currently supported.";
        return result;
    }
    while (position < args.size())
    {
        if (args[position++] == "--json")
            command.json = true;
        else
        {
            result.error = "Unknown integration option.";
            return result;
        }
    }
    result.command = std::move(command);
    return result;
}

int run_integration_cli(const IntegrationCliCommand& command)
{
    std::string error;
    if (command.action == "install")
    {
        const auto directory = codex_directory();
        if (!directory || !std::filesystem::is_directory(*directory))
            error = "Codex config directory was not found. Install Codex first.";
        else
        {
            const auto hook_path = *directory / kHookFileName;
            if (!write_atomic(hook_path, kCodexHook, error)
                || !update_hooks_file(
                    *directory / "hooks.json", hook_path, true, error)
                || !enable_codex_hooks(*directory / "config.toml", error))
            {
                // error populated by the failed operation
            }
#ifndef _WIN32
            else
            {
                std::error_code ec;
                std::filesystem::permissions(hook_path,
                    std::filesystem::perms::owner_exec
                        | std::filesystem::perms::owner_read
                        | std::filesystem::perms::owner_write,
                    std::filesystem::perm_options::add, ec);
                if (ec)
                    error = "Unable to mark the Codex hook executable.";
            }
#endif
        }
    }
    else if (command.action == "uninstall")
    {
        const auto directory = codex_directory();
        if (!directory)
            error = "Codex home cannot be resolved.";
        else
        {
            const auto hook_path = *directory / kHookFileName;
            const auto hooks_path = *directory / "hooks.json";
            if (std::filesystem::exists(hooks_path)
                && !update_hooks_file(hooks_path, hook_path, false, error))
            {
                // error populated
            }
            else if (std::filesystem::exists(hook_path))
            {
                std::string content;
                {
                    std::ifstream input(hook_path);
                    content.assign(std::istreambuf_iterator<char>(input),
                        std::istreambuf_iterator<char>());
                }
                if (content.find("DRAXUL_INTEGRATION_ID=codex")
                    == std::string::npos)
                    error = "Refusing to remove a hook not owned by Draxul.";
                else
                {
                    std::error_code ec;
                    std::filesystem::remove(hook_path, ec);
                    if (ec)
                        error = "Unable to remove the Draxul Codex hook.";
                }
            }
        }
    }
    const auto status = codex_status();
    if (!error.empty())
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    if (command.json)
        std::printf("%s\n", status.dump(2).c_str());
    else
        std::printf("codex: %s\n", status.value("state", "unknown").c_str());
    return 0;
}

} // namespace draxul
