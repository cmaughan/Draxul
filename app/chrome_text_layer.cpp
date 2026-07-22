#include "chrome_text_layer.h"

#include <algorithm>
#include <draxul/log.h>
#include <draxul/unicode.h>
#include <unordered_set>

namespace draxul
{
namespace
{
float relative_luminance(const Color& color)
{
    return 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
}

Color text_for_bg(const Color& bg)
{
    constexpr Color kDarkInk{ 0.10f, 0.10f, 0.12f, 1.0f };
    constexpr Color kLightInk{ 0.92f, 0.93f, 0.95f, 1.0f };
    return relative_luminance(bg) > 0.5f ? kDarkInk : kLightInk;
}

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
    pane_handles_.clear();
}

void ChromeTextLayer::draw(IFrameContext& frame, const ChromeLayoutOutput& layout,
    const ChromeTheme& theme)
{
    if (!renderer_ || !text_service_)
        return;
    update_top_bar(layout, theme);
    update_sidebar(layout, theme);
    update_panes(layout, theme);
    if (layout.bar_height > 0 && (!layout.tabs.empty() || !layout.right_pills.empty())
        && top_bar_handle_)
        frame.draw_grid_handle(*top_bar_handle_);
    if (layout.sidebar_width > 0 && sidebar_handle_)
        frame.draw_grid_handle(*sidebar_handle_);
    for (const auto& pane : layout.panes)
    {
        const auto it = pane_handles_.find(pane.leaf);
        if (it != pane_handles_.end() && it->second)
            frame.draw_grid_handle(*it->second);
    }
}

void ChromeTextLayer::update_top_bar(const ChromeLayoutOutput& layout, const ChromeTheme& theme)
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
    desc.pixel_pos = { layout.content_x, -layout.grid_padding };
    desc.pixel_size = { layout.bar_width, layout.cell_height + layout.grid_padding };
    top_bar_handle_->set_viewport(desc);
    top_bar_handle_->set_grid_size(layout.grid_cols, 1);
    top_bar_handle_->set_cursor(-1, -1, CursorStyle{});
    top_bar_handle_->set_cursor_visible(false);
    auto cells = transparent_cells(layout.grid_cols, 1);

    for (const auto& tab : layout.tabs)
    {
        const int prefix_cols = static_cast<int>(std::to_string(tab.tab_index).size()) + 1;
        const Color accent_bg = tab.active ? theme.tab_active_bg : theme.tab_inactive_bg;
        const Color body_fg = tab.editing ? text_for_bg(theme.tab_editing_bg)
                                          : (tab.active ? theme.tab_active_fg : theme.tab_inactive_fg);
        const Color accent_fg = text_for_bg(accent_bg);
        int col = tab.text_col;
        int consumed = 0;
        for (const auto& cluster : clusters(tab.label))
        {
            const int width = std::max(1, cluster_cell_width(cluster));
            text_service_->resolve_cluster(cluster);
            if (col >= 0 && col < layout.grid_cols)
                set_glyph(cells[static_cast<size_t>(col)], *text_service_, cluster,
                    consumed < prefix_cols ? accent_fg : body_fg);
            col += width;
            consumed += width;
        }
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
    desc.pixel_pos = { 0, 0 };
    desc.pixel_size = { layout.sidebar_width, layout.sidebar_height };
    sidebar_handle_->set_viewport(desc);
    sidebar_handle_->set_grid_size(layout.sidebar_cols, layout.sidebar_rows);
    auto cells = transparent_cells(layout.sidebar_cols, layout.sidebar_rows);

    auto write_text = [&](int row, int col, std::string_view text, Color color) {
        for (const auto& cluster : clusters(text))
        {
            if (col >= layout.sidebar_cols)
                break;
            const int width = std::max(1, cluster_cell_width(cluster));
            const size_t index = static_cast<size_t>(row * layout.sidebar_cols + col);
            set_glyph(cells[index], *text_service_, cluster, color);
            col += width;
        }
    };

    write_text(0, 1, "SPACES", theme.tab_inactive_fg);
    for (const auto& space : layout.spaces)
    {
        write_text(space.row, 1, space.label,
            space.active ? theme.tab_active_fg : theme.tab_inactive_fg);
    }
    sidebar_handle_->update_cells(cells);
}

void ChromeTextLayer::update_panes(const ChromeLayoutOutput& layout, const ChromeTheme& theme)
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
        handle->set_grid_size(pane.pill_cols, 1);
        auto cells = transparent_cells(pane.pill_cols, 1);
        const Color body_fg = pane.editing ? text_for_bg(theme.status_editing_bg) : theme.status_bar_fg;
        const Color accent_fg = text_for_bg(pane.focused
                ? theme.status_focused_accent_bg
                : theme.status_inactive_accent_bg);
        int col = 1;
        for (const auto& cluster : clusters(pane.label))
        {
            if (col >= pane.pill_cols)
                break;
            const int width = std::max(1, cluster_cell_width(cluster));
            text_service_->resolve_cluster(cluster);
            set_glyph(cells[static_cast<size_t>(col)], *text_service_, cluster,
                col < 1 + pane.prefix_cols ? accent_fg : body_fg);
            col += width;
        }
        handle->update_cells(cells);
    }
}

} // namespace draxul
