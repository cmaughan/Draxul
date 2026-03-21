#include <draxul/scrollback_buffer.h>

#include <algorithm>
#include <cassert>

namespace draxul
{

ScrollbackBuffer::ScrollbackBuffer(Callbacks cbs)
    : cbs_(std::move(cbs))
{
}

void ScrollbackBuffer::resize(int cols)
{
    if (cols == cols_)
        return;
    cols_ = cols;
    storage_.assign((size_t)kCapacity * cols, Cell{});
    write_head_ = 0;
    count_ = 0;
    offset_ = 0;
    live_snapshot_.clear();
    live_snapshot_cols_ = 0;
    live_snapshot_rows_ = 0;
}

Cell* ScrollbackBuffer::next_write_slot()
{
    if (cols_ == 0)
        return nullptr;
    return &storage_[(size_t)write_head_ * cols_];
}

void ScrollbackBuffer::commit_push()
{
    write_head_ = (write_head_ + 1) % kCapacity;
    if (count_ < kCapacity)
        ++count_;
}

std::span<const Cell> ScrollbackBuffer::row(int i) const
{
    assert(i >= 0 && i < count_);
    // When not full, the oldest slot is index 0 (write_head_ started at 0).
    // When full, the oldest slot is write_head_ (it will be overwritten next).
    const int oldest = (count_ < kCapacity) ? 0 : write_head_;
    const int slot = (oldest + i) % kCapacity;
    return std::span<const Cell>(&storage_[(size_t)slot * cols_], (size_t)cols_);
}

void ScrollbackBuffer::scroll(int rows_delta)
{
    const int max_offset = count_;
    const int new_offset = std::clamp(offset_ + rows_delta, 0, max_offset);
    if (new_offset == offset_)
        return;

    if (offset_ == 0 && new_offset > 0)
    {
        const int cols = cbs_.grid_cols();
        const int rows = cbs_.grid_rows();
        save_live_snapshot(cols, rows);
    }

    offset_ = new_offset;

    if (offset_ == 0)
        restore_live_snapshot();
    else
        update_display();
}

void ScrollbackBuffer::scroll_to_live()
{
    if (offset_ == 0)
        return;
    offset_ = 0;
    restore_live_snapshot();
}

void ScrollbackBuffer::save_live_snapshot(int cols, int rows)
{
    live_snapshot_cols_ = cols;
    live_snapshot_rows_ = rows;
    live_snapshot_.resize((size_t)cols * rows);
    for (int r = 0; r < rows; ++r)
        for (int col = 0; col < cols; ++col)
            live_snapshot_[(size_t)r * cols + col] = cbs_.get_cell(col, r);
}

void ScrollbackBuffer::reset()
{
    write_head_ = 0;
    count_ = 0;
    offset_ = 0;
    live_snapshot_.clear();
    live_snapshot_cols_ = 0;
    live_snapshot_rows_ = 0;
}

void ScrollbackBuffer::restore_live_snapshot()
{
    const int rows = cbs_.grid_rows();
    const int cols = cbs_.grid_cols();
    for (int r = 0; r < rows; ++r)
    {
        for (int col = 0; col < cols; ++col)
        {
            const size_t idx = (size_t)r * live_snapshot_cols_ + col;
            if (idx < live_snapshot_.size())
            {
                cbs_.set_cell(col, r, live_snapshot_[idx]);
            }
            else
            {
                Cell blank;
                blank.text.assign(" ");
                cbs_.set_cell(col, r, blank);
            }
        }
    }
    live_snapshot_.clear();
    cbs_.force_full_redraw();
    cbs_.flush_grid();
}

void ScrollbackBuffer::update_display()
{
    const int rows = cbs_.grid_rows();
    const int cols = cbs_.grid_cols();
    const int sbsize = count_;
    const int virtual_start = sbsize - offset_;

    for (int dr = 0; dr < rows; ++dr)
    {
        const int vr = virtual_start + dr;
        if (vr < 0)
        {
            Cell blank;
            blank.text.assign(" ");
            for (int col = 0; col < cols; ++col)
                cbs_.set_cell(col, dr, blank);
        }
        else if (vr < sbsize)
        {
            const auto sb_row = row(vr);
            const auto sb_cols = static_cast<int>(sb_row.size());
            for (int col = 0; col < cols; ++col)
            {
                if (col < sb_cols)
                {
                    cbs_.set_cell(col, dr, sb_row[col]);
                }
                else
                {
                    Cell blank;
                    blank.text.assign(" ");
                    cbs_.set_cell(col, dr, blank);
                }
            }
        }
        else
        {
            const int lr = vr - sbsize;
            for (int col = 0; col < cols; ++col)
            {
                const size_t idx = (size_t)lr * live_snapshot_cols_ + col;
                if (idx < live_snapshot_.size())
                {
                    cbs_.set_cell(col, dr, live_snapshot_[idx]);
                }
                else
                {
                    Cell blank;
                    blank.text.assign(" ");
                    cbs_.set_cell(col, dr, blank);
                }
            }
        }
    }

    cbs_.force_full_redraw();
    cbs_.flush_grid();
}

} // namespace draxul
