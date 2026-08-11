#include "server_terminal_runtime.h"

#include <draxul/log.h>

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace draxul
{

namespace
{

constexpr size_t kMaxQueuedInputBytes = 256 * 1024;

size_t scrollback_cells(int capacity, int cols)
{
    const size_t rows = static_cast<size_t>(
        capacity > 0 ? capacity : ScrollbackBuffer::kDefaultCapacity);
    return rows * static_cast<size_t>(std::max(1, cols));
}

} // namespace

ServerTerminalResourceBudget::ServerTerminalResourceBudget(
    size_t max_scrollback_cells)
    : max_scrollback_cells_(max_scrollback_cells)
{
}

bool ServerTerminalResourceBudget::replace_scrollback_reservation(
    size_t current_cells, size_t requested_cells)
{
    std::lock_guard guard(mutex_);
    if (current_cells > reserved_scrollback_cells_)
        return false;
    const size_t other_cells
        = reserved_scrollback_cells_ - current_cells;
    if (requested_cells > max_scrollback_cells_
        || other_cells
            > max_scrollback_cells_ - requested_cells)
    {
        return false;
    }
    reserved_scrollback_cells_ = other_cells + requested_cells;
    return true;
}

size_t ServerTerminalResourceBudget::reserved_scrollback_cells() const
{
    std::lock_guard guard(mutex_);
    return reserved_scrollback_cells_;
}

size_t ServerTerminalResourceBudget::max_scrollback_cells() const noexcept
{
    return max_scrollback_cells_;
}

struct ServerTerminalRuntime::InputQueueState
{
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::string> commands;
    size_t queued_bytes = 0;
    size_t in_flight_bytes = 0;
    bool stopping = false;
    bool write_failed = false;
};

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
    , process_(std::make_unique<Process>())
{
    grid_.resize(80, 24);
    highlights_.set_default_fg(Color(0.92f, 0.92f, 0.92f, 1.0f));
    highlights_.set_default_bg(Color(0.08f, 0.09f, 0.10f, 1.0f));
    reset_terminal_state();
    grid_.clear_dirty();
}

ServerTerminalRuntime::~ServerTerminalRuntime()
{
    retire_process_async();
    restore_scrollback_reservation(0);
}

bool ServerTerminalRuntime::replace_scrollback_reservation(
    int cols, std::string* error)
{
    if (!options_.resource_budget)
        return true;
    const size_t requested
        = scrollback_cells(options_.scrollback_capacity, cols);
    if (!options_.resource_budget->replace_scrollback_reservation(
            reserved_scrollback_cells_, requested))
    {
        if (error)
        {
            *error = "The Draxul server scrollback memory budget is exhausted.";
        }
        return false;
    }
    reserved_scrollback_cells_ = requested;
    return true;
}

void ServerTerminalRuntime::restore_scrollback_reservation(size_t cells)
{
    if (!options_.resource_budget
        || cells == reserved_scrollback_cells_)
    {
        return;
    }
    const bool restored
        = options_.resource_budget->replace_scrollback_reservation(
            reserved_scrollback_cells_, cells);
    if (restored)
        reserved_scrollback_cells_ = cells;
}

void ServerTerminalRuntime::start_input_writer()
{
    input_queue_ = std::make_shared<InputQueueState>();
    const auto queue = input_queue_;
    Process* const process = process_.get();
    input_writer_ = std::thread([queue, process] {
        for (;;)
        {
            std::string command;
            {
                std::unique_lock lock(queue->mutex);
                queue->wake.wait(lock, [&] {
                    return queue->stopping
                        || !queue->commands.empty();
                });
                if (queue->stopping && queue->commands.empty())
                    break;
                command = std::move(queue->commands.front());
                queue->commands.pop_front();
                queue->in_flight_bytes = command.size();
            }

            const bool written = process->write(command);

            {
                std::lock_guard lock(queue->mutex);
                queue->queued_bytes -= std::min(
                    queue->queued_bytes, command.size());
                queue->in_flight_bytes = 0;
                if (!written)
                {
                    queue->write_failed = true;
                    queue->commands.clear();
                    queue->queued_bytes = 0;
                    break;
                }
            }
        }
    });
}

void ServerTerminalRuntime::retire_process_async()
{
    if (!process_)
        return;

    if (input_queue_)
    {
        std::lock_guard lock(input_queue_->mutex);
        input_queue_->stopping = true;
        input_queue_->commands.clear();
        input_queue_->queued_bytes
            = input_queue_->in_flight_bytes;
        input_queue_->wake.notify_all();
    }

    auto process = std::move(process_);
    auto writer = std::move(input_writer_);
    input_queue_.reset();
    std::thread(
        [process = std::move(process),
            writer = std::move(writer)]() mutable {
#ifdef _WIN32
            if (writer.joinable())
            {
                process->cancel_pending_write(
                    static_cast<HANDLE>(writer.native_handle()));
                writer.join();
            }
#else
            process->request_close();
            if (writer.joinable())
                writer.join();
#endif
            process->shutdown();
        })
        .detach();
}

bool ServerTerminalRuntime::ensure_started(std::string& error)
{
    if (process_ && process_->is_running())
        return true;
    return start_process(error);
}

bool ServerTerminalRuntime::restart(std::string& error)
{
    retire_process_async();
    process_ = std::make_unique<Process>();
    reset_terminal_state();
    grid_.clear_dirty();
    return start_process(error);
}

void ServerTerminalRuntime::reset_terminal_state()
{
    grid_.clear();
    scrollback_.reset();
    core_.reset();
    clipboard_.clear();
    pending_clipboard_write_.reset();
    published_title_.clear();
    published_cursor_ = { 0, 0 };
    cursor_override_.reset();
    cursor_shape_ = CursorShape::Block;
    cursor_blink_ = false;
    cursor_visible_ = true;
    agent_output_generation_ = 0;
    agent_last_output_at_.reset();
}

bool ServerTerminalRuntime::start_process(std::string& error)
{
    const std::string working_directory
        = options_.working_directory.empty()
        ? std::filesystem::current_path().string()
        : options_.working_directory;
    const size_t previous_reservation
        = reserved_scrollback_cells_;
    if (!replace_scrollback_reservation(grid_.cols(), &error))
        return false;
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
        restore_scrollback_reservation(previous_reservation);
        error = "Unsupported Draxul server shell kind: "
            + options_.shell_kind;
        return false;
    }
    for (const auto& [command, args] : candidates)
    {
        if (process_->spawn(command, args, working_directory,
                grid_.cols(), grid_.rows(),
                options_.on_output_available,
                options_.environment))
        {
            try
            {
                scrollback_.resize(grid_.cols());
            }
            catch (const std::bad_alloc&)
            {
                process_->shutdown();
                restore_scrollback_reservation(previous_reservation);
                error = "Unable to allocate the server terminal scrollback buffer.";
                return false;
            }
            start_input_writer();
            DRAXUL_LOG_INFO(LogCategory::App,
                "Started server-owned shell pid=%llu command=%s",
                static_cast<unsigned long long>(process_->process_id()),
                command.c_str());
            return true;
        }
    }
    restore_scrollback_reservation(previous_reservation);
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
            restore_scrollback_reservation(previous_reservation);
            error
                = "WSL remote shells are supported only by the Windows server.";
            return false;
        }
        else
        {
            restore_scrollback_reservation(previous_reservation);
            error = "Unsupported Draxul server shell kind: "
                + options_.shell_kind;
            return false;
        }
    }
    if (process_->spawn(command, args, working_directory,
            options_.on_output_available, grid_.cols(), grid_.rows(),
            true, options_.environment))
    {
        try
        {
            scrollback_.resize(grid_.cols());
        }
        catch (const std::bad_alloc&)
        {
            process_->shutdown();
            restore_scrollback_reservation(previous_reservation);
            error = "Unable to allocate the server terminal scrollback buffer.";
            return false;
        }
        start_input_writer();
        DRAXUL_LOG_INFO(LogCategory::App,
            "Started server-owned shell pid=%llu command=%s",
            static_cast<unsigned long long>(process_->process_id()),
            command.c_str());
        return true;
    }
    restore_scrollback_reservation(previous_reservation);
    error = "Could not start the configured shell in the Draxul server.";
