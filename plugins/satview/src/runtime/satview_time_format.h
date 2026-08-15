#pragma once

#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <string>

namespace draxul::satview
{

inline std::string format_utc_offset(int offset_seconds)
{
    const char sign = offset_seconds < 0 ? '-' : '+';
    const int absolute_seconds = std::abs(offset_seconds);
    const int hours = absolute_seconds / 3600;
    const int minutes = (absolute_seconds % 3600) / 60;
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "UTC%c%02d:%02d", sign, hours, minutes);
    return buffer;
}

inline std::string format_local_simulation_time(double unix_seconds)
{
    if (!std::isfinite(unix_seconds)
        || unix_seconds < static_cast<double>(std::numeric_limits<std::time_t>::min())
        || unix_seconds > static_cast<double>(std::numeric_limits<std::time_t>::max()))
    {
        return "unavailable";
    }

    const std::time_t raw = static_cast<std::time_t>(std::floor(unix_seconds));
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &raw) != 0)
        return "unavailable";
#else
    if (localtime_r(&raw, &local) == nullptr)
        return "unavailable";
#endif

    char date_time[32]{};
    if (std::strftime(date_time, sizeof(date_time), "%Y-%m-%d %H:%M:%S", &local) == 0)
        return "unavailable";

    std::tm local_as_utc = local;
#ifdef _WIN32
    const std::time_t local_epoch = static_cast<std::time_t>(_mkgmtime64(&local_as_utc));
#else
    const std::time_t local_epoch = timegm(&local_as_utc);
#endif
    const int offset_seconds = static_cast<int>(std::difftime(local_epoch, raw));
    return std::string(date_time) + " (" + format_utc_offset(offset_seconds) + ")";
}

} // namespace draxul::satview
