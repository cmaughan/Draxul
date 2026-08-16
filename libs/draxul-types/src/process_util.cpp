#include <draxul/process_util.h>

#include <algorithm>
#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace draxul
{

#ifdef _WIN32

std::optional<std::string> process_start_token(void* process)
{
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(static_cast<HANDLE>(process), &created, &exited, &kernel, &user))
        return std::nullopt;
    const uint64_t value
        = (static_cast<uint64_t>(created.dwHighDateTime) << 32)
        | created.dwLowDateTime;
    return std::to_string(value);
}

bool spawn_detached(const std::filesystem::path& executable,
    const std::vector<std::filesystem::path>& arguments,
    const SpawnDetachedOptions& options,
    std::string& error)
{
    const std::wstring executable_text = executable.wstring();
    std::wstring command = quote_windows_arg(std::wstring_view(executable_text));
    for (const std::filesystem::path& argument : arguments)
    {
        command += L" ";
        command += quote_windows_arg(std::wstring_view(argument.wstring()));
    }
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    const std::wstring working_directory = options.working_directory.wstring();

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    DWORD flags = CREATE_NEW_PROCESS_GROUP;
    if (options.hide_window)
    {
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        flags |= CREATE_NO_WINDOW | DETACHED_PROCESS;
    }
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable_text.c_str(), mutable_command.data(),
            nullptr, nullptr, FALSE, flags, nullptr,
            working_directory.empty() ? nullptr : working_directory.c_str(),
            &startup, &process))
    {
        error = "Unable to launch the detached process (error "
            + std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

#else // POSIX

std::vector<std::string> resolve_exec_paths(const std::string& command)
{
    if (command.find('/') != std::string::npos)
        return { command };

    const char* raw_path = std::getenv("PATH");
    const std::string_view path
        = (raw_path != nullptr && *raw_path != '\0')
        ? std::string_view(raw_path)
        : std::string_view("/usr/bin:/bin:/usr/sbin:/sbin");

    std::vector<std::string> candidates;
    size_t start = 0;
    while (start <= path.size())
    {
        const size_t end = path.find(':', start);
        std::string_view dir = (end == std::string_view::npos)
            ? path.substr(start)
            : path.substr(start, end - start);
        if (dir.empty())
            dir = ".";

        std::string candidate(dir);
        if (!candidate.empty() && candidate.back() != '/')
            candidate.push_back('/');
        candidate += command;
        candidates.push_back(std::move(candidate));

        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }

    return candidates;
}

std::vector<std::string> build_child_environment(std::string_view term_value,
    const std::vector<std::pair<std::string, std::string>>& extra_entries)
{
    std::vector<std::string> env;
    for (char** current = environ; current != nullptr && *current != nullptr; ++current)
    {
        const std::string_view entry(*current);
        const size_t equals = entry.find('=');
        const std::string_view key = entry.substr(0, equals);
        if (key == "TERM"
            || std::any_of(extra_entries.begin(), extra_entries.end(),
                [&](const auto& value) { return value.first == key; }))
        {
            continue;
        }
        env.emplace_back(entry);
    }

    env.push_back("TERM=" + std::string(term_value));
    for (const auto& [key, value] : extra_entries)
        env.push_back(key + "=" + value);
    return env;
}

bool spawn_detached(const std::filesystem::path& executable,
    const std::vector<std::filesystem::path>& arguments,
    const SpawnDetachedOptions& options,
    std::string& error)
{
    // Build the argv (and resolve PATH candidates) BEFORE forking. The parent
    // is multithreaded (GUI or test harness), and heap allocation between
    // fork() and execv() can inherit a locked allocator and deadlock the
    // child — the exact failure recorded in kanban done/01
    // macos-app-self-launch. After fork, only async-signal-safe calls
    // (setsid/fork/chdir/dup2/execv/_exit) run.
    const std::string executable_text = executable.string();
    const std::vector<std::string> exec_paths = resolve_exec_paths(executable_text);
    std::vector<std::string> argument_storage;
    argument_storage.reserve(arguments.size() + 1);
    argument_storage.push_back(executable_text);
    for (const std::filesystem::path& argument : arguments)
        argument_storage.push_back(argument.string());
    std::vector<char*> argv;
    argv.reserve(argument_storage.size() + 1);
    for (std::string& argument : argument_storage)
        argv.push_back(argument.data());
    argv.push_back(nullptr);
    const std::string working_directory = options.working_directory.string();

    // Double-fork so the detached child reparents to launchd/init and is
    // reaped automatically when it dies. As a direct child it became a ZOMBIE
    // on exit — no caller reaps it, so `kill(pid, 0)` kept reporting the dead
    // process alive (and force-stopped servers appeared unkillable).
    const pid_t child = ::fork();
    if (child < 0)
    {
        error = "Unable to fork the detached process.";
        return false;
    }
    if (child == 0)
    {
        ::setsid();
        const pid_t grandchild = ::fork();
        if (grandchild != 0)
            _exit(grandchild < 0 ? 127 : 0);
        if (!working_directory.empty())
            (void)::chdir(working_directory.c_str());
        const int null_fd = ::open("/dev/null", O_RDWR);
        if (null_fd >= 0)
        {
            ::dup2(null_fd, STDIN_FILENO);
            ::dup2(null_fd, STDOUT_FILENO);
            ::dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO)
                ::close(null_fd);
        }
        for (const std::string& exec_path : exec_paths)
            ::execv(exec_path.c_str(), argv.data());
        _exit(127);
    }
    // The intermediate exits immediately after its fork; reap it so the
    // launcher itself leaves no zombie behind.
    int intermediate_status = 0;
    ::waitpid(child, &intermediate_status, 0);
    return true;
}

#endif

} // namespace draxul
