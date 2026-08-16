#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace draxul
{

namespace detail
{

// Quotes one argument for a Windows command line so CommandLineToArgvW /
// the MSVC CRT reproduce it verbatim: empty arguments and arguments with
// whitespace or quotes are wrapped in quotes, embedded quotes are escaped,
// and backslash runs before a quote (or the closing quote) are doubled.
template <typename Char>
std::basic_string<Char> quote_windows_arg_impl(std::basic_string_view<Char> arg)
{
    bool needs_quotes = arg.empty();
    for (const Char ch : arg)
    {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\v' || ch == '"')
        {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes)
        return std::basic_string<Char>(arg);

    std::basic_string<Char> quoted;
    quoted.push_back('"');
    size_t pending_backslashes = 0;
    for (const Char ch : arg)
    {
        if (ch == '\\')
        {
            ++pending_backslashes;
            continue;
        }

        if (ch == '"')
            quoted.append(pending_backslashes * 2 + 1, static_cast<Char>('\\'));
        else
            quoted.append(pending_backslashes, static_cast<Char>('\\'));
        pending_backslashes = 0;
        quoted.push_back(ch);
    }
    quoted.append(pending_backslashes * 2, static_cast<Char>('\\'));
    quoted.push_back('"');
    return quoted;
}

} // namespace detail

inline std::string quote_windows_arg(std::string_view arg)
{
    return detail::quote_windows_arg_impl(arg);
}

inline std::wstring quote_windows_arg(std::wstring_view arg)
{
    return detail::quote_windows_arg_impl(arg);
}

inline uint64_t current_unix_time_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

#ifdef _WIN32
// Stable identity token for an already-opened process handle (its creation
// time), used to tell a live process apart from a PID that was recycled.
// `process` is a Windows HANDLE; declared as void* so this header stays free
// of <windows.h>.
std::optional<std::string> process_start_token(void* process);
#endif

struct SpawnDetachedOptions
{
    // Empty: the child inherits the parent's working directory.
    std::filesystem::path working_directory{};
    // Windows only: run fully detached with no console window and the first
    // UI window hidden (server-style children). GUI launches leave this false.
    bool hide_window = false;
};

// Launches `executable` with `arguments` (argv[1..]) as a fully detached
// process that outlives the caller. Arguments are passed as
// std::filesystem::path so Windows keeps full wide-character fidelity for
// paths; plain ASCII flags convert exactly on every platform.
//
// POSIX: double-fork + setsid so the child reparents to launchd/init and is
// reaped automatically; stdio is redirected to /dev/null. A bare command name
// (no '/') is resolved against PATH before forking. Exec failures in the
// detached grandchild are not reported back.
// Windows: CreateProcess in a new process group.
//
// Returns false and fills `error` only when the launch itself fails.
bool spawn_detached(const std::filesystem::path& executable,
    const std::vector<std::filesystem::path>& arguments,
    const SpawnDetachedOptions& options,
    std::string& error);

#ifndef _WIN32
// Expands a bare command name into candidate absolute paths using PATH
// (falling back to /usr/bin:/bin:/usr/sbin:/sbin when PATH is unset or
// empty). A command that already contains '/' is returned as-is. The caller
// tries execv on each candidate in order.
std::vector<std::string> resolve_exec_paths(const std::string& command);

// Snapshot of the parent environment for a child process: drops TERM= and any
// variable named by `extra_entries`, then appends TERM=<term_value> followed
// by `extra_entries` in order.
std::vector<std::string> build_child_environment(std::string_view term_value,
    const std::vector<std::pair<std::string, std::string>>& extra_entries = {});
#endif

} // namespace draxul
