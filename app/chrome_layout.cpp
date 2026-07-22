#include "chrome_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <draxul/unicode.h>

namespace draxul
{
namespace
{
constexpr int kTabPadCols = 1;
constexpr int kEditMinNameCols = 10;
constexpr int kPaneStatusRightMarginCols = 1;

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

Color apply_alpha(Color color, float alpha)
{
    color.a *= std::clamp(alpha, 0.0f, 1.0f);
    return color;
}

std::vector<std::string> split_display_clusters(std::string_view text)
{
    std::vector<std::string> clusters;
    for (auto& cluster : display_clusters(text))
        clusters.push_back(std::move(cluster.text));
    return clusters;
}

int display_columns(std::string_view text)
{
    int columns = 0;
    for (const auto& cluster : split_display_clusters(text))
        columns += std::max(1, cluster_cell_width(cluster));
    return columns;
}

size_t utf8_next(std::string_view text, size_t pos)
{
    if (pos >= text.size())
        return text.size();
    ++pos;
    while (pos < text.size() && (static_cast<unsigned char>(text[pos]) & 0xC0) == 0x80)
        ++pos;
    return pos;
}

int columns_to_offset(std::string_view buffer, size_t pos)
{
    int columns = 0;
    size_t offset = 0;
    while (offset < pos && offset < buffer.size())
    {
        const size_t next = utf8_next(buffer, offset);
        columns += std::max(1, cluster_cell_width(buffer.substr(offset, next - offset)));
        offset = next;
    }
    return columns;
}

std::string truncate_to_columns(std::string_view text, int max_cols)
{
    if (max_cols <= 0)
        return {};
    if (display_columns(text) <= max_cols)
        return std::string(text);
    const int budget = std::max(0, max_cols - 1);
    std::string result;
    int used = 0;
    for (const auto& cluster : split_display_clusters(text))
    {
        const int width = std::max(1, cluster_cell_width(cluster));
        if (used + width > budget)
            break;
        result += cluster;
        used += width;
    }
    result += "\xe2\x80\xa6";
    return result;
}

void append_clusters(std::vector<ChromeLabelCluster>& out, std::string_view text, const Color& fg)
{
    for (const auto& cluster : split_display_clusters(text))
        out.push_back({ cluster, std::max(1, cluster_cell_width(cluster)), fg });
}

int cluster_columns(const std::vector<ChromeLabelCluster>& clusters)
{
    int columns = 0;
    for (const auto& cluster : clusters)
        columns += std::max(1, cluster.width);
    return columns;
}

std::string percent_text(int percent)
{
    char buffer[16];
    if (percent < 0)
        std::snprintf(buffer, sizeof(buffer), "--%%");
    else
        std::snprintf(buffer, sizeof(buffer), "%3d%%", percent);
    return buffer;
}

void append_resource(std::vector<ChromeLabelCluster>& out, const char* label, int percent,
    Color value_fg, Color label_fg, bool separator)
{
    append_clusters(out, label, label_fg);
    append_clusters(out, " ", label_fg);
    append_clusters(out, percent_text(percent), value_fg);
    if (separator)
        append_clusters(out, "  ", value_fg);
}

Color resource_bg(const SystemResourceSnapshot& snapshot, const ChromeTheme& theme)
{
    if (snapshot.cpu_percent >= 100)
        return theme.resource_pill_hot_bg;
    if (snapshot.cpu_percent >= 90 || snapshot.memory_percent >= 90)
        return theme.resource_pill_warn_bg;
    return theme.resource_pill_bg;
}
} // namespace

PaneStatusPillLayout pane_status_pill_layout(
    int pane_w_px, int cell_w_px, int number_cols, int status_text_cols, bool editing)
{
    PaneStatusPillLayout layout;
    if (cell_w_px <= 0 || number_cols <= 0)
        return layout;
    const int available = pane_w_px / cell_w_px - kPaneStatusRightMarginCols;
    const int padding = kTabPadCols * 2;
    const int prefix = number_cols + 2;
    if (editing)
        status_text_cols = std::max(status_text_cols, kEditMinNameCols);
    if (available >= padding + prefix + 1)
    {
        layout.visible = true;
        layout.text_cols = std::min(status_text_cols, available - padding - prefix);
        layout.pill_cols = padding + prefix + layout.text_cols;
    }
    else if (available >= padding + number_cols + 1)
    {
        layout.visible = true;
        layout.number_only = true;
        layout.pill_cols = padding + number_cols + 1;
    }
    return layout;
}

ChromeLayoutOutput compute_chrome_layout(const ChromeLayoutInput& input)
{
    ChromeLayoutOutput out;
    out.bar_width = input.viewport_width;
    out.cell_width = input.cell_width;
    out.cell_height = input.cell_height;
    out.grid_padding = input.grid_padding;
    out.dividers = input.dividers;
    out.focus_rect = input.focus_rect;
    out.focus_border = input.focus_border;
    out.edit_started_at = input.rename.started_at;

    const int cw = input.cell_width;
    const int ch = input.cell_height;
    if (input.show_top_bar && cw > 0 && ch > 0)
    {
        out.bar_height = ch + 2;
        out.grid_cols = std::max(0, input.viewport_width / cw);
        out.top_bar_clip = { 0.0f, 0.0f, static_cast<float>(input.viewport_width),
            static_cast<float>(out.bar_height) };
        int right_cursor = out.grid_cols;
        if (input.resources && input.resources->available() && out.grid_cols > 0)
        {
            ChromeRightPillLayout pill;
            pill.bg = resource_bg(*input.resources, input.theme);
            pill.flat_right_edge = true;
            const Color value_fg = input.theme.resource_pill_fg;
            const Color label_fg = apply_alpha(value_fg, 0.68f);
            append_resource(pill.clusters, "CPU", input.resources->cpu_percent, value_fg, label_fg, true);
            append_resource(pill.clusters, "RAM", input.resources->memory_percent, value_fg, label_fg, false);
            const int total = cluster_columns(pill.clusters) + kTabPadCols * 2;
            pill.col_end = right_cursor;
            pill.col_begin = std::max(0, right_cursor - total);
            pill.text_col = pill.col_begin + kTabPadCols;
            right_cursor = std::max(0, pill.col_begin - 1);
            out.right_pills.push_back(std::move(pill));
        }
        if (!input.weather_temperature.empty() && out.grid_cols > 0)
        {
            ChromeRightPillLayout pill;
            pill.bg = input.theme.weather_pill_bg;
            const Color fg = text_for_bg(pill.bg);
            if (!input.weather_emoji.empty())
            {
                append_clusters(pill.clusters, input.weather_emoji, fg);
                append_clusters(pill.clusters, " ", fg);
            }
            append_clusters(pill.clusters, input.weather_temperature, fg);
            const int total = cluster_columns(pill.clusters) + kTabPadCols * 2;
            pill.col_end = right_cursor;
            pill.col_begin = std::max(0, right_cursor - total);
            pill.text_col = pill.col_begin + kTabPadCols;
            right_cursor = std::max(0, pill.col_begin - 1);
            out.right_pills.push_back(std::move(pill));
        }
        if (input.chord && input.chord->second > 0.0f && out.grid_cols > 0)
        {
            ChromeRightPillLayout pill;
            pill.bg = apply_alpha(input.theme.chord_pill_bg, input.chord->second);
            append_clusters(pill.clusters, input.chord->first,
                apply_alpha(text_for_bg(input.theme.chord_pill_bg), input.chord->second));
            const int total = cluster_columns(pill.clusters) + kTabPadCols * 2;
            pill.col_end = right_cursor;
            pill.col_begin = std::max(0, right_cursor - total);
            pill.text_col = pill.col_begin + kTabPadCols;
            right_cursor = std::max(0, pill.col_begin - 1);
            out.right_pills.push_back(std::move(pill));
        }

        const float pill_h = static_cast<float>(ch) - 4.0f;
        const float half_gap = static_cast<float>(cw) * 0.25f;
        for (auto& pill : out.right_pills)
        {
            const int total = std::max(1, pill.col_end - pill.col_begin);
            const float x = std::max(0.0f,
                static_cast<float>(pill.col_begin * cw + input.grid_padding) + half_gap);
            const float width = pill.flat_right_edge && pill.col_end >= input.viewport_width / cw
                ? static_cast<float>(input.viewport_width) - x
                : static_cast<float>(total * cw) - half_gap * 2.0f;
            pill.rect = { x, 2.0f, width, pill_h };
            pill.clip = pill.rect;
            pill.accent_w = static_cast<float>(pill.accent_cols * cw);
        }

        const int tabs_end = out.right_pills.empty() ? out.grid_cols : std::max(0, right_cursor);
        int col = 0;
        for (size_t i = 0; i < input.tabs.size(); ++i)
        {
            const auto& source = input.tabs[i];
            const bool editing = input.rename.target == RenameTarget::Tab
                && input.rename.tab_id == source.tab_id;
            const std::string name = editing ? std::string(input.rename.buffer) : source.name;
            const std::string label = std::to_string(i + 1) + ": " + name;
            int label_cols = display_columns(label);
            const int digits = static_cast<int>(std::to_string(i + 1).size());
            if (editing)
                label_cols = std::max(label_cols, digits + 2 + kEditMinNameCols);
            const int total = label_cols + kTabPadCols * 2;
            if (tabs_end > 0 && col + total > tabs_end)
                break;
            ChromeTabLayout tab;
            tab.tab_id = source.tab_id;
            tab.tab_index = static_cast<int>(i) + 1;
            tab.col_begin = col;
            tab.col_end = col + total;
            tab.text_col = col + kTabPadCols;
            tab.active = source.active;
            tab.editing = editing;
            tab.label = label;
            tab.rect = { static_cast<float>(col * cw + input.grid_padding) + half_gap,
                2.0f, static_cast<float>(total * cw) - half_gap * 2.0f, pill_h };
            tab.clip = tab.rect;
            tab.accent_w = static_cast<float>((kTabPadCols + digits + 1) * cw);
            out.hit_regions.push_back({ ChromeHitKind::Tab, tab.tab_index,
                { static_cast<float>(col * cw + input.grid_padding), 0.0f,
                    static_cast<float>(total * cw), static_cast<float>(ch) } });
            if (editing)
            {
                const int caret_col = tab.text_col + digits + 2
                    + columns_to_offset(input.rename.buffer, input.rename.cursor);
                out.tab_caret = ChromeCaretLayout{ { static_cast<float>(caret_col * cw + input.grid_padding),
                    4.0f, 1.5f, pill_h - 4.0f } };
            }
            out.tabs.push_back(std::move(tab));
            col += total;
        }
    }

    if (input.show_status && cw > 0 && ch > 0)
    {
        for (const auto& pane : input.panes)
        {
            const bool editing = input.rename.target == RenameTarget::Pane
                && input.rename.leaf_id == pane.leaf;
            const std::string display_text = editing ? std::string(input.rename.buffer) : pane.text;
            const std::string number = std::to_string(pane.index);
            const int text_cols = display_columns(display_text);
            const auto size = pane_status_pill_layout(pane.pane_w, cw,
                static_cast<int>(number.size()), text_cols, editing);
            if (!size.visible)
                continue;
            ChromePaneLayout status;
            status.leaf = pane.leaf;
            status.pill_cols = size.pill_cols;
            status.prefix_cols = static_cast<int>(number.size()) + 1;
            status.focused = pane.focused;
            status.editing = editing;
            status.number_only = size.number_only;
            if (size.number_only)
                status.label = number + ":";
            else if (size.text_cols < text_cols)
                status.label = number + ": " + truncate_to_columns(display_text, size.text_cols);
            else
                status.label = number + ": " + display_text;

            const float half_gap = static_cast<float>(cw) * 0.25f;
            const float width = static_cast<float>(size.pill_cols * cw) - half_gap * 2.0f;
            const float right = static_cast<float>(pane.pane_x + pane.pane_w - kPaneStatusRightMarginCols * cw);
            const float x = right - width;
            const float y = static_cast<float>(pane.pane_y + pane.pane_h - ch) + 2.0f;
            status.rect = { x, y, width, static_cast<float>(ch) - 4.0f };
            status.clip = status.rect;
            status.accent_w = size.number_only ? width
                                              : static_cast<float>((kTabPadCols + number.size() + 1) * cw);
            const int half_gap_i = cw / 4;
            const int width_i = size.pill_cols * cw - half_gap_i * 2;
            const int x_i = pane.pane_x + pane.pane_w - kPaneStatusRightMarginCols * cw - width_i;
            status.viewport_x = x_i - input.grid_padding - half_gap_i;
            status.viewport_y = pane.pane_y + pane.pane_h - ch - input.grid_padding;
            status.viewport_w = width_i + input.grid_padding * 2;
            status.viewport_h = ch + input.grid_padding;
            out.hit_regions.push_back({ ChromeHitKind::PaneStatus, pane.leaf, status.rect });
            if (editing && !size.number_only)
            {
                const int prefix = static_cast<int>(number.size()) + 2;
                const float text_x = x - half_gap
                    + static_cast<float>((kTabPadCols + prefix) * cw);
                const float caret_x = std::min(text_x
                        + static_cast<float>(columns_to_offset(input.rename.buffer, input.rename.cursor) * cw),
                    x + width - 3.0f);
                out.pane_caret = ChromeCaretLayout{ { caret_x, y + 2.0f, 1.5f,
                    static_cast<float>(ch) - 8.0f } };
            }
            out.panes.push_back(std::move(status));
        }
    }
    return out;
}

int hit_test_chrome(const ChromeLayoutOutput& layout, ChromeHitKind kind, int px, int py)
{
    const float x = static_cast<float>(px);
    const float y = static_cast<float>(py);
    for (const auto& hit : layout.hit_regions)
    {
        if (hit.kind == kind && x >= hit.rect.x && x < hit.rect.x + hit.rect.w
            && y >= hit.rect.y && y < hit.rect.y + hit.rect.h)
            return hit.stable_id;
    }
    return kind == ChromeHitKind::PaneStatus ? kInvalidLeaf : 0;
}

} // namespace draxul
