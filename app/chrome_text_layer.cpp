#include "chrome_text_layer.h"

#include <algorithm>
#include <draxul/log.h>
#include <draxul/unicode.h>
#include <unordered_set>

namespace draxul
{
namespace
{
std::vector<std::string> clusters(std::string_view text)
{
    std::vector<std::string> result;
    for (auto& cluster : display_clusters(text))
        result.push_back(std::move(cluster.text));
    return result;
}

std::vector<CellUpdate> transparent_cells(int columns, int rows)
{
    const Color transparent{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::vector<CellUpdate> cells;
    cells.reserve(static_cast<size_t>(columns * rows));
    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < columns; ++col)
        {
            CellUpdate cell{};
            cell.col = col;
            cell.row = row;
            cell.bg = transparent;
            cell.fg = transparent;
            cells.push_back(cell);
        }
    }
    return cells;
}

void set_glyph(CellUpdate& cell, TextService& text_service, const std::string& text, Color color)
{
    cell.fg = color;
    cell.glyph = text_service.resolve_cluster(text);
    if (cell.glyph.is_color)
        cell.style_flags |= STYLE_FLAG_COLOR_GLYPH;
}

void write_pill_text(std::vector<CellUpdate>& cells, int grid_columns, int row,
    const ChromePillLayout& pill, TextService& text_service,
    const Color& accent_fg, const Color& body_fg)
{
    int col = pill.text_col;
    int consumed = 0;
    for (const auto& cluster : clusters(pill.label))
    {
        if (col < 0 || col >= grid_columns)
            break;
        const int width = std::max(1, cluster_cell_width(cluster));
        const size_t index = static_cast<size_t>(row * grid_columns + col);
        set_glyph(cells[index], text_service, cluster,
            consumed < pill.prefix_cols ? accent_fg : body_fg);
        col += width;
        consumed += width;
    }
}
} // namespace

ChromeTextLayer::ChromeTextLayer(IGridRenderer* renderer, TextService* text_service)
    : renderer_(renderer)
    , text_service_(text_service)
{
}

void ChromeTextLayer::shutdown()
{
    top_bar_handle_.reset();
    sidebar_handle_.reset();
    agents_handle_.reset();
    spaces_header_handle_.reset();
    agents_header_handle_.reset();
    pane_handles_.clear();
}

void ChromeTextLayer::draw(IFrameContext& frame, const ChromeLayoutOutput& layout,
    const ChromeTheme& theme)
{
    if (!renderer_ || !text_service_)
        return;
    update_top_bar(layout);
    update_sidebar(layout, theme);
    update_panes(layout);
    if (layout.bar_height > 0 && (!layout.tabs.empty() || !layout.right_pills.empty())
        && top_bar_handle_)
        frame.draw_grid_handle(*top_bar_handle_);
    if (layout.sidebar_width > 0 && sidebar_handle_)
        frame.draw_grid_handle(*sidebar_handle_);
    if (layout.sidebar_width > 0 && agents_handle_)
        frame.draw_grid_handle(*agents_handle_);
    if (layout.sidebar_width > 0 && spaces_header_handle_)
        frame.draw_grid_handle(*spaces_header_handle_);
    if (layout.sidebar_width > 0 && agents_header_handle_)
        frame.draw_grid_handle(*agents_header_handle_);
    for (const auto& pane : layout.panes)
    {
        const auto it = pane_handles_.find(pane.leaf);
        if (it != pane_handles_.end() && it->second)
            frame.draw_grid_handle(*it->second);
    }
}

