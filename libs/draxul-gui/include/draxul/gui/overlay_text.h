#pragma once

#include <draxul/types.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace draxul
{
class TextService;
}

namespace draxul::gui
{

struct OverlayTextCluster
{
    std::string text;
    size_t byte_start = 0;
    size_t byte_end = 0;
    int column = 0;
    int cell_width = 1;
    int continuation_cells = 0;
    bool replacement = false;
};

// Splits UTF-8 at grapheme-like display boundaries and clips only between
// clusters. A wide cluster that does not fully fit is omitted.
[[nodiscard]] std::vector<OverlayTextCluster> layout_overlay_text(
    std::string_view text, int max_columns);

[[nodiscard]] int overlay_text_width(std::string_view text);
[[nodiscard]] AtlasRegion resolve_occlusion_glyph(TextService& text_service);

} // namespace draxul::gui
