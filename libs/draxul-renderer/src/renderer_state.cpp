#include <draxul/renderer_state.h>

#include <algorithm>
#include <cstring>

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

void RendererState::copy_dirty_cells_to(void* dst) const
{
    if (!has_dirty_cells())
        return;

    auto* bytes = static_cast<std::byte*>(dst);
    std::memcpy(bytes, gpu_cells_.data() + dirty_cell_begin_, dirty_cell_size_bytes());
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

void RendererState::copy_overlay_region_to(void* dst) const
{
    auto* bytes = static_cast<std::byte*>(dst);
    if (!overlay_cells_.empty())
    {
        std::memcpy(bytes, overlay_cells_.data(), overlay_cell_count_ * sizeof(GpuCell));
    }

    GpuCell overlay = cursor_overlay_active_ ? overlay_cell_ : GpuCell{};
    std::memcpy(bytes + overlay_cell_count_ * sizeof(GpuCell), &overlay, sizeof(GpuCell));

    const size_t clear_begin = overlay_cell_count_ + 1;
    if (clear_begin < OVERLAY_CELL_CAPACITY + 1)
    {
        std::memset(bytes + clear_begin * sizeof(GpuCell), 0,
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
    grid_cols_ = cols;
    grid_rows_ = rows;
    padding_ = padding;
    cursor_applied_ = false;
    cursor_overlay_active_ = false;
    overlay_cell_count_ = 0;

    gpu_cells_.resize((size_t)cols * rows);
    relayout();
}

void RendererState::set_cell_size(int w, int h)
{
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
    cell = {};
    cell.pos_x = (float)(u.col * cell_w_ + padding_);
    cell.pos_y = (float)(u.row * cell_h_ + padding_);
    cell.size_x = (float)cell_w_;
    cell.size_y = (float)cell_h_;
    cell.bg_r = u.bg.r;
    cell.bg_g = u.bg.g;
    cell.bg_b = u.bg.b;
    cell.bg_a = u.bg.a;
    cell.fg_r = u.fg.r;
    cell.fg_g = u.fg.g;
    cell.fg_b = u.fg.b;
    cell.fg_a = u.fg.a;
    cell.sp_r = u.sp.r;
    cell.sp_g = u.sp.g;
    cell.sp_b = u.sp.b;
    cell.sp_a = u.sp.a;
    cell.uv_x0 = u.glyph.u0;
    cell.uv_y0 = u.glyph.v0;
    cell.uv_x1 = u.glyph.u1;
    cell.uv_y1 = u.glyph.v1;
    cell.glyph_offset_x = (float)u.glyph.bearing_x;
    cell.glyph_offset_y = (float)(cell_h_ - ascender_ + u.glyph.bearing_y);
    cell.glyph_size_x = (float)u.glyph.width;
    cell.glyph_size_y = (float)u.glyph.height;
    cell.style_flags = u.style_flags;
}

void RendererState::relayout()
{
    std::fill(gpu_cells_.begin(), gpu_cells_.end(), GpuCell{});
    std::fill(overlay_cells_.begin(), overlay_cells_.end(), GpuCell{});

    for (int r = 0; r < grid_rows_; r++)
    {
        for (int c = 0; c < grid_cols_; c++)
        {
            auto& cell = gpu_cells_[(size_t)r * grid_cols_ + c];
            cell.pos_x = (float)(c * cell_w_ + padding_);
            cell.pos_y = (float)(r * cell_h_ + padding_);
            cell.size_x = (float)cell_w_;
            cell.size_y = (float)cell_h_;
            cell.bg_r = default_bg_.r;
            cell.bg_g = default_bg_.g;
            cell.bg_b = default_bg_.b;
            cell.bg_a = default_bg_.a;
            cell.fg_r = 1.0f;
            cell.fg_g = 1.0f;
            cell.fg_b = 1.0f;
            cell.fg_a = 1.0f;
            cell.sp_r = 1.0f;
            cell.sp_g = 1.0f;
            cell.sp_b = 1.0f;
            cell.sp_a = 1.0f;
        }
    }

    mark_all_cells_dirty();
    overlay_dirty_ = true;
}

void RendererState::update_cells(std::span<const CellUpdate> updates)
{
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
    overlay_cell_count_ = std::min(updates.size(), OVERLAY_CELL_CAPACITY);
    std::fill(overlay_cells_.begin(), overlay_cells_.end(), GpuCell{});

    for (size_t i = 0; i < overlay_cell_count_; ++i)
        apply_update_to_cell(overlay_cells_[i], updates[i]);

    overlay_dirty_ = true;
}

void RendererState::set_cursor(int col, int row, const CursorStyle& style)
{
    restore_cursor();
    cursor_col_ = col;
    cursor_row_ = row;
    cursor_style_ = style;
}

void RendererState::restore_cursor()
{
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
        cell.fg_r = cursor_style_.fg.r;
        cell.fg_g = cursor_style_.fg.g;
        cell.fg_b = cursor_style_.fg.b;
        cell.fg_a = cursor_style_.fg.a;
        cell.bg_r = cursor_style_.bg.r;
        cell.bg_g = cursor_style_.bg.g;
        cell.bg_b = cursor_style_.bg.b;
        cell.bg_a = cursor_style_.bg.a;
        mark_cell_dirty((size_t)idx);
        return;
    }

    overlay_cell_ = {};
    overlay_cell_.bg_r = cursor_style_.bg.r;
    overlay_cell_.bg_g = cursor_style_.bg.g;
    overlay_cell_.bg_b = cursor_style_.bg.b;
    overlay_cell_.bg_a = cursor_style_.bg.a;

    int percentage = cursor_style_.cell_percentage;
    if (percentage <= 0)
        percentage = (cursor_style_.shape == CursorShape::Vertical) ? 25 : 20;

    if (cursor_style_.shape == CursorShape::Vertical)
    {
        overlay_cell_.pos_x = cell.pos_x;
        overlay_cell_.pos_y = cell.pos_y;
        overlay_cell_.size_x = std::max(1.0f, cell.size_x * static_cast<float>(percentage) / 100.0f);
        overlay_cell_.size_y = cell.size_y;
    }
    else
    {
        overlay_cell_.pos_x = cell.pos_x;
        overlay_cell_.size_y = std::max(1.0f, cell.size_y * static_cast<float>(percentage) / 100.0f);
        overlay_cell_.pos_y = cell.pos_y + cell.size_y - overlay_cell_.size_y;
        overlay_cell_.size_x = cell.size_x;
    }

    cursor_overlay_active_ = true;
    overlay_dirty_ = true;
}

void RendererState::copy_to(void* dst) const
{
    auto* bytes = static_cast<std::byte*>(dst);
    if (!gpu_cells_.empty())
    {
        std::memcpy(bytes, gpu_cells_.data(), gpu_cells_.size() * sizeof(GpuCell));
    }

    copy_overlay_region_to(bytes + gpu_cells_.size() * sizeof(GpuCell));
}

void RendererState::mark_all_cells_dirty()
{
    dirty_cell_begin_ = 0;
    dirty_cell_end_ = gpu_cells_.size();
}

void RendererState::mark_cell_dirty(size_t index)
{
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
