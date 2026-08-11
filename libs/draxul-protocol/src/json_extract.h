#pragma once

#include <cstdint>
#include <limits>
#include <nlohmann/json.hpp>
#include <type_traits>

namespace draxul
{

template <typename Target>
bool read_bounded_integer(
    const nlohmann::json& value, Target& target)
{
    static_assert(std::is_integral_v<Target>
        && !std::is_same_v<Target, bool>);

    if (value.is_number_unsigned())
    {
        const uint64_t encoded = value.get<uint64_t>();
        if constexpr (std::is_signed_v<Target>)
        {
            if (encoded
                > static_cast<uint64_t>(
                    std::numeric_limits<Target>::max()))
            {
                return false;
            }
        }
        else if (encoded
            > static_cast<uint64_t>(
                std::numeric_limits<Target>::max()))
        {
            return false;
        }
        target = static_cast<Target>(encoded);
        return true;
    }

    if (!value.is_number_integer())
        return false;
    const int64_t encoded = value.get<int64_t>();
    if constexpr (std::is_unsigned_v<Target>)
    {
        if (encoded < 0
            || static_cast<uint64_t>(encoded)
                > static_cast<uint64_t>(
                    std::numeric_limits<Target>::max()))
        {
            return false;
        }
    }
    else if (encoded
            < static_cast<int64_t>(
                std::numeric_limits<Target>::min())
        || encoded
            > static_cast<int64_t>(
                std::numeric_limits<Target>::max()))
    {
        return false;
    }
    target = static_cast<Target>(encoded);
    return true;
}

} // namespace draxul
