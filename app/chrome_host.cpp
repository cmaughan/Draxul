#include "chrome_host.h"

#include "host_manager.h"
#include "workspace.h"

#include <SDL3/SDL_keycode.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <draxul/app_config.h>
#include <draxul/log.h>
#include <draxul/text_service.h>
#include <draxul/unicode.h>
#include <nanovg.h>
#include <string_view>
#include <unordered_set>

namespace draxul
{

ChromeHost::ChromeHost(Deps deps)
    : deps_(std::move(deps))
{
}

const ChromeTheme& ChromeHost::theme() const
{
    static const ChromeTheme defaults;
    return deps_.config ? deps_.config->chrome : defaults;
}

bool ChromeHost::initialize(const HostContext& context, IHostCallbacks& /*callbacks*/)
{
    viewport_ = context.initial_viewport;
    nanovg_pass_ = create_nanovg_pass();
    running_ = nanovg_pass_ != nullptr;
    return running_;
}

void ChromeHost::shutdown()
{
    tab_handle_.reset();
    nanovg_pass_.reset();
    running_ = false;
}

bool ChromeHost::is_running() const
{
    return running_;
}

void ChromeHost::set_viewport(const HostViewport& viewport)
{
    viewport_ = viewport;
}

const SplitTree& ChromeHost::active_tree() const
{
    if (!deps_.workspaces || !deps_.active_workspace_id)
    {
        static const SplitTree dummy;
        return dummy;
    }
    for (const auto& ws : *deps_.workspaces)
    {
        if (ws->id == *deps_.active_workspace_id)
            return ws->host_manager.tree();
    }
    static const SplitTree dummy;
    return dummy;
}

int ChromeHost::tab_bar_height() const
{
    if (!deps_.grid_renderer)
        return 0;
    const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
    return ch + 2; // extra gap below tab buttons
}

// ---------------------------------------------------------------------------
// Tab bar / window chrome helpers
// ---------------------------------------------------------------------------

namespace
{
// BT.709 relative luminance (glm has no built-in helper). Alpha ignored.
inline float relative_luminance(const Color& c)
{
    return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
}

// Pick a contrasting text color for the given background. Light backgrounds
// (luminance > 0.5) get a near-black ink; darker ones get a near-white ink.
// Alpha is left at 1.0 — caller can fade afterward if needed.
inline Color text_for_bg(const Color& bg)
{
    constexpr Color kDarkInk{ 0.10f, 0.10f, 0.12f, 1.0f };
    constexpr Color kLightInk{ 0.92f, 0.93f, 0.95f, 1.0f };
    return relative_luminance(bg) > 0.5f ? kDarkInk : kLightInk;
}

constexpr int kTabPadCols = 1; // padding cells on each side of tab label
// Minimum visible columns for the editable name portion of a tab during a
// rename session, so the field stays comfortably wide even before the user
// types anything. The "N: " prefix is added on top of this.
constexpr int kEditMinNameCols = 10;

// Step backward by one UTF-8 codepoint from `pos` in `s`. Returns the new
// position, or 0 if already at the start.
inline size_t utf8_prev(const std::string& s, size_t pos)
{
    if (pos == 0)
        return 0;
    --pos;
    while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
        --pos;
    return pos;
}

// Step forward by one UTF-8 codepoint from `pos` in `s`. Returns the new
// position, clamped to s.size().
inline size_t utf8_next(const std::string& s, size_t pos)
{
    if (pos >= s.size())
        return s.size();
    ++pos;
    while (pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80)
        ++pos;
    return pos;
}

// Count display columns from buffer start up to byte offset `pos`, using
// cluster_cell_width(). Used to position the rename caret.
inline int columns_to_offset(const std::string& buffer, size_t pos)
{
    int cols = 0;
    size_t i = 0;
    while (i < pos && i < buffer.size())
    {
        const size_t next = utf8_next(buffer, i);
        const std::string cluster = buffer.substr(i, next - i);
        cols += std::max(1, cluster_cell_width(cluster));
        i = next;
    }
    return cols;
}

// Pane status pill margin from the right edge of the pane (in cells).
constexpr int kPaneStatusRightMarginCols = 1;

constexpr int kGridPadding = 4; // renderer internal cell padding

std::string tab_label(size_t index, const std::string& name)
{
    return std::to_string(index + 1) + ": " + name;
}

std::vector<std::string> split_display_clusters(std::string_view text)
{
    std::vector<std::string> clusters;
    size_t offset = 0;
    while (offset < text.size())
    {
        const size_t cluster_begin = offset;
        uint32_t cp = 0;
        if (!utf8_decode_next(text, offset, cp))
            break;

        bool previous_was_zwj = (cp == 0x200D);
        while (offset < text.size())
        {
            size_t next = offset;
            uint32_t next_cp = 0;
            if (!utf8_decode_next(text, next, next_cp))
                break;

            const bool joins_cluster = previous_was_zwj || next_cp == 0x200D || is_width_ignorable(next_cp)
                || is_emoji_modifier(next_cp);
            if (!joins_cluster)
                break;

            previous_was_zwj = (next_cp == 0x200D);
            offset = next;
        }

        clusters.emplace_back(text.substr(cluster_begin, offset - cluster_begin));
    }
    return clusters;
}

// Count total display columns for a UTF-8 string, summing the cell width of
// each grapheme-like cluster returned by split_display_clusters(). This is the
// right thing to use for tab/pane label measurement — byte-length is wrong for
// anything non-ASCII (e.g. "Ångström" is 9 bytes but 8 columns, CJK is worse).
inline int label_display_columns(std::string_view text)
{
    int cols = 0;
    for (const auto& cluster : split_display_clusters(text))
        cols += std::max(1, cluster_cell_width(cluster));
    return cols;
}

// Truncate a UTF-8 string to at most `max_cols` display columns, never
// splitting a multi-byte sequence. If truncation occurs and `add_ellipsis` is
// true, the returned string ends with U+2026 (which itself costs one column,
// so the resulting string is <= max_cols columns).
inline std::string truncate_to_columns(std::string_view text, int max_cols, bool add_ellipsis)
{
    if (max_cols <= 0)
        return {};
    const auto clusters = split_display_clusters(text);
    int total = 0;
    for (const auto& cluster : clusters)
        total += std::max(1, cluster_cell_width(cluster));
    if (total <= max_cols)
        return std::string(text);

    // Truncation needed. If we're adding an ellipsis, reserve 1 column for it.
    const int budget = add_ellipsis ? std::max(0, max_cols - 1) : max_cols;
    std::string out;
    int used = 0;
    for (const auto& cluster : clusters)
    {
        const int w = std::max(1, cluster_cell_width(cluster));
        if (used + w > budget)
            break;
        out += cluster;
        used += w;
    }
    if (add_ellipsis)
        out += "\xe2\x80\xa6"; // U+2026 HORIZONTAL ELLIPSIS
    return out;
}

void append_label_clusters(std::vector<ChromeHost::LabelCluster>& out, std::string_view text, const Color& fg)
{
    for (const auto& cluster_text : split_display_clusters(text))
    {
        ChromeHost::LabelCluster cluster;
        cluster.text = cluster_text;
        cluster.width = std::max(1, cluster_cell_width(cluster_text));
        cluster.fg = fg;
        out.push_back(std::move(cluster));
    }
}

int label_cluster_columns(std::span<const ChromeHost::LabelCluster> clusters)
{
    int total = 0;
    for (const auto& cluster : clusters)
        total += std::max(1, cluster.width);
    return total;
}

Color apply_alpha(Color color, float alpha)
{
    color.a *= std::clamp(alpha, 0.0f, 1.0f);
    return color;
}

std::string format_resource_percent_value(int percent)
{
    char buffer[16];
    if (percent < 0)
        std::snprintf(buffer, sizeof(buffer), "--%%");
    else
        std::snprintf(buffer, sizeof(buffer), "%3d%%", percent);
    return std::string(buffer);
}

Color resource_pill_background_color(const SystemResourceSnapshot& snapshot, const ChromeTheme& theme)
{
    if (snapshot.cpu_percent >= 100)
        return theme.resource_pill_hot_bg;
    if (snapshot.cpu_percent >= 90 || snapshot.memory_percent >= 90)
        return theme.resource_pill_warn_bg;
    return theme.resource_pill_bg;
}

void append_resource_metric(std::vector<ChromeHost::LabelCluster>& out, const char* label, int percent,
    const Color& value_fg, const Color& label_fg, bool append_separator)
{
    append_label_clusters(out, label, label_fg);
    append_label_clusters(out, " ", label_fg);
    append_label_clusters(out, format_resource_percent_value(percent), value_fg);
    if (append_separator)
        append_label_clusters(out, "  ", value_fg);
}
} // namespace

PaneStatusPillLayout pane_status_pill_layout(
    int pane_w_px, int cell_w_px, int number_cols, int status_text_cols, bool editing)
{
    PaneStatusPillLayout layout;
    if (cell_w_px <= 0 || number_cols <= 0)
        return layout;
    const int avail_cols = pane_w_px / cell_w_px - kPaneStatusRightMarginCols;
    const int pad_cols = kTabPadCols * 2;
    const int prefix_cols = number_cols + 2; // "N:" plus the space before the text
    if (editing)
    {
        // Keep the rename field comfortably wide even when the buffer is
        // empty (still subject to the pane clamp below).
        status_text_cols = std::max(status_text_cols, kEditMinNameCols);
    }

    if (avail_cols >= pad_cols + prefix_cols + 1)
    {
        layout.visible = true;
        layout.text_cols = std::min(status_text_cols, avail_cols - pad_cols - prefix_cols);
        layout.pill_cols = pad_cols + prefix_cols + layout.text_cols;
    }
    else if (avail_cols >= pad_cols + number_cols + 1)
    {
        layout.visible = true;
        layout.number_only = true;
        layout.pill_cols = pad_cols + number_cols + 1; // "N:"
    }
    return layout;
}

int ChromeHost::hit_test_tab(int px, int py) const
{
    if (!deps_.workspaces || deps_.workspaces->empty() || !deps_.grid_renderer)
        return 0;
    const auto& workspaces = *deps_.workspaces;
    const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
    if (cw <= 0 || ch <= 0 || py < 0 || py >= ch)
        return 0;

    int col_cursor = 0;
    for (size_t wi = 0; wi < workspaces.size(); ++wi)
    {
        const bool editing = (edit_.target == EditTarget::Workspace
            && edit_.workspace_id == workspaces[wi]->id);
        const std::string display_name = editing ? edit_.buffer : workspaces[wi]->name;
        const std::string label = tab_label(wi, display_name);
        int label_cols = label_display_columns(label);
        if (editing)
        {
            const int prefix_cols = static_cast<int>(std::to_string(wi + 1).size()) + 2; // "N: "
            label_cols = std::max(label_cols, prefix_cols + kEditMinNameCols);
        }
        const int total_cols = label_cols + kTabPadCols * 2;
        const int tab_left = col_cursor * cw + kGridPadding;
        const int tab_right = (col_cursor + total_cols) * cw + kGridPadding;
        if (px >= tab_left && px < tab_right)
            return static_cast<int>(wi) + 1; // 1-based
        col_cursor += total_cols;
    }
    return 0;
}

void ChromeHost::draw(IFrameContext& frame)
{
    if (!nanovg_pass_)
        return;

    static const std::vector<std::unique_ptr<Workspace>> kEmpty;
    const auto& workspaces = deps_.workspaces ? *deps_.workspaces : kEmpty;
    const int active_ws_id = deps_.active_workspace_id ? *deps_.active_workspace_id : -1;
    const bool show_tabs = !workspaces.empty();
    const bool show_resource_pill = deps_.system_resource_snapshot && deps_.system_resource_snapshot->available();
    const bool show_top_bar = deps_.grid_renderer && (show_tabs || show_resource_pill);
    const bool show_dividers = active_tree().leaf_count() >= 2;
    const bool show_status = deps_.config && deps_.config->show_pane_status;
    const ChromeTheme chrome_theme = theme();

    // Collect per-pane status entries (WI 78). One row of cells (cell_h tall)
    // is reserved at the bottom of every pane by App::viewport_from_descriptor.
    std::vector<PaneStatusEntry> status_entries;
    int status_strip_h = 0;
    if (show_status && deps_.grid_renderer)
    {
        const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
        status_strip_h = ch;
        if (cw > 0 && ch > 0)
        {
            // Walk the active workspace's hosts so we can pull status_text() per pane.
            const HostManager* active_hm = nullptr;
            for (const auto& ws : workspaces)
            {
                if (ws->id == active_ws_id)
                {
                    active_hm = &ws->host_manager;
                    break;
                }
            }
            if (active_hm)
            {
                const SplitTree& tree = active_hm->tree();
                const LeafId focused_leaf = tree.focused();
                int next_index = 1;
                tree.for_each_leaf([&](LeafId id, const PaneDescriptor& desc) {
                    IHost* h = active_hm->host_for(id);
                    if (!h || desc.pixel_size.x <= 0 || desc.pixel_size.y <= ch)
                        return;
                    PaneStatusEntry e;
                    e.pane_x = desc.pixel_pos.x;
                    e.pane_y = desc.pixel_pos.y;
                    e.pane_w = desc.pixel_size.x;
                    e.pane_h = desc.pixel_size.y;
                    e.index = next_index++;
                    // Prefer the user-set pane name (rename override) over
                    // the host's auto-generated status text. Empty override
                    // falls back to status_text().
                    std::string override_name;
                    if (deps_.get_pane_name)
                        override_name = deps_.get_pane_name(id);
                    e.text = override_name.empty() ? h->status_text() : std::move(override_name);
                    if (!h->is_running() && e.text.find("[exited]") == std::string::npos)
                    {
                        if (!e.text.empty())
                            e.text += " ";
                        e.text += "[exited]";
                    }
                    e.focused = (id == focused_leaf);
                    e.leaf = id;
                    status_entries.push_back(std::move(e));
                });
            }
        }
    }

    if (!show_top_bar && !show_dividers && status_entries.empty())
        return;

    // --- Shared tab layout (used by both NanoVG and grid) ---
    // Grid cells are positioned at: pixel_x = col * cw + padding.
    // We compute tab spans in column space, then derive NanoVG pill pixel coords.
    std::vector<TabLayout> tabs;
    std::vector<RightPillLayout> right_pills;
    int bar_w = viewport_.pixel_size.x;
    int bar_h = 0;
    int cw_shared = 0;
    int ch_shared = 0;

    if (show_top_bar)
    {
        const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
        cw_shared = cw;
        ch_shared = ch;
        if (cw > 0 && ch > 0)
        {
            bar_h = tab_bar_height();
            const int grid_cols = std::max(0, bar_w / cw);
            int right_col_cursor = grid_cols;
            if (show_resource_pill && grid_cols > 0)
            {
                RightPillLayout layout;
                layout.bg = resource_pill_background_color(*deps_.system_resource_snapshot, chrome_theme);
                layout.flat_right_edge = true;
                const Color value_fg = chrome_theme.resource_pill_fg;
                const Color label_fg = apply_alpha(value_fg, 0.68f);
                append_resource_metric(layout.clusters, "CPU",
                    deps_.system_resource_snapshot->cpu_percent, value_fg, label_fg, true);
                append_resource_metric(layout.clusters, "RAM",
                    deps_.system_resource_snapshot->memory_percent, value_fg, label_fg, false);

                const int total_cols = label_cluster_columns(layout.clusters) + kTabPadCols * 2;
                layout.col_end = right_col_cursor;
                layout.col_begin = std::max(0, right_col_cursor - total_cols);
                layout.text_col = layout.col_begin + kTabPadCols;
                right_col_cursor = std::max(0, layout.col_begin - 1);
                right_pills.push_back(std::move(layout));
            }

            if (deps_.weather_emoji && deps_.weather_temperature && grid_cols > 0)
            {
                auto emoji = deps_.weather_emoji();
                auto temp = deps_.weather_temperature();
                if (!temp.empty())
                {
                    RightPillLayout layout;
                    layout.bg = chrome_theme.weather_pill_bg;
                    layout.flat_right_edge = false;
                    const Color fg = text_for_bg(layout.bg);
                    if (!emoji.empty())
                    {
                        append_label_clusters(layout.clusters, emoji, fg);
                        append_label_clusters(layout.clusters, " ", fg);
                    }
                    append_label_clusters(layout.clusters, temp, fg);

                    const int total_cols = label_cluster_columns(layout.clusters) + kTabPadCols * 2;
                    layout.col_end = right_col_cursor;
                    layout.col_begin = std::max(0, right_col_cursor - total_cols);
                    layout.text_col = layout.col_begin + kTabPadCols;
                    right_col_cursor = std::max(0, layout.col_begin - 1);
                    right_pills.push_back(std::move(layout));
                }
            }

            if (deps_.chord_indicator && grid_cols > 0)
            {
                if (auto state = deps_.chord_indicator(); state && state->second > 0.0f)
                {
                    RightPillLayout layout;
                    layout.bg = apply_alpha(chrome_theme.chord_pill_bg, state->second);
                    layout.flat_right_edge = false;
                    append_label_clusters(layout.clusters, state->first,
                        apply_alpha(text_for_bg(chrome_theme.chord_pill_bg), state->second));

                    const int total_cols = label_cluster_columns(layout.clusters) + kTabPadCols * 2;
                    layout.col_end = right_col_cursor;
                    layout.col_begin = std::max(0, right_col_cursor - total_cols);
                    layout.text_col = layout.col_begin + kTabPadCols;
                    right_col_cursor = std::max(0, layout.col_begin - 1);
                    right_pills.push_back(std::move(layout));
                }
            }

            int col_cursor = 0; // start at column 0
            const int tabs_end_col = right_pills.empty() ? grid_cols : std::max(0, right_col_cursor);
            for (size_t wi = 0; wi < workspaces.size(); ++wi)
            {
                const auto& ws = workspaces[wi];
                const bool editing = (edit_.target == EditTarget::Workspace
                    && edit_.workspace_id == ws->id);
                const std::string display_name = editing ? edit_.buffer : ws->name;
                const std::string label = tab_label(wi, display_name);
                int label_cols = label_display_columns(label);
                if (editing)
                {
                    const int prefix_cols = static_cast<int>(std::to_string(wi + 1).size()) + 2; // "N: "
                    label_cols = std::max(label_cols, prefix_cols + kEditMinNameCols);
                }
                const int total_cols = label_cols + kTabPadCols * 2;
                if (tabs_end_col > 0 && col_cursor + total_cols > tabs_end_col)
                    break;

                TabLayout tl;
                tl.col_begin = col_cursor;
                tl.col_end = col_cursor + total_cols;
                tl.text_col = col_cursor + kTabPadCols;
                tl.text_len = label_cols;
                tl.active = (ws->id == active_ws_id);
                tl.editing = editing;
                tl.label = label;
                tabs.push_back(std::move(tl));

                col_cursor += total_cols;
            }
        }
    }

    // Build NanoVG pill rects from the column-based layout.
    struct TabRect
    {
        float x, y, w, h;
        float accent_w; // width of the burgundy accent region (number portion)
        bool active;
        bool editing;
    };
    std::vector<TabRect> tab_rects;
    struct CaretRect
    {
        float x, y, h;
    };
    std::optional<CaretRect> caret_rect;
    if (!tabs.empty() && cw_shared > 0)
    {
        const float pill_h = static_cast<float>(ch_shared) - 4.0f; // 2px margin top+bottom
        const float pill_y = 2.0f;
        const float half_gap = static_cast<float>(cw_shared) * 0.25f; // 0.5 col gap between tabs
        for (size_t i = 0; i < tabs.size(); ++i)
        {
            const auto& tl = tabs[i];
            const bool editing = (edit_.target == EditTarget::Workspace
                && workspaces[i]->id == edit_.workspace_id);
            // Derive pixel position from grid column: pixel_x = col * cw + padding
            const float px = static_cast<float>(tl.col_begin * cw_shared + kGridPadding) + half_gap;
            const float pw = static_cast<float>((tl.col_end - tl.col_begin) * cw_shared) - half_gap * 2.0f;
            // Accent covers left padding + number + ": " (e.g. "1: " = kTabPadCols + digits + 1 cols).
            // The colon is included; the space after it is not.
            const int num_str_len = static_cast<int>(std::to_string(i + 1).size()); // digits
            const int accent_cols = kTabPadCols + num_str_len + 1; // pad + digits + ":"
            const float aw = static_cast<float>(accent_cols * cw_shared);
            tab_rects.push_back({ px, pill_y, pw, pill_h, aw, tl.active, editing });

            if (editing)
            {
                // Caret sits after the "N: " prefix + cursor codepoints.
                const int caret_cols = columns_to_offset(edit_.buffer, edit_.cursor);
                const int caret_col = tl.text_col + (num_str_len + 2) + caret_cols; // +2 for ": "
                CaretRect cr;
                cr.x = static_cast<float>(caret_col * cw_shared + kGridPadding);
                cr.y = pill_y + 2.0f;
                cr.h = pill_h - 4.0f;
                caret_rect = cr;
            }
        }
    }

    struct RightPillRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        Color bg{};
        Color accent_bg{};
        float accent_w = 0.0f;
        bool flat_right_edge = false;
    };
    std::vector<RightPillRect> right_pill_rects;
    if (!right_pills.empty() && cw_shared > 0 && ch_shared > 0)
    {
        right_pill_rects.reserve(right_pills.size());
        for (const auto& pill : right_pills)
        {
            const int total_cols = std::max(1, pill.col_end - pill.col_begin);
            const float pill_h = static_cast<float>(ch_shared) - 4.0f;
            const float pill_y = 2.0f;
            const float half_gap = static_cast<float>(cw_shared) * 0.25f;
            // Mirror the workspace-tab inset so the shared top-bar grid lands
            // pill text at the same visual offset from the background.
            const float pill_x = std::max(
                0.0f, static_cast<float>(pill.col_begin * cw_shared + kGridPadding) + half_gap);
            const float pill_w = pill.flat_right_edge && pill.col_end >= bar_w / cw_shared
                ? static_cast<float>(bar_w) - pill_x
                : static_cast<float>(total_cols * cw_shared) - half_gap * 2.0f;
            const float accent_w = pill.accent_cols > 0
                ? static_cast<float>(pill.accent_cols * cw_shared)
                : 0.0f;
            right_pill_rects.push_back(RightPillRect{
                pill_x, pill_y, pill_w, pill_h, pill.bg, pill.accent_bg, accent_w, pill.flat_right_edge });
        }
    }

