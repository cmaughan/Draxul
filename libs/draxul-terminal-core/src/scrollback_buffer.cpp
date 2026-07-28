#include <draxul/scrollback_buffer.h>

#include <algorithm>
#include <cassert>
#include <draxul/perf_timing.h>

namespace draxul
{

ScrollbackBuffer::ScrollbackBuffer(Callbacks cbs, int capacity)
    : cbs_(std::move(cbs))
    , capacity_(capacity > 0 ? capacity : kDefaultCapacity)
{
}

void ScrollbackBuffer::set_capacity(int capacity)
{
    PERF_MEASURE();
    const int new_capacity = capacity > 0 ? capacity : kDefaultCapacity;
    if (new_capacity == capacity_)
        return;

    if (cols_ <= 0)
    {
        capacity_ = new_capacity;
        return;
    }

    const int keep = std::min(count_, new_capacity);
    std::vector<Cell> retained(static_cast<size_t>(keep) * cols_);
    const int first = count_ - keep;
    for (int i = 0; i < keep; ++i)
    {
        const auto src = row(first + i);
        auto* dst = &retained[static_cast<size_t>(i) * cols_];
        for (int col = 0; col < cols_; ++col)
            dst[col] = src[col];
    }

    capacity_ = new_capacity;
    storage_.assign(static_cast<size_t>(capacity_) * cols_, Cell{});
    for (int i = 0; i < keep; ++i)
    {
        const auto* src = &retained[static_cast<size_t>(i) * cols_];
        auto* dst = &storage_[static_cast<size_t>(i) * cols_];
        for (int col = 0; col < cols_; ++col)
            dst[col] = src[col];
    }

    write_head_ = keep % capacity_;
    count_ = keep;
    offset_ = std::min(offset_, count_);
}

void ScrollbackBuffer::resize(int cols)
{
    PERF_MEASURE();
    if (cols == cols_)
        return;

    const int old_cols = cols_;
    const int old_count = count_;

    if (old_count > 0 && old_cols > 0)
    {
        // Preserve existing scrollback rows across the resize.
        // Copy rows oldest-first into a temporary buffer, clamping/extending columns.
        const int copy_cols = std::min(old_cols, cols);
        std::vector<Cell> tmp((size_t)old_count * cols);
        for (int i = 0; i < old_count; ++i)
        {
            const auto src = row(i);
            auto* dst = &tmp[(size_t)i * cols];
            for (int c = 0; c < copy_cols; ++c)
                dst[c] = src[c];
            // Extra columns (if cols > old_cols) are already default-constructed.
        }

        // Reallocate storage and copy rows back.
        cols_ = cols;
        storage_.assign((size_t)capacity_ * cols, Cell{});
        for (int i = 0; i < old_count; ++i)
        {
            const auto* src = &tmp[(size_t)i * cols];
            auto* dst = &storage_[(size_t)i * cols];
            for (int c = 0; c < cols; ++c)
                dst[c] = src[c];
        }
        write_head_ = old_count % capacity_;
        count_ = old_count;
        // Reset scroll offset since row layout has changed.
        offset_ = 0;

        // Resize the live snapshot columns if one is active.
        if (!live_snapshot_.empty() && live_snapshot_cols_ > 0 && live_snapshot_rows_ > 0)
        {
            const int snap_copy_cols = std::min(live_snapshot_cols_, cols);
            std::vector<Cell> new_snap((size_t)live_snapshot_rows_ * cols);
            for (int r = 0; r < live_snapshot_rows_; ++r)
            {
                for (int c = 0; c < snap_copy_cols; ++c)
                    new_snap[(size_t)r * cols + c] = live_snapshot_[(size_t)r * live_snapshot_cols_ + c];
            }
            live_snapshot_ = std::move(new_snap);
            live_snapshot_cols_ = cols;
        }
    }
    else
    {
        // No existing scrollback — just allocate fresh storage.
        cols_ = cols;
        storage_.assign((size_t)capacity_ * cols, Cell{});
        write_head_ = 0;
        count_ = 0;
        offset_ = 0;
        live_snapshot_.clear();
        live_snapshot_cols_ = 0;
        live_snapshot_rows_ = 0;
    }
}

Cell* ScrollbackBuffer::next_write_slot()
{
    if (cols_ == 0)
        return nullptr;
    return &storage_[(size_t)write_head_ * cols_];
}

void ScrollbackBuffer::commit_push()
{
    PERF_MEASURE();
    write_head_ = (write_head_ + 1) % capacity_;
    if (count_ < capacity_)
        ++count_;
}

void ScrollbackBuffer::push_row(const Cell* cells, int ncols)
{
    Cell* slot = next_write_slot();
    if (!slot)
        return;
    const int copy = std::min(ncols, cols_);
    for (int c = 0; c < copy; ++c)
        slot[c] = cells[c];
    commit_push();
}

void ScrollbackBuffer::pop_newest_rows(int n, const std::function<void(std::span<const Cell>)>& visitor)
{
    n = std::min(n, count_);
    for (int i = 0; i < n; ++i)
    {
        write_head_ = (write_head_ - 1 + capacity_) % capacity_;
        --count_;
        visitor(std::span<const Cell>(&storage_[(size_t)write_head_ * cols_], (size_t)cols_));
    }
    // Clamp scroll offset to new size.
    if (offset_ > count_)
        offset_ = count_;
}

std::span<const Cell> ScrollbackBuffer::row(int i) const
{
    assert(i >= 0 && i < count_);
    // When not full, the oldest slot is index 0 (write_head_ started at 0).
    // When full, the oldest slot is write_head_ (it will be overwritten next).
    const int oldest = (count_ < capacity_) ? 0 : write_head_;
    const int slot = (oldest + i) % capacity_;
    return std::span<const Cell>(&storage_[(size_t)slot * cols_], (size_t)cols_);
}

void ScrollbackBuffer::scroll(int rows_delta)
{
    PERF_MEASURE();
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
    PERF_MEASURE();
    if (offset_ == 0)
        return;
    offset_ = 0;
    restore_live_snapshot();
}

void ScrollbackBuffer::save_live_snapshot(int cols, int rows)
{
    PERF_MEASURE();
    live_snapshot_cols_ = cols;
    live_snapshot_rows_ = rows;
    live_snapshot_.resize((size_t)cols * rows);
    for (int r = 0; r < rows; ++r)
        for (int col = 0; col < cols; ++col)
            live_snapshot_[(size_t)r * cols + col] = cbs_.get_cell(col, r);
}

void ScrollbackBuffer::reset()
{
    PERF_MEASURE();
    write_head_ = 0;
    count_ = 0;
    offset_ = 0;
    live_snapshot_.clear();
    live_snapshot_cols_ = 0;
    live_snapshot_rows_ = 0;
}

void ScrollbackBuffer::restore_live_snapshot()
{
    PERF_MEASURE();
    const int rows = cbs_.grid_rows();
    const int cols = cbs_.grid_cols();
    // Only copy columns that existed at snapshot time; blank-fill any extra
    // columns that appeared due to a resize since the snapshot was taken.
    const int copy_cols = std::min(cols, live_snapshot_cols_);
    for (int r = 0; r < rows; ++r)
    {
        for (int col = 0; col < copy_cols; ++col)
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
        // Blank-fill columns beyond the snapshot width.
        for (int col = copy_cols; col < cols; ++col)
        {
            Cell blank;
            blank.text.assign(" ");
            cbs_.set_cell(col, r, blank);
        }
    }
    live_snapshot_.clear();
    cbs_.force_full_redraw();
    cbs_.flush_grid();
}

void ScrollbackBuffer::update_display()
{
    PERF_MEASURE();
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
            const int snap_cols = std::min(cols, live_snapshot_cols_);
            for (int col = 0; col < snap_cols; ++col)
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
            // Blank-fill columns beyond the snapshot width.
            for (int col = snap_cols; col < cols; ++col)
            {
                Cell blank;
                blank.text.assign(" ");
                cbs_.set_cell(col, dr, blank);
            }
        }
    }

    cbs_.force_full_redraw();
    cbs_.flush_grid();
}

} // namespace draxul
