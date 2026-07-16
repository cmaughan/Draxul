#include <draxul/gui/toast_renderer.h>
#include <draxul/gui/overlay_text.h>

#include <algorithm>
#include <cmath>
#include <draxul/text_service.h>

namespace draxul::gui
{

namespace
{

constexpr Color kInfoBg{ 0.12f, 0.16f, 0.22f, 0.92f };
constexpr Color kWarnBg{ 0.22f, 0.18f, 0.08f, 0.92f };
constexpr Color kErrorBg{ 0.24f, 0.08f, 0.08f, 0.92f };

constexpr Color kInfoFg{ 0.7f, 0.82f, 0.95f, 1.0f };
constexpr Color kWarnFg{ 1.0f, 0.88f, 0.45f, 1.0f };
constexpr Color kErrorFg{ 1.0f, 0.55f, 0.5f, 1.0f };

constexpr Color kTextFg{ 0.88f, 0.88f, 0.9f, 1.0f };
constexpr Color kTransparent{ 0.0f, 0.0f, 0.0f, 0.0f };

constexpr int kPadH = 1; // horizontal padding inside toast
constexpr int kPadV = 0; // vertical padding (rows above/below text)
constexpr int kMarginRight = 2; // cells from right edge
constexpr int kMarginBottom = 1; // cells from bottom edge
constexpr int kToastGap = 1; // cells between stacked toasts
constexpr int kMaxToastCols = 50; // max toast width in cells
constexpr float kFadeDuration = 0.5f; // last N seconds are fade-out

Color apply_alpha(Color c, float alpha)
{
    return { c.r, c.g, c.b, c.a * alpha };
}

Color bg_for_level(ToastLevel level)
{
    switch (level)
    {
    case ToastLevel::Warn:
        return kWarnBg;
    case ToastLevel::Error:
        return kErrorBg;
    default:
        return kInfoBg;
    }
}

Color prefix_fg_for_level(ToastLevel level)
{
    switch (level)
    {
    case ToastLevel::Warn:
        return kWarnFg;
    case ToastLevel::Error:
        return kErrorFg;
    default:
        return kInfoFg;
    }
}

std::string_view prefix_for_level(ToastLevel level)
{
    switch (level)
    {
    case ToastLevel::Warn:
        return "WARN ";
    case ToastLevel::Error:
        return "ERR  ";
    default:
        return "INFO ";
    }
}

} // namespace

std::vector<CellUpdate> render_toasts(
    const ToastViewState& state,
    draxul::TextService& text_service)
{
    if (state.grid_cols <= 0 || state.grid_rows <= 0 || state.entries.empty())
        return {};

    const AtlasRegion block_glyph = resolve_occlusion_glyph(text_service);

    std::vector<CellUpdate> cells;

    // Toasts stack upward from the bottom-right corner.
    int current_row = state.grid_rows - 1 - kMarginBottom;

    for (const auto& entry : state.entries)
    {
        if (current_row < 0)
            break;

        // Compute fade alpha: full opacity except during the last kFadeDuration seconds.
        float alpha = 1.0f;
        if (entry.remaining_s < kFadeDuration && entry.total_s > kFadeDuration)
            alpha = std::clamp(entry.remaining_s / kFadeDuration, 0.0f, 1.0f);

        const auto prefix = prefix_for_level(entry.level);
        const int content_len = overlay_text_width(prefix) + overlay_text_width(entry.message);
        const int toast_cols = std::min(content_len + kPadH * 2, std::min(kMaxToastCols, state.grid_cols));
        const int toast_rows = 1 + kPadV * 2;
        const int col0 = state.grid_cols - kMarginRight - toast_cols;
        const int row0 = current_row - toast_rows + 1;

        if (row0 < 0 || col0 < 0)
            break;

        const Color bg = apply_alpha(bg_for_level(entry.level), alpha);
        const Color pfx_fg = apply_alpha(prefix_fg_for_level(entry.level), alpha);
        const Color text_fg = apply_alpha(kTextFg, alpha);

        // Fill background.
        for (int r = 0; r < toast_rows; ++r)
        {
            for (int c = 0; c < toast_cols; ++c)
            {
                CellUpdate cu;
                cu.col = col0 + c;
                cu.row = row0 + r;
                cu.bg = bg;
                cu.fg = bg; // block glyph matches bg to occlude underlying text
                cu.sp = kTransparent;
                cu.glyph = block_glyph;
                cu.style_flags = 0;
                cells.push_back(cu);
            }
        }

        // Write prefix + message on the text row.
        const int text_row = row0 + kPadV;
        const int text_col0 = col0 + kPadH;
        const int max_text = toast_cols - kPadH * 2;
        auto append_text = [&](std::string_view text, Color fg, int start_column) {
            int consumed = 0;
            for (const auto& cluster : layout_overlay_text(text, max_text - start_column))
            {
                CellUpdate cu;
                cu.col = text_col0 + start_column + cluster.column;
                cu.row = text_row;
                cu.bg = bg;
                cu.fg = fg;
                cu.sp = kTransparent;
                cu.glyph = text_service.resolve_cluster(cluster.text);
                cu.style_flags = 0;
                cells.push_back(cu);

                for (int continuation = 1; continuation < cluster.cell_width; ++continuation)
                {
                    cu.col += 1;
                    cu.fg = bg;
                    cu.glyph = {};
                    cells.push_back(cu);
                }
                consumed = cluster.column + cluster.cell_width;
            }
            return consumed;
        };

        int ci = append_text(prefix, pfx_fg, 0);
        if (ci < max_text)
        {
            ci += append_text(entry.message, text_fg, ci);
        }

        current_row = row0 - kToastGap - 1;
    }

    return cells;
}

} // namespace draxul::gui