    // Divider geometry.
    struct Divider
    {
        float x, y, w, h;
        SplitDirection dir;
    };
    std::vector<Divider> dividers;
    struct FocusRect
    {
        float x, y, w, h;
    };
    std::optional<FocusRect> focus_rect;

    if (show_dividers)
    {
        const auto& tree = active_tree();
        tree.for_each_divider([&](const SplitTree::DividerRect& r) {
            dividers.push_back({ static_cast<float>(r.x), static_cast<float>(r.y),
                static_cast<float>(r.w), static_cast<float>(r.h), r.direction });
        });

        const LeafId focused = tree.focused();
        if (focused != kInvalidLeaf)
        {
            const auto desc = tree.descriptor_for(focused);
            if (desc.pixel_size.x > 0 && desc.pixel_size.y > 0)
            {
                focus_rect = FocusRect{
                    static_cast<float>(desc.pixel_pos.x),
                    static_cast<float>(desc.pixel_pos.y),
                    static_cast<float>(desc.pixel_size.x),
                    static_cast<float>(desc.pixel_size.y)
                };
            }
        }
    }

    // Pane status pills: one rounded rect per pane, right-aligned in the
    // reserved bottom strip and styled to match the workspace tab pills.
    struct StatusPillRect
    {
        float x, y, w, h;
        float accent_w; // burgundy "number" portion (only drawn when focused)
        bool focused;
        bool editing;
    };
    // Cache reset: refilled below so InputDispatcher can hit-test pills.
    pane_pill_hits_.clear();
    std::optional<CaretRect> pane_caret_rect;
    std::vector<StatusPillRect> status_pill_rects;
    if (status_strip_h > 0 && deps_.grid_renderer)
    {
        const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
        if (cw > 0 && ch > 0)
        {
            status_pill_rects.reserve(status_entries.size());
            pane_pill_hits_.reserve(status_entries.size());
            for (const auto& e : status_entries)
            {
                // Substitute the live edit buffer when this pane is being
                // renamed; otherwise use the entry's status text.
                const bool editing = (edit_.target == EditTarget::Pane
                    && edit_.leaf_id == e.leaf);
                const std::string display_text = editing ? edit_.buffer : e.text;
                // Label = "<index>: <text>" — same prefix scheme as tabs.
                // Shared layout with update_pane_status_grids(): clamps to the
                // pane and degrades in narrow panes (truncated text →
                // number-only → hidden) so pill and text always agree.
                const std::string num_str = std::to_string(e.index);
                const PaneStatusPillLayout pill_layout = pane_status_pill_layout(e.pane_w, cw,
                    static_cast<int>(num_str.size()), label_display_columns(display_text),
                    editing);
                if (!pill_layout.visible)
                    continue;
                const int total_cols = pill_layout.pill_cols;

                // Right-align: pill ends one cell from the pane's right edge.
                // Mirror the workspace tab pill math: pill is inset by half_gap
                // on each side of the column span so the visual padding around
                // the text is symmetric (~0.75 cell on each side).
                const float half_gap = static_cast<float>(cw) * 0.25f;
                const float pill_w = static_cast<float>(total_cols * cw) - half_gap * 2.0f;
                const float pill_h = static_cast<float>(ch) - 4.0f; // 2px margin top/bottom
                const float right_edge = static_cast<float>(e.pane_x + e.pane_w
                    - kPaneStatusRightMarginCols * cw);
                const float pill_x = right_edge - pill_w;
                const float strip_top = static_cast<float>(e.pane_y + e.pane_h - status_strip_h);
                const float pill_y = strip_top + 2.0f;

                // Match the workspace-tab accent width: left padding + digits + ":".
                // The trailing space after the colon sits outside the accent.
                // A number-only pill is all accent.
                const int accent_cols = kTabPadCols + static_cast<int>(num_str.size()) + 1;
                const float accent_w = pill_layout.number_only
                    ? pill_w
                    : static_cast<float>(accent_cols * cw);

                StatusPillRect r;
                r.x = pill_x;
                r.y = pill_y;
                r.w = pill_w;
                r.h = pill_h;
                r.accent_w = accent_w;
                r.focused = e.focused;
                r.editing = editing;
                status_pill_rects.push_back(r);

                PaneStatusPillHit hit;
                hit.leaf = e.leaf;
                hit.x = pill_x;
                hit.y = pill_y;
                hit.w = pill_w;
                hit.h = pill_h;
                pane_pill_hits_.push_back(hit);

                if (editing && !pill_layout.number_only)
                {
                    // Caret position: prefix is "<num>: ", text starts after
                    // it. Convert byte cursor → display cols, advance from
                    // the text origin inside the pill. Clamp inside the pill
                    // for buffers longer than the granted text width.
                    const int prefix_cols = static_cast<int>(num_str.size()) + 2; // "N:" + space
                    const int caret_cols = columns_to_offset(edit_.buffer, edit_.cursor);
                    // Pill text origin (column 0 of the pill grid) sits at
                    // pill_x + half_gap; tab padding is handled by the grid.
                    const float text_origin_x = pill_x + half_gap
                        + static_cast<float>((kTabPadCols + prefix_cols) * cw);
                    CaretRect cr;
                    cr.x = std::min(text_origin_x + static_cast<float>(caret_cols * cw),
                        pill_x + pill_w - 3.0f);
                    cr.y = pill_y + 2.0f;
                    cr.h = pill_h - 4.0f;
                    pane_caret_rect = cr;
                }
            }
        }
    }