void ChromeTextLayer::update_top_bar(const ChromeLayoutOutput& layout)
{
    if (layout.grid_cols <= 0 || layout.cell_height <= 0)
        return;
    if (layout.tabs.empty() && layout.right_pills.empty())
        return;
    if (!top_bar_handle_)
    {
        top_bar_handle_ = renderer_->create_grid_handle();
        if (!top_bar_handle_)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "ChromeTextLayer: create_grid_handle() returned null");
            return;
        }
        top_bar_handle_->set_default_background({ 0, 0, 0, 0 });
    }

    PaneDescriptor desc;
    desc.pixel_pos = {
        layout.content_x,
        static_cast<int>(layout.top_bar_clip.y) - layout.grid_padding
    };
    desc.pixel_size = { layout.bar_width, layout.cell_height + layout.grid_padding };
    top_bar_handle_->set_viewport(desc);
    top_bar_handle_->set_grid_size(layout.grid_cols, 1);
    top_bar_handle_->set_cursor(-1, -1, CursorStyle{});
    top_bar_handle_->set_cursor_visible(false);
    auto cells = transparent_cells(layout.grid_cols, 1);

    for (const auto& tab : layout.tabs)
    {
        write_pill_text(cells, layout.grid_cols, 0, tab, *text_service_,
            tab.palette.accent_fg, tab.palette.body_fg);
    }
    for (const auto& pill : layout.right_pills)
    {
        int col = pill.text_col;
        for (const auto& cluster : pill.clusters)
        {
            text_service_->resolve_cluster(cluster.text);
            if (col >= 0 && col < layout.grid_cols)
                set_glyph(cells[static_cast<size_t>(col)], *text_service_, cluster.text, cluster.fg);
            col += std::max(1, cluster.width);
        }
    }
    top_bar_handle_->update_cells(cells);
}

void ChromeTextLayer::update_sidebar(const ChromeLayoutOutput& layout, const ChromeTheme& theme)
{
    if (layout.sidebar_width <= 0 || layout.sidebar_cols <= 0 || layout.sidebar_rows <= 0)
    {
        sidebar_handle_.reset();
        agents_handle_.reset();
        spaces_header_handle_.reset();
        agents_header_handle_.reset();
        return;
    }
    if (!sidebar_handle_)
    {
        sidebar_handle_ = renderer_->create_grid_handle();
        if (!sidebar_handle_)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "ChromeTextLayer: sidebar create_grid_handle() returned null");
            return;
        }
        sidebar_handle_->set_default_background({ 0, 0, 0, 0 });
        sidebar_handle_->set_cursor(-1, -1, CursorStyle{});
        sidebar_handle_->set_cursor_visible(false);
    }

    PaneDescriptor desc;
    desc.pixel_pos = {
        static_cast<int>(layout.sidebar_rect.x),
        static_cast<int>(layout.sidebar_rect.y) - layout.grid_padding
    };
    desc.pixel_size = {
        layout.sidebar_width,
        layout.sidebar_height + layout.grid_padding
    };
    sidebar_handle_->set_viewport(desc);
    sidebar_handle_->set_grid_size(layout.sidebar_cols, layout.sidebar_rows);
    auto cells = transparent_cells(layout.sidebar_cols, layout.sidebar_rows);

    for (const auto& space : layout.spaces)
    {
        write_pill_text(cells, layout.sidebar_cols, space.row, space,
            *text_service_, space.palette.accent_fg, space.palette.body_fg);
    }
    sidebar_handle_->update_cells(cells);

    if (layout.sidebar_agent_rows <= 0 || layout.agents.empty())
    {
        agents_handle_.reset();
    }
    else
    {
        if (!agents_handle_)
        {
            agents_handle_ = renderer_->create_grid_handle();
            if (!agents_handle_)
            {
                DRAXUL_LOG_ERROR(LogCategory::App,
                    "ChromeTextLayer: agents create_grid_handle() returned null");
            }
            else
            {
                agents_handle_->set_default_background({ 0, 0, 0, 0 });
                agents_handle_->set_cursor(-1, -1, CursorStyle{});
                agents_handle_->set_cursor_visible(false);
            }
        }
        if (agents_handle_)
        {
            PaneDescriptor agent_desc;
            agent_desc.pixel_pos = {
                static_cast<int>(layout.sidebar_agents_rect.x),
                static_cast<int>(layout.sidebar_agents_rect.y) - layout.grid_padding
            };
            agent_desc.pixel_size = {
                layout.sidebar_width,
                static_cast<int>(layout.sidebar_agents_rect.h) + layout.grid_padding
            };
            agents_handle_->set_viewport(agent_desc);
            agents_handle_->set_grid_size(layout.sidebar_cols, layout.sidebar_agent_rows);
            auto agent_cells = transparent_cells(
                layout.sidebar_cols, layout.sidebar_agent_rows);
            for (const auto& agent : layout.agents)
            {
                write_pill_text(agent_cells, layout.sidebar_cols, agent.row, agent,
                    *text_service_, agent.palette.accent_fg, agent.palette.body_fg);
            }
            agents_handle_->update_cells(agent_cells);
        }
    }

    update_section_header(spaces_header_handle_, layout,
        layout.sidebar_spaces_header, "SPACES", theme.tab_inactive_fg);
    update_section_header(agents_header_handle_, layout,
        layout.sidebar_agents_header, "AGENTS", theme.tab_inactive_fg);
}

