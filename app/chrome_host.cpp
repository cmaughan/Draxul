#include "chrome_host.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <draxul/app_config.h>
#include <draxul/text_service.h>
#include <nanovg.h>

namespace draxul
{

ChromeHost::ChromeHost(Deps deps)
    : deps_(std::move(deps))
{
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
    for (auto& ws : workspaces_)
        ws->host_manager.shutdown();
    workspaces_.clear();
    active_workspace_ = -1;
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

bool ChromeHost::create_initial_workspace(IHostCallbacks& callbacks, int pixel_w, int pixel_h)
{
    auto ws = std::make_unique<Workspace>(next_workspace_id_++, make_host_manager_deps());
    if (!ws->host_manager.create(callbacks, pixel_w, pixel_h))
    {
        last_create_error_ = ws->host_manager.error();
        return false;
    }
    ws->initialized = true;
    if (IHost* h = ws->host_manager.host())
        ws->name = h->debug_state().name;
    else
        ws->name = "tab";
    active_workspace_ = ws->id;
    workspaces_.push_back(std::move(ws));
    return true;
}

int ChromeHost::add_workspace(IHostCallbacks& callbacks, int pixel_w, int pixel_h,
    std::optional<HostKind> host_kind)
{
    auto ws = std::make_unique<Workspace>(next_workspace_id_++, make_host_manager_deps());
    // Default to the platform shell when no explicit kind is given.
    const HostKind kind = host_kind.value_or(HostManager::platform_default_split_host_kind());
    if (!ws->host_manager.create(callbacks, pixel_w, pixel_h, kind))
    {
        last_create_error_ = ws->host_manager.error();
        return -1;
    }
    ws->initialized = true;
    if (IHost* h = ws->host_manager.host())
        ws->name = h->debug_state().name;
    else
        ws->name = "tab";
    int id = ws->id;
    workspaces_.push_back(std::move(ws));
    activate_workspace(id);
    return id;
}

bool ChromeHost::close_workspace(int workspace_id, IHostCallbacks& /*callbacks*/)
{
    if (workspaces_.size() <= 1)
        return false;

    auto it = std::find_if(workspaces_.begin(), workspaces_.end(),
        [workspace_id](const auto& ws) { return ws->id == workspace_id; });
    if (it == workspaces_.end())
        return false;

    (*it)->host_manager.shutdown();

    bool was_active = (workspace_id == active_workspace_);
    workspaces_.erase(it);

    if (was_active)
        activate_workspace(workspaces_.front()->id);

    return true;
}

void ChromeHost::activate_workspace(int workspace_id)
{
    if (workspace_id == active_workspace_)
        return;

    // Notify old workspace's focused host of focus loss.
    for (auto& ws : workspaces_)
    {
        if (ws->id == active_workspace_)
        {
            if (IHost* h = ws->host_manager.focused_host())
                h->on_focus_lost();
            break;
        }
    }

    active_workspace_ = workspace_id;

    // Notify new workspace's focused host of focus gain.
    for (auto& ws : workspaces_)
    {
        if (ws->id == active_workspace_)
        {
            if (IHost* h = ws->host_manager.focused_host())
                h->on_focus_gained();
            break;
        }
    }
}

void ChromeHost::next_workspace()
{
    if (workspaces_.size() <= 1)
        return;
    for (size_t i = 0; i < workspaces_.size(); ++i)
    {
        if (workspaces_[i]->id == active_workspace_)
        {
            size_t next = (i + 1) % workspaces_.size();
            activate_workspace(workspaces_[next]->id);
            return;
        }
    }
}

void ChromeHost::prev_workspace()
{
    if (workspaces_.size() <= 1)
        return;
    for (size_t i = 0; i < workspaces_.size(); ++i)
    {
        if (workspaces_[i]->id == active_workspace_)
        {
            size_t prev = (i == 0) ? workspaces_.size() - 1 : i - 1;
            activate_workspace(workspaces_[prev]->id);
            return;
        }
    }
}

void ChromeHost::activate_workspace_by_index(int one_based_index)
{
    const int idx = one_based_index - 1;
    if (idx < 0 || idx >= static_cast<int>(workspaces_.size()))
        return;
    activate_workspace(workspaces_[static_cast<size_t>(idx)]->id);
}

HostManager& ChromeHost::active_host_manager()
{
    for (auto& ws : workspaces_)
    {
        if (ws->id == active_workspace_)
            return ws->host_manager;
    }
    // Should never happen — caller must ensure at least one workspace exists.
    static HostManager dummy(HostManager::Deps{});
    return dummy;
}

const HostManager& ChromeHost::active_host_manager() const
{
    for (const auto& ws : workspaces_)
    {
        if (ws->id == active_workspace_)
            return ws->host_manager;
    }
    static const HostManager dummy(HostManager::Deps{});
    return dummy;
}

const SplitTree& ChromeHost::active_tree() const
{
    return active_host_manager().tree();
}

int ChromeHost::tab_bar_height() const
{
    if (workspaces_.size() <= 1)
        return 0;
    if (!deps_.grid_renderer)
        return 0;
    const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
    return ch;
}

void ChromeHost::recompute_all_viewports(int origin_x, int origin_y, int pixel_w, int pixel_h)
{
    for (auto& ws : workspaces_)
        ws->host_manager.recompute_viewports(origin_x, origin_y, pixel_w, pixel_h);
}

int ChromeHost::active_workspace_id() const
{
    return active_workspace_;
}

HostManager::Deps ChromeHost::make_host_manager_deps() const
{
    HostManager::Deps hm_deps;
    hm_deps.options = deps_.options;
    hm_deps.config = deps_.config;
    hm_deps.config_document = deps_.config_document;
    hm_deps.window = deps_.window;
    hm_deps.grid_renderer = deps_.grid_renderer;
    hm_deps.imgui_host = deps_.imgui_host;
    hm_deps.text_service = deps_.text_service;
    hm_deps.display_ppi = deps_.display_ppi;
    hm_deps.owner_lifetime = deps_.owner_lifetime;
    hm_deps.compute_viewport = deps_.compute_viewport;
    return hm_deps;
}

// ---------------------------------------------------------------------------
// Tab bar: Catppuccin Mocha palette
// ---------------------------------------------------------------------------

namespace
{
// Catppuccin Mocha palette
constexpr Color kTabBarBg{ 0.094f, 0.094f, 0.145f, 1.0f }; // #181825 Mantle
constexpr Color kActiveTabBg{ 0.796f, 0.651f, 0.969f, 1.0f }; // #cba6f7 Mauve
constexpr Color kInactiveTabBg{ 0.271f, 0.278f, 0.353f, 1.0f }; // #45475a Surface1
constexpr Color kActiveTabFg{ 0.118f, 0.118f, 0.180f, 1.0f }; // #1e1e2e Base (dark on bright)
constexpr Color kInactiveTabFg{ 0.651f, 0.678f, 0.780f, 1.0f }; // #a6adc8 Subtext0
constexpr int kTabPadCols = 1; // padding cells on each side of tab label
constexpr int kGridPadding = 4; // renderer internal cell padding

std::string tab_label(size_t index, const std::string& name)
{
    return std::to_string(index + 1) + ": " + name;
}
} // namespace

int ChromeHost::hit_test_tab(int px, int py) const
{
    if (workspaces_.size() <= 1 || !deps_.grid_renderer)
        return 0;
    const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
    if (cw <= 0 || ch <= 0 || py < 0 || py >= ch)
        return 0;

    int col_cursor = 0;
    for (size_t wi = 0; wi < workspaces_.size(); ++wi)
    {
        const std::string label = tab_label(wi, workspaces_[wi]->name);
        const int total_cols = static_cast<int>(label.size()) + kTabPadCols * 2;
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

    const bool show_tabs = workspaces_.size() > 1;
    const bool show_dividers = active_tree().leaf_count() >= 2;

    if (!show_tabs && !show_dividers)
        return;

    // --- Shared tab layout (used by both NanoVG and grid) ---
    // Grid cells are positioned at: pixel_x = col * cw + padding.
    // We compute tab spans in column space, then derive NanoVG pill pixel coords.
    std::vector<TabLayout> tabs;
    int bar_w = viewport_.pixel_size.x;
    int bar_h = 0;
    int cw_shared = 0;
    int ch_shared = 0;

    if (show_tabs && deps_.grid_renderer)
    {
        const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
        cw_shared = cw;
        ch_shared = ch;
        if (cw > 0 && ch > 0)
        {
            bar_h = tab_bar_height();
            int col_cursor = 0; // start at column 0
            for (size_t wi = 0; wi < workspaces_.size(); ++wi)
            {
                const auto& ws = workspaces_[wi];
                const std::string label = tab_label(wi, ws->name);
                const int label_cols = static_cast<int>(label.size());
                const int total_cols = label_cols + kTabPadCols * 2;

                TabLayout tl;
                tl.col_begin = col_cursor;
                tl.col_end = col_cursor + total_cols;
                tl.text_col = col_cursor + kTabPadCols;
                tl.text_len = label_cols;
                tl.active = (ws->id == active_workspace_);
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
    };
    std::vector<TabRect> tab_rects;
    if (!tabs.empty() && cw_shared > 0)
    {
        const float pill_h = static_cast<float>(ch_shared) - 4.0f; // 2px margin top+bottom
        const float pill_y = 2.0f;
        const float half_gap = static_cast<float>(cw_shared) * 0.25f; // 0.5 col gap between tabs
        for (size_t i = 0; i < tabs.size(); ++i)
        {
            const auto& tl = tabs[i];
            // Derive pixel position from grid column: pixel_x = col * cw + padding
            const float px = static_cast<float>(tl.col_begin * cw_shared + kGridPadding) + half_gap;
            const float pw = static_cast<float>((tl.col_end - tl.col_begin) * cw_shared) - half_gap * 2.0f;
            // Accent covers left padding + number + ": " (e.g. "1: " = kTabPadCols + digits + 1 cols).
            // The colon is included; the space after it is not.
            const int num_str_len = static_cast<int>(std::to_string(i + 1).size()); // digits
            const int accent_cols = kTabPadCols + num_str_len + 1; // pad + digits + ":"
            const float aw = static_cast<float>(accent_cols * cw_shared);
            tab_rects.push_back({ px, pill_y, pw, pill_h, aw, tl.active });
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

    // Single NanoVG callback draws everything: tab bar shapes + dividers + focus.
    const float focus_border = deps_.config ? deps_.config->focus_border_width : 3.0f;
    nanovg_pass_->set_draw_callback(
        [tab_rects = std::move(tab_rects), bar_w, bar_h,
            dividers = std::move(dividers), focus_rect, focus_border](NVGcontext* vg, int /*w*/, int /*h*/) {
            // --- Tab bar ---
            if (!tab_rects.empty())
            {
                // Bar background
                nvgBeginPath(vg);
                nvgRect(vg, 0, 0, static_cast<float>(bar_w), static_cast<float>(bar_h));
                nvgFillColor(vg, nvgRGBAf(kTabBarBg.r, kTabBarBg.g, kTabBarBg.b, kTabBarBg.a));
                nvgFill(vg);

                for (const auto& tab : tab_rects)
                {
                    const float radius = tab.h * 0.5f; // pill shape

                    // Full pill body — inactive bg for all tabs.
                    nvgBeginPath(vg);
                    nvgRoundedRect(vg, tab.x, tab.y, tab.w, tab.h, radius);
                    nvgFillColor(vg, nvgRGBAf(kInactiveTabBg.r, kInactiveTabBg.g, kInactiveTabBg.b, kInactiveTabBg.a));
                    nvgFill(vg);

                    // Active tab: burgundy accent on the number portion (left side).
                    if (tab.active)
                    {
                        nvgSave(vg);
                        // Scissor to the pill bounds so the accent respects the rounded left end.
                        nvgIntersectScissor(vg, tab.x, tab.y, tab.w, tab.h);
                        // Draw accent rect covering the number portion.
                        nvgBeginPath(vg);
                        nvgRoundedRect(vg, tab.x, tab.y, tab.accent_w, tab.h, radius);
                        nvgFillColor(vg, nvgRGBA(140, 50, 55, 200));
                        nvgFill(vg);
                        nvgRestore(vg);
                    }
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
                nvgStrokeColor(vg, nvgRGBA(120, 120, 140, 220));
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

                nvgStrokeColor(vg, nvgRGBA(140, 50, 55, 200));
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

    // Grid handle draws tab label text on top of NanoVG shapes.
    if (show_tabs && !tabs.empty())
    {
        update_tab_grid(tabs);
        if (tab_handle_)
            frame.draw_grid_handle(*tab_handle_);
    }
}

void ChromeHost::update_tab_grid(std::span<const TabLayout> tabs)
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

    if (!tab_handle_)
    {
        tab_handle_ = deps_.grid_renderer->create_grid_handle();
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

    // Write text into grid cells using the shared column-based layout.
    // Light text on burgundy accent (number portion), standard text elsewhere.
    constexpr Color kAccentFg{ 0.85f, 0.85f, 0.90f, 1.0f }; // light text on burgundy
    for (size_t ti = 0; ti < tabs.size(); ++ti)
    {
        const auto& tl = tabs[ti];
        // Number prefix length: digits of (index+1) + ":" (the space after is name territory).
        const int num_prefix_len = static_cast<int>(std::to_string(ti + 1).size()) + 1; // digits + ":"

        for (int ci = 0; ci < tl.text_len; ++ci)
        {
            const int col = tl.text_col + ci;
            if (col < 0 || col >= grid_cols)
                continue;

            // Active tab: number chars on burgundy get light fg, rest gets inactive fg.
            const Color& fg = (tl.active && ci < num_prefix_len) ? kAccentFg : kInactiveTabFg;

            const std::string cluster(1, tl.label[static_cast<size_t>(ci)]);
            AtlasRegion glyph = deps_.text_service->resolve_cluster(cluster);

            auto& cell = cells[static_cast<size_t>(col)];
            cell.fg = fg;
            cell.glyph = glyph;
        }
    }

    tab_handle_->update_cells(cells);
    flush_atlas_if_dirty();
}

void ChromeHost::flush_atlas_if_dirty()
{
    if (!deps_.text_service || !deps_.grid_renderer)
        return;
    if (!deps_.text_service->atlas_dirty())
        return;

    const auto dirty = deps_.text_service->atlas_dirty_rect();
    if (dirty.size.x <= 0 || dirty.size.y <= 0)
        return;

    constexpr size_t kPixelSize = 4;
    const size_t row_bytes = static_cast<size_t>(dirty.size.x) * kPixelSize;
    std::vector<uint8_t> scratch(row_bytes * dirty.size.y);
    const uint8_t* atlas = deps_.text_service->atlas_data();
    const int atlas_w = deps_.text_service->atlas_width();
    for (int r = 0; r < dirty.size.y; ++r)
    {
        const uint8_t* src = atlas
            + (static_cast<size_t>(dirty.pos.y + r) * atlas_w + dirty.pos.x) * kPixelSize;
        std::memcpy(scratch.data() + static_cast<size_t>(r) * row_bytes, src, row_bytes);
    }
    deps_.grid_renderer->update_atlas_region(
        dirty.pos.x, dirty.pos.y, dirty.size.x, dirty.size.y, scratch.data());
    deps_.text_service->clear_atlas_dirty();
}

} // namespace draxul
