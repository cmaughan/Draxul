#pragma once

#include <draxul/grid.h>
#include <functional>
#include <span>
#include <vector>

namespace draxul
{

// Renderer-free ring buffer that stores scrolled-off terminal rows and manages
// a projected scrollback viewport. TerminalCore consumers feed rows into it via
// next_write_slot() / commit_push().
//
// Storage is pre-allocated once (kCapacity * cols cells).  No per-row heap
// allocation occurs in the hot path.
class ScrollbackBuffer
{
public:
    struct Callbacks
    {
        // Return the current grid dimensions.
        std::function<int()> grid_cols;
        std::function<int()> grid_rows;
        // Read a cell from the live grid.
        std::function<Cell(int col, int row)> get_cell;
        // Write a cell to the live grid.
        std::function<void(int col, int row, const Cell& c)> set_cell;
        // Mark all grid cells dirty and request a full atlas re-upload.
        std::function<void()> force_full_redraw;
        // Publish the updated projected grid to the consumer.
        std::function<void()> flush_grid;
    };

    static constexpr int kDefaultCapacity = 10000;

    explicit ScrollbackBuffer(Callbacks cbs, int capacity = kDefaultCapacity);

    void set_capacity(int capacity);

    // (Re-)allocate ring-buffer storage for rows of `cols` cells.
    // Discards all existing scrollback content.  Must be called once before
    // any push, and again whenever the terminal column count changes.
    void resize(int cols);

    // Two-phase push (zero extra allocation):
    //   1. Obtain a writable slot:  Cell* slot = next_write_slot();
    //   2. Fill it with `cols()` cells.
    //   3. Commit:                  commit_push();
    // Returns nullptr if resize() has not been called yet (cols == 0).
    Cell* next_write_slot();
    void commit_push();

    // Push a row of cells directly (convenience wrapper around next_write_slot / commit_push).
    void push_row(const Cell* cells, int ncols);

    // Pop the N newest rows from the ring buffer. Visits each row newest-first
    // via the callback. Used during resize-grow to pull rows back from scrollback.
    void pop_newest_rows(int n, const std::function<void(std::span<const Cell>)>& visitor);

    // Number of rows currently stored.
    int size() const
    {
        return count_;
    }

    // Column count the ring buffer was sized for.
    int cols() const
    {
        return cols_;
    }

    // Current scrollback offset (0 = live view, >0 = rows scrolled back).
    int offset() const
    {
        return offset_;
    }

    // Read one retained row by oldest-first index without changing the projected
    // viewport. Server runtimes use this to publish semantic, paged history while
    // leaving viewport ownership with each client.
    std::span<const Cell> row_at(int index) const;

    bool is_scrolled_back() const
    {
        return offset_ > 0;
    }

    // Scroll the viewport by rows_delta rows.  Saves / restores the live
    // snapshot as needed.
    void scroll(int rows_delta);

    // Return to the live view and restore the live grid snapshot.
    void scroll_to_live();

    // Save the current live grid into the live snapshot (used before entering
    // scrollback view).
    void save_live_snapshot(int cols, int rows);

    // Reset all scrollback state (called from reset_terminal_state).
    // Does not reallocate storage — keeps cols() and the ring buffer alive.
    void reset();

    // Invoke `fn(const Cell&)` for every cell in every stored scrollback row.
    template <typename Fn>
    void for_each_cell(Fn&& fn) const
    {
        for (int i = 0; i < count_; ++i)
        {
            const auto r = row(i);
            for (const auto& cell : r)
                fn(cell);
        }
    }

    // Remap highlight IDs in all stored scrollback rows using a functor
    // that maps old IDs to new IDs: `uint16_t fn(uint16_t old_id)`.
    template <typename Fn>
    void remap_highlight_ids(Fn&& fn)
    {
        for (auto& cell : storage_)
            cell.hl_attr_id = fn(cell.hl_attr_id);
        for (auto& cell : live_snapshot_)
            cell.hl_attr_id = fn(cell.hl_attr_id);
    }

private:
    // row(i): i=0 is the oldest row, i=size()-1 is the newest.
    std::span<const Cell> row(int i) const;

    void restore_live_snapshot();
    void update_display();

    Callbacks cbs_;

    int capacity_;

    // Flat ring-buffer storage: capacity_ * cols_ cells, pre-allocated once.
    std::vector<Cell> storage_;
    int cols_ = 0;
    int write_head_ = 0; // index of the next slot to write (0-based, mod capacity_)
    int count_ = 0; // number of valid rows stored (0..capacity_)

    int offset_ = 0;

    // Snapshot of the live grid taken when the user first scrolls back.
    std::vector<Cell> live_snapshot_;
    int live_snapshot_cols_ = 0;
    int live_snapshot_rows_ = 0;
};

} // namespace draxul
