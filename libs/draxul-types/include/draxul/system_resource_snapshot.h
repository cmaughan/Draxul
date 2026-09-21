#pragma once

namespace draxul
{

// Neutral value snapshot sampled by runtime support and consumed by app-shell
// chrome layout. Sampling policy and platform APIs do not belong in this type.
struct SystemResourceSnapshot
{
    int cpu_percent = -1;
    int memory_percent = -1;

    [[nodiscard]] bool available() const
    {
        return cpu_percent >= 0 && memory_percent >= 0;
    }

    [[nodiscard]] bool operator==(const SystemResourceSnapshot& other) const = default;
};

} // namespace draxul
