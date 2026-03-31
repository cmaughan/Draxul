#include <draxul/gui/palette_renderer.h>

#include <algorithm>
#include <draxul/text_service.h>
#include <string>

namespace draxul::gui
{

namespace
{

// Colors matching the original ImGui palette.
constexpr Color kPanelBg{ 0.12f, 0.12f, 0.14f, 1.0f };
constexpr Color kTextFg{ 0.9f, 0.9f, 0.92f, 1.0f };
constexpr Color kHighlightFg{ 1.0f, 0.85f, 0.25f, 1.0f };
constexpr Color kSelectedBg{ 0.25f, 0.45f, 0.85f, 0.6f };
constexpr Color kHintFg{ 0.5f, 0.5f, 0.5f, 1.0f };
constexpr Color kSeparatorFg{ 0.35f, 0.35f, 0.38f, 1.0f };
constexpr Color kCursorBg{ 0.8f, 0.8f, 0.8f, 0.8f };
constexpr Color kTransparent{ 0.0f, 0.0f, 0.0f, 0.0f };
constexpr Color kPromptFg{ 0.6f, 0.6f, 0.65f, 1.0f };

constexpr int kMinPanelCols = 30;
constexpr int kPanelPadding = 1; // 1-cell padding inside panel edges

struct PanelLayout
{
    int col0, row0;
    int cols, rows;
    int visible_entries;
    int scroll_offset;
};

PanelLayout compute_layout(const PaletteViewState& state)
{
    PanelLayout layout;
    layout.cols = std::max(kMinPanelCols, state.grid_cols / 2);
    if (layout.cols > state.grid_cols)
        layout.cols = state.grid_cols;

    const int max_panel_rows = std::max(4, state.grid_rows * 40 / 100);
    layout.visible_entries = std::max(1, max_panel_rows - 2); // reserve separator + input
    const int entry_count = static_cast<int>(state.entries.size());
    const int shown = std::min(entry_count, layout.visible_entries);
    layout.rows = shown + 2; // entries + separator + input line

    layout.col0 = (state.grid_cols - layout.cols) / 2;
    layout.row0 = state.grid_rows / 5;

    // Clamp so panel fits on screen.
    if (layout.row0 + layout.rows > state.grid_rows)
        layout.row0 = std::max(0, state.grid_rows - layout.rows);

    // Compute scroll offset to keep selected_index visible.
    layout.scroll_offset = 0;
    if (state.selected_index >= 0 && entry_count > layout.visible_entries)
    {
        if (state.selected_index >= layout.visible_entries)
            layout.scroll_offset = state.selected_index - layout.visible_entries + 1;
        layout.scroll_offset = std::clamp(layout.scroll_offset, 0, entry_count - layout.visible_entries);
    }

    return layout;
}

bool is_match_position(const std::vector<size_t>& positions, size_t index)
{
    for (size_t pos : positions)
    {
        if (pos == index)
            return true;
    }
    return false;
}

// A single cell in the panel grid. Written once per position, then flattened to CellUpdate.
struct PanelCell
{
    Color bg = kPanelBg;
    Color fg = kTransparent;
    AtlasRegion glyph = {};
};

} // namespace

std::vector<CellUpdate> render_palette(
    const PaletteViewState& state,
    draxul::TextService& text_service)
{
    if (state.grid_cols <= 0 || state.grid_rows <= 0)
        return {};

    const PanelLayout layout = compute_layout(state);
    const Color panel_bg{ kPanelBg.r, kPanelBg.g, kPanelBg.b, state.panel_bg_alpha };

    // Resolve a full-block glyph used to occlude underlying terminal text.
    // The FG pass draws main grid cells (terminal text) before overlay cells.
    // Overlay cells without a glyph let terminal text bleed through. A full-block
    // glyph colored to match the background covers the terminal text in the FG pass.
    const std::string block_cluster("\xe2\x96\x88"); // U+2588 FULL BLOCK
    AtlasRegion block_glyph = text_service.resolve_cluster(block_cluster);

    // Build a 2D grid of panel cells — one cell per position, no duplicates.
    const int total = layout.cols * layout.rows;
    std::vector<PanelCell> grid(static_cast<size_t>(total));

    // Fill all cells with panel background + block glyph to occlude terminal text.
    for (auto& cell : grid)
    {
        cell.bg = panel_bg;
        cell.fg = panel_bg;
        cell.glyph = block_glyph;
    }

    // Helper to index into the grid.
    auto at = [&](int local_col, int local_row) -> PanelCell& {
        return grid[static_cast<size_t>(local_row * layout.cols + local_col)];
    };

    const int pad = kPanelPadding;
    const int content_cols = layout.cols - pad * 2;

    // Entry rows.
    const int entry_count = static_cast<int>(state.entries.size());
    const int shown = std::min(entry_count - layout.scroll_offset, layout.visible_entries);
    for (int i = 0; i < shown; ++i)
    {
        const int entry_idx = layout.scroll_offset + i;
        const auto& entry = state.entries[static_cast<size_t>(entry_idx)];
        const int local_row = i;
        const bool selected = (entry_idx == state.selected_index);
        const Color row_bg = selected ? kSelectedBg : panel_bg;

        // Fill entire row with row background (handles selected highlight).
        // Also update the block glyph fg to match so it stays invisible.
        for (int c = 0; c < layout.cols; ++c)
        {
            auto& cell = at(c, local_row);
            cell.bg = row_bg;
            cell.fg = row_bg;
        }

        // Entry name with fuzzy-match highlighting.
        const int name_len = std::min(static_cast<int>(entry.name.size()), content_cols);
        for (int ci = 0; ci < name_len; ++ci)
        {
            const std::string cluster(1, entry.name[static_cast<size_t>(ci)]);
            auto& cell = at(pad + ci, local_row);
            cell.glyph = text_service.resolve_cluster(cluster);
            cell.fg = is_match_position(entry.match_positions, static_cast<size_t>(ci))
                ? kHighlightFg
                : kTextFg;
        }

        // Right-aligned shortcut hint.
        if (!entry.shortcut_hint.empty())
        {
            const int hint_len = static_cast<int>(entry.shortcut_hint.size());
            const int hint_local_col = layout.cols - pad - hint_len;
            if (hint_local_col > pad + name_len + 1)
            {
                for (int ci = 0; ci < hint_len; ++ci)
                {
                    const std::string cluster(1, entry.shortcut_hint[static_cast<size_t>(ci)]);
                    auto& cell = at(hint_local_col + ci, local_row);
                    cell.glyph = text_service.resolve_cluster(cluster);
                    cell.fg = kHintFg;
                }
            }
        }
    }

    // Separator row.
    const int sep_local_row = layout.rows - 2;
    {
        const std::string dash_cluster("\xe2\x94\x80"); // U+2500 BOX DRAWINGS LIGHT HORIZONTAL
        AtlasRegion dash_glyph = text_service.resolve_cluster(dash_cluster);
        for (int c = 0; c < layout.cols; ++c)
        {
            auto& cell = at(c, sep_local_row);
            cell.glyph = dash_glyph;
            cell.fg = kSeparatorFg;
        }
    }

    // Input line: "> " + query + cursor.
    const int input_local_row = layout.rows - 1;
    {
        // Prompt ">"
        auto& prompt = at(pad, input_local_row);
        prompt.glyph = text_service.resolve_cluster(std::string(">"));
        prompt.fg = kPromptFg;

        // Query text.
        const int query_col0 = pad + 2;
        const int query_max = content_cols - 3;
        const int query_len = std::min(static_cast<int>(state.query.size()), query_max);
        for (int i = 0; i < query_len; ++i)
        {
            const std::string cluster(1, state.query[static_cast<size_t>(i)]);
            auto& cell = at(query_col0 + i, input_local_row);
            cell.glyph = text_service.resolve_cluster(cluster);
            cell.fg = kTextFg;
        }

        // Block cursor after query text.
        const int cursor_local_col = query_col0 + query_len;
        if (cursor_local_col < layout.cols - pad)
        {
            auto& cell = at(cursor_local_col, input_local_row);
            cell.bg = kCursorBg;
            cell.fg = kCursorBg; // Match bg so block glyph is invisible
        }
    }

    // Flatten to CellUpdate vector — one entry per panel cell.
    std::vector<CellUpdate> cells;
    cells.reserve(static_cast<size_t>(total));
    for (int r = 0; r < layout.rows; ++r)
    {
        for (int c = 0; c < layout.cols; ++c)
        {
            const auto& pc = at(c, r);
            CellUpdate cu;
            cu.col = layout.col0 + c;
            cu.row = layout.row0 + r;
            cu.bg = pc.bg;
            cu.fg = pc.fg;
            cu.sp = kTransparent;
            cu.glyph = pc.glyph;
            cu.style_flags = 0;
            cells.push_back(cu);
        }
    }

    return cells;
}

} // namespace draxul::gui
