#pragma once

#include <algorithm>
#include <chrono>

namespace draxul::control_detail
{

using ControlDeadline = std::chrono::steady_clock::time_point;

inline std::chrono::milliseconds remaining_time(ControlDeadline deadline)
{
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline)
        return std::chrono::milliseconds(0);
    return std::max(std::chrono::milliseconds(1),
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
}

} // namespace draxul::control_detail
