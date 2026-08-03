#include "fake_terminal_runtime.h"

#include <algorithm>
#include <utility>

namespace draxul
{

FakeTerminalRuntime::FakeTerminalRuntime()
    : core_(*this)
{
    grid_.resize(80, 24);
    highlights_.set_default_fg(Color(0.8f, 0.84f, 0.93f, 1.0f));
    highlights_.set_default_bg(Color(0.12f, 0.12f, 0.18f, 1.0f));
    reset_terminal_state();
}

void FakeTerminalRuntime::reset_terminal_state()
{
    grid_.clear();
    core_.reset();
    core_.feed(
        "\x1B]0;Draxul Fake Remote\x07"
        "\x1B[38;2;137;180;250mDraxul remote terminal\x1B[0m\r\n"
        "fake> ");
    grid_.clear_dirty();
    process_responses_.clear();
    clipboard_.clear();
    pending_clipboard_write_.reset();
    received_input_.clear();
    published_cursor_ = { 0, 0 };
    cursor_override_.reset();
    cursor_shape_ = CursorShape::Block;
    cursor_blink_ = false;
    cursor_visible_ = true;
    dirty_ = false;
}

TerminalSemanticSnapshot FakeTerminalRuntime::snapshot() const
{
    return core_.semantic_snapshot();
}

TerminalDirtySnapshot FakeTerminalRuntime::take_delta()
{
    auto delta = core_.dirty_snapshot();
    grid_.clear_dirty();
    return delta;
}

bool FakeTerminalRuntime::ensure_started(std::string&)
{
    return true;
}

bool FakeTerminalRuntime::restart(std::string&)
{
    reset_terminal_state();
    return true;
}

bool FakeTerminalRuntime::pump()
{
    return std::exchange(dirty_, false);
}

RemoteTerminalInputResult FakeTerminalRuntime::send_input(
    std::string_view bytes)
{
    if (input_result_ != RemoteTerminalInputResult::Accepted)
        return input_result_;
    received_input_.append(bytes);
    echo_input(bytes);
    return RemoteTerminalInputResult::Accepted;
}

void FakeTerminalRuntime::set_input_result(
    RemoteTerminalInputResult result)
{
    input_result_ = result;
}

std::string_view FakeTerminalRuntime::received_input() const
{
    return received_input_;
}

bool FakeTerminalRuntime::is_running() const
{
    return true;
}

uint64_t FakeTerminalRuntime::process_id() const
{
    return 0;
}

std::optional<int> FakeTerminalRuntime::exit_code() const
{
    return std::nullopt;
}

uint64_t FakeTerminalRuntime::scrollback_rows() const
{
    return 0;
}

std::optional<TerminalSemanticSnapshot>
FakeTerminalRuntime::scrollback_page(uint64_t, size_t) const
{
    return std::nullopt;
}

std::optional<std::string>
FakeTerminalRuntime::take_clipboard_write()
{
    return std::exchange(pending_clipboard_write_, std::nullopt);
}

void FakeTerminalRuntime::echo_input(std::string_view bytes)
{
    std::string echo;
    echo.reserve(bytes.size() + 16);
    for (const char byte : bytes)
    {
        if (byte == '\r' || byte == '\n')
            echo += "\r\nfake> ";
        else
            echo.push_back(byte);
    }
    core_.feed(echo);
    dirty_ = true;
}

bool FakeTerminalRuntime::resize(int cols, int rows)
{
    core_.resize(cols, rows);
    return true;
}

Grid& FakeTerminalRuntime::terminal_grid()
{
    return grid_;
}

const Grid& FakeTerminalRuntime::terminal_grid() const
{
    return grid_;
}

HighlightTable& FakeTerminalRuntime::terminal_highlights()
{
    return highlights_;
}

const HighlightTable& FakeTerminalRuntime::terminal_highlights() const
{
    return highlights_;
}

void FakeTerminalRuntime::terminal_resize_grid(int cols, int rows)
{
    grid_.resize(cols, rows);
}

bool FakeTerminalRuntime::terminal_write_process(std::string_view bytes)
{
    process_responses_.append(bytes);
    return true;
}

void FakeTerminalRuntime::terminal_mark_activity()
{
}

void FakeTerminalRuntime::terminal_set_title(std::string_view title)
{
    published_title_ = title;
}

std::string FakeTerminalRuntime::terminal_read_clipboard() const
{
    return clipboard_;
}

void FakeTerminalRuntime::terminal_write_clipboard(std::string_view text)
{
    clipboard_ = text;
    pending_clipboard_write_ = clipboard_;
}

void FakeTerminalRuntime::terminal_set_cursor_position(
    int col, int row, TerminalCursorBlinkUpdate)
{
    published_cursor_ = { col, row };
}

std::pair<int, int>
FakeTerminalRuntime::terminal_published_cursor_position() const
{
    return published_cursor_;
}

void FakeTerminalRuntime::terminal_set_cursor_display_override(
    std::optional<std::pair<int, int>> position)
{
    cursor_override_ = position;
}

void FakeTerminalRuntime::terminal_set_cursor_style(
    CursorShape shape, bool blink, bool visible)
{
    cursor_shape_ = shape;
    cursor_blink_ = blink;
    cursor_visible_ = visible;
}

void FakeTerminalRuntime::terminal_begin_cursor_publish_batch()
{
}

void FakeTerminalRuntime::terminal_end_cursor_publish_batch()
{
}

void FakeTerminalRuntime::terminal_line_scrolled_off(int)
{
}

void FakeTerminalRuntime::terminal_mouse_mode_changed(int, bool)
{
}

void FakeTerminalRuntime::terminal_collect_extra_attr_ids(
    std::unordered_map<uint16_t, HlAttr>&)
{
}

void FakeTerminalRuntime::terminal_remap_extra_highlight_ids(
    const std::function<uint16_t(uint16_t)>&)
{
}

} // namespace draxul
