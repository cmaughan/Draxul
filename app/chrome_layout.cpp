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

int pane_content_inset(float focus_border_width)
{
    return std::max(kPaneContentInset,
        kPaneFrameOuterMargin
            + static_cast<int>(std::ceil(std::max(0.0f, focus_border_width))) + 1);
}

float pane_frame_line_inset(float focus_border_width)
{
    return static_cast<float>(kPaneFrameOuterMargin)
        + std::ceil(std::max(0.0f, focus_border_width) * 0.5f);
}

int pane_content_edge_inset(float focus_border_width, bool window_edge)
{
    if (window_edge)
        return pane_content_inset(focus_border_width) + kPaneWindowEdgeExtraInset;
    return kPaneFrameOuterMargin / 2
        + static_cast<int>(std::ceil(std::max(0.0f, focus_border_width) * 0.5f))
        + 1;
}

float pane_frame_line_edge_inset(float focus_border_width, bool window_edge)
{
    if (window_edge)
    {
        return pane_frame_line_inset(focus_border_width)
            + static_cast<float>(kPaneWindowEdgeExtraInset);
    }
    return static_cast<float>(kPaneFrameOuterMargin) * 0.5f;
}

ChromeLayoutOutput compute_chrome_layout(const ChromeLayoutInput& input)
{
    ChromeLayoutOutput out;
    const AppShellLayout& shell = input.shell_layout;
    out.content_x = shell.content.x;
    out.bar_width = shell.tab_bar.w;
    out.cell_width = input.cell_width;
    out.cell_height = input.cell_height;
    out.grid_padding = input.grid_padding;
    out.dividers = input.dividers;
    out.focus_border = input.focus_border;
    out.edit_started_at = input.rename.started_at;

    const int cw = input.cell_width;
    const int ch = input.cell_height;
    const size_t visible_pane_count = static_cast<size_t>(std::count_if(
        input.panes.begin(), input.panes.end(), [](const ChromePaneInput& pane) {
            return pane.pane_w > 0 && pane.pane_h > 0;
        }));
    for (const auto& pane : input.panes)
    {
        if (pane.pane_w <= 0 || pane.pane_h <= 0)
            continue;
        const bool window_left = pane.pane_x <= shell.pane_root.x;
        const bool window_top = pane.pane_y <= shell.pane_root.y;
        const bool window_right = pane.pane_x + pane.pane_w
            >= shell.pane_root.x + shell.pane_root.w;
        const bool window_bottom = pane.pane_y + pane.pane_h
            >= shell.pane_root.y + shell.pane_root.h;
        const float frame_left = pane_frame_line_edge_inset(
            input.focus_border, window_left);
        const float frame_top = pane_frame_line_edge_inset(
            input.focus_border, window_top);
        const float frame_right = pane_frame_line_edge_inset(
            input.focus_border, window_right);
        const float frame_bottom = pane_frame_line_edge_inset(
            input.focus_border, window_bottom);
        const int content_left = pane_content_edge_inset(
            input.focus_border, window_left);
        const int content_top = pane_content_edge_inset(
            input.focus_border, window_top);
        const int content_right = pane_content_edge_inset(
            input.focus_border, window_right);
        const int content_bottom = pane_content_edge_inset(
            input.focus_border, window_bottom);
        ChromePaneFrameLayout frame;
        frame.leaf = pane.leaf;
        // A focus accent only communicates a choice when the tab has multiple
        // visible panes. Keep the subtle inactive frame for a single-pane tab.
        frame.focused = pane.focused && visible_pane_count > 1;
        frame.content_background = pane.background;
        frame.outer = {
            static_cast<float>(pane.pane_x), static_cast<float>(pane.pane_y),
            static_cast<float>(pane.pane_w), static_cast<float>(pane.pane_h)
        };
        frame.rect = {
            static_cast<float>(pane.pane_x) + frame_left,
            static_cast<float>(pane.pane_y) + frame_top,
            std::max(0.0f, static_cast<float>(pane.pane_w) - frame_left - frame_right),
            std::max(0.0f, static_cast<float>(pane.pane_h) - frame_top - frame_bottom)
        };
        if (input.show_status && pane.grid_rows > 0 && ch > 0)
        {
            const float content_x = static_cast<float>(pane.pane_x + content_left);
            const float content_y = static_cast<float>(pane.pane_y + content_top);
            const float host_bottom = static_cast<float>(
                pane.pane_y + pane.pane_h - content_bottom - chrome_pill_band_height(ch));
            const float grid_bottom = content_y
                + static_cast<float>(input.grid_padding + pane.grid_rows * ch);
            const float tail_y = std::clamp(grid_bottom, content_y, host_bottom);
            frame.content_tail = {
                content_x,
                tail_y,
                static_cast<float>(
                    std::max(0, pane.pane_w - content_left - content_right)),
                std::max(0.0f, host_bottom - tail_y)
            };
        }
        out.pane_frames.push_back(frame);
    }
    if (shell.sidebar_visible && cw > 0 && ch > 0 && !input.spaces.empty())
    {
        out.sidebar_width = shell.sidebar.w;
        out.sidebar_height = shell.sidebar.h;
        out.sidebar_cols = std::max(1, out.sidebar_width / cw);
        out.sidebar_rows = std::max(1, out.sidebar_height / ch);
        out.sidebar_rect = {
            static_cast<float>(shell.sidebar.x), static_cast<float>(shell.sidebar.y),
            static_cast<float>(shell.sidebar.w), static_cast<float>(shell.sidebar.h)
        };
        const float sidebar_frame_left = pane_frame_line_edge_inset(
            input.focus_border, shell.sidebar.x <= shell.window.x);
        const float sidebar_frame_top = pane_frame_line_edge_inset(
            input.focus_border, shell.sidebar.y <= shell.window.y);
        const float sidebar_frame_right = pane_frame_line_edge_inset(
            input.focus_border,
            shell.sidebar.x + shell.sidebar.w >= shell.window.x + shell.window.w);
        const float sidebar_frame_bottom = pane_frame_line_edge_inset(
            input.focus_border,
            shell.sidebar.y + shell.sidebar.h >= shell.window.y + shell.window.h);
        out.sidebar_frame = {
            static_cast<float>(shell.sidebar.x) + sidebar_frame_left,
            static_cast<float>(shell.sidebar.y) + sidebar_frame_top,
            std::max(0.0f,
                static_cast<float>(shell.sidebar.w)
                    - sidebar_frame_left - sidebar_frame_right),
            std::max(0.0f,
                static_cast<float>(shell.sidebar.h)
                    - sidebar_frame_top - sidebar_frame_bottom)
        };
        const auto section_header = [&](float top) {
            return ChromeRect{
                out.sidebar_frame.x + 1.0f,
                top,
                std::max(0.0f, out.sidebar_frame.w - 2.0f),
                static_cast<float>(std::max(0, ch - 4))
            };
        };
        out.sidebar_spaces_header = section_header(out.sidebar_frame.y);
        out.sidebar_spaces_rect = {
            static_cast<float>(shell.sidebar_spaces.x),
            static_cast<float>(shell.sidebar_spaces.y),
            static_cast<float>(shell.sidebar_spaces.w),
            static_cast<float>(shell.sidebar_spaces.h)
        };
        out.sidebar_section_divider = {
            out.sidebar_frame.x,
            static_cast<float>(shell.sidebar_section_divider.y),
            out.sidebar_frame.w,
            shell.sidebar_section_divider.h > 0 ? 1.0f : 0.0f
        };
        out.sidebar_agents_rect = {
            static_cast<float>(shell.sidebar_agents.x),
            static_cast<float>(shell.sidebar_agents.y),
            static_cast<float>(shell.sidebar_agents.w),
            static_cast<float>(shell.sidebar_agents.h)
        };
        out.sidebar_agents_header = section_header(
            out.sidebar_section_divider.y + out.sidebar_section_divider.h);
        out.sidebar_divider = {
            static_cast<float>(shell.sidebar_divider.x),
            static_cast<float>(shell.sidebar_divider.y),
            static_cast<float>(shell.sidebar_divider.w),
            static_cast<float>(shell.sidebar_divider.h)
        };
        for (size_t i = 0; i < input.spaces.size(); ++i)
        {
            const int row = static_cast<int>(i) + kSidebarFirstSpaceRow;
            const int row_bottom = shell.sidebar.y + (row + 1) * ch;
            if (row >= out.sidebar_rows
                || row_bottom > shell.sidebar_spaces.y + shell.sidebar_spaces.h)
                break;
            const auto& source = input.spaces[i];
            const std::string prefix = std::to_string(i + 1) + ": ";
            const int digits = static_cast<int>(std::to_string(i + 1).size());
            const int max_label_cols = std::max(
                1, out.sidebar_cols - kTabPadCols * 2 - 1);
            ChromeSpaceLayout space;
            space.space_id = source.space_id;
            space.space_index = static_cast<int>(i) + 1;
            space.row = row;
            space.active = source.active;
            space.label = prefix + truncate_to_columns(
                source.name, std::max(1, max_label_cols - display_columns(prefix)));
            const int total = std::min(out.sidebar_cols - 1,
                display_columns(space.label) + kTabPadCols * 2);
            static_cast<ChromePillLayout&>(space) = layout_chrome_pill({
                .grid_x = static_cast<float>(shell.sidebar.x),
                .grid_y = static_cast<float>(shell.sidebar.y + row * ch),
                .columns = total,
                .text_col = kTabPadCols,
                .prefix_cols = digits + 1,
                .cell_width = cw,
                .cell_height = ch,
                .left_inset = static_cast<float>(input.grid_padding),
                .label = space.label,
                .palette = chrome_pill_palette(
                    input.theme, ChromePillRole::Space, space.active),
            });
            out.hit_regions.push_back({ ChromeHitKind::Space, space.space_id,
                { static_cast<float>(shell.sidebar.x),
                    static_cast<float>(shell.sidebar.y + row * ch),
                    static_cast<float>(out.sidebar_width), static_cast<float>(ch) } });
            out.spaces.push_back(std::move(space));
        }
    }
    if (input.show_top_bar && cw > 0 && ch > 0)
    {
        out.bar_height = shell.tab_bar.h;
        out.grid_cols = std::max(0, out.bar_width / cw);
        out.top_bar_clip = { static_cast<float>(shell.tab_bar.x),
            static_cast<float>(shell.tab_bar.y),
            static_cast<float>(out.bar_width),
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
            const Color fg = chrome_pill_text_color(pill.bg);
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
                apply_alpha(chrome_pill_text_color(input.theme.chord_pill_bg),
                    input.chord->second));
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
            const float x = static_cast<float>(out.content_x) + std::max(0.0f,
                static_cast<float>(pill.col_begin * cw + input.grid_padding) + half_gap);
            const float width = pill.flat_right_edge && pill.col_end >= out.bar_width / cw
                ? static_cast<float>(shell.tab_bar.x + shell.tab_bar.w) - x
                : static_cast<float>(total * cw) - half_gap * 2.0f;
            pill.rect = { x, static_cast<float>(shell.tab_bar.y + 2), width, pill_h };
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
            tab.active = source.active;
            tab.editing = editing;
            static_cast<ChromePillLayout&>(tab) = layout_chrome_pill({
                .grid_x = static_cast<float>(out.content_x + col * cw),
                .grid_y = static_cast<float>(shell.tab_bar.y),
                .columns = total,
                .text_col = col + kTabPadCols,
                .prefix_cols = digits + 1,
                .cell_width = cw,
                .cell_height = ch,
                .left_inset = static_cast<float>(input.grid_padding),
                .label = label,
                .palette = chrome_pill_palette(
                    input.theme, ChromePillRole::Tab, tab.active, tab.editing),
            });
            out.hit_regions.push_back({ ChromeHitKind::Tab, tab.tab_index,
                { static_cast<float>(out.content_x + col * cw + input.grid_padding),
                    static_cast<float>(shell.tab_bar.y),
                    static_cast<float>(total * cw), static_cast<float>(ch) } });
            if (editing)
            {
                const int caret_col = tab.text_col + digits + 2
                    + columns_to_offset(input.rename.buffer, input.rename.cursor);
                out.tab_caret = ChromeCaretLayout{ { static_cast<float>(out.content_x + caret_col * cw + input.grid_padding),
                    static_cast<float>(shell.tab_bar.y + 4), 1.5f, pill_h - 4.0f } };
            }
            out.tabs.push_back(std::move(tab));
            col += total;
        }
    }

    if (input.show_status && cw > 0 && ch > 0)
    {
        for (const auto& pane : input.panes)
        {
            const bool window_left = pane.pane_x <= shell.pane_root.x;
            const bool window_top = pane.pane_y <= shell.pane_root.y;
            const bool window_right = pane.pane_x + pane.pane_w
                >= shell.pane_root.x + shell.pane_root.w;
            const bool window_bottom = pane.pane_y + pane.pane_h
                >= shell.pane_root.y + shell.pane_root.h;
            const int content_left = pane_content_edge_inset(
                input.focus_border, window_left);
            const int content_top = pane_content_edge_inset(
                input.focus_border, window_top);
            const int content_right = pane_content_edge_inset(
                input.focus_border, window_right);
            const int content_bottom = pane_content_edge_inset(
                input.focus_border, window_bottom);
            const bool editing = input.rename.target == RenameTarget::Pane
                && input.rename.leaf_id == pane.leaf;
            const std::string display_text = editing ? std::string(input.rename.buffer) : pane.text;
            const std::string number = std::to_string(pane.index);
            const int text_cols = display_columns(display_text);
            const int content_width = std::max(
                0, pane.pane_w - content_left - content_right);
            const auto size = pane_status_pill_layout(content_width, cw,
                static_cast<int>(number.size()), text_cols, editing);
            if (!size.visible || pane.pane_h <= ch + content_top + content_bottom)
                continue;
            ChromePaneLayout status;
            status.leaf = pane.leaf;
            status.focused = pane.focused;
            status.editing = editing;
            status.number_only = size.number_only;
            std::string label;
            if (size.number_only)
                label = number + ":";
            else if (size.text_cols < text_cols)
                label = number + ": " + truncate_to_columns(display_text, size.text_cols);
            else
                label = number + ": " + display_text;

            const float right = static_cast<float>(
                pane.pane_x + pane.pane_w - content_right
                - kPaneStatusRightMarginCols * cw);
            const float logical_x = right - static_cast<float>(size.pill_cols * cw);
            const float logical_y = static_cast<float>(
                pane.pane_y + pane.pane_h - content_bottom - ch);
            static_cast<ChromePillLayout&>(status) = layout_chrome_pill({
                .grid_x = logical_x,
                .grid_y = logical_y,
                .columns = size.pill_cols,
                .text_col = kTabPadCols,
                .prefix_cols = static_cast<int>(number.size()) + 1,
                .cell_width = cw,
                .cell_height = ch,
                .accent_fills_pill = size.number_only,
                .label = std::move(label),
                .palette = chrome_pill_palette(
                    input.theme, ChromePillRole::Pane, status.focused, status.editing),
            });
            const int logical_x_i = static_cast<int>(logical_x);
            status.viewport_x = logical_x_i - input.grid_padding;
            status.viewport_y = pane.pane_y + pane.pane_h - content_bottom
                - ch - input.grid_padding;
            status.viewport_w = size.pill_cols * cw + input.grid_padding * 2;
            status.viewport_h = ch + input.grid_padding;
            out.hit_regions.push_back({ ChromeHitKind::PaneStatus, pane.leaf, status.rect });
            if (editing && !size.number_only)
            {
                const int prefix = static_cast<int>(number.size()) + 2;
                const float half_gap = static_cast<float>(cw) * 0.25f;
                const float text_x = status.rect.x - half_gap
                    + static_cast<float>((kTabPadCols + prefix) * cw);
                const float caret_x = std::min(text_x
                        + static_cast<float>(columns_to_offset(input.rename.buffer, input.rename.cursor) * cw),
                    status.rect.x + status.rect.w - 3.0f);
                out.pane_caret = ChromeCaretLayout{ { caret_x, status.rect.y + 2.0f, 1.5f,
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
    if (kind == ChromeHitKind::PaneStatus)
        return kInvalidLeaf;
    if (kind == ChromeHitKind::Space)
        return -1;
    return 0;
}

} // namespace draxul
