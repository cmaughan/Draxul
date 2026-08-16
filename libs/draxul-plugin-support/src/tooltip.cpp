#include <draxul/gui/overlay_text.h>
#include <draxul/gui/tooltip.h>

#include <algorithm>
#include <draxul/cluster_blit.h>
#include <draxul/perf_timing.h>
#include <draxul/text_service.h>
#include <draxul/unicode.h>

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
    const std::vector<DisplayCluster> clusters = display_clusters(text);
    blit_cluster_run_rgba(text_service, clusters, pen_x, baseline_y,
        dst_pixels, dst_width, dst_height,
        [r, g, b](uint8_t* dst, uint8_t coverage, int, int) {
            // Alpha-blend text over existing background.
            const float sa = static_cast<float>(coverage) / 255.0f;
            const float da = 1.0f - sa;
            dst[0] = static_cast<uint8_t>(std::min(255.0f, r * sa + dst[0] * da));
            dst[1] = static_cast<uint8_t>(std::min(255.0f, g * sa + dst[1] * da));
            dst[2] = static_cast<uint8_t>(std::min(255.0f, b * sa + dst[2] * da));
            dst[3] = static_cast<uint8_t>(std::min(255.0f, coverage + dst[3] * da));
        });
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