    // Single NanoVG callback draws everything: tab bar shapes + dividers + focus.
    const float focus_border = deps_.config ? deps_.config->focus_border_width : 3.0f;
    const auto edit_started_at = edit_.started_at;
    nanovg_pass_->set_draw_callback(
        [tab_rects = std::move(tab_rects), bar_w, bar_h,
            right_pill_rects = std::move(right_pill_rects),
            dividers = std::move(dividers), focus_rect, focus_border,
            status_pill_rects = std::move(status_pill_rects),
            caret_rect, pane_caret_rect, edit_started_at,
            chrome_theme](NVGcontext* vg, int /*w*/, int /*h*/) {
            // --- Pane status pills (drawn first so dividers/focus border go on top) ---
            for (const auto& s : status_pill_rects)
            {
                const float radius = s.h * 0.5f; // pill shape

                // Pill body — brightened when editing, otherwise the same
                // inactive grey background as workspace tabs.
                nvgBeginPath(vg);
                nvgRoundedRect(vg, s.x, s.y, s.w, s.h, radius);
                if (s.editing)
                    nvgFillColor(vg, nvgRGBAf(chrome_theme.status_editing_bg.r, chrome_theme.status_editing_bg.g, chrome_theme.status_editing_bg.b, chrome_theme.status_editing_bg.a));
                else
                    nvgFillColor(vg, nvgRGBAf(chrome_theme.status_bar_bg.r, chrome_theme.status_bar_bg.g, chrome_theme.status_bar_bg.b, chrome_theme.status_bar_bg.a));
                nvgFill(vg);

                // Number-prefix accent — bright green when focused, dimmed
                // green-grey otherwise. Distinguishes pane pills from the
                // burgundy-accented workspace tab pills.
                nvgSave(vg);
                nvgIntersectScissor(vg, s.x, s.y, s.w, s.h);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, s.x, s.y, s.accent_w, s.h, radius);
                if (s.focused)
                    nvgFillColor(vg, nvgRGBAf(chrome_theme.status_focused_accent_bg.r, chrome_theme.status_focused_accent_bg.g, chrome_theme.status_focused_accent_bg.b, chrome_theme.status_focused_accent_bg.a));
                else
                    nvgFillColor(vg, nvgRGBAf(chrome_theme.status_inactive_accent_bg.r, chrome_theme.status_inactive_accent_bg.g, chrome_theme.status_inactive_accent_bg.b, chrome_theme.status_inactive_accent_bg.a));
                nvgFill(vg);
                nvgRestore(vg);

                if (s.editing)
                {
                    // White outline so the rename target is unmistakable.
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, s.x, s.y, s.w, s.h, radius);
                    nvgStrokeColor(vg, nvgRGBAf(chrome_theme.editing_outline.r, chrome_theme.editing_outline.g, chrome_theme.editing_outline.b, chrome_theme.editing_outline.a));
                    nvgStrokeWidth(vg, 1.5f);
                    nvgStroke(vg);
                }
            }

