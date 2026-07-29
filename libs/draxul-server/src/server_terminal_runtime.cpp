#include "server_terminal_runtime.h"

#include <draxul/log.h>

#include <cstdlib>
#include <filesystem>
#include <utility>
#include <vector>

namespace draxul
{

ServerTerminalRuntime::ServerTerminalRuntime(
    ServerTerminalRuntimeOptions options)
    : core_(*this)
    , scrollback_([this] {
        ScrollbackBuffer::Callbacks callbacks;
        callbacks.grid_cols = [this] { return grid_.cols(); };
        callbacks.grid_rows = [this] { return grid_.rows(); };
        callbacks.get_cell = [this](int col, int row) {
            return grid_.get_cell(col, row);
        };
        callbacks.set_cell = [this](
                                 int col, int row, const Cell& cell) {
            grid_.set_cell(col, row, std::string(cell.text.view()),
                cell.hl_attr_id, cell.double_width);
        };
        callbacks.force_full_redraw = [this] { grid_.mark_all_dirty(); };
        callbacks.flush_grid = [] {};
        return callbacks;
    }(),
          options.scrollback_capacity)
    , options_(std::move(options))
{
    grid_.resize(80, 24);
    scrollback_.resize(80);
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
    scrollback_.reset();
    core_.reset();
    grid_.clear_dirty();
    return start_process(error);
}

bool ServerTerminalRuntime::start_process(std::string& error)
{
    const std::string working_directory
        = options_.working_directory.empty()
        ? std::filesystem::current_path().string()
        : options_.working_directory;
#ifdef _WIN32
    std::vector<std::pair<std::string, std::vector<std::string>>> candidates;
    if (!options_.command.empty())
    {
        candidates = { { options_.command, options_.args } };
    }
    else if (options_.shell_kind.empty()
        || options_.shell_kind == "powershell")
    {
        candidates = {
            { "pwsh.exe", { "-NoLogo" } },
            { "powershell.exe", { "-NoLogo" } },
        };
    }
    else if (options_.shell_kind == "wsl")
    {
        candidates = { { "wsl.exe", {} } };
    }
    else if (options_.shell_kind == "bash")
    {
        candidates = { { "bash.exe", {} } };
    }
    else if (options_.shell_kind == "zsh")
    {
        candidates = { { "zsh.exe", {} } };
    }
    else
    {
        error = "Unsupported Draxul server shell kind: "
            + options_.shell_kind;
        return false;
    }
    for (const auto& [command, args] : candidates)
    {
        if (process_.spawn(command, args, working_directory,
                grid_.cols(), grid_.rows(), [] {},
                options_.environment))
        {
            DRAXUL_LOG_INFO(LogCategory::App,
                "Started server-owned shell pid=%llu command=%s",
                static_cast<unsigned long long>(process_.process_id()),
                command.c_str());
            return true;
        }
    }
    error = "Could not start the configured shell in the Draxul server.";
#else
    std::string command = options_.command;
    std::vector<std::string> args = options_.args;
    if (command.empty())
    {
        if (options_.shell_kind.empty())
        {
            const char* configured_shell = std::getenv("SHELL");
            command = configured_shell && *configured_shell
                ? configured_shell
                : "bash";
        }
        else if (options_.shell_kind == "powershell")
        {
            command = "pwsh";
        }
        else if (options_.shell_kind == "bash"
            || options_.shell_kind == "zsh")
        {
            command = options_.shell_kind;
        }
        else if (options_.shell_kind == "wsl")
        {
            error
                = "WSL remote shells are supported only by the Windows server.";
            return false;
        }
        else
        {
            error = "Unsupported Draxul server shell kind: "
                + options_.shell_kind;
            return false;
        }
    }
    if (process_.spawn(command, args, working_directory, [] {},
            grid_.cols(), grid_.rows(), true, options_.environment))
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
    // Keep dirty state server-side until DEC synchronized output ends. This
    // preserves the same atomic presentation guarantee as a local terminal:
    // clients see the completed frame, never its intermediate mutations.
    return !core_.synchronized_output_active();
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

uint64_t ServerTerminalRuntime::scrollback_rows() const
{
    return static_cast<uint64_t>(scrollback_.size());
}

std::optional<TerminalSemanticSnapshot>
ServerTerminalRuntime::scrollback_page(
    uint64_t offset_from_live, size_t max_rows) const
{
    const uint64_t total = scrollback_rows();
    const uint64_t offset = std::min(offset_from_live, total);
    const size_t count = std::min({
        max_rows,
        static_cast<size_t>(offset),
        kRemoteTerminalMaxScrollbackPageRows,
    });
    if (count == 0 || scrollback_.cols() <= 0)
        return std::nullopt;

    const uint64_t start = total - offset;
    TerminalSemanticSnapshot page;
    page.cols = scrollback_.cols();
    page.rows = static_cast<int>(count);
    page.metadata.cursor.visible = false;
    page.cells.reserve(count * static_cast<size_t>(page.cols));
    for (size_t row_index = 0; row_index < count; ++row_index)
    {
        const auto row = scrollback_.row_at(
            static_cast<int>(start + row_index));
        for (const Cell& cell : row)
        {
            const uint16_t link_id = cell.hyperlink_id != 0
                ? cell.hyperlink_id
                : cell.detected_url_id;
            page.cells.push_back(capture_terminal_cell_snapshot(
                cell, highlights_,
                link_id != 0 ? grid_.link_uri(link_id)
                             : std::string_view{}));
        }
    }
    return page;
}

std::optional<std::string>
ServerTerminalRuntime::take_clipboard_write()
{
    return std::exchange(pending_clipboard_write_, std::nullopt);
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
    if (cols != grid_.cols())
        scrollback_.resize(cols);
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
    pending_clipboard_write_ = clipboard_;
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

void ServerTerminalRuntime::terminal_line_scrolled_off(int row)
{
    Cell* slot = scrollback_.next_write_slot();
    if (!slot)
        return;
    for (int col = 0; col < grid_.cols(); ++col)
        slot[col] = grid_.get_cell(col, row);
    scrollback_.commit_push();
}

void ServerTerminalRuntime::terminal_mouse_mode_changed(int, bool)
{
}

void ServerTerminalRuntime::terminal_collect_extra_attr_ids(
    std::unordered_map<uint16_t, HlAttr>& active_attrs)
{
    scrollback_.for_each_cell([this, &active_attrs](const Cell& cell) {
        if (cell.hl_attr_id != 0)
        {
            active_attrs.try_emplace(
                cell.hl_attr_id, highlights_.get(cell.hl_attr_id));
        }
    });
}

void ServerTerminalRuntime::terminal_remap_extra_highlight_ids(
    const std::function<uint16_t(uint16_t)>& remap_fn)
{
    scrollback_.remap_highlight_ids(remap_fn);
}

} // namespace draxul
