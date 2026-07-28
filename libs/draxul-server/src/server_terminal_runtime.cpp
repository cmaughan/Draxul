#include "server_terminal_runtime.h"

#include <draxul/log.h>

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace draxul
{

ServerTerminalRuntime::ServerTerminalRuntime()
    : core_(*this)
{
    grid_.resize(80, 24);
    highlights_.set_default_fg(Color(0.92f, 0.92f, 0.92f, 1.0f));
    highlights_.set_default_bg(Color(0.08f, 0.09f, 0.10f, 1.0f));
    core_.reset();
    grid_.clear_dirty();
}

ServerTerminalRuntime::~ServerTerminalRuntime()
{
    process_.shutdown();
}

bool ServerTerminalRuntime::ensure_started(std::string& error)
{
    if (process_.is_running())
        return true;
    return start_process(error);
}

bool ServerTerminalRuntime::restart(std::string& error)
{
    process_.shutdown();
    core_.reset();
    grid_.clear_dirty();
    return start_process(error);
}

bool ServerTerminalRuntime::start_process(std::string& error)
{
    const std::string working_directory
        = std::filesystem::current_path().string();
#ifdef _WIN32
    const std::vector<std::string> args{ "-NoLogo" };
    for (const std::string command : { "pwsh.exe", "powershell.exe" })
    {
        if (process_.spawn(command, args, working_directory,
                grid_.cols(), grid_.rows(), [] {}))
        {
            DRAXUL_LOG_INFO(LogCategory::App,
                "Started server-owned shell pid=%llu command=%s",
                static_cast<unsigned long long>(process_.process_id()),
                command.c_str());
            return true;
        }
    }
    error = "Could not start PowerShell in the Draxul server.";
#else
    const char* configured_shell = std::getenv("SHELL");
    const std::string command
        = configured_shell && *configured_shell ? configured_shell : "bash";
    if (process_.spawn(command, {}, working_directory, [] {},
            grid_.cols(), grid_.rows(), true))
    {
        DRAXUL_LOG_INFO(LogCategory::App,
            "Started server-owned shell pid=%llu command=%s",
            static_cast<unsigned long long>(process_.process_id()),
            command.c_str());
        return true;
    }
    error = "Could not start the configured shell in the Draxul server.";
#endif
    return false;
}

bool ServerTerminalRuntime::pump()
{
    auto chunks = process_.drain_output();
    if (chunks.empty())
        return false;
    core_.begin_output_cursor_batch();
    for (const auto& chunk : chunks)
        core_.feed(chunk);
    core_.end_output_cursor_batch();
    core_.reconcile_provisional_cursor_after_pump(true);
    return true;
}

bool ServerTerminalRuntime::send_input(std::string_view bytes)
{
    return process_.write(bytes);
}

bool ServerTerminalRuntime::resize(int cols, int rows)
{
    if (!process_.resize(cols, rows))
        return false;
    core_.resize(cols, rows);
    return true;
}

bool ServerTerminalRuntime::is_running() const
{
    return process_.is_running();
}

uint64_t ServerTerminalRuntime::process_id() const
{
    return process_.process_id();
}

TerminalSemanticSnapshot ServerTerminalRuntime::snapshot() const
{
    return core_.semantic_snapshot();
}

TerminalDirtySnapshot ServerTerminalRuntime::take_delta()
{
    auto delta = core_.dirty_snapshot();
    grid_.clear_dirty();
    return delta;
}

Grid& ServerTerminalRuntime::terminal_grid()
{
    return grid_;
}

const Grid& ServerTerminalRuntime::terminal_grid() const
{
    return grid_;
}

HighlightTable& ServerTerminalRuntime::terminal_highlights()
{
    return highlights_;
}

const HighlightTable& ServerTerminalRuntime::terminal_highlights() const
{
    return highlights_;
}

void ServerTerminalRuntime::terminal_resize_grid(int cols, int rows)
{
    grid_.resize(cols, rows);
}

bool ServerTerminalRuntime::terminal_write_process(std::string_view bytes)
{
    return process_.write(bytes);
}

void ServerTerminalRuntime::terminal_mark_activity()
{
}

void ServerTerminalRuntime::terminal_set_title(std::string_view title)
{
    published_title_ = title;
}

std::string ServerTerminalRuntime::terminal_read_clipboard() const
{
    return clipboard_;
}

void ServerTerminalRuntime::terminal_write_clipboard(std::string_view text)
{
    clipboard_ = text;
}

void ServerTerminalRuntime::terminal_set_cursor_position(
    int col, int row, TerminalCursorBlinkUpdate)
{
    published_cursor_ = { col, row };
}

std::pair<int, int>
ServerTerminalRuntime::terminal_published_cursor_position() const
{
    return published_cursor_;
}

void ServerTerminalRuntime::terminal_set_cursor_display_override(
    std::optional<std::pair<int, int>> position)
{
    cursor_override_ = position;
}

void ServerTerminalRuntime::terminal_set_cursor_style(
    CursorShape shape, bool blink, bool visible)
{
    cursor_shape_ = shape;
    cursor_blink_ = blink;
    cursor_visible_ = visible;
}

void ServerTerminalRuntime::terminal_begin_cursor_publish_batch()
{
}

void ServerTerminalRuntime::terminal_end_cursor_publish_batch()
{
}

void ServerTerminalRuntime::terminal_line_scrolled_off(int)
{
}

void ServerTerminalRuntime::terminal_mouse_mode_changed(int, bool)
{
}

void ServerTerminalRuntime::terminal_collect_extra_attr_ids(
    std::unordered_map<uint16_t, HlAttr>&)
{
}

void ServerTerminalRuntime::terminal_remap_extra_highlight_ids(
    const std::function<uint16_t(uint16_t)>&)
{
}

} // namespace draxul