            // --- Pane rename caret (blink at ~2 Hz) ---
            if (pane_caret_rect)
            {
                const auto now = std::chrono::steady_clock::now();
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - edit_started_at)
                                    .count();
                if ((ms / 500) % 2 == 0)
                {
                    nvgBeginPath(vg);
                    nvgRect(vg, pane_caret_rect->x, pane_caret_rect->y, 1.5f, pane_caret_rect->h);
                    nvgFillColor(vg, nvgRGBAf(chrome_theme.editing_outline.r, chrome_theme.editing_outline.g, chrome_theme.editing_outline.b, chrome_theme.editing_outline.a));
                    nvgFill(vg);
                }
            }

            // --- Tab bar ---
            if (bar_h > 0)
            {
                // Bar background
                nvgBeginPath(vg);
                nvgRect(vg, 0, 0, static_cast<float>(bar_w), static_cast<float>(bar_h));
                nvgFillColor(vg, nvgRGBAf(chrome_theme.tab_bar_bg.r, chrome_theme.tab_bar_bg.g, chrome_theme.tab_bar_bg.b, chrome_theme.tab_bar_bg.a));
                nvgFill(vg);
            }

            for (const auto& tab : tab_rects)
            {
                const float radius = tab.h * 0.5f; // pill shape

                // Full pill body. Editing tabs use a brighter Surface2-ish
                // shade so the field reads as "live" while the active red
                // accent on the left stays untouched.
                nvgBeginPath(vg);
                nvgRoundedRect(vg, tab.x, tab.y, tab.w, tab.h, radius);
                if (tab.editing)
                    nvgFillColor(vg, nvgRGBAf(chrome_theme.tab_editing_bg.r, chrome_theme.tab_editing_bg.g, chrome_theme.tab_editing_bg.b, chrome_theme.tab_editing_bg.a));
                else
                    nvgFillColor(vg, nvgRGBAf(chrome_theme.tab_inactive_bg.r, chrome_theme.tab_inactive_bg.g, chrome_theme.tab_inactive_bg.b, chrome_theme.tab_inactive_bg.a));
                nvgFill(vg);

                // Number-prefix accent.
                nvgSave(vg);
                nvgIntersectScissor(vg, tab.x, tab.y, tab.w, tab.h);
                nvgBeginPath(vg);
                nvgRoundedRect(vg, tab.x, tab.y, tab.accent_w, tab.h, radius);
                if (tab.active)
                    nvgFillColor(vg, nvgRGBAf(chrome_theme.tab_active_bg.r, chrome_theme.tab_active_bg.g, chrome_theme.tab_active_bg.b, chrome_theme.tab_active_bg.a));
                else
                    nvgFillColor(vg, nvgRGBAf(chrome_theme.tab_inactive_bg.r, chrome_theme.tab_inactive_bg.g, chrome_theme.tab_inactive_bg.b, chrome_theme.tab_inactive_bg.a));
                nvgFill(vg);
                nvgRestore(vg);

                if (tab.editing)
                {
                    // Editing outline: 1.5px stroke around the pill so the
                    // user immediately reads the tab as "in edit mode".
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, tab.x, tab.y, tab.w, tab.h, radius);
                    nvgStrokeColor(vg, nvgRGBAf(chrome_theme.editing_outline.r, chrome_theme.editing_outline.g, chrome_theme.editing_outline.b, chrome_theme.editing_outline.a));
                    nvgStrokeWidth(vg, 1.5f);
                    nvgStroke(vg);
                }
            }

            for (const auto& pill : right_pill_rects)
            {
                const float radius = pill.h * 0.5f;
                nvgBeginPath(vg);
                if (pill.flat_right_edge)
                {
                    nvgRoundedRectVarying(
                        vg, pill.x, pill.y, pill.w, pill.h, radius, 0.0f, 0.0f, radius);
                }
                else
                {
                    nvgRoundedRect(vg, pill.x, pill.y, pill.w, pill.h, radius);
                }
                nvgFillColor(vg, nvgRGBAf(pill.bg.r, pill.bg.g, pill.bg.b, pill.bg.a));
                nvgFill(vg);
            }

            // Rename caret — blink at ~2 Hz.
            if (caret_rect)
            {
                const auto now = std::chrono::steady_clock::now();
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - edit_started_at)
                                    .count();
                if ((ms / 500) % 2 == 0)
                {
                    nvgBeginPath(vg);
                    nvgRect(vg, caret_rect->x, caret_rect->y, 1.5f, caret_rect->h);
                    nvgFillColor(vg, nvgRGBAf(chrome_theme.editing_outline.r, chrome_theme.editing_outline.g, chrome_theme.editing_outline.b, chrome_theme.editing_outline.a));
                    nvgFill(vg);
                }
            }

            // --- Divider lines ---
            for (const auto& d : dividers)
            {
                nvgBeginPath(vg);
                if (d.dir == SplitDirection::Vertical)
                {
                    float cx = d.x + d.w * 0.5f;
                    nvgMoveTo(vg, cx, d.y);
                    nvgLineTo(vg, cx, d.y + d.h);
                }
                else
                {
                    float cy = d.y + d.h * 0.5f;
                    nvgMoveTo(vg, d.x, cy);
                    nvgLineTo(vg, d.x + d.w, cy);
                }
                nvgStrokeColor(vg, nvgRGBAf(chrome_theme.divider.r, chrome_theme.divider.g, chrome_theme.divider.b, chrome_theme.divider.a));
                nvgStrokeWidth(vg, 1.0f);
                nvgStroke(vg);
            }

            // --- Focus indicator ---
            if (focus_rect)
            {
                const float border = focus_border;
                const float half = border * 0.5f;
                constexpr float div_half = static_cast<float>(SplitTree::kDividerWidth) * 0.5f;

                const float pane_right = focus_rect->x + focus_rect->w;
                const float pane_bottom = focus_rect->y + focus_rect->h;

                bool has_right_divider = false;
                bool has_bottom_divider = false;
                for (const auto& d : dividers)
                {
                    if (d.dir == SplitDirection::Vertical
                        && std::abs(d.x - pane_right) < 1.0f)
                        has_right_divider = true;
                    if (d.dir == SplitDirection::Horizontal
                        && std::abs(d.y - pane_bottom) < 1.0f)
                        has_bottom_divider = true;
                }
                const float right = has_right_divider ? (pane_right + div_half) : (pane_right - half);
                const float bottom = has_bottom_divider ? (pane_bottom + div_half) : (pane_bottom - half);

                nvgStrokeColor(vg, nvgRGBAf(chrome_theme.focus_border.r, chrome_theme.focus_border.g, chrome_theme.focus_border.b, chrome_theme.focus_border.a));
                nvgStrokeWidth(vg, border);

                nvgBeginPath(vg);
                nvgMoveTo(vg, right, focus_rect->y);
                nvgLineTo(vg, right, bottom);
                nvgStroke(vg);

                nvgBeginPath(vg);
                nvgMoveTo(vg, focus_rect->x, bottom);
                nvgLineTo(vg, right, bottom);
                nvgStroke(vg);
            }
        });

    // NanoVG pass covers the full viewport (tab bar + content area).
    RenderViewport vp;
    vp.width = viewport_.pixel_size.x;
    vp.height = viewport_.pixel_size.y;
    frame.record_render_pass(*nanovg_pass_, vp);

    // Grid handle draws top-bar label text on top of NanoVG shapes.
    if (bar_h > 0 && (!tabs.empty() || !right_pills.empty()))
    {
        update_tab_grid(tabs, right_pills);
        if (tab_handle_)
            frame.draw_grid_handle(*tab_handle_);
    }

    // Per-pane status bar text (WI 78).
    update_pane_status_grids(frame, status_entries);

    frame.flush_submit_chunk();
}

