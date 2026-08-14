#include <draxul/gui/overlay_text.h>

#include <draxul/text_service.h>
#include <draxul/unicode.h>

#include <algorithm>

namespace draxul::gui
{

std::vector<OverlayTextCluster> layout_overlay_text(std::string_view text, int max_columns)
{
    std::vector<OverlayTextCluster> result;
    int column = 0;
    for (auto& cluster : display_clusters(text))
    {
        if (column + cluster.cell_width > std::max(0, max_columns))
            break;
        result.push_back(OverlayTextCluster{
            .text = std::move(cluster.text),
            .byte_start = cluster.byte_start,
            .byte_end = cluster.byte_end,
            .column = column,
            .cell_width = cluster.cell_width,
            .continuation_cells = cluster.cell_width - 1,
            .replacement = cluster.replacement,
        });
        column += cluster.cell_width;
    }
    return result;
}

int overlay_text_width(std::string_view text)
{
    return display_cell_width(text);
}

AtlasRegion resolve_occlusion_glyph(TextService& text_service)
{
    return text_service.resolve_cluster("\xE2\x96\x88"); // U+2588 FULL BLOCK
}

} // namespace draxul::gui
