#include <draxul/perf_timing.h>
#include <draxul/runtime_path.h>

#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace draxul
{

std::filesystem::path executable_directory()
{
    PERF_MEASURE();
#ifdef _WIN32
    std::wstring exe_path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD size = GetModuleFileNameW(nullptr, exe_path.data(), static_cast<DWORD>(exe_path.size()));
        if (size == 0)
            return {};
        if (size < exe_path.size())
        {
            exe_path.resize(size);
            return std::filesystem::path(exe_path).parent_path();
        }
        exe_path.resize(exe_path.size() * 2);
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0)
        return {};

    std::string exe_path(size, '\0');
    if (_NSGetExecutablePath(exe_path.data(), &size) != 0)
        return {};
    exe_path.resize(std::char_traits<char>::length(exe_path.c_str()));
    return std::filesystem::path(exe_path).parent_path();
#elif defined(__linux__)
    std::vector<char> exe_path(256, '\0');
    for (;;)
    {
        const ssize_t size = readlink("/proc/self/exe", exe_path.data(), exe_path.size());
        if (size < 0)
            return {};
        if (static_cast<size_t>(size) < exe_path.size())
            return std::filesystem::path(std::string(exe_path.data(), static_cast<size_t>(size))).parent_path();
        exe_path.resize(exe_path.size() * 2);
    }
#else
    return {};
#endif
}

std::filesystem::path bundled_asset_path(const std::filesystem::path& relative_path)
{
    PERF_MEASURE();
    if (relative_path.is_absolute())
        return relative_path;

    const auto exe_dir = executable_directory();
    if (!exe_dir.empty())
        return exe_dir / relative_path;

    return relative_path;
}

} // namespace draxul
