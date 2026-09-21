#include "unix_agent_process_probe.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <termios.h>
#include <unistd.h>
#include <vector>

#ifdef __APPLE__
#include <libproc.h>
#include <sys/sysctl.h>
#endif

namespace draxul::detail
{
namespace
{

#ifdef __linux__
std::vector<std::string> read_null_separated_file(
    const std::filesystem::path& path, size_t max_bytes)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    std::string contents(max_bytes, '\0');
    input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
    contents.resize(static_cast<size_t>(input.gcount()));
    std::vector<std::string> values;
    size_t offset = 0;
    while (offset < contents.size() && values.size() < 64)
    {
        const size_t end = contents.find('\0', offset);
        const size_t length = (end == std::string::npos ? contents.size() : end) - offset;
        if (length != 0)
            values.emplace_back(contents.substr(offset, length));
        if (end == std::string::npos)
            break;
        offset = end + 1;
    }
    return values;
}
#endif

#ifdef __APPLE__
void read_macos_arguments_and_hint(pid_t process_id,
    std::vector<std::string>* arguments, std::string* hint)
{
    int argument_max = 0;
    size_t argument_max_size = sizeof(argument_max);
    int argument_max_mib[] = { CTL_KERN, KERN_ARGMAX };
    if (sysctl(argument_max_mib, 2, &argument_max, &argument_max_size,
            nullptr, 0)
            != 0
        || argument_max <= 0)
        return;
    std::vector<char> buffer(
        std::min<size_t>(static_cast<size_t>(argument_max), 64 * 1024));
    size_t size = buffer.size();
    int arguments_mib[] = { CTL_KERN, KERN_PROCARGS2, process_id };
    if (sysctl(arguments_mib, 3, buffer.data(), &size, nullptr, 0) != 0
        || size <= sizeof(int))
        return;

    int argument_count = 0;
    std::memcpy(&argument_count, buffer.data(), sizeof(argument_count));
    size_t offset = sizeof(argument_count);
    while (offset < size && buffer[offset] != '\0')
        ++offset;
    while (offset < size && buffer[offset] == '\0')
        ++offset;

    for (int index = 0;
        index < argument_count && index < 64 && offset < size; ++index)
    {
        const size_t end = std::find(
                               buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                               buffer.begin() + static_cast<std::ptrdiff_t>(size), '\0')
            - buffer.begin();
        if (end > offset && arguments)
            arguments->emplace_back(buffer.data() + offset, end - offset);
        offset = end + 1;
    }
    while (offset < size)
    {
        while (offset < size && buffer[offset] == '\0')
            ++offset;
        if (offset >= size)
            break;
        const size_t end = std::find(
                               buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                               buffer.begin() + static_cast<std::ptrdiff_t>(size), '\0')
            - buffer.begin();
        const std::string_view value(buffer.data() + offset, end - offset);
        constexpr std::string_view prefix = "DRAXUL_AGENT=";
        if (value.starts_with(prefix) && hint)
        {
            *hint = value.substr(prefix.size());
            break;
        }
        offset = end + 1;
    }
}
#endif

std::optional<AgentProcessObservation> capture_foreground_group(
    pid_t foreground_group)
{
    if (foreground_group <= 0)
        return std::nullopt;
    AgentProcessObservation observation;
    observation.captured_at = std::chrono::steady_clock::now();
    observation.foreground_reliable = true;

#ifdef __APPLE__
    const int bytes = proc_listpids(PROC_PGRP_ONLY,
        static_cast<uint32_t>(foreground_group), nullptr, 0);
    if (bytes <= 0)
        return observation;
    std::vector<pid_t> process_ids(
        static_cast<size_t>(bytes) / sizeof(pid_t) + 8, 0);
    const int written = proc_listpids(PROC_PGRP_ONLY,
        static_cast<uint32_t>(foreground_group), process_ids.data(),
        static_cast<int>(process_ids.size() * sizeof(pid_t)));
    const size_t count = written > 0
        ? static_cast<size_t>(written) / sizeof(pid_t)
        : 0;
    for (size_t index = 0;
        index < count && observation.processes.size() < 128; ++index)
    {
        const pid_t process_id = process_ids[index];
        if (process_id <= 0)
            continue;
        proc_bsdinfo info = {};
        if (proc_pidinfo(process_id, PROC_PIDTBSDINFO, 0, &info, sizeof(info))
            != static_cast<int>(sizeof(info)))
            continue;
        std::array<char, PROC_PIDPATHINFO_MAXSIZE> path = {};
        const int path_length = proc_pidpath(process_id, path.data(),
            static_cast<uint32_t>(path.size()));
        std::vector<std::string> arguments;
        std::string hint;
        read_macos_arguments_and_hint(process_id, &arguments, &hint);
        observation.processes.push_back({
            .process_id = static_cast<uint64_t>(process_id),
            .parent_process_id = static_cast<uint64_t>(info.pbi_ppid),
            .executable = path_length > 0 ? std::string(path.data())
                                          : std::string(info.pbi_name),
            .arguments = std::move(arguments),
            .agent_hint = std::move(hint),
        });
    }
#elif defined(__linux__)
    std::error_code ec;
    for (const auto& directory : std::filesystem::directory_iterator("/proc", ec))
    {
        if (ec || observation.processes.size() >= 128)
            break;
        const std::string name = directory.path().filename().string();
        if (name.empty()
            || !std::all_of(name.begin(), name.end(),
                [](unsigned char ch) { return std::isdigit(ch) != 0; }))
            continue;
        const pid_t process_id = static_cast<pid_t>(
            std::strtol(name.c_str(), nullptr, 10));
        std::ifstream stat(directory.path() / "stat");
        std::string stat_line;
        std::getline(stat, stat_line);
        const size_t close = stat_line.rfind(')');
        if (close == std::string::npos || close + 2 >= stat_line.size())
            continue;
        std::istringstream fields(stat_line.substr(close + 2));
        char state = '\0';
        pid_t parent_process_id = 0;
        pid_t process_group = 0;
        fields >> state >> parent_process_id >> process_group;
        if (!fields || process_group != foreground_group)
            continue;

        std::string executable;
        const auto executable_path = std::filesystem::read_symlink(directory.path() / "exe", ec);
        if (!ec)
            executable = executable_path.string();
        ec.clear();
        auto arguments = read_null_separated_file(
            directory.path() / "cmdline", 16 * 1024);
        std::string hint;
        for (const auto& value : read_null_separated_file(
                 directory.path() / "environ", 64 * 1024))
        {
            constexpr std::string_view prefix = "DRAXUL_AGENT=";
            if (value.starts_with(prefix))
            {
                hint = value.substr(prefix.size());
                break;
            }
        }
        observation.processes.push_back({
            .process_id = static_cast<uint64_t>(process_id),
            .parent_process_id = static_cast<uint64_t>(parent_process_id),
            .executable = std::move(executable),
            .arguments = std::move(arguments),
            .agent_hint = std::move(hint),
        });
    }
#endif
    return observation;
}

} // namespace

pid_t unix_foreground_process_group(int master_fd)
{
    return master_fd >= 0 ? tcgetpgrp(master_fd) : -1;
}

std::optional<AgentProcessObservation> capture_unix_agent_processes(
    int master_fd)
{
    return capture_foreground_group(unix_foreground_process_group(master_fd));
}

} // namespace draxul::detail
