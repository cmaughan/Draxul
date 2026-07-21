#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace draxul
{

struct SelfLaunchCommand
{
    std::filesystem::path executable_path;
    std::vector<std::string> argv;
};

enum class SelfLaunchErrorApi
{
    None,
    ReaperThread,
    CreateProcessW,
    PosixSpawnAttributes,
    PosixSpawnFileActions,
    PosixSpawn,
};

struct SelfLaunchResult
{
    SelfLaunchErrorApi error_api = SelfLaunchErrorApi::None;
    int error_code = 0;

    [[nodiscard]] bool launched() const { return error_api == SelfLaunchErrorApi::None; }
    [[nodiscard]] std::string error_message() const;
};

// Constructs argv in the caller before any process API is entered. Arguments
// are preserved byte-for-byte, including empty values.
SelfLaunchCommand make_self_launch_command(
    const std::filesystem::path& executable_path, std::vector<std::string> arguments);

// Launches another executable without waiting for it. Windows retains the
// existing CreateProcessW behavior. POSIX children are spawned with
// posix_spawn and owned by a detached waiter created before the spawn call.
SelfLaunchResult launch_self_process(const SelfLaunchCommand& command);

// Resolves the current executable. argv is only a fallback on platforms that
// do not expose a native executable-path API.
std::filesystem::path resolve_self_executable_path(const std::vector<std::string>& argv);

} // namespace draxul
