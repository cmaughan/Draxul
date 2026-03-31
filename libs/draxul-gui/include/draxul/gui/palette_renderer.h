#pragma once

#include <draxul/types.h>
#include <span>
#include <string_view>
#include <vector>

namespace draxul
{
class TextService;
}

namespace draxul::gui
{

struct PaletteEntry
{
    std::string_view name;
    std::string_view shortcut_hint;
    std::vector<size_t> match_positions;
};

struct PaletteViewState
{
    int grid_cols = 0;
    int grid_rows = 0;
    std::string_view query;
    int selected_index = -1;
    std::span<const PaletteEntry> entries;
    float panel_bg_alpha = 1.0f; // palette background opacity [0.0, 1.0]
};

/// Generate overlay CellUpdate cells for the command palette.
/// Produces a full-screen dim overlay plus a centered panel with entries and input line.
[[nodiscard]] std::vector<CellUpdate> render_palette(
    const PaletteViewState& state,
    draxul::TextService& text_service);

} // namespace draxul::gui
