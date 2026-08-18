#pragma once

#include <atomic>
#include <filesystem>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace draxul::tests
{

inline std::filesystem::path unique_control_runtime_directory()
{
    // sockaddr_un::sun_path is 104 bytes on macOS, and the per-user $TMPDIR
    // already spends much of that budget. Keep control fixtures shallow.
#ifdef _WIN32
    const std::filesystem::path base = std::filesystem::temp_directory_path();
    const auto pid = static_cast<long long>(_getpid());
#else
    const std::filesystem::path base = "/tmp";
    const auto pid = static_cast<long long>(::getpid());
#endif
    static std::atomic<unsigned> counter{ 0 };
    return base / ("dxl-ctl-" + std::to_string(pid) + "-"
        + std::to_string(counter.fetch_add(1)));
}

} // namespace draxul::tests
