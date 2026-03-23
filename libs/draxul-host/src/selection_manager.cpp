#include <draxul/selection_manager.h>

#include <algorithm>
#include <draxul/log.h>

namespace draxul
{

SelectionManager::SelectionManager(Callbacks cbs)
    : cbs_(std::move(cbs))
{
}

void SelectionManager::begin_drag(GridPos pos)
{
    clear();
    sel_dragging_ = true;
    sel_start_ = pos;
    sel_end_ = pos;
}

bool SelectionManager::end_drag(GridPos pos)
{
    sel_end_ = pos;
    sel_dragging_ = false;
    if (sel_start_.pos.x != sel_end_.pos.x || sel_start_.pos.y != sel_end_.pos.y)
    {
        sel_active_ = true;
        update_overlay();
        return true;
    }
    return false;
}

void SelectionManager::update_drag(GridPos pos)
{
    if (!sel_dragging_)
        return;
    sel_end_ = pos;
    sel_active_ = (sel_start_.pos.x != sel_end_.pos.x || sel_start_.pos.y != sel_end_.pos.y);
    if (sel_active_)
        update_overlay();
}

void SelectionManager::clear()
{
    const bool was_active = sel_active_;
    sel_active_ = false;
    sel_dragging_ = false;
    cbs_.set_overlay_cells({});
    if (was_active)
        cbs_.request_frame();
}

std::string SelectionManager::extract_text() const
{
    if (!sel_active_)
        return {};

    int r1 = sel_start_.pos.y, c1 = sel_start_.pos.x;
    int r2 = sel_end_.pos.y, c2 = sel_end_.pos.x;
    if (r1 > r2 || (r1 == r2 && c1 > c2))
    {
        std::swap(r1, r2);
        std::swap(c1, c2);
    }

    std::string result;
    int cell_count = 0;
    for (int row = r1; row <= r2 && cell_count < kSelectionMaxCells; ++row)
    {
        const int sc = (row == r1) ? c1 : 0;
        const int ec = (row == r2) ? c2 : cbs_.grid_cols() - 1;
        const size_t line_start = result.size();
        for (int col = sc; col <= ec && cell_count < kSelectionMaxCells; ++col)
        {
            const auto& cell = cbs_.get_cell(col, row);
            if (!cell.double_width_cont)
                result += cell.text.view();
            ++cell_count;
        }
        if (row < r2 && cell_count < kSelectionMaxCells)
        {
            // Trim trailing spaces from this line before the newline.
            while (result.size() > line_start && result.back() == ' ')
                result.pop_back();
            result += '\n';
        }
    }
    while (!result.empty() && result.back() == ' ')
        result.pop_back();
    return result;
}

void SelectionManager::update_overlay() const
{
    if (!sel_active_)
    {
        cbs_.set_overlay_cells({});
        return;
    }

    int r1 = sel_start_.pos.y, c1 = sel_start_.pos.x;
    int r2 = sel_end_.pos.y, c2 = sel_end_.pos.x;
    if (r1 > r2 || (r1 == r2 && c1 > c2))
    {
        std::swap(r1, r2);
        std::swap(c1, c2);
    }

    static const Color kSelBg(0.27f, 0.44f, 0.78f, 1.0f);
    static const Color kSelFg(1.0f, 1.0f, 1.0f, 1.0f);

    std::vector<CellUpdate> overlays;
    overlays.reserve(std::min(kSelectionMaxCells, (r2 - r1 + 1) * cbs_.grid_cols()));
    bool truncated = false;
    for (int row = r1; row <= r2 && static_cast<int>(overlays.size()) < kSelectionMaxCells; ++row)
    {
        const int sc = (row == r1) ? c1 : 0;
        const int ec = (row == r2) ? c2 : cbs_.grid_cols() - 1;
        for (int col = sc; col <= ec && static_cast<int>(overlays.size()) < kSelectionMaxCells; ++col)
        {
            CellUpdate cu;
            cu.col = col;
            cu.row = row;
            cu.bg = kSelBg;
            cu.fg = kSelFg;
            overlays.push_back(cu);
        }
        if (static_cast<int>(overlays.size()) >= kSelectionMaxCells && row < r2)
            truncated = true;
    }
    if (truncated)
        DRAXUL_LOG_DEBUG(LogCategory::App, "selection truncated at %d cells (limit %d)",
            kSelectionMaxCells, kSelectionMaxCells);

    cbs_.set_overlay_cells(std::move(overlays));
    cbs_.request_frame();
}

} // namespace draxul
