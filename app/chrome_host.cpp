#include "chrome_host.h"

#include <algorithm>
#include <cmath>
#include <cstring>
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
    ws->name = "Tab 1";
    active_workspace_ = ws->id;
    workspaces_.push_back(std::move(ws));
    return true;
}

int ChromeHost::add_workspace(IHostCallbacks& callbacks, int pixel_w, int pixel_h)
{
    auto ws = std::make_unique<Workspace>(next_workspace_id_++, make_host_manager_deps());
    if (!ws->host_manager.create(callbacks, pixel_w, pixel_h))
    {
        last_create_error_ = ws->host_manager.error();
        return -1;
    }
    ws->initialized = true;
    ws->name = "Tab " + std::to_string(workspaces_.size() + 1);
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
    // One cell row height plus a small padding for the accent line.
    return ch + 4;
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

void ChromeHost::draw(IFrameContext& frame)
{
    if (!nanovg_pass_)
        return;

    // Draw tab bar when multiple workspaces exist.
    if (workspaces_.size() > 1)
        draw_tab_bar(frame);

    // Draw dividers and focus indicator when there are splits.
    draw_dividers_and_focus(frame);
}

// ---------------------------------------------------------------------------
// Tab bar: NanoVG shapes + grid handle text
// ---------------------------------------------------------------------------

namespace
{
constexpr Color kTabBarBg{ 0.08f, 0.08f, 0.10f, 1.0f };
constexpr Color kActiveTabBg{ 0.16f, 0.16f, 0.20f, 1.0f };
constexpr Color kInactiveTabBg{ 0.10f, 0.10f, 0.13f, 1.0f };
constexpr Color kActiveTabFg{ 0.92f, 0.92f, 0.94f, 1.0f };
constexpr Color kInactiveTabFg{ 0.50f, 0.50f, 0.55f, 1.0f };
constexpr Color kAccentColor{ 0.55f, 0.20f, 0.22f, 1.0f }; // muted burgundy
constexpr int kTabPadCols = 1; // padding cells on each side of tab label
constexpr float kTabGap = 2.0f; // pixel gap between tabs
constexpr float kTabRadius = 4.0f; // top corner radius
constexpr float kAccentHeight = 2.0f; // accent line thickness
} // namespace

void ChromeHost::draw_tab_bar(IFrameContext& frame)
{
    if (!deps_.grid_renderer || !deps_.text_service)
        return;

    const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
    if (cw <= 0 || ch <= 0)
        return;

    const int bar_h = tab_bar_height();
    const int bar_w = viewport_.pixel_size.x;

    // --- Build tab geometry for NanoVG ---
    struct TabRect
    {
        float x, y, w, h;
        bool active;
    };
    std::vector<TabRect> tabs;
    float x_cursor = kTabGap;
    for (const auto& ws : workspaces_)
    {
        const int label_cols = static_cast<int>(ws->name.size()) + kTabPadCols * 2;
        const float tab_w = static_cast<float>(label_cols * cw);
        const float tab_h = static_cast<float>(ch);
        bool is_active = (ws->id == active_workspace_);
        tabs.push_back({ x_cursor, kTabGap, tab_w, tab_h, is_active });
        x_cursor += tab_w + kTabGap;
    }

    // NanoVG draws: bar background, tab rects, accent line under active tab.
    nanovg_pass_->set_draw_callback(
        [tabs, bar_w, bar_h](NVGcontext* vg, int /*w*/, int /*h*/) {
            // Bar background
            nvgBeginPath(vg);
            nvgRect(vg, 0, 0, static_cast<float>(bar_w), static_cast<float>(bar_h));
            nvgFillColor(vg, nvgRGBAf(kTabBarBg.r, kTabBarBg.g, kTabBarBg.b, kTabBarBg.a));
            nvgFill(vg);

            for (const auto& tab : tabs)
            {
                const auto& bg = tab.active ? kActiveTabBg : kInactiveTabBg;

                // Tab body — rounded top corners.
                nvgBeginPath(vg);
                nvgRoundedRectVarying(vg, tab.x, tab.y, tab.w, tab.h,
                    kTabRadius, kTabRadius, 0.0f, 0.0f);
                nvgFillColor(vg, nvgRGBAf(bg.r, bg.g, bg.b, bg.a));
                nvgFill(vg);

                // Accent line under active tab.
                if (tab.active)
                {
                    nvgBeginPath(vg);
                    nvgRect(vg, tab.x, tab.y + tab.h, tab.w, kAccentHeight);
                    nvgFillColor(vg, nvgRGBAf(kAccentColor.r, kAccentColor.g, kAccentColor.b, kAccentColor.a));
                    nvgFill(vg);
                }
            }
        });

    RenderViewport vp;
    vp.width = bar_w;
    vp.height = bar_h;
    frame.record_render_pass(*nanovg_pass_, vp);

    // --- Grid handle draws tab label text ---
    update_tab_grid();
    if (tab_handle_)
        frame.draw_grid_handle(*tab_handle_);
}

void ChromeHost::update_tab_grid()
{
    if (!deps_.grid_renderer || !deps_.text_service)
        return;

    const auto [cw, ch] = deps_.grid_renderer->cell_size_pixels();
    if (cw <= 0 || ch <= 0)
        return;

    const int bar_w = viewport_.pixel_size.x;
    const int bar_h = tab_bar_height();
    const int grid_cols = bar_w / cw;
    const int grid_rows = bar_h / ch;

    if (grid_cols <= 0 || grid_rows <= 0)
        return;

    if (!tab_handle_)
    {
        tab_handle_ = deps_.grid_renderer->create_grid_handle();
        PaneDescriptor desc;
        desc.pixel_pos = { 0, 0 };
        desc.pixel_size = { bar_w, bar_h };
        tab_handle_->set_viewport(desc);
    }
    else
    {
        PaneDescriptor desc;
        desc.pixel_pos = { 0, 0 };
        desc.pixel_size = { bar_w, bar_h };
        tab_handle_->set_viewport(desc);
    }

    tab_handle_->set_grid_size(grid_cols, grid_rows);
    tab_handle_->set_cursor(-1, -1, CursorStyle{});

    // Build CellUpdates for tab labels.
    std::vector<CellUpdate> cells;
    const Color transparent{ 0.0f, 0.0f, 0.0f, 0.0f };

    // Fill all cells transparent so NanoVG shapes show through.
    for (int r = 0; r < grid_rows; ++r)
    {
        for (int c = 0; c < grid_cols; ++c)
        {
            CellUpdate cell{};
            cell.col = c;
            cell.row = r;
            cell.bg = transparent;
            cell.fg = transparent;
            cells.push_back(cell);
        }
    }

    // Write tab labels into row 0, positioned to match the NanoVG tab rects.
    int col_cursor = 0;
    // kTabGap is in pixels; convert to columns (round up to avoid overlap).
    const int gap_cols = std::max(1, static_cast<int>(std::ceil(kTabGap / cw)));
    col_cursor += gap_cols;

    for (const auto& ws : workspaces_)
    {
        const bool is_active = (ws->id == active_workspace_);
        const Color& fg = is_active ? kActiveTabFg : kInactiveTabFg;

        col_cursor += kTabPadCols; // left padding

        for (size_t ci = 0; ci < ws->name.size(); ++ci)
        {
            const int col = col_cursor + static_cast<int>(ci);
            if (col >= grid_cols)
                break;

            const std::string cluster(1, ws->name[ci]);
            AtlasRegion glyph = deps_.text_service->resolve_cluster(cluster);

            // Overwrite the transparent cell at this position.
            auto& cell = cells[static_cast<size_t>(col)]; // row 0
            cell.fg = fg;
            cell.glyph = glyph;
        }

        col_cursor += static_cast<int>(ws->name.size()) + kTabPadCols; // text + right pad
        col_cursor += gap_cols; // gap to next tab
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

// ---------------------------------------------------------------------------
// Dividers and focus indicator (NanoVG only)
// ---------------------------------------------------------------------------

void ChromeHost::draw_dividers_and_focus(IFrameContext& frame)
{
    const auto& tree = active_tree();
    if (tree.leaf_count() < 2)
        return;

    // Collect divider rects from the active workspace's split tree.
    struct Divider
    {
        float x, y, w, h;
        SplitDirection dir;
    };
    std::vector<Divider> dividers;
    tree.for_each_divider([&](const SplitTree::DividerRect& r) {
        dividers.push_back({ static_cast<float>(r.x), static_cast<float>(r.y),
            static_cast<float>(r.w), static_cast<float>(r.h), r.direction });
    });

    if (dividers.empty())
        return;

    // Focused pane border rect (in pixels).
    struct FocusRect
    {
        float x, y, w, h;
    };
    std::optional<FocusRect> focus_rect;
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

    nanovg_pass_->set_draw_callback(
        [dividers = std::move(dividers), focus_rect](NVGcontext* vg, int /*w*/, int /*h*/) {
            // Divider lines
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

            // Focus indicator — muted burgundy on right and bottom edges only.
            if (focus_rect)
            {
                constexpr float border = 2.0f;
                constexpr float half = border * 0.5f;
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

                // Right edge
                nvgBeginPath(vg);
                nvgMoveTo(vg, right, focus_rect->y);
                nvgLineTo(vg, right, bottom);
                nvgStroke(vg);

                // Bottom edge
                nvgBeginPath(vg);
                nvgMoveTo(vg, focus_rect->x, bottom);
                nvgLineTo(vg, right, bottom);
                nvgStroke(vg);
            }
        });

    RenderViewport vp;
    vp.width = viewport_.pixel_size.x;
    vp.height = viewport_.pixel_size.y;
    frame.record_render_pass(*nanovg_pass_, vp);
}

} // namespace draxul
