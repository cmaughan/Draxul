#pragma once

#include <cstdint>

namespace draxul
{

struct TerminalId
{
    uint64_t value = 0;

    constexpr bool valid() const noexcept
    {
        return value != 0;
    }

    bool operator==(const TerminalId&) const = default;
};

struct TerminalRuntimeGeneration
{
    uint64_t value = 0;

    constexpr bool valid() const noexcept
    {
        return value != 0;
    }

    bool operator==(const TerminalRuntimeGeneration&) const = default;
};

struct TerminalSequence
{
    uint64_t value = 0;

    bool operator==(const TerminalSequence&) const = default;
};

} // namespace draxul

