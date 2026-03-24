#pragma once

#include <draxul/glyph_atlas.h>
#include <draxul/grid.h>
#include <draxul/renderer.h>
#include <draxul/thread_check.h>
#include <vector>

namespace draxul
{

class GridRenderingPipeline
{
public:
    GridRenderingPipeline(Grid& grid, HighlightTable& highlights, IGlyphAtlas& glyph_atlas);
    void set_renderer(IGridRenderer* renderer);
    void set_grid_handle(IGridHandle* handle);
    void set_enable_ligatures(bool enable);
    void flush();
    void force_full_atlas_upload();

    // Test hook for verifying scratch-buffer reuse on the ligature expansion path.
    size_t expanded_scratch_capacity_for_testing() const
    {
        return expanded_scratch_.capacity();
    }

private:
    void upload_atlas();
    bool try_shape_ligature(int col, int row, const Cell& cell, bool is_bold, bool is_italic,
        CellUpdate& update, std::vector<CellUpdate>& updates, bool& atlas_updated);
    void expand_dirty_cells_for_ligatures(const std::vector<Grid::DirtyCell>& dirty);
    void build_cell_updates(const std::vector<Grid::DirtyCell>& dirty,
        std::vector<CellUpdate>& updates, bool& atlas_updated);

    IGridRenderer* renderer_ = nullptr;
    IGridHandle* grid_handle_ = nullptr;
    Grid& grid_;
    HighlightTable& highlights_;
    IGlyphAtlas& glyph_atlas_;

    bool force_full_atlas_upload_ = true;
    bool enable_ligatures_ = true;
    std::vector<uint8_t> atlas_upload_scratch_;
    std::vector<Grid::DirtyCell> expanded_scratch_;
    MainThreadChecker thread_checker_;
};

} // namespace draxul