void ChromeHost::update_pane_status_grids(IFrameContext& frame, std::span<const PaneStatusEntry> entries)
{
    if (!deps_.grid_renderer || !deps_.text_service)
        return;

    const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
    if (cw <= 0 || ch <= 0)
    {
        pane_status_handles_.clear();
        return;
    }

    // Prune handles for leaves that are no longer present.
    std::unordered_set<LeafId> live_ids;
    live_ids.reserve(entries.size());
    for (const auto& e : entries)
        live_ids.insert(e.leaf);
    for (auto it = pane_status_handles_.begin(); it != pane_status_handles_.end();)
    {
        if (!live_ids.count(it->first))
            it = pane_status_handles_.erase(it);
        else
            ++it;
    }

    if (entries.empty())
        return;

    // Pick fg from the underlying NanoVG fill color via luminance contrast.
    // Body matches the workspace-tab inactive bg; the number-prefix accent
    // is green when focused or dimmed grey otherwise (mirrors draw()).
    const ChromeTheme chrome_theme = theme();
    const Color body_fg_normal = chrome_theme.status_bar_fg;
    const Color body_fg_editing = text_for_bg(chrome_theme.status_editing_bg);
    const Color focused_accent_fg = text_for_bg(chrome_theme.status_focused_accent_bg);
    const Color inactive_accent_fg = text_for_bg(chrome_theme.status_inactive_accent_bg);

    for (const auto& e : entries)
    {
        const bool editing = (edit_.target == EditTarget::Pane && edit_.leaf_id == e.leaf);
        const std::string display_text = editing ? edit_.buffer : e.text;
        const Color body_fg = editing ? body_fg_editing : body_fg_normal;

        // Shared layout with the draw() pass — the NanoVG pill rect and this
        // grid text can never disagree, and both degrade identically in
        // narrow panes.
        const std::string num_str = std::to_string(e.index);
        const int text_display_cols = label_display_columns(display_text);
        const PaneStatusPillLayout pill_layout = pane_status_pill_layout(e.pane_w, cw,
            static_cast<int>(num_str.size()), text_display_cols, editing);
        if (!pill_layout.visible)
        {
            // Too narrow for even the number pill: hide it entirely.
            pane_status_handles_.erase(e.leaf);
            continue;
        }
        const int pill_cols = pill_layout.pill_cols;

        std::string label;
        if (pill_layout.number_only)
        {
            label = num_str + ":";
        }
        else if (pill_layout.text_cols < text_display_cols)
        {
            // Truncate the text portion on UTF-8 cluster boundaries (never
            // mid-sequence) with an ellipsis inside the granted width.
            label = num_str + ": "
                + truncate_to_columns(display_text, pill_layout.text_cols, /*add_ellipsis=*/true);
        }
        else
        {
            label = num_str + ": " + display_text;
        }

        // Pill placement (must mirror the StatusPillRect math in draw()):
        // total column span minus the half_gap inset on each side.
        const int half_gap = cw / 4;
        const int pill_w_px = pill_cols * cw - half_gap * 2;
        const int strip_y = e.pane_y + e.pane_h - ch;
        const int right_edge = e.pane_x + e.pane_w - kPaneStatusRightMarginCols * cw;
        const int pill_x_px = right_edge - pill_w_px;

        auto& handle = pane_status_handles_[e.leaf];
        if (!handle)
        {
            handle = deps_.grid_renderer->create_grid_handle();
            if (!handle)
            {
                DRAXUL_LOG_ERROR(LogCategory::App, "ChromeHost: pane status create_grid_handle() returned null");
                continue;
            }
            handle->set_default_background({ 0, 0, 0, 0 });
            handle->set_cursor(-1, -1, CursorStyle{});
            handle->set_cursor_visible(false);
        }

        // Viewport: shift the grid up by the renderer's internal padding so the
        // single row centers vertically (same trick as update_tab_grid), AND
        // shift it LEFT by (padding + half_gap) so column 1 (the digit) lands
        // ~0.75 cells inside the pill — matching how update_tab_grid positions
        // the digit inside its tab pills.
        const int padding = deps_.grid_renderer->padding();
        PaneDescriptor desc;
        desc.pixel_pos = { pill_x_px - padding - half_gap, strip_y - padding };
        desc.pixel_size = { pill_w_px + padding * 2, ch + padding };
        handle->set_viewport(desc);
        handle->set_grid_size(pill_cols, 1);

        // Warm any uncached glyphs before updating the handle so newly typed
        // chord/pane-status characters do not appear a frame late. Iterate
        // UTF-8 clusters (not bytes!) so multi-byte codepoints resolve as one
        // glyph and not a stream of garbage stub bytes.
        const auto label_clusters = split_display_clusters(label);
        for (const auto& cluster_text : label_clusters)
            deps_.text_service->resolve_cluster(cluster_text);

        // Cells start fully transparent — the NanoVG pill provides the bg.
        const Color transparent{ 0.0f, 0.0f, 0.0f, 0.0f };
        std::vector<CellUpdate> cells;
        cells.reserve(static_cast<size_t>(pill_cols));
        for (int c = 0; c < pill_cols; ++c)
        {
            CellUpdate cell{};
            cell.col = c;
            cell.row = 0;
            cell.bg = transparent;
            cell.fg = transparent;
            cells.push_back(cell);
        }

        // Pane-status text sits one cell tighter than workspace tabs so the
        // number aligns with the accent block instead of leaving a blank
        // leading cell inside the pill. Walk by UTF-8 cluster (not byte!) and
        // advance `col` by each cluster's display width so non-ASCII labels
        // land in the correct column.
        const int num_prefix_len = static_cast<int>(num_str.size()) + 1; // digits + ":"
        int col = 0;
        for (const auto& cluster_text : label_clusters)
        {
            if (col >= pill_cols)
                break;
            const int cluster_w = std::max(1, cluster_cell_width(cluster_text));
            const Color& accent_fg = e.focused ? focused_accent_fg : inactive_accent_fg;
            const Color& fg = (col < num_prefix_len) ? accent_fg : body_fg;
            AtlasRegion glyph = deps_.text_service->resolve_cluster(cluster_text);
            auto& cell = cells[static_cast<size_t>(col)];
            cell.fg = fg;
            cell.glyph = glyph;
            if (glyph.is_color)
                cell.style_flags |= STYLE_FLAG_COLOR_GLYPH;
            col += cluster_w;
        }

        handle->update_cells(cells);
    }

    for (const auto& e : entries)
    {
        auto it = pane_status_handles_.find(e.leaf);
        if (it != pane_status_handles_.end() && it->second)
            frame.draw_grid_handle(*it->second);
    }
}