#endif
    return false;
}

bool ServerTerminalRuntime::pump()
{
    if (!process_)
        return false;
    auto chunks = process_->drain_output();
    if (chunks.empty())
        return false;
    ++agent_output_generation_;
    agent_last_output_at_
        = std::chrono::steady_clock::now();
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

RemoteTerminalInputResult ServerTerminalRuntime::send_input(
    std::string_view bytes)
{
    const auto queue = input_queue_;
    if (!queue || bytes.empty())
        return RemoteTerminalInputResult::Failed;
    std::lock_guard lock(queue->mutex);
    if (queue->stopping || queue->write_failed)
        return RemoteTerminalInputResult::Failed;
    if (bytes.size() > kMaxQueuedInputBytes
        || queue->queued_bytes
            > kMaxQueuedInputBytes - bytes.size())
    {
        return RemoteTerminalInputResult::Backpressure;
    }
    queue->commands.emplace_back(bytes);
    queue->queued_bytes += bytes.size();
    queue->wake.notify_one();
    return RemoteTerminalInputResult::Accepted;
}

bool ServerTerminalRuntime::resize(int cols, int rows)
{
    if (!process_)
        return false;
    const int previous_cols = grid_.cols();
    const int previous_rows = grid_.rows();
    const size_t previous_reservation
        = reserved_scrollback_cells_;
    const size_t requested_reservation
        = scrollback_cells(
            options_.scrollback_capacity, cols);
    if (options_.resource_budget
        && cols != previous_cols)
    {
        const size_t maximum
            = options_.resource_budget
                  ->max_scrollback_cells();
        if (previous_reservation > maximum
            || requested_reservation
                > maximum - previous_reservation
            || !options_.resource_budget
                    ->replace_scrollback_reservation(
                        previous_reservation,
                        previous_reservation
                            + requested_reservation))
        {
            return false;
        }
        reserved_scrollback_cells_
            = previous_reservation
            + requested_reservation;
    }
    if (!process_->resize(cols, rows))
    {
        restore_scrollback_reservation(previous_reservation);
        return false;
    }
    try
    {
        core_.resize(cols, rows);
    }
    catch (const std::bad_alloc&)
    {
        const bool process_rolled_back
            = process_->resize(
                previous_cols, previous_rows);
        if (!process_rolled_back)
            retire_process_async();
        scrollback_.release_storage();
        restore_scrollback_reservation(0);
        reset_terminal_state();
        DRAXUL_LOG_ERROR(LogCategory::App,
            process_rolled_back
                ? "Unable to allocate resized server terminal buffers; "
                  "scrollback was disabled until restart"
                : "Unable to allocate resized server terminal buffers "
                  "or restore PTY dimensions; the process was retired");
        return false;
    }
    if (cols != previous_cols)
    {
        restore_scrollback_reservation(
            requested_reservation);
    }
    return true;
}

bool ServerTerminalRuntime::is_running() const
{
    return process_ && process_->is_running();
}

uint64_t ServerTerminalRuntime::process_id() const
{
    return process_ ? process_->process_id() : 0;
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

std::optional<AgentObservation>
ServerTerminalRuntime::capture_agent_observation(
    int max_rows, size_t max_bytes) const
{
    AgentObservation observation;
    observation.output_generation
        = agent_output_generation_;
    observation.captured_at
        = std::chrono::steady_clock::now();
    observation.last_output_at = agent_last_output_at_;
    observation.terminal_title = published_title_;
    observation.cursor_visible = cursor_visible_;
    observation.cursor_col = published_cursor_.first;
    observation.cursor_row = published_cursor_.second;
    observation.process_running = process_ && process_->is_running();
    observation.exit_code = process_ ? process_->exit_code() : std::nullopt;

    if (max_rows <= 0 || max_bytes == 0
        || grid_.cols() <= 0 || grid_.rows() <= 0)
    {
        return observation;
    }

    const int first_row
        = std::max(0, grid_.rows() - max_rows);
    size_t remaining = max_bytes;
    observation.bottom_rows.reserve(
        static_cast<size_t>(grid_.rows() - first_row));
    for (int row = first_row;
         row < grid_.rows() && remaining > 0; ++row)
    {
        std::string text;
        for (int col = 0; col < grid_.cols(); ++col)
        {
            const Cell& cell = grid_.get_cell(col, row);
            if (cell.double_width_cont)
                continue;
            const std::string_view cluster = cell.text.view();
            if (cluster.size() > remaining)
                break;
            text.append(cluster);
            remaining -= cluster.size();
        }
        while (!text.empty() && text.back() == ' ')
            text.pop_back();
        observation.bottom_rows.push_back(std::move(text));
    }
    return observation;
}

std::optional<AgentProcessObservation>
ServerTerminalRuntime::capture_agent_process_observation() const
{
    return process_
        ? process_->foreground_process_observation()
        : std::nullopt;
}

std::optional<int> ServerTerminalRuntime::exit_code() const
{
    return process_ ? process_->exit_code() : std::nullopt;
}

bool ServerTerminalRuntime::scrollback_storage_initialized() const noexcept
{
    return scrollback_.cols() > 0;
}

void ServerTerminalRuntime::set_environment_value(
    std::string key, std::string value)
{
    const auto existing = std::ranges::find(
        options_.environment, key,
        &std::pair<std::string, std::string>::first);
    if (existing == options_.environment.end())
    {
        options_.environment.emplace_back(
            std::move(key), std::move(value));
    }
    else
    {
        existing->second = std::move(value);
    }
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
    return send_input(bytes) == RemoteTerminalInputResult::Accepted;
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
