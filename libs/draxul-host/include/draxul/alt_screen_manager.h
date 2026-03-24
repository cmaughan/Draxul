#pragma once

#include <draxul/grid.h>
#include <functional>
#include <vector>

namespace draxul
{

// Manages main-screen / alt-screen switching for a terminal emulator.
// On enter() the current grid is snapshotted; on leave() the snapshot is
// restored.  The manager also handles resize-aware snapshot re-dimensioning
// (called by TerminalHostBase::on_viewport_changed before apply_grid_size).
class AltScreenManager
{
public:
    struct GridAccessors
    {
        // Return the current grid dimensions.
        std::function<int()> grid_cols;
        std::function<int()> grid_rows;
        // Read / write individual cells.
        std::function<Cell(int col, int row)> get_cell;
        std::function<void(int col, int row, const Cell&)> set_cell;
        // Clear the entire grid.
        std::function<void()> clear_grid;
    };

    explicit AltScreenManager(GridAccessors accessors);

    bool in_alt_screen() const
    {
        return in_alt_screen_;
    }

    // Snapshot the main screen and switch to alt screen.
    // Receives the current cursor position; resets scroll region to full
    // screen and clears pending_wrap.
    void enter(int term_col, int term_row,
        int& scroll_top_out, int& scroll_bottom_out, bool& pending_wrap_out);

    // Restore the main-screen snapshot.  Sets term_col/term_row to the saved
    // cursor position and restores the saved scroll region.
    void leave(int& term_col_out, int& term_row_out, bool& pending_wrap_out,
        int& scroll_top_out, int& scroll_bottom_out);

    // Re-dimension the snapshot when the terminal is resized while in alt screen.
    // Must be called before the grid is resized to (new_cols x new_rows);
    // prev_cols/prev_rows are the dimensions at snapshot time.
    void resize_snapshot(int new_cols, int new_rows, int prev_cols, int prev_rows);

    // Clamp the saved cursor position into valid bounds after a resize.
    void clamp_saved_cursor(int max_col, int max_row);
    template <typename Fn>
    void for_each_saved_cell(Fn&& fn) const;
    template <typename Remap>
    void remap_saved_highlight_ids(Remap&& remap);

    // Reset all state (called from reset_terminal_state).
    void reset();

private:
    struct Snapshot
    {
        std::vector<Cell> cells;
        int col = 0;
        int row = 0;
        int scroll_top = 0;
        int scroll_bottom = 0;
    };

    GridAccessors acc_;
    bool in_alt_screen_ = false;
    Snapshot saved_main_;
};

template <typename Fn>
void AltScreenManager::for_each_saved_cell(Fn&& fn) const
{
    for (const auto& cell : saved_main_.cells)
        fn(cell);
}

template <typename Remap>
void AltScreenManager::remap_saved_highlight_ids(Remap&& remap)
{
    for (auto& cell : saved_main_.cells)
        cell.hl_attr_id = remap(cell.hl_attr_id);
}

} // namespace draxul
