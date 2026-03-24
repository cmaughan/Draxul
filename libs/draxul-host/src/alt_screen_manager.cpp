#include <draxul/alt_screen_manager.h>

#include <algorithm>
#include <draxul/log.h>

namespace draxul
{

AltScreenManager::AltScreenManager(GridAccessors accessors)
    : acc_(std::move(accessors))
{
}

void AltScreenManager::enter(int term_col, int term_row,
    int& scroll_top_out, int& scroll_bottom_out, bool& pending_wrap_out)
{
    if (in_alt_screen_)
        return;

    const int cols = acc_.grid_cols();
    const int rows = acc_.grid_rows();

    saved_main_.col = term_col;
    saved_main_.row = term_row;
    saved_main_.scroll_top = scroll_top_out;
    saved_main_.scroll_bottom = scroll_bottom_out;
    saved_main_.cells.resize((size_t)cols * rows);
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            saved_main_.cells[(size_t)row * cols + col] = acc_.get_cell(col, row);

    in_alt_screen_ = true;
    acc_.clear_grid();
    scroll_top_out = 0;
    scroll_bottom_out = rows - 1;
    pending_wrap_out = false;
}

void AltScreenManager::leave(int& term_col_out, int& term_row_out, bool& pending_wrap_out,
    int& scroll_top_out, int& scroll_bottom_out)
{
    if (!in_alt_screen_)
        return;

    in_alt_screen_ = false;

    const int cols = acc_.grid_cols();
    const int rows = acc_.grid_rows();

    DRAXUL_LOG_DEBUG(LogCategory::App,
        "leave_alt_screen: snapshot %zu cells, current grid %d×%d (%d cells)",
        saved_main_.cells.size(), cols, rows, cols * rows);

    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            const size_t idx = (size_t)row * cols + col;
            if (idx < saved_main_.cells.size())
                acc_.set_cell(col, row, saved_main_.cells[idx]);
        }
    }

    term_col_out = std::clamp(saved_main_.col, 0, std::max(0, cols - 1));
    term_row_out = std::clamp(saved_main_.row, 0, std::max(0, rows - 1));
    scroll_top_out = std::clamp(saved_main_.scroll_top, 0, std::max(0, rows - 1));
    scroll_bottom_out = std::clamp(saved_main_.scroll_bottom, scroll_top_out, std::max(0, rows - 1));
    saved_main_.cells.clear();
    pending_wrap_out = false;
}

void AltScreenManager::resize_snapshot(int new_cols, int new_rows, int prev_cols, int prev_rows)
{
    if (!in_alt_screen_ || saved_main_.cells.empty())
        return;

    std::vector<Cell> resized(static_cast<size_t>(new_cols) * new_rows);
    const Cell blank{};
    for (int r = 0; r < new_rows; ++r)
    {
        for (int c = 0; c < new_cols; ++c)
        {
            const size_t dst = static_cast<size_t>(r) * new_cols + c;
            if (r < prev_rows && c < prev_cols)
            {
                const size_t idx = static_cast<size_t>(r) * prev_cols + c;
                const Cell source = idx < saved_main_.cells.size() ? saved_main_.cells[idx] : blank;
                resized[dst] = source;
            }
            else
            {
                resized[dst] = blank;
            }
        }
    }
    saved_main_.cells = std::move(resized);
}

void AltScreenManager::clamp_saved_cursor(int max_col, int max_row)
{
    saved_main_.col = std::clamp(saved_main_.col, 0, max_col);
    saved_main_.row = std::clamp(saved_main_.row, 0, max_row);
}

void AltScreenManager::reset()
{
    in_alt_screen_ = false;
    saved_main_.cells.clear();
    saved_main_.col = 0;
    saved_main_.row = 0;
}

} // namespace draxul