void ChromeHost::update_tab_grid(std::span<const TabLayout> tabs, std::span<const RightPillLayout> right_pills)
{
    if (!deps_.grid_renderer || !deps_.text_service)
        return;

    const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
    if (cw <= 0 || ch <= 0)
        return;

    const int bar_w = viewport_.pixel_size.x;
    const int grid_h = ch; // grid covers cell row, accent line below
    const int grid_cols = bar_w / cw;
    const int grid_rows = 1;

    if (grid_cols <= 0)
        return;
    const ChromeTheme chrome_theme = theme();

    if (!tab_handle_)
    {
        tab_handle_ = deps_.grid_renderer->create_grid_handle();
        if (!tab_handle_)
        {
            DRAXUL_LOG_ERROR(LogCategory::App, "ChromeHost: create_grid_handle() returned null");
            return;
        }
        tab_handle_->set_default_background({ 0, 0, 0, 0 });
    }

    PaneDescriptor desc;
    desc.pixel_pos = { 0, -kGridPadding }; // shift grid up so text centers in pills
    desc.pixel_size = { bar_w, grid_h + kGridPadding };
    tab_handle_->set_viewport(desc);
    tab_handle_->set_grid_size(grid_cols, grid_rows);
    tab_handle_->set_cursor(-1, -1, CursorStyle{});
    tab_handle_->set_cursor_visible(false);

    // Transparent cells — NanoVG pill shapes provide the tab backgrounds.
    // Grid cells only carry foreground text glyphs.
    std::vector<CellUpdate> cells;
    const Color transparent{ 0.0f, 0.0f, 0.0f, 0.0f };

    for (int c = 0; c < grid_cols; ++c)
    {
        CellUpdate cell{};
        cell.col = c;
        cell.row = 0;
        cell.bg = transparent;
        cell.fg = transparent;
        cells.push_back(cell);
    }

    // Warm any uncached glyphs before the grid handle consumes them so the
    // chord pill does not reveal late-arriving characters over multiple frames.
    for (const auto& tl : tabs)
    {
        for (const auto& cluster_text : split_display_clusters(tl.label))
            deps_.text_service->resolve_cluster(cluster_text);
    }
    for (const auto& pill : right_pills)
    {
        for (const auto& cluster : pill.clusters)
            deps_.text_service->resolve_cluster(cluster.text);
    }

    // Write text into grid cells using the shared column-based layout.
    // Foreground colors are picked from the underlying NanoVG fill via
    // luminance contrast so any future bg tweak automatically gets readable
    // ink without re-tuning a constant.
    for (size_t ti = 0; ti < tabs.size(); ++ti)
    {
        const auto& tl = tabs[ti];
        // Number prefix width: digits of (index+1) + ":" (the space after is name territory).
        const int num_prefix_cols = static_cast<int>(std::to_string(ti + 1).size()) + 1; // digits + ":"
        const Color accent_bg = tl.active ? chrome_theme.tab_active_bg : chrome_theme.tab_inactive_bg;
        const Color body_fg = tl.editing
            ? text_for_bg(chrome_theme.tab_editing_bg)
            : (tl.active ? chrome_theme.tab_active_fg : chrome_theme.tab_inactive_fg);
        const Color accent_fg = text_for_bg(accent_bg);
        int label_col = tl.text_col;
        int consumed_cols = 0;
        for (const auto& cluster_text : split_display_clusters(tl.label))
        {
            const int col = label_col;
            if (col < 0 || col >= grid_cols)
            {
                label_col += std::max(1, cluster_cell_width(cluster_text));
                consumed_cols += std::max(1, cluster_cell_width(cluster_text));
                continue;
            }

            const int cluster_width = std::max(1, cluster_cell_width(cluster_text));
            const Color& fg = (consumed_cols < num_prefix_cols) ? accent_fg : body_fg;
            AtlasRegion glyph = deps_.text_service->resolve_cluster(cluster_text);

            auto& cell = cells[static_cast<size_t>(col)];
            cell.fg = fg;
            cell.glyph = glyph;
            if (glyph.is_color)
                cell.style_flags |= STYLE_FLAG_COLOR_GLYPH;
            label_col += cluster_width;
            consumed_cols += cluster_width;
        }
    }

    for (const auto& pill : right_pills)
    {
        int col = pill.text_col;
        for (const auto& cluster : pill.clusters)
        {
            if (col < 0 || col >= grid_cols)
            {
                col += std::max(1, cluster.width);
                continue;
            }

            AtlasRegion glyph = deps_.text_service->resolve_cluster(cluster.text);
            auto& cell = cells[static_cast<size_t>(col)];
            cell.fg = cluster.fg;
            cell.glyph = glyph;
            if (glyph.is_color)
                cell.style_flags |= STYLE_FLAG_COLOR_GLYPH;
            col += std::max(1, cluster.width);
        }
    }

    tab_handle_->update_cells(cells);
}

