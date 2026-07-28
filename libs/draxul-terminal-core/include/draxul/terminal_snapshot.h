#pragma once

#include <cstddef>
#include <cstdint>
#include <draxul/grid.h>
#include <draxul/highlight.h>
#include <draxul/types.h>
#include <string>
#include <vector>

namespace draxul
{

struct TerminalStateLimits
{
    static constexpr int kMaxColumns = 1000;
    static constexpr int kMaxRows = 1000;
    static constexpr size_t kMaxCells = 1'000'000;
    static constexpr size_t kMaxCellTextBytes = CellText::kMaxLen;
    static constexpr size_t kMaxTitleBytes = 4096;
    static constexpr size_t kMaxWorkingDirectoryBytes = 32 * 1024;
    static constexpr size_t kMaxHyperlinkBytes = 32 * 1024;
    static constexpr size_t kMaxInputBytes = 1024 * 1024;
    static constexpr size_t kMaxFrameBytes = 8 * 1024 * 1024;
    static constexpr size_t kMaxScrollbackRows = 1'000'000;
};

enum class TerminalShellMarkKind : uint8_t
{
    PromptStart,
    CommandStart,
    OutputStart,
    OutputEnd,
};

struct TerminalShellMarkSnapshot
{
    TerminalShellMarkKind kind = TerminalShellMarkKind::PromptStart;
    int row = 0;
    int exit_code = -1;

    bool operator==(const TerminalShellMarkSnapshot&) const = default;
};

struct TerminalMouseModeSnapshot
{
    bool normal_tracking = false;
    bool button_motion = false;
    bool any_motion = false;
    bool sgr_coordinates = false;

    bool operator==(const TerminalMouseModeSnapshot&) const = default;
};

struct TerminalModeSnapshot
{
    bool alternate_screen = false;
    bool auto_wrap = true;
    bool origin = false;
    bool cursor_application = false;
    bool bracketed_paste = false;
    bool focus_reporting = false;
    bool synchronized_output = false;
    TerminalMouseModeSnapshot mouse;

    bool operator==(const TerminalModeSnapshot&) const = default;
};

struct TerminalCursorSnapshot
{
    int col = 0;
    int row = 0;
    bool visible = true;
    CursorShape shape = CursorShape::Block;
    bool blink = false;

    bool operator==(const TerminalCursorSnapshot&) const = default;
};

struct TerminalCellSnapshot
{
    std::string text;
    HlAttr attr;
    bool double_width = false;
    bool double_width_continuation = false;
    std::string hyperlink;

    bool operator==(const TerminalCellSnapshot&) const = default;
};

struct TerminalSnapshotMetadata
{
    TerminalCursorSnapshot cursor;
    TerminalModeSnapshot modes;
    std::string title;
    std::string working_directory;
    std::vector<TerminalShellMarkSnapshot> shell_marks;

    bool operator==(const TerminalSnapshotMetadata&) const = default;
};

struct TerminalSemanticSnapshot
{
    int cols = 0;
    int rows = 0;
    std::vector<TerminalCellSnapshot> cells;
    TerminalSnapshotMetadata metadata;

    bool operator==(const TerminalSemanticSnapshot&) const = default;
};

struct TerminalDirtyCellSnapshot
{
    int col = 0;
    int row = 0;
    TerminalCellSnapshot cell;

    bool operator==(const TerminalDirtyCellSnapshot&) const = default;
};

struct TerminalDirtySnapshot
{
    int cols = 0;
    int rows = 0;
    bool full = false;
    std::vector<TerminalDirtyCellSnapshot> cells;
    TerminalSnapshotMetadata metadata;

    bool operator==(const TerminalDirtySnapshot&) const = default;
};

TerminalSemanticSnapshot capture_terminal_semantic_snapshot(
    const Grid& grid,
    const HighlightTable& highlights,
    TerminalSnapshotMetadata metadata);

TerminalDirtySnapshot capture_terminal_dirty_snapshot(
    const Grid& grid,
    const HighlightTable& highlights,
    TerminalSnapshotMetadata metadata);

uint64_t terminal_semantic_digest(const TerminalSemanticSnapshot& snapshot);

} // namespace draxul
