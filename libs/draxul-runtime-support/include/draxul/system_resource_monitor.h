#pragma once

#include <chrono>
#include <cstdint>
#include <draxul/system_resource_snapshot.h>
#include <optional>

namespace draxul
{

class SystemResourceMonitor
{
public:
    struct CpuTimes
    {
        uint64_t idle = 0;
        uint64_t total = 0;
        bool valid = false;
    };

    bool refresh(std::chrono::steady_clock::time_point now);

    [[nodiscard]] const SystemResourceSnapshot& snapshot() const
    {
        return snapshot_;
    }

private:
    static bool sample(SystemResourceSnapshot& snapshot, CpuTimes& times);

    SystemResourceSnapshot snapshot_{};
    CpuTimes previous_cpu_times_{};
    std::optional<std::chrono::steady_clock::time_point> next_refresh_{};
};

} // namespace draxul
