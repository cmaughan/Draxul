#include <draxul/gui/tooltip.h>
#include <draxul/gui/overlay_text.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <draxul/perf_timing.h>
#include <draxul/text_service.h>
#include <limits>

namespace draxul::gui
{

namespace
{

constexpr int kTooltipPadding = 8;
constexpr int kLineSpacing = 3;
constexpr int kColumnGap = 8; // pixels between label and value columns
constexpr uint8_t kBgR = 25;
constexpr uint8_t kBgG = 25;
constexpr uint8_t kBgB = 30;
constexpr uint8_t kBgA = 210;
// Label column: dimmer.
constexpr uint8_t kLabelR = 160;
constexpr uint8_t kLabelG = 165;
constexpr uint8_t kLabelB = 175;
// Value column: brighter.
constexpr uint8_t kValueR = 235;
constexpr uint8_t kValueG = 235;
constexpr uint8_t kValueB = 240;

void blit_text_line(
    draxul::TextService& text_service,
    const std::string& text,
    int pen_x, int baseline_y,
    uint8_t* dst_pixels, int dst_width, int dst_height,
    uint8_t r, uint8_t g, uint8_t b)
{
    PERF_MEASURE();
    const FontMetrics metrics = text_service.metrics();
    const int cell_width = std::max(metrics.cell_width, 1);

    for (const auto& cluster : layout_overlay_text(text, std::numeric_limits<int>::max()))
    {
        const AtlasRegion region = text_service.resolve_cluster(cluster.text);
        const int advance = cell_width * cluster.cell_width;
        if (region.bitmap_size.x <= 0 || region.bitmap_size.y <= 0)
        {
            pen_x += advance;
            continue;
        }

        const uint8_t* atlas = text_service.atlas_data();
        const int atlas_width = text_service.atlas_width();
        const int atlas_height = text_service.atlas_height();
        if (!atlas || atlas_width <= 0 || atlas_height <= 0)
        {
            pen_x += advance;
            continue;
        }

        const int src_x0 = std::clamp(
            static_cast<int>(std::lround(region.uv.x * atlas_width)), 0, atlas_width - 1);
        const int src_y0 = std::clamp(
            static_cast<int>(std::lround(region.uv.y * atlas_height)), 0, atlas_height - 1);
        const int dst_x0 = pen_x + region.bitmap_bearing.x;
        const int dst_y0 = baseline_y - region.bitmap_bearing.y;

        for (int row = 0; row < region.bitmap_size.y; ++row)
        {
            const int dst_y = dst_y0 + row;
            const int src_y = src_y0 + row;
            if (dst_y < 0 || dst_y >= dst_height || src_y < 0 || src_y >= atlas_height)
                continue;

            for (int col = 0; col < region.bitmap_size.x; ++col)
            {
                const int dst_x = dst_x0 + col;
                const int src_x = src_x0 + col;
                if (dst_x < 0 || dst_x >= dst_width || src_x < 0 || src_x >= atlas_width)
                    continue;

                const uint8_t* src = atlas + (((src_y * atlas_width) + src_x) * 4);
                if (src[3] == 0)
                    continue;

                uint8_t* dst = dst_pixels + (((dst_y * dst_width) + dst_x) * 4);
                // Alpha-blend text over existing background.
                const float sa = static_cast<float>(src[3]) / 255.0f;
                const float da = 1.0f - sa;
                dst[0] = static_cast<uint8_t>(std::min(255.0f, r * sa + dst[0] * da));
                dst[1] = static_cast<uint8_t>(std::min(255.0f, g * sa + dst[1] * da));
                dst[2] = static_cast<uint8_t>(std::min(255.0f, b * sa + dst[2] * da));
                dst[3] = static_cast<uint8_t>(std::min(255.0f, src[3] + dst[3] * da));
            }
        }

        pen_x += advance;
    }
}

} // namespace

TooltipBitmap rasterize_tooltip(
    draxul::TextService& text_service, const TooltipData& data)
{
    PERF_MEASURE();
    const FontMetrics metrics = text_service.metrics();
    const int cell_width = std::max(metrics.cell_width, 1);
    const int cell_height = std::max(metrics.cell_height, 1);

    // Measure column widths.
    int label_max_chars = 0;
    int value_max_chars = 0;
    for (const auto& entry : data.entries)
    {
        label_max_chars = std::max(label_max_chars, overlay_text_width(entry.label));
        value_max_chars = std::max(value_max_chars, overlay_text_width(entry.value));
    }

    const int label_col_width = label_max_chars * cell_width;
    const int value_col_width = value_max_chars * cell_width;
    const int total_text_width = label_col_width + kColumnGap + value_col_width;
    const int row_count = static_cast<int>(data.entries.size());
    const int total_text_height = row_count * cell_height + (row_count - 1) * kLineSpacing;

    TooltipBitmap bitmap;
    bitmap.width = total_text_width + kTooltipPadding * 2;
    bitmap.height = total_text_height + kTooltipPadding * 2;
    bitmap.rgba.assign(static_cast<size_t>(bitmap.width * bitmap.height * 4), 0);

    // Fill semi-transparent dark background.
    for (int i = 0; i < bitmap.width * bitmap.height; ++i)
    {
        uint8_t* px = bitmap.rgba.data() + i * 4;
        px[0] = kBgR;
        px[1] = kBgG;
        px[2] = kBgB;
        px[3] = kBgA;
    }

    // Render each row: label (dim) then value (bright).
    const int value_x = kTooltipPadding + label_col_width + kColumnGap;
    for (int i = 0; i < row_count; ++i)
    {
        const int line_y = kTooltipPadding + i * (cell_height + kLineSpacing);
        const int baseline_y = line_y + metrics.ascender;

        // Label column (right-aligned within label_col_width).
        const int label_pixel_width = overlay_text_width(data.entries[i].label) * cell_width;
        const int label_x = kTooltipPadding + (label_col_width - label_pixel_width);
        blit_text_line(
            text_service, data.entries[i].label,
            label_x, baseline_y,
            bitmap.rgba.data(), bitmap.width, bitmap.height,
            kLabelR, kLabelG, kLabelB);

        // Value column (left-aligned).
        blit_text_line(
            text_service, data.entries[i].value,
            value_x, baseline_y,
            bitmap.rgba.data(), bitmap.width, bitmap.height,
            kValueR, kValueG, kValueB);
    }

    return bitmap;
}

} // namespace draxul::gui