void ChromeTextLayer::update_section_header(std::unique_ptr<IGridHandle>& handle,
    const ChromeLayoutOutput& layout, const ChromeRect& rect,
    std::string_view label, const Color& color)
{
    if (rect.w <= 0.0f || rect.h <= 0.0f)
    {
        handle.reset();
        return;
    }
    if (!handle)
    {
        handle = renderer_->create_grid_handle();
        if (!handle)
        {
            DRAXUL_LOG_ERROR(LogCategory::App,
                "ChromeTextLayer: section header create_grid_handle() returned null");
            return;
        }
        handle->set_default_background({ 0, 0, 0, 0 });
        handle->set_cursor(-1, -1, CursorStyle{});
        handle->set_cursor_visible(false);
    }

    // The vector header is the usual cell-height pill inset. Position the
    // one-row text grid two pixels above it so the label remains centered.
    PaneDescriptor desc;
    desc.pixel_pos = {
        static_cast<int>(layout.sidebar_rect.x),
        static_cast<int>(rect.y) - 2 - layout.grid_padding
    };
    desc.pixel_size = {
        layout.sidebar_width,
        layout.cell_height + layout.grid_padding
    };
    handle->set_viewport(desc);
    handle->set_grid_size(layout.sidebar_cols, 1);
    auto cells = transparent_cells(layout.sidebar_cols, 1);
    int col = 1;
    for (const auto& cluster : clusters(label))
    {
        if (col >= layout.sidebar_cols)
            break;
        const int width = std::max(1, cluster_cell_width(cluster));
        set_glyph(cells[static_cast<size_t>(col)], *text_service_, cluster, color);
        col += width;
    }
    handle->update_cells(cells);
}

void ChromeTextLayer::update_panes(const ChromeLayoutOutput& layout)
{
    std::unordered_set<LeafId> live;
    live.reserve(layout.panes.size());
    for (const auto& pane : layout.panes)
        live.insert(pane.leaf);
    for (auto it = pane_handles_.begin(); it != pane_handles_.end();)
    {
        if (!live.contains(it->first))
            it = pane_handles_.erase(it);
        else
            ++it;
    }

    for (const auto& pane : layout.panes)
    {
        auto& handle = pane_handles_[pane.leaf];
        if (!handle)
        {
            handle = renderer_->create_grid_handle();
            if (!handle)
            {
                DRAXUL_LOG_ERROR(LogCategory::App,
                    "ChromeTextLayer: pane status create_grid_handle() returned null");
                continue;
            }
            handle->set_default_background({ 0, 0, 0, 0 });
            handle->set_cursor(-1, -1, CursorStyle{});
            handle->set_cursor_visible(false);
        }
        PaneDescriptor desc;
        desc.pixel_pos = { pane.viewport_x, pane.viewport_y };
        desc.pixel_size = { pane.viewport_w, pane.viewport_h };
        handle->set_viewport(desc);
        handle->set_grid_size(pane.columns, 1);
        auto cells = transparent_cells(pane.columns, 1);
        write_pill_text(cells, pane.columns, 0, pane, *text_service_,
            pane.palette.accent_fg, pane.palette.body_fg);
        handle->update_cells(cells);
    }
}

} // namespace draxul
