#include <draxul/atlas_upload.h>
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

// Maximum cells to consider for a multi-cell ligature.
constexpr int kMaxLigatureCells = 6;

bool can_extend_ligature(const Cell& leader, const Cell& next)
{
    return !next.double_width
        && !next.double_width_cont
        && should_shape_cell_text(next)
        && leader.hl_attr_id == next.hl_attr_id;
}

CellUpdate make_cell_update(int col, int row, const Cell& cell, const Grid& grid, HighlightTable& highlights)
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
    if (grid.effective_link_id(col, row) != 0)
        update.style_flags |= STYLE_FLAG_HYPERLINK;
    return update;
}

bool cell_chainable(const Cell& cell)
{
    return !cell.double_width && !cell.double_width_cont && should_shape_cell_text(cell);
}

bool cells_chain(const Cell& lhs, const Cell& rhs)
{
    return cell_chainable(lhs) && cell_chainable(rhs) && lhs.hl_attr_id == rhs.hl_attr_id;
}

void expand_dirty_cells_for_ligatures_impl(
    const Grid& grid, const std::vector<Grid::DirtyCell>& dirty, std::vector<Grid::DirtyCell>& expanded)
{
    PERF_MEASURE();
    expanded.clear();
    if (expanded.capacity() < dirty.size())
        expanded.reserve(dirty.size());

    // Ligature grouping is anchored at the start of each shaping run (a
    // maximal stretch of chainable cells with the same highlight). A changed
    // cell can regroup its entire run — e.g. typing the 4th '/' of "////"
    // breaks the previous "///" cluster — so expand every dirty cell to the
    // full runs touching it. A fixed radius is not enough: it can land
    // mid-run, re-anchoring groups at an arbitrary column and leaving stale
    // overlapping cluster glyphs behind.
    for (const auto& cell : dirty)
    {
        // The left-adjacent run is included even when the dirty cell itself
        // no longer chains to it: an edit can split or shorten that run
        // (e.g. deleting the last '/' of "/////"), which regroups it.
        int left = cell.col;
        if (left > 0)
        {
            int run = left - 1;
            while (run > 0 && cells_chain(grid.get_cell(run - 1, cell.row), grid.get_cell(run, cell.row)))
                --run;
            left = run;
        }

        int right = cell.col;
        if (right + 1 < grid.cols())
        {
            int run = right + 1;
            while (run + 1 < grid.cols()
                && cells_chain(grid.get_cell(run, cell.row), grid.get_cell(run + 1, cell.row)))
                ++run;
            right = run;
        }

        for (int c = left; c <= right; ++c)
            expanded.push_back({ c, cell.row });
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

void GridRenderingPipeline::set_url_detection_enabled(bool enable)
{
    if (url_detection_enabled_ == enable)
        return;
    url_detection_enabled_ = enable;
    grid_.mark_all_dirty();
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
    if (cell.double_width || !should_shape_cell_text(cell))
        return false;

    // Build a candidate string of up to kMaxLigatureCells cells, breaking at
    // highlight boundaries or incompatible cells.
    const int max_end = std::min(col + kMaxLigatureCells, grid_.cols());
    std::string combined(cell.text.view());
    int span_end = col + 1; // exclusive end of the current candidate window

    for (int c = col + 1; c < max_end; ++c)
    {
        const auto& next = grid_.get_cell(c, row);
        if (!can_extend_ligature(cell, next))
            break;
        combined += std::string(next.text.view());
        span_end = c + 1;
    }

    // Need at least 2 cells for a ligature.
    if (span_end - col < 2)
        return false;

    // Try the longest candidate first, then shrink until we find a ligature or
    // exhaust the window down to a 2-cell pair.
    for (int try_end = span_end; try_end >= col + 2; --try_end)
    {
        const int candidate_len = try_end - col;

        // Build the candidate string for this length.
        std::string candidate;
        if (try_end == span_end)
        {
            candidate = combined;
        }
        else
        {
            candidate.clear();
            for (int c = col; c < try_end; ++c)
                candidate += std::string(grid_.get_cell(c, row).text.view());
        }

        const int span = glyph_atlas_.ligature_cell_span(candidate, is_bold, is_italic);
        if (span != candidate_len)
            continue;

        // Found a valid ligature spanning `span` cells.
        update.glyph = glyph_atlas_.resolve_cluster(candidate, is_bold, is_italic);
        if (update.glyph.is_color)
            update.style_flags |= STYLE_FLAG_COLOR_GLYPH;
        atlas_updated = atlas_updated || glyph_atlas_.atlas_dirty();
        updates.push_back(update);

        // Emit blank continuation updates for the remaining cells in the ligature.
        for (int c = col + 1; c < try_end; ++c)
            updates.push_back(make_cell_update(c, row, grid_.get_cell(c, row), grid_, highlights_));

        return true;
    }

    return false;
}

void GridRenderingPipeline::build_cell_updates(const std::vector<Grid::DirtyCell>& dirty,
    std::vector<CellUpdate>& updates, bool& atlas_updated)
{
    PERF_MEASURE();
    // Track the end column (exclusive) and row of cells consumed by the last ligature
    // so we can skip continuation cells that were already emitted.
    int skip_until_col = -1;
    int skip_row = -1;

    for (auto& [col, row] : dirty)
    {
        if (row == skip_row && col < skip_until_col)
            continue;

        const auto& cell = grid_.get_cell(col, row);
        CellUpdate update = make_cell_update(col, row, cell, grid_, highlights_);

        const bool is_bold = (update.style_flags & STYLE_FLAG_BOLD) != 0;
        const bool is_italic = (update.style_flags & STYLE_FLAG_ITALIC) != 0;

        if (enable_ligatures_)
        {
            const size_t before = updates.size();
            if (try_shape_ligature(col, row, cell, is_bold, is_italic, update, updates, atlas_updated))
            {
                const int ligature_cells = static_cast<int>(updates.size() - before);
                skip_until_col = col + ligature_cells;
                skip_row = row;
                continue;
            }
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
        grid_.refresh_url_detection_for_dirty_rows(url_detection_enabled_);
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
        glyph_atlas_.clear_atlas_dirty();
        force_full_atlas_upload_ = false;
    }
    else
    {
        upload_atlas_dirty_region(glyph_atlas_, *renderer_, atlas_upload_scratch_);
    }
}

} // namespace draxul
