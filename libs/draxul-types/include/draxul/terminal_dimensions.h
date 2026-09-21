#pragma once

#include <algorithm>

namespace draxul
{

// Both POSIX winsize fields and ConPTY's COORD use 16-bit dimensions. Keep
// the terminal model and native PTY on the same positive, portable range.
inline constexpr int kMaxTerminalDimension = 32767;

struct TerminalDimensions
{
    int cols = 1;
    int rows = 1;
};

[[nodiscard]] inline TerminalDimensions normalize_terminal_dimensions(
    int cols, int rows)
{
    return {
        std::clamp(cols, 1, kMaxTerminalDimension),
        std::clamp(rows, 1, kMaxTerminalDimension),
    };
}

} // namespace draxul
