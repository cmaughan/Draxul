#include <draxul/grid_rendering_pipeline.h>
#include <draxul/perf_timing.h>

#include <algorithm>
#include <cstring>

namespace draxul
{

namespace
{

bool should_shape_cell_text(const Cell& cell)
{
    return !cell.double_width_cont
        && !cell.text.empty()
        && cell.text != " ";
}

bool can_form_two_cell_ligature(const Cell& leader, const Cell& continuation)
{
    return !leader.double_width
        && !continuation.double_width
        && should_shape_cell_text(leader)
        && should_shape_cell_text(continuation)
        && leader.hl_attr_id == continuation.hl_attr_id;
}

CellUpdate make_cell_update(int col, int row, const Cell& cell, HighlightTable& highlights)
{
    PERF_MEASURE();
    const auto& hl = highlights.get(cell.hl_attr_id);

    Color fg;
    Color bg;
    Color sp;
    highlights.resolve(hl, fg, bg, &sp);

    CellUpdate update;
    update.col = col;
    update.row = row;
    update.bg = bg;
    update.fg = fg;
    update.sp = sp;
    update.style_flags = hl.style_flags();
    return update;
}

void expand_dirty_cells_for_ligatures_impl(
    const Grid& grid, const std::vector<Grid::DirtyCell>& dirty, std::vector<Grid::DirtyCell>& expanded)
{
    PERF_MEASURE();
    expanded.clear();
    const size_t needed = dirty.size() * 3;
    if (expanded.capacity() < needed)
        expanded.reserve(needed);
    expanded.insert(expanded.end(), dirty.begin(), dirty.end());

    for (const auto& cell : dirty)
    {
        if (cell.col > 0)
            expanded.push_back({ cell.col - 1, cell.row });
        if (cell.col + 1 < grid.cols())
            expanded.push_back({ cell.col + 1, cell.row });
    }

    std::sort(expanded.begin(), expanded.end(), [](const Grid::DirtyCell& lhs, const Grid::DirtyCell& rhs) {
        if (lhs.row != rhs.row)
            return lhs.row < rhs.row;
        return lhs.col < rhs.col;
    });
    expanded.erase(std::unique(expanded.begin(), expanded.end(),
                       [](const Grid::DirtyCell& lhs, const Grid::DirtyCell& rhs) {
                           return lhs.col == rhs.col && lhs.row == rhs.row;
                       }),
        expanded.end());
}

} // namespace

GridRenderingPipeline::GridRenderingPipeline(Grid& grid, HighlightTable& highlights, IGlyphAtlas& glyph_atlas)
    : grid_(grid)
    , highlights_(highlights)
    , glyph_atlas_(glyph_atlas)
{
}

void GridRenderingPipeline::set_renderer(IGridRenderer* renderer)
{
    renderer_ = renderer;
}

void GridRenderingPipeline::set_grid_handle(IGridHandle* handle)
{
    grid_handle_ = handle;
}

void GridRenderingPipeline::set_enable_ligatures(bool enable)
{
    enable_ligatures_ = enable;
}

void GridRenderingPipeline::expand_dirty_cells_for_ligatures(const std::vector<Grid::DirtyCell>& dirty)
{
    PERF_MEASURE();
    expand_dirty_cells_for_ligatures_impl(grid_, dirty, expanded_scratch_);
}

bool GridRenderingPipeline::try_shape_ligature(int col, int row, const Cell& cell, bool is_bold,
    bool is_italic, CellUpdate& update, std::vector<CellUpdate>& updates, bool& atlas_updated)
{
    PERF_MEASURE();
    if (col + 1 >= grid_.cols())
        return false;

    const auto& next = grid_.get_cell(col + 1, row);
    if (!can_form_two_cell_ligature(cell, next))
        return false;

    const std::string combined = std::string(cell.text.view()) + std::string(next.text.view());
    if (glyph_atlas_.ligature_cell_span(combined, is_bold, is_italic) != 2)
        return false;

    update.glyph = glyph_atlas_.resolve_cluster(combined, is_bold, is_italic);
    if (update.glyph.is_color)
        update.style_flags |= STYLE_FLAG_COLOR_GLYPH;
    atlas_updated = atlas_updated || glyph_atlas_.atlas_dirty();
    updates.push_back(update);
    updates.push_back(make_cell_update(col + 1, row, next, highlights_));
    return true;
}

void GridRenderingPipeline::build_cell_updates(const std::vector<Grid::DirtyCell>& dirty,
    std::vector<CellUpdate>& updates, bool& atlas_updated)
{
    PERF_MEASURE();
    bool skip_next_cell = false;
    Grid::DirtyCell skipped_cell = {};

    for (auto& [col, row] : dirty)
    {
        if (skip_next_cell && skipped_cell.col == col && skipped_cell.row == row)
        {
            skip_next_cell = false;
            continue;
        }

        const auto& cell = grid_.get_cell(col, row);
        CellUpdate update = make_cell_update(col, row, cell, highlights_);

        const bool is_bold = (update.style_flags & STYLE_FLAG_BOLD) != 0;
        const bool is_italic = (update.style_flags & STYLE_FLAG_ITALIC) != 0;

        if (enable_ligatures_
            && try_shape_ligature(col, row, cell, is_bold, is_italic, update, updates, atlas_updated))
        {
            skip_next_cell = true;
            skipped_cell = { col + 1, row };
            continue;
        }

        if (should_shape_cell_text(cell))
        {
            update.glyph = glyph_atlas_.resolve_cluster(std::string(cell.text.view()), is_bold, is_italic);
            if (update.glyph.is_color)
                update.style_flags |= STYLE_FLAG_COLOR_GLYPH;
            atlas_updated = atlas_updated || glyph_atlas_.atlas_dirty();
        }

        updates.push_back(update);
    }
}

void GridRenderingPipeline::flush()
{
    PERF_MEASURE();
    thread_checker_.assert_main_thread("GridRenderingPipeline::flush");
    if (!renderer_ || !grid_handle_)
        return;

    for (int attempt = 0; attempt < 2; attempt++)
    {
        if (attempt > 0)
        {
            grid_.mark_all_dirty();
            force_full_atlas_upload_ = true;
        }

        auto dirty = grid_.get_dirty_cells();
        if (dirty.empty())
        {
            if (force_full_atlas_upload_)
                upload_atlas();
            return;
        }
        const std::vector<Grid::DirtyCell>* dirty_cells = &dirty;
        if (enable_ligatures_)
        {
            expand_dirty_cells_for_ligatures(dirty);
            dirty_cells = &expanded_scratch_;
        }

        std::vector<CellUpdate> updates;
        updates.reserve(dirty_cells->size() + dirty_cells->size() / 2);

        bool atlas_updated = false;
        build_cell_updates(*dirty_cells, updates, atlas_updated);

        bool atlas_reset = glyph_atlas_.consume_atlas_reset();
        if (atlas_reset)
            continue;

        if (force_full_atlas_upload_ || atlas_updated)
            upload_atlas();

        grid_handle_->update_cells(updates);
        grid_.clear_dirty();
        return;
    }
}

void GridRenderingPipeline::force_full_atlas_upload()
{
    force_full_atlas_upload_ = true;
}

void GridRenderingPipeline::upload_atlas()
{
    PERF_MEASURE();
    if (!glyph_atlas_.atlas_dirty() && !force_full_atlas_upload_)
        return;

    if (force_full_atlas_upload_)
    {
        renderer_->set_atlas_texture(
            glyph_atlas_.atlas_data(), glyph_atlas_.atlas_width(), glyph_atlas_.atlas_height());
    }
    else
    {
        const auto dirty = glyph_atlas_.atlas_dirty_rect();
        if (dirty.size.x > 0 && dirty.size.y > 0)
        {
            constexpr size_t atlas_pixel_size = 4;
            atlas_upload_scratch_.resize((size_t)dirty.size.x * dirty.size.y * atlas_pixel_size);

            const uint8_t* atlas = glyph_atlas_.atlas_data();
            const int atlas_width = glyph_atlas_.atlas_width();
            for (int row = 0; row < dirty.size.y; row++)
            {
                const uint8_t* src = atlas
                    + (((size_t)(dirty.pos.y + row) * atlas_width) + dirty.pos.x) * atlas_pixel_size;
                uint8_t* dst = atlas_upload_scratch_.data() + (size_t)row * dirty.size.x * atlas_pixel_size;
                std::memcpy(dst, src, (size_t)dirty.size.x * atlas_pixel_size);
            }

            renderer_->update_atlas_region(
                dirty.pos.x, dirty.pos.y, dirty.size.x, dirty.size.y, atlas_upload_scratch_.data());
        }
    }

    glyph_atlas_.clear_atlas_dirty();
    force_full_atlas_upload_ = false;
}

} // namespace draxul
