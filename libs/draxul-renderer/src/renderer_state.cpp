#include <draxul/renderer_state.h>

#include <algorithm>
#include <cstring>
#include <draxul/perf_timing.h>

namespace draxul
{

bool RendererState::has_dirty_cells() const
{
    return dirty_cell_begin_ < dirty_cell_end_;
}

size_t RendererState::dirty_cell_offset_bytes() const
{
    return dirty_cell_begin_ * sizeof(GpuCell);
}

size_t RendererState::dirty_cell_size_bytes() const
{
    if (!has_dirty_cells())
        return 0;
    return (dirty_cell_end_ - dirty_cell_begin_) * sizeof(GpuCell);
}

void RendererState::copy_dirty_cells_to(std::byte* dst) const
{
    PERF_MEASURE();
    if (!has_dirty_cells())
        return;

    std::memcpy(dst, gpu_cells_.data() + dirty_cell_begin_, dirty_cell_size_bytes());
}

bool RendererState::overlay_region_dirty() const
{
    return overlay_dirty_;
}

size_t RendererState::overlay_offset_bytes() const
{
    return gpu_cells_.size() * sizeof(GpuCell);
}

size_t RendererState::overlay_region_size_bytes() const
{
    return (OVERLAY_CELL_CAPACITY + 1) * sizeof(GpuCell);
}

void RendererState::copy_overlay_region_to(std::byte* dst) const
{
    PERF_MEASURE();
    if (!overlay_cells_.empty())
    {
        std::memcpy(dst, overlay_cells_.data(), overlay_cell_count_ * sizeof(GpuCell));
    }

    GpuCell overlay = cursor_overlay_active_ ? overlay_cell_ : GpuCell{};
    std::memcpy(dst + overlay_cell_count_ * sizeof(GpuCell), &overlay, sizeof(GpuCell));

    const size_t clear_begin = overlay_cell_count_ + 1;
    if (clear_begin < OVERLAY_CELL_CAPACITY + 1)
    {
        std::memset(dst + clear_begin * sizeof(GpuCell), 0,
            (OVERLAY_CELL_CAPACITY + 1 - clear_begin) * sizeof(GpuCell));
    }
}

void RendererState::clear_dirty()
{
    dirty_cell_begin_ = 0;
    dirty_cell_end_ = 0;
    overlay_dirty_ = false;
}

void RendererState::set_grid_size(int cols, int rows, int padding)
{
    PERF_MEASURE();
    const int old_cols = grid_cols_;
    const int old_rows = grid_rows_;

    grid_cols_ = cols;
    grid_rows_ = rows;
    padding_ = padding;
    cursor_applied_ = false;
    cursor_overlay_active_ = false;
    overlay_cell_count_ = 0;

    // When the grid dimensions actually change, preserve existing cell content
    // (glyph atlas UVs, colors) so the pane doesn't flash blank for a frame
    // while waiting for the host to redraw after the resize.
    if (old_cols > 0 && old_rows > 0 && (old_cols != cols || old_rows != rows))
    {
        std::vector<GpuCell> old_cells = std::move(gpu_cells_);
        gpu_cells_.resize((size_t)cols * rows);

        const int copy_cols = std::min(old_cols, cols);
        const int copy_rows = std::min(old_rows, rows);

        for (int r = 0; r < rows; r++)
        {
            for (int c = 0; c < cols; c++)
            {
                auto& cell = gpu_cells_[(size_t)r * cols + c];
                if (r < copy_rows && c < copy_cols)
                {
                    // Remap from old stride to new stride, preserving content
                    cell = old_cells[(size_t)r * old_cols + c];
                }
                else
                {
                    cell = GpuCell{};
                    cell.bg = { default_bg_.r, default_bg_.g, default_bg_.b, default_bg_.a };
                    cell.fg = glm::vec4(1.0f);
                    cell.sp = glm::vec4(1.0f);
                }
                // Update position for new layout
                cell.pos = { (float)(c * cell_w_ + padding_), (float)(r * cell_h_ + padding_) };
                cell.size = { (float)cell_w_, (float)cell_h_ };
            }
        }

        std::ranges::fill(overlay_cells_, GpuCell{});
        mark_all_cells_dirty();
        overlay_dirty_ = true;
    }
    else
    {
        gpu_cells_.resize((size_t)cols * rows);
        relayout();
    }
}

void RendererState::set_cell_size(int w, int h)
{
    PERF_MEASURE();
    if (w == cell_w_ && h == cell_h_)
        return;
    cell_w_ = w;
    cell_h_ = h;
    relayout();
}

void RendererState::set_ascender(int a)
{
    ascender_ = a;
}

void RendererState::set_default_background(Color bg)
{
    default_bg_ = bg;
}

void RendererState::apply_update_to_cell(GpuCell& cell, const CellUpdate& u) const
{
    PERF_MEASURE();
    cell = {};
    cell.pos = { (float)(u.col * cell_w_ + padding_), (float)(u.row * cell_h_ + padding_) };
    cell.size = { (float)cell_w_, (float)cell_h_ };
    cell.bg = { u.bg.r, u.bg.g, u.bg.b, u.bg.a };
    cell.fg = { u.fg.r, u.fg.g, u.fg.b, u.fg.a };
    cell.sp = { u.sp.r, u.sp.g, u.sp.b, u.sp.a };
    cell.uv = u.glyph.uv;
    cell.glyph_offset = { (float)u.glyph.bearing.x, (float)(cell_h_ - ascender_ + u.glyph.bearing.y) };
    cell.glyph_size = glm::vec2(u.glyph.size);
    cell.style_flags = u.style_flags;
}

void RendererState::relayout()
{
    PERF_MEASURE();
    std::ranges::fill(gpu_cells_, GpuCell{});
    std::ranges::fill(overlay_cells_, GpuCell{});

    for (int r = 0; r < grid_rows_; r++)
    {
        for (int c = 0; c < grid_cols_; c++)
        {
            auto& cell = gpu_cells_[(size_t)r * grid_cols_ + c];
            cell.pos = { (float)(c * cell_w_ + padding_), (float)(r * cell_h_ + padding_) };
            cell.size = { (float)cell_w_, (float)cell_h_ };
            cell.bg = { default_bg_.r, default_bg_.g, default_bg_.b, default_bg_.a };
            cell.fg = glm::vec4(1.0f);
            cell.sp = glm::vec4(1.0f);
        }
    }

    mark_all_cells_dirty();
    overlay_dirty_ = true;
}

void RendererState::update_cells(std::span<const CellUpdate> updates)
{
    PERF_MEASURE();
    restore_cursor();

    for (const auto& u : updates)
    {
        if (u.col < 0 || u.col >= grid_cols_ || u.row < 0 || u.row >= grid_rows_)
            continue;

        auto& cell = gpu_cells_[(size_t)u.row * grid_cols_ + u.col];
        apply_update_to_cell(cell, u);
        mark_cell_dirty((size_t)u.row * grid_cols_ + u.col);
    }
}

void RendererState::set_overlay_cells(std::span<const CellUpdate> updates)
{
    PERF_MEASURE();
    overlay_cell_count_ = std::min(updates.size(), OVERLAY_CELL_CAPACITY);
    std::ranges::fill(overlay_cells_, GpuCell{});

    for (size_t i = 0; i < overlay_cell_count_; ++i)
        apply_update_to_cell(overlay_cells_[i], updates[i]);

    overlay_dirty_ = true;
}

void RendererState::set_cursor(int col, int row, const CursorStyle& style)
{
    PERF_MEASURE();
    restore_cursor();
    cursor_col_ = col;
    cursor_row_ = row;
    cursor_style_ = style;
}

void RendererState::restore_cursor()
{
    PERF_MEASURE();
    if (!cursor_applied_)
        return;

    cursor_applied_ = false;
    cursor_overlay_active_ = false;

    if (cursor_col_ < 0 || cursor_col_ >= grid_cols_ || cursor_row_ < 0 || cursor_row_ >= grid_rows_)
        return;

    int idx = cursor_row_ * grid_cols_ + cursor_col_;
    auto& cell = gpu_cells_[(size_t)idx];
    if (!(cell == cursor_saved_cell_))
    {
        cell = cursor_saved_cell_;
        mark_cell_dirty((size_t)idx);
    }
}

void RendererState::apply_cursor()
{
    PERF_MEASURE();
    if (!cursor_visible_)
        return;
    if (cursor_col_ < 0 || cursor_col_ >= grid_cols_ || cursor_row_ < 0 || cursor_row_ >= grid_rows_)
        return;

    int idx = cursor_row_ * grid_cols_ + cursor_col_;
    auto& cell = gpu_cells_[(size_t)idx];

    cursor_saved_cell_ = cell;
    cursor_applied_ = true;
    cursor_overlay_active_ = false;

    if (cursor_style_.shape == CursorShape::Block)
    {
        // Always use the cursor style colors for a consistent, visible block cursor.
        // Swapping the cell's fg/bg causes the cursor color to follow the character color
        // underneath (red cursor on a red keyword, etc.). cursor_style_.bg is guaranteed to
        // contrast with the background: either a properly-resolved cursor highlight (reverse
        // or explicit fg/bg), or the default_fg() fallback set in refresh_cursor_style().
        cell.fg = { cursor_style_.fg.r, cursor_style_.fg.g, cursor_style_.fg.b, cursor_style_.fg.a };
        cell.bg = { cursor_style_.bg.r, cursor_style_.bg.g, cursor_style_.bg.b, cursor_style_.bg.a };
        mark_cell_dirty((size_t)idx);
        return;
    }

    overlay_cell_ = {};
    overlay_cell_.bg = { cursor_style_.bg.r, cursor_style_.bg.g, cursor_style_.bg.b, cursor_style_.bg.a };

    int percentage = cursor_style_.cell_percentage;
    if (percentage <= 0)
        percentage = (cursor_style_.shape == CursorShape::Vertical) ? 25 : 20;

    if (cursor_style_.shape == CursorShape::Vertical)
    {
        overlay_cell_.pos = cell.pos;
        overlay_cell_.size = { std::max(1.0f, cell.size.x * static_cast<float>(percentage) / 100.0f), cell.size.y };
    }
    else
    {
        overlay_cell_.size.y = std::max(1.0f, cell.size.y * static_cast<float>(percentage) / 100.0f);
        overlay_cell_.pos = { cell.pos.x, cell.pos.y + cell.size.y - overlay_cell_.size.y };
        overlay_cell_.size.x = cell.size.x;
    }

    cursor_overlay_active_ = true;
    overlay_dirty_ = true;
}

void RendererState::copy_to(std::byte* dst) const
{
    PERF_MEASURE();
    if (!gpu_cells_.empty())
    {
        std::memcpy(dst, gpu_cells_.data(), gpu_cells_.size() * sizeof(GpuCell));
    }

    copy_overlay_region_to(dst + gpu_cells_.size() * sizeof(GpuCell));
}

void RendererState::force_dirty()
{
    mark_all_cells_dirty();
    overlay_dirty_ = true;
}

void RendererState::mark_all_cells_dirty()
{
    dirty_cell_begin_ = 0;
    dirty_cell_end_ = gpu_cells_.size();
}

void RendererState::mark_cell_dirty(size_t index)
{
    PERF_MEASURE();
    if (index >= gpu_cells_.size())
        return;

    if (!has_dirty_cells())
    {
        dirty_cell_begin_ = index;
        dirty_cell_end_ = index + 1;
        return;
    }

    dirty_cell_begin_ = std::min(dirty_cell_begin_, index);
    dirty_cell_end_ = std::max(dirty_cell_end_, index + 1);
}

} // namespace draxul
