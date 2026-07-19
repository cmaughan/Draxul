#include <draxul/gui/overlay_text.h>
#include <draxul/gui/palette_renderer.h>

#include <algorithm>
#include <draxul/text_service.h>
#include <string>
#include <string_view>

namespace draxul::gui
{

namespace
{

// Colors matching the original ImGui palette.
constexpr Color kPanelBg{ 0.12f, 0.12f, 0.14f, 1.0f };
constexpr Color kTextFg{ 0.9f, 0.9f, 0.92f, 1.0f };
constexpr Color kHighlightFg{ 1.0f, 0.85f, 0.25f, 1.0f };
constexpr Color kSelectedBg{ 0.20f, 0.33f, 0.58f, 1.0f };
constexpr Color kHintFg{ 0.5f, 0.5f, 0.5f, 1.0f };
constexpr Color kSeparatorFg{ 0.35f, 0.35f, 0.38f, 1.0f };
constexpr Color kCursorBg{ 0.8f, 0.8f, 0.8f, 0.8f };
constexpr Color kTransparent{ 0.0f, 0.0f, 0.0f, 0.0f };
constexpr Color kPromptFg{ 0.6f, 0.6f, 0.65f, 1.0f };

constexpr int kPanelPadding = 1; // 1-cell padding inside panel edges

// Geometry of the palette panel within the host grid, plus the scroll window
// into the entry list. Named PaletteLayout (not PanelLayout) to avoid
// colliding with the public diagnostics PanelLayout in <draxul/ui_panel.h>.
struct PaletteLayout
{
    int col0, row0;
    int cols, rows;
    int visible_entries;
    int scroll_offset;
};

PaletteLayout compute_palette_layout(const PaletteViewState& state)
{
    PaletteLayout layout;
    layout.cols = state.grid_cols;
    layout.rows = state.grid_rows;
    layout.visible_entries = std::max(1, layout.rows - 2); // reserve separator + input
    const int entry_count = static_cast<int>(state.entries.size());

    layout.col0 = 0;
    layout.row0 = 0;

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
    if (state.grid_cols < 5 || state.grid_rows < 2)
        return {};

    const PaletteLayout layout = compute_palette_layout(state);
    const Color panel_bg{ kPanelBg.r, kPanelBg.g, kPanelBg.b, state.panel_bg_alpha };

    // Resolve a full-block glyph used to occlude underlying terminal text.
    // The FG pass draws main grid cells (terminal text) before overlay cells.
    // Overlay cells without a glyph let terminal text bleed through. A full-block
    // glyph colored to match the background covers the terminal text in the FG pass.
    const AtlasRegion block_glyph = resolve_occlusion_glyph(text_service);

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

    auto write_text = [&](int local_col, int local_row, std::string_view text, Color fg, int max_cols,
                          const std::vector<size_t>* match_positions = nullptr) {
        int columns = 0;
        for (const auto& cluster : layout_overlay_text(text, max_cols))
        {
            auto& cell = at(local_col + cluster.column, local_row);
            cell.glyph = text_service.resolve_cluster(cluster.text);
            const bool matched = match_positions != nullptr
                && std::any_of(match_positions->begin(), match_positions->end(), [&](size_t byte) {
                       return byte >= cluster.byte_start && byte < cluster.byte_end;
                   });
            cell.fg = matched ? kHighlightFg : fg;
            for (int continuation = 1; continuation < cluster.cell_width; ++continuation)
            {
                auto& continuation_cell = at(local_col + cluster.column + continuation, local_row);
                continuation_cell.glyph = {};
                continuation_cell.fg = continuation_cell.bg;
            }
            columns = cluster.column + cluster.cell_width;
        }
        return columns;
    };

    if (state.mode == PaletteMode::Prompt)
    {
        const std::string_view title = state.title.empty() ? std::string_view("Prompt") : state.title;
        write_text(pad, 0, title, kTextFg, content_cols);

        const int input_local_row = layout.rows > 2 ? 2 : layout.rows - 1;
        int query_col0 = pad;
        if (!state.prompt.empty())
        {
            std::string prompt_label(state.prompt);
            prompt_label += ": ";
            query_col0 += write_text(query_col0, input_local_row, prompt_label, kPromptFg,
                layout.cols - pad - query_col0);
        }

        const int query_max = layout.cols - pad - query_col0;
        const int query_len = write_text(query_col0, input_local_row, state.query, kTextFg, query_max);
        const int cursor_local_col = query_col0 + query_len;
        if (cursor_local_col < layout.cols - pad)
        {
            auto& cell = at(cursor_local_col, input_local_row);
            cell.bg = kCursorBg;
            cell.fg = kCursorBg;
        }

        const int message_local_row = input_local_row + 1;
        if (!state.message.empty() && message_local_row < layout.rows)
        {
            write_text(pad, message_local_row, state.message, kHighlightFg, content_cols);
        }
    }
    else
    {
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
            const int name_len = write_text(
                pad, local_row, entry.name, kTextFg, content_cols, &entry.match_positions);

            // Right-aligned shortcut hint.
            if (!entry.shortcut_hint.empty())
            {
                const int hint_len = overlay_text_width(entry.shortcut_hint);
                const int hint_local_col = layout.cols - pad - hint_len;
                if (hint_local_col > pad + name_len + 1)
                    write_text(hint_local_col, local_row, entry.shortcut_hint, kHintFg, hint_len);
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
            const int query_len = write_text(
                query_col0, input_local_row, state.query, kTextFg, query_max);

            // Block cursor after query text.
            const int cursor_local_col = query_col0 + query_len;
            if (cursor_local_col < layout.cols - pad)
            {
                auto& cell = at(cursor_local_col, input_local_row);
                cell.bg = kCursorBg;
                cell.fg = kCursorBg; // Match bg so block glyph is invisible
            }
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