// ---------------------------------------------------------------------------
// Inline tab/pane rename (WI 128)
// ---------------------------------------------------------------------------

bool ChromeHost::is_editing_tab() const
{
    return edit_.target == EditTarget::Workspace;
}

int ChromeHost::editing_workspace_id() const
{
    return is_editing_tab() ? edit_.workspace_id : -1;
}

bool ChromeHost::is_editing_pane() const
{
    return edit_.target == EditTarget::Pane;
}

LeafId ChromeHost::editing_leaf_id() const
{
    return is_editing_pane() ? edit_.leaf_id : kInvalidLeaf;
}

bool ChromeHost::is_editing() const
{
    return edit_.target != EditTarget::None;
}

LeafId ChromeHost::hit_test_pane_status_pill(int px, int py) const
{
    const float fx = static_cast<float>(px);
    const float fy = static_cast<float>(py);
    for (const auto& hit : pane_pill_hits_)
    {
        if (fx >= hit.x && fx < hit.x + hit.w && fy >= hit.y && fy < hit.y + hit.h)
            return hit.leaf;
    }
    return kInvalidLeaf;
}

int ChromeHost::workspace_id_for_index(int tab_index) const
{
    if (!deps_.workspaces || tab_index <= 0)
        return -1;
    const auto& ws = *deps_.workspaces;
    if (static_cast<size_t>(tab_index) > ws.size())
        return -1;
    return ws[static_cast<size_t>(tab_index - 1)]->id;
}

