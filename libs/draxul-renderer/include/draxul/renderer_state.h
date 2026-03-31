#pragma once

#include <draxul/renderer.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace draxul
{

// Data sent to the GPU per cell (matches SSBO layout)
struct alignas(16) GpuCell
{
    glm::vec2 pos = {}; // Screen position in pixels
    glm::vec2 size = {}; // Cell size in pixels
    glm::vec4 bg = {};
    glm::vec4 fg = {};
    glm::vec4 sp = {};
    glm::vec4 uv = {}; // Atlas UVs (x=x0, y=y0, z=x1, w=y1)
    glm::vec2 glyph_offset = {};
    glm::vec2 glyph_size = {};
    uint32_t style_flags;
    std::array<uint32_t, 3> _pad{};

    bool operator==(const GpuCell&) const = default;
};
static_assert(sizeof(GpuCell) == 112, "GpuCell must be 112 bytes for SSBO alignment");
// Field offsets must match the shader struct layout in shaders/grid.metal and shaders/grid.glsl
static_assert(offsetof(GpuCell, pos) == 0, "GpuCell layout mismatch: pos");
static_assert(offsetof(GpuCell, bg) == 16, "GpuCell layout mismatch: bg");
static_assert(offsetof(GpuCell, fg) == 32, "GpuCell layout mismatch: fg");
static_assert(offsetof(GpuCell, sp) == 48, "GpuCell layout mismatch: sp");
static_assert(offsetof(GpuCell, uv) == 64, "GpuCell layout mismatch: uv");
static_assert(offsetof(GpuCell, glyph_offset) == 80, "GpuCell layout mismatch: glyph_offset");
static_assert(offsetof(GpuCell, glyph_size) == 88, "GpuCell layout mismatch: glyph_size");
static_assert(offsetof(GpuCell, style_flags) == 96, "GpuCell layout mismatch: style_flags");

class RendererState
{
public:
    static constexpr size_t OVERLAY_CELL_CAPACITY = 16384;

    void set_grid_size(int cols, int rows, int padding);
    void set_cell_size(int w, int h);
    void set_ascender(int a);
    void set_default_background(Color bg);

    Color default_bg() const
    {
        return default_bg_;
    }

    void update_cells(std::span<const CellUpdate> updates);
    void set_overlay_cells(std::span<const CellUpdate> updates);
    void set_cursor(int col, int row, const CursorStyle& style);
    void restore_cursor();
    void apply_cursor();

    int grid_cols() const
    {
        return grid_cols_;
    }

    int grid_rows() const
    {
        return grid_rows_;
    }

    int total_cells() const
    {
        return grid_cols_ * grid_rows_;
    }

    int bg_instances() const
    {
        return total_cells() + static_cast<int>(overlay_cell_count_) + (cursor_overlay_active_ ? 1 : 0);
    }

    int fg_instances() const
    {
        return total_cells() + static_cast<int>(overlay_cell_count_);
    }

    size_t buffer_size_bytes() const
    {
        return (gpu_cells_.size() + OVERLAY_CELL_CAPACITY + 1) * sizeof(GpuCell);
    }

    void copy_to(std::byte* dst) const;
    bool has_dirty_cells() const;
    size_t dirty_cell_offset_bytes() const;
    size_t dirty_cell_size_bytes() const;
    void copy_dirty_cells_to(std::byte* dst) const;
    bool overlay_region_dirty() const;
    size_t overlay_offset_bytes() const;
    size_t overlay_region_size_bytes() const;
    void copy_overlay_region_to(std::byte* dst) const;
    void clear_dirty();
    // Mark all cells and overlay dirty so they are re-uploaded on the next
    // upload_dirty_state() call. Called when a sibling pane is resized and the
    // shared GPU buffer offsets shift.
    void force_dirty();

private:
    void apply_update_to_cell(GpuCell& cell, const CellUpdate& update) const;
    void relayout();
    void mark_all_cells_dirty();
    void mark_cell_dirty(size_t index);

    Color default_bg_ = Color(0.1f, 0.1f, 0.1f, 1.0f);

    int grid_cols_ = 0;
    int grid_rows_ = 0;
    int cell_w_ = 10;
    int cell_h_ = 20;
    int ascender_ = 16;
    int padding_ = 4;

    int cursor_col_ = 0;
    int cursor_row_ = 0;
    CursorStyle cursor_style_ = {};

    std::vector<GpuCell> gpu_cells_;
    std::vector<GpuCell> overlay_cells_ = std::vector<GpuCell>(OVERLAY_CELL_CAPACITY);
    size_t overlay_cell_count_ = 0;
    GpuCell overlay_cell_ = {};
    GpuCell cursor_saved_cell_ = {};
    bool cursor_applied_ = false;
    bool cursor_overlay_active_ = false;
    size_t dirty_cell_begin_ = 0;
    size_t dirty_cell_end_ = 0;
    bool overlay_dirty_ = false;
};

} // namespace draxul
