#include "self_launch.h"

#include <cerrno>
#include <future>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <draxul/process_util.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#elif defined(__linux__)
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace draxul
{

namespace
{

#ifdef _WIN32
std::wstring widen_utf8(std::string_view text)
{
    if (text.empty())
        return {};
    const int size = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}
#elif defined(__APPLE__) || defined(__linux__)
class SpawnAttributes
{
public:
    int initialize()
    {
        const int result = posix_spawnattr_init(&value_);
        initialized_ = result == 0;
        return result;
    }

    ~SpawnAttributes()
    {
        if (initialized_)
            posix_spawnattr_destroy(&value_);
    }

    posix_spawnattr_t* get() { return &value_; }

private:
    posix_spawnattr_t value_{};
    bool initialized_ = false;
};

class SpawnFileActions
{
public:
    int initialize()
    {
        const int result = posix_spawn_file_actions_init(&value_);
        initialized_ = result == 0;
        return result;
    }

    ~SpawnFileActions()
    {
        if (initialized_)
            posix_spawn_file_actions_destroy(&value_);
    }

    posix_spawn_file_actions_t* get() { return &value_; }

private:
    posix_spawn_file_actions_t value_{};
    bool initialized_ = false;
};
#endif

} // namespace

std::string SelfLaunchResult::error_message() const
{
    switch (error_api)
    {
    case SelfLaunchErrorApi::None:
        return {};
    case SelfLaunchErrorApi::ReaperThread:
        return "child reaper thread creation failed: " + std::to_string(error_code);
    case SelfLaunchErrorApi::CreateProcessW:
        return "CreateProcessW failed: " + std::to_string(error_code);
    case SelfLaunchErrorApi::PosixSpawnAttributes:
        return "posix_spawnattr_init() failed: " + std::to_string(error_code);
    case SelfLaunchErrorApi::PosixSpawnFileActions:
        return "posix_spawn_file_actions_init() failed: " + std::to_string(error_code);
    case SelfLaunchErrorApi::PosixSpawn:
        return "posix_spawn() failed: " + std::to_string(error_code);
    }
    return "self-launch failed";
}

SelfLaunchCommand make_self_launch_command(
    const std::filesystem::path& executable_path, std::vector<std::string> arguments)
{
    SelfLaunchCommand command;
    command.executable_path = executable_path;
    command.argv.reserve(arguments.size() + 1);
    command.argv.emplace_back(executable_path.string());
    for (auto& argument : arguments)
        command.argv.push_back(std::move(argument));
    return command;
}

SelfLaunchResult launch_self_process(const SelfLaunchCommand& command)
{
    if (command.executable_path.empty() || command.argv.empty())
    {
#ifdef _WIN32
        return { SelfLaunchErrorApi::CreateProcessW, ERROR_FILE_NOT_FOUND };
#else
        return { SelfLaunchErrorApi::PosixSpawn, ENOENT };
#endif
    }

#ifdef _WIN32
    std::string command_line_utf8;
    for (const auto& arg : command.argv)
    {
        if (!command_line_utf8.empty())
            command_line_utf8.push_back(' ');
        command_line_utf8 += quote_windows_arg(arg);
    }

    std::wstring executable_path = command.executable_path.wstring();
    std::wstring command_line = widen_utf8(command_line_utf8);
    std::vector<wchar_t> command_line_buffer(command_line.begin(), command_line.end());
    command_line_buffer.push_back(L'\0');

    STARTUPINFOW startup_info = {};
    startup_info.cb = sizeof(startup_info);
    PROCESS_INFORMATION process_info = {};
    const BOOL ok = CreateProcessW(
        executable_path.c_str(),
        command_line_buffer.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startup_info,
        &process_info);
    if (!ok)
        return { SelfLaunchErrorApi::CreateProcessW, static_cast<int>(GetLastError()) };

    CloseHandle(process_info.hThread);
    CloseHandle(process_info.hProcess);
    return {};
#elif defined(__APPLE__) || defined(__linux__)
    SpawnAttributes attributes;
    if (const int result = attributes.initialize(); result != 0)
        return { SelfLaunchErrorApi::PosixSpawnAttributes, result };

    SpawnFileActions file_actions;
    if (const int result = file_actions.initialize(); result != 0)
        return { SelfLaunchErrorApi::PosixSpawnFileActions, result };

    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    if (const int result = posix_spawnattr_setsigmask(attributes.get(), &signal_mask); result != 0)
        return { SelfLaunchErrorApi::PosixSpawnAttributes, result };

    const short flags = POSIX_SPAWN_SETSIGMASK;
    if (const int result = posix_spawnattr_setflags(attributes.get(), flags); result != 0)
        return { SelfLaunchErrorApi::PosixSpawnAttributes, result };

    std::vector<char*> argv;
    argv.reserve(command.argv.size() + 1);
    for (const auto& arg : command.argv)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    const std::string executable_path = command.executable_path.string();

    // Establish ownership before spawning. The waiter blocks on the future,
    // then reaps exactly this child; no allocation or lock-taking occurs in a
    // post-fork child because posix_spawn owns the implementation boundary.
    std::promise<pid_t> child_promise;
    auto child_future = child_promise.get_future();
    std::thread reaper;
    try
    {
        reaper = std::thread([future = std::move(child_future)]() mutable {
            const pid_t child = future.get();
            if (child <= 0)
                return;
            int status = 0;
            while (waitpid(child, &status, 0) < 0 && errno == EINTR)
            {
            }
        });
        reaper.detach();
    }
    catch (const std::system_error& error)
    {
        child_promise.set_value(-1);
        if (reaper.joinable())
            reaper.join();
        return { SelfLaunchErrorApi::ReaperThread, error.code().value() };
    }

    pid_t child = -1;
    const int spawn_result = posix_spawn(
        &child,
        executable_path.c_str(),
        file_actions.get(),
        attributes.get(),
        argv.data(),
        environ);
    child_promise.set_value(spawn_result == 0 ? child : -1);
    if (spawn_result != 0)
        return { SelfLaunchErrorApi::PosixSpawn, spawn_result };
    return {};
#else
    return { SelfLaunchErrorApi::PosixSpawn, ENOSYS };
#endif
}

std::filesystem::path resolve_self_executable_path(const std::vector<std::string>& argv)
{
#ifdef _WIN32
    std::wstring executable_path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD size = GetModuleFileNameW(
            nullptr, executable_path.data(), static_cast<DWORD>(executable_path.size()));
        if (size == 0)
            return {};
        if (size < executable_path.size())
        {
            executable_path.resize(size);
            return std::filesystem::path(executable_path);
        }
        executable_path.resize(executable_path.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0)
        return {};

    std::string executable_path(size, '\0');
    if (_NSGetExecutablePath(executable_path.data(), &size) != 0)
        return {};
    executable_path.resize(std::char_traits<char>::length(executable_path.c_str()));
    return std::filesystem::path(executable_path);
#elif defined(__linux__)
    std::vector<char> executable_path(256, '\0');
    for (;;)
    {
        const ssize_t size = readlink("/proc/self/exe", executable_path.data(), executable_path.size());
        if (size < 0)
            break;
        if (static_cast<size_t>(size) < executable_path.size())
            return std::filesystem::path(std::string(executable_path.data(), static_cast<size_t>(size)));
        executable_path.resize(executable_path.size() * 2);
    }
#endif
    if (argv.empty())
        return {};
    return std::filesystem::absolute(std::filesystem::path(argv.front()));
}

} // namespace draxul