void ChromeHost::begin_tab_rename(int tab_index)
{
    const int id = workspace_id_for_index(tab_index);
    if (id < 0)
        return;
    begin_tab_rename_by_id(id);
}

void ChromeHost::begin_tab_rename_by_id(int workspace_id)
{
    if (!deps_.workspaces)
        return;
    // Commit any in-progress edit (tab or pane) before starting a new one,
    // unless we're already editing this exact workspace tab.
    if (edit_.target != EditTarget::None
        && !(edit_.target == EditTarget::Workspace && edit_.workspace_id == workspace_id))
    {
        commit_tab_rename();
    }

    for (const auto& ws : *deps_.workspaces)
    {
        if (ws->id != workspace_id)
            continue;
        edit_ = EditState{};
        edit_.target = EditTarget::Workspace;
        edit_.workspace_id = workspace_id;
        edit_.buffer = ws->name;
        edit_.cursor = edit_.buffer.size();
        edit_.started_at = std::chrono::steady_clock::now();
        if (deps_.request_frame)
            deps_.request_frame();
        return;
    }
}

void ChromeHost::begin_pane_rename(LeafId leaf)
{
    if (leaf == kInvalidLeaf)
        return;
    // Commit any in-progress edit before starting a new one, unless we're
    // already editing this exact pane.
    if (edit_.target != EditTarget::None
        && !(edit_.target == EditTarget::Pane && edit_.leaf_id == leaf))
    {
        commit_tab_rename();
    }

    // Seed buffer from existing user override; empty buffer means "no
    // override yet" — the placeholder appears on first key press.
    std::string seed;
    if (deps_.get_pane_name)
        seed = deps_.get_pane_name(leaf);

    edit_ = EditState{};
    edit_.target = EditTarget::Pane;
    edit_.leaf_id = leaf;
    edit_.buffer = std::move(seed);
    edit_.cursor = edit_.buffer.size();
    edit_.started_at = std::chrono::steady_clock::now();
    if (deps_.request_frame)
        deps_.request_frame();
}

void ChromeHost::commit_tab_rename()
{
    if (edit_.target == EditTarget::None)
        return;
    if (edit_.target == EditTarget::Workspace)
    {
        if (!edit_.buffer.empty() && deps_.set_workspace_name)
            deps_.set_workspace_name(edit_.workspace_id, edit_.buffer);
    }
    else if (edit_.target == EditTarget::Pane)
    {
        // Empty buffer is allowed for panes — it clears the user override
        // and reverts to host->status_text(). For workspaces we keep the
        // existing name on empty commit; the pane semantics are different
        // because the underlying status_text is always available as a
        // sensible fallback.
        if (deps_.set_pane_name)
            deps_.set_pane_name(edit_.leaf_id, edit_.buffer);
    }
    edit_ = EditState{};
    if (deps_.request_frame)
        deps_.request_frame();
}

void ChromeHost::cancel_tab_rename()
{
    if (edit_.target == EditTarget::None)
        return;
    edit_ = EditState{};
    if (deps_.request_frame)
        deps_.request_frame();
}

bool ChromeHost::on_rename_text_input(const std::string& utf8)
{
    if (edit_.target == EditTarget::None)
        return false;
    if (utf8.empty())
        return true;
    edit_.buffer.insert(edit_.cursor, utf8);
    edit_.cursor += utf8.size();
    edit_.started_at = std::chrono::steady_clock::now(); // reset blink

    // Warm any newly typed glyphs and queue the atlas-region upload BEFORE the
    // next frame begins. The renderer flushes pending atlas uploads inside
    // begin_frame(), so an upload queued from a draw pass is one frame late
    // (cells reference atlas coords whose pixels arrive on the next frame).
    // Doing the warm here, between frames, lets the next begin_frame() pick
    // up the upload before any chrome draw runs.
    if (deps_.text_service)
    {
        for (const auto& cluster : split_display_clusters(utf8))
            deps_.text_service->resolve_cluster(cluster);
    }

    if (deps_.request_frame)
        deps_.request_frame();
    return true;
}

bool ChromeHost::on_rename_key(int sdl_keycode)
{
    if (edit_.target == EditTarget::None)
        return false;

    switch (sdl_keycode)
    {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        commit_tab_rename();
        return true;
    case SDLK_ESCAPE:
        cancel_tab_rename();
        return true;
    case SDLK_BACKSPACE:
        if (edit_.cursor > 0)
        {
            const size_t prev = utf8_prev(edit_.buffer, edit_.cursor);
            edit_.buffer.erase(prev, edit_.cursor - prev);
            edit_.cursor = prev;
            edit_.started_at = std::chrono::steady_clock::now();
            if (deps_.request_frame)
                deps_.request_frame();
        }
        return true;
    case SDLK_DELETE:
        if (edit_.cursor < edit_.buffer.size())
        {
            const size_t next = utf8_next(edit_.buffer, edit_.cursor);
            edit_.buffer.erase(edit_.cursor, next - edit_.cursor);
            edit_.started_at = std::chrono::steady_clock::now();
            if (deps_.request_frame)
                deps_.request_frame();
        }
        return true;
    case SDLK_LEFT:
        edit_.cursor = utf8_prev(edit_.buffer, edit_.cursor);
        edit_.started_at = std::chrono::steady_clock::now();
        if (deps_.request_frame)
            deps_.request_frame();
        return true;
    case SDLK_RIGHT:
        edit_.cursor = utf8_next(edit_.buffer, edit_.cursor);
        edit_.started_at = std::chrono::steady_clock::now();
        if (deps_.request_frame)
            deps_.request_frame();
        return true;
    case SDLK_HOME:
        edit_.cursor = 0;
        edit_.started_at = std::chrono::steady_clock::now();
        if (deps_.request_frame)
            deps_.request_frame();
        return true;
    case SDLK_END:
        edit_.cursor = edit_.buffer.size();
        edit_.started_at = std::chrono::steady_clock::now();
        if (deps_.request_frame)
            deps_.request_frame();
        return true;
    default:
        // Swallow all keys while editing so they cannot leak to the host
        // beneath. Modifier-only keys (Shift/Ctrl/etc.) are also consumed.
        return true;
    }
}

std::optional<std::chrono::steady_clock::time_point> ChromeHost::next_deadline() const
{
    if (edit_.target == EditTarget::None)
        return std::nullopt;
    // Drive caret blink at ~2 Hz while editing.
    return std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
}

} // namespace draxul
